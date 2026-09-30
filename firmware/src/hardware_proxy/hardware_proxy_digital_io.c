#include "hardware_proxy/hardware_proxy_digital_io.h"
#include "infrastructure/drivers/gpio.h"

/* This is one of the only files in the whole firmware allowed to include
 * infrastructure/drivers headers and mention GPIO - see the header for
 * the rule. No business logic (debounce, qualification, policy) lives
 * here; this is pure pass-through with type/result translation, exactly
 * like Driver Layer itself. */

HWProxy_Result HWProxy_DigitalIo_Init(void) {
    return (GPIO_Init() == GPIO_OK) ? HWPROXY_OK : HWPROXY_ERROR;
}

HWProxy_Result HWProxy_ReadDigitalLine(HWProxy_DigitalLine line, bool* value) {
    switch (GPIO_Read((GPIO_Pin)line, value)) {
        case GPIO_OK:      return HWPROXY_OK;
        case GPIO_INVALID: return HWPROXY_INVALID;
        default:           return HWPROXY_ERROR;
    }
}

HWProxy_Result HWProxy_WriteDigitalLine(HWProxy_DigitalLine line, bool value) {
    switch (GPIO_Write((GPIO_Pin)line, value)) {
        case GPIO_OK:      return HWPROXY_OK;
        case GPIO_INVALID: return HWPROXY_INVALID;
        default:           return HWPROXY_ERROR;
    }
}
