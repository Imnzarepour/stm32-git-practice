#ifndef BOOT_CONTROL_H
#define BOOT_CONTROL_H
#include <stdint.h>
#include "bank_id.h"
#include "boot_action.h"

/* Contract source: docs/03-infrastructure-contracts/bootloader.md section 3.
 * BootControl is the part of STORAGE_RECOVERY that Bootloader owns and
 * writes; Application (System Management) never reads or writes it - see
 * firmware/src/managements/system_management.c.
 *
 * TODO(open-item): the physical backend for STORAGE_RECOVERY is not decided
 * anywhere yet (memory-storage-infrastructure.md / OPEN-ITEMS.md) - this
 * struct only fixes the *logical* layout so both sides of the repo (this
 * folder and firmware/) can agree on byte offsets before either side has a
 * real Storage driver underneath. */

typedef struct {
    BankId     active_bank;
    BootAction action;
} BootControl;

/* STORAGE_RECOVERY byte layout (provisional, not an official Contract -
 * chosen here only so this project and firmware/'s already-implemented
 * consumer (system_management.c, BOOT_ERROR_RECORD_OFFSET) agree with each
 * other from day one). If this ordering changes, both sides must change
 * together.
 *
 *   offset 0 : BootControl     (2 x uint32_t = 8 bytes)
 *   offset 8 : BootErrorRecord (see boot_error.h)
 */
#define STORAGE_RECOVERY_BOOT_CONTROL_OFFSET 0u

#endif /* BOOT_CONTROL_H */
