/*
 * nRF54L15-DK — BLE L2CAP CoC UPLINK BLASTER (peripheral -> central).
 *
 * A benchmark path MOTIVATED BY (not equivalent to) Zephyr issue #46073: a peripheral
 * bursting SDUs upstream and whether its TX buffers are reclaimed across reconnects.
 * This firmware:
 *   1) advertises + accepts the central's CoC channel,
 *   2) blasts fixed-size SDUs upstream continuously while the channel is up,
 *   3) reports a MONOTONIC, correlatable set of counters (submitted/completed/failed/
 *      pool-wait/released + cumulative bytes), tagged with a per-boot run id and the
 *      controller's shared connection AA so sink and source logs can be paired,
 *   4) on disconnect, frees + re-advertises.
 *
 * STAGE 1 = INSTRUMENTATION ONLY. Startup/connection sequencing is UNCHANGED (PHY at
 * connect, DLE-at-connect, the l2cap_accept memset, unpaced blast). The only behavioral
 * change is a TIMED buffer allocation (was K_FOREVER) so pool exhaustion shows up as a
 * climbing pool_wait counter instead of a silent thread wedge. .released is OBSERVED, not
 * yet used to gate channel reuse (that + serialized PHY->DLE->CoC are Stage 3).
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/drivers/uart.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/l2cap.h>

#define L2CAP_PSM   0x0080
#define L2CAP_MTU   512
#define SDU_SIZE    244          /* single-packet; robot-uplink telemetry size */
#define SUMMARY_MS  1000
#define TX_WINDOW   4            /* completion window (NOTE: NOT consumed by the unpaced blast) */
#define POOL_BUFS   8
#define ALLOC_TIMEOUT_MS 50      /* Stage-1: timed alloc -> pool exhaustion is OBSERVABLE */

NET_BUF_POOL_FIXED_DEFINE(tx_pool, POOL_BUFS, BT_L2CAP_SDU_BUF_SIZE(L2CAP_MTU), 8, NULL);

/* controller-owned, set on EVERY connection setup, SAME value on both roles -> the id
 * to correlate this blaster's log with the sink's log for the same connection. */
extern volatile uint32_t lll_conn_q2_aa, lll_conn_q2_session;

static struct bt_l2cap_le_chan le_chan;
static struct bt_conn *default_conn;
static volatile bool adv_active;   /* true while advertising; gates 'V' re-triggers */
static K_SEM_DEFINE(blast_sem, 0, 1);
static K_SEM_DEFINE(tx_win, TX_WINDOW, TX_WINDOW);
static volatile bool chan_up;
/* DECONFOUND TEST: pause production on session 1 after PAUSE_AFTER_S so the sender is
 * DRAINED/HEALTHY (outstanding->0, pool_wait=0) when the teardown is induced -- holding
 * sender condition constant so an abrupt vs graceful teardown differ ONLY in mode. */
#define PAUSE_AFTER_S 8
static volatile bool producing = true;
static bool paused_once;
static struct k_work_delayable pause_work;

/* ---- Stage-1 instrumentation: monotonic per-session counters + cumulative bytes ---- */
static uint32_t run_id, boot_tag;            /* per-boot identity (reboot detection + pairing) */
static uint32_t sess_aa;                     /* shared connection AA snapshot for this session */
static atomic_t submitted, completed, send_failed, pool_wait, released_cnt;
static atomic_t tx_total;                    /* cumulative accepted-send bytes (wraps 32-bit) */
static uint32_t tx_win_bytes;                /* bytes accepted this summary window */
/* STAGE 3: gate channel-object reuse on .released. 1 = free (released or never used),
 * 0 = handed to the stack for an active/tearing-down session. */
static atomic_t chan_free = ATOMIC_INIT(1);

static void pause_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	producing = false;
	printk("PRODUCER PAUSED (draining): out=%ld pool_wait=%ld run=%u sess=%u aa=0x%08x\n",
	       (long)(atomic_get(&submitted) - atomic_get(&completed)),
	       (long)atomic_get(&pool_wait), run_id, (unsigned)lll_conn_q2_session, sess_aa);
}

