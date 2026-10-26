/**
 * @file geofence_engine.h
 * @brief Geofence Detection Engine
 *
 * Defines geographic boundary zones and detects when the asset
 * enters, exits, or dwells within them.
 */

#ifndef GEOFENCE_ENGINE_H
#define GEOFENCE_ENGINE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ──────────────────────────── Constants ──────────────────────────── */

#define GEOFENCE_MAX_ZONES     8
#define GEOFENCE_MAX_VERTICES  8

/* ──────────────────────────── Types ──────────────────────────── */

typedef enum {
    GEOFENCE_TYPE_CIRCULAR  = 0,
    GEOFENCE_TYPE_POLYGONAL = 1,
} geofence_type_t;

typedef enum {
    GEOFENCE_EVENT_NONE  = 0,
    GEOFENCE_EVENT_ENTER = 1,
    GEOFENCE_EVENT_EXIT  = 2,
    GEOFENCE_EVENT_DWELL = 3,
} geofence_event_t;

/**
 * @brief Geofence zone configuration.
 */
typedef struct {
    uint8_t  zone_id;
    uint8_t  type;                           /**< 0 = circular, 1 = polygonal */
    uint8_t  num_vertices;                   /**< 0 for circular              */
    int32_t  center_lat;                     /**< × 1e7                       */
    int32_t  center_lon;                     /**< × 1e7                       */
    uint32_t radius_m;                       /**< For circular fences         */
    int32_t  vertices[GEOFENCE_MAX_VERTICES][2]; /**< [lat, lon] pairs × 1e7  */
    uint32_t dwell_time_s;                   /**< Dwell threshold in seconds  */
    uint8_t  trigger_immediate_tx;           /**< 0 or 1                      */
} geofence_zone_t;

/**
 * @brief Geofence check result for a single zone.
 */
typedef struct {
    uint8_t         zone_id;
    geofence_event_t event;
    bool            inside;
    uint32_t        dwell_elapsed_s;
} geofence_result_t;

/* ──────────────────────────── Public API ──────────────────────────── */

/**
 * @brief Initialize the geofence engine.
 *        Loads stored zones from flash.
 */
void geofence_engine_init(void);

/**
 * @brief Check all geofence zones against a position.
 * @param lat_1e7   Latitude × 1e7.
 * @param lon_1e7   Longitude × 1e7.
 * @param epoch     Current UTC epoch for dwell timing.
 * @param results   Output array of results (one per active zone).
 * @param max_results  Size of results array.
 * @return Number of events (ENTER, EXIT, or DWELL) generated.
 */
int geofence_check(int32_t lat_1e7, int32_t lon_1e7, uint32_t epoch,
                   geofence_result_t *results, int max_results);

/**
 * @brief Add or update a geofence zone.
 * @param zone  Zone configuration.
 * @return 0 on success, -1 on failure (e.g., too many zones).
 */
int geofence_add_zone(const geofence_zone_t *zone);

/**
 * @brief Remove a geofence zone by ID.
 * @param zone_id  ID of the zone to remove.
 * @return 0 on success, -1 if not found.
 */
int geofence_remove_zone(uint8_t zone_id);

/**
 * @brief Get the number of active geofence zones.
 */
int geofence_get_zone_count(void);

/**
 * @brief Get a geofence zone by index.
 * @param index  Zone index (0 to count-1).
 * @param zone   Output: zone configuration.
 * @return 0 on success, -1 if invalid index.
 */
int geofence_get_zone(int index, geofence_zone_t *zone);

/**
 * @brief Check if any event requires immediate transmission.
 * @param results      Array of check results.
 * @param num_results  Number of results.
 * @return true if any event has trigger_immediate_tx set.
 */
bool geofence_needs_immediate_tx(const geofence_result_t *results, int num_results);

/**
 * @brief Get the centroid of the nearest geofence zone (for ETA calculation).
 * @param lat_1e7    Current latitude × 1e7.
 * @param lon_1e7    Current longitude × 1e7.
 * @param dst_lat    Output: destination latitude (degrees).
 * @param dst_lon    Output: destination longitude (degrees).
 * @return 0 if a destination found, -1 if no zones configured.
 */
int geofence_get_nearest_centroid(int32_t lat_1e7, int32_t lon_1e7,
                                  float *dst_lat, float *dst_lon);

#ifdef __cplusplus
}
#endif

#endif /* GEOFENCE_ENGINE_H */
