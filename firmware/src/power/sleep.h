/**
 * @file sleep.h
 * @brief STM32L4 Low-Power Sleep Mode Wrappers
 *
 * Provides clean wrappers around STM32L4 low-power modes:
 *   - Stop 2 mode: ~1.3 µA, all SRAM retained, fast wake (~5 µs)
 *   - Standby mode: ~0.6 µA, only backup SRAM retained, RTC wake
 */

#ifndef SLEEP_H
#define SLEEP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enter Stop 2 mode with RTC wake alarm.
 *        Used for moving mode — faster wake, SRAM retained.
 * @param seconds  Number of seconds until RTC alarm wakes the MCU.
 */
void sleep_enter_stop2(uint32_t seconds);

/**
 * @brief Enter Standby mode with RTC wake alarm.
 *        Deepest sleep — only backup domain retained.
 *        Used for stationary mode — maximum power savings.
 * @param seconds  Number of seconds until RTC alarm wakes the MCU.
 */
void sleep_enter_standby(uint32_t seconds);

/**
 * @brief Configure the RTC for low-power wake-up alarms.
 *        Must be called once during initialization.
 */
void sleep_rtc_init(void);

/**
 * @brief Check if the MCU woke from Standby mode.
 * @return 1 if woke from Standby, 0 otherwise.
 */
int sleep_woke_from_standby(void);

/**
 * @brief Clear all wake-up flags after waking.
 */
void sleep_clear_wakeup_flags(void);

#ifdef __cplusplus
}
#endif

#endif /* SLEEP_H */