static void reset_session_counters(void)
{
	atomic_set(&submitted, 0); atomic_set(&completed, 0);
	atomic_set(&send_failed, 0); atomic_set(&pool_wait, 0);
	atomic_set(&released_cnt, 0);
	tx_win_bytes = 0;
}

/* ---- L2CAP CoC server (we are the sender once the channel is up) ---- */

static int chan_recv(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
	ARG_UNUSED(chan); ARG_UNUSED(buf);
	return 0; /* central doesn't send downstream in this test */
}

/* .sent = controller completion (its exact timing is implementation-dependent). */
static void chan_sent(struct bt_l2cap_chan *chan)
{
	ARG_UNUSED(chan);
	atomic_inc(&completed);
	k_sem_give(&tx_win);
}

/* STAGE 3: the stack is done with the channel object -> safe to reuse. Mark it free so
 * l2cap_accept won't hand out (or clobber) a channel still owned by the prior session. */
static void chan_released(struct bt_l2cap_chan *chan)
{
	ARG_UNUSED(chan);
	atomic_inc(&released_cnt);
	atomic_set(&chan_free, 1);
	printk("CoC released: run=%u sess=%u aa=0x%08x (total released=%ld) -> chan free\n",
	       run_id, (unsigned)lll_conn_q2_session, sess_aa, (long)atomic_get(&released_cnt));
}

static void chan_connected(struct bt_l2cap_chan *chan)
{
	struct bt_l2cap_le_chan *le = CONTAINER_OF(chan, struct bt_l2cap_le_chan, chan);
	unsigned int k = irq_lock();
	sess_aa = lll_conn_q2_aa;
	irq_unlock(k);
	reset_session_counters();
	chan_up = true;
	producing = true;   /* session 2+ produces normally; session 1 pauses via pause_work */
	if (!paused_once) { paused_once = true; k_work_reschedule(&pause_work, K_SECONDS(PAUSE_AFTER_S)); }
	k_sem_reset(&tx_win);
	for (int i = 0; i < TX_WINDOW; i++) { k_sem_give(&tx_win); }
	k_sem_give(&blast_sem);
	printk("CoC up: tx.mtu=%u tx.mps=%u run=%u sess=%u aa=0x%08x (unpaced blaster armed)\n",
	       le->tx.mtu, le->tx.mps, run_id, (unsigned)lll_conn_q2_session, sess_aa);
}

static void chan_disconnected(struct bt_l2cap_chan *chan)
{
	ARG_UNUSED(chan);
	chan_up = false;
	/* PROBE: outstanding = submitted-completed. If it stays >0 (and pool_wait climbs),
	 * TX buffers were not reclaimed; released tells whether the channel object was freed. */
	long sub = atomic_get(&submitted), cmp = atomic_get(&completed);
	printk("CoC down: submitted=%ld completed=%ld outstanding=%ld send_failed=%ld "
	       "pool_wait=%ld released=%ld | run=%u sess=%u aa=0x%08x\n",
	       sub, cmp, sub - cmp, (long)atomic_get(&send_failed), (long)atomic_get(&pool_wait),
	       (long)atomic_get(&released_cnt), run_id, (unsigned)lll_conn_q2_session, sess_aa);
}

static const struct bt_l2cap_chan_ops chan_ops = {
	.connected = chan_connected,
	.disconnected = chan_disconnected,
	.released = chan_released,
	.sent = chan_sent,
	.recv = chan_recv,
};

static int l2cap_accept(struct bt_conn *conn, struct bt_l2cap_server *server,
			struct bt_l2cap_chan **chan)
{
	ARG_UNUSED(conn); ARG_UNUSED(server);
	/* STAGE 3: only reuse the channel object once the previous session RELEASED it.
	 * If it hasn't, reject rather than clobber stack-owned state (the earlier premature
	 * memset-before-release is REMOVED). The stack initialises rx/tx on connect; we set
	 * only ops + the desired rx MTU. */
	if (!atomic_cas(&chan_free, 1, 0)) {
		printk("l2cap_accept: prior channel not yet released -> reject (-EBUSY)\n");
		return -EBUSY;
	}
	le_chan.chan.ops = &chan_ops;
	le_chan.rx.mtu = L2CAP_MTU;
	*chan = &le_chan.chan;
	return 0;
}

