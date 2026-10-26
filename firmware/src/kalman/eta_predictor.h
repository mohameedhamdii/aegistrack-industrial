/**
 * @file eta_predictor.h
 * @brief ETA Prediction Module
 *
 * Computes estimated time of arrival (ETA) to a destination geofence
 * centroid using the Kalman filter's velocity estimate.
 */

#ifndef ETA_PREDICTOR_H
#define ETA_PREDICTOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compute ETA in minutes to a destination point.
 * @param cur_lat   Current latitude (degrees).
 * @param cur_lon   Current longitude (degrees).
 * @param dst_lat   Destination latitude (degrees).
 * @param dst_lon   Destination longitude (degrees).
 * @param speed_ms  Current speed in m/s.
 * @return ETA in minutes, or UINT32_MAX if speed is zero / unreachable.
 */
uint32_t eta_compute_minutes(float cur_lat, float cur_lon,
                              float dst_lat, float dst_lon,
                              float speed_ms);

/**
 * @brief Compute haversine distance between two points in meters.
 * @param lat1  Point 1 latitude (degrees).
 * @param lon1  Point 1 longitude (degrees).
 * @param lat2  Point 2 latitude (degrees).
 * @param lon2  Point 2 longitude (degrees).
 * @return Distance in meters.
 */
float eta_haversine_m(float lat1, float lon1, float lat2, float lon2);

#ifdef __cplusplus
}
#endif

#endif /* ETA_PREDICTOR_H */
