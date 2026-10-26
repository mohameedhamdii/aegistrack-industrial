/**
 * @file packet.c
 * @brief CBOR Packet Encoding Implementation
 *
 * Lightweight CBOR encoder — no external library needed.
 * Implements a minimal subset of CBOR (RFC 7049) sufficient
 * for encoding the tracker telemetry packet.
 *
 * CBOR types used:
 *   - Unsigned integer (major type 0)
 *   - Negative integer (major type 1)
 *   - Text string (major type 3)
 *   - Map (major type 5)
 */

#include "packet.h"
#include <string.h>

/* ──────────────────────────── Sequence Counter ──────────────────────── */

static uint32_t s_sequence = 0;

/* ──────────────────────────── CBOR Encoding Helpers ──────────────────── */

/**
 * CBOR major types:
 *   0: unsigned integer
 *   1: negative integer
 *   2: byte string
 *   3: text string
 *   5: map
 */

static int cbor_encode_uint(uint8_t *buf, int offset, int max_len, uint8_t major, uint32_t val)
{
    uint8_t mt = (major << 5);

    if (val <= 23) {
        if (offset + 1 > max_len) return -1;
        buf[offset++] = mt | (uint8_t)val;
    } else if (val <= 0xFF) {
        if (offset + 2 > max_len) return -1;
        buf[offset++] = mt | 24;
        buf[offset++] = (uint8_t)val;
    } else if (val <= 0xFFFF) {
        if (offset + 3 > max_len) return -1;
        buf[offset++] = mt | 25;
        buf[offset++] = (uint8_t)(val >> 8);
        buf[offset++] = (uint8_t)(val);
    } else {
        if (offset + 5 > max_len) return -1;
        buf[offset++] = mt | 26;
        buf[offset++] = (uint8_t)(val >> 24);
        buf[offset++] = (uint8_t)(val >> 16);
        buf[offset++] = (uint8_t)(val >> 8);
        buf[offset++] = (uint8_t)(val);
    }
    return offset;
}

static int cbor_encode_int(uint8_t *buf, int offset, int max_len, int32_t val)
{
    if (val >= 0) {
        return cbor_encode_uint(buf, offset, max_len, 0, (uint32_t)val);
    } else {
        return cbor_encode_uint(buf, offset, max_len, 1, (uint32_t)(-(val + 1)));
    }
}

static int cbor_encode_text(uint8_t *buf, int offset, int max_len, const char *str)
{
    uint16_t len = (uint16_t)strlen(str);
    offset = cbor_encode_uint(buf, offset, max_len, 3, len);
    if (offset < 0) return -1;
    if (offset + len > max_len) return -1;
    memcpy(buf + offset, str, len);
    return offset + len;
}

static int cbor_encode_map_start(uint8_t *buf, int offset, int max_len, uint32_t items)
{
    return cbor_encode_uint(buf, offset, max_len, 5, items);
}

/* Helper: encode a key-value pair (string key, int32 value) */
static int cbor_kv_int(uint8_t *buf, int off, int max, const char *key, int32_t val)
{
    off = cbor_encode_text(buf, off, max, key);
    if (off < 0) return -1;
    off = cbor_encode_int(buf, off, max, val);
    return off;
}

/* Helper: encode a key-value pair (string key, uint32 value) */
static int cbor_kv_uint(uint8_t *buf, int off, int max, const char *key, uint32_t val)
{
    off = cbor_encode_text(buf, off, max, key);
    if (off < 0) return -1;
    off = cbor_encode_uint(buf, off, max, 0, val);
    return off;
}

/* Helper: encode a key-value pair (string key, string value) */
static int cbor_kv_str(uint8_t *buf, int off, int max, const char *key, const char *val)
{
    off = cbor_encode_text(buf, off, max, key);
    if (off < 0) return -1;
    off = cbor_encode_text(buf, off, max, val);
    return off;
}

/* ──────────────────────────── Public API ──────────────────────────── */

