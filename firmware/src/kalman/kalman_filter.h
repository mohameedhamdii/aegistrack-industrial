/**
 * @file kalman_filter.h
 * @brief Generic 4-State Linear Kalman Filter
 *
 * Implements a standard linear Kalman filter with a 4-element state
 * vector and 2-element measurement vector. Designed for position +
 * velocity estimation in 2D.
 *
 * State vector:  x = [lat, lon, v_lat, v_lon]
 * Measurement:   z = [lat, lon]
 */

#ifndef KALMAN_FILTER_H
#define KALMAN_FILTER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KF_STATE_DIM  4   /**< State dimension: lat, lon, v_lat, v_lon */
#define KF_MEAS_DIM   2   /**< Measurement dimension: lat, lon         */

/**
 * @brief 4×4 matrix type (row-major).
 */
typedef struct {
    float m[KF_STATE_DIM][KF_STATE_DIM];
} kf_mat4_t;

/**
 * @brief 4×2 matrix type (row-major).
 */
typedef struct {
    float m[KF_STATE_DIM][KF_MEAS_DIM];
} kf_mat4x2_t;

/**
 * @brief 2×4 matrix type (row-major).
 */
typedef struct {
    float m[KF_MEAS_DIM][KF_STATE_DIM];
} kf_mat2x4_t;

/**
 * @brief 2×2 matrix type (row-major).
 */
typedef struct {
    float m[KF_MEAS_DIM][KF_MEAS_DIM];
} kf_mat2_t;

/**
 * @brief 4-element state vector.
 */
typedef struct {
    float v[KF_STATE_DIM];
} kf_vec4_t;

/**
 * @brief 2-element measurement vector.
 */
typedef struct {
    float v[KF_MEAS_DIM];
} kf_vec2_t;

/**
 * @brief Kalman Filter instance.
 */
typedef struct {
    kf_vec4_t x;    /**< State estimate: [lat, lon, v_lat, v_lon] */
    kf_mat4_t P;    /**< Error covariance matrix (4×4)            */
    kf_mat4_t Q;    /**< Process noise covariance (4×4)           */
    kf_mat2_t R;    /**< Measurement noise covariance (2×2)       */
    kf_mat4_t F;    /**< State transition matrix (4×4)            */
    kf_mat2x4_t H;  /**< Measurement matrix (2×4)                */
    bool initialized;
} kalman_filter_t;

/* ──────────────────────────── Public API ──────────────────────────── */

/**
 * @brief Initialize the Kalman filter with initial state and covariances.
 * @param kf          KF instance.
 * @param initial_lat Initial latitude (degrees).
 * @param initial_lon Initial longitude (degrees).
 * @param q_pos       Process noise for position (degrees²).
 * @param q_vel       Process noise for velocity (degrees/s)².
 * @param r_pos       Measurement noise for position (degrees²).
 */
void kf_init(kalman_filter_t *kf,
             float initial_lat, float initial_lon,
             float q_pos, float q_vel,
             float r_pos);

/**
 * @brief Run the predict step.
 *        Updates state estimate using the constant-velocity model.
 * @param kf  KF instance.
 * @param dt  Time step in seconds since last update.
 */
void kf_predict(kalman_filter_t *kf, float dt);

/**
 * @brief Run the update step with a new GPS measurement.
 * @param kf       KF instance.
 * @param meas_lat Measured latitude (degrees).
 * @param meas_lon Measured longitude (degrees).
 */
void kf_update(kalman_filter_t *kf, float meas_lat, float meas_lon);

/**
 * @brief Get the current estimated position.
 * @param kf   KF instance.
 * @param lat  Output: estimated latitude (degrees).
 * @param lon  Output: estimated longitude (degrees).
 */
void kf_get_position(const kalman_filter_t *kf, float *lat, float *lon);

/**
 * @brief Get the current estimated velocity.
 * @param kf    KF instance.
 * @param v_lat Output: latitude velocity (degrees/s).
 * @param v_lon Output: longitude velocity (degrees/s).
 */
void kf_get_velocity(const kalman_filter_t *kf, float *v_lat, float *v_lon);

/**
 * @brief Get the current speed in m/s.
 * @param kf  KF instance.
 * @param lat Current latitude for degree-to-meter conversion.
 * @return Speed in m/s.
 */
float kf_get_speed_ms(const kalman_filter_t *kf, float lat);

/**
 * @brief Get the innovation (measurement residual) magnitude.
 *        Used for anomaly detection — large innovations indicate
 *        GPS spoofing or multipath errors.
 * @param kf       KF instance.
 * @param meas_lat Measured latitude.
 * @param meas_lon Measured longitude.
 * @return Innovation magnitude in degrees.
 */
float kf_get_innovation(const kalman_filter_t *kf, float meas_lat, float meas_lon);

/**
 * @brief Reset the filter to initial state.
 * @param kf       KF instance.
 * @param lat      New initial latitude.
 * @param lon      New initial longitude.
 */
void kf_reset(kalman_filter_t *kf, float lat, float lon);

#ifdef __cplusplus
}
#endif

#endif /* KALMAN_FILTER_H */
