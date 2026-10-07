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
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/l2cap.h>

#define L2CAP_PSM   0x0080
#define L2CAP_MTU   512
#define SDU_SIZE    480
#define TARGET_NAME "zenoh-nrf-l2cap"

/* Zephyr 4.4: NET_BUF_POOL_FIXED_DEFINE gained a user-data-size arg (here 8, per
 * the in-tree l2cap_coc_initiator sample). The central is a pure sender (SINK), so
 * it needs no RX pool / alloc_buf. */
NET_BUF_POOL_FIXED_DEFINE(tx_pool, 64, BT_L2CAP_SDU_BUF_SIZE(L2CAP_MTU), 8, NULL);

/* ---- CoC FSU-off saturation preflight: sender + occupancy accounting ---- */
/* Controller occupancy instrument (shared lll_conn.c, fsu-m0): packets/event. */
#if defined(CONFIG_BT_LL_SW_SPLIT)   /* occupancy instrument: patched OPEN controller only */
extern volatile uint16_t lll_conn_q2_pe[64];
extern volatile uint16_t lll_conn_q2_pe_max;
extern volatile uint32_t lll_conn_q2_events;
#endif

static volatile uint32_t sent_sdus;   /* bt_l2cap_chan_send accepted */
static volatile uint32_t eagain_cnt;  /* -EAGAIN: credit window exhausted (flow control) */
static volatile uint32_t poolfail_cnt;/* app SDU pool exhausted (K_NO_WAIT alloc == NULL) */

static struct bt_conn *default_conn;
static struct bt_l2cap_le_chan le_chan;
static K_SEM_DEFINE(blast_sem, 0, 1);
static volatile bool chan_up;
static bool l2cap_started;
static struct k_work_delayable dle_work;

static void start_scan(void);

/* ---- L2CAP client channel ops ---- */

static volatile uint32_t cen_rx_bytes;   /* duplex: SDU bytes received from the peripheral (uplink) */
static void chan_seg_recv(struct bt_l2cap_chan *chan, size_t sdu_len, off_t seg_offset,
			  struct net_buf_simple *seg)
{
	ARG_UNUSED(sdu_len); ARG_UNUSED(seg_offset);
	cen_rx_bytes += seg->len;
	bt_l2cap_chan_give_credits(chan, 1);   /* replenish uplink window */
}

static void chan_connected_cb(struct bt_l2cap_chan *chan)
{
	struct bt_l2cap_le_chan *le = CONTAINER_OF(chan, struct bt_l2cap_le_chan, chan);
	printk("L2CAP connected: tx.mtu=%u tx.mps=%u tx.credits=%u rx.mtu=%u rx.mps=%u\n",
	       le->tx.mtu, le->tx.mps, (unsigned int)atomic_get(&le->tx.credits),
	       le->rx.mtu, le->rx.mps);
	struct bt_conn_info info;
	if (default_conn && bt_conn_get_info(default_conn, &info) == 0) {
		printk("GATE conn: interval=%u (units 1.25ms) => %u.%02u ms\n",
		       info.le.interval, info.le.interval * 5 / 4,
		       (info.le.interval * 5 % 4) * 25);
	}
	chan_up = true;
	bt_l2cap_chan_give_credits(&le_chan.chan, 64);   /* duplex: open the uplink window */
#if defined(CONFIG_APP_AUTO_FSU) && (CONFIG_APP_FSU_MAX_US > 0)
	/* settled point (PHY=2M, DLE, L2CAP up): request FSU now. f52 arm reduces tIFS to
	 * 52us; f150 arm requests [150..150] (runs the same procedure, grants no reduction)
	 * so the goodput comparison isolates the granted spacing. Peer proves application
	 * via its own Q3FSU-DONE role=P. */
	if (default_conn) {
		const struct bt_conn_le_frame_space_update_param fp = {
			.phys = BT_HCI_LE_FRAME_SPACE_UPDATE_PHY_2M_MASK,
			.spacing_types = BT_HCI_LE_FRAME_SPACE_UPDATE_SPACING_TYPE_IFS_ACL_CP_MASK |
					 BT_HCI_LE_FRAME_SPACE_UPDATE_SPACING_TYPE_IFS_ACL_PC_MASK,
			.frame_space_min = CONFIG_APP_FSU_MIN_US,
			.frame_space_max = CONFIG_APP_FSU_MAX_US,
		};
		int rc = bt_conn_le_frame_space_update(default_conn, &fp);
		printk("Q3FSU-REQ role=C auto min=%u max=%u phys=0x%x rc=%d\n",
		       CONFIG_APP_FSU_MIN_US, CONFIG_APP_FSU_MAX_US, (unsigned)fp.phys, rc);
	}
#endif
	k_sem_give(&blast_sem);
}

static volatile uint16_t fsu_spacing = 0;  /* latched negotiated frame space (us); printed in RPT */
#if defined(CONFIG_BT_FRAME_SPACE_UPDATE)
static void fsu_updated(struct bt_conn *conn, const struct bt_conn_le_frame_space_updated *p)
{
	ARG_UNUSED(conn);
	if (p->status == 0) { fsu_spacing = p->frame_space; }
	printk("Q3FSU-DONE role=C status=0x%02x spacing=%u types=0x%x phys=0x%x initiator=%d\n",
	       p->status, p->frame_space, p->spacing_types, p->phys, (int)p->initiator);
}
#endif

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

