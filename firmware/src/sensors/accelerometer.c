/**
 * @file accelerometer.c
 * @brief LIS2DW12 Accelerometer Driver Implementation
 *
 * Communicates with the LIS2DW12 via SPI. Configures low-power mode
 * with wake-up interrupt on INT1 for motion detection.
 *
 * Register map reference: LIS2DW12 datasheet (ST)
 */

#include "accelerometer.h"
#include "../config/pin_map.h"
#include "stm32l4xx_hal.h"

#include <string.h>

/* ──────────────────────────── LIS2DW12 Registers ──────────────────── */

#define LIS2DW12_WHO_AM_I       0x0F
#define LIS2DW12_CTRL1          0x20
#define LIS2DW12_CTRL2          0x21
#define LIS2DW12_CTRL3          0x22
#define LIS2DW12_CTRL4_INT1     0x23
#define LIS2DW12_CTRL5_INT2     0x24
#define LIS2DW12_CTRL6          0x25
#define LIS2DW12_OUT_T          0x26
#define LIS2DW12_STATUS         0x27
#define LIS2DW12_OUT_X_L        0x28
#define LIS2DW12_OUT_X_H        0x29
#define LIS2DW12_OUT_Y_L        0x2A
#define LIS2DW12_OUT_Y_H        0x2B
#define LIS2DW12_OUT_Z_L        0x2C
#define LIS2DW12_OUT_Z_H        0x2D
#define LIS2DW12_WAKE_UP_THS    0x34
#define LIS2DW12_WAKE_UP_DUR    0x35
#define LIS2DW12_WAKE_UP_SRC    0x38

#define LIS2DW12_ID_VALUE       0x44

/* SPI read bit */
#define SPI_READ_BIT            0x80

/* ──────────────────────────── SPI Handle ──────────────────────────── */

extern SPI_HandleTypeDef hspi1;  /* Defined in system init */

/* ──────────────────────────── SPI Helpers ──────────────────────────── */

static void cs_low(void)
{
    HAL_GPIO_WritePin(ACCEL_CS_GPIO_PORT, ACCEL_CS_PIN, GPIO_PIN_RESET);
}

static void cs_high(void)
{
    HAL_GPIO_WritePin(ACCEL_CS_GPIO_PORT, ACCEL_CS_PIN, GPIO_PIN_SET);
}

static uint8_t spi_read_reg(uint8_t reg)
{
    uint8_t tx[2] = { reg | SPI_READ_BIT, 0x00 };
    uint8_t rx[2] = {0};

    cs_low();
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, 2, 100);
    cs_high();

    return rx[1];
}

static void spi_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { reg, val };

    cs_low();
    HAL_SPI_Transmit(&hspi1, tx, 2, 100);
    cs_high();
}

/* ──────────────────────────── Public API ──────────────────────────── */

int accelerometer_init(void)
{
    /* Initialize CS pin */
    GPIO_InitTypeDef gpio = {0};
    ACCEL_CS_GPIO_CLK_ENABLE();
    gpio.Pin   = ACCEL_CS_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ACCEL_CS_GPIO_PORT, &gpio);
    cs_high();

    HAL_Delay(10);

    /* Verify WHO_AM_I */
    if (accelerometer_who_am_i() != LIS2DW12_ID_VALUE) {
        return -1;
    }

    /* CTRL1: Low-power mode 1, 12.5 Hz ODR, low-power
     * [7:4] ODR = 0010 (12.5 Hz)
     * [3:2] MODE = 00 (low-power)
     * [1:0] LP_MODE = 01 (low-power mode 1) */
    spi_write_reg(LIS2DW12_CTRL1, 0x21);

    /* CTRL2: BDU enabled, IF_ADD_INC enabled */
    spi_write_reg(LIS2DW12_CTRL2, 0x08 | 0x04);

    /* CTRL6: ±2g full scale, low noise disabled (for low power) */
    spi_write_reg(LIS2DW12_CTRL6, 0x00);

    return 0;
}

bool accelerometer_is_active(uint16_t threshold_mg)
{
    (void)threshold_mg;
    uint8_t src = spi_read_reg(LIS2DW12_WAKE_UP_SRC);
    return (src & 0x08) != 0;  /* WU_IA bit */
}

void accelerometer_set_threshold(uint16_t threshold_mg)
{
    /* WAKE_UP_THS: 6-bit value, 1 LSB = FS/64
     * For ±2g: 1 LSB = 2000/64 ≈ 31.25 mg */
    uint8_t ths = (uint8_t)(threshold_mg / 31);
    if (ths > 63) ths = 63;

    spi_write_reg(LIS2DW12_WAKE_UP_THS, ths & 0x3F);

    /* WAKE_UP_DUR: wake-up duration = 2 samples */
    spi_write_reg(LIS2DW12_WAKE_UP_DUR, 0x02);
}

void accelerometer_enable_interrupt(bool enable)
{
    if (enable) {
        /* CTRL4_INT1: Route wake-up event to INT1 */
        spi_write_reg(LIS2DW12_CTRL4_INT1, 0x20);  /* INT1_WU */

        /* CTRL3: INT1 active high, push-pull */
        spi_write_reg(LIS2DW12_CTRL3, 0x00);

        /* Configure MCU EXTI for INT1 pin */
        GPIO_InitTypeDef gpio = {0};
        ACCEL_INT_GPIO_CLK_ENABLE();
        gpio.Pin  = ACCEL_INT_PIN;
        gpio.Mode = GPIO_MODE_IT_RISING;
        gpio.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(ACCEL_INT_GPIO_PORT, &gpio);

        HAL_NVIC_SetPriority(ACCEL_INT_EXTI_IRQn, 2, 0);
        HAL_NVIC_EnableIRQ(ACCEL_INT_EXTI_IRQn);
    } else {
        spi_write_reg(LIS2DW12_CTRL4_INT1, 0x00);
        HAL_NVIC_DisableIRQ(ACCEL_INT_EXTI_IRQn);
    }
}

int accelerometer_read(int16_t *x, int16_t *y, int16_t *z)
{
    /* Check data ready */
    uint8_t status = spi_read_reg(LIS2DW12_STATUS);
    if (!(status & 0x01)) {
        return -1;  /* No new data */
    }

    uint8_t xl = spi_read_reg(LIS2DW12_OUT_X_L);
    uint8_t xh = spi_read_reg(LIS2DW12_OUT_X_H);
    uint8_t yl = spi_read_reg(LIS2DW12_OUT_Y_L);
    uint8_t yh = spi_read_reg(LIS2DW12_OUT_Y_H);
    uint8_t zl = spi_read_reg(LIS2DW12_OUT_Z_L);
    uint8_t zh = spi_read_reg(LIS2DW12_OUT_Z_H);

    /* Raw values are 14-bit left-justified in low-power mode 1 */
    int16_t raw_x = (int16_t)((xh << 8) | xl) >> 2;
    int16_t raw_y = (int16_t)((yh << 8) | yl) >> 2;
    int16_t raw_z = (int16_t)((zh << 8) | zl) >> 2;

    /* Convert to mg: ±2g, 14-bit → sensitivity = 0.244 mg/LSB */
    *x = (int16_t)((int32_t)raw_x * 244 / 1000);
    *y = (int16_t)((int32_t)raw_y * 244 / 1000);
    *z = (int16_t)((int32_t)raw_z * 244 / 1000);

    return 0;
}

int accelerometer_who_am_i(void)
{
    return (int)spi_read_reg(LIS2DW12_WHO_AM_I);
}
