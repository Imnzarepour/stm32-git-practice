#ifndef DRIVER_ADC_H
#define DRIVER_ADC_H
#include <stdint.h>
/* Contract source: docs/03-infrastructure-contracts/driver-layer.md §6
 * خروجی Raw است؛ تبدیل به Engineering Value مسئولیت Hardware Input Management است. */
typedef int ADC_Channel;
uint16_t ADC_Read(ADC_Channel channel);
#endif
