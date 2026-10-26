/**
 * @file ubx_parser.h
 * @brief u-blox UBX Binary Protocol Parser
 *
 * Parses incoming UBX binary messages from the GNSS module.
 * Implements a byte-by-byte state machine that handles synchronization,
 * checksum verification, and message dispatching.
 */

#ifndef UBX_PARSER_H
#define UBX_PARSER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ──────────────────────────── UBX Message IDs ──────────────────────── */

#define UBX_SYNC1       0xB5
#define UBX_SYNC2       0x62

/* NAV class (0x01) */
#define UBX_CLASS_NAV   0x01
#define UBX_NAV_PVT     0x07
#define UBX_NAV_STATUS  0x03
#define UBX_NAV_SAT     0x35

/* CFG class (0x06) */
#define UBX_CLASS_CFG   0x06

/* ACK class (0x05) */
#define UBX_CLASS_ACK   0x05
#define UBX_ACK_ACK     0x01
#define UBX_ACK_NAK     0x00

/* MGA class (0x13) */
#define UBX_CLASS_MGA   0x13
#define UBX_MGA_DBD     0x80

/* ──────────────────────────── NAV-PVT Structure ──────────────────────── */

/**
 * UBX-NAV-PVT payload (92 bytes).
 * Contains position, velocity, and time solution.
 */
typedef struct __attribute__((packed)) {
    uint32_t iTOW;       /**< GPS time of week (ms)             */
    uint16_t year;       /**< Year (UTC)                        */
    uint8_t  month;      /**< Month (1-12)                      */
    uint8_t  day;        /**< Day (1-31)                        */
    uint8_t  hour;       /**< Hour (0-23)                       */
    uint8_t  min;        /**< Minute (0-59)                     */
    uint8_t  sec;        /**< Second (0-60)                     */
    uint8_t  valid;      /**< Validity flags                    */
    uint32_t tAcc;       /**< Time accuracy estimate (ns)       */
    int32_t  nano;       /**< Nanoseconds (UTC)                 */
    uint8_t  fixType;    /**< GNSS fix type                     */
    uint8_t  flags;      /**< Fix status flags                  */
    uint8_t  flags2;     /**< Additional flags                  */
    uint8_t  numSV;      /**< Number of SVs used                */
    int32_t  lon;        /**< Longitude (degrees × 1e-7)        */
    int32_t  lat;        /**< Latitude (degrees × 1e-7)         */
    int32_t  height;     /**< Height above ellipsoid (mm)       */
    int32_t  hMSL;       /**< Height above MSL (mm)             */
    uint32_t hAcc;       /**< Horizontal accuracy (mm)          */
    uint32_t vAcc;       /**< Vertical accuracy (mm)            */
    int32_t  velN;       /**< NED north velocity (mm/s)         */
    int32_t  velE;       /**< NED east velocity (mm/s)          */
    int32_t  velD;       /**< NED down velocity (mm/s)          */
    int32_t  gSpeed;     /**< Ground speed (mm/s)               */
    int32_t  headMot;    /**< Heading of motion (degrees × 1e-5)*/
    uint32_t sAcc;       /**< Speed accuracy (mm/s)             */
    uint32_t headAcc;    /**< Heading accuracy (degrees × 1e-5) */
    uint16_t pDOP;       /**< Position DOP × 100                */
    uint8_t  flags3;     /**< Additional flags                  */
    uint8_t  reserved1[5];
    int32_t  headVeh;    /**< Heading of vehicle (degrees × 1e-5)*/
    int16_t  magDec;     /**< Magnetic declination (degrees × 1e-2)*/
    uint16_t magAcc;     /**< Magnetic declination accuracy     */
} ubx_nav_pvt_t;

/* ──────────────────────────── Parser State Machine ──────────────────── */

typedef enum {
    UBX_WAIT_SYNC1 = 0,
    UBX_WAIT_SYNC2,
    UBX_WAIT_CLASS,
    UBX_WAIT_ID,
    UBX_WAIT_LEN1,
    UBX_WAIT_LEN2,
    UBX_WAIT_PAYLOAD,
    UBX_WAIT_CK_A,
    UBX_WAIT_CK_B,
} ubx_parse_state_t;

#define UBX_MAX_PAYLOAD 256

typedef void (*ubx_nav_pvt_cb_t)(const ubx_nav_pvt_t *pvt);

typedef struct {
    ubx_parse_state_t state;
    uint8_t  msg_class;
    uint8_t  msg_id;
    uint16_t payload_len;
    uint16_t payload_idx;
    uint8_t  payload[UBX_MAX_PAYLOAD];
    uint8_t  ck_a;
    uint8_t  ck_b;
    uint8_t  rx_ck_a;
    uint8_t  rx_ck_b;

    /* Callbacks */
    ubx_nav_pvt_cb_t on_nav_pvt;

    /* Statistics */
    uint32_t msg_count;
    uint32_t checksum_errors;
} ubx_parser_t;

/* ──────────────────────────── Public API ──────────────────────────── */

/**
 * @brief Initialize the UBX parser.
 */
void ubx_parser_init(ubx_parser_t *parser);

/**
 * @brief Feed one byte into the parser state machine.
 * @param parser  Parser instance.
 * @param byte    Received byte.
 */
void ubx_parser_feed(ubx_parser_t *parser, uint8_t byte);

/**
 * @brief Set callback for NAV-PVT messages.
 */
void ubx_parser_set_nav_pvt_callback(ubx_parser_t *parser, ubx_nav_pvt_cb_t cb);

/**
 * @brief Calculate UBX checksum for a buffer.
 * @param data  Buffer (starting from class byte, not sync chars).
 * @param len   Number of bytes.
 * @param ck_a  Output: checksum A.
 * @param ck_b  Output: checksum B.
 */
void ubx_checksum(const uint8_t *data, uint16_t len, uint8_t *ck_a, uint8_t *ck_b);

#ifdef __cplusplus
}
#endif

#endif /* UBX_PARSER_H */
