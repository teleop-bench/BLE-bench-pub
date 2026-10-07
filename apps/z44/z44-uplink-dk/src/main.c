/*
 * nRF52832 DK — BLE 5 L2CAP CoC UPLINK BLASTER (peripheral -> central).
 *
 * Purpose: re-test Zephyr issue #46073 on 4.4.1. On 2.7, a peripheral bursting
 * SDUs upstream exhausted the connection TX-context pool and the stuck buffers
 * were NOT reclaimed on disconnect -> the peripheral wedged until a hard reset.
 * 3.7 rewrote the host TX path (removed the BT-TX thread + segment pools), so the
 * failure may no longer reproduce. This firmware:
 *   1) advertises + accepts the central's CoC channel,
 *   2) blasts fixed-size SDUs upstream continuously while the channel is up,
 *   3) reports send rate + a rolling count of blocked sends (wedge signal),
 *   4) on disconnect, frees + re-advertises; a wedge shows as a session that
 *      connects but can no longer send (rate stays 0, blocked count climbs).
 *
 * Run protocol: let it stream, then force several disconnect/reconnect cycles
 * (power-cycle the central). If every fresh session resumes full uplink rate,
 * the TX-context reclaim path is healed on 4.4.1.
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
#define SDU_SIZE    244          /* single-packet; robot-uplink telemetry size */
#define SUMMARY_MS  1000
#define UPTIME_BEFORE_DISCONNECT_S 12  /* clean self-disconnect cadence */

#define TX_WINDOW   4            /* max SDUs in flight (bounds leak-on-disconnect) */

NET_BUF_POOL_FIXED_DEFINE(tx_pool, 8, BT_L2CAP_SDU_BUF_SIZE(L2CAP_MTU), 8, NULL);

static struct bt_l2cap_le_chan le_chan;
static struct bt_conn *default_conn;
static K_SEM_DEFINE(blast_sem, 0, 1);
static K_SEM_DEFINE(tx_win, TX_WINDOW, TX_WINDOW); /* completion-paced in-flight window */
static atomic_t inflight;         /* SDUs handed to the stack, not yet .sent — leak probe */
static struct k_work_delayable disc_work;  /* periodic clean self-disconnect */
static volatile bool chan_up;
static volatile uint32_t tx_bytes;
static volatile uint32_t blocked;

/* ---- L2CAP CoC server (we are the sender once the channel is up) ---- */

static int chan_recv(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
	ARG_UNUSED(chan);
	ARG_UNUSED(buf);
	return 0; /* central doesn't send downstream in this test */
}

/* Fires when the stack has finished with an SDU we sent (success path). We use it
 * to refill the in-flight window and to observe reclaim across a disconnect. */
static void chan_sent(struct bt_l2cap_chan *chan)
{
	ARG_UNUSED(chan);
	atomic_dec(&inflight);
	k_sem_give(&tx_win);
}

static void chan_connected(struct bt_l2cap_chan *chan)
{
	struct bt_l2cap_le_chan *le = CONTAINER_OF(chan, struct bt_l2cap_le_chan, chan);
	printk("CoC up: tx.mtu=%u tx.mps=%u — paced blaster armed (window=%d)\n",
	       le->tx.mtu, le->tx.mps, TX_WINDOW);
	/* Fresh session: reset the window + inflight so a prior leak can't wedge us. */
	atomic_set(&inflight, 0);
	k_sem_reset(&tx_win);
	for (int i = 0; i < TX_WINDOW; i++) {
		k_sem_give(&tx_win);
	}
	tx_bytes = 0;
	blocked = 0;
	chan_up = true;
	k_sem_give(&blast_sem);
	/* self-disconnect DISABLED for the central-reboot isolation test — the central
	 * now drives the drop (via sys_reboot), DK just stays up and blasts.
	 * k_work_reschedule(&disc_work, K_SECONDS(UPTIME_BEFORE_DISCONNECT_S)); */
}

