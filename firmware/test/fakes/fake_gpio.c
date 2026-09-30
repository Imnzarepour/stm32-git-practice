#include "infrastructure/drivers/gpio.h"

/* Fake GPIO برای Unit Test - طبق اصل Fake/Stub در driver-layer.md §23 */
static bool s_pin_state[64];

GPIO_Result GPIO_Init(void) { return GPIO_OK; }
GPIO_Result GPIO_Read(GPIO_Pin pin, bool* value) {
    if (pin < 0 || pin >= 64 || !value) return GPIO_INVALID;
    *value = s_pin_state[pin];
    return GPIO_OK;
}
GPIO_Result GPIO_Write(GPIO_Pin pin, bool value) {
    if (pin < 0 || pin >= 64) return GPIO_INVALID;
    s_pin_state[pin] = value;
    return GPIO_OK;
}

/* برای تست: مقدار پین را از بیرون ست کن (شبیه‌سازی سخت‌افزار) */
void FakeGpio_SetPin(GPIO_Pin pin, bool value) {
    if (pin >= 0 && pin < 64) s_pin_state[pin] = value;
}