/* ---- GAP: 2M PHY -> DLE -> open L2CAP ---- */

static void open_l2cap(void)
{
	if (l2cap_started || !default_conn) {
		return;
	}
	l2cap_started = true;
	le_chan.chan.ops = &chan_ops;
	le_chan.rx.mtu = L2CAP_MTU;
	le_chan.rx.mps = 247;              /* seg_recv: ~one DLE-251 PDU per segment (uplink) */
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
#if defined(CONFIG_BT_FRAME_SPACE_UPDATE)
	.frame_space_updated = fsu_updated,
#endif
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

	struct bt_le_conn_param *param = BT_LE_CONN_PARAM(CONFIG_APP_CONN_INT_UNITS, CONFIG_APP_CONN_INT_UNITS, 0, 400); /* swept */
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

		printk("blasting %u-byte SDUs...\n", SDU_SIZE);
		while (chan_up) {
			/* K_FOREVER: block precisely until a pool buffer frees, then send
			 * immediately — no polling gap. (A K_NO_WAIT + k_msleep(1) retry loop
			 * throttles the pump on its own 1 ms granularity and suppresses
			 * occupancy — an instrument artifact, not the link's ceiling.)
			 * The blocking alloc IS the pool backpressure; credits are tracked
			 * separately via -EAGAIN below. */
			struct net_buf *buf = net_buf_alloc(&tx_pool, K_FOREVER);
			net_buf_reserve(buf, BT_L2CAP_SDU_CHAN_SEND_RESERVE);
			net_buf_add_mem(buf, payload, SDU_SIZE);
			int e = bt_l2cap_chan_send(&le_chan.chan, buf);
			if (e == -EAGAIN) {
				net_buf_unref(buf);
				eagain_cnt++;
				k_msleep(1);
				continue;
			}
			if (e < 0) {
				net_buf_unref(buf);
				break;
			}
			sent_sdus++;
		}
	}
}

K_THREAD_DEFINE(blast_tid, 2048, blast_fn, NULL, NULL, NULL, 10, 0, 0);

/* ---- reporter: sender accounting + controller packets/event occupancy ----
 * Low cadence (10 s) and off the blast thread so it doesn't perturb the pump. */
#define REPORT_MS 10000
static void report_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
#if defined(CONFIG_BT_LL_SW_SPLIT)
	static uint16_t prev_pe[64];
	uint32_t prev_ev = 0;
#endif
	uint32_t prev_sent = 0, prev_eag = 0, prev_pf = 0;
	while (1) {
		k_msleep(REPORT_MS);
		if (!chan_up) { continue; }

		uint32_t s = sent_sdus, eag = eagain_cnt, pf = poolfail_cnt;
#if defined(CONFIG_BT_LL_SW_SPLIT)
		/* snapshot OPEN-controller occupancy histogram + diff since last window */
		uint16_t pe[64];
		for (int i = 0; i < 64; i++) { pe[i] = lll_conn_q2_pe[i]; }
		uint32_t ev = lll_conn_q2_events;
		uint32_t d_ev = ev - prev_ev;
		uint64_t d_pkts = 0;
		uint32_t d_nonempty = 0;
		for (int i = 0; i < 64; i++) {
			uint16_t d = pe[i] - prev_pe[i];
			d_pkts += (uint64_t)i * d;
			if (i > 0) { d_nonempty += d; }
		}
		uint32_t mean_x100 = d_ev ? (uint32_t)(d_pkts * 100 / d_ev) : 0;
		printk("RPT occ: events=%u mean_pkts/ev=%u.%02u max=%u nonempty_ev=%u fsu=%u | "
		       "sender: sent=%u(+%u SDU) eagain=%u(+%u) poolfail=%u(+%u)\n",
		       d_ev, mean_x100 / 100, mean_x100 % 100, lll_conn_q2_pe_max, d_nonempty,
		       (unsigned)fsu_spacing,
		       s, s - prev_sent, eag, eag - prev_eag, pf, pf - prev_pf);
		for (int i = 0; i < 64; i++) { prev_pe[i] = pe[i]; }
		prev_ev = ev;
#else
		/* SDC: no per-event occupancy counters available — sender + fsu only */
		printk("RPT occ: (SDC no-occ) fsu=%u | "
		       "sender: sent=%u(+%u SDU) eagain=%u(+%u) poolfail=%u(+%u)\n",
		       (unsigned)fsu_spacing,
		       s, s - prev_sent, eag, eag - prev_eag, pf, pf - prev_pf);
#endif
		prev_sent = s; prev_eag = eag; prev_pf = pf;
	}
}
K_THREAD_DEFINE(report_tid, 1536, report_fn, NULL, NULL, NULL, 11, 0, 0);

/* duplex uplink reporter: cumulative bytes received from the peripheral, 1 s cadence */
static void cenrx_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	while (1) {
		k_msleep(1000);
		if (chan_up) { printk("CENRX cum_total=%u B\n", (unsigned)cen_rx_bytes); }
	}
}
K_THREAD_DEFINE(cenrx_tid, 1024, cenrx_fn, NULL, NULL, NULL, 12, 0, 0);

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
