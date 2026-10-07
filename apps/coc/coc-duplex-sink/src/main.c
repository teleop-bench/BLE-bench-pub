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
#define SDU_SIZE 244
/* DUPLEX FIX (see cocdx-cen): shallow TX pool = air-completion self-pacing so this node's
 * downlink-credit PDUs don't sit behind a deep uplink queue. 16 << 64-credit window. */
NET_BUF_POOL_FIXED_DEFINE(tx_pool, CONFIG_APP_TX_POOL_COUNT, BT_L2CAP_SDU_BUF_SIZE(L2CAP_MTU), 8, NULL);
static struct bt_conn *default_conn;
static volatile uint32_t rx_bytes;      /* per-window, reset each SUMMARY_MS */
static volatile uint32_t rx_total;      /* cumulative since CoC up (never reset mid-session) */
static volatile uint32_t rx_segs;       /* cumulative segment count (K-frames delivered) */
static volatile bool chan_up;
static volatile uint32_t up_sent, up_eagain;   /* uplink diagnostics */
static volatile int up_last_err;

/* ---- L2CAP CoC server (seg_recv sink) ---- */

/* outstanding-credit count; reset whenever a fresh window is granted (stale across reconnects = credit leak) */
static int32_t avail = RX_CREDITS;   /* watermark: refill to RX_CREDITS when below LOW_WM */
static void seg_recv(struct bt_l2cap_chan *chan, size_t sdu_len, off_t seg_offset,
		     struct net_buf_simple *seg)
{
	ARG_UNUSED(sdu_len);
	ARG_UNUSED(seg_offset);
	rx_bytes += seg->len;
	rx_total += seg->len;
	rx_segs++;
	if (--avail < 24) { bt_l2cap_chan_give_credits(chan, RX_CREDITS - avail); avail = RX_CREDITS; }
}

static void chan_connected(struct bt_l2cap_chan *chan)
{
	ARG_UNUSED(chan);
	chan_up = true;
	rx_bytes = 0;
	rx_total = 0;
	rx_segs = 0;
	printk("CoC up — seg_recv sink ready (RX_CREDITS=%u, MPS=%u) txcred=%ld\n", RX_CREDITS, L2CAP_MPS,
	       (long)atomic_get(&le_chan.tx.credits));
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
	/* seg_recv: the host never resets rx.credits, so a reused channel would send the previous
	 * connection's leftover credits as this connection's initial credits */
	atomic_set(&le_chan.rx.credits, 0);
	avail = RX_CREDITS;
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
	/* DUPLEX FIX: the peripheral MUST extend its own TX data length, else it stays at the
	 * 27-octet default -> the central's RX max = min(cen_rx, periph_tx) = 27 -> the uplink
	 * (sink->central) fragments into 27-byte PDUs and collapses to ~0-50 KB/s. Requesting
	 * the max here makes the central's rx_max become 251 so uplink PDUs go full-size. */
#ifndef APP_SINK_DLE
	/* bisect: v2 had no explicit peripheral DLE call */
#else
	int dle = bt_conn_le_data_len_update(conn, BT_LE_DATA_LEN_PARAM_MAX);
	printk("sink DLE req rc=%d\n", dle);
#endif
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
#if defined(CONFIG_BT_TESTING)   /* Stage 2: peripheral host->controller PDUs handed down */
extern volatile uint32_t l2cap_pull_pdus;
#endif

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
			printk("SINK rx: %u KB/s (%u B / %u ms) fsu=%u | cum_total=%u B cum_segs=%u | UP sent=%u eagain=%u lasterr=%d txcred=%ld host_pulls=%u\n",
			       b / 1024, b, SUMMARY_MS, (unsigned)fsu_spacing, rx_total, rx_segs,
			       up_sent, up_eagain, up_last_err, (long)atomic_get(&le_chan.tx.credits),
#if defined(CONFIG_BT_TESTING)
			       l2cap_pull_pdus);
#else
			       0U);
#endif
		}
	}
}

K_THREAD_DEFINE(sum_tid, 1024, sum_fn, NULL, NULL, NULL, 10, 0, 0);

