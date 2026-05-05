#pragma once

#include "esp_err.h"
#include "cJSON.h"

esp_err_t help_service_init(void);
esp_err_t help_service_list(cJSON **out);
esp_err_t help_service_get_section(const char *section_id, cJSON **out);
