/**
 * @file satellite.h
 * @brief Optional Satellite Modem Driver (Iridium SBD)
 *
 * Fallback communication channel for areas without NB-IoT coverage.
 * Uses the RockBLOCK 9603 Iridium SBD modem.
 */

#ifndef SATELLITE_H
#define SATELLITE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int  satellite_init(void);
int  satellite_send(const uint8_t *data, uint16_t len);
bool satellite_is_available(void);
int  satellite_get_signal_quality(void);
void satellite_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* SATELLITE_H */
