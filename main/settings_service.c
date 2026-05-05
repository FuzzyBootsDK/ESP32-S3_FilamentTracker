#include "settings_service.h"

#include "storage_nvs.h"
#include "json_codec.h"
#include "esp_log.h"

#include <string.h>

#define TAG "settings"

/* NVS keys */
#define NVS_KEY_SETTINGS "settings_v1"

static app_settings_t s_settings;
static bool           s_loaded = false;

/* ── Defaults ─────────────────────────────────────────────── */

static void apply_defaults(app_settings_t *s)
{
    if (!s->device_name[0]) snprintf(s->device_name, sizeof(s->device_name), "filament-tracker-s3");
    if (!s->theme[0])       snprintf(s->theme,       sizeof(s->theme),       "dark");
    if (!s->timezone[0])    snprintf(s->timezone,    sizeof(s->timezone),    "UTC");
    if (!s->low_stock_threshold_grams) s->low_stock_threshold_grams = 150;
    if (!s->mqtt.broker_port)          s->mqtt.broker_port          = 1883;
    if (!s->mqtt.client_id[0])
        snprintf(s->mqtt.client_id, sizeof(s->mqtt.client_id), "filament-tracker-s3");
    if (!s->ui.page_size) s->ui.page_size = 25;
    s->ui.auto_refresh = true;
    if (!s->schema_version) s->schema_version = 1;
}

/* ── Persist ─────────────────────────────────────────────── */

static esp_err_t save(void)
{
    cJSON *obj = json_encode_settings(&s_settings, false /* include password */);
    if (!obj) return ESP_ERR_NO_MEM;

    char *str = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!str) return ESP_ERR_NO_MEM;

    esp_err_t ret = storage_nvs_set_blob(NVS_KEY_SETTINGS, str, strlen(str) + 1);
    cJSON_free(str);
    return ret;
}

/* ── Init ─────────────────────────────────────────────────── */

esp_err_t settings_service_init(void)
{
    memset(&s_settings, 0, sizeof(s_settings));

    /* Try to load existing blob */
    size_t    len    = 0;
    esp_err_t ret    = storage_nvs_get_blob(NVS_KEY_SETTINGS, NULL, &len);
    bool      exists = (ret == ESP_OK && len > 1);

    if (exists) {
        char *buf = malloc(len);
        if (!buf) return ESP_ERR_NO_MEM;

        ret = storage_nvs_get_blob(NVS_KEY_SETTINGS, buf, &len);
        if (ret == ESP_OK) {
            cJSON *obj = cJSON_ParseWithLength(buf, len);
            if (obj) {
                json_decode_settings(obj, &s_settings);
                cJSON_Delete(obj);
            }
        }
        free(buf);
    }

    apply_defaults(&s_settings);

    if (!exists) {
        /* First boot — persist defaults */
        save();
    }

    s_loaded = true;
    ESP_LOGI(TAG, "Settings loaded (device: %s, theme: %s)", s_settings.device_name, s_settings.theme);
    return ESP_OK;
}

/* ── Get ──────────────────────────────────────────────────── */

const app_settings_t *settings_service_get(void)
{
    return &s_settings;
}

/* ── Update ───────────────────────────────────────────────── */

esp_err_t settings_service_update(const app_settings_t *new_settings)
{
    memcpy(&s_settings, new_settings, sizeof(s_settings));
    apply_defaults(&s_settings);

    return save();
}

/* ── Theme ───────────────────────────────────────────────── */

esp_err_t settings_service_set_theme(const char *theme)
{
    snprintf(s_settings.theme, sizeof(s_settings.theme), "%s", theme);
    return save();
}

/* ── Convenience ─────────────────────────────────────────── */

int settings_service_get_threshold(void)
{
    return s_settings.low_stock_threshold_grams;
}
