/*
 * nRF54L15-DK — BLE L2CAP CoC UPLINK SINK (central).
 *
 * Counterpart to z54-uplink-dk (a benchmark path #46073-motivated). Scans for the DK
 * peripheral, drives 2M -> DLE -> opens the CoC channel, then SINKS the peripheral's
 * uplink blast using the seg_recv credit API. STAGE 1 = INSTRUMENTATION ONLY: reports
 * SINK-DELIVERED bytes (the primary throughput metric) via an atomic exchange, tagged
 * with a per-boot run id and the controller's shared connection AA so this log pairs
 * with the blaster's. Startup sequencing (PHY-at-connect, DLE-gated CoC open) is UNCHANGED.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/atomic.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/l2cap.h>

/* controller-owned shared connection AA/session (same value on both roles) -> pairs this
 * sink log with the blaster log for the same connection. */
extern volatile uint32_t lll_conn_q2_aa, lll_conn_q2_session;

#define L2CAP_PSM   0x0080
#define L2CAP_MTU   512
#define L2CAP_MPS   247
#define RX_CREDITS  20
#define TARGET_NAME "zenoh-nrf-l2cap"
#define SUMMARY_MS  1000

static struct bt_conn *default_conn;
static struct bt_l2cap_le_chan le_chan;
static volatile bool chan_up;
static atomic_t rx_win;      /* sink bytes this summary window (atomic exchange) */
static atomic_t rx_total;    /* cumulative sink-delivered bytes (wraps 32-bit) */
static uint32_t run_id, boot_tag, sess_aa;
static bool l2cap_started;
/* STAGE 3: serialize PHY->DLE->CoC. Open the CoC ONLY after 2M is confirmed AND the
 * effective TX/RX data length reaches 251 -- never on a bare timer. A setup watchdog
 * retries the missing step instead of opening a marginal channel. */
static bool phy_2m_ok, dle_ok;
static uint32_t setup_retries;
static struct k_work_delayable setup_wd;
#define SETUP_WD_MS      750
#define SETUP_MAX_RETRY  6
#define EFF_LEN_MIN      251

/* ISOLATION TEST: GRACEFUL loss + central reboot (no USB unplug).
 * ~12 s after the channel is up, the central disconnects GRACEFULLY (LL_TERMINATE),
 * then reboots from the disconnected callback. This holds "central reboots" constant
 * and varies only graceful-vs-abrupt vs the known-good graceful+alive result:
 *   - if the reconnected uplink WEDGES -> the REBOOT is the culprit
 *     => robot-out-of-range (abrupt + central alive) likely RECOVERS (good for failover)
 *   - if it RECOVERS -> abruptness was the culprit
 *     => robot-out-of-range likely WEDGES (bad for failover) */
#define REBOOT_AFTER_S 12
static struct k_work_delayable reboot_work;
static volatile bool test_reboot_pending;

