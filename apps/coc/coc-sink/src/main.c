/*
 * nRF52832 DK — BLE 5 L2CAP CoC SINK. Zephyr 4.4.1 port of nrf52-l2cap-echo.
 *
 * The point of the port: RX uses the new **seg_recv** credit API (3.4 hardcoded
 * the old implicit RX credits to 1; seg_recv restores explicit control). We keep a
 * credit window so the sender isn't starved, count received bytes, and log KB/s.
 *
 * Ported vs 2.7: <zephyr/...> includes; 3-arg `accept` (adds bt_l2cap_server*);
 * chan_ops uses `.seg_recv` (not `.recv`/`.alloc_buf`); rx.mps set for seg_recv.
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
#define L2CAP_MPS   247      /* ~one DLE-251 PDU per segment */
#define RX_CREDITS  64       /* deep credit window: let airtime (not credits) bind */
#define SUMMARY_MS  1000

static struct bt_l2cap_le_chan le_chan;
static struct bt_conn *default_conn;
static volatile uint32_t rx_bytes;      /* per-window, reset each SUMMARY_MS */
static volatile uint32_t rx_total;      /* cumulative since CoC up (never reset mid-session) */
static volatile uint32_t rx_segs;       /* cumulative segment count (K-frames delivered) */
static volatile bool chan_up;

/* ---- L2CAP CoC server (seg_recv sink) ---- */

static void seg_recv(struct bt_l2cap_chan *chan, size_t sdu_len, off_t seg_offset,
		     struct net_buf_simple *seg)
{
	ARG_UNUSED(sdu_len);
	ARG_UNUSED(seg_offset);
	rx_bytes += seg->len;
	rx_total += seg->len;
	rx_segs++;
#if defined(CONFIG_APP_CREDIT_BATCH)
	/* Batched return: one credit PDU per ~41 segments (refill when fewer than 24 of the window remain). */
	static int32_t avail = RX_CREDITS;
	if (--avail < 24) { bt_l2cap_chan_give_credits(chan, RX_CREDITS - avail); avail = RX_CREDITS; }
#else
	bt_l2cap_chan_give_credits(chan, 1); /* replenish: keep the window at RX_CREDITS */
#endif
}

static void chan_connected(struct bt_l2cap_chan *chan)
{
	ARG_UNUSED(chan);
	chan_up = true;
	rx_bytes = 0;
	rx_total = 0;
	rx_segs = 0;
	printk("CoC up — seg_recv sink ready (RX_CREDITS=%u, MPS=%u)\n", RX_CREDITS, L2CAP_MPS);
}

static void chan_disconnected(struct bt_l2cap_chan *chan)
{
	ARG_UNUSED(chan);
	chan_up = false;
	printk("CoC down\n");
}

static const struct bt_l2cap_chan_ops chan_ops = {
	.connected = chan_connected,
	.disconnected = chan_disconnected,
	.seg_recv = seg_recv,
};

static int l2cap_accept(struct bt_conn *conn, struct bt_l2cap_server *server,
			struct bt_l2cap_chan **chan)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(server);
	le_chan.chan.ops = &chan_ops;
	le_chan.rx.mtu = L2CAP_MTU;
	le_chan.rx.mps = L2CAP_MPS;
	*chan = &le_chan.chan;
	/* initial credits before CONNECTING -> sent in the connection PDU */
	atomic_set(&le_chan.rx.credits, 0);   /* seg_recv: drop stale credits from a previous connection */
	bt_l2cap_chan_give_credits(&le_chan.chan, RX_CREDITS);
	return 0;
}

static struct bt_l2cap_server server = {
	.psm = L2CAP_PSM,
	.sec_level = BT_SECURITY_L1,
	.accept = l2cap_accept,
};

/* ---- advertising ---- */

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* deferred + retrying re-advertise: calling bt_le_adv_start directly in the
 * disconnected callback races the stack and returns -EAGAIN(-12), leaving the sink
 * un-discoverable. Defer to a work item and retry until it succeeds. */
static struct k_work_delayable adv_work;
static void adv_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("adv start failed (%d) -> retry\n", err);
		k_work_reschedule(&adv_work, K_MSEC(200));
	} else {
		printk("Advertising as '%s'\n", CONFIG_BT_DEVICE_NAME);
	}
}
static void start_adv(void)
{
	k_work_reschedule(&adv_work, K_NO_WAIT);
}

/* ---- GAP: central drives PHY/DLE; we accept + log + re-advertise ---- */

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
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	printk("GAP disconnected (0x%02x)\n", reason);
	if (default_conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}
	chan_up = false;
	start_adv();
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

static volatile uint16_t fsu_spacing = 0;  /* latched negotiated frame space (us); printed in SINK rx */
#if defined(CONFIG_BT_FRAME_SPACE_UPDATE)
/* peer-side (responder) FSU completion: the independent proof the sink's controller
 * actually applied the shorter tIFS — not just that the central asked. */
static void fsu_updated(struct bt_conn *conn, const struct bt_conn_le_frame_space_updated *p)
{
	ARG_UNUSED(conn);
	if (p->status == 0) { fsu_spacing = p->frame_space; }
	printk("Q3FSU-DONE role=P status=0x%02x spacing=%u types=0x%x phys=0x%x initiator=%d\n",
	       p->status, p->frame_space, p->spacing_types, p->phys, (int)p->initiator);
}
#endif

BT_CONN_CB_DEFINE(conn_cbs) = {
	.connected = connected,
	.disconnected = disconnected,
	.le_phy_updated = le_phy_updated,
	.le_data_len_updated = le_data_len_updated,
#if defined(CONFIG_BT_FRAME_SPACE_UPDATE)
	.frame_space_updated = fsu_updated,
#endif
};

/* ---- summary thread: report KB/s ---- */

static void sum_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	while (1) {
		k_msleep(SUMMARY_MS);
		if (chan_up) {
			uint32_t b = rx_bytes;
			rx_bytes = 0;
			/* cumulative counters printed too => fixed-window goodput can be
			 * recomputed offline as (rx_total_2 - rx_total_1)/(t2 - t1). */
			printk("SINK rx: %u KB/s (%u B / %u ms) fsu=%u | cum_total=%u B cum_segs=%u\n",
			       b / 1024, b, SUMMARY_MS, (unsigned)fsu_spacing, rx_total, rx_segs);
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
	printk("\n=== nRF52 L2CAP CoC SINK (Zephyr 4.4.1, seg_recv) ===\n");
	bt_l2cap_server_register(&server);
	printk("L2CAP server on PSM 0x%04x\n", L2CAP_PSM);
	k_work_init_delayable(&adv_work, adv_work_fn);
	start_adv();
	return 0;
}
