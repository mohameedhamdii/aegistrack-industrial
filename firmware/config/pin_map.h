/**
 * @file pin_map.h
 * @brief Hardware Pin Map for Asset Tracker PCB
 *
 * Central pin assignment reference. All peripheral GPIO assignments
 * are defined here for easy PCB routing changes.
 *
 * MCU: STM32L4R5ZI (LQFP144)
 */

#ifndef PIN_MAP_H
#define PIN_MAP_H

#include "stm32l4xx_hal.h"

/* ──────────────────────────── GNSS Module (USART2) ──────────────────── */
#define GNSS_UART_TX_PIN        GPIO_PIN_2
#define GNSS_UART_TX_PORT       GPIOA
#define GNSS_UART_RX_PIN        GPIO_PIN_3
#define GNSS_UART_RX_PORT       GPIOA
#define GNSS_UART_AF            GPIO_AF7_USART2

/* GNSS Power Load Switch (TPS22918 EN pin) */
#define GNSS_SW_PIN             GPIO_PIN_4
#define GNSS_SW_GPIO_PORT       GPIOA
#define GNSS_SW_GPIO_CLK_ENABLE()  __HAL_RCC_GPIOA_CLK_ENABLE()

/* ──────────────────────────── NB-IoT Modem (USART1) ──────────────────── */
#define MODEM_UART_TX_PIN       GPIO_PIN_9
#define MODEM_UART_TX_PORT      GPIOA
#define MODEM_UART_RX_PIN       GPIO_PIN_10
#define MODEM_UART_RX_PORT      GPIOA
#define MODEM_UART_AF           GPIO_AF7_USART1

/* Modem Power Load Switch (TPS22918 EN pin) */
#define MODEM_SW_PIN            GPIO_PIN_5
#define MODEM_SW_GPIO_PORT      GPIOA
#define MODEM_SW_GPIO_CLK_ENABLE()  __HAL_RCC_GPIOA_CLK_ENABLE()

/* ──────────────────────────── Accelerometer (SPI1) ──────────────────── */
#define ACCEL_SPI_SCK_PIN       GPIO_PIN_5
#define ACCEL_SPI_SCK_PORT      GPIOB
#define ACCEL_SPI_MISO_PIN      GPIO_PIN_6
#define ACCEL_SPI_MISO_PORT     GPIOB
#define ACCEL_SPI_MOSI_PIN      GPIO_PIN_7
#define ACCEL_SPI_MOSI_PORT     GPIOB
#define ACCEL_SPI_AF            GPIO_AF5_SPI1

/* Accelerometer Chip Select */
#define ACCEL_CS_PIN            GPIO_PIN_4
#define ACCEL_CS_GPIO_PORT      GPIOB
#define ACCEL_CS_GPIO_CLK_ENABLE()  __HAL_RCC_GPIOB_CLK_ENABLE()

/* Accelerometer INT1 (wake-on-motion) — connected to WKUP pin */
#define ACCEL_INT_PIN           GPIO_PIN_0
#define ACCEL_INT_GPIO_PORT     GPIOA
#define ACCEL_INT_GPIO_CLK_ENABLE()  __HAL_RCC_GPIOA_CLK_ENABLE()
#define ACCEL_INT_EXTI_IRQn     EXTI0_IRQn

/* ──────────────────────────── Status LED ──────────────────────────── */
#define LED_PIN                 GPIO_PIN_13
#define LED_GPIO_PORT           GPIOC
#define LED_GPIO_CLK_ENABLE()   __HAL_RCC_GPIOC_CLK_ENABLE()

/* ──────────────────────────── Battery Voltage ADC ──────────────────── */
#define VBAT_ADC_PIN            GPIO_PIN_1
#define VBAT_ADC_PORT           GPIOA
#define VBAT_ADC_CHANNEL        ADC_CHANNEL_6

#endif /* PIN_MAP_H */
