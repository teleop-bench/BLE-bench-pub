/*
 * nRF54L15-DK — TWO-CHANNEL CoC latency test (Test §11.3, 2026-08-25).
 * Bulk (480 B blast) on CoC PSM 0x0080 with its own tx_pool; the stop-signal ping-pong on a
 * SEPARATE CoC PSM 0x0081 with its own tx_pool + credit window. Saturate bulk, measure control
 * RTT. Tests whether a separate L2CAP channel isolates control latency from bulk saturation
 * (stack-level priority lane) or whether both merge at the single LL TX queue (predicted).
 * Compare to shared-channel ~290 ms (coclat) and GATT-paced ~28 ms.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/l2cap.h>

#define PSM_BULK    0x0080
#define PSM_CTRL    0x0081
#define L2CAP_MTU   512
#define BULK_SIZE   480
#define PING_SIZE   20
#define PING_GAP_MS 20
#define TARGET_NAME "zenoh-nrf-l2cap"

NET_BUF_POOL_FIXED_DEFINE(bulk_pool, 64, BT_L2CAP_SDU_BUF_SIZE(L2CAP_MTU), 8, NULL);
NET_BUF_POOL_FIXED_DEFINE(ctrl_pool, 4,  BT_L2CAP_SDU_BUF_SIZE(L2CAP_MTU), 8, NULL);

static struct bt_conn *default_conn;
static struct bt_l2cap_le_chan bulk_chan, ctrl_chan;
static volatile bool bulk_up, ctrl_up;
static bool l2cap_started;
static struct k_work_delayable dle_work;
static K_SEM_DEFINE(blast_sem, 0, 1);
static K_SEM_DEFINE(ping_echo_sem, 0, 1);
static volatile uint32_t sent_sdus, eagain_cnt;
static volatile uint32_t ping_seq_out, ping_send_cyc, ping_rtt_us;
static volatile bool ping_got_echo;
static volatile uint32_t acc_n, acc_sum_us, acc_min_us = 0xFFFFFFFF, acc_max_us, acc_over30, acc_timeouts;
static volatile uint16_t fsu_spacing;

static void start_scan(void);

/* control-channel seg_recv: echoed ping -> RTT */
static void ctrl_seg_recv(struct bt_l2cap_chan *chan, size_t sdu_len, off_t seg_offset,
			  struct net_buf_simple *seg)
{
	if (sdu_len == PING_SIZE && seg_offset == 0 && seg->len >= 8 && memcmp(seg->data, "PING", 4) == 0) {
		uint32_t seq; memcpy(&seq, seg->data + 4, 4);
		if (seq == ping_seq_out) {
			ping_rtt_us = k_cyc_to_us_near32(k_cycle_get_32() - ping_send_cyc);
			ping_got_echo = true; k_sem_give(&ping_echo_sem);
		}
	}
	bt_l2cap_chan_give_credits(chan, 1);
}
/* bulk-channel seg_recv: unused (bulk is downlink-only) but keep credits open */
static void bulk_seg_recv(struct bt_l2cap_chan *chan, size_t sdu_len, off_t seg_offset,
			  struct net_buf_simple *seg)
{ ARG_UNUSED(sdu_len); ARG_UNUSED(seg_offset); ARG_UNUSED(seg); bt_l2cap_chan_give_credits(chan, 1); }

#if defined(CONFIG_BT_FRAME_SPACE_UPDATE)
static void fsu_updated(struct bt_conn *conn, const struct bt_conn_le_frame_space_updated *p)
{ ARG_UNUSED(conn); if (p->status == 0) { fsu_spacing = p->frame_space; }
  printk("Q3FSU-DONE status=0x%02x spacing=%u\n", p->status, p->frame_space); }
#endif

static void request_fsu(void)
{
#if defined(CONFIG_APP_AUTO_FSU) && (CONFIG_APP_FSU_MAX_US > 0)
	if (!default_conn) { return; }
	const struct bt_conn_le_frame_space_update_param fp = {
		.phys = BT_HCI_LE_FRAME_SPACE_UPDATE_PHY_2M_MASK,
		.spacing_types = BT_HCI_LE_FRAME_SPACE_UPDATE_SPACING_TYPE_IFS_ACL_CP_MASK |
				 BT_HCI_LE_FRAME_SPACE_UPDATE_SPACING_TYPE_IFS_ACL_PC_MASK,
		.frame_space_min = CONFIG_APP_FSU_MIN_US, .frame_space_max = CONFIG_APP_FSU_MAX_US,
	};
	printk("Q3FSU-REQ rc=%d\n", bt_conn_le_frame_space_update(default_conn, &fp));
#endif
}

