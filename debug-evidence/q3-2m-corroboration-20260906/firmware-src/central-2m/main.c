/*
 * Q2 minimal central: scan for "q2periph", connect at 7.5 ms / 1M, then pin the
 * channel map to TWO data channels (10 & 11) so a large steady fraction of
 * connection events land on the observer's channel. The connection AA/CRCInit
 * are printed by the controller patch (zephyr-patches/q2-print-conn-params.patch)
 * at ll_create_connection; read them to tune the observer (-DQ2 -DAA -DCRCINIT
 * -DCHAN=10). Build with the OPEN controller (CONFIG_BT_LL_SW_SPLIT=y), 1M only.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/device.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci_types.h>

/* pinned channel map: data channels 10 and 11 used, all others "bad".
 * bit10 -> byte1 bit2, bit11 -> byte1 bit3 => byte1 = 0x0C. */
static const uint8_t Q2_CHAN_MAP[5] = { 0x00, 0x0C, 0x00, 0x00, 0x00 };
#define Q2_OBS_CHAN 10   /* observer parks here (one of the two) */

/* controller ground-truth per-channel counters (non-circular denominators):
 * q2_evt = SCHEDULED opportunities, q2_tx = ACTUAL TX-completed packets. These
 * are FREE-RUNNING/monotonic (never reset by the app -> no race). session &
 * aa are set by the controller at connection setup (race-free, before the
 * first event); aa = connection Access Address = shared id across C/P/observer.
 * The host WINDOW markers bracket the counter deltas. */
extern volatile uint32_t lll_conn_q2_evt[40], lll_conn_q2_tx[40];
extern volatile uint32_t lll_conn_q2_session, lll_conn_q2_aa;

static struct bt_conn *default_conn;

static bool ad_has_name(struct bt_data *data, void *user_data)
{
	bool *found = user_data;
	if (data->type == BT_DATA_NAME_COMPLETE &&
	    data->data_len == sizeof("q2periph") - 1 &&
	    !memcmp(data->data, "q2periph", data->data_len)) {
		*found = true;
		return false;
	}
	return true;
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
		    struct net_buf_simple *ad)
{
	bool found = false;
	if (default_conn) return;
	if (type != BT_GAP_ADV_TYPE_ADV_IND && type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND)
		return;
	bt_data_parse(ad, ad_has_name, &found);
	if (!found) return;
	bt_le_scan_stop();
	struct bt_le_conn_param *cp = BT_LE_CONN_PARAM(16, 16, 0, 400); /* 20 ms fixed (ring-fit for 2M 30s capture) */
	int err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, cp, &default_conn);
	printk("CENTRAL found q2periph rssi=%d -> create err=%d\n", rssi, err);
	if (err) bt_le_scan_start(BT_LE_SCAN_ACTIVE, scan_cb);
}

/* Q3a FSU (COMMAND-TRIGGERED): the runner sends exactly ONE 'F' after CAPTURE-START
 * + the configured baseline dwell, so the observer sees the on-air tIFS step
 * 150->APP_FSU_MIN_US us MID-CAPTURE (both pre- and post-step plateaus in one run).
 * The old auto-at-connect scheduling is REMOVED (it could only yield a post-FSU
 * plateau). The whole path is gated behind the EXPLICIT Q3 mode (CONFIG_APP_Q3_MODE)
 * so Q2 runtime behavior is unchanged. APP_FSU_MAX_US=0 => no-request (f150
 * control): an 'F' is rejected, never sent to the stack. `frame_space_updated` is
 * the central-side completion (a cross-val source). */
#if defined(CONFIG_BT_FRAME_SPACE_UPDATE)
static void fsu_updated(struct bt_conn *conn, const struct bt_conn_le_frame_space_updated *p)
{
	ARG_UNUSED(conn);
	unsigned int k = irq_lock();
	uint32_t aa = lll_conn_q2_aa, ses = lll_conn_q2_session;
	irq_unlock(k);
	printk("Q3FSU-DONE role=C aa=0x%08x sess=%u status=0x%02x spacing=%u types=0x%x phys=0x%x initiator=%d\n",
	       aa, ses, p->status, p->frame_space, p->spacing_types, p->phys, (int)p->initiator);
}
#endif

#if defined(CONFIG_APP_Q3_MODE)
static uint32_t q3_fsu_seq;   /* # of accepted 'F' requests issued (mid-capture) */
/* one runtime 'F': issue a SINGLE frame-space request + a machine-readable record.
 * Reject cleanly (no stack call) if not connected / no-request build / duplicate. */
