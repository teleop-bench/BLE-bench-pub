/*
 * MINIMAL REPRO — L2CAP CoC central (INITIATOR + RECEIVER, self-reboots).
 * Derived from Zephyr's samples/bluetooth/l2cap_coc_initiator (Apache-2.0). The only
 * changes vs the stock sample: (1) it does NOT send (it is the receiver), and (2) it
 * REBOOTS ~10 s after the channel connects, to exercise the peripheral's TX recovery
 * across a fresh-central reconnect. See the acceptor for the bug description.
 *
 * DELIBERATELY BUGGY (keep as is): the default build grants its credit window only after connect, so the acceptor's
 * channel opens with 0 TX credits; that is the trigger of the Zephyr 4.4 host stall this pair reproduces
 * (debug-evidence/zephyr-l2cap-zero-credit-repro-20261007). Do not copy this pattern. Build with
 * -DCREDITS_IN_REQUEST=1 for the correct pattern (window in the connection request) used as the control.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>

#define PSM          0x29
#define REBOOT_ENABLED
#define REBOOT_AFTER 25   /* first 25 s show the HEALTHY baseline, then reboot -> wedge */
#define RX_CREDITS   20

static struct bt_conn *default_conn;
static struct k_work_delayable reboot_work;
static uint32_t n_recv;

static void start_scan(void);

static void reboot_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	printk(">>> central self-reboot (received %u SDUs) <<<\n", n_recv);
	sys_reboot(SYS_REBOOT_COLD);
}

/* Receive with EXPLICIT credit replenishment (seg_recv). The stock sample's implicit
 * credits stall a continuous sender after a few packets — that would mask the bug. */
static void client_seg_recv(struct bt_l2cap_chan *chan, size_t sdu_len,
			    off_t seg_offset, struct net_buf_simple *seg)
{
	ARG_UNUSED(sdu_len); ARG_UNUSED(seg_offset);
	n_recv += seg->len;
	bt_l2cap_chan_give_credits(chan, 1);   /* keep the sender fed */
}
static void client_chan_connected(struct bt_l2cap_chan *chan)
{
	printk("L2CAP channel connected\n");
#ifndef CREDITS_IN_REQUEST
	bt_l2cap_chan_give_credits(chan, RX_CREDITS);   /* initial window (after connect: the peer starts at 0 credits) */
#endif
#ifdef REBOOT_ENABLED
	printk("will reboot in %d s\n", REBOOT_AFTER);
	k_work_reschedule(&reboot_work, K_SECONDS(REBOOT_AFTER));
#endif
}
static void client_chan_disconnected(struct bt_l2cap_chan *chan)
{
	ARG_UNUSED(chan);
	printk("L2CAP channel disconnected\n");
}

static struct bt_l2cap_chan_ops client_ops = {
	.seg_recv = client_seg_recv,
	.connected = client_chan_connected,
	.disconnected = client_chan_disconnected,
};
static struct bt_l2cap_le_chan client_chan = { .chan.ops = &client_ops };

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed (err %u)\n", err);
		bt_conn_unref(default_conn);   /* release the create() ref, don't leak it */
		default_conn = NULL;
		start_scan();
		return;
	}
	printk("Connected\n");
	/* default_conn already holds the reference from bt_conn_le_create() — do NOT
	 * re-ref here (that leaks it and blocks all future reconnects). */
	client_chan.rx.mtu = 247;   /* let the acceptor send us up to 247-byte SDUs */
	client_chan.rx.mps = 247;   /* seg_recv needs an MPS */
#ifdef CREDITS_IN_REQUEST
	/* control build: the initial window rides in the connection request, so the peer never starts at 0 credits */
	atomic_set(&client_chan.rx.credits, 0);
	bt_l2cap_chan_give_credits(&client_chan.chan, RX_CREDITS);
#endif
	int rc = bt_l2cap_chan_connect(default_conn, &client_chan.chan, PSM);
	printk("l2cap connect rc=%d\n", rc);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	printk("Disconnected (reason %u)\n", reason);
	if (default_conn) { bt_conn_unref(default_conn); default_conn = NULL; }
	start_scan();
}
static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
};

#define TARGET_NAME "z44-repro"
static bool name_match(struct bt_data *data, void *user_data)
{
	bool *m = user_data;
	if ((data->type == BT_DATA_NAME_COMPLETE || data->type == BT_DATA_NAME_SHORTENED) &&
	    data->data_len == strlen(TARGET_NAME) &&
	    memcmp(data->data, TARGET_NAME, data->data_len) == 0) {
		*m = true;
		return false;
	}
	return true;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	ARG_UNUSED(rssi); ARG_UNUSED(type);
	if (default_conn) { return; }
	bool match = false;
	bt_data_parse(ad, name_match, &match);
	if (!match) { return; }               /* only connect to the repro acceptor */
	if (bt_le_scan_stop()) { return; }
	struct bt_le_conn_param *param = BT_LE_CONN_PARAM_DEFAULT;
	if (bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, param, &default_conn)) {
		start_scan();
	}
}

static void start_scan(void)
{
	struct bt_le_scan_param scan_param = {
		.type = BT_HCI_LE_SCAN_ACTIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};
	int err = bt_le_scan_start(&scan_param, device_found);
	printk(err ? "scan start failed %d\n" : "scanning\n", err);
}

int main(void)
{
	int err = bt_enable(NULL);
	if (err) { printk("bt_enable %d\n", err); return err; }
	printk("\n=== MINIMAL REPRO initiator+receiver (self-reboots), PSM 0x%x ===\n", PSM);
	k_work_init_delayable(&reboot_work, reboot_work_fn);
	bt_conn_cb_register(&conn_callbacks);
	start_scan();
	return 0;
}