static void chan_disconnected(struct bt_l2cap_chan *chan)
{
	ARG_UNUSED(chan);
	chan_up = false;
	/* KEY PROBE: how many SDUs were still in flight when the link dropped?
	 * If .sent fires for them during teardown, inflight returns to 0 (reclaimed).
	 * If it stays >0 in the idle heartbeat, those buffers leaked. */
	printk("CoC down: inflight=%ld at disconnect\n", (long)atomic_get(&inflight));
}

static const struct bt_l2cap_chan_ops chan_ops = {
	.connected = chan_connected,
	.disconnected = chan_disconnected,
	.sent = chan_sent,
	.recv = chan_recv,
};

static int l2cap_accept(struct bt_conn *conn, struct bt_l2cap_server *server,
			struct bt_l2cap_chan **chan)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(server);
	/* FIX ATTEMPT: wipe stale channel state (credits, TX queue, in-flight refs)
	 * so a freshly-rebooted central meets a clean channel — reusing the static
	 * struct dirty is the suspected reconnect-wedge cause. */
	memset(&le_chan, 0, sizeof(le_chan));
	le_chan.chan.ops = &chan_ops;
	le_chan.rx.mtu = L2CAP_MTU;
	*chan = &le_chan.chan;
	return 0;
}

static struct bt_l2cap_server server = {
	.psm = L2CAP_PSM,
	.sec_level = BT_SECURITY_L1,
	.accept = l2cap_accept,
};

/* ---- self-disconnect isolation: cleanly drop the LINK on a timer (central stays
 * alive, no reboot / USB churn) so we test link-drop recovery in isolation. ---- */
static void disc_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	if (default_conn) {
		printk(">>> self-disconnect (clean link drop, central stays up)\n");
		bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

/* ---- advertising ---- */

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* Restart advertising from a workqueue, NOT synchronously in disconnected():
 * the just-closed connection object isn't reaped yet inside that callback, so
 * with BT_MAX_CONN=1 there's no free conn slot and bt_le_adv_start() returns
 * -ENOMEM. Deferring (and retrying) lets the stack free the slot first. */
static struct k_work_delayable adv_work;

static void adv_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err == -ENOMEM || err == -EAGAIN) {
		/* conn slot not freed yet — back off and retry */
		k_work_reschedule(&adv_work, K_MSEC(100));
		return;
	}
	if (err) {
		printk("adv start failed (%d)\n", err);
	} else {
		printk("Advertising as '%s'\n", CONFIG_BT_DEVICE_NAME);
	}
}

static void start_adv(void)
{
	k_work_reschedule(&adv_work, K_NO_WAIT);
}

/* ---- GAP ---- */

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("GAP connect failed (0x%02x)\n", err);
		return;
	}
	default_conn = bt_conn_ref(conn);
	struct bt_conn_info info;
	if (bt_conn_get_info(conn, &info) == 0) {
		printk("GAP connected: interval=%u\n", info.le.interval);
	}
	/* THE UPLINK FIX: the peripheral must raise ITS OWN TX data length — the
	 * central's DLE request only sets the central's TX (downlink). Without this
	 * the peripheral's TX stays at the 27-octet default, capping the uplink. */
	int e = bt_conn_le_data_len_update(conn, BT_LE_DATA_LEN_PARAM_MAX);
	printk("peripheral DLE req rc=%d\n", e);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	printk("GAP disconnected (0x%02x)\n", reason);
	chan_up = false;
	if (default_conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}
	start_adv();  /* if TX contexts leaked, the NEXT session will connect but not send */
}

static void le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *p)
{
	ARG_UNUSED(conn);
	printk("PHY updated: tx=%u rx=%u\n", p->tx_phy, p->rx_phy);
}

static void le_data_len_updated(struct bt_conn *conn,
				struct bt_conn_le_data_len_info *i)
{
	ARG_UNUSED(conn);
	printk("DLE updated: tx_max=%u rx_max=%u\n", i->tx_max_len, i->rx_max_len);
}

