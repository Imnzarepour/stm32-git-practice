#include "hardware_proxy/hardware_proxy_watchdog.h"
#include "infrastructure/drivers/watchdog.h"
#include <stdio.h>
#include <stdlib.h>

/* This is one of the only files allowed to include infrastructure/drivers
 * headers - see the header for the rule. Pure pass-through, no policy. */

void HWProxy_ResetMcu(void) {
    Watchdog_Reset();

    /* system_management.c's own comment on HWProxy_ResetMcu() already
     * documents "روی سخت‌افزار واقعی برنمی‌گردد" (never returns on real
     * hardware) - exit() is the Host-appropriate way to actually honor
     * that on a PC: the "firmware" process really does stop here, the
     * same way an MCU reset really does restart execution from Bootloader.
     * This is what makes the two-process Host simulation of a real Reset
     * boundary possible (this process exits; a separately-invoked
     * "bootloader" process - bootloader/src/host_main.c - starts fresh
     * afterward and reads whatever this process persisted to the shared
     * sim_*.bin files via hardware_proxy_storage.c). Never compiled into a
     * real target build (this file has no "_stm32f4.c" suffix, but is the
     * same eternal-Host-Stub-only file every other Hardware Proxy domain
     * already has one of - see hardware-proxy.md section 7). */
    fflush(NULL);
    exit(0);
}

void HWProxy_InitWatchdog(void) {
    Watchdog_Init();
}

void HWProxy_RefreshWatchdog(void) {
    Watchdog_Refresh();
}
