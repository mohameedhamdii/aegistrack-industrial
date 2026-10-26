/**
 * @file temperature.c
 * @brief Temperature Sensor Implementation
 *
 * Reads the STM32L4 internal die temperature sensor via ADC.
 * For more accurate readings, use the optional TMP117 I2C sensor.
 */

#include "temperature.h"
#include "stm32l4xx_hal.h"

static ADC_HandleTypeDef s_hadc;

int temperature_init(void)
{
    /* Enable ADC clock */
    __HAL_RCC_ADC_CLK_ENABLE();

    s_hadc.Instance                   = ADC1;
    s_hadc.Init.ClockPrescaler        = ADC_CLOCK_ASYNC_DIV1;
    s_hadc.Init.Resolution            = ADC_RESOLUTION_12B;
    s_hadc.Init.ScanConvMode          = ADC_SCAN_DISABLE;
    s_hadc.Init.ContinuousConvMode    = DISABLE;
    s_hadc.Init.DiscontinuousConvMode = DISABLE;
    s_hadc.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    s_hadc.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    s_hadc.Init.NbrOfConversion       = 1;

    if (HAL_ADC_Init(&s_hadc) != HAL_OK) {
        return -1;
    }

    return 0;
}

int8_t temperature_read_celsius(void)
{
    /* Configure ADC channel for temperature sensor */
    ADC_ChannelConfTypeDef ch = {0};
    ch.Channel      = ADC_CHANNEL_TEMPSENSOR;
    ch.Rank         = ADC_REGULAR_RANK_1;
    ch.SamplingTime = ADC_SAMPLETIME_640CYCLES_5;
    HAL_ADC_ConfigChannel(&s_hadc, &ch);

    /* Start conversion */
    HAL_ADC_Start(&s_hadc);
    HAL_ADC_PollForConversion(&s_hadc, 100);
    uint32_t raw = HAL_ADC_GetValue(&s_hadc);
    HAL_ADC_Stop(&s_hadc);

    /* Convert ADC value to temperature using factory calibration */
    /* TS_CAL1 = calibration value at 30°C */
    /* TS_CAL2 = calibration value at 130°C (or 110°C on some parts) */
    uint16_t ts_cal1 = *((uint16_t *)0x1FFF75A8);  /* 30°C calibration */
    uint16_t ts_cal2 = *((uint16_t *)0x1FFF75CA);  /* 130°C calibration */

    float temp = 30.0f + (float)(raw - ts_cal1) * (130.0f - 30.0f) /
                 (float)(ts_cal2 - ts_cal1);

    return (int8_t)temp;
}
