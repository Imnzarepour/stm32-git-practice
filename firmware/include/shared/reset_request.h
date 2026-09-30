#ifndef SHARED_RESET_REQUEST_H
#define SHARED_RESET_REQUEST_H
#include <stdint.h>

/* Contract source: docs/02-management-contracts/reset-request-contract.md
 *
 * قانون: فقط System Management مجاز به اجرای Reset است. هیچ Management
 * دیگری نباید مستقیماً MCU یا Module را Reset کند.
 */

typedef enum {
    RESET_TYPE_SYSTEM_RESET,
    RESET_TYPE_MODULE_RESET
} ResetType;

/* TODO(open-item): طبق docs/04-open-items/OPEN-ITEMS.md، این enum ناقص است.
 * HARDWARE_INPUT و ACTUATOR هم به این مسیر نیاز دارند اما در سند اصلی نبودند. */
typedef enum {
    RESET_SOURCE_GPS,
    RESET_SOURCE_SERVER,
    RESET_SOURCE_SMS,
    RESET_SOURCE_CALL,
    RESET_SOURCE_FIRMWARE_UPDATE,
    RESET_SOURCE_DIAGNOSTIC,
    RESET_SOURCE_HARDWARE_INPUT, /* TODO: تأیید رسمی نشده */
    RESET_SOURCE_ACTUATOR        /* TODO: تأیید رسمی نشده */
} ResetSource;

typedef enum {
    RESET_REASON_MODULE_UNRESPONSIVE,
    RESET_REASON_RECOVERY_FAILED,
    RESET_REASON_COMMUNICATION_STUCK,
    RESET_REASON_INITIALIZATION_FAILED,
    RESET_REASON_UNEXPECTED_STATE,
    /* Deliberate, not a failure: Firmware Update Management has already
     * written Boot Action=PROGRAM into STORAGE_RECOVERY (see
     * managements/firmware_update_management.c) and needs the MCU to
     * actually reset so Bootloader picks it up - docs/03-infrastructure-
     * contracts/bootloader.md section 18. reset-request-contract.md §4-4
     * lists its Reason values as examples, not an exhaustive/closed set. */
    RESET_REASON_FIRMWARE_INSTALL_REQUESTED
} ResetReason;

/* Target مقداریه مثل "MCU" یا "EC200U"؛ برای SYSTEM_RESET پیش‌فرض MCU است. */
typedef struct {
    ResetType   type;
    const char* target;
    ResetSource source;
    ResetReason reason;
    uint32_t    timestamp;
} ResetRequest;

/* پیاده‌سازی واقعی داخل System Management است.
 * این تابع Non-Blocking است و هیچ Response/Ack عمومی برنمی‌گرداند. */
void System_SubmitResetRequest(const ResetRequest* request);

#endif /* SHARED_RESET_REQUEST_H */
