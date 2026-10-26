/**
 * @file ubx_parser.c
 * @brief u-blox UBX Binary Protocol Parser Implementation
 *
 * Byte-by-byte state machine parser for UBX messages.
 * Handles sync detection, checksum verification, and payload dispatch.
 */

#include "ubx_parser.h"
#include <string.h>

/* ──────────────────────────── Checksum ──────────────────────────── */

void ubx_checksum(const uint8_t *data, uint16_t len, uint8_t *ck_a, uint8_t *ck_b)
{
    *ck_a = 0;
    *ck_b = 0;
    for (uint16_t i = 0; i < len; i++) {
        *ck_a += data[i];
        *ck_b += *ck_a;
    }
}

/* ──────────────────────────── Message Dispatch ──────────────────────── */

static void dispatch_message(ubx_parser_t *parser)
{
    parser->msg_count++;

    if (parser->msg_class == UBX_CLASS_NAV && parser->msg_id == UBX_NAV_PVT) {
        if (parser->on_nav_pvt != NULL && parser->payload_len >= sizeof(ubx_nav_pvt_t)) {
            const ubx_nav_pvt_t *pvt = (const ubx_nav_pvt_t *)parser->payload;
            parser->on_nav_pvt(pvt);
        }
    }
    /* Additional message handlers can be added here:
     * - NAV-STATUS for fix status
     * - ACK-ACK/NAK for configuration acknowledgement
     * - MGA-DBD for ephemeris database
     */
}

/* ──────────────────────────── Public API ──────────────────────────── */

void ubx_parser_init(ubx_parser_t *parser)
{
    memset(parser, 0, sizeof(ubx_parser_t));
    parser->state = UBX_WAIT_SYNC1;
}

void ubx_parser_set_nav_pvt_callback(ubx_parser_t *parser, ubx_nav_pvt_cb_t cb)
{
    parser->on_nav_pvt = cb;
}

void ubx_parser_feed(ubx_parser_t *parser, uint8_t byte)
{
    switch (parser->state) {

    case UBX_WAIT_SYNC1:
        if (byte == UBX_SYNC1) {
            parser->state = UBX_WAIT_SYNC2;
        }
        break;

    case UBX_WAIT_SYNC2:
        if (byte == UBX_SYNC2) {
            parser->state = UBX_WAIT_CLASS;
            parser->ck_a  = 0;
            parser->ck_b  = 0;
        } else {
            parser->state = UBX_WAIT_SYNC1;
        }
        break;

    case UBX_WAIT_CLASS:
        parser->msg_class = byte;
        parser->ck_a += byte;
        parser->ck_b += parser->ck_a;
        parser->state = UBX_WAIT_ID;
        break;

    case UBX_WAIT_ID:
        parser->msg_id = byte;
        parser->ck_a += byte;
        parser->ck_b += parser->ck_a;
        parser->state = UBX_WAIT_LEN1;
        break;

    case UBX_WAIT_LEN1:
        parser->payload_len = byte;
        parser->ck_a += byte;
        parser->ck_b += parser->ck_a;
        parser->state = UBX_WAIT_LEN2;
        break;

    case UBX_WAIT_LEN2:
        parser->payload_len |= ((uint16_t)byte << 8);
        parser->ck_a += byte;
        parser->ck_b += parser->ck_a;
        parser->payload_idx = 0;

        if (parser->payload_len == 0) {
            parser->state = UBX_WAIT_CK_A;
        } else if (parser->payload_len > UBX_MAX_PAYLOAD) {
            /* Payload too large — skip this message */
            parser->state = UBX_WAIT_SYNC1;
        } else {
            parser->state = UBX_WAIT_PAYLOAD;
        }
        break;

    case UBX_WAIT_PAYLOAD:
        parser->payload[parser->payload_idx++] = byte;
        parser->ck_a += byte;
        parser->ck_b += parser->ck_a;

        if (parser->payload_idx >= parser->payload_len) {
            parser->state = UBX_WAIT_CK_A;
        }
        break;

    case UBX_WAIT_CK_A:
        parser->rx_ck_a = byte;
        parser->state = UBX_WAIT_CK_B;
        break;

    case UBX_WAIT_CK_B:
        parser->rx_ck_b = byte;

        /* Verify checksum */
        if (parser->ck_a == parser->rx_ck_a &&
            parser->ck_b == parser->rx_ck_b) {
            dispatch_message(parser);
        } else {
            parser->checksum_errors++;
        }

        parser->state = UBX_WAIT_SYNC1;
        break;
    }
}
