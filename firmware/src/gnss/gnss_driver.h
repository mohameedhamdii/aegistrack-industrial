/**
 * @file gnss_driver.h
 * @brief u-blox MAX-M10S GNSS Driver (UBX Binary Protocol)
 *
 * Communicates with the u-blox GNSS module over UART using the binary
 * UBX protocol for efficient fix acquisition. Supports hot/warm/cold
 * start strategies and ephemeris caching for fast TTFF.
 */

#ifndef GNSS_DRIVER_H
#define GNSS_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ──────────────────────────── Fix Status ──────────────────────────── */

typedef enum {
    GNSS_FIX_NONE    = 0,  /**< No fix                */
    GNSS_FIX_DEAD_RECK = 1,/**< Dead reckoning only   */
    GNSS_FIX_2D      = 2,  /**< 2D fix                */
    GNSS_FIX_3D      = 3,  /**< 3D fix                */
    GNSS_FIX_COMBINED = 4, /**< GNSS + dead reckoning */
    GNSS_FIX_TIME_ONLY = 5 /**< Time-only fix         */
} gnss_fix_status_t;

/* ──────────────────────────── Fix Data ──────────────────────────── */

typedef struct {
    gnss_fix_status_t fix_type;

    /* Position (WGS84) */
    int32_t  latitude;     /**< Latitude in degrees × 1e-7  */
    int32_t  longitude;    /**< Longitude in degrees × 1e-7 */
    int32_t  altitude_mm;  /**< Altitude above MSL in mm    */

    /* Accuracy */
    uint32_t h_acc_mm;     /**< Horizontal accuracy in mm   */
    uint32_t v_acc_mm;     /**< Vertical accuracy in mm     */
    uint16_t pdop;         /**< Position DOP × 100          */

    /* Velocity */
    int32_t  vel_n_mm_s;   /**< North velocity in mm/s      */
    int32_t  vel_e_mm_s;   /**< East velocity in mm/s       */
    int32_t  vel_d_mm_s;   /**< Down velocity in mm/s       */
    uint32_t ground_speed; /**< Ground speed in mm/s        */
    int32_t  heading;      /**< Heading of motion × 1e-5 deg*/

    /* Time (UTC) */
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  min;
    uint8_t  sec;
    uint8_t  valid_time;   /**< Bit flags: bit0=validDate, bit1=validTime */

    /* Satellites */
    uint8_t  num_sv;       /**< Number of satellites used    */

    /* Epoch timestamp */
    uint32_t epoch;        /**< Unix timestamp of fix       */
} gnss_fix_t;

/* ──────────────────────────── Public API ──────────────────────────── */

/**
 * @brief Initialize the GNSS driver.
 *        Configures UART, sends initial UBX configuration.
 * @return 0 on success, -1 on failure.
 */
int gnss_init(void);

/**
 * @brief Start a GPS fix acquisition.
 *        Powers on GNSS (if not already) and begins fix process.
 *        Uses hot start if ephemeris is valid, else cold start.
 * @return 0 on success, -1 on failure.
 */
int gnss_start_fix(void);

/**
 * @brief Poll for fix completion. Non-blocking.
 * @return Current fix status.
 */
gnss_fix_status_t gnss_get_fix_status(void);

/**
 * @brief Get the latest fix data.
 * @param fix  Output: filled with fix data if available.
 * @return 0 if valid fix available, -1 if no fix.
 */
int gnss_get_fix(gnss_fix_t *fix);

/**
 * @brief Process incoming UART data from GNSS module.
 *        Should be called from UART RX interrupt or DMA callback.
 * @param data  Pointer to received bytes.
 * @param len   Number of bytes received.
 */
void gnss_process_rx(const uint8_t *data, uint16_t len);

/**
 * @brief Power down the GNSS module and save ephemeris data.
 */
void gnss_shutdown(void);

/**
 * @brief Inject AssistNow Offline data for faster TTFF.
 * @param data  Pointer to ephemeris data.
 * @param len   Length in bytes.
 * @return 0 on success, -1 on failure.
 */
int gnss_inject_ephemeris(const uint8_t *data, uint32_t len);

/**
 * @brief Check if cached ephemeris is still valid.
 * @return true if valid, false if expired.
 */
bool gnss_ephemeris_valid(void);

/**
 * @brief Convert fix data to floating-point latitude.
 */
static inline double gnss_lat_deg(const gnss_fix_t *fix) {
    return (double)fix->latitude * 1e-7;
}

/**
 * @brief Convert fix data to floating-point longitude.
 */
static inline double gnss_lon_deg(const gnss_fix_t *fix) {
    return (double)fix->longitude * 1e-7;
}

#ifdef __cplusplus
}
#endif

#endif /* GNSS_DRIVER_H */
