#include "infrastructure/drivers/adc.h"

/* Fake ADC برای Unit Test - طبق اصل Fake/Stub در driver-layer.md §23 (مشابه fake_gpio.c) */
static uint16_t s_channel_value[8];

uint16_t ADC_Read(ADC_Channel channel) {
    if (channel < 0 || channel >= 8) return 0;
    return s_channel_value[channel];
}

/* برای تست: مقدار Raw یک Channel را از بیرون ست کن (شبیه‌سازی سخت‌افزار) */
void FakeAdc_SetChannel(ADC_Channel channel, uint16_t value) {
    if (channel >= 0 && channel < 8) s_channel_value[channel] = value;
}
