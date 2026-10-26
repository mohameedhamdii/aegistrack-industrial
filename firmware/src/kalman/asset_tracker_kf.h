/**
 * @file asset_tracker_kf.h
 * @brief Asset-Tracking-Specific Kalman Filter Wrapper
 *
 * Wraps the generic KF with asset-tracking-specific logic:
 * - Automatic parameter tuning for containers vs. livestock
 * - Anomaly detection (GPS spoofing, multipath, implausible speed)
 * - Stationary detection from accelerometer for KF velocity reset
 */

#ifndef ASSET_TRACKER_KF_H
#define ASSET_TRACKER_KF_H

#include "kalman_filter.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ──────────────────────────── Asset Types ──────────────────────────── */

typedef enum {
    ASSET_TYPE_CONTAINER = 0,  /**< Shipping container — slow, predictable */
    ASSET_TYPE_LIVESTOCK = 1,  /**< Animal — faster, less predictable      */
} asset_type_t;

/* ──────────────────────────── Anomaly Flags ──────────────────────────── */

#define ANOMALY_NONE           0x00
#define ANOMALY_INNOVATION     0x01  /**< Innovation > 5× expected       */
#define ANOMALY_SPEED          0x02  /**< Speed exceeds plausible max    */
#define ANOMALY_JUMP           0x04  /**< Position jump > threshold      */

/* ──────────────────────────── Tracker KF State ──────────────────────── */

typedef struct {
    kalman_filter_t kf;
    asset_type_t    asset_type;
    uint32_t        last_update_epoch;
    float           max_speed_ms;        /**< Max plausible speed for this asset */
    float           innovation_threshold; /**< Innovation threshold for anomaly   */
    uint8_t         anomaly_flags;
    bool            is_stationary;
    uint32_t        fix_count;
} asset_tracker_kf_t;

/* ──────────────────────────── Public API ──────────────────────────── */

/**
 * @brief Initialize the asset tracker Kalman filter.
 * @param tkf         Tracker KF instance.
 * @param asset_type  Type of asset being tracked.
 * @param initial_lat Initial latitude (degrees).
 * @param initial_lon Initial longitude (degrees).
 */
void tracker_kf_init(asset_tracker_kf_t *tkf, asset_type_t asset_type,
                     float initial_lat, float initial_lon);

/**
 * @brief Process a new GPS fix through the Kalman filter.
 * @param tkf       Tracker KF instance.
 * @param lat       Measured latitude (degrees).
 * @param lon       Measured longitude (degrees).
 * @param epoch     UTC epoch of the fix.
 * @param hdop      Horizontal DOP from GPS.
 * @return Anomaly flags (0 = normal).
 */
uint8_t tracker_kf_process_fix(asset_tracker_kf_t *tkf,
                                float lat, float lon,
                                uint32_t epoch, float hdop);

/**
 * @brief Notify the tracker KF that the asset is stationary.
 *        Resets velocity to zero to prevent KF divergence.
 * @param tkf         Tracker KF instance.
 * @param stationary  true if stationary (from accelerometer).
 */
void tracker_kf_set_stationary(asset_tracker_kf_t *tkf, bool stationary);

/**
 * @brief Get the filtered position.
 * @param tkf  Tracker KF instance.
 * @param lat  Output: filtered latitude.
 * @param lon  Output: filtered longitude.
 */
void tracker_kf_get_position(const asset_tracker_kf_t *tkf,
                              float *lat, float *lon);

/**
 * @brief Get the estimated speed in m/s.
 */
float tracker_kf_get_speed(const asset_tracker_kf_t *tkf);

/**
 * @brief Get the estimated heading in degrees (0-360).
 */
float tracker_kf_get_heading(const asset_tracker_kf_t *tkf);

/**
 * @brief Get the current anomaly flags.
 */
uint8_t tracker_kf_get_anomaly_flags(const asset_tracker_kf_t *tkf);

#ifdef __cplusplus
}
#endif

#endif /* ASSET_TRACKER_KF_H */
