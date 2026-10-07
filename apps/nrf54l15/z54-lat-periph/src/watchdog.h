/* Actuator-side safety watchdog (safety/STOP-MEASUREMENT.md §1).
 * DRAFT: hardware-abstracted bench implementation — see watchdog.c header comment
 * for what is and is NOT final-safety-path here. */
#ifndef APP_WATCHDOG_H_
#define APP_WATCHDOG_H_

#include <zephyr/kernel.h>

/* Assert STOP and configure all safety GPIOs. MUST be called before bt_enable()
 * so the system boots into the safe (STOP-asserted, latched) state.
 * Returns 0 on success; nonzero = GPIO bring-up failed -> the caller MUST NOT
 * start Bluetooth, and RUNNING is made unreachable (hw_fault latch). */
int safety_wdt_early_init(void);

/* Feed the watchdog from the dedicated heartbeat GATT write callback.
 * Validates length + epoch + monotonic sequence (serial-number arithmetic);
 * invalid frames are counted and ignored — they never feed the timer. */
void safety_wdt_heartbeat(const void *buf, uint16_t len);

/* One-line state/counter summary for the 1 Hz telemetry report. */
void safety_wdt_status(char *out, size_t n);

#endif