BT_CONN_CB_DEFINE(conn_cbs) = {
	.connected = connected,
	.disconnected = disconnected,
	.le_phy_updated = le_phy_updated,
	.le_data_len_updated = le_data_len_updated,
};

/* ---- uplink blast thread ---- */

static void blast_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	static uint8_t payload[SDU_SIZE];
	memset(payload, 'U', sizeof(payload));

	while (1) {
		k_sem_take(&blast_sem, K_FOREVER);
		printk("uplink blasting %d-byte SDUs...\n", SDU_SIZE);
		uint32_t spin = 0;
		while (chan_up) {
			/* Pace on the completion window: block until a prior SDU's .sent
			 * frees a slot. This bounds in-flight SDUs to TX_WINDOW, so a
			 * disconnect can strand at most TX_WINDOW buffers (< pool). */
			if (k_sem_take(&tx_win, K_MSEC(500)) != 0) {
				if (chan_up && ++spin > 4) { /* ~2s with no completions */
					blocked++;
					printk("STALL: no completions (inflight=%ld) — .sent not firing?\n",
					       (long)atomic_get(&inflight));
					spin = 0;
				}
				continue;
			}
			spin = 0;
			struct net_buf *buf = net_buf_alloc(&tx_pool, K_MSEC(100));
			if (!buf) {
				/* window said go but pool is dry => buffers stranded below L2CAP */
				printk("POOL: empty despite free window (inflight=%ld)\n",
				       (long)atomic_get(&inflight));
				k_sem_give(&tx_win);
				continue;
			}
			net_buf_reserve(buf, BT_L2CAP_SDU_CHAN_SEND_RESERVE);
			net_buf_add_mem(buf, payload, SDU_SIZE);
			int e = bt_l2cap_chan_send(&le_chan.chan, buf);
			if (e < 0) {
				net_buf_unref(buf);
				k_sem_give(&tx_win); /* nothing in flight for this one */
				if (e != -ENOTCONN && e != -ESHUTDOWN) {
					printk("send err %d\n", e);
				}
				break;
			}
			atomic_inc(&inflight);
			tx_bytes += SDU_SIZE;
		}
	}
}

K_THREAD_DEFINE(blast_tid, 2048, blast_fn, NULL, NULL, NULL, 10, 0, 0);

/* ---- summary thread ---- */

static void sum_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	uint32_t idle = 0;
	while (1) {
		k_msleep(SUMMARY_MS);
		if (chan_up) {
			uint32_t b = tx_bytes;
			tx_bytes = 0;
			idle = 0;
			printk("UPLINK tx: %u KB/s  (%u bytes/s, blocked-events=%u)\n",
			       b / 1024, b, blocked);
		} else {
			/* disconnected heartbeat: proves the DK is alive + shows adv/conn state,
			 * so "silent" can never be mistaken for "advertising but not reconnecting" */
			printk("idle %us: conn=%s inflight=%ld (advertising, awaiting central)\n",
			       ++idle, default_conn ? "yes" : "none",
			       (long)atomic_get(&inflight));
		}
	}
}

K_THREAD_DEFINE(sum_tid, 1024, sum_fn, NULL, NULL, NULL, 10, 0, 0);

int main(void)
{
	int err = bt_enable(NULL);
	if (err) {
		printk("bt_enable failed (%d)\n", err);
		return 0;
	}
	printk("\n=== nRF52 L2CAP CoC UPLINK BLASTER (Zephyr 4.4.1, #46073 re-test) ===\n");
	k_work_init_delayable(&adv_work, adv_work_fn);
	k_work_init_delayable(&disc_work, disc_work_fn);
	bt_l2cap_server_register(&server);
	printk("L2CAP server on PSM 0x%04x\n", L2CAP_PSM);
	start_adv();
	return 0;
}
