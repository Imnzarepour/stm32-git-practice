#include "infrastructure/drivers/gpio.h"
/* STUB - وابسته به MCU واقعی. تیم مسئول باید Register mapping را پر کند. */
GPIO_Result GPIO_Init(void) { return GPIO_OK; }
GPIO_Result GPIO_Read(GPIO_Pin pin, bool* value) { (void)pin; if (value) *value = false; return GPIO_OK; }
GPIO_Result GPIO_Write(GPIO_Pin pin, bool value) { (void)pin; (void)value; return GPIO_OK; }
