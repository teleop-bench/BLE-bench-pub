/*
 * Q2 minimal peripheral: advertise connectable as "q2periph", accept the
 * connection, hold it. No GATT needed -- the LL exchanges empty data PDUs each
 * connection event, which is exactly the C->P pair the observer measures.
 * Build with the OPEN controller (CONFIG_BT_LL_SW_SPLIT=y), 1M only.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/device.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

/* §9.1 saturation: a bulk-sink characteristic so the central can blast the link to
 * near-full events (the SAME-SESSION saturation the goodput run has). Counted, never
 * echoed. tIFS the observer measures is per-frame -> unaffected, but the event is now
 * full, so the observer sees the goodput regime's on-air timing. */
static const struct bt_uuid_128 svc_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12340010, 0x1234, 0x1000, 0x8000, 0x00805f9b34fb));
static const struct bt_uuid_128 blk_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12340013, 0x1234, 0x1000, 0x8000, 0x00805f9b34fb));
static volatile uint32_t blk_bytes;
static ssize_t blk_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn); ARG_UNUSED(attr); ARG_UNUSED(buf); ARG_UNUSED(offset); ARG_UNUSED(flags);
	blk_bytes += len;
	return len;
}
BT_GATT_SERVICE_DEFINE(sink_svc,
	BT_GATT_PRIMARY_SERVICE(&svc_uuid),
	BT_GATT_CHARACTERISTIC(&blk_uuid.uuid, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE, NULL, blk_write, NULL),
);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, "q2periph", sizeof("q2periph") - 1),
};

#define Q2_OBS_CHAN 10   /* observer parks here (must match the central) */
/* controller ground-truth per-channel counters: q2_evt = SCHEDULED, q2_tx (this
 * role) = CRC-good RESPONSE-OPPORTUNITY (conservative denom, >= actual TX):
 * ACTUAL TX-completed (the peripheral only TXes if it received the central
 * packet). FREE-RUNNING (no app reset). session & aa (connection Access
 * Address) are controller-set at connection setup (race-free). */
extern volatile uint32_t lll_conn_q2_evt[40], lll_conn_q2_tx[40];
extern volatile uint32_t lll_conn_q2_session, lll_conn_q2_aa;

/* Q3a on-chip cross-val: the controller's per-(tifs,phy) RX-PHYEND->TX-READY
 * histogram, measured HERE on the RESPONDER -- the same turnaround the observer
 * sees on air (central-END -> periph-ADDRESS). The runner clears it at
 * CAPTURE-START ('K') and freezes+drains it after CAPTURE-END ('D'); a mid-step run
 * yields SEPARATE tifs=150 and tifs=100 bins in one drain. */
#if defined(CONFIG_BT_CTLR_TIFS_CAPTURE_BENCH)
extern void bt_ctlr_tifs_clear(void);
extern void bt_ctlr_tifs_freeze(void);
extern uint32_t bt_ctlr_tifs_drain_fmt(char *buf, uint32_t buflen);
static uint32_t tifs_life;   /* increments per clear -> exactly one lifecycle/run */
#endif

static void adv_start(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
	printk("PERIPH adv_start err=%d\n", err);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	struct bt_conn_info info;
	if (err) { printk("PERIPH connect fail 0x%02x\n", err); adv_start(); return; }
	bt_conn_get_info(conn, &info);
	printk("PERIPH connected interval=%uus ; sess=%u aa=0x%08x\n",
	       info.le.interval * 1250, lll_conn_q2_session, lll_conn_q2_aa);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	/* command-gated: do NOT auto-re-advertise (that raced + returned -12);
	 * the runner re-drives startup deterministically. */
	printk("PERIPH disconnected reason=0x%02x -> idle\n", reason);
}

/* rev-6: this peripheral's own auto connection-parameter update (~5 s post-connect) is
 * the startup transient that resets the negotiated frame space (ull_conn_update_parameters).
 * Emit ONE AA/session-bound record so the runner EVENT-GATES the steady ABBA sequence on
 * it. printk runs BEFORE the settle + measurement window (no radio-path perturbation). */
static uint32_t q3_paramupd_seq;    /* # updates seen: the runner requires EXACTLY one */
static void le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
			     uint16_t timeout)
{
	ARG_UNUSED(conn);
	unsigned int k = irq_lock();
	uint32_t aa = lll_conn_q2_aa, ses = lll_conn_q2_session;
	irq_unlock(k);
	printk("Q3PARAMUPD role=P aa=0x%08x sess=%u interval=%u latency=%u timeout=%u seq=%u\n",
	       aa, ses, (unsigned)interval, (unsigned)latency, (unsigned)timeout,
	       (unsigned)(++q3_paramupd_seq));
}

/* Q3a: the RESPONDER-side FSU completion, bound to the connection AA/session, is the
 * independent PEER-PARTICIPATION proof (the central's HCI initiator field alone does
 * not show the peer engaged). Present whenever FSU is enabled (the fsu-* builds). */
#if defined(CONFIG_BT_FRAME_SPACE_UPDATE)
static void fsu_updated(struct bt_conn *conn, const struct bt_conn_le_frame_space_updated *p)
{
	ARG_UNUSED(conn);
	unsigned int k = irq_lock();
	uint32_t aa = lll_conn_q2_aa, ses = lll_conn_q2_session;
	irq_unlock(k);
	printk("Q3FSU-DONE role=P aa=0x%08x sess=%u status=0x%02x spacing=%u types=0x%x phys=0x%x initiator=%d\n",
	       aa, ses, p->status, p->frame_space, p->spacing_types, p->phys, (int)p->initiator);
}
#endif

