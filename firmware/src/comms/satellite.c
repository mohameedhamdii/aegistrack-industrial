/**
 * @file satellite.c
 * @brief Satellite Modem Stub Implementation
 *
 * Placeholder for Iridium SBD satellite fallback.
 * Implementation depends on specific satellite modem hardware.
 */

#include "satellite.h"

int satellite_init(void)
{
    /* TODO: Initialize Iridium SBD modem (RockBLOCK 9603)
     * - UART initialization
     * - AT+SBDWB for binary write
     * - AT+SBDIX for session initiation
     */
    return -1;  /* Not implemented */
}

int satellite_send(const uint8_t *data, uint16_t len)
{
    (void)data;
    (void)len;
    return -1;  /* Not implemented */
}

bool satellite_is_available(void)
{
    return false;  /* Not available until implemented */
}

int satellite_get_signal_quality(void)
{
    return 0;
}

void satellite_shutdown(void)
{
    /* No-op */
}
