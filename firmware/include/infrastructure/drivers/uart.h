#ifndef DRIVER_UART_H
#define DRIVER_UART_H
#include <stdint.h>
#include <stdbool.h>
/* Contract source: docs/03-infrastructure-contracts/driver-layer.md §8
 * برای ارتباط با EC200U. AT Command/SMS/GNSS/TCP اینجا نباید تعریف شود -
 * آن‌ها مسئولیت EC200U Manager هستند (که هنوز Contract نهایی‌اش باز است). */
typedef int UART_Channel;
bool     UART_Send(UART_Channel channel, const uint8_t* data, uint32_t length);
uint32_t UART_Read(UART_Channel channel, uint8_t* buffer, uint32_t length);
#endif
