#ifndef HARDWARE_PROXY_STORAGE_H
#define HARDWARE_PROXY_STORAGE_H
#include <stdint.h>

/* Contract source: docs/03-infrastructure-contracts/hardware-proxy.md
 * (Storage domain, added alongside _digital_io/_analog_io - same rule as
 * both: only firmware/src/hardware_proxy/hardware_proxy_storage*.c may
 * mention Flash/EEPROM registers or include an EEPROM-emulation
 * middleware header. Storage Infrastructure (infrastructure/storage.c)
 * is the only consumer of this header.)
 *
 * Backs STORAGE_CONFIG only (infrastructure/storage.c) - explicit user
 * decision: Device Configuration is persisted with ST's EEPROM Emulation,
 * the standard "emulate EEPROM in internal Flash" middleware ST ships for
 * both the F1 and F4 families (same EE_Init/EE_ReadVariable/
 * EE_WriteVariable entry points on both), not external NOR. Every other
 * StorageObject (STORAGE_FIRMWARE_UPDATE in particular - External NOR, per
 * memory-storage-infrastructure.md section 9) is untouched by this
 * domain.
 *
 * HWProxy_StorageRegion has exactly one member today because Config is
 * the only real need right now (CLAUDE.md: no hypothetical features).
 * Adding a future Region is one enum entry here plus one address-range
 * row in the target-specific .c file - nothing above this header changes
 * shape. */

/* HWPROXY_STORAGE_REGION_RECOVERY / HWPROXY_STORAGE_REGION_FIRMWARE_UPDATE
 * were added later, purely for the Host (PC) two-process simulation of
 * the full Download -> Install -> Reset -> Program -> CRC -> Bank Switch
 * pipeline (see hardware_proxy_storage.c's own comment) - NOT a claim
 * about the real physical backend for STORAGE_RECOVERY (still open -
 * shares the same EEPROM Emulation as Config? a separate area? -
 * OPEN-ITEMS.md) or STORAGE_FIRMWARE_UPDATE (External NOR, per
 * memory-storage-infrastructure.md section 9 - still with an outside
 * contractor). hardware_proxy_storage_stm32f4.c deliberately does NOT
 * implement either yet - both already fall through its existing
 * "unsupported Region" rejection unchanged, so target builds are
 * unaffected until the real backend for each is actually decided/wired. */
typedef enum {
    HWPROXY_STORAGE_REGION_CONFIG,
    HWPROXY_STORAGE_REGION_RECOVERY,        /* Host-Simulation only today - see comment above */
    HWPROXY_STORAGE_REGION_FIRMWARE_UPDATE  /* Host-Simulation only today - see comment above */
} HWProxy_StorageRegion;

typedef enum {
    HWPROXY_STORAGE_OK,
    HWPROXY_STORAGE_INVALID,
    HWPROXY_STORAGE_ERROR
} HWProxy_StorageResult;

HWProxy_StorageResult HWProxy_Storage_Init(void);
HWProxy_StorageResult HWProxy_Storage_Read(HWProxy_StorageRegion region, uint32_t offset, void* buffer, uint32_t length);
HWProxy_StorageResult HWProxy_Storage_Write(HWProxy_StorageRegion region, uint32_t offset, const void* data, uint32_t length);
HWProxy_StorageResult HWProxy_Storage_Erase(HWProxy_StorageRegion region);

#endif /* HARDWARE_PROXY_STORAGE_H */
