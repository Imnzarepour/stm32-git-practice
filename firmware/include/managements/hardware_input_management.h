#ifndef MGMT_HARDWARE_INPUT_H
#define MGMT_HARDWARE_INPUT_H
#include <stdint.h>
#include <stdbool.h>

/* Contract source: docs/02-management-contracts/hardware-input-management.md
 *
 * API naming (Open Item #1, yellow, in OPEN-ITEMS.md) was finalized with
 * the user: the hardware-input-io-contract-DRAFT-A.md style was chosen
 * (GetDigitalInput/GetBatteryVoltage instead of Input_GetDigitalState or
 * HardwareInput_GetState). That decision only settles interface naming;
 * the conceptual State/Event model from both drafts (a stable State plus
 * an Event on Change/Condition) is honored in the implementation.
 */

typedef enum {
    DIGITAL_INPUT_ACC,
    DIGITAL_INPUT_DOOR,
    DIGITAL_INPUT_SHOCK
    /* A 4th digital channel has not been assigned yet - see the System I/O doc */
} DigitalInputId;

typedef struct {
    bool     value;
    bool     valid;
    uint32_t timestamp;
} DigitalInputValue;

typedef enum {
    ANALOG_INPUT_BATTERY_VOLTAGE,          /* main vehicle battery - the important one, also sent to Telemetry */
    ANALOG_INPUT_INTERNAL_BATTERY_VOLTAGE  /* secondary channel - may be removed later, per the user */
} AnalogInputId;

typedef struct {
    uint32_t millivolt;
    bool     valid;
    uint32_t timestamp;
} BatteryVoltage;

void               HardwareInput_Init(void);
void               HardwareInput_Process(void);
DigitalInputValue  GetDigitalInput(DigitalInputId id);          /* Read-only, non-blocking, never triggers a new acquisition */
BatteryVoltage     GetBatteryVoltage(void);                     /* Main vehicle battery - read-only, non-blocking */
BatteryVoltage     GetInternalBatteryVoltage(void);              /* Secondary channel - read-only, non-blocking */

#endif
