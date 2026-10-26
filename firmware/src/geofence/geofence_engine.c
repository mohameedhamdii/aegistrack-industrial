/**
 * @file geofence_engine.c
 * @brief Geofence Detection Engine Implementation
 */

#include "geofence_engine.h"
#include "point_in_polygon.h"
#include "geofence_storage.h"
#include <math.h>
#include <string.h>

/* ──────────────────────────── Constants ──────────────────────────── */

#define EARTH_RADIUS_M   6371000.0f
#define DEG_TO_RAD       0.01745329f
#define COORD_SCALE      1e-7f   /* Convert int32 × 1e7 to degrees */

/* ──────────────────────────── Private State ──────────────────────────── */

static geofence_zone_t s_zones[GEOFENCE_MAX_ZONES];
static int             s_zone_count = 0;

/* Per-zone tracking for enter/exit/dwell detection */
typedef struct {
    bool     was_inside;
    uint32_t enter_epoch;
    bool     dwell_triggered;
} zone_state_t;

static zone_state_t s_zone_state[GEOFENCE_MAX_ZONES];

/* ──────────────────────────── Distance Helpers ──────────────────────── */

/**
 * @brief Compute haversine distance in meters between two points.
 */
static float haversine_m(float lat1_deg, float lon1_deg,
                         float lat2_deg, float lon2_deg)
{
    float dlat = (lat2_deg - lat1_deg) * DEG_TO_RAD;
    float dlon = (lon2_deg - lon1_deg) * DEG_TO_RAD;
    float la1  = lat1_deg * DEG_TO_RAD;
    float la2  = lat2_deg * DEG_TO_RAD;

    float a = sinf(dlat * 0.5f) * sinf(dlat * 0.5f) +
              cosf(la1) * cosf(la2) *
              sinf(dlon * 0.5f) * sinf(dlon * 0.5f);
    float c = 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));

    return EARTH_RADIUS_M * c;
}

/* ──────────────────────────── Zone Check Helpers ──────────────────────── */

static bool check_circular(const geofence_zone_t *zone,
                           float lat_deg, float lon_deg)
{
    float center_lat = (float)zone->center_lat * COORD_SCALE;
    float center_lon = (float)zone->center_lon * COORD_SCALE;

    float dist = haversine_m(lat_deg, lon_deg, center_lat, center_lon);
    return dist <= (float)zone->radius_m;
}

static bool check_polygonal(const geofence_zone_t *zone,
                            float lat_deg, float lon_deg)
{
    if (zone->num_vertices < 3) {
        return false;
    }

    /* Convert vertices to float degrees for point-in-polygon test */
    float poly_lats[GEOFENCE_MAX_VERTICES];
    float poly_lons[GEOFENCE_MAX_VERTICES];

    for (int i = 0; i < zone->num_vertices; i++) {
        poly_lats[i] = (float)zone->vertices[i][0] * COORD_SCALE;
        poly_lons[i] = (float)zone->vertices[i][1] * COORD_SCALE;
    }

    return point_in_polygon(lat_deg, lon_deg,
                            poly_lats, poly_lons,
                            zone->num_vertices);
}

/* ──────────────────────────── Public API ──────────────────────────── */

void geofence_engine_init(void)
{
    memset(s_zones, 0, sizeof(s_zones));
    memset(s_zone_state, 0, sizeof(s_zone_state));
    s_zone_count = 0;

    /* Load zones from flash storage */
    geofence_storage_init();
    s_zone_count = geofence_storage_load(s_zones, GEOFENCE_MAX_ZONES);
}

int geofence_check(int32_t lat_1e7, int32_t lon_1e7, uint32_t epoch,
                   geofence_result_t *results, int max_results)
{
    float lat_deg = (float)lat_1e7 * COORD_SCALE;
    float lon_deg = (float)lon_1e7 * COORD_SCALE;
    int event_count = 0;

    for (int i = 0; i < s_zone_count && event_count < max_results; i++) {
        bool inside;

        if (s_zones[i].type == GEOFENCE_TYPE_CIRCULAR) {
            inside = check_circular(&s_zones[i], lat_deg, lon_deg);
        } else {
            inside = check_polygonal(&s_zones[i], lat_deg, lon_deg);
        }

        /* Detect transitions */
        geofence_event_t event = GEOFENCE_EVENT_NONE;

        if (inside && !s_zone_state[i].was_inside) {
            event = GEOFENCE_EVENT_ENTER;
            s_zone_state[i].enter_epoch = epoch;
            s_zone_state[i].dwell_triggered = false;
        } else if (!inside && s_zone_state[i].was_inside) {
            event = GEOFENCE_EVENT_EXIT;
            s_zone_state[i].enter_epoch = 0;
            s_zone_state[i].dwell_triggered = false;
        } else if (inside && s_zone_state[i].was_inside) {
            /* Check dwell */
            if (!s_zone_state[i].dwell_triggered &&
                s_zones[i].dwell_time_s > 0 &&
                s_zone_state[i].enter_epoch > 0 &&
                epoch > s_zone_state[i].enter_epoch)
            {
                uint32_t elapsed = epoch - s_zone_state[i].enter_epoch;
                if (elapsed >= s_zones[i].dwell_time_s) {
                    event = GEOFENCE_EVENT_DWELL;
                    s_zone_state[i].dwell_triggered = true;
                }
            }
        }

        s_zone_state[i].was_inside = inside;

        /* Record result if there's an event */
        if (event != GEOFENCE_EVENT_NONE) {
            results[event_count].zone_id = s_zones[i].zone_id;
            results[event_count].event   = event;
            results[event_count].inside  = inside;
            results[event_count].dwell_elapsed_s =
                (s_zone_state[i].enter_epoch > 0 && epoch > s_zone_state[i].enter_epoch)
                    ? (epoch - s_zone_state[i].enter_epoch) : 0;
            event_count++;
        }
    }

    return event_count;
}

