/*
 * nRF54L15-DK — RTT ping-pong CENTRAL (Phase 0 baseline driver).
 * Connects (link runs at 2M by default — confirmed by forcing 1M, which changed it; the
 * exact trigger is controller-level, host auto-PHY is off). An explicit PHY update (1M OR 2M)
 * CRASHES the link at the 1ms/52us operating point, so we issue none (see try_2m/try_1m).
 * Exchanges MTU, discovers the ping-pong char, subscribes to notify, drops to 1ms, then serialized
 * ping->pong: timestamp a write, wait for the notify (pong), measure the round trip
 * locally. Reports min/mean/max app API->callback RTT (us) every second.
 * NOTE: RTT is application API-to-callback, not a direct on-air measurement; it includes
 * host/controller scheduling on both ends. The 1ms interval + 52us tIFS are non-spec
 * (Zephyr<->Zephyr only). No sequence validation yet (robustness gap; runs show timeouts=0).
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#define TARGET_NAME "z54-lat"

static struct bt_uuid_128 chrc_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12340011, 0x1234, 0x1000, 0x8000, 0x00805f9b34fb));
#if defined(CONFIG_APP_HB_SENDER)
#include <zephyr/drivers/gpio.h>
#include <zephyr/random/random.h>
/* Dedicated safety-heartbeat characteristic (§14.9) — separate from ping-pong. */
static struct bt_uuid_128 hb_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12340012, 0x1234, 0x1000, 0x8000, 0x00805f9b34fb));
static struct bt_gatt_discover_params hb_disc_params;
static volatile uint16_t hb_handle;      /* 0 until discovered */
static volatile bool hb_frozen;          /* sw0 test hook: ONE-WAY suppress sending until reboot */
static uint32_t hb_epoch;                /* random per boot — NORMALLY a new epoch (32-bit, probabilistic) */
static uint32_t hb_seq;
static uint32_t hb_dropped;              /* prior HB outstanding at period, or write error */
static atomic_t hb_outstanding;          /* one-outstanding completion gate */
static atomic_t hb_conn_gen;             /* bumped at every disconnect (rev-3) — under conn_mutex */
static uint32_t hb_attempted, hb_queued, hb_completed, hb_missed_sched;
static uint32_t hb_aborted_disconnect;   /* outstanding op orphaned by a disconnect */
static const struct gpio_dt_spec alive_gpio = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), alive_gpios);
static const struct gpio_dt_spec hbtx_gpio  = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), hbtx_gpios);
static const struct gpio_dt_spec fault_gpio = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), fault_gpios);
#endif

#if defined(CONFIG_APP_LOAD_RAMP)
static struct bt_uuid_128 blk_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12340013, 0x1234, 0x1000, 0x8000, 0x00805f9b34fb));
static struct bt_gatt_discover_params blk_disc_params;
static volatile uint16_t blk_handle;   /* 0 until discovered */
#endif

static struct bt_conn *default_conn;
/* Guards default_conn against the disconnect callback clearing/unref'ing it while
 * the HB sender or ping thread is submitting (rev-2 review fix). Take a counted
 * reference under the mutex for each submission; unref after use. */
static K_MUTEX_DEFINE(conn_mutex);
static struct bt_conn *conn_ref_get(void)
{
	struct bt_conn *c = NULL;
	k_mutex_lock(&conn_mutex, K_FOREVER);
	if (default_conn) { c = bt_conn_ref(default_conn); }
	k_mutex_unlock(&conn_mutex);
	return c;
}

static struct bt_gatt_discover_params disc_params;
static struct bt_gatt_subscribe_params sub_params;
static struct bt_gatt_exchange_params mtu_params;
static volatile uint16_t value_handle;
static volatile bool subscribed;
static volatile bool fast_ready;     /* set once the low-latency interval is applied */
static volatile bool phy_2m;         /* set once the 2M PHY update is confirmed */
static volatile bool fast_requested; /* guard: param_update->1ms issued once */
/* DISCRIMINATOR TEST 1: pin interval (no low-lat drop) + start discovery only AFTER a fixed
 * delay past the 2M-PHY-update completion, to exclude any PHY-switch/transition overlap. */
/* TRACK 1 (product/safety, §12.3): pin the spec 7.5 ms interval — no low-lat drop. Pings run
 * at 7.5 ms (fast_ready is set on connect when pinned). */
#if defined(CONFIG_APP_GO_FAST_1MS)
static const bool pin_interval = false;   /* §6.1 positive control: drop to 2M/1ms */
#else
static const bool pin_interval = true;
#endif
/* DIRECT-1MS TEST (§13.16, PHY-confounded — ran at 1M): create the connection already at 1 ms.
 * Off for Track 1 (connect at 7.5 ms). */
static const bool direct_1ms = false;
#define POST_PHY_DELAY_MS 500
static volatile bool mtu_done;
static volatile bool disc_scheduled;
static struct k_work_delayable disc_work;
/* NEGATIVE RESULT (2026-08-01): 2M @ 1ms + 52us tIFS reaches the operating point but
 * CRASHES ~1s in (reproducible) AND shows no RTT gain. Left OFF by default
 * so the rig stays stable on the CONFIRMED-2M link. (An earlier "1M" label
 * here was stale: the record corrected it — "all prior 1M
 * numbers were actually 2M", proven by forcing 1M. M0 Phase 4 re-tests this as a
 * reproducibility check, not as an open historical question.) */
static const bool try_2m = false;
/* Diagnostic (kept, default OFF): explicitly request 1M. Confirmed 2026-08-01 that (a) the
 * default link IS 2M — forcing 1M changed it to tx=1 rx=1 — and (b) the crash is NOT
 * 2M-specific: an explicit 1M request also crashes (ZEPHYR FATAL ERROR 36 + Failed LE Set
 * PHY -13). Conclusion: don't issue ANY explicit PHY update at the 1ms/52us operating point. */
static const bool try_1m = false;
static K_SEM_DEFINE(pong_sem, 0, 1);
static volatile uint64_t echo_rx_bytes;   /* duplex cells: notification bytes received */

/* Drop to the 1ms interval — only after BOTH subscribed and 2M PHY are in place.
 * Sequencing 2M first (at the stable 7.5ms interval) then 1ms is the whole point:
 * a 2M PHY-update *at* 1ms fails (-13); done at 7.5ms it sticks across the drop. */
