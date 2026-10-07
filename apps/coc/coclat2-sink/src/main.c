/*
 * nRF54L15-DK — TWO-CHANNEL CoC latency SINK/echo (Test §11.3). Two L2CAP servers: bulk on
 * PSM 0x0080 (counts), control on PSM 0x0081 (echoes the stop-signal ping on its own channel).
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/l2cap.h>

#define PSM_BULK    0x0080
#define PSM_CTRL    0x0081
#define L2CAP_MTU   512
#define L2CAP_MPS   247
#define RX_CREDITS  64
#define PING_SIZE   20
#define SUMMARY_MS  1000

static struct bt_l2cap_le_chan bulk_chan, ctrl_chan;
NET_BUF_POOL_FIXED_DEFINE(ctrl_tx, 4, BT_L2CAP_SDU_BUF_SIZE(L2CAP_MTU), 8, NULL);
static struct bt_conn *default_conn;
static volatile uint32_t rx_bytes, rx_total, rx_segs, echoes, echo_fail;
static volatile bool bulk_up, ctrl_up;
static uint8_t echo_buf[PING_SIZE];
static K_SEM_DEFINE(echo_sem, 0, 1);
static volatile uint16_t fsu_spacing;

/* outstanding-credit count; reset whenever a fresh window is granted (stale across reconnects = credit leak) */
static int32_t bulk_avail = RX_CREDITS, ctrl_avail = RX_CREDITS;
static void bulk_seg_recv(struct bt_l2cap_chan *chan, size_t sdu_len, off_t seg_offset, struct net_buf_simple *seg)
{
	ARG_UNUSED(sdu_len); ARG_UNUSED(seg_offset);
	rx_bytes += seg->len; rx_total += seg->len; rx_segs++;
	if (--bulk_avail < 24) { bt_l2cap_chan_give_credits(chan, RX_CREDITS - bulk_avail); bulk_avail = RX_CREDITS; }
}
static void ctrl_seg_recv(struct bt_l2cap_chan *chan, size_t sdu_len, off_t seg_offset, struct net_buf_simple *seg)
{
	if (sdu_len == PING_SIZE && seg_offset == 0 && seg->len >= 8 && memcmp(seg->data, "PING", 4) == 0) {
		memcpy(echo_buf, seg->data, PING_SIZE); k_sem_give(&echo_sem);
	}
	if (--ctrl_avail < 24) { bt_l2cap_chan_give_credits(chan, RX_CREDITS - ctrl_avail); ctrl_avail = RX_CREDITS; }
}
static void bulk_conn(struct bt_l2cap_chan *chan) { ARG_UNUSED(chan); bulk_up = true; rx_bytes = rx_total = rx_segs = 0; printk("BULK up\n"); }
static void ctrl_conn(struct bt_l2cap_chan *chan) { ARG_UNUSED(chan); ctrl_up = true; printk("CTRL up\n"); }
static void bulk_disc(struct bt_l2cap_chan *chan) { ARG_UNUSED(chan); bulk_up = false; }
static void ctrl_disc(struct bt_l2cap_chan *chan) { ARG_UNUSED(chan); ctrl_up = false; }
static const struct bt_l2cap_chan_ops bulk_ops = { .connected = bulk_conn, .disconnected = bulk_disc, .seg_recv = bulk_seg_recv };
static const struct bt_l2cap_chan_ops ctrl_ops = { .connected = ctrl_conn, .disconnected = ctrl_disc, .seg_recv = ctrl_seg_recv };

static int accept_bulk(struct bt_conn *conn, struct bt_l2cap_server *s, struct bt_l2cap_chan **chan)
{ ARG_UNUSED(conn); ARG_UNUSED(s); bulk_chan.chan.ops = &bulk_ops; bulk_chan.rx.mtu = L2CAP_MTU; bulk_chan.rx.mps = L2CAP_MPS; *chan = &bulk_chan.chan; atomic_set(&bulk_chan.rx.credits, 0); /* seg_recv: drop stale credits */ bulk_avail = RX_CREDITS; bt_l2cap_chan_give_credits(&bulk_chan.chan, RX_CREDITS); return 0; }
static int accept_ctrl(struct bt_conn *conn, struct bt_l2cap_server *s, struct bt_l2cap_chan **chan)
{ ARG_UNUSED(conn); ARG_UNUSED(s); ctrl_chan.chan.ops = &ctrl_ops; ctrl_chan.rx.mtu = L2CAP_MTU; ctrl_chan.rx.mps = L2CAP_MPS; *chan = &ctrl_chan.chan; atomic_set(&ctrl_chan.rx.credits, 0); /* seg_recv: drop stale credits */ ctrl_avail = RX_CREDITS; bt_l2cap_chan_give_credits(&ctrl_chan.chan, RX_CREDITS); return 0; }
static struct bt_l2cap_server srv_bulk = { .psm = PSM_BULK, .sec_level = BT_SECURITY_L1, .accept = accept_bulk };
static struct bt_l2cap_server srv_ctrl = { .psm = PSM_CTRL, .sec_level = BT_SECURITY_L1, .accept = accept_ctrl };

