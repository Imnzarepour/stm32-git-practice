#ifndef INFRA_STORAGE_H
#define INFRA_STORAGE_H
#include <stdint.h>

/* Contract source: docs/03-infrastructure-contracts/memory-storage-infrastructure.md
 *
 * STORAGE_CONFIG backend: CLOSED (explicit user decision) - ST's EEPROM
 * Emulation over internal Flash, shared design across MCU_F4 and MCU_F1,
 * wired via a dedicated Hardware Proxy domain
 * (hardware_proxy/hardware_proxy_storage.h) - see storage.c and
 * docs/03-infrastructure-contracts/hardware-proxy.md.
 *
 * TODO(open-item): every OTHER StorageObject below is still an
 * unimplemented placeholder (Storage_Read/Write/Erase/GetStatus return
 * STORAGE_ERROR for them, unchanged). STORAGE_FIRMWARE_UPDATE in particular is NOT
 * meant to reuse the EEPROM Emulation backend - it belongs on External
 * NOR per memory-storage-infrastructure.md section 9 (see
 * docs/05-outsourcing/nor-storage-outsourcing-spec.md), which is its own,
 * still-open decision (OPEN-ITEMS.md). */

typedef enum {
    STORAGE_CONFIG,
    STORAGE_EVENT,
    STORAGE_TELEMETRY,
    STORAGE_FIRMWARE_UPDATE,
    STORAGE_RECOVERY,
    STORAGE_LOG /* LOG Partition - see docs/03-infrastructure-contracts/log-infrastructure.md and infrastructure/log.c. Still Stub like every StorageObject below except STORAGE_CONFIG. */
} StorageObject;

/* Logical capacity Storage Infrastructure guarantees for STORAGE_CONFIG,
 * regardless of physical backend (Contract section 22: "these names are a
 * logical Contract, not tied to a physical address"). Configuration
 * Infrastructure (infrastructure/configuration.c) statically asserts its
 * DeviceConfig fits within this - bump it there if DeviceConfig grows. */
#define STORAGE_CONFIG_CAPACITY_BYTES 512u

typedef enum {
    STORAGE_OK,
    STORAGE_BUSY,
    STORAGE_INVALID,
    STORAGE_ERROR
} StorageResult;

StorageResult Storage_Read(StorageObject object, uint32_t offset, void* buffer, uint32_t length);
StorageResult Storage_Write(StorageObject object, uint32_t offset, const void* data, uint32_t length);
StorageResult Storage_Erase(StorageObject object);
StorageResult Storage_GetStatus(StorageObject object);
void          Storage_Process(void); /* برای عملیات Async زمان‌بر (Erase/Write بزرگ) */

#endif /* INFRA_STORAGE_H */
