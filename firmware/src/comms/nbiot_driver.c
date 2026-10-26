/**
 * @file nbiot_driver.c
 * @brief SARA-R410M NB-IoT Modem Driver Implementation
 *
 * Implements AT command sequences for the u-blox SARA-R410M modem:
 *   1. Power ON via load switch
 *   2. Wait for +CEREG: 1 or 5 (registered/roaming)
 *   3. Open UDP socket
 *   4. Send CBOR data packet
 *   5. Receive downlink commands
 *   6. Enter PSM
 */

#include "nbiot_driver.h"
#include "../config/comms_config.h"
#include "../config/pin_map.h"
#include "stm32l4xx_hal.h"

#include <string.h>
#include <stdio.h>

/* ──────────────────────────── Constants ──────────────────────────── */

#define MODEM_UART_INSTANCE   USART1
#define MODEM_UART_BAUDRATE   115200
#define MODEM_RX_BUF_SIZE     512
#define AT_CMD_MAX_LEN        256
#define AT_RESP_MAX_LEN       512
#define AT_TIMEOUT_MS         5000
#define SOCKET_ID             0

/* ──────────────────────────── AT Command Sequences ──────────────────── */

/* PSM config: T3324 = 00100001 (2 s active timer)
 *             T3412 = 10100101 (6 hours periodic TAU) */
#define AT_PSM_CONFIG   "AT+CPSMS=1,,,\"10100101\",\"00100001\"\r\n"

/* ──────────────────────────── Private State ──────────────────────────── */

static UART_HandleTypeDef s_modem_uart;
static nbiot_config_t     s_config;
static nbiot_tx_status_t  s_tx_status  = NBIOT_TX_IDLE;
static nbiot_reg_status_t s_reg_status = NBIOT_REG_NONE;
static int16_t            s_rsrp       = -999;

static uint8_t  s_rx_buf[MODEM_RX_BUF_SIZE];
static uint16_t s_rx_idx = 0;
static char     s_at_resp[AT_RESP_MAX_LEN];
static volatile bool s_resp_ready = false;

/* ──────────────────────────── UART Helpers ──────────────────────────── */

static int modem_uart_init(void)
{
    s_modem_uart.Instance          = MODEM_UART_INSTANCE;
    s_modem_uart.Init.BaudRate     = MODEM_UART_BAUDRATE;
    s_modem_uart.Init.WordLength   = UART_WORDLENGTH_8B;
    s_modem_uart.Init.StopBits     = UART_STOPBITS_1;
    s_modem_uart.Init.Parity       = UART_PARITY_NONE;
    s_modem_uart.Init.Mode         = UART_MODE_TX_RX;
    s_modem_uart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    s_modem_uart.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&s_modem_uart) != HAL_OK) {
        return -1;
    }

    HAL_UART_Receive_IT(&s_modem_uart, s_rx_buf, 1);
    return 0;
}

static void at_send(const char *cmd)
{
    s_resp_ready = false;
    s_rx_idx = 0;
    memset(s_at_resp, 0, sizeof(s_at_resp));
    HAL_UART_Transmit(&s_modem_uart, (uint8_t *)cmd, (uint16_t)strlen(cmd), 1000);
}

static int at_wait_response(const char *expected, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms) {
        if (s_resp_ready) {
            if (strstr(s_at_resp, expected) != NULL) {
                return 0;  /* Found expected response */
            }
            if (strstr(s_at_resp, "ERROR") != NULL) {
                return -1;  /* AT error */
            }
            s_resp_ready = false;
        }
        HAL_Delay(10);
    }
    return -2;  /* Timeout */
}

static int at_send_wait(const char *cmd, const char *expected, uint32_t timeout_ms)
{
    at_send(cmd);
    return at_wait_response(expected, timeout_ms);
}

/* ──────────────────────────── Public API ──────────────────────────── */

int nbiot_init(const nbiot_config_t *cfg)
{
    memcpy(&s_config, cfg, sizeof(nbiot_config_t));

    if (modem_uart_init() != 0) {
        return -1;
    }

    /* Basic modem initialization */
    HAL_Delay(2000);  /* Wait for modem boot */

    /* Disable echo */
    if (at_send_wait("ATE0\r\n", "OK", AT_TIMEOUT_MS) != 0) {
        return -1;
    }

    /* Check modem presence */
    if (at_send_wait("AT\r\n", "OK", AT_TIMEOUT_MS) != 0) {
        return -1;
    }

    /* Set APN */
    char cmd[AT_CMD_MAX_LEN];
    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"\r\n", s_config.apn);
    if (at_send_wait(cmd, "OK", AT_TIMEOUT_MS) != 0) {
        return -1;
    }

    /* Configure PSM */
    if (at_send_wait(AT_PSM_CONFIG, "OK", AT_TIMEOUT_MS) != 0) {
        /* PSM config failed — not fatal, continue without PSM */
    }

    /* Enable network registration URC */
    at_send_wait("AT+CEREG=1\r\n", "OK", AT_TIMEOUT_MS);

    return 0;
}

