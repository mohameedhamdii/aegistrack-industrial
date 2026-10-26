/**
 * @file nbiot_driver.h
 * @brief SARA-R410M NB-IoT / LTE-M Modem Driver
 *
 * AT command interface over UART for the u-blox SARA-R410M modem.
 * Handles registration, UDP socket communication, and Power Saving Mode.
 */

#ifndef NBIOT_DRIVER_H
#define NBIOT_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ──────────────────────────── Status Types ──────────────────────────── */

typedef enum {
    NBIOT_TX_IDLE     = 0,
    NBIOT_TX_BUSY     = 1,
    NBIOT_TX_SUCCESS  = 2,
    NBIOT_TX_FAILED   = 3,
} nbiot_tx_status_t;

typedef enum {
    NBIOT_REG_NONE       = 0,
    NBIOT_REG_SEARCHING  = 1,
    NBIOT_REG_REGISTERED = 2,
    NBIOT_REG_DENIED     = 3,
    NBIOT_REG_ROAMING    = 4,
} nbiot_reg_status_t;

/* ──────────────────────────── Configuration ──────────────────────────── */

typedef struct {
    const char *apn;              /**< APN string (e.g., "iot.1nce.net")  */
    const char *server_ip;        /**< Backend server IP                  */
    uint16_t    server_port;      /**< Backend server UDP port            */
    uint32_t    reg_timeout_ms;   /**< Registration timeout (60000 ms)    */
    uint32_t    tx_timeout_ms;    /**< TX timeout (30000 ms)              */
} nbiot_config_t;

/* ──────────────────────────── Public API ──────────────────────────── */

/**
 * @brief Initialize the NB-IoT modem driver.
 * @param cfg  Modem configuration.
 * @return 0 on success, -1 on failure.
 */
int nbiot_init(const nbiot_config_t *cfg);

/**
 * @brief Start modem registration sequence.
 *        Non-blocking — poll status with nbiot_get_reg_status().
 * @return 0 if sequence started, -1 on failure.
 */
int nbiot_connect(void);

/**
 * @brief Get network registration status.
 */
nbiot_reg_status_t nbiot_get_reg_status(void);

/**
 * @brief Send a data packet over UDP.
 *        Non-blocking — poll status with nbiot_get_tx_status().
 * @param data  Data buffer.
 * @param len   Data length (max 512 bytes).
 * @return 0 if send started, -1 on failure.
 */
int nbiot_send(const uint8_t *data, uint16_t len);

/**
 * @brief Get transmission status.
 */
nbiot_tx_status_t nbiot_get_tx_status(void);

/**
 * @brief Check for and receive downlink data.
 * @param buf   Output buffer.
 * @param max_len  Buffer size.
 * @return Number of bytes received, 0 if none, -1 on error.
 */
int nbiot_receive(uint8_t *buf, uint16_t max_len);

/**
 * @brief Enter Power Saving Mode (PSM).
 *        Configures T3324 and T3412 timers.
 * @return 0 on success, -1 on failure.
 */
int nbiot_enter_psm(void);

/**
 * @brief Process modem UART data. Call from UART RX ISR.
 * @param data  Received bytes.
 * @param len   Number of bytes.
 */
void nbiot_process_rx(const uint8_t *data, uint16_t len);

/**
 * @brief Get signal quality (RSRP in dBm).
 * @return RSRP value, or -999 if unknown.
 */
int16_t nbiot_get_rsrp(void);

/**
 * @brief Power down the modem completely.
 */
void nbiot_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* NBIOT_DRIVER_H */
