#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdint.h>

/** Initialise the WebSocket broadcaster (must be called after HTTP server starts). */
esp_err_t ws_init(httpd_handle_t server);

/** WebSocket upgrade handler — register this on the /ws route. */
esp_err_t ws_handler(httpd_req_t *req);

/* ── Broadcast helpers (called by services) ──────────────── */
void ws_broadcast_inventory_created(const char *type_id);
void ws_broadcast_inventory_updated(const char *type_id, int remaining_grams, uint32_t ts);
void ws_broadcast_inventory_deleted(const char *type_id);
void ws_broadcast_ams_updated(const char *ams_id, int slot);
void ws_broadcast_mqtt_status_updated(void);
void ws_broadcast_runtime_updated(void);
void ws_broadcast_mqtt_message_received(const char *topic, int topic_len,
										const char *payload, int payload_len);
void ws_broadcast_settings_updated(void);
void ws_broadcast_storage_saved(void);
