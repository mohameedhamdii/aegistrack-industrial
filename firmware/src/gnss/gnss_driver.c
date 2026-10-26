/**
 * @file gnss_driver.c
 * @brief u-blox MAX-M10S GNSS Driver Implementation
 *
 * Handles UART communication with the u-blox GNSS module using
 * the binary UBX protocol. Supports NAV-PVT parsing for position,
 * velocity, and time data.
 */

#include "gnss_driver.h"
#include "ubx_parser.h"
#include "ephemeris_cache.h"
#include "../config/pin_map.h"
#include "stm32l4xx_hal.h"

#include <string.h>

/* ──────────────────────────── Configuration ──────────────────────────── */

#define GNSS_UART_INSTANCE  USART2
#define GNSS_UART_BAUDRATE  9600
#define GNSS_RX_BUF_SIZE    256
#define GNSS_FIX_TIMEOUT_S  60

/* ──────────────────────────── UBX Configuration Messages ──────────────── */

/* UBX-CFG-PRT: Configure UART1 for UBX protocol only (disable NMEA) */
static const uint8_t UBX_CFG_PRT[] = {
    0xB5, 0x62,             /* Sync chars           */
    0x06, 0x00,             /* Class: CFG, ID: PRT  */
    0x14, 0x00,             /* Length: 20 bytes      */
    0x01,                   /* Port ID: UART1       */
    0x00,                   /* Reserved             */
    0x00, 0x00,             /* txReady              */
    0xD0, 0x08, 0x00, 0x00, /* mode: 8N1            */
    0x80, 0x25, 0x00, 0x00, /* baudRate: 9600       */
    0x01, 0x00,             /* inProtoMask: UBX     */
    0x01, 0x00,             /* outProtoMask: UBX    */
    0x00, 0x00,             /* flags                */
    0x00, 0x00,             /* reserved2            */
    0xB6, 0x4F              /* CK_A, CK_B           */
};

/* UBX-CFG-MSG: Enable NAV-PVT on UART1 (rate=1) */
static const uint8_t UBX_CFG_MSG_NAV_PVT[] = {
    0xB5, 0x62,
    0x06, 0x01,             /* Class: CFG, ID: MSG  */
    0x08, 0x00,             /* Length: 8 bytes      */
    0x01, 0x07,             /* NAV-PVT              */
    0x00,                   /* DDC rate             */
    0x01,                   /* UART1 rate = 1       */
    0x00,                   /* UART2 rate           */
    0x00,                   /* USB rate             */
    0x00,                   /* SPI rate             */
    0x00,                   /* reserved             */
    0x18, 0xE1              /* CK_A, CK_B           */
};

/* UBX-CFG-PM2: Power management — 1 Hz cyclic tracking */
static const uint8_t UBX_CFG_PM2[] = {
    0xB5, 0x62,
    0x06, 0x3B,             /* Class: CFG, ID: PM2  */
    0x2C, 0x00,             /* Length: 44 bytes     */
    0x02,                   /* Version              */
    0x00,                   /* Reserved             */
    0x00,                   /* maxStartupStateDur   */
    0x00,                   /* Reserved             */
    0x00, 0x09, 0x10, 0x00, /* flags: update RTC, update EPH, cyclic */
    0xE8, 0x03, 0x00, 0x00, /* updatePeriod: 1000 ms */
    0xE8, 0x03, 0x00, 0x00, /* searchPeriod: 1000 ms */
    0x00, 0x00, 0x00, 0x00, /* gridOffset          */
    0x00, 0x00,             /* onTime               */
    0x01, 0x00,             /* minAcqTime: 1 s     */
    0x00, 0x00, 0x00, 0x00, /* reserved             */
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x86, 0xEE              /* CK_A, CK_B           */
};

/* ──────────────────────────── Private State ──────────────────────────── */

static UART_HandleTypeDef s_huart;
static gnss_fix_t         s_latest_fix;
static bool               s_fix_valid = false;
static uint32_t           s_fix_start_tick = 0;
static uint8_t            s_rx_buf[GNSS_RX_BUF_SIZE];
static ubx_parser_t       s_parser;

/* ──────────────────────────── UART Helpers ──────────────────────────── */

static int gnss_uart_init(void)
{
    s_huart.Instance          = GNSS_UART_INSTANCE;
    s_huart.Init.BaudRate     = GNSS_UART_BAUDRATE;
    s_huart.Init.WordLength   = UART_WORDLENGTH_8B;
    s_huart.Init.StopBits     = UART_STOPBITS_1;
    s_huart.Init.Parity       = UART_PARITY_NONE;
    s_huart.Init.Mode         = UART_MODE_TX_RX;
    s_huart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    s_huart.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&s_huart) != HAL_OK) {
        return -1;
    }

    /* Start receiving in interrupt mode */
    HAL_UART_Receive_IT(&s_huart, s_rx_buf, 1);

    return 0;
}

static void gnss_uart_send(const uint8_t *data, uint16_t len)
{
    HAL_UART_Transmit(&s_huart, (uint8_t *)data, len, 1000);
}

