/**
 * @file test_ubx_parser.c
 * @brief Unit Tests for u-blox UBX Binary Protocol Parser
 *
 * Validates byte-by-byte parsing of UBX frames, checksum calculation,
 * NAV-PVT payload extraction, noise resilience, and checksum error rejection.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include "../src/gnss/ubx_parser.h"

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("  FAIL: %s == %s (got %lld, expected %lld) at line %d\n", \
               #a, #b, (long long)(a), (long long)(b), __LINE__); \
        failures++; \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s at line %d\n", #cond, __LINE__); \
        failures++; \
    } \
} while(0)

static int failures = 0;
static ubx_nav_pvt_t last_pvt;
static int pvt_callback_count = 0;

static void on_pvt_received(const ubx_nav_pvt_t *pvt)
{
    memcpy(&last_pvt, pvt, sizeof(ubx_nav_pvt_t));
    pvt_callback_count++;
}

/* Helper to assemble and feed a UBX packet */
static void feed_ubx_packet(ubx_parser_t *parser, uint8_t msg_class, uint8_t msg_id,
                            const uint8_t *payload, uint16_t payload_len)
{
    /* Sync */
    ubx_parser_feed(parser, UBX_SYNC1);
    ubx_parser_feed(parser, UBX_SYNC2);

    /* Buffer for checksum: class, id, len_lo, len_hi, payload */
    uint8_t header[4];
    header[0] = msg_class;
    header[1] = msg_id;
    header[2] = (uint8_t)(payload_len & 0xFF);
    header[3] = (uint8_t)((payload_len >> 8) & 0xFF);

    uint8_t ck_a = 0, ck_b = 0;
    for (int i = 0; i < 4; i++) {
        ck_a += header[i];
        ck_b += ck_a;
        ubx_parser_feed(parser, header[i]);
    }

    for (uint16_t i = 0; i < payload_len; i++) {
        ck_a += payload[i];
        ck_b += ck_a;
        ubx_parser_feed(parser, payload[i]);
    }

    ubx_parser_feed(parser, ck_a);
    ubx_parser_feed(parser, ck_b);
}

/* ──────────────────────────── Test: Checksum Calculation ──────────────── */

static void test_checksum_algorithm(void)
{
    printf("Test: UBX 8-bit Fletcher checksum calculation\n");

    /* Example test vector */
    uint8_t test_data[] = {0x01, 0x07, 0x00, 0x00};
    uint8_t ck_a = 0, ck_b = 0;
    ubx_checksum(test_data, sizeof(test_data), &ck_a, &ck_b);

    /*
     * ck_a = 0x01 + 0x07 = 0x08
     * ck_b = (0x01) + (0x01+0x07) + (0x01+0x07) + (0x01+0x07) = 1 + 8 + 8 + 8 = 25 = 0x19
     */
    ASSERT_EQ(ck_a, 0x08);
    ASSERT_EQ(ck_b, 0x19);
}

/* ──────────────────────────── Test: NAV-PVT Parse ───────────────────── */

static void test_nav_pvt_parsing(void)
{
    printf("Test: NAV-PVT packet parsing and field extraction\n");

    ubx_parser_t parser;
    ubx_parser_init(&parser);
    ubx_parser_set_nav_pvt_callback(&parser, on_pvt_received);
    pvt_callback_count = 0;

    ubx_nav_pvt_t sample_pvt;
    memset(&sample_pvt, 0, sizeof(sample_pvt));
    sample_pvt.iTOW = 3600000;
    sample_pvt.year = 2026;
    sample_pvt.month = 10;
    sample_pvt.day = 26;
    sample_pvt.hour = 19;
    sample_pvt.min = 30;
    sample_pvt.sec = 0;
    sample_pvt.valid = 0x07; /* Date, Time, Fully resolved */
    sample_pvt.fixType = 3;  /* 3D fix */
    sample_pvt.flags = 0x01; /* GNSS fix OK */
    sample_pvt.numSV = 14;
    sample_pvt.lon = (int32_t)(-0.1278 * 1e7);
    sample_pvt.lat = (int32_t)(51.5074 * 1e7);
    sample_pvt.height = 35000; /* 35m */
    sample_pvt.hMSL = 20000;   /* 20m */
    sample_pvt.hAcc = 1800;    /* 1.8m */
    sample_pvt.vAcc = 2500;    /* 2.5m */
    sample_pvt.gSpeed = 1250;  /* 1.25 m/s */
    sample_pvt.headMot = (int32_t)(180.0 * 1e5);
    sample_pvt.pDOP = 120;     /* 1.20 */

    feed_ubx_packet(&parser, UBX_CLASS_NAV, UBX_NAV_PVT,
                    (const uint8_t *)&sample_pvt, sizeof(sample_pvt));

    ASSERT_EQ(pvt_callback_count, 1);
    ASSERT_EQ(parser.msg_count, 1);
    ASSERT_EQ(parser.checksum_errors, 0);

    /* Verify extracted data */
    ASSERT_EQ(last_pvt.year, 2026);
    ASSERT_EQ(last_pvt.month, 10);
    ASSERT_EQ(last_pvt.day, 26);
    ASSERT_EQ(last_pvt.fixType, 3);
    ASSERT_EQ(last_pvt.numSV, 14);
    ASSERT_EQ(last_pvt.lat, sample_pvt.lat);
    ASSERT_EQ(last_pvt.lon, sample_pvt.lon);
    ASSERT_EQ(last_pvt.gSpeed, 1250);
    ASSERT_EQ(last_pvt.hAcc, 1800);
    ASSERT_EQ(last_pvt.pDOP, 120);
}