static void go_fast(struct bt_conn *conn)
{
	if (pin_interval) { return; }
#if defined(CONFIG_BT_CTLR_PHY_2M)
	if (!subscribed || !phy_2m || fast_requested) { return; }
#else
	if (!subscribed || fast_requested) { return; }   /* 1M test: no 2M gate */
#endif
	fast_requested = true;
	struct bt_le_conn_param *fast = BT_LE_CONN_PARAM(1, 1, 0, 400);
	int r = bt_conn_le_param_update(conn, fast);
	printk("param update ->1ms rc=%d\n", r);
}

/* RTT stats (us), reset each report */
static uint32_t n, rtt_min = 0xffffffff, rtt_max, timeouts;
static uint64_t rtt_sum;
/* Cumulative (whole-soak) tail/loss. gt2/gt5 size the short tail; gt15/gt20/gt30 bound
 * p99.9 for the Track-1 7.5 ms baseline (typical RTT ~11.9 ms there). NOTE: tot_n/tot_to
 * cover only ACCEPTED writes -> they give a CONDITIONAL pong-timeout rate among attempted
 * transactions, NOT link availability. Availability lives in the counters below. */
static uint32_t tot_n, tot_to, rtt_max_ever, gt2ms, gt5ms, gt15ms, gt20ms, gt30ms;
/* Per-stage RTT histogram, 1 ms bins 0..255 ms (top bin = overflow). Reset at the start of
 * each load-ramp stage and dumped at its end, so offline we get true p50/p99/p99.9 per load
 * level instead of a single-observation windowed max. Written by the ping thread; read+reset
 * by the load-ramp thread at stage boundaries (a rare boundary race costs <1 sample). */
#define RTT_HIST_NB 256
static uint16_t rtt_hist[RTT_HIST_NB];
static uint32_t rtt_hist_to;   /* pong timeouts within the current stage */
/* Availability accounting (Track 1): every probe the loop *intends* is scheduled; a write
 * either fails (wr_fail) or is accepted (wr_ok = tot_n + tot_to). Seconds with the link
 * down (not subscribed/ready) and the longest continuous outage are tracked at 1 Hz. */
static uint32_t probes_sched, wr_fail_cnt, down_secs, max_outage_secs;
/* Point 8 discriminator: delay pinging N ms after the 1 ms interval-update callback (0 = immediate).
 * If a nonzero delay converts the ~5/6 startup wedges into sustained runs, the collision is with
 * connection-update / startup traffic rather than steady-state echo. */
static const uint32_t ping_delay_ms = 500;
static volatile int64_t fast_ready_at;

/* DIAGNOSTIC (assert-disabled fault-injection soak, 2026-08-01): SN/NESN-progress proxy +
 * cancel/disconnect telemetry. `lll_conn_trx_busy_cancels` is the controller counter for the
 * (now non-fatal) trx_busy cancel path — see lll_conn.c. */
#if defined(CONFIG_BT_LL_SW_SPLIT)
extern uint32_t volatile lll_conn_trx_busy_cancels;
#else
static uint32_t volatile lll_conn_trx_busy_cancels; /* SDC/other controllers: patched
                                                     * open-LL diagnostic unavailable;
                                                     * reads as 0 */
#endif
static volatile uint32_t last_sent_seq;   /* seq of the ping currently outstanding */
static volatile uint32_t seq_mismatch;    /* pong seq != outstanding ping seq (reorder/dup/stale) */
static volatile uint32_t bad_len;         /* pong length != 8 (malformed) */
static volatile uint32_t disc_count;      /* unexpected disconnects during the soak */
static volatile uint8_t  last_disc_reason;

static void start_scan(void);

static uint8_t notify_cb(struct bt_conn *conn, struct bt_gatt_subscribe_params *p,
			 const void *data, uint16_t length)
{
	echo_rx_bytes += length;   /* duplex: count reverse-direction payload */
	ARG_UNUSED(conn); ARG_UNUSED(p);
	if (!data) { subscribed = false; return BT_GATT_ITER_STOP; }
	if (length != 8) {
		bad_len++;                      /* malformed pong — do NOT wake the waiter */
		return BT_GATT_ITER_CONTINUE;
	}
	if (sys_get_le32(data) != last_sent_seq) {
		seq_mismatch++;                 /* stale/dup/reorder/late pong (old seq) — do NOT wake;
						 * wait for the pong matching the outstanding ping.
						 * NOTE: this rig's runs had seqbad=0 (serialized, no reorder)
						 * so the old unconditional give never fired — latent; fixed
						 * for correctness after EATT's multi-bearer reorder exposed it. */
		return BT_GATT_ITER_CONTINUE;
	}
	k_sem_give(&pong_sem);           /* the MATCHING pong arrived */
	return BT_GATT_ITER_CONTINUE;
}


/* ---- SDC 6.x feature cells (sdc-benchmark-plan.md F1/S1) ----------------
 * Runs once, 3 s after SUBSCRIBED: query supported short intervals (S1
 * protocol requirement), then issue the configured requests. Results arrive
 * via the host callbacks below and land in the serial log. Compiled only
 * when the 6.x host features are enabled (sdc-*.conf fragments). */
#if defined(CONFIG_BT_SHORTER_CONNECTION_INTERVALS) || defined(CONFIG_BT_FRAME_SPACE_UPDATE)
static void sdc6x_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	struct bt_conn *conn = conn_ref_get();
	if (!conn) { return; }
#if defined(CONFIG_BT_SHORTER_CONNECTION_INTERVALS)
	struct bt_conn_le_min_conn_interval_info info;
	int qr = bt_conn_le_read_min_conn_interval_groups(&info);
	if (qr == 0) {
		printk("SCI: supported min interval=%u us, groups=%u\n",
		       info.min_supported_conn_interval_us, info.num_groups);
	} else {
		printk("SCI: min-interval query rc=%d\n", qr);
	}
#if CONFIG_APP_SCI_INTERVAL_125US > 0
	const struct bt_conn_le_conn_rate_param rp = {
		.interval_min_125us = CONFIG_APP_SCI_INTERVAL_125US,
		.interval_max_125us = CONFIG_APP_SCI_INTERVAL_125US,
		.subrate_min = 1, .subrate_max = 1,
		.max_latency = 0, .continuation_number = 0,
		.supervision_timeout_10ms = 400,
		.min_ce_len_125us = 1,
		.max_ce_len_125us = CONFIG_APP_SCI_INTERVAL_125US,   /* CE may span the interval */
	};
	int rr = bt_conn_le_conn_rate_request(conn, &rp);
	printk("SCI: conn_rate_request %u x125us rc=%d\n",
	       CONFIG_APP_SCI_INTERVAL_125US, rr);
