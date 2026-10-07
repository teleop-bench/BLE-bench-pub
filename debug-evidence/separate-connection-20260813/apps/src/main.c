/*
 * nRF54L15 — SEPARATE-CONNECTION latency-under-load central.
 * Holds TWO connections at once: conn_a (safety) carries the stop-signal ping-pong;
 * conn_b (bulk) carries a saturating bulk blast. Two connections are scheduled as
 * separate connection events, so bulk on B cannot head-of-line-block the ping on A.
 * Measures the stop-signal RTT on A while B is saturated — the hard-latency-bound test.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/uuid.h>

#define SAFETY_NAME "safety-peer"
#define BULK_NAME   "bulk-peer"

/* ping char (write-without-response + notify echo) and bulk-sink char (write) */
static const struct bt_uuid_128 ping_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12340011, 0x1234, 0x1000, 0x8000, 0x00805f9b34fb));
static const struct bt_uuid_128 bulk_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12340013, 0x1234, 0x1000, 0x8000, 0x00805f9b34fb));

static struct bt_conn *conn_a;      /* safety */
static struct bt_conn *conn_b;      /* bulk */
static uint16_t ping_handle;        /* value handle on conn_a */
static uint16_t bulk_handle;        /* value handle on conn_b */
static volatile bool ping_ready, bulk_ready;

static struct bt_gatt_subscribe_params sub_a;
static struct bt_gatt_discover_params disc_a, ccc_a, disc_b;

static K_SEM_DEFINE(pong_sem, 0, 1);
static volatile uint32_t last_seq, seq_bad;

/* RTT stats (us): windowed reset each report + cumulative tail */
static uint32_t n, rtt_min = 0xffffffff, rtt_max, timeouts;
static uint64_t rtt_sum;
static uint32_t tot_n, tot_to, maxever, g2, g5, g15, g20, g30, probes;
static volatile uint32_t bulk_sent;   /* bulk writes accepted (throughput proxy) */

static void start_scan(void);

/* ---- pong notify on conn_a ---- */
static uint8_t notify_cb(struct bt_conn *conn, struct bt_gatt_subscribe_params *p,
			 const void *data, uint16_t length)
{
	ARG_UNUSED(conn); ARG_UNUSED(p);
	if (!data) { ping_ready = false; return BT_GATT_ITER_STOP; }
	if (length == 8 && sys_get_le32(data) != last_seq) { seq_bad++; }
	k_sem_give(&pong_sem);
	return BT_GATT_ITER_CONTINUE;
}

/* ---- discovery ---- */
static uint8_t disc_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
		       struct bt_gatt_discover_params *params)
{
	if (!attr) { return BT_GATT_ITER_STOP; }

	if (params == &disc_b) {                       /* bulk conn: just the char */
		bulk_handle = bt_gatt_attr_value_handle(attr);
		bulk_ready = true;
		printk("BULK char handle=%u — bulk starts\n", bulk_handle);
		return BT_GATT_ITER_STOP;
	}
	/* safety conn */
	if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		ping_handle = bt_gatt_attr_value_handle(attr);
		sub_a.value_handle = ping_handle;
		ccc_a.uuid = BT_UUID_GATT_CCC;
		ccc_a.start_handle = attr->handle + 2;
		ccc_a.end_handle = 0xffff;
		ccc_a.type = BT_GATT_DISCOVER_DESCRIPTOR;
		ccc_a.func = disc_cb;
		bt_gatt_discover(conn, &ccc_a);
		return BT_GATT_ITER_STOP;
	}
	/* CCC descriptor -> subscribe for pongs */
	sub_a.notify = notify_cb;
	sub_a.value = BT_GATT_CCC_NOTIFY;
	sub_a.ccc_handle = attr->handle;
	int e = bt_gatt_subscribe(conn, &sub_a);
	if (!e || e == -EALREADY) {
		ping_ready = true;
		printk("SAFETY subscribed, ping vh=%u — pings start\n", ping_handle);
	} else {
		printk("subscribe rc=%d\n", e);
	}
	return BT_GATT_ITER_STOP;
}

