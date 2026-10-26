/**
 * @file ephemeris_cache.h
 * @brief GNSS Ephemeris Cache in MCU Flash
 *
 * Stores u-blox AssistNow Offline ephemeris data in MCU flash
 * for fast TTFF (Time To First Fix). Data is downloaded during
 * each NB-IoT connection and valid for ~2 weeks.
 */

#ifndef EPHEMERIS_CACHE_H
#define EPHEMERIS_CACHE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EPH_CACHE_MAX_SIZE   (32 * 1024)  /**< 32 KB max cache size */

/**
 * @brief Initialize the ephemeris cache.
 */
void ephemeris_cache_init(void);

/**
 * @brief Store new ephemeris data to flash.
 * @param data      Pointer to AssistNow data.
 * @param len       Length in bytes (must be <= EPH_CACHE_MAX_SIZE).
 * @param timestamp UTC epoch when data was downloaded.
 * @return 0 on success, -1 on failure.
 */
int ephemeris_cache_store(const uint8_t *data, uint32_t len, uint32_t timestamp);

/**
 * @brief Get pointer to cached ephemeris data.
 * @param data  Output: pointer to data in flash.
 * @param len   Output: length of data.
 * @return 0 if data available, -1 if cache empty.
 */
int ephemeris_cache_get(const uint8_t **data, uint32_t *len);

/**
 * @brief Check if cached ephemeris data is still valid.
 *        Validity is based on age — AssistNow data valid for ~14 days.
 * @return true if valid, false if expired or empty.
 */
bool ephemeris_cache_valid(void);

/**
 * @brief Erase the ephemeris cache.
 */
void ephemeris_cache_erase(void);

#ifdef __cplusplus
}
#endif

#endif /* EPHEMERIS_CACHE_H */
