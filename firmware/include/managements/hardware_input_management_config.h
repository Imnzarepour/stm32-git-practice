#ifndef MGMT_HARDWARE_INPUT_CONFIG_H
#define MGMT_HARDWARE_INPUT_CONFIG_H

/* Every tunable constant for Hardware Input Management lives in this one
 * file, per explicit request, so nobody has to go hunting through
 * hardware_input_management.c to find a timing/gain/threshold value.
 * None of these are part of the Contract
 * (docs/02-management-contracts/hardware-input-management.md) - the
 * Contract explicitly calls debounce/filter/gain details "this
 * Management's internal design". Every value here is a placeholder
 * pending real hardware calibration; changing one never requires
 * touching the Management's logic, only this file. */

/* --- Digital debounce (ms) - see process_digital_input() --- */
#define ACC_DEBOUNCE_MS   100u
#define DOOR_DEBOUNCE_MS  100u
#define SHOCK_DEBOUNCE_MS  50u

/* --- Analog raw -> voltage conversion --- */
#define ADC_MAX_RAW       4095u /* Assumes 12-bit ADC resolution */
#define ADC_REF_MILLIVOLT 3300u /* Assumes Vref = 3.3V */

/* Gain applied AFTER raw-to-pin-voltage conversion (represents whatever
 * resistor divider/attenuation sits between the real signal and the ADC
 * pin). Expected to change repeatedly during the project as the board
 * is finalized - that is exactly why each channel gets its own named
 * constant instead of one shared value. */
#define BATTERY_VOLTAGE_GAIN          4u /* Vehicle/main battery */
#define INTERNAL_BATTERY_VOLTAGE_GAIN 2u /* Secondary channel - may be removed later */

/* --- Noise filtering (Exponential Moving Average) ---
 * filtered = filtered + (sample - filtered) / 2^BATTERY_EMA_SHIFT
 *
 * This shift is the "how many samples worth of averaging" knob: an EMA
 * with shift N behaves roughly like a moving average over 2^N samples
 * (shift 3 => ~8 samples), but without needing a sample buffer - every
 * Process() cycle does one cheap, non-blocking update, so the smoothed
 * value is always already up to date. There is no separate "wait and
 * collect N samples, then compute" step: by the time a periodic
 * Telemetry push (see below) is due, the already-current filtered value
 * is simply read and sent - never computed from scratch at that moment. */
#define BATTERY_EMA_SHIFT 3u

/* --- Low battery detection (main vehicle battery channel only) --- */
#define LOW_BATTERY_THRESHOLD_MV  11000u
#define LOW_BATTERY_HYSTERESIS_MV   500u /* Recovers back to normal at 11.5V */

/* --- Telemetry cadence ---
 * How often the current (already-filtered) reading is sent to the
 * Telemetry Queue. Each analog channel has its own independent
 * interval. NOTE: the Telemetry payload format itself is still a system-
 * wide open item (OPEN-ITEMS.md, red, #3); only the cadence is decided
 * here. */
#define BATTERY_VOLTAGE_TELEMETRY_INTERVAL_MS          30000u
#define INTERNAL_BATTERY_VOLTAGE_TELEMETRY_INTERVAL_MS 30000u

/* --- Power Mode behavior ---
 * In LOW_POWER, the Telemetry cadence for analog channels is slowed
 * down by this multiplier (e.g. 30s becomes 120s) to save power/
 * communication cost. Digital inputs (ACC/Door/Shock) are NOT affected
 * by Power Mode - per hardware-input-management.md section 11,
 * safety/wake-up-relevant inputs must stay fully monitored regardless
 * of Power Mode. */
#define LOW_POWER_TELEMETRY_RATE_MULTIPLIER 4u

#endif /* MGMT_HARDWARE_INPUT_CONFIG_H */
