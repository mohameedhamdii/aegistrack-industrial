/**
 * @file asset_tracker_kf.c
 * @brief Asset-Tracking Kalman Filter Wrapper Implementation
 */

#include "asset_tracker_kf.h"
#include <math.h>
#include <string.h>

/* ──────────────────────────── Constants ──────────────────────────── */

/* Container: max ~80 km/h on a truck, typically much slower */
#define CONTAINER_MAX_SPEED_MS   55.0f   /* ~200 km/h (allow for highway) */
#define CONTAINER_Q_POS          1e-10f  /* Low process noise              */
#define CONTAINER_Q_VEL          1e-8f   /* Low velocity noise             */

/* Livestock: max ~15 m/s (running horse), typically walking */
#define LIVESTOCK_MAX_SPEED_MS   20.0f   /* ~72 km/h (galloping horse)     */
#define LIVESTOCK_Q_POS          1e-9f   /* Higher process noise           */
#define LIVESTOCK_Q_VEL          1e-7f   /* Higher velocity noise          */

/* GPS accuracy: ~3m CEP → R ≈ (3m / 111320 m/deg)² */
#define BASE_R_POS               7.26e-10f  /* (3/111320)² deg²            */

/* Anomaly thresholds */
#define INNOVATION_MULTIPLIER    5.0f
#define POSITION_JUMP_DEG        0.01f   /* ~1.1 km                       */

#define DEG_TO_RAD  0.01745329f
#define DEG_TO_M    111320.0f

/* ──────────────────────────── Public API ──────────────────────────── */

void tracker_kf_init(asset_tracker_kf_t *tkf, asset_type_t asset_type,
                     float initial_lat, float initial_lon)
{
    memset(tkf, 0, sizeof(asset_tracker_kf_t));
    tkf->asset_type = asset_type;

    float q_pos, q_vel;

    switch (asset_type) {
    case ASSET_TYPE_CONTAINER:
        q_pos = CONTAINER_Q_POS;
        q_vel = CONTAINER_Q_VEL;
        tkf->max_speed_ms = CONTAINER_MAX_SPEED_MS;
        break;
    case ASSET_TYPE_LIVESTOCK:
        q_pos = LIVESTOCK_Q_POS;
        q_vel = LIVESTOCK_Q_VEL;
        tkf->max_speed_ms = LIVESTOCK_MAX_SPEED_MS;
        break;
    default:
        q_pos = CONTAINER_Q_POS;
        q_vel = CONTAINER_Q_VEL;
        tkf->max_speed_ms = CONTAINER_MAX_SPEED_MS;
        break;
    }

    kf_init(&tkf->kf, initial_lat, initial_lon,
            q_pos, q_vel, BASE_R_POS);

    tkf->innovation_threshold = sqrtf(BASE_R_POS) * INNOVATION_MULTIPLIER;
}

uint8_t tracker_kf_process_fix(asset_tracker_kf_t *tkf,
                                float lat, float lon,
                                uint32_t epoch, float hdop)
{
    tkf->anomaly_flags = ANOMALY_NONE;
    tkf->fix_count++;

    /* Adjust measurement noise based on HDOP */
    float r = BASE_R_POS * hdop * hdop;
    tkf->kf.R.m[0][0] = r;
    tkf->kf.R.m[1][1] = r;

    /* Compute time delta */
    float dt = 0.0f;
    if (tkf->last_update_epoch > 0 && epoch > tkf->last_update_epoch) {
        dt = (float)(epoch - tkf->last_update_epoch);
    }

    /* Predict step (only if we have a previous fix) */
    if (tkf->fix_count > 1 && dt > 0.0f && dt < 86400.0f) {
        /* If stationary, reset velocity before predict */
        if (tkf->is_stationary) {
            tkf->kf.x.v[2] = 0.0f;
            tkf->kf.x.v[3] = 0.0f;
        }
        kf_predict(&tkf->kf, dt);
    }

    /* Check innovation for anomaly detection */
    if (tkf->fix_count > 1) {
        float innovation = kf_get_innovation(&tkf->kf, lat, lon);

        if (innovation > tkf->innovation_threshold) {
            tkf->anomaly_flags |= ANOMALY_INNOVATION;
        }

        /* Check for position jump */
        if (innovation > POSITION_JUMP_DEG) {
            tkf->anomaly_flags |= ANOMALY_JUMP;
            /* Reset KF on massive jump (likely lost fix then re-acquired) */
            kf_reset(&tkf->kf, lat, lon);
        }
    }

    /* Update step with new measurement */
    kf_update(&tkf->kf, lat, lon);

    /* Check speed for plausibility */
    float speed = kf_get_speed_ms(&tkf->kf, lat);
    if (speed > tkf->max_speed_ms) {
        tkf->anomaly_flags |= ANOMALY_SPEED;
    }

    tkf->last_update_epoch = epoch;

    return tkf->anomaly_flags;
}

void tracker_kf_set_stationary(asset_tracker_kf_t *tkf, bool stationary)
{
    tkf->is_stationary = stationary;
    if (stationary) {
        /* Zero out velocity to prevent KF divergence during prolonged standstill */
        tkf->kf.x.v[2] = 0.0f;
        tkf->kf.x.v[3] = 0.0f;
    }
}

void tracker_kf_get_position(const asset_tracker_kf_t *tkf,
                              float *lat, float *lon)
{
    kf_get_position(&tkf->kf, lat, lon);
}

float tracker_kf_get_speed(const asset_tracker_kf_t *tkf)
{
    float lat = tkf->kf.x.v[0];
    return kf_get_speed_ms(&tkf->kf, lat);
}

float tracker_kf_get_heading(const asset_tracker_kf_t *tkf)
{
    float v_lat = tkf->kf.x.v[2];
    float v_lon = tkf->kf.x.v[3];

    if (fabsf(v_lat) < 1e-12f && fabsf(v_lon) < 1e-12f) {
        return 0.0f;  /* Stationary — no heading */
    }

    float heading = atan2f(v_lon, v_lat) * (180.0f / 3.14159265f);
    if (heading < 0.0f) {
        heading += 360.0f;
    }
    return heading;
}

uint8_t tracker_kf_get_anomaly_flags(const asset_tracker_kf_t *tkf)
{
    return tkf->anomaly_flags;
}
