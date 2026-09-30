#include "hardware_proxy/hardware_proxy_uart.h"
#include "infrastructure/drivers/uart.h"

/* Host/Build-without-real-hardware pass-through - same role as
 * hardware_proxy_can.c: pure Send/Read translation onto the generic
 * infrastructure/drivers/uart.h Host-Stub, no business logic. The real
 * MCU_F4 implementation (hardware_proxy_uart_stm32f4.c) does NOT go
 * through infrastructure/drivers/uart.h at all - it talks to the HAL UART
 * peripheral directly, same precedent hardware_proxy_analog_io_stm32f4.c
 * already set (CMakeLists.txt excludes every "*_stm32f4.c" file from this
 * host build, so both files never coexist in one binary). */

static UART_Channel s_channel = 0;

void HWProxy_UART_Init(void) {
    /* infrastructure/drivers/uart.h's Host-Stub has no Init() of its own -
     * nothing to do here beyond picking the (single) channel number. */
}

HWProxy_UartResult HWProxy_UART_Send(const uint8_t* data, uint32_t length) {
    if (data == NULL || length == 0u) {
        return HWPROXY_UART_ERROR;
    }
    return UART_Send(s_channel, data, length) ? HWPROXY_UART_OK : HWPROXY_UART_ERROR;
}

uint32_t HWProxy_UART_Read(uint8_t* buffer, uint32_t buffer_size) {
    if (buffer == NULL || buffer_size == 0u) {
        return 0u;
    }
    return UART_Read(s_channel, buffer, buffer_size);
}
