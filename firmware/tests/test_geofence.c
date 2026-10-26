/**
 * @file test_geofence.c
 * @brief Unit Tests for Geofence Engine & Point-in-Polygon
 *
 * Validates point-in-polygon ray-casting algorithm on concave, convex,
 * and boundary conditions, as well as geofence circular and polygonal
 * enter, exit, and dwell event generation.
 */

#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "../src/geofence/point_in_polygon.h"
#include "../src/geofence/geofence_engine.h"

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s at line %d\n", #cond, __LINE__); \
        failures++; \
    } \
} while(0)

#define ASSERT_FALSE(cond) do { \
    if ((cond)) { \
        printf("  FAIL: expected false for %s at line %d\n", #cond, __LINE__); \
        failures++; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("  FAIL: %s == %s (got %d, expected %d) at line %d\n", \
               #a, #b, (int)(a), (int)(b), __LINE__); \
        failures++; \
    } \
} while(0)

static int failures = 0;

/* ──────────────────────────── Test: Simple Square ───────────────────── */

static void test_pip_square(void)
{
    printf("Test: Point-in-polygon simple square\n");

    const float lats[] = {0.0f, 0.0f, 10.0f, 10.0f};
    const float lons[] = {0.0f, 10.0f, 10.0f, 0.0f};

    /* Inside */
    ASSERT_TRUE(point_in_polygon(5.0f, 5.0f, lats, lons, 4));
    ASSERT_TRUE(point_in_polygon(1.0f, 1.0f, lats, lons, 4));
    ASSERT_TRUE(point_in_polygon(9.0f, 9.0f, lats, lons, 4));

    /* Outside */
    ASSERT_FALSE(point_in_polygon(-1.0f, 5.0f, lats, lons, 4));
    ASSERT_FALSE(point_in_polygon(11.0f, 5.0f, lats, lons, 4));
    ASSERT_FALSE(point_in_polygon(5.0f, -1.0f, lats, lons, 4));
    ASSERT_FALSE(point_in_polygon(5.0f, 11.0f, lats, lons, 4));
}

/* ──────────────────────────── Test: Triangle ────────────────────────── */

static void test_pip_triangle(void)
{
    printf("Test: Point-in-polygon triangle\n");

    const float lats[] = {0.0f, 0.0f, 10.0f};
    const float lons[] = {0.0f, 10.0f, 5.0f};

    /* Inside centroid (lat ~3.33, lon 5.0) */
    ASSERT_TRUE(point_in_polygon(3.0f, 5.0f, lats, lons, 3));
    ASSERT_TRUE(point_in_polygon(1.0f, 5.0f, lats, lons, 3));

    /* Outside */
    ASSERT_FALSE(point_in_polygon(8.0f, 1.0f, lats, lons, 3));
    ASSERT_FALSE(point_in_polygon(8.0f, 9.0f, lats, lons, 3));
    ASSERT_FALSE(point_in_polygon(-1.0f, 5.0f, lats, lons, 3));
}

/* ──────────────────────────── Test: Concave L-Shape ─────────────────── */

static void test_pip_l_shape(void)
{
    printf("Test: Point-in-polygon concave L-shape\n");

    /*
       (0,10)┌────┐(4,10)
             │    │
             │    └─────────┐(10,4)
             │              │
             └──────────────┘
            (0,0)          (10,0)
    */
    const float lats[] = {0.0f, 0.0f, 4.0f, 4.0f, 10.0f, 10.0f};
    const float lons[] = {0.0f, 10.0f, 10.0f, 4.0f, 4.0f, 0.0f};

    /* Inside left column */
    ASSERT_TRUE(point_in_polygon(7.0f, 2.0f, lats, lons, 6));

    /* Inside bottom row */
    ASSERT_TRUE(point_in_polygon(2.0f, 7.0f, lats, lons, 6));

    /* Cutout notch (outside the concave boundary) */
    ASSERT_FALSE(point_in_polygon(7.0f, 7.0f, lats, lons, 6));

    /* Completely outside */
    ASSERT_FALSE(point_in_polygon(12.0f, 12.0f, lats, lons, 6));
    ASSERT_FALSE(point_in_polygon(-2.0f, 2.0f, lats, lons, 6));
}

/* ──────────────────────────── Test: Realistic Geofence Harbor ────────── */

static void test_pip_harbor(void)
{
    printf("Test: Point-in-polygon real coordinates (Port of Rotterdam)\n");

    /* Approx polygon around Europort container terminal */
    const float lats[] = {51.9500f, 51.9600f, 51.9550f, 51.9450f, 51.9400f};
    const float lons[] = {4.1000f,  4.1200f,  4.1600f,  4.1500f,  4.1100f};

    /* Inside center */
    ASSERT_TRUE(point_in_polygon(51.9500f, 4.1300f, lats, lons, 5));

    /* Outside north */
    ASSERT_FALSE(point_in_polygon(51.9700f, 4.1300f, lats, lons, 5));

    /* Outside east */
    ASSERT_FALSE(point_in_polygon(51.9500f, 4.1800f, lats, lons, 5));
}

/* ──────────────────────────── Test: Geofence Engine Events ───────────── */

static void test_geofence_engine_lifecycle(void)
{
    printf("Test: Geofence engine add, enter, dwell, exit cycle\n");

    geofence_engine_init();

    /* Zone 1: Circular around (51.5074, -0.1278) radius 500m */
    geofence_zone_t circ_zone;
    memset(&circ_zone, 0, sizeof(circ_zone));
    circ_zone.zone_id = 1;
    circ_zone.type = GEOFENCE_TYPE_CIRCULAR;
    circ_zone.center_lat = (int32_t)(51.5074 * 1e7);
    circ_zone.center_lon = (int32_t)(-0.1278 * 1e7);
    circ_zone.radius_m = 500;
    circ_zone.dwell_time_s = 1800; /* 30 min */
    circ_zone.trigger_immediate_tx = 1;

    int res = geofence_add_zone(&circ_zone);
    ASSERT_EQ(res, 0);
    ASSERT_EQ(geofence_get_zone_count(), 1);

    geofence_result_t results[GEOFENCE_MAX_ZONES];

    /* 1. Outside fence */
    int32_t out_lat = (int32_t)(51.5200 * 1e7); /* ~1.4 km away */
    int32_t out_lon = (int32_t)(-0.1278 * 1e7);
    int ev = geofence_check(out_lat, out_lon, 1000, results, GEOFENCE_MAX_ZONES);
    ASSERT_EQ(ev, 0);
    ASSERT_FALSE(results[0].inside);

    /* 2. Step inside -> ENTER event */
    int32_t in_lat = (int32_t)(51.5075 * 1e7);
    int32_t in_lon = (int32_t)(-0.1278 * 1e7);
    ev = geofence_check(in_lat, in_lon, 1100, results, GEOFENCE_MAX_ZONES);
    ASSERT_EQ(ev, 1);
    ASSERT_EQ(results[0].event, GEOFENCE_EVENT_ENTER);
    ASSERT_TRUE(results[0].inside);
    ASSERT_TRUE(geofence_needs_immediate_tx(results, 1));

    /* 3. Still inside, dwell time not yet reached (100s later) */
    ev = geofence_check(in_lat, in_lon, 1200, results, GEOFENCE_MAX_ZONES);
    ASSERT_EQ(ev, 0);
    ASSERT_TRUE(results[0].inside);

    /* 4. Still inside, dwell time exceeded (2000s later) -> DWELL event */
    ev = geofence_check(in_lat, in_lon, 3200, results, GEOFENCE_MAX_ZONES);
    ASSERT_EQ(ev, 1);
    ASSERT_EQ(results[0].event, GEOFENCE_EVENT_DWELL);

    /* 5. Move outside -> EXIT event */
    ev = geofence_check(out_lat, out_lon, 3300, results, GEOFENCE_MAX_ZONES);
    ASSERT_EQ(ev, 1);
    ASSERT_EQ(results[0].event, GEOFENCE_EVENT_EXIT);
    ASSERT_FALSE(results[0].inside);

    /* Clean up */
    geofence_remove_zone(1);
    ASSERT_EQ(geofence_get_zone_count(), 0);
}

/* ──────────────────────────── Main Runner ───────────────────────────── */

int main(void)
{
    printf("=== Geofence & Point-in-Polygon Unit Tests ===\n");

    test_pip_square();
    test_pip_triangle();
    test_pip_l_shape();
    test_pip_harbor();
    test_geofence_engine_lifecycle();

    if (failures == 0) {
        printf("\nALL GEOFENCE TESTS PASSED (0 failures)\n");
        return 0;
    } else {
        printf("\nTESTS FAILED with %d failure(s)\n", failures);
        return 1;
    }
}
