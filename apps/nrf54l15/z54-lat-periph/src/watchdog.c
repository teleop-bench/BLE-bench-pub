/*
 * Actuator-side safety watchdog — DRAFT bench implementation (§14.9), rev 3.
 *
 * State machine:  STOPPED (STOP asserted, LATCHED)
 *                   --re-arm request (physical button sw0)--> ARMING
 *                 ARMING: requires CONFIG_APP_WDT_ARM_HEARTBEATS consecutive VALID
 *                   heartbeats, each >= CONFIG_APP_WDT_ARM_MIN_SPACING_MS after the
 *                   previous counted one, AND a minimum dwell of
 *                   CONFIG_APP_WDT_ARM_MIN_DURATION_MS in ARMING (anti-burst: ten
 *                   queued frames delivered back-to-back must NOT arm)
 *                   --> RUNNING (STOP de-asserted)
 *                 RUNNING: each valid heartbeat restarts the deadline timer;
 *                   expiry --> STOPPED (latched again).
 *
 * CONCURRENCY (rev-2 fix): every state transition and safety-GPIO change happens
 * under one k_spinlock shared with the expiry ISR, so expiry always wins: if the
 * deadline fires while an ARMING->RUNNING transition is in flight, the transition
 * re-observes STOPPED under the lock and refuses to de-assert STOP.
 *
 * Validity of a heartbeat: dedicated GATT handle only (main.c routes just that
 * characteristic here, offset==0 enforced), exact 8-byte {epoch u32, seq u32} LE,
 * epoch equal to the epoch captured at ARMING entry, and seq FRESH by
 * serial-number arithmetic ((int32_t)(seq - last) > 0, handles 32-bit wrap).
 * A central reboot NORMALLY produces a new random epoch (32-bit, probabilistic —
 * not a guarantee) -> its heartbeats are invalid in ARMING/RUNNING -> deadline
 * expiry -> STOP; only the physical button restarts ARMING. The seq/epoch checks
 * reject ACCIDENTAL duplicates/reordering only — NOT an active attacker;
 * production needs authenticated heartbeats bound to a fresh session nonce.
 *
 * DEADLINE (rev 4): the 200 ms deadline runs on the ON-CHIP HARDWARE WATCHDOG
 * (wdt31, 32.768 kHz LFCLK, fed per valid heartbeat) — no RTOS timer in the
 * expiry path. Expiry: the WDT pre-reset interrupt toggles the wdtexp marker
 * and drives STOP, then the peripheral RESETS THE CHIP (~2 LFCLK later) into
 * the latched-STOP boot state. A hung RTOS/app stops feeding and trips anyway.
 * During the reset itself the pin is hi-Z — the EXTERNAL pull-down carries
 * STOP across the gap (that is what it is for).
 *
 * WHAT IS NOT FINAL-SAFETY-PATH IN THIS DRAFT (explicit, per review):
 *  - The hardware WDT shares the SoC with the application: a whole-chip
 *    failure (clock/power/latch-up) defeats it. Chip-independent coverage
 *    needs an EXTERNAL watchdog part in the actuator circuit.
 *  - STOP is a bench GPIO + LED mirror; electrical fail-safety depends on the
 *    EXTERNAL pull (reset/unpowered/hi-Z must read as STOP) and the real
 *    actuator circuit. An LED demo is not electrical validation.
 *  - Overlay pins are BENCH PLACEHOLDERS pending the §14.9 hardware inputs.
 *
 * Logic-analyzer channels (rev-2 fix — clean interval semantics):
 *  - hbrx:   toggles once per VALID heartbeat (edge = event)
 *  - wdtexp: toggles at expiry-callback ENTRY, BEFORE the STOP write, and is
 *            touched nowhere else -> an independent expiry marker (the old
 *            level-mirror produced an apparent negative expiry->STOP delay)
 *  - stop:   level (asserted low externally-pulled)
 */
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/hwinfo.h>
#include <stdio.h>
#include "watchdog.h"

#define ZUSER DT_PATH(zephyr_user)

