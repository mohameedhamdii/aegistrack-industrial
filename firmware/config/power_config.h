/**
 * @file power_config.h
 * @brief Power Management Configuration Defaults
 */

#ifndef POWER_CONFIG_H
#define POWER_CONFIG_H

/* Fix intervals */
#define DEFAULT_STATIONARY_INTERVAL_S   21600   /* 6 hours   */
#define DEFAULT_MOVING_INTERVAL_S       900     /* 15 min    */

/* Motion detection */
#define DEFAULT_MOTION_CONFIRM_MS       3000    /* 3 seconds */
#define DEFAULT_MOTION_TIMEOUT_S        300     /* 5 minutes */
#define DEFAULT_ACCEL_THRESHOLD_MG      50      /* 50 mg     */

/* GNSS */
#define DEFAULT_GNSS_TIMEOUT_S          60      /* 60 seconds */

/* Communications */
#define DEFAULT_NBIOT_MAX_RETRIES       3

#endif /* POWER_CONFIG_H */
