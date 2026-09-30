#include "hardware_proxy/hardware_proxy_analog_io.h"
#include "infrastructure/drivers/adc.h"

/* This is one of the only files in the whole firmware allowed to include
 * infrastructure/drivers headers and mention ADC - see the header for
 * the rule. Pure pass-through, no conversion/filtering here; that stays
 * in the consuming Management. */

void HWProxy_AnalogIo_Init(void) {
    /* nothing to do on the host stub */
}

uint16_t HWProxy_ReadAnalogChannel(HWProxy_AnalogChannel channel) {
    return ADC_Read((ADC_Channel)channel);
}
