/*
 * nRF54L15-DK — L2CAP CoC latency-under-load SINK/echo (Test B', 2026-08-25).
 * Counterpart to coclat-central. Receives the 480-byte bulk (counts KB/s) AND echoes the
 * tiny PING stop-signals straight back on the same CoC channel. The echo rides the reverse
 * (credit-granted) path; under saturated downlink it competes for the peripheral's TX slot,
 * so the central's measured ping RTT reflects real CoC backpressure under load.
 *
 * Ping vs bulk: distinguished by sdu_len (PING_SIZE=20 vs 480). Echo is decoupled to a thread
 * (not sent from the RX callback) for safety; the wakeup adds negligible (~us) jitter vs ms RTT.
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
#define L2CAP_MPS   247
#define RX_CREDITS  64
#define PING_SIZE   20
#define SUMMARY_MS  1000

static struct bt_l2cap_le_chan le_chan;
NET_BUF_POOL_FIXED_DEFINE(tx_pool, 8, BT_L2CAP_SDU_BUF_SIZE(L2CAP_MTU), 8, NULL);
static struct bt_conn *default_conn;
static volatile uint32_t rx_bytes, rx_total, rx_segs;
static volatile bool chan_up;
static volatile uint32_t echoes, echo_fail;

/* single-slot echo hand-off (pings are serialized by the central, so one outstanding) */
static uint8_t echo_buf[PING_SIZE];
static K_SEM_DEFINE(echo_sem, 0, 1);

/* outstanding-credit count; reset whenever a fresh window is granted (stale across reconnects = credit leak) */
static int32_t avail = RX_CREDITS;
static void seg_recv(struct bt_l2cap_chan *chan, size_t sdu_len, off_t seg_offset,
		     struct net_buf_simple *seg)
{
	if (sdu_len == PING_SIZE && seg_offset == 0 && seg->len >= 8 &&
	    memcmp(seg->data, "PING", 4) == 0) {
		/* stop-signal: hand off to the echo thread */
		memcpy(echo_buf, seg->data, PING_SIZE);
		k_sem_give(&echo_sem);
	} else {
		/* bulk: count goodput */
		rx_bytes += seg->len;
		rx_total += seg->len;
		rx_segs++;
	}
	if (--avail < 24) { bt_l2cap_chan_give_credits(chan, RX_CREDITS - avail); avail = RX_CREDITS; }
}

static void chan_connected(struct bt_l2cap_chan *chan)
{
	ARG_UNUSED(chan);
	chan_up = true; rx_bytes = 0; rx_total = 0; rx_segs = 0;
	printk("CoC up — coclat sink ready (RX_CREDITS=%u, MPS=%u)\n", RX_CREDITS, L2CAP_MPS);
}
static void chan_disconnected(struct bt_l2cap_chan *chan) { ARG_UNUSED(chan); chan_up = false; printk("CoC down\n"); }

static const struct bt_l2cap_chan_ops chan_ops = {
	.connected = chan_connected, .disconnected = chan_disconnected, .seg_recv = seg_recv,
};
static int l2cap_accept(struct bt_conn *conn, struct bt_l2cap_server *server, struct bt_l2cap_chan **chan)
{
	ARG_UNUSED(conn); ARG_UNUSED(server);
	le_chan.chan.ops = &chan_ops;
	le_chan.rx.mtu = L2CAP_MTU; le_chan.rx.mps = L2CAP_MPS;
	*chan = &le_chan.chan;
	/* seg_recv: the host never resets rx.credits, so a reused channel would send the previous
	 * connection's leftover credits as this connection's initial credits */
	atomic_set(&le_chan.rx.credits, 0);
	avail = RX_CREDITS;
	bt_l2cap_chan_give_credits(&le_chan.chan, RX_CREDITS);
	return 0;
}
static struct bt_l2cap_server server = { .psm = L2CAP_PSM, .sec_level = BT_SECURITY_L1, .accept = l2cap_accept };