static void q3_handle_F(void)
{
	if (!default_conn) { printk("Q3FSU-REJECT role=C reason=no-conn\n"); return; }
#if CONFIG_APP_FSU_MAX_US > 0
	if (q3_fsu_seq > 0) { printk("Q3FSU-REJECT role=C reason=duplicate seq=%u\n", (unsigned)q3_fsu_seq); return; }
	unsigned int k = irq_lock();
	uint32_t aa = lll_conn_q2_aa, ses = lll_conn_q2_session;
	irq_unlock(k);
	const struct bt_conn_le_frame_space_update_param fp = {
		.phys = BT_HCI_LE_FRAME_SPACE_UPDATE_PHY_2M_MASK,
		.spacing_types = BT_HCI_LE_FRAME_SPACE_UPDATE_SPACING_TYPE_IFS_ACL_CP_MASK |
				 BT_HCI_LE_FRAME_SPACE_UPDATE_SPACING_TYPE_IFS_ACL_PC_MASK,
		.frame_space_min = CONFIG_APP_FSU_MIN_US,
		.frame_space_max = CONFIG_APP_FSU_MAX_US,
	};
	int rc = bt_conn_le_frame_space_update(default_conn, &fp);
	printk("Q3FSU-REQ role=C aa=0x%08x sess=%u min=%u max=%u phys=0x%x types=0x%x rc=%d seq=%u\n",
	       aa, ses, CONFIG_APP_FSU_MIN_US, CONFIG_APP_FSU_MAX_US,
	       (unsigned)fp.phys, (unsigned)fp.spacing_types, rc, (unsigned)(++q3_fsu_seq));
#else
	printk("Q3FSU-REJECT role=C reason=no-request-build\n");
#endif
}
#endif /* CONFIG_APP_Q3_MODE */

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("CENTRAL connect fail 0x%02x\n", err);
		bt_conn_unref(default_conn); default_conn = NULL;
		bt_le_scan_start(BT_LE_SCAN_ACTIVE, scan_cb);
		return;
	}
	struct bt_conn_info info; bt_conn_get_info(conn, &info);
	/* The map was pinned BEFORE connecting (see main), so the connection is
	 * created directly with the 2-channel map -- no LL channel-map-update
	 * transition/instant. The controller Q2CONN print logs the ACTUAL applied
	 * map + chan_count (must read 2). */
	printk("CENTRAL connected interval=%uus PHY-req-2M ; map pinned pre-connect ; sess=%u aa=0x%08x\n",
	       info.le.interval * 1250, lll_conn_q2_session, lll_conn_q2_aa);
	/* 2M port: AUTO_PHY_UPDATE is off, so drive the switch to 2M explicitly (observer
	 * decodes 2M). tIFS is interval/PHY-independent to measure, but the link must be 2M
	 * for the 2M FSU arm + the 2M gap_proxy calibration (offset 383). */
	int pr = bt_conn_le_phy_update(conn, BT_CONN_LE_PHY_PARAM_2M);
	printk("CENTRAL phy_update->2M rc=%d\n", pr);
	/* NO auto FSU here: the runner triggers it via 'F' after CAPTURE-START (Q3). */
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("CENTRAL disconnected 0x%02x -> rescan\n", reason);
	bt_conn_unref(default_conn); default_conn = NULL;
	bt_le_scan_start(BT_LE_SCAN_ACTIVE, scan_cb);
}

/* rev-6: the peripheral's auto connection-parameter update (~5 s post-connect) is the
 * startup transient that resets the negotiated frame space in ull_conn_update_parameters().
 * Emit ONE machine-readable record per connection so the runner can EVENT-GATE the steady
 * ABBA sequence on it (request FSU only AFTER this update). Bound to the connection
 * AA/session; the printk runs BEFORE the settle + measurement window (no path perturbation). */
static uint32_t q3_paramupd_seq;    /* # updates seen: the runner requires EXACTLY one */
static void le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
			     uint16_t timeout)
{
	ARG_UNUSED(conn);
	unsigned int k = irq_lock();
	uint32_t aa = lll_conn_q2_aa, ses = lll_conn_q2_session;
	irq_unlock(k);
	/* emit EVERY update (with a seq) so the runner can detect a second one that would
	 * silently reset FSU during the window -- "no later update" is verified, not assumed. */
	printk("Q3PARAMUPD role=C aa=0x%08x sess=%u interval=%u latency=%u timeout=%u seq=%u\n",
	       aa, ses, (unsigned)interval, (unsigned)latency, (unsigned)timeout,
	       (unsigned)(++q3_paramupd_seq));
}

