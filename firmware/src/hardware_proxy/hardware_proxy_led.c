#include "hardware_proxy/hardware_proxy_led.h"
#include "infrastructure/drivers/led.h"

/* This is one of the only files in the whole firmware allowed to include
 * infrastructure/drivers headers and mention LED/Buzzer - see the header
 * for the rule. Pure pass-through with type translation, no business
 * logic (which status maps to which LED_State) - exactly like
 * hardware_proxy_digital_io.c. */

static LED_Id to_driver_id(HWProxy_LedId id) {
    switch (id) {
        case HWPROXY_LED_GSM:   return LED_GSM;
        case HWPROXY_LED_GPS:   return LED_GPS;
        case HWPROXY_LED_SIM:   return LED_SIM;
        case HWPROXY_LED_POWER: return LED_POWER;
        default:                 return LED_POWER;
    }
}

static LED_State to_driver_state(HWProxy_LedState state) {
    switch (state) {
        case HWPROXY_LED_OFF:        return LED_OFF;
        case HWPROXY_LED_ON:         return LED_ON;
        case HWPROXY_LED_BLINK_SLOW: return LED_BLINK_SLOW;
        case HWPROXY_LED_BLINK_FAST: return LED_BLINK_FAST;
        default:                      return LED_OFF;
    }
}

void HWProxy_SetLed(HWProxy_LedId id, HWProxy_LedState state) {
    LED_Set(to_driver_id(id), to_driver_state(state));
}

void HWProxy_Led_Process(void) {
    /* Nothing to do on the host stub - LED_Set() itself is already a no-op,
     * so there is no real pin state to keep toggling. Only
     * hardware_proxy_led_stm32f4.c does real work here. */
}

void HWProxy_SetBuzzer(bool on) {
    Buzzer_Set(on);
}
