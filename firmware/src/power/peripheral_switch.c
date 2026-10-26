/**
 * @file peripheral_switch.c
 * @brief GPIO-controlled Peripheral Power Switching Implementation
 *
 * Uses GPIOs to drive TPS22918 load switch EN pins.
 * Pin assignments are defined in config/pin_map.h.
 */

#include "peripheral_switch.h"
#include "../config/pin_map.h"
#include "stm32l4xx_hal.h"

/* ──────────────────────────── Private State ──────────────────────────── */

static bool s_gnss_on  = false;
static bool s_modem_on = false;

/* ──────────────────────────── Implementation ──────────────────────────── */

void periph_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* Enable GPIO clocks for peripheral switch pins */
    GNSS_SW_GPIO_CLK_ENABLE();
    MODEM_SW_GPIO_CLK_ENABLE();

    /* GNSS load switch EN pin — push-pull, no pull, low speed */
    gpio.Pin   = GNSS_SW_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GNSS_SW_GPIO_PORT, &gpio);

    /* Modem load switch EN pin */
    gpio.Pin = MODEM_SW_PIN;
    HAL_GPIO_Init(MODEM_SW_GPIO_PORT, &gpio);

    /* Start with all peripherals OFF */
    periph_all_off();
}

void periph_gnss_power(bool on)
{
    HAL_GPIO_WritePin(GNSS_SW_GPIO_PORT, GNSS_SW_PIN,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    s_gnss_on = on;

    if (on) {
        /* TPS22918 turn-on time is ~60 µs — add small delay for rail to settle */
        HAL_Delay(1);
    }
}

void periph_modem_power(bool on)
{
    HAL_GPIO_WritePin(MODEM_SW_GPIO_PORT, MODEM_SW_PIN,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    s_modem_on = on;

    if (on) {
        /* Modem needs ~100 ms to initialize after power-on */
        HAL_Delay(100);
    }
}

bool periph_gnss_is_on(void)
{
    return s_gnss_on;
}

bool periph_modem_is_on(void)
{
    return s_modem_on;
}

void periph_all_off(void)
{
    periph_gnss_power(false);
    periph_modem_power(false);
}