#endif
#endif /* SHORTER_CONNECTION_INTERVALS */
#if defined(CONFIG_BT_FRAME_SPACE_UPDATE) && CONFIG_APP_FSU_MAX_US > 0
	const struct bt_conn_le_frame_space_update_param fp = {
		.phys = BT_HCI_LE_FRAME_SPACE_UPDATE_PHY_1M_MASK |
			BT_HCI_LE_FRAME_SPACE_UPDATE_PHY_2M_MASK,
		.spacing_types = BT_HCI_LE_FRAME_SPACE_UPDATE_SPACING_TYPE_IFS_ACL_CP_MASK |
				 BT_HCI_LE_FRAME_SPACE_UPDATE_SPACING_TYPE_IFS_ACL_PC_MASK,
		.frame_space_min = CONFIG_APP_FSU_MIN_US,
		.frame_space_max = CONFIG_APP_FSU_MAX_US,
	};
	int fr = bt_conn_le_frame_space_update(conn, &fp);
	printk("FSU: request [%u..%u] us (ACL both dirs, 1M+2M) rc=%d\n",
	       CONFIG_APP_FSU_MIN_US, CONFIG_APP_FSU_MAX_US, fr);
#endif
	bt_conn_unref(conn);
}
static K_WORK_DELAYABLE_DEFINE(sdc6x_work, sdc6x_work_fn);

#if defined(CONFIG_BT_SHORTER_CONNECTION_INTERVALS)
static void sdc6x_rate_changed(struct bt_conn *conn, uint8_t status,
			       const struct bt_conn_le_conn_rate_changed *p)
{
	ARG_UNUSED(conn);
	printk("SCI: rate_changed status=0x%02x interval=%u us subrate=%u lat=%u\n",
	       status, p->interval_us, p->subrate_factor, p->peripheral_latency);
}
#endif
#if defined(CONFIG_BT_FRAME_SPACE_UPDATE)
static void sdc6x_fsu_updated(struct bt_conn *conn,
			      const struct bt_conn_le_frame_space_updated *p)
{
	ARG_UNUSED(conn);
	printk("FSU: updated status=0x%02x spacing=%u us phys=0x%x types=0x%x initiator=%d\n",
	       p->status, p->frame_space, p->phys, p->spacing_types, (int)p->initiator);
}
#endif
BT_CONN_CB_DEFINE(sdc6x_cbs) = {
#if defined(CONFIG_BT_SHORTER_CONNECTION_INTERVALS)
	.conn_rate_changed = sdc6x_rate_changed,
#endif
#if defined(CONFIG_BT_FRAME_SPACE_UPDATE)
	.frame_space_updated = sdc6x_fsu_updated,
#endif
};
#endif /* SDC 6.x feature cells */

static uint8_t discover_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   struct bt_gatt_discover_params *params)
{
	if (!attr) { printk("discover done (nothing)\n"); return BT_GATT_ITER_STOP; }
#if defined(CONFIG_APP_HB_SENDER)
	if (params == &hb_disc_params) {
		hb_handle = bt_gatt_attr_value_handle(attr);
		printk("HB char discovered, handle=%u — heartbeats start\n", hb_handle);
		return BT_GATT_ITER_STOP;
	}
#endif
#if defined(CONFIG_APP_LOAD_RAMP)
	if (params == &blk_disc_params) {
		blk_handle = bt_gatt_attr_value_handle(attr);
		printk("LOADRAMP: bulk char handle=%u\n", blk_handle);
		return BT_GATT_ITER_STOP;
	}
#endif
	if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		value_handle = bt_gatt_attr_value_handle(attr);
		sub_params.value_handle = value_handle;
		disc_params.uuid = BT_UUID_GATT_CCC;
		disc_params.start_handle = attr->handle + 2;
		disc_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
		bt_gatt_discover(conn, &disc_params);
		return BT_GATT_ITER_STOP;
	}
	sub_params.notify = notify_cb;
	sub_params.value = BT_GATT_CCC_NOTIFY;
	sub_params.ccc_handle = attr->handle;
	int e = bt_gatt_subscribe(conn, &sub_params);
	if (!e || e == -EALREADY) {
		subscribed = true;
		printk("SUBSCRIBED, vh=%u\n", value_handle);
		go_fast(conn);   /* drop to 1ms once 2M is also confirmed (may already be) */
#if defined(CONFIG_APP_LOAD_RAMP)
		blk_disc_params.uuid = &blk_uuid.uuid;
		blk_disc_params.func = discover_cb;
		blk_disc_params.start_handle = 0x0001;
		blk_disc_params.end_handle = 0xffff;
		blk_disc_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
		bt_gatt_discover(conn, &blk_disc_params);
#endif
#if defined(CONFIG_BT_SHORTER_CONNECTION_INTERVALS) || defined(CONFIG_BT_FRAME_SPACE_UPDATE)
		k_work_schedule(&sdc6x_work,
				K_SECONDS(CONFIG_APP_SDC6X_DELAY_S));
#endif
#if defined(CONFIG_APP_HB_SENDER)
		/* Chain a second discovery for the dedicated heartbeat characteristic. */
		hb_disc_params.uuid = &hb_uuid.uuid;
		hb_disc_params.func = discover_cb;
		hb_disc_params.start_handle = 0x0001;
		hb_disc_params.end_handle = 0xffff;
		hb_disc_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
		bt_gatt_discover(conn, &hb_disc_params);
#endif
	} else { printk("subscribe rc=%d\n", e); }
	return BT_GATT_ITER_STOP;
}

