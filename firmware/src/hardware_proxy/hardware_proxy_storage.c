#include "hardware_proxy/hardware_proxy_storage.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* Host-Stub - used by the plain host (PC) "firmware" executable
 * (host_main.c) and by Ceedling's own host build. NEVER linked into a
 * Management/Infrastructure unit test, which mocks this header instead
 * (docs/03-infrastructure-contracts/hardware-proxy.md section 6);
 * test_storage.c is what actually exercises infrastructure/storage.c's
 * logic.
 *
 * HWPROXY_STORAGE_REGION_CONFIG simulates with a static RAM buffer - every
 * host run starts empty, same as before this file grew the two Regions
 * below. This has nothing to do with the Flash-backed behavior in
 * hardware_proxy_storage_stm32f4.c.
 *
 * HWPROXY_STORAGE_REGION_RECOVERY / HWPROXY_STORAGE_REGION_FIRMWARE_UPDATE
 * are file-backed instead (plain fopen/fread/fwrite) - unlike Config,
 * these two specifically need to survive process exit, so the Host
 * two-process simulation of a real Reset (this "firmware" process writes
 * and exits; a separate "bootloader" process starts fresh afterward and
 * must see what was written - bootloader/src/boot_hal_host_stub.c) has
 * something real to observe. The file names/layout here are hand-
 * coordinated with that file (same discipline already used for
 * STORAGE_RECOVERY's BootControl/BootErrorRecord byte offsets between the
 * two independent projects) - run both executables from the same working
 * directory. This is purely a Host development/simulation convenience,
 * not a claim about either object's real physical backend (both still
 * open - see hardware_proxy_storage.h's own comment). */

#define CONFIG_REGION_CAPACITY_BYTES 512u /* must stay >= STORAGE_CONFIG_CAPACITY_BYTES, infrastructure/storage.h */

static uint8_t s_config_region[CONFIG_REGION_CAPACITY_BYTES];

/* Must match bootloader/src/boot_hal_host_stub.c's SIM_RECOVERY_FILE
 * exactly - that file's EEPROM_Read/Write() read/write the same file. */
#define RECOVERY_SIM_FILE             "sim_storage_recovery.bin"
#define RECOVERY_SIM_CAPACITY_BYTES   64u /* must match boot_hal_host_stub.c's SIM_RECOVERY_CAPACITY_BYTES */

/* Must match bootloader/src/boot_hal_host_stub.c's SIM_FIRMWARE_UPDATE_FILE
 * exactly - that file's NewImage_Read/GetSize() read the same file as the
 * raw payload (after synthesizing a BootImageHeader in front of it). */
#define FIRMWARE_UPDATE_SIM_FILE           "sim_nor_firmware_update.bin"
#define FIRMWARE_UPDATE_SIM_CAPACITY_BYTES (2u * 1024u * 1024u) /* generous placeholder cap for Host simulation only - real External NOR capacity is a separate, still-open question (nor-flash-infrastructure.md) */

static HWProxy_StorageResult file_region_read(const char* path, uint32_t capacity, uint32_t offset, void* buffer, uint32_t length) {
    if (buffer == NULL || offset > capacity || length > capacity - offset) {
        return HWPROXY_STORAGE_INVALID;
    }

    FILE* f = fopen(path, "rb");
    if (!f) {
        return HWPROXY_STORAGE_ERROR; /* never written yet - same "blank device" meaning every other Storage backend uses */
    }
    if (fseek(f, (long)offset, SEEK_SET) != 0) {
        fclose(f);
        return HWPROXY_STORAGE_ERROR;
    }
    size_t read = fread(buffer, 1, length, f);
    fclose(f);

    if (read != length) {
        /* Short read (offset+length past what was ever written) - zero-
         * fill the remainder rather than handing back uninitialized
         * memory or failing outright, same "blank region" convention
         * boot_hal_host_stub.c's file_read() uses on the other side. */
        memset((uint8_t*)buffer + read, 0xFF, length - read);
    }
    return HWPROXY_STORAGE_OK;
}

/* Read-modify-write against the whole file, same shape
 * hardware_proxy_storage_stm32f4.c's EEPROM Emulation read-modify-write
 * already uses (there, virtual-address-slot-backed; here, file-backed) -
 * a byte-granular write must never truncate/corrupt bytes outside
 * [offset, offset+length) that were already persisted. */
