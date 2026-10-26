/**
 * @file sleep.c
 * @brief STM32L4 Low-Power Sleep Mode Implementation
 */

#include "sleep.h"
#include "stm32l4xx_hal.h"
#include "stm32l4xx_hal_rtc.h"
#include "stm32l4xx_hal_pwr.h"
#include "stm32l4xx_hal_pwr_ex.h"

/* ──────────────────────────── RTC Handle ──────────────────────────── */

extern RTC_HandleTypeDef hrtc;  /* Defined in main.c or system init */

/* ──────────────────────────── Implementation ──────────────────────── */

void sleep_rtc_init(void)
{
    /* Enable LSE (32.768 kHz crystal) as RTC clock source */
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    osc.LSEState       = RCC_LSE_ON;
    HAL_RCC_OscConfig(&osc);

    RCC_PeriphCLKInitTypeDef clk = {0};
    clk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    clk.RTCClockSelection    = RCC_RTCCLKSOURCE_LSE;
    HAL_RCCEx_PeriphCLKConfig(&clk);

    __HAL_RCC_RTC_ENABLE();

    /* Initialize RTC */
    hrtc.Instance            = RTC;
    hrtc.Init.HourFormat     = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv   = 127;
    hrtc.Init.SynchPrediv    = 255;
    hrtc.Init.OutPut         = RTC_OUTPUT_DISABLE;
    HAL_RTC_Init(&hrtc);
}

/**
 * @brief Set an RTC wake-up timer for the given duration.
 */
static void set_rtc_wakeup(uint32_t seconds)
{
    /* Disable existing wake-up timer */
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);

    /* RTC wake-up timer clocked at 1 Hz (CK_SPRE) for durations up to 18h.
     * For longer durations, use the WUT auto-reload with 2^16 extension. */
    if (seconds <= 0xFFFF) {
        HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, (uint16_t)seconds,
                                     RTC_WAKEUPCLOCK_CK_SPRE_16BITS);
    } else {
        /* For durations > 65535 s (~18 h), use the 17-bit counter mode */
        uint32_t wut_val = seconds - 0x10000;
        if (wut_val > 0xFFFF) {
            wut_val = 0xFFFF;  /* Max ~36 h with this mode */
        }
        HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, (uint16_t)wut_val,
                                     RTC_WAKEUPCLOCK_CK_SPRE_17BITS);
    }
}

void sleep_enter_stop2(uint32_t seconds)
{
    /* Set RTC wake-up */
    set_rtc_wakeup(seconds);

    /* Disable SysTick interrupt to prevent wake */
    HAL_SuspendTick();

    /* Configure and enter Stop 2 mode */
    HAL_PWREx_EnableLowPowerRunMode();

    /* Clear all wake-up flags */
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);

    /* Enter Stop 2 mode — all SRAM retained, ~1.3 µA */
    HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);

    /* ── Execution resumes here after wake-up ── */

    /* Reconfigure system clock (MSI → PLL → 80 MHz) */
    SystemClock_Config();  /* Defined in main.c */

    /* Resume SysTick */
    HAL_ResumeTick();
}

void sleep_enter_standby(uint32_t seconds)
{
    /* Set RTC wake-up */
    set_rtc_wakeup(seconds);

    /* Enable WKUP pin for accelerometer interrupt (WKUP1 = PA0) */
    HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN1_HIGH);

    /* Clear all wake-up flags */
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);

    /* Enter Standby mode — only backup domain retained, ~0.6 µA */
    HAL_PWR_EnterSTANDBYMode();

    /* ── Execution does NOT resume here ── */
    /* After Standby, the MCU resets and starts from main() */
}

int sleep_woke_from_standby(void)
{
    return (__HAL_PWR_GET_FLAG(PWR_FLAG_SB) != RESET) ? 1 : 0;
}

void sleep_clear_wakeup_flags(void)
{
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
    __HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&hrtc, RTC_FLAG_WUTF);
}
