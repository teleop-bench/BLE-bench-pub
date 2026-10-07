/*
 * nRF54L15-DK — RTT ping-pong PERIPHERAL (Phase 0 baseline / echo).
 * GATT char (write-without-response + notify). On each ping (write) it echoes the
 * payload straight back as a notification (pong). App-level echo → RTT is bounded by
 * the connection interval (~7.5 ms). Phase 2 will move this echo into the controller.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

static struct bt_uuid_128 svc_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12340010, 0x1234, 0x1000, 0x8000, 0x00805f9b34fb));
static struct bt_uuid_128 chrc_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12340011, 0x1234, 0x1000, 0x8000, 0x00805f9b34fb));
#if defined(CONFIG_APP_SAFETY_WATCHDOG)
#include "watchdog.h"
/* Dedicated heartbeat characteristic (§14.9): the ONLY writes that can feed the
 * safety watchdog. The ping-pong char above never touches it. */
static struct bt_uuid_128 hb_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12340012, 0x1234, 0x1000, 0x8000, 0x00805f9b34fb));
#endif

static volatile bool notify_enabled;
static volatile uint32_t pongs;
static volatile uint64_t rx_bytes;   /* throughput cells: bytes received on the ping char */
static volatile uint64_t blk_bytes;  /* load-ramp cells: bytes received on the bulk-sink char */
/* Dedicated bulk-sink characteristic (load-ramp campaign): concurrent
 * background load lands here — counted, never echoed — so the ping char's
 * RTT stays clean. */
static struct bt_uuid_128 blk_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12340013, 0x1234, 0x1000, 0x8000, 0x00805f9b34fb));
static ssize_t blk_write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			    const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn); ARG_UNUSED(attr); ARG_UNUSED(buf);
	ARG_UNUSED(offset); ARG_UNUSED(flags);
	blk_bytes += len;
	return len;
}
/* DIAGNOSTIC (assert-disabled fault-injection soak): peripheral-side cancel counter +
 * disconnect telemetry. `lll_conn_trx_busy_cancels` is the controller counter (lll_conn.c). */
#if defined(CONFIG_BT_LL_SW_SPLIT)
extern uint32_t volatile lll_conn_trx_busy_cancels;
#else
static uint32_t volatile lll_conn_trx_busy_cancels; /* SDC/other controllers: patched
                                                     * open-LL diagnostic unavailable;
                                                     * reads as 0 */
#endif
#if defined(CONFIG_BT_CTLR_INEVENT_ECHO)
extern void lll_conn_echo_stats(uint32_t *rx, uint32_t *tx, uint32_t *pending, uint32_t *stale);
#endif
#if defined(CONFIG_BT_CTLR_TIFS_CAPTURE_BENCH)
extern uint32_t bt_ctlr_tifs_drain_fmt(char *buf, uint32_t buflen);
extern void bt_ctlr_tifs_clear(void);
extern void bt_ctlr_tifs_freeze(void);
#endif
static volatile uint32_t disc_count;
static volatile uint8_t  last_disc_reason;
static volatile int64_t  conn_ms;         /* uptime at CCC-enable (connection active) */
static volatile uint32_t pongs_at_conn;   /* pong count at CCC-enable */

static void ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	if (notify_enabled) { conn_ms = k_uptime_get(); pongs_at_conn = pongs; }
	printk("CCC: notify %s\n", notify_enabled ? "ENABLED" : "off");
}

/* ping arrives -> echo it right back as a notification (pong).
 * Phase 1 (host echo): RTT bounded by the connection interval (~1.9 ms @ 1 ms int).
 * Phase 2 (CONFIG_BT_CTLR_INEVENT_ECHO): the controller echoes in the SAME event
 * and the host echo below must be suppressed to avoid a double-pong. */
static ssize_t write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(offset); ARG_UNUSED(flags);
#if defined(CONFIG_APP_TPUT_SINK)
	/* Throughput cells: SINK ONLY. The default echo doubles the air load
	 * (each byte flies twice) — Phase-A finding: that halved apparent
	 * throughput and masked the FSU delta. */
	ARG_UNUSED(conn); ARG_UNUSED(attr);
#elif !defined(CONFIG_BT_CTLR_INEVENT_ECHO)
	if (notify_enabled) {
		bt_gatt_notify(conn, attr, buf, len);   /* echo == pong */
	}
#else
	ARG_UNUSED(conn); ARG_UNUSED(attr); ARG_UNUSED(buf);
