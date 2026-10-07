/*
 * nRF52832 DK — BLE 5 L2CAP CoC echo server (Zephyr)
 *
 * Peripheral counterpart to ../test-l2cap-echo.py (Linux central over the
 * TP-Link BT 5.4 dongle). Opens an L2CAP CoC server on PSM 0x80, echoes
 * SDUs, and honours the same STREAM / SINK / ECHO commands the Python
 * client sends, so the exact same test harness works against this device.
 *
 * Because the nRF52 is BLE 5 (unlike the classic ESP32 = BT 4.2, 1M-only),
 * this enables the two knobs that raise raw throughput:
 *   - LE 2M PHY            (CONFIG_BT_AUTO_PHY_UPDATE)
 *   - Data Length Extension (251-byte PDUs, CONFIG_BT_*_DATA_LEN*)
 * plus a fast connection interval requested on connect.
 */

/* Zephyr 2.7.x (as bundled by PlatformIO's framework-zephyr) — pre-3.0
 * include layout: no "zephyr/" prefix. */
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
#define STREAM_SDUS 200

/* SDU buffers for RX reassembly and TX. */
/* Zephyr 2.7: NET_BUF_POOL_FIXED_DEFINE(name, count, data_size, destroy)
 * tx_pool is deliberately small (3): it hard-bounds how many SDUs can be
 * in flight at once. A 480-byte SDU is ~2 L2CAP segments, so <=3 SDUs keeps
 * us under CONFIG_BT_CONN_TX_MAX (10) — the sender can never over-queue and
 * exhaust the TX-context pool, which is what wedged the STREAM burst. */
NET_BUF_POOL_FIXED_DEFINE(tx_pool, 3, BT_L2CAP_SDU_BUF_SIZE(L2CAP_MTU), NULL);
NET_BUF_POOL_FIXED_DEFINE(rx_pool, 10, BT_L2CAP_SDU_BUF_SIZE(L2CAP_MTU), NULL);

struct app_chan {
	struct bt_l2cap_le_chan le;
	bool active;
	bool sink_mode;
};

static struct app_chan g_chan;
static struct bt_conn *default_conn;
static K_SEM_DEFINE(stream_sem, 0, 1);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void start_adv(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("adv start failed (%d)\n", err);
	} else {
		printk("Advertising as '%s'\n", CONFIG_BT_DEVICE_NAME);
	}
}

/* ---- L2CAP channel callbacks ---- */

static struct net_buf *chan_alloc_buf(struct bt_l2cap_chan *chan)
{
	ARG_UNUSED(chan);
	return net_buf_alloc(&rx_pool, K_FOREVER);
}

/* Echo is decoupled from the RX thread. chan_recv() only enqueues the SDU
 * here; echo_fn() (its own thread) does the actual bt_l2cap_chan_send().
 * Sending from the recv callback deadlocks: that same thread frees TX buffers
 * via the HCI "packets complete" events, so blocking it in a send starves the
 * buffers the send is waiting on ("Unable to allocate TX context"). */
static K_FIFO_DEFINE(echo_fifo);

static void echo_enqueue(const uint8_t *data, uint16_t len)
{
	struct net_buf *buf = net_buf_alloc(&tx_pool, K_NO_WAIT);
	if (!buf) {
		printk("echo drop (no buf)\n");
		return;
	}
	net_buf_reserve(buf, BT_L2CAP_SDU_CHAN_SEND_RESERVE);
	net_buf_add_mem(buf, data, len);
	net_buf_put(&echo_fifo, buf);
}

