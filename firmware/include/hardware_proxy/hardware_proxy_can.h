#ifndef HARDWARE_PROXY_CAN_H
#define HARDWARE_PROXY_CAN_H
#include <stdint.h>

/* Contract source: docs/03-infrastructure-contracts/hardware-proxy.md
 * (this exact Domain was already anticipated there: "Diagnostic برای CAN").
 *
 * Only the Hardware Proxy implementation (firmware/src/hardware_proxy/) is
 * allowed to include infrastructure/drivers/can.h or mention CAN Registers
 * - every Management must go through this header instead. Today
 * Diagnostic Management is the ONLY consumer of this Domain
 * (docs/02-management-contracts/diagnostic-management.md section 19: CAN
 * is exclusive to Diagnostic, no Arbitration with any other Management -
 * a closed decision, not a temporary simplification). */

typedef struct {
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
} HWProxy_CanFrame;

typedef enum {
    HWPROXY_CAN_OK,
    HWPROXY_CAN_ERROR,  /* also what HWProxy_CAN_Receive() returns when nothing has arrived yet - the driver underneath does not distinguish "no data" from "error" (see infrastructure/drivers/can.c) */
    HWPROXY_CAN_BUSY
} HWProxy_CanResult;

HWProxy_CanResult HWProxy_CAN_Init(void);

/* Replaces CAN_Send for every consumer above this layer. */
HWProxy_CanResult HWProxy_CAN_Send(const HWProxy_CanFrame* frame);

/* Replaces CAN_Receive for every consumer above this layer - Non-Blocking
 * poll, must be called repeatedly from a Management's own Process(). */
HWProxy_CanResult HWProxy_CAN_Receive(HWProxy_CanFrame* frame);

#endif /* HARDWARE_PROXY_CAN_H */