static const struct gpio_dt_spec stop_gpio = GPIO_DT_SPEC_GET(ZUSER, stop_gpios);
static const struct gpio_dt_spec hbrx_gpio = GPIO_DT_SPEC_GET(ZUSER, hbrx_gpios);
static const struct gpio_dt_spec wdtexp_gpio = GPIO_DT_SPEC_GET(ZUSER, wdtexp_gpios);
static const struct gpio_dt_spec hbrx2_gpio = GPIO_DT_SPEC_GET(ZUSER, hbrx2_gpios);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec rearm_btn = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

enum wdt_state { WDT_STOPPED, WDT_ARMING, WDT_RUNNING };
static enum wdt_state state = WDT_STOPPED;   /* guarded by `lock` */
static struct k_spinlock lock;               /* serializes state + safety GPIOs vs expiry ISR */
static bool hw_fault;                        /* GPIO fault (bring-up or runtime): never RUNNING */
static bool epoch_locked;                    /* epoch capture enabled ONLY by the button:
                                              * cleared in rearm_pressed(), set by the first
                                              * heartbeat after it. Invalid frames may reset the
                                              * consecutive count but NEVER clear/replace the
                                              * epoch — otherwise a second sender could arm
                                              * without a new physical re-arm (rev-3 fix). */
static uint32_t epoch, last_seq;
static volatile uint32_t gpio_runtime_fail;  /* runtime safety-GPIO write failures */
static uint32_t arm_count;
static int64_t arming_since_ms, last_counted_hb_ms;
static volatile uint32_t hb_valid, hb_invalid, stops, rearms;
static int64_t last_valid_hb_ms, stop_at_ms;

/* Must be called with `lock` held. SILENT (no printk in the critical section —
 * callers log after releasing the lock); the STOP write is checked: a runtime
 * GPIO failure latches hw_fault and falls back to releasing the pin to hi-Z so
 * the EXTERNAL pull-down asserts STOP electrically. */
static void stop_assert_locked(void)
{
	int rc = gpio_pin_set_dt(&stop_gpio, 1);   /* active (= line low = STOP) */
	if (rc) {
		gpio_runtime_fail++;
		hw_fault = true;
		/* ATTEMPTS hi-Z fallback so the external pull-down can assert STOP
		 * electrically — also checked, but if both writes fail nothing more
		 * can be done from software: the analyzer's PHYSICAL STOP trace is
		 * authoritative, never this code path's bookkeeping. */
		if (gpio_pin_configure_dt(&stop_gpio, GPIO_DISCONNECTED)) {
			gpio_runtime_fail++;
		}
	}
	(void)gpio_pin_set_dt(&led, 1);
	state = WDT_STOPPED;
	stops++;
	stop_at_ms = k_uptime_get();
}

/* Hardware watchdog (wdt31 = alias watchdog0). Channel installed at early
 * init; the countdown STARTS only at ARMING entry (starting it at boot would
 * reset-loop a system whose designed boot state is latched-STOP with no
 * heartbeats). Once set up, the nRF WDT cannot be stopped by software — it
 * either gets fed or it resets the chip. */
static const struct device *const hw_wdt_dev = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static int hw_wdt_chan = -1;
static bool hw_wdt_live;                     /* countdown running (guarded by `lock`) */

/* Pre-reset window (~2 LFCLK ≈ 61 µs on nRF): marker edge first, then STOP.
 * No lock, no printk — the chip resets moments after this returns, and
 * K_SPINLOCK sections mask this IRQ, so a mid-transition RUN write is always
 * older than this STOP write (last writer wins = STOP). Boot then re-enters
 * latched-STOP; the external pull-down owns the line during reset itself. */
static void hw_wdt_expiry_cb(const struct device *dev, int chan)
{
	ARG_UNUSED(dev); ARG_UNUSED(chan);
	gpio_pin_toggle_dt(&wdtexp_gpio);          /* LA channel 4: expiry-entry marker */
	(void)gpio_pin_set_dt(&stop_gpio, 1);      /* active (= line low = STOP) */
	(void)gpio_pin_set_dt(&led, 1);
}

/* Must be called with `lock` held. Starts the countdown on first use (ARMING
 * entry), feeds it afterwards. Failure latches hw_fault + STOP. */
