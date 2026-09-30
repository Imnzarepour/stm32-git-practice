#ifndef BOOT_HAL_H
#define BOOT_HAL_H
#include <stdint.h>
#include "bank_id.h"

/* Boundary layer between Bootloader decision logic (bootloader/src/
 * bootloader.c) and real hardware - this project's equivalent of
 * firmware/'s Hardware Proxy (docs/03-infrastructure-contracts/
 * hardware-proxy.md), even though bootloader/ is a fully separate project
 * that must never include anything under firmware/. bootloader.c must
 * NEVER call HAL/CMSIS/register access directly, and must never include
 * an ST HAL header - only the functions declared here.
 *
 * Real implementations belong in mcu_f4_boot/ and mcu_f1_boot/, one .c
 * file per target (same *_stm32f4.c / *_stm32f1.c suffix convention
 * firmware/src/hardware_proxy/ already uses), calling the real Flash/
 * CRC-peripheral/EEPROM-emulation HAL APIs of each board. Neither exists
 * yet - no CubeIDE project has been created for either board's Bootloader
 * (mcu_f4_boot/README.md, mcu_f1_boot/README.md). Until then, the only
 * implementation of this header is test_bootloader.c's CMock-generated
 * Mockboot_hal.h, exactly like firmware/test/unit/test_storage.c mocks
 * hardware_proxy_storage.h instead of touching real EEPROM Emulation. */

typedef enum {
    BOOT_HAL_OK,
    BOOT_HAL_ERROR
} BootHalResult;

/* --- Flash: Bank 1 / Bank 2 firmware image storage ---
 * bootloader.md section 2: "Programming always happens on the Inactive
 * Bank; the Active Bank is untouched until the full Update succeeds." All
 * three functions below operate within a single Bank (BOOT_BANK_SIZE_BYTES
 * each, see boot_config.h) - bootloader.c never addresses raw Flash. */
BootHalResult Flash_Read(BankId bank, uint32_t offset, void* buffer, uint32_t length);
BootHalResult Flash_Program(BankId bank, uint32_t offset, const void* data, uint32_t length);
BootHalResult Flash_Erase(BankId bank);

/* Never returns on real hardware (disables interrupts, sets VTOR/MSP,
 * branches into the image's Reset Handler - bootloader.md section 3.3).
 * Only a Host/test double returns, so tests can assert it was called with
 * the right Bank - same pattern as firmware/hardware_proxy_watchdog.h's
 * HWProxy_ResetMcu(). */
void Flash_JumpToApplication(BankId bank);

/* --- CRC ---
 * Algorithm/output size is not fixed by any Contract yet (bootloader.md
 * sections 16/19 leave the Integrity vs Authenticity policy open) - this
 * boundary only fixes one thing so bootloader.c can stay independent of
 * that decision: a CRC32 computed over `length` bytes of `bank` starting
 * at `offset`. Swapping the algorithm/peripheral later never touches
 * bootloader.c. */
uint32_t CRC_Calculate(BankId bank, uint32_t offset, uint32_t length);

/* --- EEPROM: STORAGE_RECOVERY (BootControl + BootErrorRecord) ---
 * Same STORAGE_RECOVERY byte layout firmware/'s System Management already
 * reads/writes (see boot_control.h / boot_error.h for the offsets). */
BootHalResult EEPROM_Read(uint32_t offset, void* buffer, uint32_t length);
BootHalResult EEPROM_Write(uint32_t offset, const void* data, uint32_t length);

/* --- New Image Source: TEMPORARY placeholder ---
 * TODO(open-item): stands in for "wherever Firmware Update eventually stages a
 * downloaded image" - bootloader.md section 6 / OPEN-ITEMS.md #2 leaves
 * the real relationship between Firmware Update's download location and the
 * Inactive Bank completely open (direct download into the Inactive Bank?
 * a separate Staging area on External NOR that Bootloader copies/
 * decrypts from?). This pair of functions exists only so
 * program_inactive_bank() (bootloader.c) is testable today, per an
 * explicit request to keep testing/debugging moving instead of blocking
 * on that decision. Must be replaced - likely removed entirely and
 * folded into whatever real source Firmware Update settles on - once that Open Item
 * closes; every caller in bootloader.c that touches it will need to
 * change together with it. The image these functions serve is expected
 * to already be shaped like a Bank (BootImageHeader + payload, see
 * boot_image_header.h) - Bootloader only copies it, it does not build the
 * header itself.
 *
 * Since this was written, docs/03-infrastructure-contracts/
 * nor-flash-infrastructure.md gave this exact boundary a name and shape:
 * "PROGRAM Staging" on External NOR (W25Q128), Raw (no LittleFS),
 * Writer=Firmware Update Management, Reader=Bootloader (that doc's section 4-3) -
 * which matches this pair of functions closely. docs/03-infrastructure-
 * contracts/file-transfer-infrastructure.md (section 14) fills in the
 * rest of the chain upstream of this: Server -> File Transfer
 * Infrastructure (generic chunked download, does not know this is
 * firmware) -> Firmware Staging (LittleFS) -> Firmware Update Management validates and
 * writes PROGRAM Staging raw -> sets Boot Action=PROGRAM -> Reset. That
 * document still leaves open whether this intermediate copy is even
 * needed at all (nor-flash-infrastructure.md section 4-2) and whether
 * 16MB of NOR is even enough for the current partition plan (section
 * 4-1, blocking) - so this is still a placeholder, not a closed
 * decision; only its name/likely shape is less of a guess now. */
BootHalResult NewImage_Read(uint32_t offset, void* buffer, uint32_t length);
uint32_t      NewImage_GetSize(void); /* 0 = no new image available/pending */

#endif /* BOOT_HAL_H */
