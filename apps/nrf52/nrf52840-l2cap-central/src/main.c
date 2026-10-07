/*
 * nRF52840 Dongle — BLE 5 L2CAP CoC *central* throughput driver.
 *
 * Scans for the nRF52832 DK peripheral ("zenoh-nrf-l2cap"), connects AS THE
 * CENTRAL with a fixed 7.5 ms connection interval, forces LE 2M PHY + DLE,
 * opens an L2CAP CoC channel on PSM 0x80, puts the peer in SINK mode, and
 * blasts 480-byte SDUs continuously. The DK peripheral measures and prints the
 * received throughput over its J-Link console.
 *
 * Unlike Linux/BlueZ (which won't shorten the interval and sends ~1-2 packets
 * per event), a Nordic central controls the interval and packs many packets per
 * event — the path to >100 KB/s.
 *
 * Zephyr 2.7 API (PlatformIO framework-zephyr).
 */

#include <zephyr.h>
#include <sys/printk.h>
#include <string.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <bluetooth/gap.h>
#include <bluetooth/l2cap.h>

#define L2CAP_PSM   0x0080
#define L2CAP_MTU   512
#define SDU_SIZE    480
#define TARGET_NAME "zenoh-nrf-l2cap"

NET_BUF_POOL_FIXED_DEFINE(tx_pool, 8, BT_L2CAP_SDU_BUF_SIZE(L2CAP_MTU), NULL);
NET_BUF_POOL_FIXED_DEFINE(rx_pool, 6, BT_L2CAP_SDU_BUF_SIZE(L2CAP_MTU), NULL);

static struct bt_conn *default_conn;
static struct bt_l2cap_le_chan le_chan;
static K_SEM_DEFINE(blast_sem, 0, 1);
static volatile bool chan_up;

/* Connection-setup state. The 2M PHY / DLE updates are negotiated in sequence
 * and *verified* before we open the L2CAP channel — firing them rapid-fire (and
 * racing the peer's own requests) intermittently fails the 2M PHY update
 * (HCI 0x0c "command disallowed"), leaving the link at 1M = ~half throughput.
 * That was the source of the bimodal ~100 vs ~43 KB/s results. */
#define PHY_MAX_RETRY 6
static bool phy_2m_ok;
static bool l2cap_started;
static int  phy_retries;
static struct k_work_delayable phy_work;
static struct k_work_delayable dle_work;

static void start_scan(void);

/* ---- L2CAP client channel ops ---- */

static struct net_buf *chan_alloc_buf(struct bt_l2cap_chan *chan)
{
	ARG_UNUSED(chan);
	return net_buf_alloc(&rx_pool, K_FOREVER);
}

static int chan_recv(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
	ARG_UNUSED(chan);
	ARG_UNUSED(buf);
	return 0; /* SINK test: peer doesn't echo */
}

static void chan_connected_cb(struct bt_l2cap_chan *chan)
{
	struct bt_l2cap_le_chan *le =
		CONTAINER_OF(chan, struct bt_l2cap_le_chan, chan);
	printk("L2CAP connected: tx.mtu=%u rx.mtu=%u\n", le->tx.mtu, le->rx.mtu);
	chan_up = true;
	k_sem_give(&blast_sem);
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
	.recv = chan_recv,
	.alloc_buf = chan_alloc_buf,
};

/* ---- GAP: verified 2M PHY -> DLE -> L2CAP sequencing ---- */

/* Open the L2CAP CoC channel exactly once (idempotent). */
static void open_l2cap(void)
{
	if (l2cap_started || !default_conn) {
		return;
	}
	l2cap_started = true;
	le_chan.chan.ops = &chan_ops;
	le_chan.rx.mtu = L2CAP_MTU;
	int e = bt_l2cap_chan_connect(default_conn, &le_chan.chan, L2CAP_PSM);
	printk("l2cap_chan_connect rc=%d\n", e);
}

/* Request DLE (after 2M is confirmed) and arm a fallback to open the channel
 * even if the DLE-complete callback never arrives. */
static void request_dle(void)
{
	if (!default_conn) {
		return;
	}
	int e = bt_conn_le_data_len_update(default_conn, BT_LE_DATA_LEN_PARAM_MAX);
	printk("dle req rc=%d\n", e);
	k_work_reschedule(&dle_work, K_MSEC(300));
}

/* Retry the 2M PHY update until le_phy_updated confirms it (or we give up and
 * proceed at whatever PHY we have, logging a warning). Spaced so each LL
 * procedure can complete before the next request. */
