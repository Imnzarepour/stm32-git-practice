#include "bootloader.h"
#include "boot_hal.h"
#include "boot_control.h"
#include "boot_error.h"
#include "boot_image_header.h"
#include "boot_config.h"
#include <stdbool.h>
#include <stddef.h>

/* Contract source: docs/03-infrastructure-contracts/bootloader.md,
 * mainly sections 4-15 and 18 (the overall flow diagram).
 *
 * This file only talks to hardware through boot_hal.h (Flash_*, CRC_*,
 * EEPROM_*, NewImage_*) - never a HAL/CMSIS/register call directly, per
 * this project's own non-negotiable rule (mirrored from firmware/'s
 * Hardware Proxy discipline, see boot_hal.h's own top comment). Real
 * target implementations of boot_hal.h do not exist yet (no CubeIDE
 * project for either board's Bootloader) - today this file is only
 * exercised by test_bootloader.c through a CMock-generated Mockboot_hal.h.
 *
 * Two things below are explicitly TEMPORARY placeholders, not closed
 * architecture decisions - kept so this file is written and testable now
 * instead of blocked on open items, per an explicit request to do so:
 *   - BootImageHeader (boot_image_header.h) - the image format/Signature
 *     policy is still open (bootloader.md sections 16/17).
 *   - NewImage_* (boot_hal.h) - the Firmware Update <-> Inactive Bank relationship is
 *     still open (bootloader.md section 6, OPEN-ITEMS.md #2). Now named
 *     "PROGRAM Staging" by docs/03-infrastructure-contracts/
 *     nor-flash-infrastructure.md section 4-3, but that document's own
 *     sections 4-1 (16MB NOR capacity, blocking) and 4-2 (is this
 *     intermediate copy even needed?) are still open too.
 * Both are called out again at their use sites below. */

static BankId other_bank(BankId bank) {
    return (bank == BANK_1) ? BANK_2 : BANK_1;
}

/* bootloader.md section 4 ("Read Active Bank -> Calculate CRC -> Compare")
 * and section 8 ("Programming -> CRC -> OK/FAIL") are the exact same
 * check applied to two different Banks - the src/README.md sketch names
 * them check_active_bank_crc() and verify_programmed_bank() separately,
 * but the logic itself is identical, so it lives here once. */
static bool bank_image_is_valid(BankId bank) {
    BootImageHeader header;

    if (Flash_Read(bank, 0, &header, sizeof(header)) != BOOT_HAL_OK) {
        return false;
    }
    if (header.magic != BOOT_IMAGE_MAGIC) {
        return false; /* blank/erased/garbage Bank - never treated as "valid with zero size" */
    }
    if (header.image_size == 0u || header.image_size > BOOT_BANK_SIZE_BYTES - sizeof(header)) {
        return false; /* malformed - would read past this Bank's own reserved space */
    }

    uint32_t computed_crc = CRC_Calculate(bank, (uint32_t)sizeof(header), header.image_size);
    return computed_crc == header.image_crc;
}

/* bootloader.md section 12: persists a Boot Error into STORAGE_RECOVERY.
 * The Application side (firmware/src/managements/system_management.c ->
 * check_boot_error()) reads this at its own next Startup, turns it into a
 * Fault, and clears it - this file never clears its own error record, it
 * only ever writes one. */
static void record_boot_error(uint32_t error_type, uint32_t error_code) {
    BootErrorRecord error;
    error.error_type = error_type;
    error.error_code = error_code;
    (void)EEPROM_Write(STORAGE_RECOVERY_BOOT_ERROR_OFFSET, &error, sizeof(error));
}

/* bootloader.md section 11: Active Bank only changes AFTER a successful
 * Verify - this is the only place in this file allowed to change it, and
 * it is only ever called right after bank_image_is_valid() returned true
 * for the newly-programmed Bank (see run_program_flow() below). Boot
 * Action is reset to BOOT_ACTION_BOOT in the same write, so the next
 * Bootloader_Run() boots normally instead of re-entering PROGRAM. */
static void switch_active_bank(BankId new_active_bank) {
    BootControl control;
    control.active_bank = new_active_bank;
    control.action      = BOOT_ACTION_BOOT;
    (void)EEPROM_Write(STORAGE_RECOVERY_BOOT_CONTROL_OFFSET, &control, sizeof(control));
}

/* bootloader.md section 9: on total failure, the previous Active Bank
 * must be left untouched (section 10, Fail-Safe) - this only rewrites
 * Boot Action back to BOOT, using whatever Bank was already active. */
static void keep_active_bank_and_return_to_boot(BankId still_active_bank) {
    BootControl control;
    control.active_bank = still_active_bank;
    control.action       = BOOT_ACTION_BOOT;
    (void)EEPROM_Write(STORAGE_RECOVERY_BOOT_CONTROL_OFFSET, &control, sizeof(control));
}

/* bootloader.md sections 5-7: Programming always targets the Inactive
 * Bank. TEMPORARY: the source of the new image is NewImage_* (boot_hal.h)
 * - a stand-in for Firmware Update's still-undecided staging location (section 6).
 * Returns false on any failure (missing/oversized image, Erase failure,
 * a chunk Read/Program failure - including a simulated mid-write power
 * loss in tests); the caller (run_program_flow()) is the one that decides
 * what a false result means (Retry vs give up). */
