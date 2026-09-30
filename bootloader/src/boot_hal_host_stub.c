#include "boot_hal.h"
#include "boot_config.h"
#include <stdio.h>
#include <string.h>

/* Host (PC) Simulation of boot_hal.h - the boundary between bootloader.c's
 * decision logic and real hardware. Until now the ONLY implementation of
 * this header anywhere in the project was test_bootloader.c's CMock-
 * generated Mockboot_hal.h (see boot_hal.h's own top comment) - this file
 * is the first real, host-buildable one, added specifically so the full
 * Download -> Install -> Reset -> Program -> CRC -> Bank Switch pipeline
 * can be exercised end-to-end on a PC, across two SEPARATE processes
 * (the "firmware" host executable and this "bootloader" host executable
 * are genuinely different programs with no shared memory - exactly like
 * a real MCU reset boundary), not just inside a single Ceedling test run.
 *
 * Persistence is file-backed (plain fopen/fread/fwrite), one file per
 * physical memory this simulates - each file survives process exit, which
 * is the whole point (the firmware process writes and exits/"resets"; this
 * bootloader process starts fresh afterward and must see what was
 * written):
 *   - SIM_RECOVERY_FILE - STORAGE_RECOVERY (BootControl / BootErrorRecord /
 *     firmware/'s FirmwareUpdatePendingInstallRecord, offsets 0/8/16 - see
 *     boot_control.h / boot_error.h). MUST be the exact same file
 *     firmware/'s hardware_proxy_storage.c (host-stub,
 *     HWPROXY_STORAGE_REGION_RECOVERY) reads/writes - run both executables
 *     from the same working directory.
 *   - SIM_BANK1_FILE / SIM_BANK2_FILE - the two Application Banks
 *     (bootloader.md section 2). Only this project ever touches these -
 *     firmware/ has no notion of its own Bank while running.
 *   - SIM_FIRMWARE_UPDATE_FILE - the external NOR's STORAGE_FIRMWARE_UPDATE
 *     region (memory-storage-infrastructure.md section 9). MUST be the
 *     same file firmware/'s hardware_proxy_storage.c
 *     (HWPROXY_STORAGE_REGION_FIRMWARE_UPDATE) writes a Download into.
 *
 * NewImage_Read()/NewImage_GetSize() (the TEMPORARY placeholder boot_hal.h
 * itself documents - OPEN-ITEMS.md #2, still open) synthesize a
 * BootImageHeader on the fly from SIM_FIRMWARE_UPDATE_FILE's raw bytes
 * (magic + size + a real CRC32 computed here) rather than requiring
 * firmware_update_management.c to build one itself - that keeps this
 * simulation entirely inside the boundary the header already calls a
 * placeholder, without reaching into still-undecided, real image-format/
 * Signature territory (bootloader.md sections 16/17) on the firmware/
 * side. Once the real Firmware Update <-> Inactive Bank relationship is
 * decided, this whole file (like NewImage_* itself) gets replaced - never
 * touched by the real per-target boot_hal implementation
 * (mcu_f4_boot/mcu_f1_boot).
 *
 * CRC32 (IEEE 802.3 / zlib / PKZIP polynomial, reflected, poly 0xEDB88320,
 * init/final XOR 0xFFFFFFFF): bootloader.md sections 16/19 leave the real
 * Integrity/Authenticity algorithm open - this is a concrete, standard,
 * deterministic choice so CRC_Calculate() has a real implementation for
 * simulation, not a claim about the final algorithm. */

#define SIM_RECOVERY_FILE         "sim_storage_recovery.bin"
#define SIM_BANK1_FILE            "sim_bank1.bin"
#define SIM_BANK2_FILE            "sim_bank2.bin"
#define SIM_FIRMWARE_UPDATE_FILE  "sim_nor_firmware_update.bin"

#define SIM_RECOVERY_CAPACITY_BYTES 64u /* generous headroom over the 3 known sub-records (24 bytes total) - see boot_control.h/boot_error.h */

static const char* bank_file(BankId bank) {
    return (bank == BANK_1) ? SIM_BANK1_FILE : SIM_BANK2_FILE;
}

/* --- Generic file-backed byte range helpers --- */

static BootHalResult file_read(const char* path, uint32_t offset, void* buffer, uint32_t length) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return BOOT_HAL_ERROR; /* never written yet - same "blank device" meaning every caller already handles */
    }

    if (fseek(f, (long)offset, SEEK_SET) != 0) {
        fclose(f);
        return BOOT_HAL_ERROR;
    }
    size_t read = fread(buffer, 1, length, f);
    fclose(f);

    if (read != length) {
        /* Short read (offset+length past what was ever written) - zero-fill
         * the remainder, same "blank Flash reads as a known pattern" idea
         * real hardware has, rather than handing the caller uninitialized
         * memory. Bytes actually read are left untouched. */
        memset((uint8_t*)buffer + read, 0xFF, length - read);
    }
    return BOOT_HAL_OK;
}

