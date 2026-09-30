#ifndef HARDWARE_PROXY_LED_H
#define HARDWARE_PROXY_LED_H
#include <stdbool.h>

/* Contract source: docs/03-infrastructure-contracts/hardware-proxy.md
 * See hardware_proxy_digital_io.h for the full rationale of this layer.
 *
 * Domain: local indicator hardware (status LEDs + Buzzer) - System
 * Management's own Driver dependency per
 * management-infrastructure-dependency.md §5 ("System | ... | Watchdog/LED").
 * Only firmware/src/hardware_proxy/hardware_proxy_led.c may include
 * infrastructure/drivers/led.h; System Management (or anything else above
 * this layer) must go through this header instead. The enum values below
 * are this layer's own type, not led.h's LED_Id/LED_State - Management
 * never sees the Driver's own type, same convention HWProxy_DigitalLine
 * already uses for GPIO_Pin. */

typedef enum {
    HWPROXY_LED_GSM,
    HWPROXY_LED_GPS,
    HWPROXY_LED_SIM,
    HWPROXY_LED_POWER
} HWProxy_LedId;

typedef enum {
    HWPROXY_LED_OFF,
    HWPROXY_LED_ON,
    HWPROXY_LED_BLINK_SLOW,
    HWPROXY_LED_BLINK_FAST
} HWProxy_LedState;

/* Replaces LED_Set for every consumer above this layer. No business logic
 * here (same rule as every other Hardware Proxy domain) - deciding which
 * state a given LED should show is entirely System Management's job. */
void HWProxy_SetLed(HWProxy_LedId id, HWProxy_LedState state);

/* Advances Blink timing for any LED currently showing HWPROXY_LED_BLINK_SLOW/
 * FAST - must be called once per Super Loop lap (same Non-Blocking,
 * Time-Base-driven pattern every other periodic behavior in this firmware
 * already uses, e.g. hardware_input_management.c's debounce/EMA/Telemetry
 * cadence - this is not a new architectural shape). A single
 * HWProxy_SetLed() write cannot make a raw GPIO pin visibly blink by
 * itself; this function is what actually toggles the pin at a fixed rate
 * while a Blink state is active. System Management (the only caller today)
 * invokes this right alongside its own LED decision in System_Process().
 * The host-stub implementation is a no-op (HWProxy_SetLed() itself already
 * does nothing there); only the real *_stm32f4.c implementation does work
 * here - see hardware_proxy_led_stm32f4.c. */
void HWProxy_Led_Process(void);

/* Replaces Buzzer_Set for every consumer above this layer. Exposed for
 * completeness (System Management owns this Driver dependency too), but
 * nothing calls it yet - see the top-of-file note in system_management.c
 * for why (no trigger condition defined anywhere in the Contract). */
void HWProxy_SetBuzzer(bool on);

#endif /* HARDWARE_PROXY_LED_H */
