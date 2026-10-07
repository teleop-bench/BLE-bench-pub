/*
 * Event-fill reporter (bench-only): prints the patched open controller's per-connection-event
 * transaction histogram every 2 s, so packets per event can be read alongside throughput.
 * Compiled only when CONFIG_BT_CTLR_FSU_EVENTFILL_DIAG=y (see CMakeLists.txt); normal builds
 * of this app are unaffected. The counters are defined by the fsu-m0 controller patch 0015.
 *
 * Output, per 2 s window:  RPT pe: events=<n> mean=<m.mm> mode=<k> max=<k> | <k>:<count> ...
 * where <k> is transactions in a closed connection event (in GATT echo duplex, one transaction
 * = one central packet + the peripheral's reply).
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

extern volatile uint16_t lll_conn_q2_pe[64];
extern volatile uint16_t lll_conn_q2_pe_max;
extern volatile uint32_t lll_conn_q2_events;

#define EVENTFILL_REPORT_MS 2000

static void eventfill_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	static uint16_t prev[64];
	uint32_t prev_ev = lll_conn_q2_events;

	for (int i = 0; i < 64; i++) {
		prev[i] = lll_conn_q2_pe[i];
	}
	while (1) {
		k_msleep(EVENTFILL_REPORT_MS);
		uint16_t now[64];
		uint32_t ev = lll_conn_q2_events, d_ev = ev - prev_ev;
		uint64_t pkts = 0;
		uint16_t best = 0;
		int mode = 0;

		for (int i = 0; i < 64; i++) {
			now[i] = lll_conn_q2_pe[i];
			uint16_t d = now[i] - prev[i];

			pkts += (uint64_t)i * d;
			if (d > best) {
				best = d;
				mode = i;
			}
		}
		uint32_t mean_x100 = d_ev ? (uint32_t)(pkts * 100 / d_ev) : 0;

		printk("RPT pe: events=%u mean=%u.%02u mode=%d max=%u |", d_ev, mean_x100 / 100,
		       mean_x100 % 100, mode, lll_conn_q2_pe_max);
		for (int i = 0; i < 64; i++) {
			uint16_t d = now[i] - prev[i];

			if (d) {
				printk(" %d:%u", i, d);
			}
			prev[i] = now[i];
		}
		printk("\n");
		prev_ev = ev;
	}
}

K_THREAD_DEFINE(eventfill_tid, 1024, eventfill_fn, NULL, NULL, NULL, 12, 0, 1000);
