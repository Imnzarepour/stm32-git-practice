#ifndef HARDWARE_PROXY_ANALOG_IO_H
#define HARDWARE_PROXY_ANALOG_IO_H
#include <stdint.h>

/* Contract source: docs/03-infrastructure-contracts/hardware-proxy.md
 * See hardware_proxy_digital_io.h for the full rationale of this layer
 * and why it is split into one file per peripheral domain.
 */

typedef int HWProxy_AnalogChannel; /* Logical channel number; physical ADC channel mapping is hidden inside the implementation */

/* One-time setup so that later reads are a plain non-blocking register
 * read (see HWProxy_ReadAnalogChannel below) - never call this more than
 * once. */
void HWProxy_AnalogIo_Init(void);

/* Replaces ADC_Read for every consumer above this layer. Output is still
 * raw; converting it to an engineering value (e.g. millivolts) stays the
 * responsibility of the consuming Management (e.g. Hardware Input).
 *
 * Must never start a new conversion / block waiting for one - it only
 * reads whatever the latest already-completed sample is. Acquisition
 * happens once, in HWProxy_AnalogIo_Init(), not on every Get - same rule
 * as the Digital I/O side (see driver-layer.md section 17: "Get API
 * never triggers Acquisition"). */
uint16_t HWProxy_ReadAnalogChannel(HWProxy_AnalogChannel channel);

#endif /* HARDWARE_PROXY_ANALOG_IO_H */