/* Read-modify-write against the whole file so a partial-range write never
 * truncates bytes outside [offset, offset+length) that were already there -
 * same "byte-granular update, whole-region persistence" shape
 * hardware_proxy_storage_stm32f4.c's EEPROM Emulation read-modify-write
 * already uses on the firmware/ side, just file-backed instead of
 * virtual-address-slot-backed. */
static BootHalResult file_write(const char* path, uint32_t offset, const void* data, uint32_t length) {
    FILE* f = fopen(path, "r+b");
    if (!f) {
        f = fopen(path, "w+b"); /* first write ever to this simulated region */
    }
    if (!f) {
        return BOOT_HAL_ERROR;
    }

    if (fseek(f, (long)offset, SEEK_SET) != 0) {
        fclose(f);
        return BOOT_HAL_ERROR;
    }
    size_t written = fwrite(data, 1, length, f);
    fclose(f);

    return (written == length) ? BOOT_HAL_OK : BOOT_HAL_ERROR;
}

/* --- Flash: Bank 1 / Bank 2 --- */

BootHalResult Flash_Read(BankId bank, uint32_t offset, void* buffer, uint32_t length) {
    if (offset > BOOT_BANK_SIZE_BYTES || length > BOOT_BANK_SIZE_BYTES - offset) {
        return BOOT_HAL_ERROR;
    }
    return file_read(bank_file(bank), offset, buffer, length);
}

BootHalResult Flash_Program(BankId bank, uint32_t offset, const void* data, uint32_t length) {
    if (offset > BOOT_BANK_SIZE_BYTES || length > BOOT_BANK_SIZE_BYTES - offset) {
        return BOOT_HAL_ERROR;
    }
    return file_write(bank_file(bank), offset, data, length);
}

BootHalResult Flash_Erase(BankId bank) {
    /* Real Flash Erase sets every byte to 0xFF - truncate-then-refill so a
     * stale, larger previous image can never leak past the end of a
     * smaller new one. */
    FILE* f = fopen(bank_file(bank), "wb");
    if (!f) {
        return BOOT_HAL_ERROR;
    }

    uint8_t blank[256];
    memset(blank, 0xFF, sizeof(blank));
    uint32_t remaining = BOOT_BANK_SIZE_BYTES;
    while (remaining > 0u) {
        uint32_t chunk = (remaining < sizeof(blank)) ? remaining : (uint32_t)sizeof(blank);
        if (fwrite(blank, 1, chunk, f) != chunk) {
            fclose(f);
            return BOOT_HAL_ERROR;
        }
        remaining -= chunk;
    }
    fclose(f);
    return BOOT_HAL_OK;
}

void Flash_JumpToApplication(BankId bank) {
    /* Never returns on real hardware (VTOR/MSP + branch into the image's
     * Reset Handler) - this Host double instead just reports success and
     * returns, same "only a Host/test double returns" convention the
     * header documents, mirroring firmware/'s HWProxy_ResetMcu() Host
     * behavior. There is no real Application to jump to on a PC. */
    printf("[bootloader] CRC valid - would jump to Bank %d (Host simulation: staying in process, not jumping)\n", (int)bank);
}

/* --- CRC32 (IEEE 802.3 / zlib, reflected, poly 0xEDB88320) ---
 * Table-less bit-loop implementation - simplicity over speed, fine for a
 * one-shot boot-time check on a simulated ~384KB image. */
static uint32_t crc32_update(uint32_t crc, const uint8_t* data, uint32_t length) {
    uint32_t i, j;
    for (i = 0; i < length; i++) {
        crc ^= data[i];
        for (j = 0; j < 8u; j++) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        }
    }
    return crc;
}

uint32_t CRC_Calculate(BankId bank, uint32_t offset, uint32_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    uint8_t  buffer[256];
    uint32_t remaining = length;
    uint32_t pos        = offset;

    while (remaining > 0u) {
        uint32_t chunk = (remaining < sizeof(buffer)) ? remaining : (uint32_t)sizeof(buffer);
        if (Flash_Read(bank, pos, buffer, chunk) != BOOT_HAL_OK) {
            return 0u; /* unreadable - will simply never match a stored CRC, same safe-failure shape as everywhere else here */
        }
        crc = crc32_update(crc, buffer, chunk);
        pos       += chunk;
        remaining -= chunk;
    }
    return crc ^ 0xFFFFFFFFu;
}

/* --- EEPROM: STORAGE_RECOVERY --- */

