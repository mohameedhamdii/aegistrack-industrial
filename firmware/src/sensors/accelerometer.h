/**
 * @file accelerometer.h
 * @brief LIS2DW12 Ultra-Low-Power Accelerometer Driver
 *
 * Controls the LIS2DW12 3-axis accelerometer for wake-on-motion
 * detection. Operates at 1 µA in low-power mode with hardware
 * interrupt output.
 */

#ifndef ACCELEROMETER_H
#define ACCELEROMETER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the accelerometer via SPI/I2C.
 * @return 0 on success, -1 on failure.
 */
int accelerometer_init(void);

/**
 * @brief Check if the accelerometer detects motion above threshold.
 * @param threshold_mg  Threshold in milli-g.
 * @return true if motion detected, false otherwise.
 */
bool accelerometer_is_active(uint16_t threshold_mg);

/**
 * @brief Set the wake-up threshold.
 * @param threshold_mg  Threshold in milli-g.
 */
void accelerometer_set_threshold(uint16_t threshold_mg);

/**
 * @brief Enable or disable the INT1 interrupt for wake-on-motion.
 * @param enable  true to enable, false to disable.
 */
void accelerometer_enable_interrupt(bool enable);

/**
 * @brief Read raw acceleration data.
 * @param x  Output: X-axis acceleration in mg.
 * @param y  Output: Y-axis acceleration in mg.
 * @param z  Output: Z-axis acceleration in mg.
 * @return 0 on success, -1 on failure.
 */
int accelerometer_read(int16_t *x, int16_t *y, int16_t *z);

/**
 * @brief Get the device ID for verification (should be 0x44 for LIS2DW12).
 * @return Device ID, or -1 on communication failure.
 */
int accelerometer_who_am_i(void);

#ifdef __cplusplus
}
#endif

#endif /* ACCELEROMETER_H */
