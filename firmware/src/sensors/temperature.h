/**
 * @file temperature.h
 * @brief Temperature Sensor (MCU Die or TMP117)
 */

#ifndef TEMPERATURE_H
#define TEMPERATURE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int temperature_init(void);
int8_t temperature_read_celsius(void);

#ifdef __cplusplus
}
#endif

#endif /* TEMPERATURE_H */
