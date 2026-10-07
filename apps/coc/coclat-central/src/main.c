/*
 * nRF54L15-DK — L2CAP CoC latency-under-load CENTRAL (Test B', 2026-08-25).
 * Mirrors the GATT latency-under-load rig but ALL-CoC: a serialized tiny stop-signal
 * ping-pong (PING SDU -> echoed by the sink) rides the SAME CoC channel as a saturating
 * 480-byte bulk blast. The ping shares the tx_pool + credit window with bulk, so its RTT
 * measures CoC's credit/queue backpressure under load — the thing the GATT recommendation
 * assumes but never measured for CoC. Report: bulk sent + ping RTT distribution / sec.
 *
 * Ping detection: the sink distinguishes ping (sdu_len==PING_SIZE) from bulk (sdu_len==480)
 * and echoes pings; the central's seg_recv computes RTT on the echo.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/l2cap.h>

#define L2CAP_PSM   0x0080
#define L2CAP_MTU   512
#define BULK_SIZE   480
#define PING_SIZE   20            /* tiny stop-signal SDU (1 segment, <= MPS) */
#define PING_GAP_MS 20            /* min gap between serialized pings */
#define TARGET_NAME "zenoh-nrf-l2cap"

/* deep pool so bulk saturates and the ping must queue behind it (the backpressure under test) */
NET_BUF_POOL_FIXED_DEFINE(tx_pool, CONFIG_APP_POOL_DEPTH, BT_L2CAP_SDU_BUF_SIZE(L2CAP_MTU), 8, NULL);

static struct bt_conn *default_conn;
static struct bt_l2cap_le_chan le_chan;
static K_SEM_DEFINE(blast_sem, 0, 1);
static K_SEM_DEFINE(ping_echo_sem, 0, 1);
static volatile bool chan_up;
static bool l2cap_started;
static struct k_work_delayable dle_work;
static volatile uint32_t sent_sdus, eagain_cnt;

/* ping RTT state */
static volatile uint32_t ping_seq_out;      /* seq of the outstanding ping */
static volatile uint32_t ping_send_cyc;     /* k_cycle at send */
static volatile uint32_t ping_rtt_us;       /* last measured RTT (us) */
static volatile bool     ping_got_echo;
/* per-second RTT accumulator (written by ping thread, snapshotted+cleared by reporter) */
static volatile uint32_t acc_n, acc_sum_us, acc_min_us = 0xFFFFFFFF, acc_max_us, acc_over30, acc_timeouts;

static void start_scan(void);

/* ---- L2CAP client channel ops: receive echoed pings, compute RTT ---- */
static void chan_seg_recv(struct bt_l2cap_chan *chan, size_t sdu_len, off_t seg_offset,
			  struct net_buf_simple *seg)
{
	/* echoed ping = small single-segment SDU starting with "PING" */
	if (sdu_len == PING_SIZE && seg_offset == 0 && seg->len >= 8 &&
	    memcmp(seg->data, "PING", 4) == 0) {
		uint32_t seq;
		memcpy(&seq, seg->data + 4, 4);
		if (seq == ping_seq_out) {
			uint32_t dt = k_cycle_get_32() - ping_send_cyc;
			ping_rtt_us = k_cyc_to_us_near32(dt);
			ping_got_echo = true;
			k_sem_give(&ping_echo_sem);
		}
	}
	/* keep the reverse-path credits open (echoes are tiny + rare, a fixed window is fine) */
	bt_l2cap_chan_give_credits(chan, 1);
	ARG_UNUSED(chan);
}

static volatile uint16_t fsu_spacing;
#if defined(CONFIG_BT_FRAME_SPACE_UPDATE)
static void fsu_updated(struct bt_conn *conn, const struct bt_conn_le_frame_space_updated *p)
{
	ARG_UNUSED(conn);
	if (p->status == 0) { fsu_spacing = p->frame_space; }
	printk("Q3FSU-DONE status=0x%02x spacing=%u\n", p->status, p->frame_space);
}
#endif

