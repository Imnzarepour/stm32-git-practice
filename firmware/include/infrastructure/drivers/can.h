#ifndef DRIVER_CAN_H
#define DRIVER_CAN_H
#include <stdbool.h>
#include <stdint.h>
/* Contract source: docs/03-infrastructure-contracts/driver-layer.md §7
 * Driver مفهوم DTC/Diagnostic/Vehicle Parameter را نمی‌شناسد؛ فقط Frame خام. */
typedef struct {
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
} CAN_Frame;

typedef enum { CAN_OK, CAN_ERROR, CAN_BUSY } CAN_Result;

CAN_Result CAN_Init(void);
CAN_Result CAN_Send(const CAN_Frame* frame);
CAN_Result CAN_Receive(CAN_Frame* frame);
#endif