static void bulk_connected(struct bt_l2cap_chan *chan)
{
	ARG_UNUSED(chan); bulk_up = true;
	printk("BULK chan up\n");
	k_sem_give(&blast_sem);
	request_fsu();
}
static void ctrl_connected(struct bt_l2cap_chan *chan)
{
	ARG_UNUSED(chan); ctrl_up = true;
	printk("CTRL chan up\n");
}
static void bulk_disc(struct bt_l2cap_chan *chan) { ARG_UNUSED(chan); bulk_up = false; }
static void ctrl_disc(struct bt_l2cap_chan *chan) { ARG_UNUSED(chan); ctrl_up = false; }
static const struct bt_l2cap_chan_ops bulk_ops = { .connected = bulk_connected, .disconnected = bulk_disc, .seg_recv = bulk_seg_recv };
static const struct bt_l2cap_chan_ops ctrl_ops = { .connected = ctrl_connected, .disconnected = ctrl_disc, .seg_recv = ctrl_seg_recv };

static void open_l2cap(void)
{
	if (l2cap_started || !default_conn) { return; }
	l2cap_started = true;
	bulk_chan.chan.ops = &bulk_ops; bulk_chan.rx.mtu = L2CAP_MTU; bulk_chan.rx.mps = 247;
	ctrl_chan.chan.ops = &ctrl_ops; ctrl_chan.rx.mtu = L2CAP_MTU; ctrl_chan.rx.mps = 247;
	/* Initial window goes IN the connection request, and stale rx.credits from a previous connection are dropped first
	 * (seg_recv: the host never resets them). Granting only after connect leaves the peer with 0 initial credits, which
	 * trips a Zephyr 4.4 host stall if the peer sends first (debug-evidence/coc-credit-fixes-20261006). */
	atomic_set(&bulk_chan.rx.credits, 0); bt_l2cap_chan_give_credits(&bulk_chan.chan, 64);
	atomic_set(&ctrl_chan.rx.credits, 0); bt_l2cap_chan_give_credits(&ctrl_chan.chan, 64);
	printk("connect bulk rc=%d\n", bt_l2cap_chan_connect(default_conn, &bulk_chan.chan, PSM_BULK));
	printk("connect ctrl rc=%d\n", bt_l2cap_chan_connect(default_conn, &ctrl_chan.chan, PSM_CTRL));
}
static void dle_work_fn(struct k_work *w) { ARG_UNUSED(w); open_l2cap(); }

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) { printk("connect failed 0x%02x\n", err); bt_conn_unref(default_conn); default_conn = NULL; start_scan(); return; }
	printk("GAP connected\n"); l2cap_started = false;
	bt_conn_le_phy_update(conn, BT_CONN_LE_PHY_PARAM_2M);
	k_work_reschedule(&dle_work, K_MSEC(500));
}
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn); printk("GAP disconnected 0x%02x\n", reason);
	k_work_cancel_delayable(&dle_work); bulk_up = ctrl_up = false; l2cap_started = false;
	if (default_conn) { bt_conn_unref(default_conn); default_conn = NULL; } start_scan();
}
static void le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *param)
{
	printk("PHY: tx=%u rx=%u\n", param->tx_phy, param->rx_phy);
	if (param->tx_phy == BT_GAP_LE_PHY_2M) { bt_conn_le_data_len_update(conn, BT_LE_DATA_LEN_PARAM_MAX); }
}
static void le_data_len_updated(struct bt_conn *conn, struct bt_conn_le_data_len_info *info)
{
	printk("DLE: tx_max=%u rx_max=%u\n", info->tx_max_len, info->rx_max_len);
	if (info->tx_max_len < 251) { return; }   /* wait for full DLE (else FSU -EACCES) */
	k_work_cancel_delayable(&dle_work); open_l2cap();
}
BT_CONN_CB_DEFINE(conn_cbs) = { .connected = connected, .disconnected = disconnected,
	.le_phy_updated = le_phy_updated, .le_data_len_updated = le_data_len_updated,
#if defined(CONFIG_BT_FRAME_SPACE_UPDATE)
	.frame_space_updated = fsu_updated,
#endif
};

static bool ad_match_name(struct bt_data *data, void *user_data)
{
	bool *m = user_data;
	if ((data->type == BT_DATA_NAME_COMPLETE || data->type == BT_DATA_NAME_SHORTENED) &&
	    data->data_len == strlen(TARGET_NAME) && memcmp(data->data, TARGET_NAME, data->data_len) == 0) { *m = true; return false; }
	return true;
}
static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type, struct net_buf_simple *ad)
{
	ARG_UNUSED(rssi);
	if (default_conn || (type != BT_GAP_ADV_TYPE_ADV_IND && type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND)) { return; }
	bool m = false; bt_data_parse(ad, ad_match_name, &m); if (!m) { return; }
	bt_le_scan_stop();
	struct bt_le_conn_param *param = BT_LE_CONN_PARAM(CONFIG_APP_CONN_INT_UNITS, CONFIG_APP_CONN_INT_UNITS, 0, 400);
	if (bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, param, &default_conn)) { start_scan(); }
}
static void start_scan(void)
{
	struct bt_le_scan_param sp = { .type = BT_LE_SCAN_TYPE_ACTIVE, .options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL, .window = BT_GAP_SCAN_FAST_WINDOW };
	bt_le_scan_start(&sp, device_found);
}

