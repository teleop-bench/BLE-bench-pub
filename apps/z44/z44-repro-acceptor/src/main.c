/*
 * MINIMAL REPRO — L2CAP CoC peripheral (ACCEPTOR + SENDER).
 * Derived from Zephyr's samples/bluetooth/l2cap_coc_acceptor (Apache-2.0), with the
 * only additions being: (1) connectable advertising, (2) a send loop on the accepted
 * channel, (3) a .sent completion counter. Everything else is the stock sample.
 *
 * BUG under test (Zephyr 4.4.1, ll_sw_split, no SoftDevice): after the CENTRAL reboots
 * and re-establishes this channel, bt_l2cap_chan_send() keeps returning success but the
 * .sent callback NEVER fires again — TX is permanently dead until the peripheral reboots.
 * A clean disconnect with the central staying alive recovers fine.
 *
 * Expected healthy output: "sent=.. done=.." both climbing every second, forever,
 *   across the central's reboots.
 * Bug output: after the first central reboot, "done" stops climbing (completions stop);
 *   the pool drains and "alloc-fail" appears.
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define PSM      0x29
#define DATA_MTU 20   /* small — fits the default CoC MTU; the wedge is size-independent */

NET_BUF_POOL_FIXED_DEFINE(data_pool, 4, BT_L2CAP_SDU_BUF_SIZE(DATA_MTU), 8, NULL);

static struct bt_l2cap_chan *g_chan;
static volatile bool chan_up;
static K_SEM_DEFINE(go, 0, 1);
static uint32_t n_sent, n_done, n_allocfail;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void start_adv(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
	printk(err ? "adv start failed %d\n" : "advertising\n", err);
}

/* ---- stock sample server, + .sent counter ---- */
static int l2cap_recv(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
	ARG_UNUSED(chan); ARG_UNUSED(buf);
	return 0;
}
static void l2cap_sent(struct bt_l2cap_chan *chan)
{
	ARG_UNUSED(chan);
	n_done++;               /* SDU completed — this is what stops firing after the bug */
}
static void l2cap_connected(struct bt_l2cap_chan *chan)
{
	printk("L2CAP channel connected\n");
	g_chan = chan;
	n_sent = n_done = n_allocfail = 0;
	chan_up = true;
	k_sem_give(&go);
}
static void l2cap_disconnected(struct bt_l2cap_chan *chan)
{
	ARG_UNUSED(chan);
	printk("L2CAP channel disconnected\n");
	chan_up = false;
}

static struct bt_l2cap_chan_ops l2cap_ops = {
	.recv = l2cap_recv,
	.sent = l2cap_sent,
	.connected = l2cap_connected,
	.disconnected = l2cap_disconnected,
};

/* Must be an le_chan (not plain bt_l2cap_chan) so it has TX/RX endpoint storage —
 * required to *send* from the acceptor. The stock sample is receive-only and uses a
 * plain chan; sending from that returns -EINVAL. */
static struct bt_l2cap_le_chan l2cap_chans[CONFIG_BT_MAX_CONN];

static int accept_cb(struct bt_conn *conn, struct bt_l2cap_server *server,
		     struct bt_l2cap_chan **chan)
{
	uint8_t conn_index = bt_conn_index(conn);
	ARG_UNUSED(server);
	l2cap_chans[conn_index] = (struct bt_l2cap_le_chan){ .chan.ops = &l2cap_ops };
	/* 23 = the spec minimum LE MTU; Zephyr >= 4.4.2 rejects a smaller value in the connection response (the original
	 * DATA_MTU 20 made the initiator disconnect at once). The 20-byte messages are unchanged. */
	l2cap_chans[conn_index].rx.mtu = 23;
	*chan = &l2cap_chans[conn_index].chan;
	return 0;
}

static struct bt_l2cap_server server = {
	.psm = PSM,
	.sec_level = BT_SECURITY_L1,
	.accept = accept_cb,
};

static void conn_disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	printk("GAP disconnected (0x%02x) — re-advertising once the connection is freed\n", reason);
}
/* restart advertising only after the connection object is released (restarting in .disconnected fails with -ENOMEM) */
static void conn_recycled(void) { start_adv(); }
BT_CONN_CB_DEFINE(conn_cbs) = { .disconnected = conn_disconnected, .recycled = conn_recycled };

/* ---- send loop (the only real addition) ---- */
static void send_task(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	static const uint8_t msg[DATA_MTU] = { 0 };
	while (1) {
		k_sem_take(&go, K_FOREVER);
		while (chan_up) {
			struct net_buf *buf = net_buf_alloc(&data_pool, K_NO_WAIT);
			if (!buf) {          /* pool drained => prior SDUs never completed */
				n_allocfail++;
				k_sleep(K_MSEC(20));
				continue;
			}
			net_buf_reserve(buf, BT_L2CAP_SDU_CHAN_SEND_RESERVE);
			net_buf_add_mem(buf, msg, sizeof(msg));
			int e = bt_l2cap_chan_send(g_chan, buf);
			if (e < 0) {
				net_buf_unref(buf);
				if (e != -ENOTCONN) { printk("send err %d\n", e); }
				k_sleep(K_MSEC(20));
				continue;
			}
			n_sent++;
			k_sleep(K_MSEC(20));
		}
	}
}
K_THREAD_DEFINE(send_tid, 1024, send_task, NULL, NULL, NULL, 7, 0, 0);

static void report_task(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	while (1) {
		k_sleep(K_SECONDS(1));
		if (chan_up || n_sent) {
			printk("sent=%u done=%u alloc-fail=%u  %s\n", n_sent, n_done,
			       n_allocfail,
			       (n_sent > n_done + 4) ? "<-- TX WEDGED (completions stalled)" : "");
		}
	}
}
K_THREAD_DEFINE(report_tid, 1024, report_task, NULL, NULL, NULL, 7, 0, 0);

int main(void)
{
	int err = bt_enable(NULL);
	if (err) { printk("bt_enable %d\n", err); return err; }
	bt_l2cap_server_register(&server);
	printk("\n=== MINIMAL REPRO acceptor+sender, PSM 0x%x ===\n", PSM);
	start_adv();
	return 0;
}