static void deadline_kick_locked(void)
{
	if (!hw_wdt_live) {
		if (wdt_setup(hw_wdt_dev, WDT_OPT_PAUSE_HALTED_BY_DBG) == 0) {
			hw_wdt_live = true;
		} else {
			gpio_runtime_fail++;
			stop_assert_locked();
			hw_fault = true;
		}
	} else {
		(void)wdt_feed(hw_wdt_dev, hw_wdt_chan);
	}
}

void safety_wdt_heartbeat(const void *buf, uint16_t len)
{
	if (len != 8) { hb_invalid++; return; }
	uint32_t e = sys_get_le32(buf);
	uint32_t q = sys_get_le32((const uint8_t *)buf + 4);
	int64_t now = k_uptime_get();
	bool became_running = false;

	K_SPINLOCK(&lock) {
		if (state == WDT_STOPPED || hw_fault) { hb_invalid++; K_SPINLOCK_BREAK; }

		if (state == WDT_ARMING && !epoch_locked) {
			/* First structurally-valid HB after the BUTTON locks the epoch.
			 * epoch_locked is cleared only in rearm_pressed() — an invalid
			 * frame later resets the count but can never re-open capture. */
			epoch = e; last_seq = q;
			epoch_locked = true;
			arm_count = 1;
			last_counted_hb_ms = now;
		} else if (epoch_locked && e == epoch && (int32_t)(q - last_seq) > 0) {
			last_seq = q;
			if (state == WDT_ARMING) {
				/* Anti-burst: count only appropriately-SPACED heartbeats. */
				if (now - last_counted_hb_ms >= CONFIG_APP_WDT_ARM_MIN_SPACING_MS) {
					arm_count++;
					last_counted_hb_ms = now;
				}
			}
		} else {
			hb_invalid++;
			if (state == WDT_ARMING) { arm_count = 0; }  /* consecutive broken */
			K_SPINLOCK_BREAK;
		}

		hb_valid++;
		last_valid_hb_ms = now;
		gpio_pin_toggle_dt(&hbrx_gpio);   /* LA: one edge per valid HB */
		gpio_pin_toggle_dt(&hbrx2_gpio);  /* same evidence for the EXTERNAL watchdog */
		deadline_kick_locked();           /* feed the HARDWARE watchdog */

		/* Arm only after N spaced heartbeats AND a minimum ARMING dwell —
		 * evaluated and applied UNDER THE LOCK, so an expiry that latched
		 * STOPPED first makes this branch unreachable (expiry wins). */
		if (state == WDT_ARMING &&
		    arm_count >= CONFIG_APP_WDT_ARM_HEARTBEATS &&
		    now - arming_since_ms >= CONFIG_APP_WDT_ARM_MIN_DURATION_MS) {
			int rc = gpio_pin_set_dt(&stop_gpio, 0);   /* drive RUN (line high) */
			if (rc) {
				/* Cannot verify RUN drive: stay safe. */
				gpio_runtime_fail++;
				stop_assert_locked();
				hw_fault = true;
			} else {
				state = WDT_RUNNING;
				(void)gpio_pin_set_dt(&led, 0);
				became_running = true;
			}
		}
	}
	if (became_running) {
		printk("WDT: RUNNING (armed: %u spaced HBs over %lld ms, epoch=%08x)\n",
		       arm_count, k_uptime_get() - arming_since_ms, epoch);
	}
}

static struct gpio_callback btn_cb;
static void rearm_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev); ARG_UNUSED(cb); ARG_UNUSED(pins);
	bool entered = false;

	K_SPINLOCK(&lock) {
		if (state != WDT_STOPPED || hw_fault) { K_SPINLOCK_BREAK; }
		/* Physical re-arm request (bench stand-in for the production physical /
		 * authenticated re-arm). Enters ARMING only. The ONLY place epoch
		 * capture is re-enabled (rev-3). */
		rearms++;
		epoch_locked = false;
		arm_count = 0;
		arming_since_ms = k_uptime_get();
		state = WDT_ARMING;
		deadline_kick_locked();   /* start (first arm) or feed the HW countdown */
		entered = true;
	}
	if (entered) { printk("WDT: ARMING (button re-arm request #%u)\n", rearms); }
}

static bool booted_from_wdt;   /* prior trip evidence (reset-cause register) */

