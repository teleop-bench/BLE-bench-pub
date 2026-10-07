/*
 * Chip-EXTERNAL safety watchdog demonstrator (§14.9 rung 3) — nRF52-DK.
 *
 * Watches the peripheral's valid-heartbeat evidence (HBRX mirror line, one
 * toggle per valid heartbeat) from SEPARATE silicon with its own clock,
 * power, and hardware WDT. If edges stop for WDT_TIMEOUT_MS, this chip's
 * hardware watchdog fires: pre-reset ISR toggles the expiry marker and
 * asserts an INDEPENDENT STOP output, then the chip resets into the
 * latched-STOP boot state. Covers the failure class the on-chip nRF54
 * watchdog cannot: the whole radio SoC dying with its pins frozen.
 *
 * DEMONSTRATOR SIMPLIFICATION (documented, deliberate): re-arm is AUTOMATIC
 * after N spaced edges + a dwell (no button). The manual §14.9 re-arm gate
 * lives on the nRF54 watchdog; system STOP is the AND of both lines being
 * high (either asserting low = stop), so auto-re-arm here cannot resume
 * motion on its own. Production would use a dumb watchdog IC, not an MCU.
 *
 * Fail-safe inventory: edges stop -> HW WDT trips (this code not consulted);
 * this chip unpowered/reset -> pin hi-Z -> external pull-down asserts STOP
 * (production part; bench uses the cross-board stand-in when testing that).
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/hwinfo.h>

#define WDT_TIMEOUT_MS     200
#define ARM_EDGES          10
#define ARM_MIN_SPACING_MS 10
#define ARM_MIN_DWELL_MS   180

#define ZUSER DT_PATH(zephyr_user)
static const struct gpio_dt_spec feed_gpio = GPIO_DT_SPEC_GET(ZUSER, feed_gpios);
static const struct gpio_dt_spec stop_gpio = GPIO_DT_SPEC_GET(ZUSER, stop_gpios);
static const struct gpio_dt_spec exp_gpio  = GPIO_DT_SPEC_GET(ZUSER, extexp_gpios);
static const struct gpio_dt_spec led      = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct device *const wdt_dev = DEVICE_DT_GET(DT_ALIAS(watchdog0));

enum { ST_ARMING, ST_RUNNING };            /* boots ARMING with STOP asserted */
static volatile int state = ST_ARMING;
static volatile uint32_t edges, arm_count, feeds;
static int64_t arming_since_ms, last_counted_ms, last_edge_ms;
static int wdt_chan = -1;
static bool wdt_live, booted_from_wdt;

/* Pre-reset window: marker first, then STOP. The chip resets moments later
 * and boots back into ARMING with STOP asserted. */
static void wdt_expiry_cb(const struct device *dev, int chan)
{
	ARG_UNUSED(dev); ARG_UNUSED(chan);
	gpio_pin_toggle_dt(&exp_gpio);
	(void)gpio_pin_set_dt(&stop_gpio, 1);   /* asserted = line low */
	(void)gpio_pin_set_dt(&led, 1);
}

/* One toggle per valid heartbeat, both edges. Single ISR context — the only
 * writer of the state machine. */