/* Discovery, started only after MTU + 2M-confirmed + a fixed post-PHY-switch delay. */
static void disc_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	struct bt_conn *conn = conn_ref_get();   /* no raw default_conn use: disconnected()
	                                          * (BT RX thread) can clear+unref concurrently;
	                                          * cancel_delayable() doesn't stop a running fn */
	if (!conn) { return; }
	struct bt_conn_info info;
	uint32_t iv = (bt_conn_get_info(conn, &info) == 0) ? info.le.interval_us : 0xffffffff;
	printk("discovery start (post-PHY +%dms) interval=%u phy2m=%d\n", POST_PHY_DELAY_MS, iv, phy_2m);
	disc_params.uuid = &chrc_uuid.uuid;
	disc_params.func = discover_cb;
	disc_params.start_handle = 0x0001;
	disc_params.end_handle = 0xffff;
	disc_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
	bt_gatt_discover(conn, &disc_params);
	bt_conn_unref(conn);
}
static void maybe_start_disc(void)
{
#if defined(CONFIG_BT_CTLR_PHY_2M)
	/* Product path: wait for the auto-2M switch, then a fixed delay, before discovery. */
	if (mtu_done && phy_2m && !disc_scheduled) {
#else
	/* 1M stability test: no 2M transition to wait for — start after MTU + fixed delay. */
	if (mtu_done && !disc_scheduled) {
#endif
		disc_scheduled = true;
		k_work_schedule(&disc_work, K_MSEC(POST_PHY_DELAY_MS));
	}
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency, uint16_t timeout)
{
	ARG_UNUSED(conn); ARG_UNUSED(latency); ARG_UNUSED(timeout);
	if (interval <= 3) { fast_ready = true; fast_ready_at = k_uptime_get(); }   /* low-latency applied */
	printk(">>> interval now %u (units) — pinging (delay=%ums)\n", interval, ping_delay_ms);
}

static void le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *p)
{
	ARG_UNUSED(conn);
	printk("PHY tx=%u rx=%u\n", p->tx_phy, p->rx_phy);
	if (p->tx_phy == BT_GAP_LE_PHY_2M && p->rx_phy == BT_GAP_LE_PHY_2M) {
		phy_2m = true;
		maybe_start_disc();   /* discovery waits for MTU + 2M + fixed delay */
	}
}

static void mtu_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_exchange_params *p)
{
	ARG_UNUSED(conn); ARG_UNUSED(p);
	printk("MTU err=%u mtu=%u\n", err, bt_gatt_get_mtu(conn));
	mtu_done = true;
	maybe_start_disc();   /* discovery starts only after 2M-confirmed + fixed delay */
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) { printk("connect failed 0x%02x\n", err); bt_conn_unref(default_conn); default_conn = NULL; start_scan(); return; }
	struct bt_conn_info info;
	if (bt_conn_get_info(conn, &info) == 0) {
		printk("GAP connected interval=%u\n", info.le.interval_us);
		/* Ping when the link is already at its operating interval: direct-1ms arm (<=3), or
		 * Track-1 product mode (interval pinned at 7.5 ms — no update coming). */
		if (info.le.interval_us <= 3750 || pin_interval) {   /* <=3 legacy units */
			fast_ready = true; fast_ready_at = k_uptime_get();
		}
	}
	fast_requested = false;
	if (try_1m) {
		phy_2m = true;   /* unblock go_fast; we only want to force the PHY down to 1M here */
		struct bt_conn_le_phy_param phy = {
			.options = BT_CONN_LE_PHY_OPT_NONE,
			.pref_tx_phy = BT_GAP_LE_PHY_1M, .pref_rx_phy = BT_GAP_LE_PHY_1M,
		};
		int pe = bt_conn_le_phy_update(conn, &phy);
		printk("PHY update ->1M rc=%d\n", pe);
	} else if (try_2m) {
		/* Request 2M at the stable 7.5ms interval (before dropping to 1ms). Intended to
		 * halve ping/pong airtime; in practice 2M+1ms+52us-tIFS crashes ~1s in (see note). */
		phy_2m = false;
		struct bt_conn_le_phy_param phy = {
			.options = BT_CONN_LE_PHY_OPT_NONE,
			.pref_tx_phy = BT_GAP_LE_PHY_2M, .pref_rx_phy = BT_GAP_LE_PHY_2M,
		};
		int pe = bt_conn_le_phy_update(conn, &phy);
		printk("PHY update ->2M rc=%d\n", pe);
	} else {
		phy_2m = false;  /* TEST 1: wait for the REAL auto-2M (le_phy_updated) before discovery */
	}
	mtu_done = false; disc_scheduled = false;   /* fresh per connection */
	mtu_params.func = mtu_cb;
	bt_gatt_exchange_mtu(conn, &mtu_params);
}
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	printk("GAP disconnected 0x%02x\n", reason);
	disc_count++; last_disc_reason = reason;
	subscribed = false;
	/* MUST reset before a reconnect: otherwise the ping loop starts pinging (and the
	 * peripheral starts echoing) on the fresh link BEFORE the 1 ms param update completes
	 * — the known echo-vs-connection-update interference (§10.3). Also drain any stale
	 * pong so the next measurement can't be satisfied by a leftover give. */
	fast_ready = false;
	value_handle = 0;
	mtu_done = false; disc_scheduled = false; phy_2m = false;
	k_work_cancel_delayable(&disc_work);
#if defined(CONFIG_BT_SHORTER_CONNECTION_INTERVALS) || defined(CONFIG_BT_FRAME_SPACE_UPDATE)
	/* Cancel pending 6.x feature work (SCI/FSU) so a request scheduled on a
	 * now-dead connection cannot fire against the next one; it is freshly
	 * scheduled from the next connection's SUBSCRIBED. */
	k_work_cancel_delayable(&sdc6x_work);
#endif
#if defined(CONFIG_APP_HB_SENDER)
	hb_handle = 0;   /* stop heartbeats until rediscovered on the next connection */
#endif
	k_sem_reset(&pong_sem);
	k_mutex_lock(&conn_mutex, K_FOREVER);
#if defined(CONFIG_APP_HB_SENDER)
	atomic_inc(&hb_conn_gen);   /* rev-4: bump + gate-clear under the SAME mutex as the
	                             * conn clear, so no sender can pair old conn/new gen */
	if (atomic_test_and_clear_bit(&hb_outstanding, 0)) {
		hb_aborted_disconnect++;   /* its callback may never run — gate freed here */
	}
#endif
	if (default_conn) { bt_conn_unref(default_conn); default_conn = NULL; }
	k_mutex_unlock(&conn_mutex);
	start_scan();
}
BT_CONN_CB_DEFINE(conn_cbs) = {
	.connected = connected, .disconnected = disconnected,
	.le_param_updated = le_param_updated, .le_phy_updated = le_phy_updated,
};

