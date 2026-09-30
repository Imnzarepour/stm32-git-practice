#include "hardware_proxy/hardware_proxy_uart.h"
#include "main.h"
#include <string.h>

/* Real STM32F4 implementation of the Hardware Proxy UART contract - the
 * physical link to the EC200U module (USART2, PD5=TX/PD6=RX, CubeMX
 * MX_USART2_UART_Init(), 115200 8N1, MCU_F4/ConnectedCar.ioc).
 *
 * Only compiled by the MCU_F4 CubeIDE project, never by the host build -
 * same reasoning as hardware_proxy_analog_io_stm32f4.c (HAL types only
 * exist for the ARM toolchain). CMakeLists.txt excludes every
 * "*_stm32f4.c" file from the host build; hardware_proxy_uart.c (the
 * host-stub) is used there instead.
 *
 * `huart2` is generated and configured by CubeMX in Core/Src/main.c
 * (MX_USART2_UART_Init). CubeMX did NOT enable the USART2 global
 * interrupt in the .ioc (no USART2_IRQHandler exists in
 * Core/Src/stm32f4xx_it.c), so this file is fully self-contained instead
 * of relying on any CubeMX-generated interrupt wiring: HWProxy_UART_Init()
 * below enables the NVIC line itself, and USART2_IRQHandler()/the HAL
 * callbacks are all defined right here. CMSIS's startup file declares
 * USART2_IRQHandler as a `.weak` alias of Default_Handler (Core/Startup/
 * startup_stm32f407vgtx.s) - providing a strong definition in this file
 * overrides it at link time without touching any CubeMX-generated file,
 * same "reconfigure what CubeMX set up, from inside Hardware Proxy only"
 * precedent hardware_proxy_analog_io_stm32f4.c already established for
 * hadc1's Continuous Conversion Mode.
 *
 * RX: interrupt-driven, one byte at a time (HAL_UART_Receive_IT
 * re-armed from HAL_UART_RxCpltCallback), pushed into a classic
 * single-producer (ISR)/single-consumer (HWProxy_UART_Read(), called only
 * from the Super Loop) ring buffer - safe without disabling interrupts
 * because head is only ever written by the ISR and tail only ever written
 * by the consumer (docs/03-infrastructure-contracts/driver-layer.md
 * section 8's "Buffer/Interrupt" responsibility, now satisfied for real).
 * TX: interrupt-driven (HAL_UART_Transmit_IT), one transfer in flight at a
 * time - HWProxy_UART_Send() reports HWPROXY_UART_BUSY instead of queuing
 * a second one, matching this Domain's own Non-Blocking Contract exactly
 * (Super Loop callers retry on a later Process() tick, never block here). */

#define HWPROXY_UART_RX_RING_BYTES 512u
#define HWPROXY_UART_TX_BUFFER_BYTES 512u

extern UART_HandleTypeDef huart2;

static volatile uint8_t  s_rx_ring[HWPROXY_UART_RX_RING_BYTES];
static volatile uint32_t s_rx_head; /* next free slot - written only by the ISR */
static volatile uint32_t s_rx_tail; /* next slot to read - written only by HWProxy_UART_Read() */
static uint8_t           s_rx_isr_byte; /* single-byte landing zone HAL_UART_Receive_IT DMAs/copies each received byte into */

static uint8_t           s_tx_buffer[HWPROXY_UART_TX_BUFFER_BYTES];
static volatile bool     s_tx_busy;

static void rx_ring_push(uint8_t byte) {
    uint32_t next_head = (s_rx_head + 1u) % HWPROXY_UART_RX_RING_BYTES;
    if (next_head == s_rx_tail) {
        return; /* ring full - drop the newest byte rather than corrupt the buffer; a stuck/flooding link is EC200U Manager's own Timeout/Recovery to notice, not this layer's to solve */
    }
    s_rx_ring[s_rx_head] = byte;
    s_rx_head = next_head;
}

void HWProxy_UART_Init(void) {
    s_rx_head  = 0u;
    s_rx_tail  = 0u;
    s_tx_busy  = false;

    HAL_NVIC_SetPriority(USART2_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);

    HAL_UART_Receive_IT(&huart2, &s_rx_isr_byte, 1u);
}

HWProxy_UartResult HWProxy_UART_Send(const uint8_t* data, uint32_t length) {
    if (data == NULL || length == 0u) {
        return HWPROXY_UART_ERROR;
    }
    if (s_tx_busy) {
        return HWPROXY_UART_BUSY;
    }
    if (length > sizeof(s_tx_buffer)) {
        return HWPROXY_UART_ERROR; /* cannot safely send a partial AT command/payload */
    }

    memcpy(s_tx_buffer, data, length);
    s_tx_busy = true;
    if (HAL_UART_Transmit_IT(&huart2, s_tx_buffer, length) != HAL_OK) {
        s_tx_busy = false;
        return HWPROXY_UART_ERROR;
    }
    return HWPROXY_UART_OK;
}

uint32_t HWProxy_UART_Read(uint8_t* buffer, uint32_t buffer_size) {
    uint32_t copied = 0u;
    if (buffer == NULL || buffer_size == 0u) {
        return 0u;
    }
    while (copied < buffer_size && s_rx_tail != s_rx_head) {
        buffer[copied] = s_rx_ring[s_rx_tail];
        s_rx_tail = (s_rx_tail + 1u) % HWPROXY_UART_RX_RING_BYTES;
        copied++;
    }
    return copied;
}

/* --- HAL callbacks / IRQ handler - see this file's own top comment for
 * why these are defined here instead of in CubeMX-generated files. --- */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart) {
    if (huart->Instance == USART2) {
        rx_ring_push(s_rx_isr_byte);
        HAL_UART_Receive_IT(&huart2, &s_rx_isr_byte, 1u); /* re-arm for the next byte - HAL_UART_Receive_IT is one-shot per call */
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart) {
    if (huart->Instance == USART2) {
        s_tx_busy = false;
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart) {
    if (huart->Instance == USART2) {
        /* Overrun/Framing/Noise error - HAL already clears the error flags
         * internally before invoking this callback; both directions must
         * be kept alive regardless (a stuck RX path would silently starve
         * every AT response EC200U Manager is waiting on). */
        s_tx_busy = false;
        HAL_UART_Receive_IT(&huart2, &s_rx_isr_byte, 1u);
    }
}

void USART2_IRQHandler(void) {
    HAL_UART_IRQHandler(&huart2);
}