BT_CONN_CB_DEFINE(cbs) = {
	.connected = connected, .disconnected = disconnected,
	.le_param_updated = le_param_updated,
#if defined(CONFIG_BT_FRAME_SPACE_UPDATE)
	.frame_space_updated = fsu_updated,
#endif
};

int main(void)
{
	const struct device *ucon = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	int err = bt_enable(NULL);
	uint32_t boottag = k_cycle_get_32();  /* boot TAG (not a guaranteed-unique nonce) */
	printk("\n== Q2 peripheral (q2periph) == bt_enable=%d\n", err);
	printk("Q2READY role=P dev=%08x%08x boottag=0x%08x\n",
	       (unsigned)NRF_FICR->INFO.DEVICEID[1], (unsigned)NRF_FICR->INFO.DEVICEID[0],
	       (unsigned)boottag);
	/* wait for the runner's ADVERTISE command 'A' before advertising */
	{ unsigned char c = 0; while (c != 'A') { if (uart_poll_in(ucon, &c) != 0) { c = 0; k_msleep(2); } } }
	if (!err) adv_start();
	printk("ADV-READY role=P\n");
	/* command channel: 'S' -> atomic Q2SNAP (denominator boundary). */
	uint32_t snap_seq = 0; int64_t last_evt = 0;
	while (1) {
		unsigned char c;
		if (uart_poll_in(ucon, &c) == 0) {
			if (c == 'S') {
				unsigned int k = irq_lock();
				uint32_t aa = lll_conn_q2_aa, ses = lll_conn_q2_session;
				uint32_t sc = lll_conn_q2_evt[Q2_OBS_CHAN], tx = lll_conn_q2_tx[Q2_OBS_CHAN];
				irq_unlock(k);
				printk("Q2SNAP role=P seq=%u boottag=0x%08x aa=0x%08x sess=%u ch=%u sched=%u tx=%u\n",
				       snap_seq++, (unsigned)boottag, aa, ses, Q2_OBS_CHAN, sc, tx);
			}
			else if (c == 'M') {
				/* Q3 F-time PHASE snapshot (responder side): same counters as 'S',
				 * printed AT the transition so the analyzer can split the ch10
				 * response-opportunity denominator into pre/post for phase-specific
				 * retention. Exactly one per run. */
				unsigned int k = irq_lock();
				uint32_t aa = lll_conn_q2_aa, ses = lll_conn_q2_session;
				uint32_t sc = lll_conn_q2_evt[Q2_OBS_CHAN], tx = lll_conn_q2_tx[Q2_OBS_CHAN];
				irq_unlock(k);
				printk("Q3PHASESNAP role=P boottag=0x%08x aa=0x%08x sess=%u ch=%u sched=%u tx=%u\n",
				       (unsigned)boottag, aa, ses, Q2_OBS_CHAN, sc, tx);
			}
#if defined(CONFIG_BT_CTLR_TIFS_CAPTURE_BENCH)
			/* on-chip lifecycle, bound to role/AA/session/life so the runner can
			 * prove exactly ONE clear->freeze->drain belongs to THIS connection.
			 * 'K' clear (at CAPTURE-START), 'D' FREEZE ONLY (at CAPTURE-END, so the
			 * on-chip window == the observer window), 'R' drain the frozen bins
			 * (after the END snapshots). */
			else if (c == 'K') {
				unsigned int k = irq_lock();
				uint32_t aa = lll_conn_q2_aa, ses = lll_conn_q2_session; irq_unlock(k);
				bt_ctlr_tifs_clear();
				printk("TIFS-CLEARED role=P aa=0x%08x sess=%u life=%u\n", aa, ses, (unsigned)(++tifs_life));
			}
			else if (c == 'D') {
				unsigned int k = irq_lock();
				uint32_t aa = lll_conn_q2_aa, ses = lll_conn_q2_session; irq_unlock(k);
				bt_ctlr_tifs_freeze();
				printk("TIFS-FROZEN role=P aa=0x%08x sess=%u life=%u\n", aa, ses, (unsigned)tifs_life);
			}
			else if (c == 'R') {
				static char tb[600];
				uint32_t n = bt_ctlr_tifs_drain_fmt(tb, sizeof(tb));
				if (n) { printk("%s", tb); }
				unsigned int k = irq_lock();
				uint32_t aa = lll_conn_q2_aa, ses = lll_conn_q2_session; irq_unlock(k);
				printk("TIFS-DRAIN-DONE role=P aa=0x%08x sess=%u life=%u\n", aa, ses, (unsigned)tifs_life);
			}
#endif
		}
		int64_t now = k_uptime_get();
		if (now - last_evt >= 100) {
			last_evt = now;
			unsigned int k = irq_lock();
			uint32_t aa = lll_conn_q2_aa, ses = lll_conn_q2_session;
			uint32_t sc = lll_conn_q2_evt[Q2_OBS_CHAN], tx = lll_conn_q2_tx[Q2_OBS_CHAN];
			irq_unlock(k);
			printk("Q2EVT role=P aa=0x%08x sess=%u t=%lldms ch=%u sched=%u tx=%u\n",
			       aa, ses, now, Q2_OBS_CHAN, sc, tx);
		}
		k_msleep(2);
	}
	return 0;
}