static void phy_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	if (!default_conn || phy_2m_ok || l2cap_started) {
		return;
	}
	if (phy_retries++ >= PHY_MAX_RETRY) {
		printk("WARN: 2M PHY not confirmed after %d tries; proceeding\n",
		       PHY_MAX_RETRY);
		request_dle();
		return;
	}
	int e = bt_conn_le_phy_update(default_conn, BT_CONN_LE_PHY_PARAM_2M);
	printk("phy retry %d rc=%d\n", phy_retries, e);
	k_work_reschedule(&phy_work, K_MSEC(200));
}

static void dle_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	open_l2cap(); /* fallback: don't stall if DLE callback never fires */
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

	/* Drive 2M PHY first; DLE and the L2CAP channel follow only once 2M is
	 * verified (see le_phy_updated / le_data_len_updated). This removes the
	 * rapid-fire PHY+DLE collision that left some connections at 1M. */
	phy_2m_ok = false;
	l2cap_started = false;
	phy_retries = 0;

	int e = bt_conn_le_phy_update(conn, BT_CONN_LE_PHY_PARAM_2M);
	printk("phy req rc=%d\n", e);
	k_work_reschedule(&phy_work, K_MSEC(200));
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	printk("GAP disconnected 0x%02x\n", reason);
	k_work_cancel_delayable(&phy_work);
	k_work_cancel_delayable(&dle_work);
	chan_up = false;
	l2cap_started = false;
	if (default_conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}
	start_scan();
}

static void le_phy_updated(struct bt_conn *conn,
			   struct bt_conn_le_phy_info *param)
{
	ARG_UNUSED(conn);
	printk("PHY: tx=%u rx=%u (2=2M)\n", param->tx_phy, param->rx_phy);
	if (param->tx_phy == BT_GAP_LE_PHY_2M &&
	    param->rx_phy == BT_GAP_LE_PHY_2M) {
		phy_2m_ok = true;
		k_work_cancel_delayable(&phy_work);
		request_dle();
	}
	/* else: phy_work re-requests 2M until confirmed or retries exhausted */
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
	if (data->type == BT_DATA_NAME_COMPLETE ||
	    data->type == BT_DATA_NAME_SHORTENED) {
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
	if (type != BT_GAP_ADV_TYPE_ADV_IND &&
	    type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
		return;
	}

	bool match = false;
	bt_data_parse(ad, ad_match_name, &match);
	if (!match) {
		return;
	}

	char s[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, s, sizeof(s));
	printk("found %s (rssi %d), connecting\n", s, rssi);

	bt_le_scan_stop();

	/* Fixed 7.5 ms interval; we are the central so this sticks. */
	struct bt_le_conn_param *param = BT_LE_CONN_PARAM(6, 6, 0, 400);
	int e = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, param,
				  &default_conn);
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

/* ---- blast thread: continuously send 480-byte SDUs to the peer's SINK ---- */

static void blast_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	static uint8_t payload[SDU_SIZE];
	memset(payload, 'X', sizeof(payload));

	while (1) {
		k_sem_take(&blast_sem, K_FOREVER);

		/* put the peer in SINK (receive-only) mode */
		struct net_buf *cmd = net_buf_alloc(&tx_pool, K_FOREVER);
		net_buf_reserve(cmd, BT_L2CAP_SDU_CHAN_SEND_RESERVE);
		net_buf_add_mem(cmd, "SINK\x00\x00", 6);
		bt_l2cap_chan_send(&le_chan.chan, cmd);
		k_msleep(300);

		printk("blasting 480-byte SDUs...\n");
		while (chan_up) {
			struct net_buf *buf = net_buf_alloc(&tx_pool, K_FOREVER);
			net_buf_reserve(buf, BT_L2CAP_SDU_CHAN_SEND_RESERVE);
			net_buf_add_mem(buf, payload, SDU_SIZE);
			int e = bt_l2cap_chan_send(&le_chan.chan, buf);
			if (e == -EAGAIN) {
				net_buf_unref(buf);
				k_msleep(1); /* no credits — back off */
				continue;
			}
			if (e < 0) {
				net_buf_unref(buf);
				break;
			}
		}
	}
}

K_THREAD_DEFINE(blast_tid, 2048, blast_fn, NULL, NULL, NULL, 10, 0, 0);

/* ---- main ---- */

void main(void)
{
	int err = bt_enable(NULL);
	if (err) {
		printk("bt_enable failed %d\n", err);
		return;
	}
	printk("\n=== nRF52840 L2CAP CoC central ===\n");
	k_work_init_delayable(&phy_work, phy_work_fn);
	k_work_init_delayable(&dle_work, dle_work_fn);
	start_scan();
}
