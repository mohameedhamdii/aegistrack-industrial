/**
 * @file power_manager.h
 * @brief Power Management State Machine for Asset Tracker
 *
 * Controls all power states and peripheral power gating.
 * This is the most critical module — it determines whether the device
 * achieves multi-year battery life.
 *
 * Operating modes:
 *   STANDBY          — Default between fixes, RTC + accel only (~1.6 µA)
 *   MOTION_DETECT    — Accel INT fired, MCU confirms motion (~200 µA, 10 ms)
 *   GNSS_ACQUIRE     — Scheduled fix or motion confirmed (5–12 mA)
 *   TRANSMIT         — After fix acquired, NB-IoT TX (230 mA peak)
 *   STANDBY_MOVING   — Object in motion, frequent fixes (every 15 min)
 */

#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ──────────────────────────── Power States ──────────────────────────── */

typedef enum {
    PWR_STATE_STANDBY         = 0,  /**< Deep sleep, RTC wake every 6 h     */
    PWR_STATE_MOTION_DETECT   = 1,  /**< Brief wake to confirm motion       */
    PWR_STATE_GNSS_ACQUIRE    = 2,  /**< GPS fix acquisition                */
    PWR_STATE_TRANSMIT        = 3,  /**< Cellular/satellite transmission    */
    PWR_STATE_STANDBY_MOVING  = 4,  /**< Moving — faster fix rate (15 min)  */
} power_state_t;

/* ──────────────────────────── Wake Sources ──────────────────────────── */

typedef enum {
    WAKE_SOURCE_RTC           = 0,  /**< RTC alarm (scheduled fix)          */
    WAKE_SOURCE_ACCEL_INT     = 1,  /**< Accelerometer interrupt            */
    WAKE_SOURCE_DOWNLINK_CMD  = 2,  /**< Remote command to request fix      */
} wake_source_t;

/* ──────────────────────────── Configuration ──────────────────────────── */

typedef struct {
    uint32_t stationary_interval_s;   /**< Fix interval when stationary (default 21600 = 6h) */
    uint32_t moving_interval_s;       /**< Fix interval when moving (default 900 = 15min)    */
    uint32_t motion_confirm_ms;       /**< Duration to confirm sustained motion (3000 ms)    */
    uint32_t motion_timeout_s;        /**< No motion for this long → stationary (300 = 5min) */
    uint16_t accel_threshold_mg;      /**< Accelerometer threshold in mg (default 50)        */
    uint16_t gnss_timeout_s;          /**< Max time to wait for GPS fix (60 s)               */
    uint8_t  nbiot_max_retries;       /**< NB-IoT registration retries before satellite (3)  */
} power_config_t;

/* ──────────────────────────── Runtime Status ──────────────────────────── */

typedef struct {
    power_state_t current_state;
    power_state_t previous_state;
    wake_source_t last_wake_source;
    uint32_t      state_enter_tick;    /**< Tick count when current state was entered  */
    uint32_t      last_fix_epoch;      /**< UTC epoch of last successful GPS fix       */
    uint32_t      last_tx_epoch;       /**< UTC epoch of last successful transmission  */
    uint32_t      motion_start_tick;   /**< Tick when motion was first detected         */
    uint16_t      consecutive_tx_fail; /**< Consecutive NB-IoT TX failures             */
    bool          motion_confirmed;    /**< True if sustained motion confirmed          */
    bool          fix_pending;         /**< True if a GPS fix is currently in progress  */
    bool          tx_pending;          /**< True if a transmission is queued            */
} power_status_t;

/* ──────────────────────────── Public API ──────────────────────────── */

/**
 * @brief Initialize the power manager with the given configuration.
 * @param cfg  Pointer to power configuration. NULL uses defaults.
 */
void power_manager_init(const power_config_t *cfg);

/**
 * @brief Run one iteration of the power state machine.
 *        Call this in the main loop after waking from sleep.
 * @param wake_src  The source that woke the MCU.
 */
void power_manager_run(wake_source_t wake_src);

/**
 * @brief Get the current power manager status.
 * @return Pointer to the internal status structure (read-only).
 */
const power_status_t* power_manager_get_status(void);

/**
 * @brief Get the current power state.
 * @return Current power state enum value.
 */
power_state_t power_manager_get_state(void);

/**
 * @brief Force a state transition (e.g., from a remote command).
 * @param new_state  The target state.
 */
void power_manager_force_state(power_state_t new_state);

/**
 * @brief Notify the power manager that a GPS fix was obtained.
 * @param epoch  UTC epoch time of the fix.
 */
void power_manager_notify_fix(uint32_t epoch);

/**
 * @brief Notify the power manager that a transmission completed.
 * @param success  True if TX succeeded, false otherwise.
 * @param epoch    UTC epoch time of the TX.
 */
void power_manager_notify_tx(bool success, uint32_t epoch);

/**
 * @brief Notify the power manager of accelerometer activity.
 * @param active  True if accelerometer detected motion above threshold.
 */
void power_manager_notify_motion(bool active);

/**
 * @brief Get the default power configuration.
 * @param cfg  Output: filled with default values.
 */
void power_manager_get_defaults(power_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* POWER_MANAGER_H */