static void echo_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	while (1) {
		k_sem_take(&echo_sem, K_FOREVER);
		if (!ctrl_up) { continue; }
		struct net_buf *buf = net_buf_alloc(&ctrl_tx, K_MSEC(100));
		if (!buf) { echo_fail++; continue; }
		net_buf_reserve(buf, BT_L2CAP_SDU_CHAN_SEND_RESERVE); net_buf_add_mem(buf, echo_buf, PING_SIZE);
		if (bt_l2cap_chan_send(&ctrl_chan.chan, buf) < 0) { net_buf_unref(buf); echo_fail++; } else { echoes++; }
	}
}
K_THREAD_DEFINE(echo_tid, 2048, echo_fn, NULL, NULL, NULL, 8, 0, 0);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};
static struct k_work_delayable adv_work;
static void adv_work_fn(struct k_work *w)
{ ARG_UNUSED(w); int e = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
  if (e) { k_work_reschedule(&adv_work, K_MSEC(200)); } else { printk("Advertising\n"); } }
static void start_adv(void) { k_work_reschedule(&adv_work, K_NO_WAIT); }

static void connected(struct bt_conn *conn, uint8_t err)
{ if (err) { printk("connect failed 0x%02x\n", err); return; } default_conn = bt_conn_ref(conn);
  struct bt_conn_info i; if (bt_conn_get_info(conn, &i) == 0) { printk("GAP connected interval=%u\n", i.le.interval); } }
static void disconnected(struct bt_conn *conn, uint8_t reason)
{ ARG_UNUSED(conn); printk("GAP disc 0x%02x\n", reason); if (default_conn) { bt_conn_unref(default_conn); default_conn = NULL; } bulk_up = ctrl_up = false; start_adv(); }
static void le_phy(struct bt_conn *conn, struct bt_conn_le_phy_info *p) { ARG_UNUSED(conn); printk("PHY tx=%u rx=%u\n", p->tx_phy, p->rx_phy); }
static void le_dle(struct bt_conn *conn, struct bt_conn_le_data_len_info *i) { ARG_UNUSED(conn); printk("DLE tx=%u rx=%u\n", i->tx_max_len, i->rx_max_len); }
#if defined(CONFIG_BT_FRAME_SPACE_UPDATE)
static void fsu_updated(struct bt_conn *conn, const struct bt_conn_le_frame_space_updated *p) { ARG_UNUSED(conn); if (p->status == 0) { fsu_spacing = p->frame_space; } }
#endif
BT_CONN_CB_DEFINE(cb) = { .connected = connected, .disconnected = disconnected, .le_phy_updated = le_phy, .le_data_len_updated = le_dle,
#if defined(CONFIG_BT_FRAME_SPACE_UPDATE)
	.frame_space_updated = fsu_updated,
#endif
};

static void sum_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	while (1) { k_msleep(SUMMARY_MS);
		if (bulk_up) { uint32_t bb = rx_bytes; rx_bytes = 0;
			printk("SINK rx: %u KB/s (%u B) fsu=%u | cum=%u segs=%u | echoes=%u fail=%u ctrl=%d\n",
			       bb / 1024, bb, (unsigned)fsu_spacing, rx_total, rx_segs, echoes, echo_fail, (int)ctrl_up); } }
}
K_THREAD_DEFINE(sum_tid, 1024, sum_fn, NULL, NULL, NULL, 10, 0, 0);

int main(void)
{
	if (bt_enable(NULL)) { printk("bt_enable failed\n"); return 0; }
	printk("\n=== TWO-CHANNEL CoC SINK (bulk 0x80 + control 0x81) ===\n");
	bt_l2cap_server_register(&srv_bulk);
	bt_l2cap_server_register(&srv_ctrl);
	printk("L2CAP servers on 0x%04x + 0x%04x\n", PSM_BULK, PSM_CTRL);
	k_work_init_delayable(&adv_work, adv_work_fn);
	start_adv();
	return 0;
}
