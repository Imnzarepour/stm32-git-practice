#ifndef BOOT_ERROR_H
#define BOOT_ERROR_H
#include <stdint.h>

/* Contract source: docs/03-infrastructure-contracts/bootloader.md section 12/13.
 *
 * This is the exact layout firmware/src/managements/system_management.c
 * already reads (check_boot_error(), BOOT_ERROR_RECORD_OFFSET) and converts
 * into a Fault on the Application side - this is the write side of that
 * same STORAGE_RECOVERY sub-record, at offset
 * STORAGE_RECOVERY_BOOT_ERROR_OFFSET (see boot_control.h). If a field is
 * added/removed/reordered here, system_management.c's local BootErrorRecord
 * struct and BOOT_ERROR_RECORD_OFFSET must change together with it.
 *
 * TODO(open-item): the doc only says "Error Type / Error Code / information
 * needed for Diagnosis" without pinning down the diagnosis payload - kept
 * to exactly the two fields the Application side already consumes.
 * error_type == 0 means "no Boot Error recorded" (BOOT_ERROR_TYPE_NONE on
 * the Application side). */

typedef struct {
    uint32_t error_type;
    uint32_t error_code;
} BootErrorRecord;

#define STORAGE_RECOVERY_BOOT_ERROR_OFFSET 8u /* right after BootControl - see boot_control.h */

/* error_type == 0 means "no Boot Error recorded" - must match
 * BOOT_ERROR_TYPE_NONE in firmware/src/managements/system_management.c. */
#define BOOT_ERROR_TYPE_NONE 0u

/* TODO(open-item): local/provisional, same discipline as
 * SYSTEM_FAULT_CODE_* in system_management.c - no official Contract fixes
 * these values (OPEN-ITEMS.md #5, Fault struct still open). Only used as
 * error_type by bootloader/src/bootloader.c. */
#define BOOT_ERROR_TYPE_ACTIVE_BANK_INVALID 1u /* neither Bank had a valid image at Boot time (bootloader.md section 4, worst case) */
#define BOOT_ERROR_TYPE_PROGRAMMING_FAILED  2u /* Program+Verify failed BOOT_MAX_PROGRAM_RETRIES times in a row (section 9) */

#endif /* BOOT_ERROR_H */
