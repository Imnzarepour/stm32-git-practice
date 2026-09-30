#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

/* All the tunable constants bootloader/src/bootloader.c needs, collected
 * in one place - same convention as
 * firmware/include/managements/hardware_input_management_config.h.
 *
 * TODO(open-item): BOOT_BANK_SIZE_BYTES is an ESTIMATE, not a closed
 * decision - it WILL change once real firmware size is measured and the
 * Flash partition layout is explicitly confirmed by the user. It exists
 * so Bank layout math (bootloader.c) and Host tests have a concrete,
 * deterministic number today. Whoever closes the real decision must
 * update this one constant; nothing else in bootloader.c needs to change
 * since every Bank access already goes through
 * Flash_Read/Flash_Program/Flash_Erase with this as the only size
 * reference.
 *
 * Where 384 KB comes from (docs/03-infrastructure-contracts/
 * nor-flash-infrastructure.md section 4-1-1): STM32F407VGTx has only
 * 1024 KB of INTERNAL Flash total (MCU_F4/STM32F407VGTX_FLASH.ld) -
 * shared between the Bootloader region, both Banks, and
 * STORAGE_CONFIG/STORAGE_RECOVERY. Using the real F407 sector map
 * (4x16KB + 1x64KB + 7x128KB): Bootloader = sectors 0-3 (64 KB), Bank A =
 * 3x128KB sectors (384 KB), Bank B = the other 3x128KB sectors (384 KB),
 * leaving 192 KB (sector 4 + the last 128KB sector) for
 * Config/Recovery/margin - which is exactly why 384 KB replaces the
 * previous 256 KB placeholder (256 KB would already be too small for a
 * plausible ~300 KB real image). This also corrects the earlier ~2 MB
 * Firmware Image assumption in nor-flash-infrastructure.md section 4-1,
 * which never fit this board's actual 1 MB internal Flash budget to begin
 * with. */
#define BOOT_BANK_SIZE_BYTES (384u * 1024u) /* ESTIMATE, changed from 256KB - see note above */

/* "Retry Limit" (bootloader.md section 9) - the document explicitly says
 * this is an implementation parameter, not something the architecture
 * Contract fixes, so picking a number here is not a guess of an open
 * architectural decision. */
#define BOOT_MAX_PROGRAM_RETRIES 3u

/* Chunk size used by bootloader.c when copying a new image from
 * NewImage_Read() into Flash_Program() - an implementation detail of the
 * copy loop, not part of any Contract. Kept small so a Fake/CMock-driven
 * Host test can simulate a power loss partway through a multi-chunk
 * write without needing a huge buffer. */
#define BOOT_COPY_CHUNK_BYTES 64u

#endif /* BOOT_CONFIG_H */
