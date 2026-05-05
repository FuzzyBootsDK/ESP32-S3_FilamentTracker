#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

/**
 * Initialise NVS flash and open the application namespace.
 * Must be called once before any other storage_nvs_* function.
 */
esp_err_t storage_nvs_init(void);

esp_err_t storage_nvs_get_u32(const char *key, uint32_t *out, uint32_t default_val);
esp_err_t storage_nvs_set_u32(const char *key, uint32_t value);

esp_err_t storage_nvs_get_str(const char *key, char *buf, size_t buf_len);
esp_err_t storage_nvs_set_str(const char *key, const char *value);

esp_err_t storage_nvs_get_blob(const char *key, void *buf, size_t *len);
esp_err_t storage_nvs_set_blob(const char *key, const void *buf, size_t len);

esp_err_t storage_nvs_erase_key(const char *key);
