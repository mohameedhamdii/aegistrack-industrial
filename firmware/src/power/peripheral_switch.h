/**
 * @file peripheral_switch.h
 * @brief GPIO-controlled Peripheral Power Switching
 *
 * Controls load switches (TPS22918) that power-gate the GNSS module
 * and NB-IoT modem. Each peripheral has a dedicated GPIO that drives
 * the load switch EN pin.
 */

#ifndef PERIPHERAL_SWITCH_H
#define PERIPHERAL_SWITCH_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize all peripheral power switch GPIOs.
 *        Sets all switches to OFF (peripherals unpowered).
 */
void periph_init(void);

/**
 * @brief Control GNSS module power via load switch.
 * @param on  true = power ON, false = power OFF
 */
void periph_gnss_power(bool on);

/**
 * @brief Control NB-IoT modem power via load switch.
 * @param on  true = power ON, false = power OFF
 */
void periph_modem_power(bool on);

/**
 * @brief Check if GNSS module is currently powered.
 * @return true if powered, false otherwise.
 */
bool periph_gnss_is_on(void);

/**
 * @brief Check if NB-IoT modem is currently powered.
 * @return true if powered, false otherwise.
 */
bool periph_modem_is_on(void);

/**
 * @brief Power off all peripherals (emergency shutdown).
 */
void periph_all_off(void);

#ifdef __cplusplus
}
#endif

#endif /* PERIPHERAL_SWITCH_H */
