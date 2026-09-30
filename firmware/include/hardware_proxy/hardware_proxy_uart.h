#ifndef HARDWARE_PROXY_UART_H
#define HARDWARE_PROXY_UART_H
#include <stdint.h>
#include <stdbool.h>

/* Contract source: docs/03-infrastructure-contracts/hardware-proxy.md
 * (this exact Domain was already anticipated there: "EC200U Manager برای
 * UART"). Only the Hardware Proxy implementation (firmware/src/hardware_proxy/)
 * is allowed to include infrastructure/drivers/uart.h or mention USART/
 * UART Registers/IRQ - every Management/Infrastructure above this
 * (including EC200U Manager) must go through this header instead.
 *
 * Today EC200U Manager is the ONLY consumer of this Domain - the UART
 * channel to the EC200U module. Non-Blocking, byte-stream oriented (no
 * line/frame concept here - AT Command framing, response matching, URC
 * detection all belong to EC200U Manager per ec200u-manager.md section 8:
 * "UART Driver فقط ارسال Byte/دریافت Byte/Buffer/DMA/Interrupt/Hardware
 * Error را مدیریت می‌کند"). */

typedef enum {
    HWPROXY_UART_OK,
    HWPROXY_UART_BUSY,  /* a previous Send() has not finished transmitting yet */
    HWPROXY_UART_ERROR
} HWProxy_UartResult;

void HWProxy_UART_Init(void);

/* Non-Blocking: hands `length` bytes to the transmit path and returns
 * immediately - HWPROXY_UART_BUSY if the previous Send() is still in
 * flight (caller must retry later, same convention as EC200U_Result's own
 * BUSY), HWPROXY_UART_ERROR for a NULL data pointer or zero length,
 * HWPROXY_UART_OK once accepted (not a guarantee of on-the-wire delivery,
 * same "accepted for transmission" semantics as every EC200U Capability
 * function above this layer). `data` is copied/consumed immediately - no
 * ownership kept over the caller's buffer afterward. */
HWProxy_UartResult HWProxy_UART_Send(const uint8_t* data, uint32_t length);

/* Non-Blocking pop of whatever bytes have arrived since the last call -
 * copies up to `buffer_size` bytes into the caller-owned `buffer` and
 * returns how many were actually copied (0 if nothing has arrived, never
 * blocks waiting for more). Same "copy into caller buffer, return count"
 * convention infrastructure/drivers/uart.h's own UART_Read() already used. */
uint32_t HWProxy_UART_Read(uint8_t* buffer, uint32_t buffer_size);

#endif /* HARDWARE_PROXY_UART_H */