int packet_encode_cbor(const tracker_packet_t *pkt, uint8_t *buf, uint16_t max_len)
{
    int off = 0;

    /* Map with 15 key-value pairs */
    off = cbor_encode_map_start(buf, off, max_len, 15);
    if (off < 0) return -1;

    /* "id": tracker ID string */
    off = cbor_kv_str(buf, off, max_len, "id", pkt->tracker_id);
    if (off < 0) return -1;

    /* "ts": timestamp */
    off = cbor_kv_uint(buf, off, max_len, "ts", pkt->timestamp);
    if (off < 0) return -1;

    /* "lat": latitude × 1e7 (integer for compact encoding) */
    off = cbor_kv_int(buf, off, max_len, "lat", pkt->latitude);
    if (off < 0) return -1;

    /* "lon": longitude × 1e7 */
    off = cbor_kv_int(buf, off, max_len, "lon", pkt->longitude);
    if (off < 0) return -1;

    /* "alt": altitude in mm */
    off = cbor_kv_int(buf, off, max_len, "alt", pkt->altitude_mm);
    if (off < 0) return -1;

    /* "spd": speed in mm/s */
    off = cbor_kv_uint(buf, off, max_len, "spd", pkt->speed_mm_s);
    if (off < 0) return -1;

    /* "hdg": heading × 1e5 */
    off = cbor_kv_int(buf, off, max_len, "hdg", pkt->heading_1e5);
    if (off < 0) return -1;

    /* "bat": battery mV */
    off = cbor_kv_uint(buf, off, max_len, "bat", pkt->battery_mv);
    if (off < 0) return -1;

    /* "temp": temperature °C */
    off = cbor_kv_int(buf, off, max_len, "temp", (int32_t)pkt->temperature);
    if (off < 0) return -1;

    /* "fix": fix type */
    off = cbor_kv_uint(buf, off, max_len, "fix", pkt->fix_type);
    if (off < 0) return -1;

    /* "hdop": HDOP × 100 */
    off = cbor_kv_uint(buf, off, max_len, "hdop", pkt->hdop_100);
    if (off < 0) return -1;

    /* "geo_evt": geofence event */
    off = cbor_kv_uint(buf, off, max_len, "geo_evt", pkt->geofence_event);
    if (off < 0) return -1;

    /* "geo_zone": geofence zone ID */
    off = cbor_kv_uint(buf, off, max_len, "geo_zone", pkt->geofence_zone);
    if (off < 0) return -1;

    /* "anomaly": anomaly flags */
    off = cbor_kv_uint(buf, off, max_len, "anomaly", pkt->anomaly_flags);
    if (off < 0) return -1;

    /* "seq": sequence number */
    off = cbor_kv_uint(buf, off, max_len, "seq", pkt->sequence);
    if (off < 0) return -1;

    return off;  /* Total encoded length */
}

static int cbor_decode_head(const uint8_t *buf, int *offset, int len, uint8_t *major, uint64_t *val)
{
    if (*offset >= len) return -1;
    uint8_t b = buf[(*offset)++];
    *major = b >> 5;
    uint8_t info = b & 0x1F;

    if (info <= 23) {
        *val = info;
    } else if (info == 24) {
        if (*offset + 1 > len) return -1;
        *val = buf[(*offset)++];
    } else if (info == 25) {
        if (*offset + 2 > len) return -1;
        *val = ((uint64_t)buf[*offset] << 8) | buf[*offset + 1];
        *offset += 2;
    } else if (info == 26) {
        if (*offset + 4 > len) return -1;
        *val = ((uint64_t)buf[*offset] << 24) |
               ((uint64_t)buf[*offset + 1] << 16) |
               ((uint64_t)buf[*offset + 2] << 8) |
               buf[*offset + 3];
        *offset += 4;
    } else {
        return -1;
    }
    return 0;
}

int packet_decode_cbor(const uint8_t *buf, uint16_t len, tracker_packet_t *pkt)
{
    if (!buf || !pkt || len == 0) return -1;
    memset(pkt, 0, sizeof(tracker_packet_t));

    int off = 0;
    uint8_t major;
    uint64_t map_items;

    if (cbor_decode_head(buf, &off, len, &major, &map_items) < 0 || major != 5) {
        return -1;
    }

    for (uint64_t i = 0; i < map_items; i++) {
        uint64_t klen;
        if (cbor_decode_head(buf, &off, len, &major, &klen) < 0 || major != 3) {
            return -1;
        }
        if (off + (int)klen > len || klen >= 32) return -1;

        char key[32];
        memcpy(key, buf + off, klen);
        key[klen] = '\0';
        off += (int)klen;

        uint64_t num_val;
        if (cbor_decode_head(buf, &off, len, &major, &num_val) < 0) {
            return -1;
        }

        if (strcmp(key, "id") == 0 && major == 3) {
            if (off + (int)num_val > len || num_val >= sizeof(pkt->tracker_id)) return -1;
            memcpy(pkt->tracker_id, buf + off, num_val);
            pkt->tracker_id[num_val] = '\0';
            off += (int)num_val;
            continue;
        }

        int32_t sval = (major == 1) ? (int32_t)(-(int64_t)num_val - 1) : (int32_t)num_val;

        if (strcmp(key, "ts") == 0)             pkt->timestamp = (uint32_t)num_val;
        else if (strcmp(key, "lat") == 0)       pkt->latitude = sval;
        else if (strcmp(key, "lon") == 0)       pkt->longitude = sval;
        else if (strcmp(key, "alt") == 0)       pkt->altitude_mm = sval;
        else if (strcmp(key, "spd") == 0)       pkt->speed_mm_s = (uint32_t)num_val;
        else if (strcmp(key, "hdg") == 0)       pkt->heading_1e5 = sval;
        else if (strcmp(key, "bat") == 0)       pkt->battery_mv = (uint16_t)num_val;
        else if (strcmp(key, "temp") == 0)      pkt->temperature = (int8_t)sval;
        else if (strcmp(key, "fix") == 0)       pkt->fix_type = (uint8_t)num_val;
        else if (strcmp(key, "hdop") == 0)      pkt->hdop_100 = (uint16_t)num_val;
        else if (strcmp(key, "geo_evt") == 0)   pkt->geofence_event = (uint8_t)num_val;
        else if (strcmp(key, "geo_zone") == 0)  pkt->geofence_zone = (uint8_t)num_val;
        else if (strcmp(key, "anomaly") == 0)   pkt->anomaly_flags = (uint8_t)num_val;
        else if (strcmp(key, "seq") == 0)       pkt->sequence = (uint32_t)num_val;
    }

    return 0;
}

uint32_t packet_next_sequence(void)
{
    return s_sequence++;
}