static void chan_connected_cb(struct bt_l2cap_chan *chan)
{
	struct bt_l2cap_le_chan *le = CONTAINER_OF(chan, struct bt_l2cap_le_chan, chan);
	printk("L2CAP connected: tx.mtu=%u tx.mps=%u rx.mtu=%u rx.mps=%u\n",
	       le->tx.mtu, le->tx.mps, le->rx.mtu, le->rx.mps);
	struct bt_conn_info info;
	if (default_conn && bt_conn_get_info(default_conn, &info) == 0) {
		printk("GATE conn: interval=%u (units 1.25ms) => %u.%02u ms\n",
		       info.le.interval, info.le.interval * 5 / 4, (info.le.interval * 5 % 4) * 25);
	}
	chan_up = true;
#if defined(CONFIG_APP_AUTO_FSU) && (CONFIG_APP_FSU_MAX_US > 0)
	if (default_conn) {
		const struct bt_conn_le_frame_space_update_param fp = {
			.phys = BT_HCI_LE_FRAME_SPACE_UPDATE_PHY_2M_MASK,
			.spacing_types = BT_HCI_LE_FRAME_SPACE_UPDATE_SPACING_TYPE_IFS_ACL_CP_MASK |
					 BT_HCI_LE_FRAME_SPACE_UPDATE_SPACING_TYPE_IFS_ACL_PC_MASK,
			.frame_space_min = CONFIG_APP_FSU_MIN_US,
			.frame_space_max = CONFIG_APP_FSU_MAX_US,
		};
		int _frc = bt_conn_le_frame_space_update(default_conn, &fp);
		printk("Q3FSU-REQ rc=%d min=%d max=%d\n", _frc, CONFIG_APP_FSU_MIN_US, CONFIG_APP_FSU_MAX_US);
	}
#endif
#if !defined(CONFIG_APP_NO_BULK)
	k_sem_give(&blast_sem);   /* omit -> idle (unsaturated) ping-pong only */
#endif
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
	.seg_recv = chan_seg_recv,
};

/* ---- GAP: 2M -> DLE -> L2CAP ---- */
static void open_l2cap(void)
{
	if (l2cap_started || !default_conn) { return; }
	l2cap_started = true;
	le_chan.chan.ops = &chan_ops;
	le_chan.rx.mtu = L2CAP_MTU;
	le_chan.rx.mps = 247;
	/* Initial window goes IN the connection request, and stale rx.credits from a previous connection are dropped first
	 * (seg_recv: the host never resets them). Granting only after connect leaves the peer with 0 initial credits, which
	 * trips a Zephyr 4.4 host stall if the peer sends first (debug-evidence/coc-credit-fixes-20261006). */
	atomic_set(&le_chan.rx.credits, 0);
	bt_l2cap_chan_give_credits(&le_chan.chan, 64);   /* open the reverse (echo) window */
	int e = bt_l2cap_chan_connect(default_conn, &le_chan.chan, L2CAP_PSM);
	printk("l2cap_chan_connect rc=%d\n", e);
}
static void dle_work_fn(struct k_work *w) { ARG_UNUSED(w); open_l2cap(); }

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) { printk("connect failed 0x%02x\n", err); bt_conn_unref(default_conn); default_conn = NULL; start_scan(); return; }
	printk("GAP connected\n");
	l2cap_started = false;
	bt_conn_le_phy_update(conn, BT_CONN_LE_PHY_PARAM_2M);
	k_work_reschedule(&dle_work, K_MSEC(500));
}
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	printk("GAP disconnected 0x%02x\n", reason);
	k_work_cancel_delayable(&dle_work);
	chan_up = false; l2cap_started = false;
	if (default_conn) { bt_conn_unref(default_conn); default_conn = NULL; }
	start_scan();
}
static void le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *param)
{
	printk("PHY: tx=%u rx=%u (2=2M)\n", param->tx_phy, param->rx_phy);
	if (param->tx_phy == BT_GAP_LE_PHY_2M && param->rx_phy == BT_GAP_LE_PHY_2M) {
		bt_conn_le_data_len_update(conn, BT_LE_DATA_LEN_PARAM_MAX);
	}
}
static void le_data_len_updated(struct bt_conn *conn, struct bt_conn_le_data_len_info *info)
{
	ARG_UNUSED(conn);
	printk("DLE: tx_max=%u rx_max=%u\n", info->tx_max_len, info->rx_max_len);
	if (info->tx_max_len < 251) { return; }  /* wait for FULL DLE (after PHY=2M) or FSU req gets -EACCES */
	k_work_cancel_delayable(&dle_work);
	open_l2cap();
}
BT_CONN_CB_DEFINE(conn_cbs) = {
	.connected = connected, .disconnected = disconnected,
	.le_phy_updated = le_phy_updated, .le_data_len_updated = le_data_len_updated,
#if defined(CONFIG_BT_FRAME_SPACE_UPDATE)
	.frame_space_updated = fsu_updated,
#endif
};

/* ---- scan ---- */
static bool ad_match_name(struct bt_data *data, void *user_data)
{
	bool *match = user_data;
	if (data->type == BT_DATA_NAME_COMPLETE || data->type == BT_DATA_NAME_SHORTENED) {
		if (data->data_len == strlen(TARGET_NAME) && memcmp(data->data, TARGET_NAME, data->data_len) == 0) {
			*match = true; return false;
		}
	}
	return true;
}
static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type, struct net_buf_simple *ad)
{
	ARG_UNUSED(rssi);
	if (default_conn) { return; }
	if (type != BT_GAP_ADV_TYPE_ADV_IND && type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND) { return; }
	bool match = false; bt_data_parse(ad, ad_match_name, &match);
	if (!match) { return; }
	bt_le_scan_stop();
	struct bt_le_conn_param *param = BT_LE_CONN_PARAM(CONFIG_APP_CONN_INT_UNITS, CONFIG_APP_CONN_INT_UNITS, 0, 400);
	int e = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, param, &default_conn);
	if (e) { printk("create conn rc=%d\n", e); start_scan(); }
}
static void start_scan(void)
{
	struct bt_le_scan_param sp = { .type = BT_LE_SCAN_TYPE_ACTIVE, .options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL, .window = BT_GAP_SCAN_FAST_WINDOW };
	bt_le_scan_start(&sp, device_found);
}

