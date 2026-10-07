/*
 * nRF52832 DK — GATT DOWNLINK sink (peripheral). Zephyr 4.4.1.
 * Peripheral hosts a characteristic that accepts Write-Without-Response; the central
 * blasts 244-byte writes and we count bytes -> KB/s. Measures GATT central->peripheral
 * (downlink), to compare with CoC downlink (~150) and GATT uplink/notify (~44).
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#define SUMMARY_MS 1000

static struct bt_uuid_128 svc_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12340001, 0x1234, 0x1000, 0x8000, 0x00805f9b34fb));
static struct bt_uuid_128 chrc_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12340003, 0x1234, 0x1000, 0x8000, 0x00805f9b34fb));

static volatile uint32_t rx_bytes;
static volatile bool connected_flag;

static ssize_t write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn); ARG_UNUSED(attr); ARG_UNUSED(buf);
	ARG_UNUSED(offset); ARG_UNUSED(flags);
	rx_bytes += len;
	return len;
}

BT_GATT_SERVICE_DEFINE(dl_svc,
	BT_GATT_PRIMARY_SERVICE(&svc_uuid),
	BT_GATT_CHARACTERISTIC(&chrc_uuid.uuid,
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE, NULL, write_cb, NULL),
);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void start_adv(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("adv start failed (%d)\n", err);
	} else {
		printk("Advertising as '%s'\n", CONFIG_BT_DEVICE_NAME);
	}
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) { printk("connect failed 0x%02x\n", err); return; }
	struct bt_conn_info info;
	if (bt_conn_get_info(conn, &info) == 0) {
		printk("GAP connected: interval=%u\n", info.le.interval);
	}
	rx_bytes = 0;
	connected_flag = true;
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	printk("GAP disconnected 0x%02x\n", reason);
	connected_flag = false;
	start_adv();
}

static void le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *p)
{
	ARG_UNUSED(conn);
	printk("PHY: tx=%u rx=%u\n", p->tx_phy, p->rx_phy);
}

static void le_data_len_updated(struct bt_conn *conn,
				struct bt_conn_le_data_len_info *i)
{
	ARG_UNUSED(conn);
	printk("DLE: tx_max=%u rx_max=%u\n", i->tx_max_len, i->rx_max_len);
}

BT_CONN_CB_DEFINE(conn_cbs) = {
	.connected = connected,
	.disconnected = disconnected,
	.le_phy_updated = le_phy_updated,
	.le_data_len_updated = le_data_len_updated,
};

static void sum_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	while (1) {
		k_msleep(SUMMARY_MS);
		if (connected_flag) {
			uint32_t v = rx_bytes;
			rx_bytes = 0;
			printk("GATT-WRITE rx: %u KB/s  (%u bytes/s)\n", v / 1024, v);
		}
	}
}

K_THREAD_DEFINE(sum_tid, 1024, sum_fn, NULL, NULL, NULL, 10, 0, 0);

int main(void)
{
	int err = bt_enable(NULL);
	if (err) { printk("bt_enable failed %d\n", err); return 0; }
	printk("\n=== nRF52 GATT DOWNLINK sink (write-without-response, 4.4.1) ===\n");
	start_adv();
	return 0;
}