static bool ad_name(struct bt_data *data, void *ud)
{
	bool *m = ud;
	if ((data->type == BT_DATA_NAME_COMPLETE || data->type == BT_DATA_NAME_SHORTENED) &&
	    data->data_len == strlen(TARGET_NAME) && !memcmp(data->data, TARGET_NAME, data->data_len)) {
		*m = true; return false;
	}
	return true;
}
static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type, struct net_buf_simple *ad)
{
	ARG_UNUSED(rssi); ARG_UNUSED(type);
	if (default_conn) { return; }
	bool m = false; bt_data_parse(ad, ad_name, &m);
	if (!m) { return; }
	bt_le_scan_stop();
	struct bt_le_conn_param *p = direct_1ms ? BT_LE_CONN_PARAM(1, 1, 0, 400)
						: BT_LE_CONN_PARAM(CONFIG_APP_CONN_INT_UNITS, CONFIG_APP_CONN_INT_UNITS, 0, 400);
	if (bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, p, &default_conn)) { start_scan(); }
}
static void start_scan(void)
{
	struct bt_le_scan_param sp = { .type = BT_LE_SCAN_TYPE_ACTIVE, .options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL, .window = BT_GAP_SCAN_FAST_WINDOW };
	int e = bt_le_scan_start(&sp, device_found);
	printk("scan rc=%d\n", e);
}

#if defined(CONFIG_APP_HB_SENDER)
/* ---- safety-heartbeat sender (§14.9, rev 2): fixed period, decoupled from ping/pong ----
 * Absolute timepoints preserve phase; when the thread wakes LATE, every missed
 * period is SKIPPED (counted hb_missed_sched) — never emitted as a catch-up burst.
 * At most ONE heartbeat is outstanding: bt_gatt_write_without_response_cb()'s
 * completion callback clears the flag; if it hasn't run by the next period the
 * new frame is dropped (counted). Note "queued" == accepted by the ATT path,
 * not proof of transmission — hence the completion gate. Sends from a thread,
 * never a timer ISR. */
static void hb_complete_cb(struct bt_conn *conn, void *user_data)
{
	ARG_UNUSED(conn);
	/* rev-5: generation rides IMMUTABLY in user_data (rev-4), AND the
	 * compare+clear is SERIALIZED under conn_mutex — as separate atomics, an
	 * old callback could pass the compare, get preempted across a disconnect
	 * + new connection, then clear the NEW op's gate. Callback runs in
	 * workqueue context, so a mutex is appropriate. Within one generation the
	 * one-outstanding invariant means a set gate can only be this op's. */
	uint32_t op_gen = (uint32_t)(uintptr_t)user_data;
	k_mutex_lock(&conn_mutex, K_FOREVER);
	if (op_gen == (uint32_t)atomic_get(&hb_conn_gen) &&
	    atomic_test_and_clear_bit(&hb_outstanding, 0)) {
		hb_completed++;
	}
	k_mutex_unlock(&conn_mutex);
}
static void hb_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	static uint8_t frame[8];   /* static: must stay valid until hb_complete_cb */
	int64_t next = k_uptime_get();
	while (1) {
		next += CONFIG_APP_HB_PERIOD_MS;
		int64_t now = k_uptime_get();
		if (next < now) {
			/* Late: skip every missed period — never catch up. */
			uint32_t missed = (uint32_t)((now - next) / CONFIG_APP_HB_PERIOD_MS) + 1;
			hb_missed_sched += missed;
			next += (int64_t)missed * CONFIG_APP_HB_PERIOD_MS;
		}
		k_sleep(K_TIMEOUT_ABS_MS(next));
		if (hb_frozen || hb_handle == 0) { continue; }
		hb_attempted++;
		/* rev-5: acquire {connection, generation, outstanding=true} as ONE
		 * locked step — nothing can interleave a disconnect between them. */
		uint32_t op_gen = 0;
		struct bt_conn *conn = NULL;
		k_mutex_lock(&conn_mutex, K_FOREVER);
		if (atomic_test_and_set_bit(&hb_outstanding, 0)) {
			k_mutex_unlock(&conn_mutex);
			hb_dropped++;              /* prior HB not completed yet */
			continue;
		}
		if (default_conn) {
			conn = bt_conn_ref(default_conn);
			op_gen = (uint32_t)atomic_get(&hb_conn_gen);
		} else {
			atomic_clear(&hb_outstanding);
		}
		k_mutex_unlock(&conn_mutex);
		if (!conn) {
			hb_dropped++;              /* count so totals reconcile */
			continue;
		}
		sys_put_le32(hb_epoch, &frame[0]);
		sys_put_le32(++hb_seq, &frame[4]);
		int e = bt_gatt_write_without_response_cb(conn, hb_handle, frame, sizeof(frame),
							  false, hb_complete_cb,
							  (void *)(uintptr_t)op_gen);
		if (e) {
			hb_dropped++;
			/* Same locked compare-and-clear as the callback: a disconnect
			 * may have intervened since the send failed. */
			k_mutex_lock(&conn_mutex, K_FOREVER);
			if (op_gen == (uint32_t)atomic_get(&hb_conn_gen)) {
				(void)atomic_test_and_clear_bit(&hb_outstanding, 0);
			}
			k_mutex_unlock(&conn_mutex);
		} else {
			hb_queued++;
			gpio_pin_toggle_dt(&hbtx_gpio);   /* LA: one edge per ACCEPTED-for-queueING HB */
		}
		bt_conn_unref(conn);
	}
}
K_THREAD_DEFINE(hb_tid, 1536, hb_fn, NULL, NULL, NULL, 6, 0, 0);

/* sw0: SUPPRESS heartbeat sending (fault-injection test hook — the task keeps
 * running; this simulates silence, not a literal task freeze). The alive pin
 * stays high so the LA distinguishes suppressed-sending from power loss, and
 * the fault pin toggles to timestamp the injection edge itself. */
static struct gpio_callback hb_btn_cb;
static void hb_freeze_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev); ARG_UNUSED(cb); ARG_UNUSED(pins);
	/* ONE-WAY (rev-3, debounce-proof): first press suppresses sending until
	 * reboot; later presses/bounces are ignored, so exactly one fault edge. */
	if (hb_frozen) { return; }
	hb_frozen = true;
	gpio_pin_toggle_dt(&fault_gpio);   /* LA channel 1: single fault-injection edge */
	printk("HB sending SUPPRESSED until reboot (test hook; task alive)\n");
}
static const struct gpio_dt_spec hb_btn = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
/* Cross-board fail-safe monitor: input + internal pull-down, jumpered to the
 * peripheral's STOP line for the unpowered->STOP-low check (bench stand-in for
 * the discrete external pull-down; this board survives that test). Diagnostic
 * only — bring-up failure must not disable the sender. */