static void start_disc(struct bt_conn *conn, bool safety)
{
	if (safety) {
		disc_a.uuid = &ping_uuid.uuid;
		disc_a.func = disc_cb;
		disc_a.start_handle = 0x0001;
		disc_a.end_handle = 0xffff;
		disc_a.type = BT_GATT_DISCOVER_CHARACTERISTIC;
		bt_gatt_discover(conn, &disc_a);
	} else {
		disc_b.uuid = &bulk_uuid.uuid;
		disc_b.func = disc_cb;
		disc_b.start_handle = 0x0001;
		disc_b.end_handle = 0xffff;
		disc_b.type = BT_GATT_DISCOVER_CHARACTERISTIC;
		bt_gatt_discover(conn, &disc_b);
	}
}

/* ---- GAP ---- */
static void le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *p)
{
	if (p->tx_phy == BT_GAP_LE_PHY_2M && p->rx_phy == BT_GAP_LE_PHY_2M) {
		bt_conn_le_data_len_update(conn, BT_LE_DATA_LEN_PARAM_MAX);
	}
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	bool safety = (conn == conn_a);
	if (err) {
		printk("connect failed 0x%02x (%s)\n", err, safety ? "safety" : "bulk");
		if (safety) { bt_conn_unref(conn_a); conn_a = NULL; }
		else { bt_conn_unref(conn_b); conn_b = NULL; }
		start_scan();
		return;
	}
	struct bt_conn_info info;
	uint16_t iv = (bt_conn_get_info(conn, &info) == 0) ? info.le.interval : 0;
	printk("GAP connected: %s interval=%u\n", safety ? "SAFETY" : "BULK", iv);
	/* No explicit 2M PHY update: on two concurrent connections the PHY-update
	 * procedure perturbs the scheduler and drops the 2nd link. RTT is interval-
	 * bound, so 1M is fine for the latency test; auto-DLE still applies. */
	start_disc(conn, safety);
	if (!conn_a || !conn_b) { start_scan(); }   /* still need the other peer */
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	bool safety = (conn == conn_a);
	printk("GAP disconnected 0x%02x (%s)\n", reason, safety ? "safety" : "bulk");
	if (safety) { ping_ready = false; bt_conn_unref(conn_a); conn_a = NULL; }
	else { bulk_ready = false; bt_conn_unref(conn_b); conn_b = NULL; }
	start_scan();
}

BT_CONN_CB_DEFINE(conn_cbs) = {
	.connected = connected,
	.disconnected = disconnected,
	.le_phy_updated = le_phy_updated,
};

/* ---- scan: connect to whichever named peer we still need ---- */
struct name_match { bool safety, bulk; };
static bool ad_name(struct bt_data *data, void *ud)
{
	struct name_match *m = ud;
	if (data->type == BT_DATA_NAME_COMPLETE || data->type == BT_DATA_NAME_SHORTENED) {
		if (data->data_len == strlen(SAFETY_NAME) &&
		    !memcmp(data->data, SAFETY_NAME, data->data_len)) { m->safety = true; return false; }
		if (data->data_len == strlen(BULK_NAME) &&
		    !memcmp(data->data, BULK_NAME, data->data_len)) { m->bulk = true; return false; }
	}
	return true;
}
static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	ARG_UNUSED(rssi); ARG_UNUSED(type);
	struct name_match m = {0};
	bt_data_parse(ad, ad_name, &m);
	struct bt_conn **slot = NULL;
	if (m.safety && !conn_a) { slot = &conn_a; }
	else if (m.bulk && !conn_b) { slot = &conn_b; }
	if (!slot) { return; }
	bt_le_scan_stop();
	/* safety: 7.5 ms (low latency); bulk: 50 ms (throughput-favorable + leaves the
	 * scheduler room — two 7.5 ms connections drop the second on the open controller). */
	struct bt_le_conn_param *param = (slot == &conn_a)
		? BT_LE_CONN_PARAM(6, 6, 0, 400)
		: BT_LE_CONN_PARAM(40, 40, 0, 400);
	if (bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, param, slot)) { start_scan(); }
}
static void start_scan(void)
{
	if (conn_a && conn_b) { return; }   /* both connected */
	struct bt_le_scan_param sp = { .type = BT_LE_SCAN_TYPE_ACTIVE, .options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL, .window = BT_GAP_SCAN_FAST_WINDOW };
	int e = bt_le_scan_start(&sp, device_found);
	if (e && e != -EALREADY) { printk("scan rc=%d\n", e); }
}

