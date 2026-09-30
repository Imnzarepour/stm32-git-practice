#ifndef BOOT_BANK_ID_H
#define BOOT_BANK_ID_H

/* Contract source: docs/03-infrastructure-contracts/bootloader.md section 2/3.
 * Deliberately independent from firmware/include - the Bootloader world
 * never includes anything under firmware/. */

typedef enum {
    BANK_1,
    BANK_2
} BankId;

#endif /* BOOT_BANK_ID_H */
