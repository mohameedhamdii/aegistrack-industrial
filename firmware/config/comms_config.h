/**
 * @file comms_config.h
 * @brief Communications Configuration
 */

#ifndef COMMS_CONFIG_H
#define COMMS_CONFIG_H

/* NB-IoT APN */
#define DEFAULT_APN             "iot.1nce.net"

/* Backend server */
#define DEFAULT_SERVER_IP       "0.0.0.0"      /* Set to your server IP */
#define DEFAULT_SERVER_PORT     5683            /* CoAP/UDP port        */

/* Timeouts */
#define DEFAULT_REG_TIMEOUT_MS  60000           /* 60 s registration    */
#define DEFAULT_TX_TIMEOUT_MS   30000           /* 30 s TX timeout      */

/* Tracker identity */
#define TRACKER_ID              "TRACKER-001"

#endif /* COMMS_CONFIG_H */