static bool program_inactive_bank(BankId inactive_bank) {
    uint32_t total_bytes = NewImage_GetSize();

    if (total_bytes == 0u || total_bytes > BOOT_BANK_SIZE_BYTES) {
        return false; /* nothing pending, or it would not fit in one Bank */
    }

    if (Flash_Erase(inactive_bank) != BOOT_HAL_OK) {
        return false;
    }

    uint8_t  chunk_buffer[BOOT_COPY_CHUNK_BYTES];
    uint32_t offset = 0u;

    while (offset < total_bytes) {
        uint32_t remaining = total_bytes - offset;
        uint32_t chunk_len = (remaining < BOOT_COPY_CHUNK_BYTES) ? remaining : BOOT_COPY_CHUNK_BYTES;

        if (NewImage_Read(offset, chunk_buffer, chunk_len) != BOOT_HAL_OK) {
            return false;
        }
        if (Flash_Program(inactive_bank, offset, chunk_buffer, chunk_len) != BOOT_HAL_OK) {
            return false; /* covers a simulated power loss mid-write in tests */
        }
        offset += chunk_len;
    }

    return true;
}

/* bootloader.md section 9: bounded Retry, then a safe return to the
 * previous Active Bank - never an unbounded loop, never leaves the system
 * without a valid Active Bank. Called only when Boot Action was PROGRAM
 * (see Bootloader_Run() below). */
static void run_program_flow(BankId active_bank) {
    BankId   inactive_bank = other_bank(active_bank);
    uint32_t attempt;

    for (attempt = 0u; attempt < BOOT_MAX_PROGRAM_RETRIES; attempt++) {
        if (program_inactive_bank(inactive_bank) && bank_image_is_valid(inactive_bank)) {
            switch_active_bank(inactive_bank); /* section 11 - only after a successful Verify */
            return;
        }
    }

    /* Retries exhausted (section 9) - the previous Active Bank is still
     * valid (Fail-Safe, section 10: it was never touched by any of the
     * attempts above, only inactive_bank was), so it is safe to fall back
     * to it and continue booting normally. */
    record_boot_error(BOOT_ERROR_TYPE_PROGRAMMING_FAILED, attempt);
    keep_active_bank_and_return_to_boot(active_bank);
}

void Bootloader_Run(void) {
    BootControl control;

    if (EEPROM_Read(STORAGE_RECOVERY_BOOT_CONTROL_OFFSET, &control, sizeof(control)) != BOOT_HAL_OK) {
        /* No valid BootControl yet (first boot on blank EEPROM, or the
         * read itself failed) - bootloader.md does not define a formal
         * first-boot default anywhere, so this is a deliberate, minimal,
         * documented choice: assume Bank 1 and a normal BOOT, exactly as
         * if it had already been booting from Bank 1 - AND persist that
         * assumption immediately, so every later reader (this file's own
         * next Bootloader_Run(), or the Application's
         * firmware_update_management.c preserving active_bank before it
         * writes a PROGRAM request) finds a real record instead of
         * hitting this same "unreadable" fallback again. Without this
         * write, a brand-new device's very first real Firmware Update
         * Install attempt would always fail: the Application's own
         * pre-PROGRAM read of STORAGE_RECOVERY would find nothing, and it
         * deliberately never guesses in that case (see OPEN-ITEMS.md). A
         * failure writing this back is not itself treated as fatal - the
         * in-memory default is used either way, and the worst-case path
         * below (record_boot_error + ACTIVE_BANK_INVALID) still applies
         * if Bank 1 also turns out invalid. */
        control.active_bank = BANK_1;
        control.action       = BOOT_ACTION_BOOT;
        (void)EEPROM_Write(STORAGE_RECOVERY_BOOT_CONTROL_OFFSET, &control, sizeof(control));
    }

    if (control.action == BOOT_ACTION_PROGRAM) {
        run_program_flow(control.active_bank);
        /* run_program_flow() always leaves EEPROM in a BOOT_ACTION_BOOT
         * state (either the new Bank on success, or the old one on
         * failure) - reload so the BOOT flow below sees the final result
         * instead of the stale PROGRAM request. */
        if (EEPROM_Read(STORAGE_RECOVERY_BOOT_CONTROL_OFFSET, &control, sizeof(control)) != BOOT_HAL_OK) {
            return; /* EEPROM itself is unreadable right after writing it - stay in Bootloader, nothing safe left to decide */
        }
    }

    if (bank_image_is_valid(control.active_bank)) {
        Flash_JumpToApplication(control.active_bank); /* never returns on real hardware */
        return; /* only reached on the Host/test double */
    }

    /* bootloader.md section 4, worst case: the Active Bank itself is
     * invalid. Never guess (e.g. never try the other Bank on our own
     * initiative - that is a Recovery Mode decision, out of scope per
     * section 14/22) - stay in Bootloader. */
    record_boot_error(BOOT_ERROR_TYPE_ACTIVE_BANK_INVALID, (uint32_t)control.active_bank);
}