/* duplex uplink blast: peripheral sends SDUs to the central on the same CoC channel */
static void blast_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	static uint8_t payload[SDU_SIZE];
	static uint32_t sent_cnt;
	memset(payload, 'Y', sizeof(payload));
	if (!IS_ENABLED(CONFIG_APP_UPLINK)) { return; }
	while (1) {
		if (!chan_up) { k_msleep(50); continue; }
		struct net_buf *buf = net_buf_alloc(&tx_pool, K_FOREVER);
		net_buf_reserve(buf, BT_L2CAP_SDU_CHAN_SEND_RESERVE);
		net_buf_add_mem(buf, payload, SDU_SIZE);
		int e = bt_l2cap_chan_send(&le_chan.chan, buf);
		if (e < 0) { net_buf_unref(buf); up_last_err = e; if (e == -EAGAIN) up_eagain++; k_msleep(e == -EAGAIN ? 1 : 10); }
		else { up_sent++; if ((++sent_cnt & 0x7) == 0) { k_yield(); } }  /* let BT RX run -> credits return */
	}
}
K_THREAD_DEFINE(blast_tid, 2048, blast_fn, NULL, NULL, NULL, 11, 0, 0);

#if defined(CONFIG_APP_CREDIT_TRACE)
/* ---- TX credit/queue trace (diagnostic, default off) ----
 * Samples this side's CoC TX state every CONFIG_APP_CREDIT_TRACE_US and prints the 1 s
 * distribution as a CTRACE line. Each sample lands in the first matching state:
 *   nofeed   - no SDU waiting in the channel's L2CAP queue (app pool all in flight / idle)
 *   cr0      - SDU waiting, channel TX credits 0 (the peer has not returned credits)
 *   ctrlfull - SDU waiting with credits, but every controller ACL buffer is taken
 *   hostheld - SDU waiting, credits > 0 and a free controller buffer (host not handing down)
 * Also: ctrlempty (all controller ACL buffers free), mean credits, credit returns seen
 * (increases between samples) and how many separate zero-credit episodes began. */
struct k_sem *bt_conn_get_pkts(struct bt_conn *conn);   /* host-internal (conn_internal.h) */
static void ctrace_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	uint32_t n = 0, nofeed = 0, cr0 = 0, full = 0, held = 0, empty = 0;
	uint32_t cr_sum = 0, rets = 0, ret_cr = 0, zep = 0, prev_cr = 0, lim = 0;
	int64_t t0 = k_uptime_get();
	while (1) {
		k_sleep(K_USEC(CONFIG_APP_CREDIT_TRACE_US));
		struct bt_conn *conn = default_conn;
		if (!chan_up || !conn) { n = 0; t0 = k_uptime_get(); continue; }
		uint32_t cr = (uint32_t)atomic_get(&le_chan.tx.credits);
		struct k_sem *pk = bt_conn_get_pkts(conn);
		uint32_t freeb = k_sem_count_get(pk);
		bool waiting = !k_fifo_is_empty(&le_chan.tx_queue);
		lim = pk->limit;
		if (n && cr > prev_cr) { rets++; ret_cr += cr - prev_cr; }
		if (cr == 0 && (n == 0 || prev_cr != 0)) { zep++; }
		prev_cr = cr; n++; cr_sum += cr;
		if (freeb == lim) { empty++; }
		if (!waiting) { nofeed++; } else if (cr == 0) { cr0++; }
		else if (freeb == 0) { full++; } else { held++; }
		if (k_uptime_get() - t0 >= 1000) {
			printk("CTRACE n=%u nofeed=%u cr0=%u ctrlfull=%u hostheld=%u | ctrlempty=%u "
			       "ctrlbufs=%u cr_mean_x10=%u returns=%u ret_cr=%u zero_ep=%u\n",
			       n, nofeed, cr0, full, held, empty, lim, n ? cr_sum * 10 / n : 0,
			       rets, ret_cr, zep);
			n = nofeed = cr0 = full = held = empty = cr_sum = rets = ret_cr = zep = 0;
			t0 = k_uptime_get();
		}
	}
}
K_THREAD_DEFINE(ctrace_tid, 1024, ctrace_fn, NULL, NULL, NULL, 5, 0, 0);
#endif

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