/* ---- echo thread: send the stashed ping back on the reverse (credit-granted) path ---- */
static void echo_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	while (1) {
		k_sem_take(&echo_sem, K_FOREVER);
		if (!chan_up) { continue; }
		struct net_buf *buf = net_buf_alloc(&tx_pool, K_MSEC(100));
		if (!buf) { echo_fail++; continue; }
		net_buf_reserve(buf, BT_L2CAP_SDU_CHAN_SEND_RESERVE);
		net_buf_add_mem(buf, echo_buf, PING_SIZE);
		int e = bt_l2cap_chan_send(&le_chan.chan, buf);
		if (e < 0) { net_buf_unref(buf); echo_fail++; } else { echoes++; }
	}
}
K_THREAD_DEFINE(echo_tid, 2048, echo_fn, NULL, NULL, NULL, 8, 0, 0);   /* high prio: echo promptly */

/* ---- advertising ---- */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};
static struct k_work_delayable adv_work;
static void adv_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) { printk("adv start failed (%d)\n", err); k_work_reschedule(&adv_work, K_MSEC(200)); }
	else { printk("Advertising as '%s'\n", CONFIG_BT_DEVICE_NAME); }
}
static void start_adv(void) { k_work_reschedule(&adv_work, K_NO_WAIT); }

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) { printk("GAP connect failed (0x%02x)\n", err); return; }
	default_conn = bt_conn_ref(conn);
	struct bt_conn_info info;
	if (bt_conn_get_info(conn, &info) == 0) { printk("GAP connected: interval=%u\n", info.le.interval); }
}
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	printk("GAP disconnected (0x%02x)\n", reason);
	if (default_conn) { bt_conn_unref(default_conn); default_conn = NULL; }
	chan_up = false; start_adv();
}
static void le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *p) { ARG_UNUSED(conn); printk("PHY updated: tx=%u rx=%u\n", p->tx_phy, p->rx_phy); }
static void le_data_len_updated(struct bt_conn *conn, struct bt_conn_le_data_len_info *i) { ARG_UNUSED(conn); printk("DLE updated: tx_max=%u rx_max=%u\n", i->tx_max_len, i->rx_max_len); }

static volatile uint16_t fsu_spacing;
#if defined(CONFIG_BT_FRAME_SPACE_UPDATE)
static void fsu_updated(struct bt_conn *conn, const struct bt_conn_le_frame_space_updated *p)
{
	ARG_UNUSED(conn);
	if (p->status == 0) { fsu_spacing = p->frame_space; }
}
#endif
BT_CONN_CB_DEFINE(conn_cbs) = {
	.connected = connected, .disconnected = disconnected,
	.le_phy_updated = le_phy_updated, .le_data_len_updated = le_data_len_updated,
#if defined(CONFIG_BT_FRAME_SPACE_UPDATE)
	.frame_space_updated = fsu_updated,
#endif
};

static void sum_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	while (1) {
		k_msleep(SUMMARY_MS);
		if (chan_up) {
			uint32_t bb = rx_bytes; rx_bytes = 0;
			printk("SINK rx: %u KB/s (%u B / %u ms) fsu=%u | cum_total=%u B cum_segs=%u | echoes=%u fail=%u\n",
			       bb / 1024, bb, SUMMARY_MS, (unsigned)fsu_spacing, rx_total, rx_segs, echoes, echo_fail);
		}
	}
}
K_THREAD_DEFINE(sum_tid, 1024, sum_fn, NULL, NULL, NULL, 10, 0, 0);

int main(void)
{
	int err = bt_enable(NULL);
	if (err) { printk("bt_enable failed (%d)\n", err); return 0; }
	printk("\n=== CoC latency-under-load SINK/echo (Zephyr 4.4.1, seg_recv) ===\n");
	bt_l2cap_server_register(&server);
	printk("L2CAP server on PSM 0x%04x\n", L2CAP_PSM);
	k_work_init_delayable(&adv_work, adv_work_fn);
	start_adv();
	return 0;
}