static void blast_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	static uint8_t payload[BULK_SIZE]; memset(payload, 'X', sizeof(payload));
	while (1) {
		k_sem_take(&blast_sem, K_FOREVER);
		k_msleep(500);   /* warm-up: let FSU negotiate before flooding */
		while (bulk_up) {
			struct net_buf *buf = net_buf_alloc(&bulk_pool, K_FOREVER);
			net_buf_reserve(buf, BT_L2CAP_SDU_CHAN_SEND_RESERVE);
			net_buf_add_mem(buf, payload, BULK_SIZE);
			int e = bt_l2cap_chan_send(&bulk_chan.chan, buf);
			if (e == -EAGAIN) { net_buf_unref(buf); eagain_cnt++; k_msleep(1); continue; }
			if (e < 0) { net_buf_unref(buf); break; }
			sent_sdus++; if ((sent_sdus & 0x7) == 0) { k_yield(); }
#if (CONFIG_APP_OFFERED_KBPS > 0)
			k_usleep((BULK_SIZE * 1000U) / (CONFIG_APP_OFFERED_KBPS * 1024U / 1000U));
#endif
		}
	}
}
K_THREAD_DEFINE(blast_tid, 2048, blast_fn, NULL, NULL, NULL, 10, 0, 0);

static void ping_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	uint32_t seq = 0;
	while (1) {
		if (!ctrl_up) { k_msleep(50); continue; }
		seq++;
		uint8_t p[PING_SIZE]; memset(p, 0, sizeof(p)); memcpy(p, "PING", 4); memcpy(p + 4, &seq, 4);
		struct net_buf *buf = net_buf_alloc(&ctrl_pool, K_FOREVER);   /* SEPARATE pool from bulk */
		net_buf_reserve(buf, BT_L2CAP_SDU_CHAN_SEND_RESERVE); net_buf_add_mem(buf, p, PING_SIZE);
		ping_got_echo = false; ping_seq_out = seq; ping_send_cyc = k_cycle_get_32();
		if (bt_l2cap_chan_send(&ctrl_chan.chan, buf) < 0) { net_buf_unref(buf); k_msleep(5); continue; }
		if (k_sem_take(&ping_echo_sem, K_MSEC(500)) == 0 && ping_got_echo) {
			uint32_t r = ping_rtt_us; acc_n++; acc_sum_us += r;
			if (r < acc_min_us) { acc_min_us = r; } if (r > acc_max_us) { acc_max_us = r; }
			if (r > 30000) { acc_over30++; }
		} else { acc_timeouts++; }
		k_msleep(PING_GAP_MS);
	}
}
K_THREAD_DEFINE(ping_tid, 2048, ping_fn, NULL, NULL, NULL, 9, 0, 0);

static void report_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	uint32_t prev = 0;
	while (1) {
		k_msleep(1000);
		if (!bulk_up) { continue; }
		uint32_t n = acc_n, sum = acc_sum_us, mn = acc_min_us, mx = acc_max_us, ov = acc_over30, to = acc_timeouts;
		acc_n = 0; acc_sum_us = 0; acc_min_us = 0xFFFFFFFF; acc_max_us = 0; acc_over30 = 0; acc_timeouts = 0;
		uint32_t s = sent_sdus;
		printk("LAT2: pings=%u rtt_us[min/mean/max]=%u/%u/%u over30ms=%u to=%u | bulk=%u(+%u) fsu=%u ctrl=%d\n",
		       n, (n ? mn : 0), (n ? sum / n : 0), mx, ov, to, s, s - prev, (unsigned)fsu_spacing, (int)ctrl_up);
		prev = s;
	}
}
K_THREAD_DEFINE(report_tid, 1536, report_fn, NULL, NULL, NULL, 11, 0, 0);

int main(void)
{
	if (bt_enable(NULL)) { printk("bt_enable failed\n"); return 0; }
	printk("\n=== TWO-CHANNEL CoC latency (bulk PSM 0x80 + control PSM 0x81) ===\n");
	k_work_init_delayable(&dle_work, dle_work_fn);
	start_scan();
	return 0;
}
