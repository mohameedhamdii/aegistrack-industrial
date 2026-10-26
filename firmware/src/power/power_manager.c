/**
 * @file power_manager.c
 * @brief Power Management State Machine Implementation
 *
 * Implements the core power state machine that orchestrates all
 * peripheral power gating and duty cycling to achieve multi-year
 * battery life on a single lithium thionyl chloride cell.
 */

#include "power_manager.h"
#include "sleep.h"
#include "peripheral_switch.h"
#include "../sensors/accelerometer.h"
#include "../gnss/gnss_driver.h"
#include "../comms/nbiot_driver.h"
#include "stm32l4xx_hal.h"

#include <string.h>

/* ──────────────────────────── Private State ──────────────────────────── */

static power_config_t s_config;
static power_status_t s_status;
static uint32_t       s_motion_last_active_tick;

/* ──────────────────────────── Defaults ──────────────────────────── */

static const power_config_t DEFAULT_CONFIG = {
    .stationary_interval_s  = 21600,  /* 6 hours  */
    .moving_interval_s      = 900,    /* 15 min   */
    .motion_confirm_ms      = 3000,   /* 3 s      */
    .motion_timeout_s       = 300,    /* 5 min    */
    .accel_threshold_mg     = 50,     /* 50 mg    */
    .gnss_timeout_s         = 60,     /* 60 s     */
    .nbiot_max_retries      = 3,
};

/* ──────────────────────────── State Transition Helpers ──────────────── */

static void enter_state(power_state_t new_state)
{
    s_status.previous_state   = s_status.current_state;
    s_status.current_state    = new_state;
    s_status.state_enter_tick = HAL_GetTick();
}

static uint32_t time_in_state_ms(void)
{
    return HAL_GetTick() - s_status.state_enter_tick;
}

/* ──────────────────────────── State Handlers ──────────────────────────── */

/**
 * STANDBY state handler.
 * Device is in deep sleep, only RTC and accelerometer are active.
 * Wakes on RTC alarm (scheduled fix) or accelerometer interrupt.
 */
static void handle_standby(wake_source_t wake_src)
{
    switch (wake_src) {
    case WAKE_SOURCE_RTC:
        /* Scheduled fix time — power on GNSS */
        periph_gnss_power(true);
        enter_state(PWR_STATE_GNSS_ACQUIRE);
        break;

    case WAKE_SOURCE_ACCEL_INT:
        /* Accelerometer fired — need to confirm sustained motion */
        enter_state(PWR_STATE_MOTION_DETECT);
        break;

    case WAKE_SOURCE_DOWNLINK_CMD:
        /* Remote command to acquire fix immediately */
        periph_gnss_power(true);
        enter_state(PWR_STATE_GNSS_ACQUIRE);
        break;
    }
}

/**
 * MOTION_DETECT state handler.
 * MCU wakes briefly to confirm sustained motion (>3 s above threshold).
 */
static void handle_motion_detect(wake_source_t wake_src)
{
    (void)wake_src;

    /* Read accelerometer for motion confirmation */
    bool motion_active = accelerometer_is_active(s_config.accel_threshold_mg);

    if (motion_active) {
        if (!s_status.motion_confirmed) {
            /* First detection — record start time */
            if (s_status.motion_start_tick == 0) {
                s_status.motion_start_tick = HAL_GetTick();
            }

            /* Check if motion has been sustained long enough */
            uint32_t duration = HAL_GetTick() - s_status.motion_start_tick;
            if (duration >= s_config.motion_confirm_ms) {
                s_status.motion_confirmed = true;
                s_motion_last_active_tick = HAL_GetTick();

                /* Transition to moving standby — more frequent fixes */
                enter_state(PWR_STATE_STANDBY_MOVING);

                /* Immediately start a GPS fix */
                periph_gnss_power(true);
                enter_state(PWR_STATE_GNSS_ACQUIRE);
            }
            /* else: keep sampling, stay in MOTION_DETECT */
        }
    } else {
        /* Motion stopped before confirmation — go back to sleep */
        s_status.motion_start_tick = 0;
        enter_state(PWR_STATE_STANDBY);
        sleep_enter_standby(s_config.stationary_interval_s);
    }
}

/**
 * GNSS_ACQUIRE state handler.
 * GPS module is powered on, acquiring a fix.
 */
static void handle_gnss_acquire(wake_source_t wake_src)
{
    (void)wake_src;

    gnss_fix_status_t fix_status = gnss_get_fix_status();

    if (fix_status == GNSS_FIX_3D || fix_status == GNSS_FIX_2D) {
        /* Fix acquired — store it and move to transmit */
        periph_gnss_power(false);
        s_status.fix_pending = false;

        /* Power on modem and transmit */
        periph_modem_power(true);
        enter_state(PWR_STATE_TRANSMIT);

    } else if (time_in_state_ms() >= (s_config.gnss_timeout_s * 1000U)) {
        /* Timeout — no fix. Power down GNSS, still transmit "no fix" packet */
        periph_gnss_power(false);
        s_status.fix_pending = false;

        periph_modem_power(true);
        enter_state(PWR_STATE_TRANSMIT);

    }
    /* else: still acquiring, stay in this state */
}

/**
 * TRANSMIT state handler.
 * NB-IoT modem is powered on, sending data packet.
 */