static void reboot_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	if (default_conn) {
		printk(">>> central: graceful disconnect, then reboot <<<\n");
		test_reboot_pending = true;
		bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

static void start_scan(void);

/* ---- L2CAP client channel: seg_recv sink ---- */

static void seg_recv(struct bt_l2cap_chan *chan, size_t sdu_len, off_t seg_offset,
		     struct net_buf_simple *seg)
{
	ARG_UNUSED(sdu_len);
	ARG_UNUSED(seg_offset);
	atomic_add(&rx_win, seg->len);
	atomic_add(&rx_total, seg->len);
	bt_l2cap_chan_give_credits(chan, 1);
}

/* DIAGNOSTIC (graceful-disconnect companion): one-shot. After the FIRST session streams
 * ~10 s, gracefully disconnect (LL_TERMINATE) -- central stays up and reconnects. Compares
 * graceful teardown vs the abrupt (supervision-timeout) drop used elsewhere. */
static struct k_work_delayable gdisc_work;
static bool gdisc_fired;
static void gdisc_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	if (default_conn) {
		printk(">>> central GRACEFUL disconnect (LL_TERMINATE) run=%u aa=0x%08x <<<\n", run_id, sess_aa);
		bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

static void chan_connected_cb(struct bt_l2cap_chan *chan)
{
	unsigned int k = irq_lock(); sess_aa = lll_conn_q2_aa; irq_unlock(k);
	atomic_set(&rx_win, 0);
	printk("L2CAP connected — uplink sink ready run=%u sess=%u aa=0x%08x\n",
	       run_id, (unsigned)lll_conn_q2_session, sess_aa);
	bt_l2cap_chan_give_credits(chan, RX_CREDITS); /* seg_recv: initial window */
	chan_up = true;
	if (!gdisc_fired) { gdisc_fired = true; k_work_reschedule(&gdisc_work, K_SECONDS(10)); }
	(void)reboot_work_fn;
}

static void chan_disconnected_cb(struct bt_l2cap_chan *chan)
{
	ARG_UNUSED(chan);
	printk("L2CAP disconnected\n");
	chan_up = false;
}

static const struct bt_l2cap_chan_ops chan_ops = {
	.connected = chan_connected_cb,
	.disconnected = chan_disconnected_cb,
	.seg_recv = seg_recv,
};

/* Open the CoC ONLY when 2M AND effective 251 are both confirmed. */
static void try_open_coc(void)
{
	if (l2cap_started || !default_conn || !phy_2m_ok || !dle_ok) {
		return;
	}
	l2cap_started = true;
	le_chan.chan.ops = &chan_ops;
	le_chan.rx.mtu = L2CAP_MTU;
	le_chan.rx.mps = L2CAP_MPS;
	int e = bt_l2cap_chan_connect(default_conn, &le_chan.chan, L2CAP_PSM);
	printk("l2cap_chan_connect rc=%d (after 2M + eff251) run=%u aa=0x%08x\n", e, run_id, sess_aa);
}

/* Setup watchdog: NEVER opens a marginal CoC on a timer -- it RETRIES the missing
 * PHY/DLE step until both are confirmed (bounded), then try_open_coc() proceeds. */
static void setup_wd_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	if (l2cap_started || !default_conn) {
		return;
	}
	if (setup_retries++ >= SETUP_MAX_RETRY) {
		printk("setup STALLED: 2m=%d dle=%d retries=%u (no CoC opened) run=%u aa=0x%08x\n",
		       phy_2m_ok, dle_ok, setup_retries, run_id, sess_aa);
		return;   /* leave the connection idle rather than open a bad channel */
	}
	if (!phy_2m_ok) {
		int e = bt_conn_le_phy_update(default_conn, BT_CONN_LE_PHY_PARAM_2M);
		printk("setup retry: phy req rc=%d (2m=%d)\n", e, phy_2m_ok);
	} else if (!dle_ok) {
		int e = bt_conn_le_data_len_update(default_conn, BT_LE_DATA_LEN_PARAM_MAX);
		printk("setup retry: dle req rc=%d (dle=%d)\n", e, dle_ok);
	}
	k_work_reschedule(&setup_wd, K_MSEC(SETUP_WD_MS));
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("connect failed 0x%02x\n", err);
		bt_conn_unref(default_conn);
		default_conn = NULL;
		start_scan();
		return;
	}
	unsigned int k = irq_lock(); sess_aa = lll_conn_q2_aa; irq_unlock(k);
	struct bt_conn_info info; char addr[BT_ADDR_LE_STR_LEN] = "?";
	if (bt_conn_get_info(conn, &info) == 0) { bt_addr_le_to_str(info.le.dst, addr, sizeof(addr)); }
	printk("GAP connected: peer=%s run=%u sess=%u aa=0x%08x\n",
	       addr, run_id, (unsigned)lll_conn_q2_session, sess_aa);
	/* STAGE 3: serialized bring-up. Reset the setup state, request PHY, and let the
	 * watchdog drive PHY->DLE->CoC. NO timer-based premature CoC open. */
	l2cap_started = false; phy_2m_ok = false; dle_ok = false; setup_retries = 0;
	int e = bt_conn_le_phy_update(conn, BT_CONN_LE_PHY_PARAM_2M);
	printk("phy req rc=%d\n", e);
	k_work_reschedule(&setup_wd, K_MSEC(SETUP_WD_MS));
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	printk("GAP disconnected 0x%02x run=%u aa=0x%08x\n", reason, run_id, sess_aa);
	if (test_reboot_pending) {
		printk(">>> rebooting now (graceful terminate sent) <<<\n");
		sys_reboot(SYS_REBOOT_COLD);
	}
	k_work_cancel_delayable(&setup_wd);
	chan_up = false;
	l2cap_started = false; phy_2m_ok = false; dle_ok = false;
	if (default_conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}
	start_scan();
}