static const struct gpio_dt_spec mon_gpio = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), mon_gpios);
static bool mon_ok;

static int hb_sender_init(void)
{
	int rc = 0;
	hb_epoch = sys_rand32_get();   /* NORMALLY a new epoch per boot (32-bit, probabilistic) */
	if (!gpio_is_ready_dt(&alive_gpio) || !gpio_is_ready_dt(&hbtx_gpio) ||
	    !gpio_is_ready_dt(&fault_gpio) || !gpio_is_ready_dt(&hb_btn)) { rc = -ENODEV; }
	/* alive: high while powered; needs an EXTERNAL pull-down so it FALLS (not
	 * floats) on power loss. */
	if (!rc) { rc = gpio_pin_configure_dt(&alive_gpio, GPIO_OUTPUT_ACTIVE); }
	if (!rc) { rc = gpio_pin_configure_dt(&hbtx_gpio, GPIO_OUTPUT_INACTIVE); }
	if (!rc) { rc = gpio_pin_configure_dt(&fault_gpio, GPIO_OUTPUT_INACTIVE); }
	if (!rc) { rc = gpio_pin_configure_dt(&hb_btn, GPIO_INPUT); }
	if (!rc) { rc = gpio_pin_interrupt_configure_dt(&hb_btn, GPIO_INT_EDGE_TO_ACTIVE); }
	if (!rc) {
		gpio_init_callback(&hb_btn_cb, hb_freeze_pressed, BIT(hb_btn.pin));
		rc = gpio_add_callback(hb_btn.port, &hb_btn_cb);
	}
	if (rc) {
		/* Measurement markers unusable -> this measurement build must not
		 * pretend to measure: suppress sending entirely. */
		hb_frozen = true;
		printk("HB sender: GPIO bring-up FAILED (%d) — sender disabled\n", rc);
		return rc;
	}
	printk("HB sender: period %d ms, epoch=%08x\n", CONFIG_APP_HB_PERIOD_MS, hb_epoch);
	mon_ok = gpio_is_ready_dt(&mon_gpio) &&
		 gpio_pin_configure_dt(&mon_gpio, GPIO_INPUT) == 0;
	if (!mon_ok) { printk("mon pin bring-up failed (diagnostic only)\n"); }
	return 0;
}
#endif /* CONFIG_APP_HB_SENDER */

#if defined(CONFIG_APP_LOAD_RAMP)
#if defined(CONFIG_APP_LOAD_POLITE)
/* Mitigation arm: completion-pacing. A 1-permit sem gates each bulk write on the previous
 * write's completion callback, so at most ONE bulk write is in the TX path — the queue stays
 * shallow (the stop-signal ping isn't stuck behind a backlog) while bulk still saturates. */
static K_SEM_DEFINE(bulk_sem, 1, 1);
static void bulk_sent_cb(struct bt_conn *c, void *ud) { ARG_UNUSED(c); ARG_UNUSED(ud); k_sem_give(&bulk_sem); }
#endif

/* Compute per-stage percentiles from the 1 ms-bin histogram and emit them in a SINGLE printk
 * (many small printks flood CONFIG_LOG's buffer -> "messages dropped"). Bin index = ms, so
 * percentiles are 1 ms-resolved — plenty for a 2..175 ms tail. Tag = the load target just
 * completed. Does NOT reset; the next stage clears it at its start. */
static void rtt_hist_dump(uint16_t target)
{
	uint32_t tot = 0, pmax = 0, g30 = 0, g100 = 0;
	for (int i = 0; i < RTT_HIST_NB; i++) {
		tot += rtt_hist[i];
		if (rtt_hist[i]) { pmax = i; }
		if (i >= 30)  { g30  += rtt_hist[i]; }
		if (i >= 100) { g100 += rtt_hist[i]; }
	}
	uint32_t p50 = 0, p90 = 0, p99 = 0, p999 = 0;
	if (tot) {
		uint32_t c50  = tot / 2;
		uint32_t c90  = (uint32_t)((uint64_t)tot * 90  / 100);
		uint32_t c99  = (uint32_t)((uint64_t)tot * 99  / 100);
		uint32_t c999 = (uint32_t)((uint64_t)tot * 999 / 1000);
		uint32_t cum = 0;
		for (int i = 0; i < RTT_HIST_NB; i++) {
			cum += rtt_hist[i];
			if (!p50  && cum >= c50)  { p50  = i; }
			if (!p90  && cum >= c90)  { p90  = i; }
			if (!p99  && cum >= c99)  { p99  = i; }
			if (!p999 && cum >= c999) { p999 = i; }
		}
	}
	printk("PCTL target=%u tot=%u to=%u p50=%u p90=%u p99=%u p99.9=%u max=%u g30=%u g100=%u (ms/counts)\n",
	       target, tot, rtt_hist_to, p50, p90, p99, p999, pmax, g30, g100);
}

/* Self-stepping background-load ramp: token-bucket 244 B writes to the bulk-sink char at each
 * target rate for 60 s, pings untouched. Naive arm = fire-and-forget writes; polite arm
 * (CONFIG_APP_LOAD_POLITE) = 1-outstanding completion-paced. A fine RTT histogram is captured
 * per stage so the tail is a stable percentile, not a single-observation max. */
static void load_ramp_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
#if defined(CONFIG_APP_LOAD_RAMP_HIGH)
	static const uint16_t rates[] = { 0, 140, 150, 160, 170, 180, 190 };
#else
	static const uint16_t rates[] = { 0, 25, 50, 75, 100, 125, 150 };