static struct gpio_callback feed_cb_data;
static void feed_edge(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev); ARG_UNUSED(cb); ARG_UNUSED(pins);
	int64_t now = k_uptime_get();
	edges++;

	if (state == ST_RUNNING) {
		(void)wdt_feed(wdt_dev, wdt_chan);
		feeds++;
		last_edge_ms = now;
		return;
	}

	/* ARMING: require N edges spaced >= ARM_MIN_SPACING_MS plus a dwell —
	 * a queued burst must not demonstrate liveness. A silence gap longer
	 * than the deadline restarts the count. */
	if (now - last_edge_ms > WDT_TIMEOUT_MS) {
		arm_count = 0;
		arming_since_ms = now;
	}
	last_edge_ms = now;
	if (arm_count == 0 || now - last_counted_ms >= ARM_MIN_SPACING_MS) {
		if (arm_count == 0) { arming_since_ms = now; }
		arm_count++;
		last_counted_ms = now;
	}
	if (arm_count >= ARM_EDGES && now - arming_since_ms >= ARM_MIN_DWELL_MS) {
		if (!wdt_live) {
			if (wdt_setup(wdt_dev, WDT_OPT_PAUSE_HALTED_BY_DBG) != 0) {
				return;   /* stay ARMING, STOP stays asserted */
			}
			wdt_live = true;
		}
		(void)wdt_feed(wdt_dev, wdt_chan);
		(void)gpio_pin_set_dt(&stop_gpio, 0);   /* drive RUN (line high) */
		(void)gpio_pin_set_dt(&led, 0);
		state = ST_RUNNING;
	}
}

int main(void)
{
	uint32_t cause = 0;
	if (hwinfo_get_reset_cause(&cause) == 0) {
		booted_from_wdt = (cause & RESET_WATCHDOG) != 0;
		(void)hwinfo_clear_reset_cause();
	}

	int rc = 0;
	if (!gpio_is_ready_dt(&feed_gpio) || !gpio_is_ready_dt(&stop_gpio) ||
	    !gpio_is_ready_dt(&exp_gpio) || !gpio_is_ready_dt(&led) ||
	    !device_is_ready(wdt_dev)) { rc = -ENODEV; }
	if (!rc) { rc = gpio_pin_configure_dt(&stop_gpio, GPIO_OUTPUT_ACTIVE); }  /* STOP first */
	if (!rc) { rc = gpio_pin_configure_dt(&exp_gpio, GPIO_OUTPUT_INACTIVE); }
	if (!rc) { rc = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE); }
	if (!rc) { rc = gpio_pin_configure_dt(&feed_gpio, GPIO_INPUT); }
	if (!rc) { rc = gpio_pin_interrupt_configure_dt(&feed_gpio, GPIO_INT_EDGE_BOTH); }
	if (!rc) {
		const struct wdt_timeout_cfg cfg = {
			.window = { .min = 0, .max = WDT_TIMEOUT_MS },
			.callback = wdt_expiry_cb,
			.flags = WDT_FLAG_RESET_SOC,
		};
		wdt_chan = wdt_install_timeout(wdt_dev, &cfg);
		if (wdt_chan < 0) { rc = wdt_chan; }
	}
	if (!rc) {
		gpio_init_callback(&feed_cb_data, feed_edge, BIT(feed_gpio.pin));
		rc = gpio_add_callback(feed_gpio.port, &feed_cb_data);
	}
	if (rc) {
		printk("EXTWDT: bring-up FAILED (%d) — STOP latched, watching nothing\n", rc);
		return 0;   /* STOP stays asserted (or hi-Z + pull in production) */
	}
	printk("EXTWDT: external watchdog up — deadline %d ms on nRF52 wdt0, "
	       "arm N=%d spacing>=%d ms dwell>=%d ms (AUTO re-arm, demonstrator)%s\n",
	       WDT_TIMEOUT_MS, ARM_EDGES, ARM_MIN_SPACING_MS, ARM_MIN_DWELL_MS,
	       booted_from_wdt ? " [boot follows HW-WDT reset — prior STOP trip]" : "");

	uint32_t sec = 0, prev_edges = 0;
	while (1) {
		k_msleep(1000);
		sec++;
		uint32_t e = edges;
		printk("X t=%us ext=%s%s edges=%u(+%u/s) feeds=%u arm_count=%u\n",
		       sec, state == ST_RUNNING ? "RUNNING" : "ARMING",
		       booted_from_wdt ? "(posttrip)" : "", e, e - prev_edges,
		       feeds, arm_count);
		prev_edges = e;
	}
	return 0;
}
