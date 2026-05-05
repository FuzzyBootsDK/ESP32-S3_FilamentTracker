#pragma once

#include "esp_err.h"
#include "model_ams.h"
#include "cJSON.h"
#include <stdbool.h>

esp_err_t ams_service_init(void);
esp_err_t ams_service_save(void);
void      ams_service_process_dirty(void);

/* ── Links ───────────────────────────────────────────────── */
esp_err_t ams_service_list_units(cJSON **out);
esp_err_t ams_service_list_links(cJSON **out);
esp_err_t ams_service_create_link(const ams_link_t *req, char *new_id);
esp_err_t ams_service_update_link(const char *id, const ams_link_t *req);
esp_err_t ams_service_delete_link(const char *id);
esp_err_t ams_service_sync(int *out_updated);

/* ── Helpers used by inventory_service ───────────────────── */
bool ams_service_type_has_links(const char *type_id);
bool ams_service_spool_is_linked(const char *spool_id, char *out_link_id);

/** Update last_sync_weight for the link associated with a spool. */
void ams_service_update_weight(const char *spool_id, int weight);

/* needed by json_codec / api_http */
#include "cJSON.h"