#endif
	static uint8_t bulk[244];
	while (1) {
		if (!subscribed || !blk_handle) { k_msleep(100); continue; }
		for (size_t i = 0; i < ARRAY_SIZE(rates); i++) {
			uint16_t r = rates[i];
			printk("LOADRAMP: target=%u KBps for 60s\n", r);
			memset(rtt_hist, 0, sizeof(rtt_hist)); rtt_hist_to = 0;  /* fresh per stage */
			int64_t stage_end = k_uptime_get() + 60000;
			/* tokens: r KB/s = r*1024/244 writes/s */
			uint32_t wr_per_s = r ? (r * 1024U) / 244U : 0;
			while (k_uptime_get() < stage_end) {
				if (!subscribed || !blk_handle) { break; }
				if (!wr_per_s) { k_msleep(200); continue; }
				int64_t sec_end = k_uptime_get() + 1000;
				uint32_t sent = 0;
				while (sent < wr_per_s && k_uptime_get() < sec_end) {
					struct bt_conn *pc = conn_ref_get();
					if (!pc) { break; }
#if defined(CONFIG_APP_LOAD_POLITE)
					if (k_sem_take(&bulk_sem, K_MSEC(50)) != 0) { bt_conn_unref(pc); continue; }
					int e = bt_gatt_write_without_response_cb(pc, blk_handle,
									       bulk, sizeof(bulk), false, bulk_sent_cb, NULL);
					if (e != 0) { k_sem_give(&bulk_sem); }
#else
					int e = bt_gatt_write_without_response(pc, blk_handle,
									       bulk, sizeof(bulk), false);
#endif
					bt_conn_unref(pc);
					if (e == 0) { sent++; }
					else { k_msleep(2); }
				}
				int64_t left = sec_end - k_uptime_get();
				if (left > 0) { k_msleep((uint32_t)left); }
			}
			rtt_hist_dump(r);   /* percentile histogram for this load level */
		}
		printk("LOADRAMP: cycle complete\n");
	}
}
K_THREAD_DEFINE(load_ramp_tid, 2048, load_ramp_fn, NULL, NULL, NULL, 8, 0, 0);
#endif /* CONFIG_APP_LOAD_RAMP */

#if defined(CONFIG_APP_TPUT_BLAST)
/* Completion callback shared with the blast loop's statics via file scope. */
static volatile uint32_t g_comp_cnt, g_comp_last, g_comp_gmin = ~0U, g_comp_gmax, g_comp_gcnt;
static volatile uint64_t g_comp_gsum;
static void blast_done_impl(struct bt_conn *c, void *ud)
{
	ARG_UNUSED(c); ARG_UNUSED(ud);
	uint32_t now = k_cycle_get_32();
	if (g_comp_last) {
		uint32_t g = now - g_comp_last;
		if (g < g_comp_gmin) { g_comp_gmin = g; }
		if (g > g_comp_gmax) { g_comp_gmax = g; }
		g_comp_gsum += g; g_comp_gcnt++;
	}
	g_comp_last = now;
	g_comp_cnt++;
}
#endif

/* ---- ping thread: serialized RTT (or throughput blast when configured) ---- */
static void ping_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
#if defined(CONFIG_APP_TPUT_BLAST)
	/* Throughput cell: saturate the link with max-payload GATT
	 * write-without-response; the PERIPHERAL's rxkBps counter is the
	 * measurement. Fills until -ENOMEM, then yields for a buffer. */
	static uint8_t blast[247];
	uint32_t sent = 0;
	/* Completion cadence via the _cb variant: callback fires when the stack
	 * releases the buffer (controller consumed it). */
	uint32_t first_calls[16]; uint32_t first_n = 0; bool first_printed = false;
	/* Phase-A diagnosis (fsu-throughput-plan.md): per-second accounting that
	 * discriminates "the write call blocks" from "completions are paced". */
	uint32_t att = 0, ok = 0, enomem = 0, oerr = 0;
	uint32_t c_min = ~0U, c_max = 0; uint64_t c_sum = 0;
	uint32_t okgap_max = 0, last_ok_cyc = 0;
	int64_t next_report = k_uptime_get() + 1000;
	const uint32_t cyc_per_us = sys_clock_hw_cycles_per_sec() / 1000000U;
	bool phy2m_requested = false;
	while (1) {
		if (!subscribed || !default_conn) { phy2m_requested = false; k_msleep(50); continue; }
		struct bt_conn *pc = conn_ref_get();
		if (!pc) { k_msleep(2); continue; }
		if (!phy2m_requested) {
			/* Phase-A finding: the link can sit at 1M during blast (8 us/byte
			 * measured). Pin 2M explicitly for the throughput cells. */
			phy2m_requested = true;
			int pr = bt_conn_le_phy_update(pc, BT_CONN_LE_PHY_PARAM_2M);
			printk("BLAST: phy_update->2M rc=%d\n", pr);
		}
		uint16_t payload_len = bt_gatt_get_mtu(pc) - 3U;
		if (payload_len > sizeof(blast)) { payload_len = sizeof(blast); }
#if defined(CONFIG_APP_TPUT_PAYLOAD_CAP) && CONFIG_APP_TPUT_PAYLOAD_CAP > 0
		if (payload_len > CONFIG_APP_TPUT_PAYLOAD_CAP) { payload_len = CONFIG_APP_TPUT_PAYLOAD_CAP; }
#endif
		sys_put_le32(sent, blast);
		uint32_t c0 = k_cycle_get_32();
		int e = bt_gatt_write_without_response_cb(pc, value_handle, blast,
							  payload_len, false,
							  blast_done_impl, NULL);
		uint32_t dc = k_cycle_get_32() - c0;
		if (first_n < 16) { first_calls[first_n++] = dc / cyc_per_us; }
		else if (!first_printed) {
			first_printed = true;
			printk("BLAST first16_call_us:");
			for (int i = 0; i < 16; i++) { printk(" %u", first_calls[i]); }
			printk("\n");
		}
		bt_conn_unref(pc);
		att++;
		if (dc < c_min) { c_min = dc; }
		if (dc > c_max) { c_max = dc; }
		c_sum += dc;
		if (e == 0) {
			sent++; ok++;
			if (last_ok_cyc) {
				uint32_t g = c0 - last_ok_cyc;
				if (g > okgap_max) { okgap_max = g; }
			}
			last_ok_cyc = c0;
		} else if (e == -ENOMEM) { enomem++; k_yield(); }
		else { oerr++; k_yield(); }
		if (k_uptime_get() >= next_report) {
			next_report += 1000;
			struct bt_conn *pc2 = conn_ref_get();
			struct bt_conn_info bi;
			uint8_t ptx = 0, prx = 0;
			if (bt_conn_get_info(pc2 ? pc2 : NULL, &bi) == 0 && bi.le.phy) { ptx = bi.le.phy->tx_phy; prx = bi.le.phy->rx_phy; }
			uint32_t cc = g_comp_cnt; g_comp_cnt = 0;
			uint32_t cgmin = g_comp_gmin, cgmax = g_comp_gmax;
			uint32_t cgavg = g_comp_gcnt ? (uint32_t)(g_comp_gsum / g_comp_gcnt) : 0;
			g_comp_gmin = ~0U; g_comp_gmax = 0; g_comp_gsum = 0; g_comp_gcnt = 0;
			static uint64_t prev_echo;
			uint32_t exk = (uint32_t)((echo_rx_bytes - prev_echo) / 1024U);
			prev_echo = echo_rx_bytes;
			printk("BLASTC comp=%u exkBps=%u cgap_us[min/avg/max]=%u/%u/%u\n",
			       cc, exk, cgmin / cyc_per_us, cgavg / cyc_per_us, cgmax / cyc_per_us);
			printk("BLAST phy=%u/%u att=%u ok=%u enomem=%u err=%u call_us[min/avg/max]=%u/%u/%u okgap_max_us=%u\n",
			       ptx, prx, att, ok, enomem, oerr, c_min / cyc_per_us,
			       (uint32_t)(c_sum / (att ? att : 1)) / cyc_per_us,
			       c_max / cyc_per_us, okgap_max / cyc_per_us);
			if (pc2) { bt_conn_unref(pc2); }
			att = ok = enomem = oerr = 0;
			c_min = ~0U; c_max = 0; c_sum = 0; okgap_max = 0;
		}
	}
