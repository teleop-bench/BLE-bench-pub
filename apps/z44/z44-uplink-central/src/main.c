/*
 * nRF52840 dongle — BLE 5 L2CAP CoC UPLINK SINK (central).
 *
 * Counterpart to z44-uplink-dk (#46073 re-test). Scans for the DK peripheral,
 * drives 2M -> DLE -> opens the CoC channel, then SINKS the peripheral's uplink
 * blast using the seg_recv credit API and reports KB/s. The DK is the device
 * under test; this side just needs to keep credits flowing so the peripheral's
 * TX path stays under pressure.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/l2cap.h>

#define L2CAP_PSM   0x0080
#define L2CAP_MTU   512
#define L2CAP_MPS   247
#define RX_CREDITS  20
#define TARGET_NAME "zenoh-nrf-l2cap"
#define SUMMARY_MS  1000

static struct bt_conn *default_conn;
static struct bt_l2cap_le_chan le_chan;
static volatile bool chan_up;
static volatile uint32_t rx_bytes;
static bool l2cap_started;
static struct k_work_delayable dle_work;

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
	rx_bytes += seg->len;
	bt_l2cap_chan_give_credits(chan, 1);
}

static void chan_connected_cb(struct bt_l2cap_chan *chan)
{
	printk("L2CAP connected — uplink sink ready\n");
	rx_bytes = 0;
	chan_up = true;
	k_work_reschedule(&reboot_work, K_SECONDS(REBOOT_AFTER_S));
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

static void open_l2cap(void)
{
	if (l2cap_started || !default_conn) {
		return;
	}
	l2cap_started = true;
	le_chan.chan.ops = &chan_ops;
	le_chan.rx.mtu = L2CAP_MTU;
	le_chan.rx.mps = L2CAP_MPS;
	/* Initial window goes IN the connection request, and stale rx.credits from a previous connection are dropped first
	 * (seg_recv: the host never resets them). Granting only after connect leaves the peer with 0 initial credits, which
	 * trips a Zephyr 4.4 host TX stall when the peer sends first: the "reconnect wedge" these rigs used to show
	 * (debug-evidence/coc-credit-fixes-20261006, zephyr-l2cap-zero-credit-repro-20261007). */
	atomic_set(&le_chan.rx.credits, 0);
	bt_l2cap_chan_give_credits(&le_chan.chan, RX_CREDITS); /* seg_recv: initial window */
	int e = bt_l2cap_chan_connect(default_conn, &le_chan.chan, L2CAP_PSM);
	printk("l2cap_chan_connect rc=%d\n", e);
}

static void dle_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	open_l2cap();
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
	printk("GAP connected\n");
	l2cap_started = false;
	int e = bt_conn_le_phy_update(conn, BT_CONN_LE_PHY_PARAM_2M);
	printk("phy req rc=%d\n", e);
	k_work_reschedule(&dle_work, K_MSEC(500));
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	printk("GAP disconnected 0x%02x\n", reason);
	if (test_reboot_pending) {
		printk(">>> rebooting now (graceful terminate sent) <<<\n");
		sys_reboot(SYS_REBOOT_COLD);
	}
	k_work_cancel_delayable(&dle_work);
	chan_up = false;
	l2cap_started = false;
	if (default_conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}
	start_scan();
}

static void le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *param)
{
	printk("PHY: tx=%u rx=%u\n", param->tx_phy, param->rx_phy);
	if (param->tx_phy == BT_GAP_LE_PHY_2M && param->rx_phy == BT_GAP_LE_PHY_2M) {
		int e = bt_conn_le_data_len_update(conn, BT_LE_DATA_LEN_PARAM_MAX);
		printk("dle req rc=%d\n", e);
	}
}

static void le_data_len_updated(struct bt_conn *conn,
				struct bt_conn_le_data_len_info *info)
{
	ARG_UNUSED(conn);
	printk("DLE: tx_max=%u rx_max=%u\n", info->tx_max_len, info->rx_max_len);
	k_work_cancel_delayable(&dle_work);
	open_l2cap();
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
			uint32_t b = rx_bytes;
			rx_bytes = 0;
			printk("UPLINK-SINK rx: %u KB/s  (%u bytes/s)\n", b / 1024, b);
		}
	}
}

K_THREAD_DEFINE(sum_tid, 1024, sum_fn, NULL, NULL, NULL, 10, 0, 0);

int main(void)
{
	int err = bt_enable(NULL);
	if (err) {
		printk("bt_enable failed %d\n", err);
		return 0;
	}
	printk("\n=== nRF52840 L2CAP CoC UPLINK SINK (Zephyr 4.4.1) — central-reboot test ===\n");
	k_work_init_delayable(&dle_work, dle_work_fn);
	k_work_init_delayable(&reboot_work, reboot_work_fn);
	start_scan();
	return 0;
}