static int chan_recv(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
	uint16_t len = buf->len;
	const uint8_t *d = buf->data;

	if (len == 6 && memcmp(d, "STREAM", 6) == 0) {
		printk("STREAM requested\n");
		k_sem_give(&stream_sem);
		return 0;
	}
	if (len >= 4 && memcmp(d, "SINK", 4) == 0) {
		g_chan.sink_mode = true;
		printk("SINK mode\n");
		return 0;
	}
	if (len >= 4 && memcmp(d, "ECHO", 4) == 0) {
		g_chan.sink_mode = false;
		printk("ECHO mode\n");
		return 0;
	}

	if (g_chan.sink_mode) {
		/* Measure receive throughput (used by the nRF52840 dongle central
		 * blast test). Prints a rolling ~1 s KB/s figure. */
		static int64_t t0;
		static uint32_t acc;
		if (acc == 0) {
			t0 = k_uptime_get();
		}
		acc += len;
		int64_t dt = k_uptime_get() - t0;
		if (dt >= 1000) {
			printk("SINK rx: %lld KB/s  (%u bytes / %lld ms)\n",
			       (int64_t)acc * 1000 / dt / 1024, acc, dt);
			acc = 0;
		}
		return 0; /* receive-only: no echo */
	}

	echo_enqueue(d, len); /* hand off to echo_fn thread, never send here */
	return 0;
}

static void chan_connected(struct bt_l2cap_chan *chan)
{
	struct bt_l2cap_le_chan *le =
		CONTAINER_OF(chan, struct bt_l2cap_le_chan, chan);
	g_chan.active = true;
	printk("L2CAP connected: tx.mtu=%u rx.mtu=%u\n", le->tx.mtu, le->rx.mtu);
}

static void chan_disconnected(struct bt_l2cap_chan *chan)
{
	ARG_UNUSED(chan);
	g_chan.active = false;
	printk("L2CAP disconnected\n");
}

static const struct bt_l2cap_chan_ops chan_ops = {
	.connected = chan_connected,
	.disconnected = chan_disconnected,
	.recv = chan_recv,
	.alloc_buf = chan_alloc_buf,
};

/* Zephyr 2.7 uses the 2-arg accept callback (no server parameter). */
static int l2cap_accept(struct bt_conn *conn, struct bt_l2cap_chan **chan)
{
	ARG_UNUSED(conn);
	printk("L2CAP accept\n");
	g_chan.le.chan.ops = &chan_ops;
	g_chan.le.rx.mtu = L2CAP_MTU;
	*chan = &g_chan.le.chan;
	return 0;
}

static struct bt_l2cap_server server = {
	.psm = L2CAP_PSM,
	.sec_level = BT_SECURITY_L1,
	.accept = l2cap_accept,
};

/* ---- GAP connection callbacks ---- */

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("GAP connect failed (0x%02x)\n", err);
		return;
	}
	default_conn = bt_conn_ref(conn);

	struct bt_conn_info info;
	if (bt_conn_get_info(conn, &info) == 0) {
		printk("GAP connected: interval=%u (%u.%02u ms)\n",
		       info.le.interval, (info.le.interval * 5) / 4,
		       ((info.le.interval * 5) % 4) * 25);
	} else {
		printk("GAP connected\n");
	}

	/* Request the BLE 5 throughput knobs; the central may or may not grant. */
	bt_conn_le_phy_update(conn, BT_CONN_LE_PHY_PARAM_2M);
	bt_conn_le_data_len_update(conn, BT_LE_DATA_LEN_PARAM_MAX);

	struct bt_le_conn_param *cp = BT_LE_CONN_PARAM(6, 6, 0, 400);
	bt_conn_le_param_update(conn, cp);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	printk("GAP disconnected (0x%02x)\n", reason);
	if (default_conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}
	g_chan.active = false;
	g_chan.sink_mode = false;

	/* Drain any echoes still queued from this (possibly wedged) session so
	 * their buffers don't leak and poison the next connection. */
	struct net_buf *b;
	while ((b = net_buf_get(&echo_fifo, K_NO_WAIT)) != NULL) {
		net_buf_unref(b);
	}

	start_adv(); /* re-advertise so the next test run can reconnect */
}

static void le_phy_updated(struct bt_conn *conn,
			   struct bt_conn_le_phy_info *param)
{
	ARG_UNUSED(conn);
	printk("PHY updated: tx=%u rx=%u  (1=1M, 2=2M)\n",
	       param->tx_phy, param->rx_phy);
}