int safety_wdt_early_init(void)
{
	int rc = 0;

	/* Reset-cause evidence: a hardware-WDT trip ends in a chip reset, so the
	 * proof a trip happened survives here (read + clear so it is per-boot). */
	uint32_t cause = 0;
	if (hwinfo_get_reset_cause(&cause) == 0) {
		booted_from_wdt = (cause & RESET_WATCHDOG) != 0;
		(void)hwinfo_clear_reset_cause();
	}

	/* Called BEFORE bt_enable(). Any failure here must prevent Bluetooth
	 * startup and make RUNNING unreachable (hw_fault latch). */
	if (!gpio_is_ready_dt(&stop_gpio) || !gpio_is_ready_dt(&wdtexp_gpio) ||
	    !gpio_is_ready_dt(&hbrx_gpio) || !gpio_is_ready_dt(&led) ||
	    !gpio_is_ready_dt(&rearm_btn) || !device_is_ready(hw_wdt_dev)) {
		rc = -ENODEV;
	}
	if (!rc) {
		/* Install the deadline channel now (setup happens at first ARMING).
		 * WDT_FLAG_RESET_SOC: expiry = pre-reset callback + chip reset into
		 * the latched-STOP boot state. */
		const struct wdt_timeout_cfg cfg = {
			.window = { .min = 0, .max = CONFIG_APP_WDT_TIMEOUT_MS },
			.callback = hw_wdt_expiry_cb,
			.flags = WDT_FLAG_RESET_SOC,
		};
		hw_wdt_chan = wdt_install_timeout(hw_wdt_dev, &cfg);
		if (hw_wdt_chan < 0) { rc = hw_wdt_chan; }
	}
	if (!rc) { rc = gpio_pin_configure_dt(&stop_gpio, GPIO_OUTPUT_ACTIVE); }  /* STOP */
	if (!rc) { rc = gpio_pin_configure_dt(&wdtexp_gpio, GPIO_OUTPUT_INACTIVE); }
	if (!rc) { rc = gpio_pin_configure_dt(&hbrx_gpio, GPIO_OUTPUT_INACTIVE); }
	if (!rc) { rc = gpio_pin_configure_dt(&hbrx2_gpio, GPIO_OUTPUT_INACTIVE); }
	if (!rc) { rc = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE); }
	if (!rc) { rc = gpio_pin_configure_dt(&rearm_btn, GPIO_INPUT); }
	if (!rc) { rc = gpio_pin_interrupt_configure_dt(&rearm_btn, GPIO_INT_EDGE_TO_ACTIVE); }
	if (!rc) {
		gpio_init_callback(&btn_cb, rearm_pressed, BIT(rearm_btn.pin));
		rc = gpio_add_callback(rearm_btn.port, &btn_cb);
	}

	state = WDT_STOPPED;
	stops = 0;   /* boot-assert is the initial condition, not a trip event */
	if (rc) {
		hw_fault = true;
		printk("WDT: GPIO bring-up FAILED (%d) — STOP latched, BT must not start\n", rc);
		return rc;
	}
	printk("WDT: early init — STOP asserted before BT init "
	       "(HARDWARE deadline %d ms on wdt31, arm N=%d spacing>=%d ms dwell>=%d ms)%s\n",
	       CONFIG_APP_WDT_TIMEOUT_MS, CONFIG_APP_WDT_ARM_HEARTBEATS,
	       CONFIG_APP_WDT_ARM_MIN_SPACING_MS, CONFIG_APP_WDT_ARM_MIN_DURATION_MS,
	       booted_from_wdt ? " [boot follows HW-WDT reset — prior STOP trip]" : "");
	return 0;
}

void safety_wdt_status(char *out, size_t n)
{
	static const char *names[] = {"STOPPED", "ARMING", "RUNNING"};
	snprintf(out, n, "wdt=%s%s%s%s hb_ok=%u hb_bad=%u stops=%u rearms=%u gpiofail=%u",
		 names[state], hw_wdt_live ? "(hw)" : "",
		 booted_from_wdt ? "(posttrip)" : "", hw_fault ? "(HWFAULT)" : "",
		 hb_valid, hb_invalid, stops, rearms, gpio_runtime_fail);
}
