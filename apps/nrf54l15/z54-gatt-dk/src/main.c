/*
 * nRF52832 DK — GATT NOTIFICATION throughput blaster (peripheral -> central).
 * Zephyr 4.4.1. Counterpart to the L2CAP CoC uplink test, to compare CoC vs GATT
 * on the same rig / same 244-byte single-packet payload / 2M+DLE+7.5ms.
 *
 * Peripheral hosts a GATT service with one NOTIFY characteristic; once the central
 * subscribes (writes the CCC), it blasts 244-byte notifications continuously.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#define NOTIFY_SIZE 244          /* ATT_MTU(247) - 3; single-packet, matches CoC test */
#define SUMMARY_MS  1000

/* 128-bit service + characteristic UUIDs */
static struct bt_uuid_128 svc_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12340001, 0x1234, 0x1000, 0x8000, 0x00805f9b34fb));
static struct bt_uuid_128 chrc_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12340002, 0x1234, 0x1000, 0x8000, 0x00805f9b34fb));

static volatile bool notify_enabled;
static volatile uint32_t tx_bytes;
static K_SEM_DEFINE(go, 0, 1);

static void ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	printk("CCC: notify %s\n", notify_enabled ? "ENABLED" : "disabled");
	if (notify_enabled) {
		tx_bytes = 0;
		k_sem_give(&go);
	}
}

BT_GATT_SERVICE_DEFINE(tput_svc,
	BT_GATT_PRIMARY_SERVICE(&svc_uuid),
	BT_GATT_CHARACTERISTIC(&chrc_uuid.uuid, BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE, NULL, NULL, NULL),
	BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);
/* tput_svc.attrs[1] = the characteristic value attribute (notify source) */

/* ---- advertising ---- */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void start_adv(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
	printk(err ? "adv start failed (%d)\n" : "Advertising as '%s'\n",
	       err ? err : (int)0);
	if (!err) {
		printk("(name '%s')\n", CONFIG_BT_DEVICE_NAME);
	}
}

/* ---- GAP ---- */
static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) { printk("connect failed 0x%02x\n", err); return; }
	struct bt_conn_info info;
	if (bt_conn_get_info(conn, &info) == 0) {
		printk("GAP connected: interval=%u\n", info.le.interval);
	}
	/* uplink fix: peripheral raises its own TX DLE (else notify TX stays at 27 octets) */
	int e = bt_conn_le_data_len_update(conn, BT_LE_DATA_LEN_PARAM_MAX);
	printk("peripheral DLE req rc=%d\n", e);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	printk("GAP disconnected 0x%02x\n", reason);
	notify_enabled = false;
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

/* ---- notify blast thread ---- */
static void blast_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	static uint8_t payload[NOTIFY_SIZE];
	memset(payload, 'G', sizeof(payload));

	while (1) {
		k_sem_take(&go, K_FOREVER);
		printk("blasting %d-byte notifications...\n", NOTIFY_SIZE);
		while (notify_enabled) {
			int e = bt_gatt_notify(NULL, &tput_svc.attrs[1], payload, NOTIFY_SIZE);
			if (e == -ENOMEM || e == -EAGAIN) {
				k_msleep(1); /* TX buffers full — backpressure */
				continue;
			}
			if (e < 0) {
				if (e != -ENOTCONN) {
					printk("notify err %d\n", e);
				}
				break;
			}
			tx_bytes += NOTIFY_SIZE;
		}
	}
}

K_THREAD_DEFINE(blast_tid, 2048, blast_fn, NULL, NULL, NULL, 10, 0, 0);

/* ---- summary thread ---- */
static void sum_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	while (1) {
		k_msleep(SUMMARY_MS);
		if (notify_enabled) {
			uint32_t v = tx_bytes;
			tx_bytes = 0;
			printk("GATT-NOTIFY tx: %u KB/s  (%u bytes/s)\n", v / 1024, v);
		}
	}
}

K_THREAD_DEFINE(sum_tid, 1024, sum_fn, NULL, NULL, NULL, 10, 0, 0);

int main(void)
{
	int err = bt_enable(NULL);
	if (err) { printk("bt_enable failed %d\n", err); return 0; }
	printk("\n=== nRF52 GATT NOTIFY blaster (Zephyr 4.4.1) ===\n");
	start_adv();
	return 0;
}
