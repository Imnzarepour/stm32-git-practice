#include "hardware_proxy/hardware_proxy_analog_io.h"
#include "main.h"

/* Real STM32F4 implementation of the Hardware Proxy analog I/O contract.
 *
 * Only compiled by the MCU_F4 CubeIDE project, never by the host build -
 * same reasoning as hardware_proxy_digital_io_stm32f4.c (HAL types only
 * exist for the ARM toolchain). CMakeLists.txt excludes every
 * "*_stm32f4.c" file from the host build; hardware_proxy_analog_io.c
 * (the host-stub) is used there instead.
 *
 * `hadc1` is generated and configured by CubeMX in Core/Src/main.c
 * (MX_ADC1_Init): ADC1, channel 0, pin PA0/BATTERY_VOLTAGE, 12-bit
 * resolution, single rank. CubeMX generates it with Continuous
 * Conversion Mode disabled (one-shot); we turn it into a free-running
 * conversion once, here, so that every later read is a plain register
 * read (HAL_ADC_GetValue) of the latest completed sample - never a
 * blocking wait for a new one. This is what keeps HWProxy_ReadAnalogChannel
 * non-blocking and compliant with the "Get never triggers Acquisition"
 * rule, matching how the digital side treats GPIO reads as fast
 * register reads (driver-layer.md section 17).
 *
 * PA1 (ADC1_IN1) is also wired to analog mode by CubeMX (see
 * HAL_ADC_MspInit) for a possible future 2nd channel (INTERNAL_BATTERY
 * per pins name.txt), but no Management uses it yet, so it is left
 * unconfigured here - same "reserved but dormant" treatment as
 * reserve_input on the digital side. */

extern ADC_HandleTypeDef hadc1;

void HWProxy_AnalogIo_Init(void) {
    hadc1.Init.ContinuousConvMode = ENABLE;
    HAL_ADC_Init(&hadc1);
    HAL_ADC_Start(&hadc1);
}

uint16_t HWProxy_ReadAnalogChannel(HWProxy_AnalogChannel channel) {
    if (channel != 0) {
        /* Only channel 0 (BATTERY_VOLTAGE) is wired for continuous
         * conversion today. */
        return 0;
    }

    return (uint16_t)HAL_ADC_GetValue(&hadc1);
}
