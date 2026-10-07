/*
 * nRF52840 dongle — GATT NOTIFICATION throughput SINK (central).
 * Zephyr 4.4.1. Connects to the GATT notify blaster, drives 2M->DLE->MTU(247),
 * discovers the notify characteristic + CCC, subscribes, and counts bytes -> KB/s.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#define TARGET_NAME "zenoh-nrf-l2cap"
#define SUMMARY_MS  1000

static struct bt_uuid_128 chrc_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12340002, 0x1234, 0x1000, 0x8000, 0x00805f9b34fb));

static struct bt_conn *default_conn;
static volatile uint32_t rx_bytes;
static volatile bool subscribed;

static struct bt_gatt_discover_params disc_params;
static struct bt_gatt_subscribe_params sub_params;
static struct bt_gatt_exchange_params mtu_params;

static void start_scan(void);

/* ---- notification sink ---- */
static uint8_t notify_cb(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
			 const void *data, uint16_t length)
{
	ARG_UNUSED(conn); ARG_UNUSED(params);
	if (!data) { /* unsubscribed */
		subscribed = false;
		return BT_GATT_ITER_STOP;
	}
	rx_bytes += length;
	return BT_GATT_ITER_CONTINUE;
}

/* ---- discovery: characteristic -> CCC -> subscribe ---- */
static uint8_t discover_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   struct bt_gatt_discover_params *params)
{
	if (!attr) {
		printk("discover complete (no more)\n");
		return BT_GATT_ITER_STOP;
	}

	if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		sub_params.value_handle = bt_gatt_attr_value_handle(attr);
		printk("char found: value_handle=%u\n", sub_params.value_handle);
		/* now find the CCC descriptor just after the value */
		disc_params.uuid = BT_UUID_GATT_CCC;
		disc_params.start_handle = attr->handle + 2;
		disc_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
		int e = bt_gatt_discover(conn, &disc_params);
		if (e) { printk("CCC discover rc=%d\n", e); }
		return BT_GATT_ITER_STOP;
	}

	/* descriptor (CCC) */
	sub_params.notify = notify_cb;
	sub_params.value = BT_GATT_CCC_NOTIFY;
	sub_params.ccc_handle = attr->handle;
	int e = bt_gatt_subscribe(conn, &sub_params);
	if (e && e != -EALREADY) {
		printk("subscribe rc=%d\n", e);
	} else {
		subscribed = true;
		rx_bytes = 0;
		printk("SUBSCRIBED — counting notifications\n");
	}
	return BT_GATT_ITER_STOP;
}

static void start_discovery(struct bt_conn *conn)
{
	disc_params.uuid = &chrc_uuid.uuid;
	disc_params.func = discover_cb;
	disc_params.start_handle = 0x0001;
	disc_params.end_handle = 0xffff;
	disc_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
	int e = bt_gatt_discover(conn, &disc_params);
	printk("discover start rc=%d\n", e);
}

/* ---- MTU exchange ---- */
static void mtu_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_exchange_params *p)
{
	ARG_UNUSED(p);
	printk("MTU exchanged: err=%u, mtu=%u\n", err, bt_gatt_get_mtu(conn));
	start_discovery(conn);
}

/* ---- GAP: 2M -> DLE -> MTU -> discover ---- */
static void le_data_len_updated(struct bt_conn *conn,
				struct bt_conn_le_data_len_info *info)
{
	printk("DLE: tx_max=%u rx_max=%u\n", info->tx_max_len, info->rx_max_len);
	mtu_params.func = mtu_cb;
	int e = bt_gatt_exchange_mtu(conn, &mtu_params);
	printk("mtu exchange rc=%d\n", e);
}

static void le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *param)
{
	printk("PHY: tx=%u rx=%u\n", param->tx_phy, param->rx_phy);
	if (param->tx_phy == BT_GAP_LE_PHY_2M && param->rx_phy == BT_GAP_LE_PHY_2M) {
		int e = bt_conn_le_data_len_update(conn, BT_LE_DATA_LEN_PARAM_MAX);
		printk("dle req rc=%d\n", e);
	}
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("connect failed 0x%02x\n", err);
		bt_conn_unref(default_conn); default_conn = NULL;
		start_scan();
		return;
	}
	printk("GAP connected\n");
	int e = bt_conn_le_phy_update(conn, BT_CONN_LE_PHY_PARAM_2M);
	printk("phy req rc=%d\n", e);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	printk("GAP disconnected 0x%02x\n", reason);
	subscribed = false;
	if (default_conn) { bt_conn_unref(default_conn); default_conn = NULL; }
	start_scan();
}

BT_CONN_CB_DEFINE(conn_cbs) = {
	.connected = connected,
	.disconnected = disconnected,
	.le_phy_updated = le_phy_updated,
	.le_data_len_updated = le_data_len_updated,
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
	ARG_UNUSED(rssi);
	if (default_conn) { return; }
	if (type != BT_GAP_ADV_TYPE_ADV_IND && type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
		return;
	}
	bool match = false;
	bt_data_parse(ad, ad_match_name, &match);
	if (!match) { return; }
	bt_le_scan_stop();
	struct bt_le_conn_param *param = BT_LE_CONN_PARAM(6, 6, 0, 400);
	int e = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, param, &default_conn);
	if (e) { printk("create conn rc=%d\n", e); start_scan(); }
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

/* ---- summary ---- */
static void sum_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	while (1) {
		k_msleep(SUMMARY_MS);
		if (subscribed) {
			uint32_t v = rx_bytes;
			rx_bytes = 0;
			printk("GATT-NOTIFY rx: %u KB/s  (%u bytes/s)\n", v / 1024, v);
		}
	}
}

K_THREAD_DEFINE(sum_tid, 1024, sum_fn, NULL, NULL, NULL, 10, 0, 0);

int main(void)
{
	int err = bt_enable(NULL);
	if (err) { printk("bt_enable failed %d\n", err); return 0; }
	printk("\n=== nRF52840 GATT NOTIFY sink (Zephyr 4.4.1) ===\n");
	start_scan();
	return 0;
}
