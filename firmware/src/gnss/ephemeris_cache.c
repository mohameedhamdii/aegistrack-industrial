/**
 * @file ephemeris_cache.c
 * @brief GNSS Ephemeris Cache Implementation
 *
 * Stores AssistNow Offline data in a dedicated flash sector.
 * Uses a simple header + data layout with timestamp for validity checking.
 */

#include "ephemeris_cache.h"
#include "stm32l4xx_hal.h"
#include "stm32l4xx_hal_flash.h"
#include "stm32l4xx_hal_flash_ex.h"

#include <string.h>

/* ──────────────────────────── Flash Layout ──────────────────────────── */

/**
 * Ephemeris data is stored in the last 32 KB of Bank 2 flash.
 * STM32L4R5 has 2 MB flash — we reserve pages at the end.
 *
 * Flash sector layout for ephemeris:
 *   Offset 0x00:  Header (12 bytes)
 *   Offset 0x10:  Ephemeris data (up to 32 KB - 16 bytes)
 */

/* Use a dedicated flash area — adjust base address for your linker script */
#define EPH_FLASH_BASE    0x081F8000  /* Last 32 KB of 2 MB flash */
#define EPH_FLASH_PAGE    (255)       /* Last page in Bank 2      */

#define EPH_VALIDITY_S    (14 * 24 * 3600)  /* 14 days in seconds */

/* Header stored at the beginning of the flash sector */
typedef struct {
    uint32_t magic;        /* 0xEPH1CACE */
    uint32_t timestamp;    /* UTC epoch when data was stored */
    uint32_t data_len;     /* Length of ephemeris data */
} eph_header_t;

#define EPH_MAGIC  0xE9C1CACE

/* ──────────────────────────── Private Helpers ──────────────────────── */

static const eph_header_t* get_header(void)
{
    return (const eph_header_t *)EPH_FLASH_BASE;
}

static const uint8_t* get_data_ptr(void)
{
    return (const uint8_t *)(EPH_FLASH_BASE + sizeof(eph_header_t));
}

static int erase_flash_sector(void)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t page_error;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks     = FLASH_BANK_2;
    erase.Page      = EPH_FLASH_PAGE;
    erase.NbPages   = 4;  /* 4 pages × 8 KB = 32 KB */

    HAL_FLASH_Unlock();
    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase, &page_error);
    HAL_FLASH_Lock();

    return (status == HAL_OK) ? 0 : -1;
}

static int write_flash(uint32_t address, const uint8_t *data, uint32_t len)
{
    HAL_FLASH_Unlock();

    /* STM32L4 programs in 64-bit (8-byte) double-words */
    uint32_t aligned_len = (len + 7) & ~7U;
    uint8_t buf[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    for (uint32_t i = 0; i < aligned_len; i += 8) {
        memset(buf, 0xFF, 8);
        uint32_t copy_len = (len - i >= 8) ? 8 : (len - i);
        memcpy(buf, data + i, copy_len);

        uint64_t dword;
        memcpy(&dword, buf, 8);

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                              address + i, dword) != HAL_OK) {
            HAL_FLASH_Lock();
            return -1;
        }
    }

    HAL_FLASH_Lock();
    return 0;
}

/* ──────────────────────────── Public API ──────────────────────────── */

void ephemeris_cache_init(void)
{
    /* Nothing to initialize — data persists in flash */
}

int ephemeris_cache_store(const uint8_t *data, uint32_t len, uint32_t timestamp)
{
    if (data == NULL || len == 0 || len > (EPH_CACHE_MAX_SIZE - sizeof(eph_header_t))) {
        return -1;
    }

    /* Erase the flash sector */
    if (erase_flash_sector() != 0) {
        return -1;
    }

    /* Write header */
    eph_header_t header = {
        .magic     = EPH_MAGIC,
        .timestamp = timestamp,
        .data_len  = len,
    };
    if (write_flash(EPH_FLASH_BASE, (const uint8_t *)&header, sizeof(header)) != 0) {
        return -1;
    }

    /* Write data */
    if (write_flash(EPH_FLASH_BASE + sizeof(eph_header_t), data, len) != 0) {
        return -1;
    }

    return 0;
}

int ephemeris_cache_get(const uint8_t **data, uint32_t *len)
{
    const eph_header_t *hdr = get_header();

    if (hdr->magic != EPH_MAGIC || hdr->data_len == 0) {
        return -1;
    }

    *data = get_data_ptr();
    *len  = hdr->data_len;
    return 0;
}

bool ephemeris_cache_valid(void)
{
    const eph_header_t *hdr = get_header();

    if (hdr->magic != EPH_MAGIC || hdr->data_len == 0) {
        return false;
    }

    /* Check age — need current time from RTC */
    /* For simplicity, we check against the last known GPS time */
    /* In production, read RTC to get current epoch */
    extern uint32_t system_get_epoch(void);
    uint32_t now = system_get_epoch();

    if (now > hdr->timestamp && (now - hdr->timestamp) > EPH_VALIDITY_S) {
        return false;  /* Data older than 14 days */
    }

    return true;
}

void ephemeris_cache_erase(void)
{
    erase_flash_sector();
}
