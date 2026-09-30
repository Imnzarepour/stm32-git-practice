#include "infrastructure/storage.h"
#include "hardware_proxy/hardware_proxy_storage.h"
#include <stddef.h>
#include <stdbool.h>

/* STORAGE_CONFIG is backed by Hardware Proxy's Storage domain (ST EEPROM
 * Emulation over internal Flash - explicit user decision, shared design
 * across MCU_F4 and MCU_F1: see hardware_proxy/hardware_proxy_storage.h).
 *
 * STORAGE_RECOVERY and STORAGE_FIRMWARE_UPDATE route to that same Hardware
 * Proxy domain's HWPROXY_STORAGE_REGION_RECOVERY /
 * HWPROXY_STORAGE_REGION_FIRMWARE_UPDATE - but today that is ONLY
 * implemented by the Host-Stub (hardware_proxy_storage.c, file-backed, for
 * the two-process Download->Install->Reset->Program->CRC->Bank-Switch
 * simulation - see that file's own comment). hardware_proxy_storage_stm32f4.c
 * deliberately does not implement either Region yet, so on a real target
 * build these two objects still behave exactly like before this file
 * changed: every call returns STORAGE_ERROR (now via the Region's own
 * "unsupported" rejection, rather than this file's own blanket check) -
 * neither object's real physical backend is decided yet (OPEN-ITEMS.md).
 *
 * STORAGE_EVENT/STORAGE_TELEMETRY/STORAGE_LOG remain untouched STUBs
 * (still return STORAGE_ERROR) - no backend decision at all yet for
 * those, not in scope here. */

static StorageResult ToStorageResult(HWProxy_StorageResult result) {
    switch (result) {
        case HWPROXY_STORAGE_OK:      return STORAGE_OK;
        case HWPROXY_STORAGE_INVALID: return STORAGE_INVALID;
        default:                      return STORAGE_ERROR;
    }
}

static bool ConfigRangeValid(uint32_t offset, uint32_t length) {
    return offset <= STORAGE_CONFIG_CAPACITY_BYTES && length <= STORAGE_CONFIG_CAPACITY_BYTES - offset;
}

/* STORAGE_RECOVERY/STORAGE_FIRMWARE_UPDATE's own capacity is enforced by
 * the Hardware Proxy Region itself (hardware_proxy_storage.c) - this layer
 * does not duplicate those numbers, just maps the object onto its Region
 * and lets HWProxy_Storage_* reject an out-of-range access as
 * HWPROXY_STORAGE_INVALID, same outcome ConfigRangeValid() produces for
 * STORAGE_CONFIG, one layer down instead of here. */
static bool RegionFor(StorageObject object, HWProxy_StorageRegion* out_region) {
    switch (object) {
        case STORAGE_CONFIG:           *out_region = HWPROXY_STORAGE_REGION_CONFIG;          return true;
        case STORAGE_RECOVERY:         *out_region = HWPROXY_STORAGE_REGION_RECOVERY;         return true;
        case STORAGE_FIRMWARE_UPDATE:  *out_region = HWPROXY_STORAGE_REGION_FIRMWARE_UPDATE;  return true;
        default:                       return false; /* STORAGE_EVENT/STORAGE_TELEMETRY/STORAGE_LOG - still an unimplemented placeholder */
    }
}

StorageResult Storage_Read(StorageObject object, uint32_t offset, void* buffer, uint32_t length) {
    HWProxy_StorageRegion region;
    if (!RegionFor(object, &region)) {
        return STORAGE_ERROR;
    }
    if (buffer == NULL || (object == STORAGE_CONFIG && !ConfigRangeValid(offset, length))) {
        return STORAGE_INVALID;
    }
    return ToStorageResult(HWProxy_Storage_Read(region, offset, buffer, length));
}

StorageResult Storage_Write(StorageObject object, uint32_t offset, const void* data, uint32_t length) {
    HWProxy_StorageRegion region;
    if (!RegionFor(object, &region)) {
        return STORAGE_ERROR;
    }
    if (data == NULL || (object == STORAGE_CONFIG && !ConfigRangeValid(offset, length))) {
        return STORAGE_INVALID;
    }
    return ToStorageResult(HWProxy_Storage_Write(region, offset, data, length));
}

StorageResult Storage_Erase(StorageObject object) {
    HWProxy_StorageRegion region;
    if (!RegionFor(object, &region)) {
        return STORAGE_ERROR;
    }
    return ToStorageResult(HWProxy_Storage_Erase(region));
}

StorageResult Storage_GetStatus(StorageObject object) {
    HWProxy_StorageRegion region;
    if (!RegionFor(object, &region)) {
        return STORAGE_ERROR;
    }
    /* EEPROM Emulation reads/writes a handful of bytes at a time and are
     * treated as synchronous (memory-storage-infrastructure.md section 12:
     * "small, fast reads/writes can be Synchronous") - there is no
     * pending/async state to report here. STORAGE_RECOVERY/
     * STORAGE_FIRMWARE_UPDATE's Host-Simulation file-backed Regions are
     * synchronous too (plain fopen/fread/fwrite). */
    (void)region;
    return ToStorageResult(HWProxy_Storage_Init());
}

void Storage_Process(void) {
    /* Nothing asynchronous to advance for any Region backed today (Config/
     * Recovery/Firmware-Update-Simulation - see Storage_GetStatus() above);
     * STORAGE_EVENT/STORAGE_TELEMETRY/STORAGE_LOG are still unimplemented
     * placeholders. */
}