static struct bt_l2cap_server server = {
	.psm = L2CAP_PSM,
	.sec_level = BT_SECURITY_L1,
	.accept = l2cap_accept,
};

/* ---- advertising ---- */

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* Restart advertising from a workqueue (the just-closed conn object isn't reaped inside
 * disconnected(); deferring lets the stack free the slot before bt_le_adv_start). */
static struct k_work_delayable adv_work;

static void adv_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err == -ENOMEM || err == -EAGAIN) {
		k_work_reschedule(&adv_work, K_MSEC(100));
		return;
	}
	if (err) { printk("adv start failed (%d)\n", err); }
	else { adv_active = true; printk("Advertising as '%s' run=%u\n", CONFIG_BT_DEVICE_NAME, run_id); }
}

static void start_adv(void) { k_work_reschedule(&adv_work, K_NO_WAIT); }

/* DECONFOUND v3 — COMMAND-GATED advertising. The peripheral never advertises on its own
 * (not at boot, not after a disconnect); it advertises ONLY when the host sends 'V' on the
 * console, AFTER the host has seen the central's machine-readable SCAN-READY. This lets a
 * REBOOTED central reconnect in central-first order (Order B), separating "central reboot"
 * from "Order A". A background thread reads the console (no shell). */
static void adv_reader_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	const struct device *con = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	if (!device_is_ready(con)) { return; }
	while (1) {
		unsigned char ch;
		while (uart_poll_in(con, &ch) == 0) {
			if (ch == 'V') {
				/* Idempotent: only (re)start advertising if idle -- NOT while connected or
				 * already advertising. Prevents a 'V' burst from restarting adv mid-session. */
				if (!default_conn && !adv_active) {
					printk("ADV-CMD: 'V' received -> advertising run=%u\n", run_id);
					start_adv();
				}
			}
		}
		k_msleep(20);
	}
}
K_THREAD_DEFINE(adv_tid, 768, adv_reader_fn, NULL, NULL, NULL, 11, 0, 0);

/* ---- GAP ---- */

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) { printk("GAP connect failed (0x%02x)\n", err); return; }
	adv_active = false;   /* advertising auto-stops on connect */
	default_conn = bt_conn_ref(conn);
	struct bt_conn_info info;
	char addr[BT_ADDR_LE_STR_LEN] = "?";
	unsigned int k = irq_lock();
	sess_aa = lll_conn_q2_aa;
	irq_unlock(k);
	if (bt_conn_get_info(conn, &info) == 0) {
		bt_addr_le_to_str(info.le.dst, addr, sizeof(addr));
		printk("GAP connected: interval=%u peer=%s run=%u sess=%u aa=0x%08x\n",
		       info.le.interval, addr, run_id, (unsigned)lll_conn_q2_session, sess_aa);
	}
	/* THE UPLINK FIX (unchanged in Stage 1): raise the peripheral's OWN TX data length. */
	int e = bt_conn_le_data_len_update(conn, BT_LE_DATA_LEN_PARAM_MAX);
	printk("peripheral DLE req rc=%d\n", e);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	printk("GAP disconnected (0x%02x) run=%u aa=0x%08x\n", reason, run_id, sess_aa);
	chan_up = false;
	adv_active = false;
	if (default_conn) { bt_conn_unref(default_conn); default_conn = NULL; }
	/* GATED: do NOT auto re-advertise -- wait for the host's 'V' (after central SCAN-READY). */
	printk("ADV-GATED: disconnected, awaiting 'V' run=%u\n", run_id);
}

static void le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *p)
{
	ARG_UNUSED(conn);
	printk("PHY updated: tx=%u rx=%u run=%u\n", p->tx_phy, p->rx_phy, run_id);
}

static void le_data_len_updated(struct bt_conn *conn, struct bt_conn_le_data_len_info *i)
{
	ARG_UNUSED(conn);
	printk("DLE updated: tx_max=%u rx_max=%u run=%u\n", i->tx_max_len, i->rx_max_len, run_id);
}