static void handle_transmit(wake_source_t wake_src)
{
    (void)wake_src;

    nbiot_tx_status_t tx_status = nbiot_get_tx_status();

    if (tx_status == NBIOT_TX_SUCCESS) {
        /* Transmission succeeded */
        s_status.tx_pending = false;
        s_status.consecutive_tx_fail = 0;
        periph_modem_power(false);

        /* Return to appropriate standby mode */
        if (s_status.motion_confirmed) {
            enter_state(PWR_STATE_STANDBY_MOVING);
            sleep_enter_stop2(s_config.moving_interval_s);
        } else {
            enter_state(PWR_STATE_STANDBY);
            sleep_enter_standby(s_config.stationary_interval_s);
        }

    } else if (tx_status == NBIOT_TX_FAILED) {
        /* Transmission failed */
        s_status.consecutive_tx_fail++;
        periph_modem_power(false);

        if (s_status.consecutive_tx_fail >= s_config.nbiot_max_retries) {
            /* Too many failures — try satellite if available, else sleep */
            /* TODO: satellite fallback */
            s_status.consecutive_tx_fail = 0;
        }

        /* Return to sleep regardless */
        if (s_status.motion_confirmed) {
            enter_state(PWR_STATE_STANDBY_MOVING);
            sleep_enter_stop2(s_config.moving_interval_s);
        } else {
            enter_state(PWR_STATE_STANDBY);
            sleep_enter_standby(s_config.stationary_interval_s);
        }

    } else if (time_in_state_ms() >= 30000U) {
        /* TX timeout (30 s) — abort */
        s_status.consecutive_tx_fail++;
        periph_modem_power(false);

        if (s_status.motion_confirmed) {
            enter_state(PWR_STATE_STANDBY_MOVING);
            sleep_enter_stop2(s_config.moving_interval_s);
        } else {
            enter_state(PWR_STATE_STANDBY);
            sleep_enter_standby(s_config.stationary_interval_s);
        }
    }
    /* else: still transmitting, stay in this state */
}

/**
 * STANDBY_MOVING state handler.
 * Object is in motion — more frequent fixes (every 15 min).
 * Check if motion has stopped.
 */
static void handle_standby_moving(wake_source_t wake_src)
{
    /* Check if motion has stopped */
    bool motion_active = accelerometer_is_active(s_config.accel_threshold_mg);

    if (motion_active) {
        s_motion_last_active_tick = HAL_GetTick();
    }

    uint32_t idle_time = HAL_GetTick() - s_motion_last_active_tick;
    if (idle_time >= (s_config.motion_timeout_s * 1000U)) {
        /* Motion stopped for 5 minutes — return to stationary mode */
        s_status.motion_confirmed  = false;
        s_status.motion_start_tick = 0;
        enter_state(PWR_STATE_STANDBY);
        sleep_enter_standby(s_config.stationary_interval_s);
        return;
    }

    /* Still moving — handle wake source */
    switch (wake_src) {
    case WAKE_SOURCE_RTC:
        /* Scheduled moving fix */
        periph_gnss_power(true);
        enter_state(PWR_STATE_GNSS_ACQUIRE);
        break;

    case WAKE_SOURCE_ACCEL_INT:
        /* Already in moving mode — just update activity timestamp */
        s_motion_last_active_tick = HAL_GetTick();
        sleep_enter_stop2(s_config.moving_interval_s);
        break;

    case WAKE_SOURCE_DOWNLINK_CMD:
        periph_gnss_power(true);
        enter_state(PWR_STATE_GNSS_ACQUIRE);
        break;
    }
}

/* ──────────────────────────── Public API ──────────────────────────── */

void power_manager_init(const power_config_t *cfg)
{
    if (cfg != NULL) {
        memcpy(&s_config, cfg, sizeof(power_config_t));
    } else {
        memcpy(&s_config, &DEFAULT_CONFIG, sizeof(power_config_t));
    }

    memset(&s_status, 0, sizeof(power_status_t));
    s_status.current_state  = PWR_STATE_STANDBY;
    s_status.previous_state = PWR_STATE_STANDBY;

    /* Initialize peripheral switches — all off */
    periph_init();
    periph_gnss_power(false);
    periph_modem_power(false);

    /* Configure accelerometer for wake-on-motion */
    accelerometer_init();
    accelerometer_set_threshold(s_config.accel_threshold_mg);
    accelerometer_enable_interrupt(true);
}

void power_manager_run(wake_source_t wake_src)
{
    s_status.last_wake_source = wake_src;

    switch (s_status.current_state) {
    case PWR_STATE_STANDBY:
        handle_standby(wake_src);
        break;
    case PWR_STATE_MOTION_DETECT:
        handle_motion_detect(wake_src);
        break;
    case PWR_STATE_GNSS_ACQUIRE:
        handle_gnss_acquire(wake_src);
        break;
    case PWR_STATE_TRANSMIT:
        handle_transmit(wake_src);
        break;
    case PWR_STATE_STANDBY_MOVING:
        handle_standby_moving(wake_src);
        break;
    }
}

const power_status_t* power_manager_get_status(void)
{
    return &s_status;
}

power_state_t power_manager_get_state(void)
{
    return s_status.current_state;
}

void power_manager_force_state(power_state_t new_state)
{
    enter_state(new_state);
}

void power_manager_notify_fix(uint32_t epoch)
{
    s_status.last_fix_epoch = epoch;
    s_status.fix_pending    = false;
}

void power_manager_notify_tx(bool success, uint32_t epoch)
{
    s_status.last_tx_epoch = epoch;
    s_status.tx_pending    = false;
    if (success) {
        s_status.consecutive_tx_fail = 0;
    } else {
        s_status.consecutive_tx_fail++;
    }
}

void power_manager_notify_motion(bool active)
{
    if (active) {
        s_motion_last_active_tick = HAL_GetTick();
    }
}

void power_manager_get_defaults(power_config_t *cfg)
{
    if (cfg != NULL) {
        memcpy(cfg, &DEFAULT_CONFIG, sizeof(power_config_t));
    }
}