#endif
	pongs++;
	rx_bytes += len;
	return len;
}

#if defined(CONFIG_APP_SAFETY_WATCHDOG)
static ssize_t hb_write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn); ARG_UNUSED(attr);
	/* Full write-envelope validation: no partial/prepared writes may feed the
	 * watchdog; length is validated inside (exact 8). */
	if (offset != 0) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET); }
	if (flags & BT_GATT_WRITE_FLAG_PREPARE) { return BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED); }
	safety_wdt_heartbeat(buf, len);   /* epoch/seq/length validity checked inside */
	return len;
}
#endif

BT_GATT_SERVICE_DEFINE(pp_svc,
	BT_GATT_PRIMARY_SERVICE(&svc_uuid),
	BT_GATT_CHARACTERISTIC(&chrc_uuid.uuid,
			       BT_GATT_CHRC_WRITE_WITHOUT_RESP | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_WRITE, NULL, write_cb, NULL),
	BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(&blk_uuid.uuid,
			       BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE, NULL, blk_write_cb, NULL),
#if defined(CONFIG_APP_SAFETY_WATCHDOG)
	BT_GATT_CHARACTERISTIC(&hb_uuid.uuid,
			       BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE, NULL, hb_write_cb, NULL),
#endif
);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* Advertising (re)start runs in a work item, not directly in BT callbacks: the extended_adv
 * sample defers it out of recycled() to avoid potential deadlock, and a work item lets us
 * retry with backoff on transient failure (e.g. -ENOMEM if the conn object isn't free yet). */
#if defined(CONFIG_APP_ADV_FAIL_INJECT)
static bool post_recycled;      /* set once the first recycled() fires (not initial boot) */
static bool fail_injected;      /* inject exactly once */
#endif

static void adv_work_fn(struct k_work *w)
{
#if defined(CONFIG_APP_ADV_FAIL_INJECT)
	/* Test-only: simulate one -ENOMEM on the first post-recycled invocation, then let the
	 * normal retry path call the real API 100 ms later. No BT API interception. */
	if (post_recycled && !fail_injected) {
		fail_injected = true;
		printk("synthetic_fail=1 retry_scheduled=1 (injected -ENOMEM, test-only)\n");
		k_work_schedule(k_work_delayable_from_work(w), K_MSEC(100));
		return;
	}
#endif
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err == 0 || err == -EALREADY) {
		printk("advertising\n");
	} else {
		printk("adv start failed %d — retrying in 100ms\n", err);
		k_work_schedule(k_work_delayable_from_work(w), K_MSEC(100));
	}
}
static K_WORK_DELAYABLE_DEFINE(adv_work, adv_work_fn);

static void start_adv(void)
{
	k_work_schedule(&adv_work, K_NO_WAIT);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) { printk("connect failed 0x%02x\n", err); return; }
	struct bt_conn_info info;
	if (bt_conn_get_info(conn, &info) == 0) { printk("GAP connected interval=%u\n", info.le.interval_us); }
	/* no DLE needed — RTT payloads are tiny (8 B) */
}
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	int64_t life = conn_ms ? (k_uptime_get() - conn_ms) : -1;
	printk("GAP disconnected 0x%02x  lifetime=%lldms pongs_in_conn=%u\n",
	       reason, life, pongs - pongs_at_conn);
	disc_count++; last_disc_reason = reason;
	notify_enabled = false;
	/* Do NOT restart advertising here: with BT_MAX_CONN=1 the conn object is still held at
	 * this point, so a connectable adv start races it and fails -ENOMEM ("adv start failed
	 * -12", the Track-1 self-recovery bug). The recycled() callback below fires once the conn
	 * object is actually freed — restart there (recommended by the bt_conn_cb docs). */
}

static void recycled(void)
{
#if defined(CONFIG_APP_ADV_FAIL_INJECT)
	post_recycled = true;   /* arm the one-shot synthetic failure (not at initial boot) */
#endif
	start_adv();   /* conn object released -> a connectable advertiser can be created again */
}

#if defined(CONFIG_APP_SAFETY_WATCHDOG)
/* Cross-board fail-safe monitor: input + internal pull-down, jumpered to the
 * CENTRAL's ALIVE line for the power-loss->ALIVE-falls check (bench stand-in
 * for the discrete external pull-down; this board survives that test).
 * Diagnostic only. */
static const struct gpio_dt_spec mon_gpio = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), mon_gpios);
static bool mon_ok;
#endif

