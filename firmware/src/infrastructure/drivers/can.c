#include "infrastructure/drivers/can.h"
/* STUB - وابسته به MCU واقعی. */
CAN_Result CAN_Init(void) { return CAN_OK; }
CAN_Result CAN_Send(const CAN_Frame* frame) { (void)frame; return CAN_OK; }
CAN_Result CAN_Receive(CAN_Frame* frame) { (void)frame; return CAN_ERROR; /* یعنی فعلا داده‌ای نیست */ }
