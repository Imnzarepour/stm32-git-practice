#ifndef HARDWARE_PROXY_DIGITAL_IO_H
#define HARDWARE_PROXY_DIGITAL_IO_H
#include <stdint.h>
#include <stdbool.h>

/* Contract source: docs/03-infrastructure-contracts/hardware-proxy.md
 *
 * Core rule of this layer (explicit user decision, not a draft):
 *
 *   Only the Hardware Proxy implementation (files under
 *   firmware/src/hardware_proxy/) is allowed to include headers from
 *   infrastructure/drivers/ or mention GPIO/Pin/Register in its code.
 *   Every Management (and every other Infrastructure piece) must go
 *   through this header instead.
 *
 * Hardware Proxy is a peer of Infrastructure and Managements, not a
 * sub-folder of Infrastructure - it sits directly above Driver Layer
 * (docs/03-infrastructure-contracts/driver-layer.md), which still owns
 * the raw register access underneath it, unchanged.
 *
 * Why this layer exists: when each Management included its own driver
 * header directly (gpio.h, adc.h, ...), unit-testing that Management
 * required faking/mocking that same driver, and because the real driver
 * implementation was also linked into the project, the test build hit a
 * linker "multiple definition" error. Centralizing all hardware access
 * behind one proxy header per peripheral domain means each Management's
 * test only mocks the domain(s) it actually touches - nothing else.
 *
 * Why this is split into "_digital_io" / "_analog_io" / future "_uart",
 * "_can", "_interrupt" files instead of one big file: so that a test
 * for a Management that only needs digital I/O (e.g. Hardware Input)
 * never has to know about the UART or CAN mock, and adding a new
 * peripheral domain tomorrow never touches this file.
 */

typedef int HWProxy_DigitalLine; /* Logical line number; physical pin mapping is hidden inside the implementation */

typedef enum {
    HWPROXY_OK,
    HWPROXY_ERROR,
    HWPROXY_INVALID
} HWProxy_Result;

HWProxy_Result HWProxy_DigitalIo_Init(void);

/* Replaces GPIO_Read for every consumer above this layer */
HWProxy_Result HWProxy_ReadDigitalLine(HWProxy_DigitalLine line, bool* value);

/* Replaces GPIO_Write for every consumer above this layer */
HWProxy_Result HWProxy_WriteDigitalLine(HWProxy_DigitalLine line, bool value);

#endif /* HARDWARE_PROXY_DIGITAL_IO_H */