/* ---- bulk blast: saturate the link with 480-byte SDUs ---- */
static void blast_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	static uint8_t payload[BULK_SIZE];
	memset(payload, 'X', sizeof(payload));
	while (1) {
		k_sem_take(&blast_sem, K_FOREVER);
		k_msleep(500);   /* warm-up: let the FSU LLCP negotiate before the bulk floods the link */
		while (chan_up) {
			struct net_buf *buf = net_buf_alloc(&tx_pool, K_FOREVER);
			net_buf_reserve(buf, BT_L2CAP_SDU_CHAN_SEND_RESERVE);
			net_buf_add_mem(buf, payload, BULK_SIZE);
			int e = bt_l2cap_chan_send(&le_chan.chan, buf);
			if (e == -EAGAIN) { net_buf_unref(buf); eagain_cnt++; k_msleep(1); continue; }
			if (e < 0) { net_buf_unref(buf); break; }
			sent_sdus++;
			if ((sent_sdus & 0x7) == 0) { k_yield(); }
#if (CONFIG_APP_OFFERED_KBPS > 0)
			/* pace to the offered rate: us/SDU = BULK_SIZE / (KBps*1024) * 1e6 */
			k_usleep((BULK_SIZE * 1000U) / (CONFIG_APP_OFFERED_KBPS * 1024U / 1000U));
#endif
		}
	}
}
K_THREAD_DEFINE(blast_tid, 2048, blast_fn, NULL, NULL, NULL, 10, 0, 0);

/* ---- ping thread: serialized stop-signal ping-pong through the SAME channel/pool as bulk ---- */
static void ping_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	uint32_t seq = 0;
	while (1) {
		if (!chan_up) { k_msleep(50); continue; }
		seq++;
		uint8_t p[PING_SIZE];
		memset(p, 0, sizeof(p));
		memcpy(p, "PING", 4);
		memcpy(p + 4, &seq, 4);
		struct net_buf *buf = net_buf_alloc(&tx_pool, K_FOREVER);   /* queues behind bulk = backpressure */
		net_buf_reserve(buf, BT_L2CAP_SDU_CHAN_SEND_RESERVE);
		net_buf_add_mem(buf, p, PING_SIZE);
		ping_got_echo = false;
		ping_seq_out = seq;
		ping_send_cyc = k_cycle_get_32();
		int e = bt_l2cap_chan_send(&le_chan.chan, buf);
		if (e < 0) { net_buf_unref(buf); k_msleep(5); continue; }
		/* wait for the echo (500 ms timeout) */
		if (k_sem_take(&ping_echo_sem, K_MSEC(500)) == 0 && ping_got_echo) {
			uint32_t r = ping_rtt_us;
			acc_n++; acc_sum_us += r;
			if (r < acc_min_us) { acc_min_us = r; }
			if (r > acc_max_us) { acc_max_us = r; }
			if (r > 30000) { acc_over30++; }
		} else {
			acc_timeouts++;
		}
		k_msleep(PING_GAP_MS);
	}
}
K_THREAD_DEFINE(ping_tid, 2048, ping_fn, NULL, NULL, NULL, 9, 0, 0);

/* ---- reporter: bulk sent + ping RTT distribution, 1 s ---- */
static void report_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	uint32_t prev_sent = 0;
	while (1) {
		k_msleep(1000);
		if (!chan_up) { continue; }
		uint32_t n = acc_n, sum = acc_sum_us, mn = acc_min_us, mx = acc_max_us, ov = acc_over30, to = acc_timeouts;
		acc_n = 0; acc_sum_us = 0; acc_min_us = 0xFFFFFFFF; acc_max_us = 0; acc_over30 = 0; acc_timeouts = 0;
		uint32_t s = sent_sdus;
		uint32_t mean = n ? sum / n : 0;
		printk("LAT: pings=%u rtt_us[min/mean/max]=%u/%u/%u over30ms=%u timeouts=%u | bulk=%u(+%u SDU) eagain=%u fsu=%u\n",
		       n, (n ? mn : 0), mean, mx, ov, to, s, s - prev_sent, eagain_cnt, (unsigned)fsu_spacing);
		prev_sent = s;
	}
}
K_THREAD_DEFINE(report_tid, 1536, report_fn, NULL, NULL, NULL, 11, 0, 0);

int main(void)
{
	int err = bt_enable(NULL);
	if (err) { printk("bt_enable failed %d\n", err); return 0; }
	printk("\n=== CoC latency-under-load CENTRAL (bulk 480 + ping-pong stop-signal) ===\n");
	k_work_init_delayable(&dle_work, dle_work_fn);
	start_scan();
	return 0;
}
