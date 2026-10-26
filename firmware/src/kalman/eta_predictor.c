/**
 * @file eta_predictor.c
 * @brief ETA Prediction Implementation
 */

#include "eta_predictor.h"
#include <math.h>

#define EARTH_RADIUS_M  6371000.0f
#define DEG_TO_RAD      0.01745329f
#define MIN_SPEED_MS    0.1f  /* Minimum speed to compute ETA */

float eta_haversine_m(float lat1, float lon1, float lat2, float lon2)
{
    float dlat = (lat2 - lat1) * DEG_TO_RAD;
    float dlon = (lon2 - lon1) * DEG_TO_RAD;
    float la1  = lat1 * DEG_TO_RAD;
    float la2  = lat2 * DEG_TO_RAD;

    float a = sinf(dlat * 0.5f) * sinf(dlat * 0.5f) +
              cosf(la1) * cosf(la2) *
              sinf(dlon * 0.5f) * sinf(dlon * 0.5f);

    float c = 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));

    return EARTH_RADIUS_M * c;
}

uint32_t eta_compute_minutes(float cur_lat, float cur_lon,
                              float dst_lat, float dst_lon,
                              float speed_ms)
{
    if (speed_ms < MIN_SPEED_MS) {
        return UINT32_MAX;  /* Not moving — ETA undefined */
    }

    float distance_m = eta_haversine_m(cur_lat, cur_lon, dst_lat, dst_lon);

    if (distance_m < 10.0f) {
        return 0;  /* Already at destination */
    }

    float eta_seconds = distance_m / speed_ms;
    uint32_t eta_minutes = (uint32_t)(eta_seconds / 60.0f);

    /* Cap at ~68 years to prevent overflow */
    if (eta_minutes > 35791394U) {
        return UINT32_MAX;
    }

    return eta_minutes;
}
