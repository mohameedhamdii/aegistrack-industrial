/**
 * @file downlink.h
 * @brief Downlink Command Parser
 */

#ifndef DOWNLINK_H
#define DOWNLINK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DL_CMD_NONE           = 0,
    DL_CMD_SET_INTERVAL   = 1,
    DL_CMD_SET_GEOFENCE   = 2,
    DL_CMD_REQUEST_FIX    = 3,
    DL_CMD_OTA_URL        = 4,
} downlink_cmd_type_t;

typedef struct {
    downlink_cmd_type_t type;
    uint32_t            interval_s;
    char                ota_url[128];
    /* Geofence data is parsed separately */
} downlink_cmd_t;

int  downlink_parse(const uint8_t *data, uint16_t len, downlink_cmd_t *cmd);
void downlink_process(const downlink_cmd_t *cmd);

#ifdef __cplusplus
}
#endif

#endif /* DOWNLINK_H */
