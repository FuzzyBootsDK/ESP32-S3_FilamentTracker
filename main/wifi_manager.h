#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * Wi-Fi Manager — handles first-boot provisioning via a captive-portal AP
 * and normal STA operation on subsequent boots.
 *
 * Flow:
 *   1. If NVS contains SSID credentials  →  start STA mode (async connect).
 *      Returns WIFI_MANAGER_MODE_STA immediately; connection completes in the
 *      background via the Wi-Fi event loop.
 *
 *   2. If no credentials stored (first boot or after reset)  →  start AP mode.
 *      The device broadcasts SSID "FilamentTracker", a DNS server redirects
 *      all queries to 192.168.4.1, and a minimal HTTP server on port 80 serves
 *      the Wi-Fi setup page.  After the user submits credentials the device
 *      saves them to NVS and reboots automatically into STA mode.
 *      Returns WIFI_MANAGER_MODE_AP.  The caller should NOT start the main
 *      application HTTP server in this mode.
 */

typedef enum {
    WIFI_MANAGER_MODE_STA,  /**< Connected to (or trying to connect to) a Wi-Fi network. */
    WIFI_MANAGER_MODE_AP,   /**< Captive-portal provisioning mode; device is an AP.      */
} wifi_manager_mode_t;

/**
 * Initialise Wi-Fi.  Must be called after storage_nvs_init().
 * Internally calls esp_netif_init() and esp_event_loop_create_default().
 */
wifi_manager_mode_t wifi_manager_init(void);

/** True when SSID credentials are stored in NVS (regardless of connection state). */
bool wifi_manager_has_credentials(void);

/**
 * Erase stored Wi-Fi credentials from NVS and reboot.
 * The device will start in AP / captive-portal mode on the next boot.
 * Safe to call from any task.
 */
void wifi_manager_reset_credentials(void);
