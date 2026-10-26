/**
 * @file test_kalman.c
 * @brief Unit Tests for Kalman Filter
 *
 * Validates KF convergence on synthetic GPS data and correct
 * velocity/speed estimates.
 */

#include <stdio.h>
#include <math.h>
#include <assert.h>
#include "../src/kalman/kalman_filter.h"
#include "../src/kalman/asset_tracker_kf.h"
#include "../src/kalman/eta_predictor.h"

#define ASSERT_NEAR(a, b, tol) do { \
    if (fabs((double)(a) - (double)(b)) > (double)(tol)) { \
        printf("  FAIL: %s ≈ %s  (got %.8f, expected %.8f, tol %.8f)\n", \
               #a, #b, (double)(a), (double)(b), (double)(tol)); \
        failures++; \
    } \
} while(0)

static int failures = 0;

/* ──────────────────────────── Test: KF Convergence ──────────────────── */

static void test_kf_convergence(void)
{
    printf("Test: KF converges on stationary GPS data\n");

    kalman_filter_t kf;
    kf_init(&kf, 51.5074f, -0.1278f, 1e-10f, 1e-8f, 7.26e-10f);

    /* Feed 10 measurements at the same location with small noise */
    float measurements[][2] = {
        {51.50742f, -0.12782f},
        {51.50738f, -0.12778f},
        {51.50741f, -0.12781f},
        {51.50739f, -0.12779f},
        {51.50740f, -0.12780f},
        {51.50741f, -0.12781f},
        {51.50740f, -0.12780f},
        {51.50740f, -0.12780f},
        {51.50741f, -0.12779f},
        {51.50740f, -0.12780f},
    };

    for (int i = 0; i < 10; i++) {
        kf_predict(&kf, 1.0f);
        kf_update(&kf, measurements[i][0], measurements[i][1]);
    }

    float lat, lon;
    kf_get_position(&kf, &lat, &lon);

    /* Should converge near the true position */
    ASSERT_NEAR(lat, 51.5074f, 0.001f);
    ASSERT_NEAR(lon, -0.1278f, 0.001f);

    /* Velocity should be near zero (stationary) */
    float v_lat, v_lon;
    kf_get_velocity(&kf, &v_lat, &v_lon);
    ASSERT_NEAR(v_lat, 0.0f, 0.001f);
    ASSERT_NEAR(v_lon, 0.0f, 0.001f);

    printf("  Position: (%.6f, %.6f)\n", lat, lon);
    printf("  Velocity: (%.8f, %.8f)\n", v_lat, v_lon);
}

/* ──────────────────────────── Test: KF Moving Target ──────────────────── */

static void test_kf_moving(void)
{
    printf("Test: KF tracks moving target\n");

    kalman_filter_t kf;
    kf_init(&kf, 51.5000f, -0.1000f, 1e-10f, 1e-8f, 7.26e-10f);

    /* Simulate movement northward: ~0.00001 deg/s ≈ 1.1 m/s */
    float dt = 10.0f;  /* 10 seconds between fixes */
    float speed_deg_s = 0.00001f;

    for (int i = 0; i < 20; i++) {
        float true_lat = 51.5000f + speed_deg_s * dt * (i + 1);
        float true_lon = -0.1000f;

        kf_predict(&kf, dt);
        kf_update(&kf, true_lat, true_lon);
    }

    float lat, lon;
    kf_get_position(&kf, &lat, &lon);

    float expected_lat = 51.5000f + speed_deg_s * dt * 20;
    ASSERT_NEAR(lat, expected_lat, 0.001f);

    float speed = kf_get_speed_ms(&kf, lat);
    printf("  Final position: (%.6f, %.6f)\n", lat, lon);
    printf("  Estimated speed: %.2f m/s\n", speed);

    /* Speed should be approximately 1.1 m/s */
    ASSERT_NEAR(speed, 1.1f, 0.5f);
}

/* ──────────────────────────── Test: Innovation Anomaly ──────────────────── */

