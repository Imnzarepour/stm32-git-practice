#ifndef BOOTLOADER_H
#define BOOTLOADER_H

/* Contract source: docs/03-infrastructure-contracts/bootloader.md section 18.
 *
 * Entry point called once, right after Reset, before any Application code
 * runs (bootloader.md section 0/10 - Bootloader is not part of the
 * Application Super Loop). On real hardware this either never returns
 * (it jumps into the Application via Flash_JumpToApplication(), see
 * boot_hal.h) or returns having deliberately stayed in Bootloader mode
 * (no valid image anywhere - section 4/14, Recovery/Update mode is out of
 * scope here).
 *
 * Unlike firmware/'s Management Process() functions, this is explicitly
 * ALLOWED to run sequentially / block within itself (bootloader.md
 * section 15: "Non-Blocking applies to the Application Super Loop -
 * Bootloader does not necessarily share that execution model"). It must
 * still have bounded retries (boot_config.h's BOOT_MAX_PROGRAM_RETRIES)
 * and a defined failure path, which is what section 15 actually requires. */
void Bootloader_Run(void);

#endif /* BOOTLOADER_H */
