#include "infrastructure/drivers/uart.h"
/* STUB - وابسته به MCU واقعی؛ برای اتصال به EC200U استفاده خواهد شد. */
bool UART_Send(UART_Channel channel, const uint8_t* data, uint32_t length) { (void)channel; (void)data; (void)length; return true; }
uint32_t UART_Read(UART_Channel channel, uint8_t* buffer, uint32_t length) { (void)channel; (void)buffer; (void)length; return 0; }
