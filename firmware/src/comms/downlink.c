/**
 * @file downlink.c
 * @brief Downlink Command Parser Implementation
 *
 * Parses JSON commands from the backend received via NB-IoT downlink.
 * Uses a minimal JSON parser (no external library).
 */

#include "downlink.h"
#include "../power/power_manager.h"
#include "../geofence/geofence_engine.h"
#include <string.h>
#include <stdlib.h>

/* ──────────────────── Simple JSON Key Finder ──────────────────── */

static const char* json_find_key(const char *json, const char *key)
{
    char search[64];
    int len = snprintf(search, sizeof(search), "\"%s\"", key);
    if (len < 0) return NULL;

    const char *p = strstr(json, search);
    if (p == NULL) return NULL;

    p += len;  /* Skip past key string */
    while (*p == ' ' || *p == ':') p++;
    return p;
}

static int json_get_int(const char *json, const char *key, int *out)
{
    const char *p = json_find_key(json, key);
    if (p == NULL) return -1;
    *out = atoi(p);
    return 0;
}

static int json_get_string(const char *json, const char *key, char *out, int max_len)
{
    const char *p = json_find_key(json, key);
    if (p == NULL || *p != '"') return -1;
    p++;  /* Skip opening quote */
    int i = 0;
    while (*p && *p != '"' && i < max_len - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 0;
}

/* ──────────────────── Public API ──────────────────── */

int downlink_parse(const uint8_t *data, uint16_t len, downlink_cmd_t *cmd)
{
    if (data == NULL || len == 0 || cmd == NULL) {
        return -1;
    }

    memset(cmd, 0, sizeof(downlink_cmd_t));

    /* Treat data as JSON string */
    const char *json = (const char *)data;

    char cmd_str[32] = {0};
    if (json_get_string(json, "cmd", cmd_str, sizeof(cmd_str)) != 0) {
        return -1;  /* No "cmd" field */
    }

    if (strcmp(cmd_str, "set_interval") == 0) {
        cmd->type = DL_CMD_SET_INTERVAL;
        int val = 0;
        if (json_get_int(json, "value", &val) == 0 && val > 0) {
            cmd->interval_s = (uint32_t)val;
        }
    } else if (strcmp(cmd_str, "set_geofence") == 0) {
        cmd->type = DL_CMD_SET_GEOFENCE;
        /* Geofence zone parsing is complex — handled in downlink_process */
    } else if (strcmp(cmd_str, "request_fix") == 0) {
        cmd->type = DL_CMD_REQUEST_FIX;
    } else if (strcmp(cmd_str, "ota_url") == 0) {
        cmd->type = DL_CMD_OTA_URL;
        json_get_string(json, "url", cmd->ota_url, sizeof(cmd->ota_url));
    } else {
        cmd->type = DL_CMD_NONE;
        return -1;
    }

    return 0;
}

void downlink_process(const downlink_cmd_t *cmd)
{
    switch (cmd->type) {
    case DL_CMD_SET_INTERVAL:
        /* Update fix interval — handled via power manager config update */
        /* In production, write new interval to flash config */
        break;

    case DL_CMD_REQUEST_FIX:
        /* Force an immediate GPS fix */
        power_manager_force_state(PWR_STATE_GNSS_ACQUIRE);
        break;

    case DL_CMD_SET_GEOFENCE:
        /* Parsed geofence zones would be added here */
        break;

    case DL_CMD_OTA_URL:
        /* OTA firmware update — stretch goal */
        break;

    case DL_CMD_NONE:
    default:
        break;
    }
}