BT_CONN_CB_DEFINE(cbs) = {
	.connected = connected, .disconnected = disconnected,
	.le_param_updated = le_param_updated,
#if defined(CONFIG_BT_FRAME_SPACE_UPDATE)
	.frame_space_updated = fsu_updated,
#endif
};

int main(void)
{
	const struct device *ucon = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	int err = bt_enable(NULL);
	uint32_t boottag = k_cycle_get_32();  /* boot TAG (not a guaranteed-unique nonce) */
	printk("\n== Q2 central == bt_enable=%d\n", err);
	printk("Q2READY role=C dev=%08x%08x boottag=0x%08x\n",
	       (unsigned)NRF_FICR->INFO.DEVICEID[1], (unsigned)NRF_FICR->INFO.DEVICEID[0],
	       (unsigned)boottag);
	/* wait for the runner's CONNECT command 'C' (issued only AFTER the
	 * peripheral is advertising) so startup is deterministic (no reset race). */
	{ unsigned char cc = 0; while (cc != 'C') { if (uart_poll_in(ucon, &cc) != 0) { cc = 0; k_msleep(2); } } }
	if (!err) {
		/* Pin the 2-channel map BEFORE connecting (skipped if Q2_NO_CHANMAP,
		 * for the 37-channel diagnostic). */
#ifndef Q2_NO_CHANMAP
		err = bt_le_set_chan_map((uint8_t *)Q2_CHAN_MAP);
		printk("CENTRAL pin chan-map {10,11} pre-connect err=%d ; obs ch=%u\n",
		       err, Q2_OBS_CHAN);
#else
		printk("CENTRAL chan-map NOT pinned (37-channel diagnostic)\n");
#endif
		err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, scan_cb);
		printk("CENTRAL scan err=%d\n", err);
	}
	/* report the ground-truth per-channel TX-event count so the observer's
	 * retention denominator is independent of what the observer saw. */
	/* Command channel: on 'S' from the runner, emit an ATOMIC Q2SNAP (the
	 * denominator boundary). Q2EVT every ~100ms is kept for link-health/debug.
	 * The runner takes START snaps BEFORE observer GO and END snaps after
	 * FROZEN -> the endpoint interval CONTAINS the observer interval. */
	uint32_t snap_seq = 0; int64_t last_evt = 0;
	while (1) {
		unsigned char c;
		if (uart_poll_in(ucon, &c) == 0) {
			if (c == 'S') {
				unsigned int k = irq_lock();
				uint32_t aa = lll_conn_q2_aa, ses = lll_conn_q2_session;
				uint32_t sc = lll_conn_q2_evt[Q2_OBS_CHAN], tx = lll_conn_q2_tx[Q2_OBS_CHAN];
				irq_unlock(k);
				printk("Q2SNAP role=C seq=%u boottag=0x%08x aa=0x%08x sess=%u ch=%u sched=%u tx=%u\n",
				       snap_seq++, (unsigned)boottag, aa, ses, Q2_OBS_CHAN, sc, tx);
			}
#if defined(CONFIG_APP_Q3_MODE)
			else if (c == 'F') { q3_handle_F(); }   /* runner's mid-capture FSU trigger */
			else if (c == 'M') {
				/* Q3 F-time PHASE snapshot: the same free-running counters as 'S',
				 * printed AT the transition so the analyzer can split the ch10 TX
				 * denominator into pre (START->M) and post (M->END) phases for
				 * phase-SPECIFIC retention. Exactly one per run (no seq needed). */
				unsigned int k = irq_lock();
				uint32_t aa = lll_conn_q2_aa, ses = lll_conn_q2_session;
				uint32_t sc = lll_conn_q2_evt[Q2_OBS_CHAN], tx = lll_conn_q2_tx[Q2_OBS_CHAN];
				irq_unlock(k);
				printk("Q3PHASESNAP role=C boottag=0x%08x aa=0x%08x sess=%u ch=%u sched=%u tx=%u\n",
				       (unsigned)boottag, aa, ses, Q2_OBS_CHAN, sc, tx);
			}
#endif
		}
		int64_t now = k_uptime_get();
		if (now - last_evt >= 100) {
			last_evt = now;
			unsigned int k = irq_lock();
			uint32_t aa = lll_conn_q2_aa, ses = lll_conn_q2_session;
			uint32_t sc = lll_conn_q2_evt[Q2_OBS_CHAN], tx = lll_conn_q2_tx[Q2_OBS_CHAN];
			irq_unlock(k);
			printk("Q2EVT role=C aa=0x%08x sess=%u t=%lldms ch=%u sched=%u tx=%u\n",
			       aa, ses, now, Q2_OBS_CHAN, sc, tx);
		}
		k_msleep(2);
	}
	return 0;
}
