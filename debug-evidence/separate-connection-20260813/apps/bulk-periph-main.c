/* nRF52832 DK — bulk-sink peripheral ("bulk-peer"). Counts bytes on the bulk char. */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/uuid.h>

static const struct bt_uuid_128 svc_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12340010, 0x1234, 0x1000, 0x8000, 0x00805f9b34fb));
static const struct bt_uuid_128 blk_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12340013, 0x1234, 0x1000, 0x8000, 0x00805f9b34fb));

static volatile uint32_t blk_bytes;
static volatile bool up;

static ssize_t blk_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn); ARG_UNUSED(attr); ARG_UNUSED(buf); ARG_UNUSED(offset); ARG_UNUSED(flags);
	blk_bytes += len;
	return len;
}

BT_GATT_SERVICE_DEFINE(svc,
	BT_GATT_PRIMARY_SERVICE(&svc_uuid),
	BT_GATT_CHARACTERISTIC(&blk_uuid.uuid, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE, NULL, blk_write, NULL),
);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void adv(void)
{
	int e = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
	printk("adv '%s' rc=%d\n", CONFIG_BT_DEVICE_NAME, e);
}
static void connected(struct bt_conn *conn, uint8_t err)
{
	ARG_UNUSED(conn);
	if (err) { printk("conn fail 0x%02x\n", err); return; }
	up = true; blk_bytes = 0;
	printk("BULK connected\n");
}
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn); up = false;
	printk("BULK disconnected 0x%02x\n", reason);
	adv();
}
static void le_data_len_updated(struct bt_conn *conn, struct bt_conn_le_data_len_info *i)
{
	ARG_UNUSED(conn);
	printk("BULK DLE tx=%u rx=%u\n", i->tx_max_len, i->rx_max_len);
}
BT_CONN_CB_DEFINE(cbs) = { .connected = connected, .disconnected = disconnected,
			   .le_data_len_updated = le_data_len_updated };

static void rep(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	while (1) {
		k_msleep(1000);
		if (up) { uint32_t x = blk_bytes; blk_bytes = 0;
			printk("BULK rx %u KB/s\n", x / 1024); }
	}
}
K_THREAD_DEFINE(rep_tid, 1024, rep, NULL, NULL, NULL, 9, 0, 0);

int main(void)
{
	int err = bt_enable(NULL);
	if (err) { printk("bt_enable %d\n", err); return 0; }
	printk("\n=== nRF52832 bulk-sink peripheral ===\n");
	adv();
	return 0;
}