static void le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *param)
{
	printk("PHY: tx=%u rx=%u run=%u\n", param->tx_phy, param->rx_phy, run_id);
	if (param->tx_phy == BT_GAP_LE_PHY_2M && param->rx_phy == BT_GAP_LE_PHY_2M) {
		phy_2m_ok = true;
		int e = bt_conn_le_data_len_update(conn, BT_LE_DATA_LEN_PARAM_MAX);
		printk("dle req rc=%d\n", e);
	}
}

static void le_data_len_updated(struct bt_conn *conn,
				struct bt_conn_le_data_len_info *info)
{
	ARG_UNUSED(conn);
	printk("DLE: tx_max=%u rx_max=%u run=%u\n", info->tx_max_len, info->rx_max_len, run_id);
	/* STAGE 3: require the EFFECTIVE length to reach 251 both directions before CoC. */
	dle_ok = (info->tx_max_len >= EFF_LEN_MIN && info->rx_max_len >= EFF_LEN_MIN);
	try_open_coc();
}

BT_CONN_CB_DEFINE(conn_cbs) = {
	.connected = connected,
	.disconnected = disconnected,
	.le_phy_updated = le_phy_updated,
	.le_data_len_updated = le_data_len_updated,
};

/* ---- scan ---- */

static bool ad_match_name(struct bt_data *data, void *user_data)
{
	bool *match = user_data;
	if (data->type == BT_DATA_NAME_COMPLETE || data->type == BT_DATA_NAME_SHORTENED) {
		if (data->data_len == strlen(TARGET_NAME) &&
		    memcmp(data->data, TARGET_NAME, data->data_len) == 0) {
			*match = true;
			return false;
		}
	}
	return true;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	if (default_conn) {
		return;
	}
	if (type != BT_GAP_ADV_TYPE_ADV_IND && type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
		return;
	}
	bool match = false;
	bt_data_parse(ad, ad_match_name, &match);
	if (!match) {
		return;
	}
	bt_le_scan_stop();
	struct bt_le_conn_param *param = BT_LE_CONN_PARAM(6, 6, 0, 400);
	int e = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, param, &default_conn);
	if (e) {
		printk("create conn rc=%d\n", e);
		start_scan();
	}
}

static void start_scan(void)
{
	struct bt_le_scan_param sp = {
		.type = BT_LE_SCAN_TYPE_ACTIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};
	int e = bt_le_scan_start(&sp, device_found);
	printk("scan start rc=%d\n", e);
}

/* ---- summary thread ---- */

static void sum_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	while (1) {
		k_msleep(SUMMARY_MS);
		if (chan_up) {
			uint32_t b = atomic_clear(&rx_win);   /* atomic exchange: window bytes */
			printk("UPLINK-SINK rx: %u KiB/s (win_bytes=%u total=%ld) | run=%u sess=%u aa=0x%08x\n",
			       b / 1024, b, (long)atomic_get(&rx_total), run_id,
			       (unsigned)lll_conn_q2_session, sess_aa);
		}
	}
}

K_THREAD_DEFINE(sum_tid, 1024, sum_fn, NULL, NULL, NULL, 10, 0, 0);

int main(void)
{
	boot_tag = k_cycle_get_32();
	int err = bt_enable(NULL);
	if (err) {
		printk("bt_enable failed %d\n", err);
		return 0;
	}
	run_id = k_cycle_get_32();
	printk("\n=== nRF54L15 L2CAP CoC UPLINK SINK (Zephyr 4.4.1) run=%u boot_tag=0x%08x ===\n",
	       run_id, boot_tag);
	k_work_init_delayable(&setup_wd, setup_wd_fn);
	k_work_init_delayable(&gdisc_work, gdisc_work_fn);
	k_work_init_delayable(&reboot_work, reboot_work_fn);
	start_scan();
	return 0;
}
