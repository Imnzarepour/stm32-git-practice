#ifndef MGMT_POWER_H
#define MGMT_POWER_H

/* Contract source: docs/02-management-contracts/power-management.md
 * قاعده: ACC ON -> NORMAL ، ACC OFF -> LOW_POWER (این دو تنها Mode هستند)
 * Power Management فقط Mode را تعیین/اعلام می‌کند؛ رفتار هر Capability در
 * LOW_POWER مسئولیت خود آن Management است. */

typedef enum {
    POWER_NORMAL,
    POWER_LOW_POWER
} PowerMode;

void      Power_Init(void);
void      Power_Process(void);
PowerMode Power_GetMode(void); /* Read-Only، Non-Blocking */

#endif