int nbiot_connect(void)
{
    s_reg_status = NBIOT_REG_SEARCHING;

    /* Trigger registration */
    at_send("AT+CFUN=1\r\n");

    /* Poll for registration with timeout */
    uint32_t start = HAL_GetTick();
    uint32_t timeout = s_config.reg_timeout_ms > 0 ? s_config.reg_timeout_ms : 60000;

    while ((HAL_GetTick() - start) < timeout) {
        at_send("AT+CEREG?\r\n");
        HAL_Delay(500);

        if (s_resp_ready) {
            /* Parse +CEREG: <n>,<stat> */
            char *p = strstr(s_at_resp, "+CEREG:");
            if (p != NULL) {
                int n, stat;
                if (sscanf(p, "+CEREG: %d,%d", &n, &stat) >= 2) {
                    if (stat == 1) {
                        s_reg_status = NBIOT_REG_REGISTERED;
                        return 0;
                    } else if (stat == 5) {
                        s_reg_status = NBIOT_REG_ROAMING;
                        return 0;
                    } else if (stat == 3) {
                        s_reg_status = NBIOT_REG_DENIED;
                        return -1;
                    }
                }
            }
            s_resp_ready = false;
        }

        HAL_Delay(2000);
    }

    s_reg_status = NBIOT_REG_NONE;
    return -1;  /* Timeout */
}

nbiot_reg_status_t nbiot_get_reg_status(void)
{
    return s_reg_status;
}

int nbiot_send(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0 || len > 512) {
        return -1;
    }

    s_tx_status = NBIOT_TX_BUSY;

    /* Create UDP socket */
    char cmd[AT_CMD_MAX_LEN];
    snprintf(cmd, sizeof(cmd), "AT+USOCR=17\r\n");
    if (at_send_wait(cmd, "OK", AT_TIMEOUT_MS) != 0) {
        s_tx_status = NBIOT_TX_FAILED;
        return -1;
    }

    /* Send data via UDP */
    snprintf(cmd, sizeof(cmd), "AT+USOST=%d,\"%s\",%u,%u\r\n",
             SOCKET_ID, s_config.server_ip, s_config.server_port, len);
    at_send(cmd);

    /* Wait for prompt ">" then send binary data */
    HAL_Delay(100);
    HAL_UART_Transmit(&s_modem_uart, (uint8_t *)data, len, 5000);

    if (at_wait_response("OK", s_config.tx_timeout_ms) == 0) {
        s_tx_status = NBIOT_TX_SUCCESS;
    } else {
        s_tx_status = NBIOT_TX_FAILED;
    }

    /* Close socket */
    snprintf(cmd, sizeof(cmd), "AT+USOCL=%d\r\n", SOCKET_ID);
    at_send_wait(cmd, "OK", AT_TIMEOUT_MS);

    return (s_tx_status == NBIOT_TX_SUCCESS) ? 0 : -1;
}

nbiot_tx_status_t nbiot_get_tx_status(void)
{
    return s_tx_status;
}

int nbiot_receive(uint8_t *buf, uint16_t max_len)
{
    /* Check for pending data on the socket */
    char cmd[AT_CMD_MAX_LEN];
    snprintf(cmd, sizeof(cmd), "AT+USORF=%d,%u\r\n", SOCKET_ID, max_len);
    at_send(cmd);

    if (at_wait_response("+USORF:", 5000) == 0) {
        /* Parse response: +USORF: <socket>,\"<ip>\",<port>,<length>,\"<data>\" */
        char *p = strstr(s_at_resp, "+USORF:");
        if (p != NULL) {
            int sock, port, dlen;
            char ip[32];
            if (sscanf(p, "+USORF: %d,\"%31[^\"]\",%d,%d,", &sock, ip, &port, &dlen) >= 4) {
                if (dlen > 0 && dlen <= max_len) {
                    /* Find the data portion after the last comma and quote */
                    char *data_start = strrchr(p, '\"');
                    if (data_start != NULL) {
                        data_start++;  /* Skip opening quote */
                        memcpy(buf, data_start, (uint16_t)dlen);
                        return dlen;
                    }
                }
            }
        }
    }
    return 0;  /* No data */
}

int nbiot_enter_psm(void)
{
    return at_send_wait(AT_PSM_CONFIG, "OK", AT_TIMEOUT_MS);
}

void nbiot_process_rx(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        if (s_rx_idx < AT_RESP_MAX_LEN - 1) {
            s_at_resp[s_rx_idx++] = (char)data[i];
            s_at_resp[s_rx_idx]   = '\0';

            /* Check for line termination */
            if (data[i] == '\n') {
                s_resp_ready = true;
            }
        }
    }
}

int16_t nbiot_get_rsrp(void)
{
    at_send("AT+CESQ\r\n");
    if (at_wait_response("+CESQ:", 3000) == 0) {
        char *p = strstr(s_at_resp, "+CESQ:");
        if (p != NULL) {
            int rxlev, ber, rscp, ecno, rsrq, rsrp_idx;
            if (sscanf(p, "+CESQ: %d,%d,%d,%d,%d,%d",
                       &rxlev, &ber, &rscp, &ecno, &rsrq, &rsrp_idx) >= 6) {
                /* Convert index to dBm: RSRP = index - 141 */
                if (rsrp_idx != 255) {
                    s_rsrp = (int16_t)(rsrp_idx - 141);
                }
            }
        }
    }
    return s_rsrp;
}

void nbiot_shutdown(void)
{
    at_send_wait("AT+CPWROFF\r\n", "OK", 5000);
    HAL_UART_DeInit(&s_modem_uart);
}

/* ──────────────────────────── UART RX Interrupt ──────────────────────── */

void USART1_IRQHandler_Modem(void)
{
    if (s_modem_uart.Instance == MODEM_UART_INSTANCE) {
        nbiot_process_rx(s_rx_buf, 1);
        HAL_UART_Receive_IT(&s_modem_uart, s_rx_buf, 1);
    }
}
