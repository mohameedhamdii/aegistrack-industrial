/**
 * @file geofence_storage.h
 * @brief Geofence Zone Flash Storage
 */

#ifndef GEOFENCE_STORAGE_H
#define GEOFENCE_STORAGE_H

#include "geofence_engine.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void geofence_storage_init(void);
int  geofence_storage_load(geofence_zone_t *zones, int max_zones);
int  geofence_storage_save(const geofence_zone_t *zones, int count);
void geofence_storage_erase(void);

#ifdef __cplusplus
}
#endif

#endif /* GEOFENCE_STORAGE_H */
