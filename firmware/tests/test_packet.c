/**
 * @file test_packet.c
 * @brief Unit Tests for CBOR Packet Encoding & Decoding
 *
 * Validates compact CBOR encoding (< 100 bytes target), round-trip decode
 * precision, negative coordinate handling, negative temperatures, and
 * sequence numbering.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include "../src/comms/packet.h"

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("  FAIL: %s == %s (got %lld, expected %lld) at line %d\n", \
               #a, #b, (long long)(a), (long long)(b), __LINE__); \
        failures++; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("  FAIL: %s == %s (got '%s', expected '%s') at line %d\n", \
               #a, #b, (a), (b), __LINE__); \
        failures++; \
    } \
} while(0)

static int failures = 0;

/* ──────────────────────────── Test: Normal Telemetry Round-Trip ─────── */

static void test_telemetry_roundtrip(void)
{
    printf("Test: Full telemetry packet encode/decode round-trip\n");

    tracker_packet_t orig;
    memset(&orig, 0, sizeof(orig));
    strncpy(orig.tracker_id, "ASSET-CONTAINER-409", sizeof(orig.tracker_id) - 1);
    orig.timestamp = 1761500000;
    orig.latitude = (int32_t)(51.5074 * 1e7);
    orig.longitude = (int32_t)(-0.1278 * 1e7);
    orig.altitude_mm = 45000;      /* 45m */
    orig.speed_mm_s = 2500;        /* 2.5 m/s */
    orig.heading_1e5 = 9000000;    /* 90 degrees */
    orig.battery_mv = 3620;        /* 3.62 V */
    orig.temperature = 21;         /* 21 C */
    orig.fix_type = 3;             /* 3D fix */
    orig.hdop_100 = 110;           /* 1.10 */
    orig.geofence_event = 1;       /* ENTER */
    orig.geofence_zone = 2;        /* Zone 2 */
    orig.anomaly_flags = 0;
    orig.sequence = 1042;

    uint8_t buffer[PACKET_MAX_SIZE];
    int encoded_len = packet_encode_cbor(&orig, buffer, sizeof(buffer));

    printf("  Encoded packet size: %d bytes (CBOR map)\n", encoded_len);
    if (encoded_len <= 0 || encoded_len > 120) {
        printf("  FAIL: encoded_len out of expected compact range (got %d)\n", encoded_len);
        failures++;
    }

    tracker_packet_t decoded;
    int dec_res = packet_decode_cbor(buffer, (uint16_t)encoded_len, &decoded);
    ASSERT_EQ(dec_res, 0);

    ASSERT_STR_EQ(decoded.tracker_id, orig.tracker_id);
    ASSERT_EQ(decoded.timestamp, orig.timestamp);
    ASSERT_EQ(decoded.latitude, orig.latitude);
    ASSERT_EQ(decoded.longitude, orig.longitude);
    ASSERT_EQ(decoded.altitude_mm, orig.altitude_mm);
    ASSERT_EQ(decoded.speed_mm_s, orig.speed_mm_s);
    ASSERT_EQ(decoded.heading_1e5, orig.heading_1e5);
    ASSERT_EQ(decoded.battery_mv, orig.battery_mv);
    ASSERT_EQ(decoded.temperature, orig.temperature);
    ASSERT_EQ(decoded.fix_type, orig.fix_type);
    ASSERT_EQ(decoded.hdop_100, orig.hdop_100);
    ASSERT_EQ(decoded.geofence_event, orig.geofence_event);
    ASSERT_EQ(decoded.geofence_zone, orig.geofence_zone);
    ASSERT_EQ(decoded.anomaly_flags, orig.anomaly_flags);
    ASSERT_EQ(decoded.sequence, orig.sequence);
}

/* ──────────────────────────── Test: Negative Values ─────────────────── */

static void test_negative_values(void)
{
    printf("Test: Negative coordinates and sub-zero temperatures\n");

    tracker_packet_t orig;
    memset(&orig, 0, sizeof(orig));
    strncpy(orig.tracker_id, "LIVESTOCK-NZ-001", sizeof(orig.tracker_id) - 1);
    orig.timestamp = 1761501000;
    orig.latitude = (int32_t)(-43.5321 * 1e7);   /* South (Christchurch, NZ) */
    orig.longitude = (int32_t)(172.6362 * 1e7);  /* East */
    orig.altitude_mm = -15000;                   /* Below sea level / negative */
    orig.speed_mm_s = 0;
    orig.heading_1e5 = 0;
    orig.battery_mv = 3550;
    orig.temperature = -18;                      /* Freezer / cold weather -18 C */
    orig.fix_type = 3;
    orig.hdop_100 = 85;
    orig.geofence_event = 0;
    orig.geofence_zone = 0;
    orig.anomaly_flags = 1;                      /* e.g. low temp warning */
    orig.sequence = 1043;

    uint8_t buffer[PACKET_MAX_SIZE];
    int encoded_len = packet_encode_cbor(&orig, buffer, sizeof(buffer));
    ASSERT_EQ((encoded_len > 0), 1);

    tracker_packet_t decoded;
    int dec_res = packet_decode_cbor(buffer, (uint16_t)encoded_len, &decoded);
    ASSERT_EQ(dec_res, 0);

    ASSERT_EQ(decoded.latitude, orig.latitude);
    ASSERT_EQ(decoded.longitude, orig.longitude);
    ASSERT_EQ(decoded.altitude_mm, orig.altitude_mm);
    ASSERT_EQ(decoded.temperature, -18);
    ASSERT_EQ(decoded.anomaly_flags, 1);
}

/* ──────────────────────────── Test: Sequence Counter ────────────────── */

static void test_sequence_counter(void)
{
    printf("Test: Sequence counter monotonicity\n");

    uint32_t seq1 = packet_next_sequence();
    uint32_t seq2 = packet_next_sequence();
    uint32_t seq3 = packet_next_sequence();

    ASSERT_EQ(seq2, seq1 + 1);
    ASSERT_EQ(seq3, seq2 + 1);
}

/* ──────────────────────────── Main Runner ───────────────────────────── */

int main(void)
{
    printf("=== CBOR Packet Unit Tests ===\n");

    test_telemetry_roundtrip();
    test_negative_values();
    test_sequence_counter();

    if (failures == 0) {
        printf("\nALL PACKET TESTS PASSED (0 failures)\n");
        return 0;
    } else {
        printf("\nTESTS FAILED with %d failure(s)\n", failures);
        return 1;
    }
}