BT_CONN_CB_DEFINE(conn_cbs) = {
	.connected = connected,
	.disconnected = disconnected,
	.le_phy_updated = le_phy_updated,
	.le_data_len_updated = le_data_len_updated,
};

/* ---- uplink blast thread (unpaced; Stage-1 timed alloc so a wedge is observable) ---- */

static void blast_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	static uint8_t payload[SDU_SIZE];
	memset(payload, 'U', sizeof(payload));

	while (1) {
		k_sem_take(&blast_sem, K_FOREVER);
		printk("uplink blasting %d-byte SDUs... run=%u aa=0x%08x\n", SDU_SIZE, run_id, sess_aa);
		while (chan_up) {
			if (!producing) { k_msleep(20); continue; }   /* paused: let the pipeline drain */
			struct net_buf *buf = net_buf_alloc(&tx_pool, K_MSEC(ALLOC_TIMEOUT_MS));
			if (!buf) {           /* pool exhausted within the timeout -> OBSERVE, don't wedge */
				atomic_inc(&pool_wait);
				continue;
			}
			net_buf_reserve(buf, BT_L2CAP_SDU_CHAN_SEND_RESERVE);
			net_buf_add_mem(buf, payload, SDU_SIZE);
			atomic_inc(&submitted);                 /* count BEFORE send */
			int e = bt_l2cap_chan_send(&le_chan.chan, buf);
			if (e < 0) {
				atomic_dec(&submitted);         /* roll back: not accepted */
				net_buf_unref(buf);
				if (e == -EAGAIN) { k_msleep(1); continue; }
				atomic_inc(&send_failed);
				if (e != -ENOTCONN && e != -ESHUTDOWN) { printk("send err %d\n", e); }
				break;
			}
			tx_win_bytes += SDU_SIZE;
			atomic_add(&tx_total, SDU_SIZE);
		}
	}
}

K_THREAD_DEFINE(blast_tid, 2048, blast_fn, NULL, NULL, NULL, 10, 0, 0);

/* ---- summary thread ---- */

static void sum_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	uint32_t idle = 0;
	while (1) {
		k_msleep(SUMMARY_MS);
		if (chan_up) {
			uint32_t b = tx_win_bytes; tx_win_bytes = 0; idle = 0;
			long sub = atomic_get(&submitted), cmp = atomic_get(&completed);
			printk("UPLINK tx: %u KiB/s (win_bytes=%u total=%ld) sub=%ld cmp=%ld out=%ld "
			       "fail=%ld poolwait=%ld rel=%ld | run=%u sess=%u aa=0x%08x\n",
			       b / 1024, b, (long)atomic_get(&tx_total), sub, cmp, sub - cmp,
			       (long)atomic_get(&send_failed), (long)atomic_get(&pool_wait),
			       (long)atomic_get(&released_cnt), run_id,
			       (unsigned)lll_conn_q2_session, sess_aa);
		} else {
			printk("idle %us: conn=%s run=%u (advertising, awaiting central)\n",
			       ++idle, default_conn ? "yes" : "none", run_id);
		}
	}
}

K_THREAD_DEFINE(sum_tid, 1024, sum_fn, NULL, NULL, NULL, 10, 0, 0);

int main(void)
{
	boot_tag = k_cycle_get_32();
	int err = bt_enable(NULL);
	if (err) { printk("bt_enable failed (%d)\n", err); return 0; }
	run_id = k_cycle_get_32();
	printk("\n=== nRF54L15 L2CAP CoC UPLINK BLASTER (Zephyr 4.4.1; #46073-motivated) "
	       "run=%u boot_tag=0x%08x ===\n", run_id, boot_tag);
	k_work_init_delayable(&adv_work, adv_work_fn);
	k_work_init_delayable(&pause_work, pause_work_fn);
	bt_l2cap_server_register(&server);
	printk("L2CAP server on PSM 0x%04x\n", L2CAP_PSM);
	/* Auto-advertise on BOOT (a periph that boots then sits idle before advertising wedges
	 * its first session -- observed). Only the RE-advertisement after a disconnect is gated,
	 * so session 2 (post central-reboot) can be forced central-first via the host's 'V'. */
	start_adv();
	return 0;
}
