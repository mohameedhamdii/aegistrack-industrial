/**
 * @file main.c
 * @brief Asset Tracker Firmware — Main Entry Point
 *
 * Orchestrates the entire tracker lifecycle:
 *   1. System initialization (clocks, peripherals)
 *   2. Check wake source (RTC alarm vs. accelerometer INT vs. reset)
 *   3. Run power state machine
 *   4. Acquire GPS fix → Kalman filter → Geofence check → Transmit
 *   5. Return to deep sleep
 *
 * MCU: STM32L4R5ZI
 * Battery: Tadiran TL-5930 3.6V 19Ah
 */

#include "stm32l4xx_hal.h"

#include "power/power_manager.h"
#include "power/sleep.h"
#include "power/peripheral_switch.h"
#include "gnss/gnss_driver.h"
#include "kalman/asset_tracker_kf.h"
#include "kalman/eta_predictor.h"
#include "geofence/geofence_engine.h"
#include "comms/nbiot_driver.h"
#include "comms/packet.h"
#include "comms/downlink.h"
#include "sensors/accelerometer.h"
#include "sensors/temperature.h"
#include "config/comms_config.h"
#include "config/power_config.h"
#include "config/pin_map.h"

#include <string.h>

/* ──────────────────────────── Global Handles ──────────────────────────── */

RTC_HandleTypeDef  hrtc;
SPI_HandleTypeDef  hspi1;
ADC_HandleTypeDef  hadc1;

/* ──────────────────────────── Module Instances ──────────────────────── */

static asset_tracker_kf_t s_tracker_kf;
static gnss_fix_t         s_current_fix;
static bool               s_kf_initialized = false;

/* ──────────────────────────── System Clock ──────────────────────────── */

/**
 * @brief Configure system clock to 80 MHz via MSI + PLL.
 *        Uses MSI at 4 MHz as PLL source.
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /* MSI 4 MHz → PLL → 80 MHz */
    osc.OscillatorType = RCC_OSCILLATORTYPE_MSI;
    osc.MSIState       = RCC_MSI_ON;
    osc.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    osc.MSIClockRange  = RCC_MSIRANGE_6;  /* 4 MHz */
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_MSI;
    osc.PLL.PLLM       = 1;
    osc.PLL.PLLN       = 40;
    osc.PLL.PLLP       = RCC_PLLP_DIV7;
    osc.PLL.PLLQ       = RCC_PLLQ_DIV2;
    osc.PLL.PLLR       = RCC_PLLR_DIV2;
    HAL_RCC_OscConfig(&osc);

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider   = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider  = RCC_HCLK_DIV1;
    clk.APB2CLKDivider  = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4);
}

/* ──────────────────────────── System Epoch ──────────────────────────── */

/**
 * @brief Get current UTC epoch from RTC.
 */
uint32_t system_get_epoch(void)
{
    RTC_TimeTypeDef time;
    RTC_DateTypeDef date;
    HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN);

    /* Simplified epoch calculation (accurate for 2000-2099) */
    uint32_t year = 2000 + date.Year;
    uint32_t days = 0;

    /* Days in complete years since 1970 */
    for (uint32_t y = 1970; y < year; y++) {
        days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    }

    /* Days in complete months of current year */
    static const uint16_t month_days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    for (uint8_t m = 1; m < date.Month; m++) {
        days += month_days[m];
        if (m == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
            days++;
        }
    }
    days += date.Date - 1;

    return days * 86400 + time.Hours * 3600 + time.Minutes * 60 + time.Seconds;
}

/* ──────────────────────────── Battery Voltage ──────────────────────── */

static uint16_t read_battery_mv(void)
{
    ADC_ChannelConfTypeDef ch = {0};
    ch.Channel      = VBAT_ADC_CHANNEL;
    ch.Rank         = ADC_REGULAR_RANK_1;
    ch.SamplingTime = ADC_SAMPLETIME_247CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc1, &ch);

    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 100);
    uint32_t raw = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    /* Assuming 1:1 voltage divider and 3.3V reference:
     * V_bat = raw * 3300 * 2 / 4096 */
    return (uint16_t)(raw * 6600 / 4096);
}

/* ──────────────────────────── Wake Source Detection ──────────────────── */