#endif /* CONFIG_APP_TPUT_BLAST */
	uint8_t payload[8];
	uint32_t seq = 0;
	uint32_t wfail = 0, last_wfail_rc = 0;
	while (1) {
		if (!subscribed || !default_conn || !fast_ready) { k_msleep(50); continue; }
		if (ping_delay_ms && (k_uptime_get() - fast_ready_at) < (int64_t)ping_delay_ms) {
			k_msleep(10); continue;   /* point 8: hold pings until N ms after the interval update */
		}
		last_sent_seq = seq;             /* outstanding ping seq for the pong seq-check */
		k_sem_reset(&pong_sem);          /* drain any leftover give so this probe only wakes on its match */
		sys_put_le32(seq++, payload);
		probes_sched++;
		struct bt_conn *pc = conn_ref_get();   /* rev-2: no raw default_conn use */
		if (!pc) { k_msleep(2); continue; }
		uint32_t t0 = k_cycle_get_32();
		int e = bt_gatt_write_without_response(pc, value_handle, payload, sizeof(payload), false);
		bt_conn_unref(pc);
		if (e) {
			wr_fail_cnt++;
			if (e != (int)last_wfail_rc || (wfail % 200) == 0) {
				printk("write fail rc=%d (count=%u)\n", e, ++wfail);
				last_wfail_rc = e;
			} else { wfail++; }
			k_msleep(2); continue;
		}
		if (k_sem_take(&pong_sem, K_MSEC(200)) == 0) {
			uint32_t us = k_cyc_to_us_near32(k_cycle_get_32() - t0);
			n++; rtt_sum += us;
			if (us < rtt_min) rtt_min = us;
			if (us > rtt_max) rtt_max = us;
			tot_n++;
			if (us > rtt_max_ever) rtt_max_ever = us;
			if (us > 2000) gt2ms++;
			if (us > 5000) gt5ms++;
			if (us > 15000) gt15ms++;
			if (us > 20000) gt20ms++;
			if (us > 30000) gt30ms++;
			uint32_t _ms = us / 1000;
			if (_ms >= RTT_HIST_NB) _ms = RTT_HIST_NB - 1;
			rtt_hist[_ms]++;
		} else {
			timeouts++; tot_to++;
			rtt_hist_to++;
		}
		k_msleep(3);   /* pacing between round-trips */
	}
}
K_THREAD_DEFINE(ping_tid, 2048, ping_fn, NULL, NULL, NULL, 7, 0, 0);

static void report_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	uint32_t sec = 0, prev_cancels = 0, outage_run = 0;
	while (1) {
		k_msleep(1000);
		sec++;
		/* 1 Hz availability: a second is "down" when the probe loop can't run. */
		if (!subscribed || !fast_ready || !default_conn) {
			down_secs++;
			outage_run++;
			if (outage_run > max_outage_secs) { max_outage_secs = outage_run; }
		} else {
			outage_run = 0;
		}
		uint32_t cancels = lll_conn_trx_busy_cancels;
		/* Always print (even n==0) so a stalled-but-alive link is visible. Fault-injection
		 * telemetry: cancel path rate + SN/NESN-progress proxy (seq_mismatch/bad_len) +
		 * disconnects + cumulative RTT tail. */
		char hb[96] = "";
#if defined(CONFIG_APP_HB_SENDER)
		snprintf(hb, sizeof(hb), " | hb a=%u q=%u c=%u drop=%u miss=%u abrt=%u%s mon=%d",
			 hb_attempted, hb_queued, hb_completed, hb_dropped,
			 hb_missed_sched, hb_aborted_disconnect, hb_frozen ? " SUPPRESSED" : "",
			 mon_ok ? gpio_pin_get_dt(&mon_gpio) : -1);
#endif
		printk("t=%us RTT mean=%u min=%u max=%u n=%u to=%u | cancels=%u(+%u/s) "
		       "seqbad=%u len_bad=%u disc=%u(0x%02x) | tot_n=%u tot_to=%u maxever=%u "
		       ">2ms=%u >5ms=%u >15ms=%u >20ms=%u >30ms=%u "
		       "| probes=%u wrfail=%u down=%us maxout=%us%s\n",
		       sec, n ? (uint32_t)(rtt_sum / n) : 0, n ? rtt_min : 0, rtt_max, n, timeouts,
		       cancels, cancels - prev_cancels, seq_mismatch, bad_len,
		       disc_count, last_disc_reason, tot_n, tot_to, rtt_max_ever,
		       gt2ms, gt5ms, gt15ms, gt20ms, gt30ms,
		       probes_sched, wr_fail_cnt, down_secs, max_outage_secs, hb);
		prev_cancels = cancels;
		n = 0; rtt_sum = 0; rtt_min = 0xffffffff; rtt_max = 0; timeouts = 0;
	}
}
K_THREAD_DEFINE(report_tid, 1024, report_fn, NULL, NULL, NULL, 7, 0, 0);

int main(void)
{
#if defined(CONFIG_APP_HB_SENDER)
	(void)hb_sender_init();   /* on failure the sender is disabled (logged) */
#endif
	int err = bt_enable(NULL);
	if (err) { printk("bt_enable %d\n", err); return 0; }
	k_work_init_delayable(&disc_work, disc_work_fn);
	printk("\n=== nRF54L15 RTT ping-pong CENTRAL ===\n");
	start_scan();
	return 0;
}
