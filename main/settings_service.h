#pragma once

#include "esp_err.h"
#include "model_settings.h"

/**
 * Load settings from NVS. Applies defaults for any missing keys.
 * Must be called after storage_nvs_init().
 */
esp_err_t settings_service_init(void);

/** Get a read-only pointer to the current settings. */
const app_settings_t *settings_service_get(void);

/**
 * Replace settings entirely from a new struct, persist to NVS.
 * Callers should merge against current settings when doing partial updates.
 */
esp_err_t settings_service_update(const app_settings_t *new_settings);

/** Update only the theme field. */
esp_err_t settings_service_set_theme(const char *theme);

/** Convenience: get low-stock threshold. */
int settings_service_get_threshold(void);