static void le_data_len_updated(struct bt_conn *conn,
				struct bt_conn_le_data_len_info *info)
{
	ARG_UNUSED(conn);
	printk("DLE updated: tx_max_len=%u rx_max_len=%u\n",
	       info->tx_max_len, info->rx_max_len);
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval,
			     uint16_t latency, uint16_t timeout)
{
	ARG_UNUSED(conn);
	/* interval is in 1.25 ms units */
	printk("PARAM: interval=%u (%u.%02u ms) latency=%u timeout=%u\n",
	       interval, (interval * 5) / 4, ((interval * 5) % 4) * 25,
	       latency, timeout);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.le_phy_updated = le_phy_updated,
	.le_data_len_updated = le_data_len_updated,
	.le_param_updated = le_param_updated,
};

/* ---- Echo TX thread: drains echo_fifo, sends off the RX thread ---- */

static void echo_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	while (1) {
		struct net_buf *buf = net_buf_get(&echo_fifo, K_FOREVER);
		if (!g_chan.active) {
			net_buf_unref(buf);
			continue;
		}
		int err;
		do {
			err = bt_l2cap_chan_send(&g_chan.le.chan, buf);
			if (err == -EAGAIN) {
				k_msleep(1); /* no credits yet — back off, retry */
			}
		} while (err == -EAGAIN && g_chan.active);

		if (err < 0) {
			net_buf_unref(buf); /* gave up (disconnect/error) */
		}
		/* on success the stack took ownership of buf */
	}
}

/* Priority 10 keeps this BELOW Zephyr's BT RX (8) and HCI TX (7) threads, so
 * the stack always gets CPU to recycle TX buffers/contexts and process credits
 * — otherwise a burst here starves it and TX contexts never free. */
K_THREAD_DEFINE(echo_tid, 1536, echo_fn, NULL, NULL, NULL, 10, 0, 0);

/* ---- STREAM: unidirectional burst nRF -> Linux ---- */

static void stream_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	static uint8_t payload[SDU_SIZE];

	memset(payload, 'S', sizeof(payload));

	while (1) {
		k_sem_take(&stream_sem, K_FOREVER);
		if (!g_chan.active) {
			continue;
		}
		printk("STREAM: sending %d x %d bytes\n", STREAM_SDUS, SDU_SIZE);
		int64_t t0 = k_uptime_get();
		uint32_t sent = 0;

		for (int i = 0; i < STREAM_SDUS && g_chan.active;) {
			struct net_buf *buf = net_buf_alloc(&tx_pool, K_FOREVER);
			net_buf_reserve(buf, BT_L2CAP_SDU_CHAN_SEND_RESERVE);
			payload[0] = i & 0xFF;
			payload[1] = (i >> 8) & 0xFF;
			net_buf_add_mem(buf, payload, SDU_SIZE);

			int err = bt_l2cap_chan_send(&g_chan.le.chan, buf);
			if (err == -EAGAIN) {
				net_buf_unref(buf);
				k_msleep(1); /* no credits yet — back off, retry */
				continue;
			}
			if (err < 0) {
				net_buf_unref(buf);
				printk("STREAM send err %d\n", err);
				break;
			}
			sent++;
			i++;
		}

		int64_t dt = k_uptime_get() - t0;
		printk("STREAM done: %u SDUs in %lld ms (%lld KB/s)\n", sent, dt,
		       dt > 0 ? ((int64_t)sent * SDU_SIZE) / dt : 0);
	}
}

K_THREAD_DEFINE(stream_tid, 2048, stream_fn, NULL, NULL, NULL, 10, 0, 0);

/* ---- main ---- */

int main(void)
{
	int err = bt_enable(NULL);
	if (err) {
		printk("bt_enable failed (%d)\n", err);
		return 0;
	}
	printk("Bluetooth initialised\n");

	bt_l2cap_server_register(&server);
	printk("L2CAP server on PSM 0x%04x, MTU %u\n", L2CAP_PSM, L2CAP_MTU);

	bt_addr_le_t addr = {0};
	size_t count = 1;
	char addr_s[BT_ADDR_LE_STR_LEN];
	bt_id_get(&addr, &count);
	bt_addr_le_to_str(&addr, addr_s, sizeof(addr_s));
	printk("Identity addr=%s\n", addr_s);

	start_adv();

	return 0;
}
