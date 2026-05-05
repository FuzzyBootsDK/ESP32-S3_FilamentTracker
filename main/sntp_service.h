#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * Start SNTP synchronisation and apply the POSIX timezone string.
 *
 * Call once after Wi-Fi Station mode is active (IP obtained).
 *
 * @param tz_posix  POSIX TZ string, e.g. "CET-1CEST,M3.5.0,M10.5.0/3".
 *                  Pass NULL or "" to use UTC.
 * @param ntp_server  NTP hostname, e.g. "pool.ntp.org".
 *                    Pass NULL to use "pool.ntp.org".
 */
esp_err_t sntp_service_start(const char *tz_posix, const char *ntp_server);

/**
 * Apply (or change) the POSIX timezone string without restarting SNTP.
 * Safe to call at any time after sntp_service_start().
 *
 * @param tz_posix  POSIX TZ string. NULL / "" = UTC.
 */
void sntp_service_set_timezone(const char *tz_posix);

/**
 * Return true once at least one SNTP sync has completed successfully.
 */
bool sntp_service_is_synced(void);
