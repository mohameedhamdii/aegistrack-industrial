/**
 * @file geofence_storage.c
 * @brief Geofence Zone Flash Storage Implementation
 *
 * Stores geofence zones in a dedicated flash sector with a magic
 * number header for integrity checking.
 */

#include "geofence_storage.h"
#include <string.h>

#ifdef USE_HAL_DRIVER
#include "stm32l4xx_hal.h"
#include "stm32l4xx_hal_flash.h"
#include "stm32l4xx_hal_flash_ex.h"

/* Flash address for geofence data — separate from ephemeris cache */
#define GEO_FLASH_BASE    0x081F0000  /* 32 KB before ephemeris area */
#define GEO_FLASH_PAGE    (251)
#define GEO_MAGIC         0x4645474F  /* "GEFO" */

typedef struct {
    uint32_t magic;
    uint32_t count;
    uint32_t reserved[2];
} geo_header_t;

void geofence_storage_init(void)
{
    /* Nothing needed — flash persists across resets */
}

int geofence_storage_load(geofence_zone_t *zones, int max_zones)
{
    const geo_header_t *hdr = (const geo_header_t *)GEO_FLASH_BASE;

    if (hdr->magic != GEO_MAGIC || hdr->count == 0) {
        return 0;
    }

    int count = (int)hdr->count;
    if (count > max_zones) {
        count = max_zones;
    }

    const geofence_zone_t *stored = (const geofence_zone_t *)(GEO_FLASH_BASE + sizeof(geo_header_t));
    memcpy(zones, stored, count * sizeof(geofence_zone_t));

    return count;
}

int geofence_storage_save(const geofence_zone_t *zones, int count)
{
    /* Erase flash pages */
    FLASH_EraseInitTypeDef erase;
    uint32_t page_error;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks     = FLASH_BANK_2;
    erase.Page      = GEO_FLASH_PAGE;
    erase.NbPages   = 4;

    HAL_FLASH_Unlock();
    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
        HAL_FLASH_Lock();
        return -1;
    }

    /* Write header */
    geo_header_t hdr = { .magic = GEO_MAGIC, .count = (uint32_t)count };

    /* Program in 64-bit double-words */
    uint8_t buf[sizeof(geo_header_t)];
    memcpy(buf, &hdr, sizeof(hdr));

    for (uint32_t i = 0; i < sizeof(buf); i += 8) {
        uint64_t dword;
        uint8_t tmp[8] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        uint32_t cplen = sizeof(buf) - i;
        if (cplen > 8) cplen = 8;
        memcpy(tmp, buf + i, cplen);
        memcpy(&dword, tmp, 8);
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, GEO_FLASH_BASE + i, dword);
    }

    /* Write zone data */
    uint32_t data_offset = sizeof(geo_header_t);
    uint32_t data_size = count * sizeof(geofence_zone_t);
    const uint8_t *data = (const uint8_t *)zones;

    for (uint32_t i = 0; i < data_size; i += 8) {
        uint64_t dword;
        uint8_t tmp[8] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        uint32_t cplen = data_size - i;
        if (cplen > 8) cplen = 8;
        memcpy(tmp, data + i, cplen);
        memcpy(&dword, tmp, 8);
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, GEO_FLASH_BASE + data_offset + i, dword);
    }

    HAL_FLASH_Lock();
    return 0;
}

void geofence_storage_erase(void)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t page_error;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks     = FLASH_BANK_2;
    erase.Page      = GEO_FLASH_PAGE;
    erase.NbPages   = 4;

    HAL_FLASH_Unlock();
    HAL_FLASHEx_Erase(&erase, &page_error);
    HAL_FLASH_Lock();
}
#else /* !USE_HAL_DRIVER — Host unit test / simulation build */

static geofence_zone_t s_mock_storage[GEOFENCE_MAX_ZONES];
static int             s_mock_count = 0;

void geofence_storage_init(void)
{
    /* Keep existing in-memory mock data */
}

int geofence_storage_load(geofence_zone_t *zones, int max_zones)
{
    int count = (s_mock_count < max_zones) ? s_mock_count : max_zones;
    if (count > 0 && zones != NULL) {
        memcpy(zones, s_mock_storage, count * sizeof(geofence_zone_t));
    }
    return count;
}

int geofence_storage_save(const geofence_zone_t *zones, int count)
{
    if (count > GEOFENCE_MAX_ZONES) count = GEOFENCE_MAX_ZONES;
    s_mock_count = count;
    if (count > 0 && zones != NULL) {
        memcpy(s_mock_storage, zones, count * sizeof(geofence_zone_t));
    }
    return 0;
}

void geofence_storage_erase(void)
{
    memset(s_mock_storage, 0, sizeof(s_mock_storage));
    s_mock_count = 0;
}
#endif
