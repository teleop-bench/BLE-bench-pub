/*
 * nRF52840 Dongle — BLE 5 L2CAP CoC *central* throughput driver.
 * Zephyr 4.4.1 port of nrf52840-l2cap-central (was Zephyr 2.7 / PlatformIO).
 *
 * Ported changes vs 2.7: <zephyr/...> include paths; simplified 2M->DLE->L2CAP
 * sequencing (the LLCP rewrite + fixed PHY-collision handling remove the need for
 * the old verify-and-retry hack). SINK: peer counts bytes, doesn't echo.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/l2cap.h>

#define L2CAP_PSM   0x0080
#define L2CAP_MTU   512
#define SDU_SIZE    244
#define TARGET_NAME "zenoh-nrf-l2cap"

/* Zephyr 4.4: NET_BUF_POOL_FIXED_DEFINE gained a user-data-size arg (here 8, per
 * the in-tree l2cap_coc_initiator sample). The central is a pure sender (SINK), so
 * it needs no RX pool / alloc_buf. */
NET_BUF_POOL_FIXED_DEFINE(tx_pool, 8, BT_L2CAP_SDU_BUF_SIZE(L2CAP_MTU), 8, NULL);

static struct bt_conn *default_conn;
static struct bt_l2cap_le_chan le_chan;
static K_SEM_DEFINE(blast_sem, 0, 1);
static volatile bool chan_up;
static bool l2cap_started;
static struct k_work_delayable dle_work;

static void start_scan(void);

/* ---- L2CAP client channel ops ---- */

static int chan_recv(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
	ARG_UNUSED(chan);
	ARG_UNUSED(buf);
	return 0; /* SINK test: peer doesn't echo */
}

static void chan_connected_cb(struct bt_l2cap_chan *chan)
{
	struct bt_l2cap_le_chan *le = CONTAINER_OF(chan, struct bt_l2cap_le_chan, chan);
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
};

/* ---- GAP: 2M PHY -> DLE -> open L2CAP ---- */

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

static void dle_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	open_l2cap(); /* fallback if the DLE-complete callback never fires */
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
	printk("PHY: tx=%u rx=%u (2=2M)\n", param->tx_phy, param->rx_phy);
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
	char s[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, s, sizeof(s));
	printk("found %s (rssi %d), connecting\n", s, rssi);
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

/* ---- blast thread: continuously send 480-byte SDUs to the peer's SINK ---- */

static void blast_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	static uint8_t payload[SDU_SIZE];
	memset(payload, 'X', sizeof(payload));

	while (1) {
		k_sem_take(&blast_sem, K_FOREVER);

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
				k_msleep(1);
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

int main(void)
{
	int err = bt_enable(NULL);
	if (err) {
		printk("bt_enable failed %d\n", err);
		return 0;
	}
	printk("\n=== nRF52840 L2CAP CoC central (Zephyr 4.4.1) ===\n");
	k_work_init_delayable(&dle_work, dle_work_fn);
	start_scan();
	return 0;
}