static HWProxy_StorageResult file_region_write(const char* path, uint32_t capacity, uint32_t offset, const void* data, uint32_t length) {
    if (data == NULL || offset > capacity || length > capacity - offset) {
        return HWPROXY_STORAGE_INVALID;
    }

    FILE* f = fopen(path, "r+b");
    if (!f) {
        f = fopen(path, "w+b"); /* first write ever to this simulated Region */
    }
    if (!f) {
        return HWPROXY_STORAGE_ERROR;
    }
    if (fseek(f, (long)offset, SEEK_SET) != 0) {
        fclose(f);
        return HWPROXY_STORAGE_ERROR;
    }
    size_t written = fwrite(data, 1, length, f);
    fclose(f);

    return (written == length) ? HWPROXY_STORAGE_OK : HWPROXY_STORAGE_ERROR;
}

HWProxy_StorageResult HWProxy_Storage_Init(void) {
    return HWPROXY_STORAGE_OK;
}

HWProxy_StorageResult HWProxy_Storage_Read(HWProxy_StorageRegion region, uint32_t offset, void* buffer, uint32_t length) {
    switch (region) {
        case HWPROXY_STORAGE_REGION_CONFIG:
            if (buffer == NULL || offset > CONFIG_REGION_CAPACITY_BYTES || length > CONFIG_REGION_CAPACITY_BYTES - offset) {
                return HWPROXY_STORAGE_INVALID;
            }
            memcpy(buffer, &s_config_region[offset], length);
            return HWPROXY_STORAGE_OK;
        case HWPROXY_STORAGE_REGION_RECOVERY:
            return file_region_read(RECOVERY_SIM_FILE, RECOVERY_SIM_CAPACITY_BYTES, offset, buffer, length);
        case HWPROXY_STORAGE_REGION_FIRMWARE_UPDATE:
            return file_region_read(FIRMWARE_UPDATE_SIM_FILE, FIRMWARE_UPDATE_SIM_CAPACITY_BYTES, offset, buffer, length);
        default:
            return HWPROXY_STORAGE_INVALID;
    }
}

HWProxy_StorageResult HWProxy_Storage_Write(HWProxy_StorageRegion region, uint32_t offset, const void* data, uint32_t length) {
    switch (region) {
        case HWPROXY_STORAGE_REGION_CONFIG:
            if (data == NULL || offset > CONFIG_REGION_CAPACITY_BYTES || length > CONFIG_REGION_CAPACITY_BYTES - offset) {
                return HWPROXY_STORAGE_INVALID;
            }
            memcpy(&s_config_region[offset], data, length);
            return HWPROXY_STORAGE_OK;
        case HWPROXY_STORAGE_REGION_RECOVERY:
            return file_region_write(RECOVERY_SIM_FILE, RECOVERY_SIM_CAPACITY_BYTES, offset, data, length);
        case HWPROXY_STORAGE_REGION_FIRMWARE_UPDATE:
            return file_region_write(FIRMWARE_UPDATE_SIM_FILE, FIRMWARE_UPDATE_SIM_CAPACITY_BYTES, offset, data, length);
        default:
            return HWPROXY_STORAGE_INVALID;
    }
}

/* remove()'s only failure this cares about is "could not delete an
 * existing file" (a real problem); "the file never existed in the first
 * place" is not a failure to erase something already blank. */
static HWProxy_StorageResult remove_sim_file(const char* path) {
    if (remove(path) == 0) {
        return HWPROXY_STORAGE_OK;
    }
    return (errno == ENOENT) ? HWPROXY_STORAGE_OK : HWPROXY_STORAGE_ERROR;
}

HWProxy_StorageResult HWProxy_Storage_Erase(HWProxy_StorageRegion region) {
    switch (region) {
        case HWPROXY_STORAGE_REGION_CONFIG:
            memset(s_config_region, 0, sizeof(s_config_region));
            return HWPROXY_STORAGE_OK;
        case HWPROXY_STORAGE_REGION_RECOVERY:
            return remove_sim_file(RECOVERY_SIM_FILE);
        case HWPROXY_STORAGE_REGION_FIRMWARE_UPDATE:
            return remove_sim_file(FIRMWARE_UPDATE_SIM_FILE);
        default:
            return HWPROXY_STORAGE_INVALID;
    }
}
