#include "storage_nvs.h"
#include "storage_fs.h"
#include "settings_service.h"
#include "inventory_service.h"
#include "ams_service.h"
#include "mqtt_service.h"
#include "help_service.h"
#include "api_http.h"
#include "wifi_manager.h"
#include "sntp_service.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

#define TAG "main"

/* ── Periodic dirty-save timer ───────────────────────────── */

static void save_timer_cb(void *arg)
{
    inventory_service_process_dirty();
    ams_service_process_dirty();
}

/* ── Entry point ─────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "Filament Tracker booting…");

    /* Mark this firmware image as valid so the bootloader does not roll back
       to the previous OTA partition on next boot. Safe to call on factory
       images too — it is a no-op when OTA is not in progress. */
    esp_ota_mark_app_valid_cancel_rollback();

    /* 1. NVS */
    ESP_ERROR_CHECK(storage_nvs_init());

    /* 2. LittleFS */
    ESP_ERROR_CHECK(storage_fs_init());

    /* 3. Settings */
    ESP_ERROR_CHECK(settings_service_init());

    /* 4. Inventory */
    ESP_ERROR_CHECK(inventory_service_init());

    /* 5. AMS */
    ESP_ERROR_CHECK(ams_service_init());

    /* 6. Help */
    ESP_ERROR_CHECK(help_service_init());

    /* 7. Wi-Fi — provisioning portal or normal STA mode */
    wifi_manager_mode_t wifi_mode = wifi_manager_init();

    if (wifi_mode == WIFI_MANAGER_MODE_AP) {
        /* Device is in captive-portal mode.  The portal HTTP server and DNS
           server are already running inside wifi_manager.  We must NOT start
           the main application HTTP server on the same port.
           The device will reboot automatically after the user saves creds. */
        ESP_LOGI(TAG, "Provisioning mode active — waiting for Wi-Fi credentials");
        while (1) vTaskDelay(pdMS_TO_TICKS(5000));
    }

    /* 8. HTTP server + WebSocket (STA mode only) */
    httpd_handle_t server = api_http_start();
    if (!server) ESP_LOGE(TAG, "HTTP server failed to start — continuing without UI");

    /* Fetch settings once — used by SNTP and MQTT initialisers below. */
    const app_settings_t *s = settings_service_get();

    /* 9. SNTP — start time sync using the configured timezone.
       The timezone is a POSIX TZ string stored in settings (e.g. "CET-1CEST,M3.5.0,M10.5.0/3").
       Falls back to UTC if the field is empty. */
    sntp_service_start(s->timezone, NULL /* use default pool.ntp.org */);

    /* 10. MQTT (after Wi-Fi started) */
    mqtt_service_init(&s->mqtt);

    /* 11. Periodic dirty-save: check every 5 seconds */
    esp_timer_handle_t save_timer;
    esp_timer_create_args_t timer_args = {
        .callback = save_timer_cb,
        .name     = "save_timer",
    };
    esp_timer_create(&timer_args, &save_timer);
    esp_timer_start_periodic(save_timer, 5 * 1000 * 1000); /* 5 s in µs */

    ESP_LOGI(TAG, "Boot complete. Open http://<device-ip>/ in your browser.");
}
