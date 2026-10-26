/**
 * @file packet.h
 * @brief CBOR Packet Encoding/Decoding for Asset Tracker
 *
 * Encodes tracker telemetry into compact CBOR packets (~60 bytes)
 * for efficient NB-IoT transmission.
 */

#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PACKET_MAX_SIZE  200

/**
 * @brief Tracker telemetry packet.
 */
typedef struct {
    char     tracker_id[32];
    uint32_t timestamp;
    int32_t  latitude;       /**< degrees × 1e7         */
    int32_t  longitude;      /**< degrees × 1e7         */
    int32_t  altitude_mm;    /**< mm above MSL           */
    uint32_t speed_mm_s;     /**< ground speed in mm/s   */
    int32_t  heading_1e5;    /**< heading × 1e5 degrees  */
    uint16_t battery_mv;     /**< battery voltage in mV  */
    int8_t   temperature;    /**< temperature in °C      */
    uint8_t  fix_type;       /**< GNSS fix type          */
    uint16_t hdop_100;       /**< HDOP × 100             */
    uint8_t  geofence_event; /**< 0=none,1=enter,2=exit,3=dwell */
    uint8_t  geofence_zone;  /**< zone ID                */
    uint8_t  anomaly_flags;  /**< anomaly bit flags      */
    uint32_t eta_minutes;    /**< ETA to next geofence   */
    uint32_t sequence;       /**< packet sequence number */
} tracker_packet_t;

/**
 * @brief Encode a tracker packet into CBOR format.
 * @param pkt     Input packet data.
 * @param buf     Output buffer for CBOR data.
 * @param max_len Buffer size.
 * @return Number of bytes encoded, or -1 on error.
 */
int packet_encode_cbor(const tracker_packet_t *pkt, uint8_t *buf, uint16_t max_len);

/**
 * @brief Decode a CBOR packet into tracker packet structure.
 * @param buf     Input CBOR data.
 * @param len     Input data length.
 * @param pkt     Output packet structure.
 * @return 0 on success, -1 on error.
 */
int packet_decode_cbor(const uint8_t *buf, uint16_t len, tracker_packet_t *pkt);

/**
 * @brief Get the next sequence number (auto-incrementing, persistent).
 * @return Next sequence number.
 */
uint32_t packet_next_sequence(void);

#ifdef __cplusplus
}
#endif

#endif /* PACKET_H */