BootHalResult EEPROM_Read(uint32_t offset, void* buffer, uint32_t length) {
    if (offset > SIM_RECOVERY_CAPACITY_BYTES || length > SIM_RECOVERY_CAPACITY_BYTES - offset) {
        return BOOT_HAL_ERROR;
    }
    return file_read(SIM_RECOVERY_FILE, offset, buffer, length);
}

BootHalResult EEPROM_Write(uint32_t offset, const void* data, uint32_t length) {
    if (offset > SIM_RECOVERY_CAPACITY_BYTES || length > SIM_RECOVERY_CAPACITY_BYTES - offset) {
        return BOOT_HAL_ERROR;
    }
    return file_write(SIM_RECOVERY_FILE, offset, data, length);
}

/* --- New Image Source (TEMPORARY placeholder, see this file's top comment) --- */

/* ASCII "SAIP" - must equal boot_image_header.h's BOOT_IMAGE_MAGIC.
 * Duplicated here (not #included) because bootloader/src/ never includes
 * bootloader/include/ files that bootloader.c itself does not also
 * include as part of the public boot_hal.h boundary - this constant is a
 * private synthesis detail of this Host stub only. Kept in sync by hand;
 * if boot_image_header.h's magic ever changes, this must change with it. */
#define SIM_IMAGE_MAGIC 0x53414950u

static long raw_firmware_update_file_size(void) {
    FILE* f = fopen(SIM_FIRMWARE_UPDATE_FILE, "rb");
    if (!f) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    long size = ftell(f);
    fclose(f);
    return (size > 0) ? size : 0;
}

uint32_t NewImage_GetSize(void) {
    long raw_size = raw_firmware_update_file_size();
    return (raw_size > 0) ? (uint32_t)raw_size + (uint32_t)sizeof(uint32_t) * 3u : 0u; /* + synthesized BootImageHeader (magic/size/crc, 3 x uint32_t) */
}

BootHalResult NewImage_Read(uint32_t offset, void* buffer, uint32_t length) {
    long     raw_size = raw_firmware_update_file_size();
    uint32_t header_bytes[3]; /* magic, image_size, image_crc - same field order as boot_image_header.h's BootImageHeader */

    if (raw_size <= 0) {
        return BOOT_HAL_ERROR; /* nothing pending */
    }

    header_bytes[0] = SIM_IMAGE_MAGIC;
    header_bytes[1] = (uint32_t)raw_size;
    header_bytes[2] = 0xFFFFFFFFu;
    {
        /* Compute the CRC over the raw payload directly from the NOR file
         * (not through Flash_Read/a Bank - this is the source image, not
         * a Bank yet) using the same algorithm CRC_Calculate() uses, so
         * bank_image_is_valid()'s later check against this synthesized
         * header actually succeeds once the payload is copied into a
         * Bank. */
        FILE* f = fopen(SIM_FIRMWARE_UPDATE_FILE, "rb");
        if (!f) {
            return BOOT_HAL_ERROR;
        }
        uint32_t crc = 0xFFFFFFFFu;
        uint8_t  buffer[256];
        size_t   n;
        while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0u) {
            crc = crc32_update(crc, buffer, (uint32_t)n);
        }
        fclose(f);
        header_bytes[2] = crc ^ 0xFFFFFFFFu;
    }

    {
        const uint32_t header_size = (uint32_t)sizeof(header_bytes);
        uint8_t*       out         = (uint8_t*)buffer;
        uint32_t       copied      = 0u;

        /* Serve whatever [offset, offset+length) overlaps the synthesized
         * header first (plain memcpy, no file I/O needed), then the raw
         * payload right after it in one single open/seek/read - exactly
         * the "header at offset 0, payload right after" shape
         * boot_image_header.h documents. */
        if (offset < header_size) {
            uint32_t from_header = header_size - offset;
            if (from_header > length) {
                from_header = length;
            }
            memcpy(out, ((const uint8_t*)header_bytes) + offset, from_header);
            copied = from_header;
        }

        if (copied < length) {
            uint32_t payload_pos    = offset + copied - header_size;
            uint32_t payload_needed = length - copied;

            if (payload_pos >= (uint32_t)raw_size || payload_needed > (uint32_t)raw_size - payload_pos) {
                return BOOT_HAL_ERROR; /* past the end of the (header + payload) image */
            }

            FILE* f = fopen(SIM_FIRMWARE_UPDATE_FILE, "rb");
            if (!f || fseek(f, (long)payload_pos, SEEK_SET) != 0
                || fread(&out[copied], 1, payload_needed, f) != payload_needed) {
                if (f) fclose(f);
                return BOOT_HAL_ERROR;
            }
            fclose(f);
        }
    }

    return BOOT_HAL_OK;
}