int geofence_add_zone(const geofence_zone_t *zone)
{
    /* Check if zone ID already exists — update it */
    for (int i = 0; i < s_zone_count; i++) {
        if (s_zones[i].zone_id == zone->zone_id) {
            memcpy(&s_zones[i], zone, sizeof(geofence_zone_t));
            memset(&s_zone_state[i], 0, sizeof(zone_state_t));
            geofence_storage_save(s_zones, s_zone_count);
            return 0;
        }
    }

    /* Add new zone */
    if (s_zone_count >= GEOFENCE_MAX_ZONES) {
        return -1;  /* No room */
    }

    memcpy(&s_zones[s_zone_count], zone, sizeof(geofence_zone_t));
    memset(&s_zone_state[s_zone_count], 0, sizeof(zone_state_t));
    s_zone_count++;

    geofence_storage_save(s_zones, s_zone_count);
    return 0;
}

int geofence_remove_zone(uint8_t zone_id)
{
    for (int i = 0; i < s_zone_count; i++) {
        if (s_zones[i].zone_id == zone_id) {
            /* Shift remaining zones down */
            for (int j = i; j < s_zone_count - 1; j++) {
                s_zones[j]      = s_zones[j + 1];
                s_zone_state[j] = s_zone_state[j + 1];
            }
            s_zone_count--;
            geofence_storage_save(s_zones, s_zone_count);
            return 0;
        }
    }
    return -1;  /* Not found */
}

int geofence_get_zone_count(void)
{
    return s_zone_count;
}

int geofence_get_zone(int index, geofence_zone_t *zone)
{
    if (index < 0 || index >= s_zone_count || zone == NULL) {
        return -1;
    }
    memcpy(zone, &s_zones[index], sizeof(geofence_zone_t));
    return 0;
}

bool geofence_needs_immediate_tx(const geofence_result_t *results, int num_results)
{
    for (int i = 0; i < num_results; i++) {
        /* Look up the zone config to check trigger_immediate_tx */
        for (int j = 0; j < s_zone_count; j++) {
            if (s_zones[j].zone_id == results[i].zone_id &&
                s_zones[j].trigger_immediate_tx) {
                return true;
            }
        }
    }
    return false;
}

int geofence_get_nearest_centroid(int32_t lat_1e7, int32_t lon_1e7,
                                  float *dst_lat, float *dst_lon)
{
    if (s_zone_count == 0) {
        return -1;
    }

    float lat_deg = (float)lat_1e7 * COORD_SCALE;
    float lon_deg = (float)lon_1e7 * COORD_SCALE;
    float min_dist = 1e12f;
    int nearest = -1;

    for (int i = 0; i < s_zone_count; i++) {
        float c_lat, c_lon;

        if (s_zones[i].type == GEOFENCE_TYPE_CIRCULAR) {
            c_lat = (float)s_zones[i].center_lat * COORD_SCALE;
            c_lon = (float)s_zones[i].center_lon * COORD_SCALE;
        } else {
            /* Compute centroid of polygon */
            float sum_lat = 0, sum_lon = 0;
            for (int v = 0; v < s_zones[i].num_vertices; v++) {
                sum_lat += (float)s_zones[i].vertices[v][0] * COORD_SCALE;
                sum_lon += (float)s_zones[i].vertices[v][1] * COORD_SCALE;
            }
            c_lat = sum_lat / s_zones[i].num_vertices;
            c_lon = sum_lon / s_zones[i].num_vertices;
        }

        float dist = haversine_m(lat_deg, lon_deg, c_lat, c_lon);
        if (dist < min_dist) {
            min_dist = dist;
            nearest = i;
            *dst_lat = c_lat;
            *dst_lon = c_lon;
        }
    }

    return (nearest >= 0) ? 0 : -1;
}
