#pragma once

#include "esp_err.h"
#include "model_runtime.h"
#include "model_settings.h"
#include <stdbool.h>

esp_err_t mqtt_service_init(const mqtt_settings_t *cfg);
esp_err_t mqtt_service_connect(void);
esp_err_t mqtt_service_disconnect(void);
esp_err_t mqtt_service_apply_settings(const mqtt_settings_t *cfg);

/** Test connectivity with provided (possibly unsaved) credentials. */
esp_err_t mqtt_service_test(const mqtt_settings_t *cfg, bool *reachable, bool *authenticated);

/** Thread-safe snapshot of the current runtime state. */
void mqtt_service_get_runtime(mqtt_runtime_t *out);

/** Is the MQTT client currently connected? */
bool mqtt_service_is_connected(void);

/** Fill an mqtt_status JSON object for the API. */
#include "cJSON.h"
cJSON *mqtt_service_status_json(void);