/* ──────────────────────────── UBX Callback ──────────────────────────── */

/**
 * Called by the UBX parser when a complete NAV-PVT message is received.
 */
static void on_nav_pvt(const ubx_nav_pvt_t *pvt)
{
    s_latest_fix.fix_type = (gnss_fix_status_t)pvt->fixType;

    if (pvt->fixType >= 2) {
        s_latest_fix.latitude     = pvt->lat;
        s_latest_fix.longitude    = pvt->lon;
        s_latest_fix.altitude_mm  = pvt->hMSL;
        s_latest_fix.h_acc_mm     = pvt->hAcc;
        s_latest_fix.v_acc_mm     = pvt->vAcc;
        s_latest_fix.vel_n_mm_s   = pvt->velN;
        s_latest_fix.vel_e_mm_s   = pvt->velE;
        s_latest_fix.vel_d_mm_s   = pvt->velD;
        s_latest_fix.ground_speed = pvt->gSpeed;
        s_latest_fix.heading      = pvt->headMot;
        s_latest_fix.num_sv       = pvt->numSV;
        s_latest_fix.pdop         = pvt->pDOP;

        s_latest_fix.year  = pvt->year;
        s_latest_fix.month = pvt->month;
        s_latest_fix.day   = pvt->day;
        s_latest_fix.hour  = pvt->hour;
        s_latest_fix.min   = pvt->min;
        s_latest_fix.sec   = pvt->sec;

        /* Compute Unix epoch (simplified — accurate for 2000-2099) */
        /* Full implementation would use a proper date-to-epoch conversion */
        s_latest_fix.valid_time = pvt->valid & 0x03;

        s_fix_valid = true;
    }
}

/* ──────────────────────────── Public API ──────────────────────────── */

int gnss_init(void)
{
    /* Initialize UBX parser */
    ubx_parser_init(&s_parser);
    ubx_parser_set_nav_pvt_callback(&s_parser, on_nav_pvt);

    /* Initialize UART */
    if (gnss_uart_init() != 0) {
        return -1;
    }

    /* Send UBX configuration: UBX-only mode, enable NAV-PVT */
    gnss_uart_send(UBX_CFG_PRT, sizeof(UBX_CFG_PRT));
    HAL_Delay(100);

    gnss_uart_send(UBX_CFG_MSG_NAV_PVT, sizeof(UBX_CFG_MSG_NAV_PVT));
    HAL_Delay(50);

    gnss_uart_send(UBX_CFG_PM2, sizeof(UBX_CFG_PM2));
    HAL_Delay(50);

    return 0;
}

int gnss_start_fix(void)
{
    s_fix_valid      = false;
    s_fix_start_tick = HAL_GetTick();

    /* Inject cached ephemeris if available for faster TTFF */
    if (ephemeris_cache_valid()) {
        const uint8_t *eph_data;
        uint32_t eph_len;
        if (ephemeris_cache_get(&eph_data, &eph_len) == 0) {
            gnss_inject_ephemeris(eph_data, eph_len);
        }
    }

    return 0;
}

gnss_fix_status_t gnss_get_fix_status(void)
{
    return s_latest_fix.fix_type;
}

int gnss_get_fix(gnss_fix_t *fix)
{
    if (!s_fix_valid || fix == NULL) {
        return -1;
    }
    memcpy(fix, &s_latest_fix, sizeof(gnss_fix_t));
    return 0;
}

void gnss_process_rx(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        ubx_parser_feed(&s_parser, data[i]);
    }
}

void gnss_shutdown(void)
{
    /* Save ephemeris data before power-down */
    /* UBX-MGA-DBD: request navigation database dump */
    static const uint8_t UBX_MGA_DBD_POLL[] = {
        0xB5, 0x62, 0x13, 0x80, 0x00, 0x00, 0x93, 0xCC
    };
    gnss_uart_send(UBX_MGA_DBD_POLL, sizeof(UBX_MGA_DBD_POLL));
    HAL_Delay(500);  /* Allow time for database dump */

    /* De-init UART */
    HAL_UART_DeInit(&s_huart);
}

int gnss_inject_ephemeris(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return -1;
    }

    /* Send AssistNow data in chunks (max 512 bytes per UBX message) */
    const uint32_t CHUNK_SIZE = 512;
    uint32_t offset = 0;

    while (offset < len) {
        uint32_t chunk_len = (len - offset > CHUNK_SIZE) ? CHUNK_SIZE : (len - offset);
        gnss_uart_send(data + offset, (uint16_t)chunk_len);
        HAL_Delay(10);  /* Brief delay between chunks */
        offset += chunk_len;
    }

    return 0;
}

bool gnss_ephemeris_valid(void)
{
    return ephemeris_cache_valid();
}

/* ──────────────────────────── UART RX Interrupt Handler ──────────────── */

/**
 * HAL UART RX complete callback — feed received byte to UBX parser.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == GNSS_UART_INSTANCE) {
        ubx_parser_feed(&s_parser, s_rx_buf[0]);

        /* Re-arm receive for next byte */
        HAL_UART_Receive_IT(huart, s_rx_buf, 1);
    }
}
