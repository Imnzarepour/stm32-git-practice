#include "hardware_proxy/hardware_proxy_can.h"
#include "infrastructure/drivers/can.h"

/* One of the only files in the whole firmware allowed to include
 * infrastructure/drivers headers and mention CAN - see the header for the
 * rule. No business logic (Diagnostic Protocol, UDS, DTC decoding) lives
 * here; this is pure pass-through with type/result translation, exactly
 * like hardware_proxy_digital_io.c. */

static HWProxy_CanFrame ToProxyFrame(const CAN_Frame* frame) {
    HWProxy_CanFrame proxy_frame;
    proxy_frame.id  = frame->id;
    proxy_frame.dlc = frame->dlc;
    for (uint8_t i = 0; i < 8u; i++) {
        proxy_frame.data[i] = frame->data[i];
    }
    return proxy_frame;
}

static CAN_Frame ToDriverFrame(const HWProxy_CanFrame* proxy_frame) {
    CAN_Frame frame;
    frame.id  = proxy_frame->id;
    frame.dlc = proxy_frame->dlc;
    for (uint8_t i = 0; i < 8u; i++) {
        frame.data[i] = proxy_frame->data[i];
    }
    return frame;
}

static HWProxy_CanResult ToProxyResult(CAN_Result result) {
    switch (result) {
        case CAN_OK:    return HWPROXY_CAN_OK;
        case CAN_BUSY:  return HWPROXY_CAN_BUSY;
        default:        return HWPROXY_CAN_ERROR;
    }
}

HWProxy_CanResult HWProxy_CAN_Init(void) {
    return ToProxyResult(CAN_Init());
}

HWProxy_CanResult HWProxy_CAN_Send(const HWProxy_CanFrame* frame) {
    CAN_Frame driver_frame = ToDriverFrame(frame);
    return ToProxyResult(CAN_Send(&driver_frame));
}

HWProxy_CanResult HWProxy_CAN_Receive(HWProxy_CanFrame* frame) {
    CAN_Frame driver_frame;
    CAN_Result result = CAN_Receive(&driver_frame);
    if (result == CAN_OK) {
        *frame = ToProxyFrame(&driver_frame);
    }
    return ToProxyResult(result);
}
