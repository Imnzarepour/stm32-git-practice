#ifndef HARDWARE_PROXY_WATCHDOG_H
#define HARDWARE_PROXY_WATCHDOG_H

/* Contract source: docs/03-infrastructure-contracts/hardware-proxy.md
 * See hardware_proxy_digital_io.h for the full rationale of this layer
 * and why it is split into one file per peripheral domain.
 *
 * This domain exists so System Management (a Management) can reach the
 * Watchdog Driver without directly including infrastructure/drivers/
 * watchdog.h, which rule #7 forbids. Init/Refresh used to be called
 * directly from firmware_main.c, bypassing this layer AND System
 * Management itself (management-infrastructure-dependency.md §5 lists
 * Watchdog as System Management's own Driver dependency, not the Super
 * Loop wiring's) - that has been fixed: firmware_main.c no longer touches
 * Watchdog at all, System_Init()/System_Process() do, through this Proxy. */

/* Forces an immediate hardware Watchdog-triggered MCU reset - does not
 * return on real hardware. Deciding when to call this belongs entirely to
 * System Management (system-management.md §3-1); this layer has no policy. */
void HWProxy_ResetMcu(void);

/* One-time Watchdog peripheral setup, called once from System_Init(). */
void HWProxy_InitWatchdog(void);

/* Feeds the Watchdog so it does not fire. Deciding *when* to call this
 * (the "Policy" system-management.md §3-3 asks for) belongs entirely to
 * System Management (see System_Process() in system_management.c); this
 * layer is pure pass-through, same as HWProxy_ResetMcu() above. */
void HWProxy_RefreshWatchdog(void);

#endif /* HARDWARE_PROXY_WATCHDOG_H */
