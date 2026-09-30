#include "managements/power_management.h"
#include "managements/hardware_input_management.h"

/* Contract: docs/02-management-contracts/power-management.md
 * Rule (section 3-1 / 10): ACC ON -> NORMAL, ACC OFF -> LOW_POWER; these
 * are the only two Modes. ACC comes from Hardware Input Management via
 * the Data path (Provider -> Latest Value -> Consumer, GetDigitalInput),
 * never a direct GPIO/register read - Power Management has no hardware
 * ownership of ACC (section 5-1). */

static PowerMode s_mode;

void Power_Init(void) {
    s_mode = POWER_LOW_POWER; /* Conservative, low-consumption default until the
                                 real ACC status is known - decided by the user
                                 to answer the previously open question */
}

void Power_Process(void) {
    DigitalInputValue acc = GetDigitalInput(DIGITAL_INPUT_ACC);

    if (!acc.valid) {
        return; /* Keep the last known Mode until ACC is trustworthy again */
    }

    s_mode = acc.value ? POWER_NORMAL : POWER_LOW_POWER;
}

PowerMode Power_GetMode(void) {
    return s_mode;
}
