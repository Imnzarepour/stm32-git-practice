#ifndef DRIVER_WATCHDOG_H
#define DRIVER_WATCHDOG_H
/* Contract source: docs/03-infrastructure-contracts/driver-layer.md §12
 * تصمیم "کِی Refresh شود" مسئولیت System Management است، نه این Driver. */
void Watchdog_Init(void);
void Watchdog_Refresh(void);

/* Forces an immediate hardware Watchdog-triggered MCU reset (does not
 * return on real hardware). The only Driver-level primitive allowed to
 * cause an MCU reset (driver-layer.md §21) - deciding *when* to call it
 * belongs to System Management (system-management.md §3-1), never to the
 * Driver itself. */
void Watchdog_Reset(void);
#endif