static void test_kf_innovation_anomaly(void)
{
    printf("Test: KF detects anomalous measurement\n");

    kalman_filter_t kf;
    kf_init(&kf, 51.5074f, -0.1278f, 1e-10f, 1e-8f, 7.26e-10f);

    /* Feed some normal measurements */
    for (int i = 0; i < 5; i++) {
        kf_predict(&kf, 1.0f);
        kf_update(&kf, 51.5074f + 0.00001f * i, -0.1278f);
    }

    /* Now inject a large jump (GPS spoofing) */
    float innovation = kf_get_innovation(&kf, 52.0f, -0.5f);
    printf("  Innovation for spoofed measurement: %.6f degrees\n", innovation);

    /* Innovation should be large */
    assert(innovation > 0.01f);
    printf("  Anomaly correctly detected (innovation > threshold)\n");
}

/* ──────────────────────────── Test: ETA Prediction ──────────────────── */

static void test_eta_prediction(void)
{
    printf("Test: ETA prediction\n");

    /* London to Paris: ~340 km */
    float lat1 = 51.5074f, lon1 = -0.1278f;   /* London */
    float lat2 = 48.8566f, lon2 = 2.3522f;    /* Paris  */

    float dist = eta_haversine_m(lat1, lon1, lat2, lon2);
    printf("  London-Paris distance: %.0f m (expected ~340 km)\n", dist);
    ASSERT_NEAR(dist, 340000.0f, 20000.0f);

    /* At 100 km/h ≈ 27.78 m/s, ETA ≈ 204 minutes */
    uint32_t eta = eta_compute_minutes(lat1, lon1, lat2, lon2, 27.78f);
    printf("  ETA at 100 km/h: %u minutes\n", eta);
    ASSERT_NEAR((float)eta, 204.0f, 20.0f);

    /* Zero speed — should return UINT32_MAX */
    uint32_t eta_zero = eta_compute_minutes(lat1, lon1, lat2, lon2, 0.0f);
    assert(eta_zero == UINT32_MAX);
    printf("  Zero speed ETA: UINT32_MAX (correct)\n");
}

/* ──────────────────────────── Test: Asset Tracker KF ──────────────────── */

static void test_tracker_kf(void)
{
    printf("Test: Asset tracker KF wrapper\n");

    asset_tracker_kf_t tkf;
    tracker_kf_init(&tkf, ASSET_TYPE_CONTAINER, 51.5074f, -0.1278f);

    /* Process 5 normal fixes */
    for (int i = 0; i < 5; i++) {
        uint8_t flags = tracker_kf_process_fix(&tkf,
            51.5074f + 0.0001f * i, -0.1278f,
            1700000000 + 60 * i, 1.2f);
        printf("  Fix %d: anomaly_flags = 0x%02X\n", i + 1, flags);
    }

    /* Inject implausible speed (container moving at 300 km/h) */
    uint8_t flags = tracker_kf_process_fix(&tkf,
        52.5f, -0.1278f,  /* ~111 km jump in 60 s = 6660 km/h */
        1700000000 + 300, 1.0f);
    printf("  Spoofed fix: anomaly_flags = 0x%02X\n", flags);
    /* Should have ANOMALY_JUMP flag set */

    /* Test stationary mode */
    tracker_kf_set_stationary(&tkf, true);
    float speed = tracker_kf_get_speed(&tkf);
    printf("  Speed after stationary: %.4f m/s\n", speed);
}

/* ──────────────────────────── Main ──────────────────────────── */

int main(void)
{
    printf("========================================\n");
    printf("  Kalman Filter Unit Tests\n");
    printf("========================================\n\n");

    test_kf_convergence();
    printf("\n");
    test_kf_moving();
    printf("\n");
    test_kf_innovation_anomaly();
    printf("\n");
    test_eta_prediction();
    printf("\n");
    test_tracker_kf();

    printf("\n========================================\n");
    if (failures == 0) {
        printf("  ALL TESTS PASSED\n");
    } else {
        printf("  %d ASSERTION(S) FAILED\n", failures);
    }
    printf("========================================\n");

    return failures;
}