static wake_source_t detect_wake_source(void)
{
    if (sleep_woke_from_standby()) {
        /* Check if RTC wake-up flag is set */
        if (__HAL_RTC_WAKEUPTIMER_GET_FLAG(&hrtc, RTC_FLAG_WUTF)) {
            sleep_clear_wakeup_flags();
            return WAKE_SOURCE_RTC;
        }
        /* Otherwise, accelerometer INT woke us */
        sleep_clear_wakeup_flags();
        return WAKE_SOURCE_ACCEL_INT;
    }

    /* Cold boot or reset — treat as RTC wake */
    return WAKE_SOURCE_RTC;
}

/* ──────────────────────────── Tracker Main Loop ──────────────────────── */

/**
 * @brief Execute one complete tracker cycle:
 *   GPS fix → KF → Geofence → Packet → TX → Sleep
 */
static void tracker_cycle(wake_source_t wake_src)
{
    uint32_t epoch = system_get_epoch();

    /* ── Step 1: Acquire GPS fix ── */
    periph_gnss_power(true);
    gnss_init();
    gnss_start_fix();

    uint32_t fix_start = HAL_GetTick();
    bool fix_acquired = false;

    while ((HAL_GetTick() - fix_start) < (DEFAULT_GNSS_TIMEOUT_S * 1000U)) {
        if (gnss_get_fix_status() >= GNSS_FIX_2D) {
            gnss_get_fix(&s_current_fix);
            fix_acquired = true;
            break;
        }
        HAL_Delay(100);
    }

    gnss_shutdown();
    periph_gnss_power(false);

    /* Update epoch from GPS time if fix acquired */
    if (fix_acquired && s_current_fix.valid_time) {
        epoch = s_current_fix.epoch;
    }

    /* ── Step 2: Kalman Filter ── */
    uint8_t anomaly_flags = 0;

    if (fix_acquired) {
        float lat = gnss_lat_deg(&s_current_fix);
        float lon = gnss_lon_deg(&s_current_fix);
        float hdop = (float)s_current_fix.pdop / 100.0f;

        if (!s_kf_initialized) {
            tracker_kf_init(&s_tracker_kf, ASSET_TYPE_CONTAINER, lat, lon);
            s_kf_initialized = true;
        }

        /* Update KF with stationary status from accelerometer */
        bool motion_active = accelerometer_is_active(DEFAULT_ACCEL_THRESHOLD_MG);
        tracker_kf_set_stationary(&s_tracker_kf, !motion_active);

        anomaly_flags = tracker_kf_process_fix(&s_tracker_kf, lat, lon, epoch, hdop);

        power_manager_notify_fix(epoch);
    }

    /* ── Step 3: Geofence Check ── */
    geofence_result_t geo_results[GEOFENCE_MAX_ZONES];
    int num_geo_events = 0;
    uint8_t geo_event = 0;
    uint8_t geo_zone = 0;

    if (fix_acquired) {
        num_geo_events = geofence_check(
            s_current_fix.latitude, s_current_fix.longitude, epoch,
            geo_results, GEOFENCE_MAX_ZONES);

        if (num_geo_events > 0) {
            geo_event = (uint8_t)geo_results[0].event;
            geo_zone  = geo_results[0].zone_id;
        }
    }

    /* ── Step 4: Compute ETA ── */
    uint32_t eta_minutes = UINT32_MAX;
    if (fix_acquired && s_kf_initialized) {
        float kf_lat, kf_lon;
        tracker_kf_get_position(&s_tracker_kf, &kf_lat, &kf_lon);
        float speed = tracker_kf_get_speed(&s_tracker_kf);

        float dst_lat, dst_lon;
        if (geofence_get_nearest_centroid(s_current_fix.latitude,
                                          s_current_fix.longitude,
                                          &dst_lat, &dst_lon) == 0) {
            eta_minutes = eta_compute_minutes(kf_lat, kf_lon,
                                              dst_lat, dst_lon, speed);
        }
    }

    /* ── Step 5: Build and transmit packet ── */
    tracker_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    strncpy(pkt.tracker_id, TRACKER_ID, sizeof(pkt.tracker_id) - 1);
    pkt.timestamp      = epoch;
    pkt.latitude       = fix_acquired ? s_current_fix.latitude : 0;
    pkt.longitude      = fix_acquired ? s_current_fix.longitude : 0;
    pkt.altitude_mm    = fix_acquired ? s_current_fix.altitude_mm : 0;
    pkt.speed_mm_s     = fix_acquired ? s_current_fix.ground_speed : 0;
    pkt.heading_1e5    = fix_acquired ? s_current_fix.heading : 0;
    pkt.battery_mv     = read_battery_mv();
    pkt.temperature    = temperature_read_celsius();
    pkt.fix_type       = fix_acquired ? (uint8_t)s_current_fix.fix_type : 0;
    pkt.hdop_100       = fix_acquired ? s_current_fix.pdop : 0;
    pkt.geofence_event = geo_event;
    pkt.geofence_zone  = geo_zone;
    pkt.anomaly_flags  = anomaly_flags;
    pkt.eta_minutes    = (eta_minutes != UINT32_MAX) ? eta_minutes : 0;
    pkt.sequence       = packet_next_sequence();

    /* Encode to CBOR */
    uint8_t cbor_buf[PACKET_MAX_SIZE];
    int cbor_len = packet_encode_cbor(&pkt, cbor_buf, sizeof(cbor_buf));

    if (cbor_len > 0) {
        /* Power on modem and transmit */
        periph_modem_power(true);

        nbiot_config_t modem_cfg = {
            .apn           = DEFAULT_APN,
            .server_ip     = DEFAULT_SERVER_IP,
            .server_port   = DEFAULT_SERVER_PORT,
            .reg_timeout_ms = DEFAULT_REG_TIMEOUT_MS,
            .tx_timeout_ms  = DEFAULT_TX_TIMEOUT_MS,
        };
        nbiot_init(&modem_cfg);

        if (nbiot_connect() == 0) {
            nbiot_send(cbor_buf, (uint16_t)cbor_len);

            /* Check for downlink commands */
            uint8_t dl_buf[256];
            int dl_len = nbiot_receive(dl_buf, sizeof(dl_buf));
            if (dl_len > 0) {
                downlink_cmd_t dl_cmd;
                if (downlink_parse(dl_buf, (uint16_t)dl_len, &dl_cmd) == 0) {
                    downlink_process(&dl_cmd);
                }
            }

            power_manager_notify_tx(true, epoch);
        } else {
            power_manager_notify_tx(false, epoch);
        }

        nbiot_enter_psm();
        periph_modem_power(false);
    }

    /* ── Step 6: Return to sleep ── */
    const power_status_t *status = power_manager_get_status();
    if (status->motion_confirmed) {
        sleep_enter_stop2(DEFAULT_MOVING_INTERVAL_S);
    } else {
        sleep_enter_standby(DEFAULT_STATIONARY_INTERVAL_S);
    }
}

