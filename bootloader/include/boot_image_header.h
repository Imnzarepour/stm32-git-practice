#ifndef BOOT_IMAGE_HEADER_H
#define BOOT_IMAGE_HEADER_H
#include <stdint.h>

/* TEMPORARY / provisional - explicitly NOT an official Contract.
 * bootloader.md leaves the real image format, Signature type, and
 * Upgrade/Downgrade Version policy open (sections 16/17) - this is only
 * the minimum needed to make "Calculate CRC -> Compare" (sections 4/8)
 * something bootloader.c can actually execute and test today, per an
 * explicit request to keep testing/debugging moving instead of blocking
 * on those open decisions.
 *
 * Layout: this header sits at offset 0 of every Bank (Bank 1 and Bank 2
 * are identical in shape); the payload (the actual firmware bytes) comes
 * right after it, image_size bytes long.
 *
 * Replace this whole file (and every bootloader.c function that touches
 * it) once the real image format / Signature policy is decided - do not
 * treat this as load-bearing beyond Host testing. */

#define BOOT_IMAGE_MAGIC 0x53414950u /* ASCII "SAIP" - arbitrary, only used
                                        to tell "an image was written here"
                                        apart from blank/erased Flash */

typedef struct {
    uint32_t magic;      /* must equal BOOT_IMAGE_MAGIC, else the Bank is treated as empty/invalid - never as "valid with zero size" */
    uint32_t image_size; /* payload bytes right after this header, not including the header itself */
    uint32_t image_crc;  /* CRC32 over exactly image_size payload bytes, computed by whatever staged this image (see boot_hal.h NewImage_* - not by the Bootloader itself) */
} BootImageHeader;

#endif /* BOOT_IMAGE_HEADER_H */
