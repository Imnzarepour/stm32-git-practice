#ifndef DRIVER_GPIO_H
#define DRIVER_GPIO_H
#include <stdbool.h>
/* Contract source: docs/03-infrastructure-contracts/driver-layer.md §5
 * قانون: Driver هیچ Debounce/Qualification/Event نمی‌سازد؛ فقط Register خام. */
typedef enum { GPIO_OK, GPIO_ERROR, GPIO_INVALID } GPIO_Result;
typedef int GPIO_Pin; /* TODO: Pin mapping واقعی در Board Config تعریف شود */

GPIO_Result GPIO_Init(void);
GPIO_Result GPIO_Read(GPIO_Pin pin, bool* value);
GPIO_Result GPIO_Write(GPIO_Pin pin, bool value);

#endif