/* ──────────────────────────── Test: Checksum Error Rejection ─────────── */

static void test_checksum_error_rejection(void)
{
    printf("Test: Corrupted packet checksum rejection\n");

    ubx_parser_t parser;
    ubx_parser_init(&parser);
    ubx_parser_set_nav_pvt_callback(&parser, on_pvt_received);
    int prev_count = pvt_callback_count;

    ubx_nav_pvt_t sample_pvt;
    memset(&sample_pvt, 0, sizeof(sample_pvt));
    sample_pvt.fixType = 3;

    /* Feed sync */
    ubx_parser_feed(&parser, UBX_SYNC1);
    ubx_parser_feed(&parser, UBX_SYNC2);

    /* Class, ID, len */
    ubx_parser_feed(&parser, UBX_CLASS_NAV);
    ubx_parser_feed(&parser, UBX_NAV_PVT);
    uint16_t len = sizeof(sample_pvt);
    ubx_parser_feed(&parser, (uint8_t)(len & 0xFF));
    ubx_parser_feed(&parser, (uint8_t)((len >> 8) & 0xFF));

    /* Payload */
    const uint8_t *payload = (const uint8_t *)&sample_pvt;
    for (uint16_t i = 0; i < len; i++) {
        ubx_parser_feed(&parser, payload[i]);
    }

    /* Send deliberately wrong checksum bytes */
    ubx_parser_feed(&parser, 0xFF);
    ubx_parser_feed(&parser, 0xFE);

    ASSERT_EQ(pvt_callback_count, prev_count); /* No callback fired */
    ASSERT_EQ(parser.checksum_errors, 1);
}

/* ──────────────────────────── Test: Noise Stream Recovery ────────────── */

static void test_noise_recovery(void)
{
    printf("Test: Parser ignores garbage bytes before valid frame\n");

    ubx_parser_t parser;
    ubx_parser_init(&parser);
    ubx_parser_set_nav_pvt_callback(&parser, on_pvt_received);
    int prev_count = pvt_callback_count;

    /* Feed random stream noise */
    uint8_t noise[] = {0x00, 0x42, 0xFF, UBX_SYNC1, 0x11, 0xAA, 0x55, 0x00};
    for (size_t i = 0; i < sizeof(noise); i++) {
        ubx_parser_feed(&parser, noise[i]);
    }

    /* Now feed valid packet */
    ubx_nav_pvt_t sample;
    memset(&sample, 0, sizeof(sample));
    sample.fixType = 2;
    sample.lat = 12345678;

    feed_ubx_packet(&parser, UBX_CLASS_NAV, UBX_NAV_PVT,
                    (const uint8_t *)&sample, sizeof(sample));

    ASSERT_EQ(pvt_callback_count, prev_count + 1);
    ASSERT_EQ(last_pvt.lat, 12345678);
}

/* ──────────────────────────── Main Runner ───────────────────────────── */

int main(void)
{
    printf("=== u-blox UBX Binary Parser Unit Tests ===\n");

    test_checksum_algorithm();
    test_nav_pvt_parsing();
    test_checksum_error_rejection();
    test_noise_recovery();

    if (failures == 0) {
        printf("\nALL UBX PARSER TESTS PASSED (0 failures)\n");
        return 0;
    } else {
        printf("\nTESTS FAILED with %d failure(s)\n", failures);
        return 1;
    }
}