/* Periodic liveness + fault-injection telemetry (peripheral role). */
static void periph_report_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	uint32_t sec = 0, prev_pongs = 0, prev_cancels = 0;
#if defined(CONFIG_APP_SAFETY_WATCHDOG)
	mon_ok = gpio_is_ready_dt(&mon_gpio) &&
		 gpio_pin_configure_dt(&mon_gpio, GPIO_INPUT) == 0;
	if (!mon_ok) { printk("mon pin bring-up failed (diagnostic only)\n"); }
#endif
	while (1) {
		k_msleep(1000);
		sec++;
		uint32_t p = pongs, cancels = lll_conn_trx_busy_cancels;
		static uint64_t prev_bytes, prev_blk;
		uint64_t rb = rx_bytes, bb = blk_bytes;
		uint32_t kbps = (uint32_t)((rb - prev_bytes) / 1024U);
		uint32_t blkkbps = (uint32_t)((bb - prev_blk) / 1024U);
		prev_bytes = rb; prev_blk = bb;
		uint32_t erx = 0, etx = 0, epend = 0, estale = 0;
#if defined(CONFIG_BT_CTLR_INEVENT_ECHO)
		lll_conn_echo_stats(&erx, &etx, &epend, &estale);
#endif
		char wdt[96] = "";
#if defined(CONFIG_APP_SAFETY_WATCHDOG)
		char tmp[72];
		safety_wdt_status(tmp, sizeof(tmp));
		snprintf(wdt, sizeof(wdt), " | %s mon=%d", tmp,
			 mon_ok ? gpio_pin_get_dt(&mon_gpio) : -1);
#endif
		printk("P t=%us rxkBps=%u blkkBps=%u pongs=%u(+%u/s) cancels=%u(+%u/s) notify=%d disc=%u(0x%02x) "
		       "| echo rx=%u tx=%u pend=%u stale=%u%s\n",
		       sec, kbps, blkkbps, p, p - prev_pongs, cancels, cancels - prev_cancels,
		       notify_enabled, disc_count, last_disc_reason, erx, etx, epend, estale, wdt);
		prev_pongs = p; prev_cancels = cancels;
#if defined(CONFIG_BT_CTLR_TIFS_CAPTURE_BENCH)
		/* §6.1 rev 3: clear bins ONCE after the post-FSU settle so only
		 * post-settle transitions aggregate (CONFIG-timed; FSU fires
		 * ~+12 s, +2 s settle). Then print the snapshotted per-tifs
		 * stats each second (cumulative from the clear). */
		if (sec == CONFIG_APP_TIFS_CLEAR_S) {
			bt_ctlr_tifs_clear();          /* start post-settle capture */
			printk("TIFS cleared at t=%us (post-settle)\n", sec);
		}
		if (sec == CONFIG_APP_TIFS_END_S) {
			bt_ctlr_tifs_freeze();         /* freeze: bins now static */
			printk("TIFS frozen at t=%us\n", sec);
		}
		if (sec >= CONFIG_APP_TIFS_END_S) {
			/* frozen -> reading the bins is lock-free and copy-free */
			static char tb[600];
			uint32_t n = bt_ctlr_tifs_drain_fmt(tb, sizeof(tb));
			if (n) {
				printk("%s", tb);
			}
		}
#endif
	}
}
K_THREAD_DEFINE(periph_report_tid, 1024, periph_report_fn, NULL, NULL, NULL, 7, 0, 0);
static void le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *p)
{ ARG_UNUSED(conn); printk("PHY tx=%u rx=%u\n", p->tx_phy, p->rx_phy); }

BT_CONN_CB_DEFINE(conn_cbs) = {
	.connected = connected, .disconnected = disconnected, .recycled = recycled,
	.le_phy_updated = le_phy_updated,
};

int main(void)
{
#if defined(CONFIG_APP_SAFETY_WATCHDOG)
	if (safety_wdt_early_init()) {   /* STOP asserted BEFORE Bluetooth init (§14.9) */
		printk("safety GPIO bring-up failed — refusing to start Bluetooth\n");
		return 0;                /* STOP stays latched; no radio */
	}
#endif
	int err = bt_enable(NULL);
	if (err) { printk("bt_enable %d\n", err); return 0; }
	printk("\n=== nRF54L15 RTT ping-pong PERIPHERAL (echo) ===\n");
	start_adv();
	return 0;
}