/* ---- ping thread: serialized RTT on conn_a ---- */
static void ping_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	uint8_t payload[8];
	uint32_t seq = 0;
	while (1) {
		if (!ping_ready || !conn_a) { k_msleep(50); continue; }
		last_seq = seq;
		sys_put_le32(seq++, payload);
		probes++;
		struct bt_conn *pc = bt_conn_ref(conn_a);
		if (!pc) { k_msleep(2); continue; }
		uint32_t t0 = k_cycle_get_32();
		int e = bt_gatt_write_without_response(pc, ping_handle, payload, sizeof(payload), false);
		bt_conn_unref(pc);
		if (e) { k_msleep(2); continue; }
		if (k_sem_take(&pong_sem, K_MSEC(200)) == 0) {
			uint32_t us = k_cyc_to_us_near32(k_cycle_get_32() - t0);
			n++; rtt_sum += us; tot_n++;
			if (us < rtt_min) rtt_min = us;
			if (us > rtt_max) rtt_max = us;
			if (us > maxever) maxever = us;
			if (us > 2000) g2++;
			if (us > 5000) g5++;
			if (us > 15000) g15++;
			if (us > 20000) g20++;
			if (us > 30000) g30++;
		} else { timeouts++; tot_to++; }
		k_msleep(3);
	}
}
K_THREAD_DEFINE(ping_tid, 2048, ping_fn, NULL, NULL, NULL, 7, 0, 0);

/* ---- bulk thread: saturate conn_b ---- */
static void bulk_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	static uint8_t buf[244];
	memset(buf, 'B', sizeof(buf));
	while (1) {
		if (!bulk_ready || !conn_b) { k_msleep(50); continue; }
		struct bt_conn *pc = bt_conn_ref(conn_b);
		if (!pc) { k_msleep(2); continue; }
		int e = bt_gatt_write_without_response(pc, bulk_handle, buf, sizeof(buf), false);
		bt_conn_unref(pc);
		if (e == -ENOMEM) { k_yield(); }
		else if (e) { k_msleep(2); }
		else { bulk_sent++; }
	}
}
K_THREAD_DEFINE(bulk_tid, 2048, bulk_fn, NULL, NULL, NULL, 8, 0, 0);

/* ---- report: per-second RTT + tail + bulk rate ---- */
static void report_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	uint32_t sec = 0, prev_bulk = 0;
	while (1) {
		k_msleep(1000);
		sec++;
		uint32_t bs = bulk_sent;
		printk("t=%us SAFETY RTT mean=%u min=%u max=%u n=%u to=%u | "
		       "tot_n=%u tot_to=%u maxever=%u >2ms=%u >5ms=%u >15ms=%u >20ms=%u >30ms=%u "
		       "| BULK writes=%u(+%u/s ~%uKB/s) seqbad=%u a=%d b=%d\n",
		       sec, n ? (uint32_t)(rtt_sum / n) : 0, n ? rtt_min : 0, rtt_max, n, timeouts,
		       tot_n, tot_to, maxever, g2, g5, g15, g20, g30,
		       bs, bs - prev_bulk, (bs - prev_bulk) * 244 / 1024, seq_bad,
		       conn_a ? 1 : 0, conn_b ? 1 : 0);
		prev_bulk = bs;
		n = 0; rtt_sum = 0; rtt_min = 0xffffffff; rtt_max = 0; timeouts = 0;
	}
}
K_THREAD_DEFINE(report_tid, 1024, report_fn, NULL, NULL, NULL, 7, 0, 0);

int main(void)
{
	int err = bt_enable(NULL);
	if (err) { printk("bt_enable %d\n", err); return 0; }
	printk("\n=== nRF54L15 SEPARATE-CONNECTION central (safety + bulk) ===\n");
	start_scan();
	return 0;
}