/* ──────────────────────────── Main Entry ──────────────────────────── */

int main(void)
{
    /* HAL initialization */
    HAL_Init();
    SystemClock_Config();

    /* Initialize RTC for wake-up alarms */
    sleep_rtc_init();

    /* Detect what woke us */
    wake_source_t wake_src = detect_wake_source();

    /* Initialize power manager */
    power_manager_init(NULL);

    /* Initialize geofence engine (loads zones from flash) */
    geofence_engine_init();

    /* Initialize temperature sensor */
    temperature_init();

    /* Run the tracker cycle */
    tracker_cycle(wake_src);

    /* Should never reach here — sleep functions either:
     * - Enter Standby (reset on wake) — execution restarts from main()
     * - Enter Stop 2 (resume after WFI) — but we loop back anyway
     */
    while (1) {
        tracker_cycle(WAKE_SOURCE_RTC);
    }
}

/* ──────────────────────────── IRQ Handlers ──────────────────────────── */

/**
 * @brief EXTI0 interrupt — accelerometer INT1 wake-on-motion.
 */
void EXTI0_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(ACCEL_INT_PIN);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == ACCEL_INT_PIN) {
        power_manager_notify_motion(true);
    }
}

/**
 * @brief RTC wake-up timer interrupt.
 */
void RTC_WKUP_IRQHandler(void)
{
    HAL_RTCEx_WakeUpTimerIRQHandler(&hrtc);
}

/**
 * @brief SysTick handler.
 */
void SysTick_Handler(void)
{
    HAL_IncTick();
}
