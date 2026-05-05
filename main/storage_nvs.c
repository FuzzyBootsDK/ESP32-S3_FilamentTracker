#include "storage_nvs.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#define TAG           "nvs"
#define NVS_NAMESPACE "filament_app"

static nvs_handle_t s_handle;
static bool         s_ready = false;

/* ── Init ─────────────────────────────────────────────────── */

esp_err_t storage_nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase (reason: %s)", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_ready = true;
    ESP_LOGI(TAG, "NVS initialised (namespace: %s)", NVS_NAMESPACE);
    return ESP_OK;
}

/* ── Helpers ─────────────────────────────────────────────── */

#define CHECK_READY() do { if (!s_ready) return ESP_ERR_INVALID_STATE; } while (0)

/* ── u32 ─────────────────────────────────────────────────── */

esp_err_t storage_nvs_get_u32(const char *key, uint32_t *out, uint32_t default_val)
{
    CHECK_READY();
    esp_err_t ret = nvs_get_u32(s_handle, key, out);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *out = default_val;
        return ESP_OK;
    }
    return ret;
}

esp_err_t storage_nvs_set_u32(const char *key, uint32_t value)
{
    CHECK_READY();
    esp_err_t ret = nvs_set_u32(s_handle, key, value);
    if (ret != ESP_OK) return ret;
    return nvs_commit(s_handle);
}

/* ── str ─────────────────────────────────────────────────── */

esp_err_t storage_nvs_get_str(const char *key, char *buf, size_t buf_len)
{
    CHECK_READY();
    size_t len = buf_len;
    esp_err_t ret = nvs_get_str(s_handle, key, buf, &len);
    return ret;
}

esp_err_t storage_nvs_set_str(const char *key, const char *value)
{
    CHECK_READY();
    esp_err_t ret = nvs_set_str(s_handle, key, value);
    if (ret != ESP_OK) return ret;
    return nvs_commit(s_handle);
}

/* ── blob ─────────────────────────────────────────────────── */

esp_err_t storage_nvs_get_blob(const char *key, void *buf, size_t *len)
{
    CHECK_READY();
    return nvs_get_blob(s_handle, key, buf, len);
}

esp_err_t storage_nvs_set_blob(const char *key, const void *buf, size_t len)
{
    CHECK_READY();
    esp_err_t ret = nvs_set_blob(s_handle, key, buf, len);
    if (ret != ESP_OK) return ret;
    return nvs_commit(s_handle);
}

/* ── erase ───────────────────────────────────────────────── */

esp_err_t storage_nvs_erase_key(const char *key)
{
    CHECK_READY();
    esp_err_t ret = nvs_erase_key(s_handle, key);
    if (ret == ESP_ERR_NVS_NOT_FOUND) return ESP_OK; /* already gone */
    if (ret != ESP_OK) return ret;
    return nvs_commit(s_handle);
}
