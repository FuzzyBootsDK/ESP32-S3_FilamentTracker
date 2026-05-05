#include "api_http.h"
#include "api_ws.h"

#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "sntp_service.h"

#include "inventory_service.h"
#include "ams_service.h"
#include "mqtt_service.h"
#include "settings_service.h"
#include "help_service.h"
#include "storage_fs.h"
#include "json_codec.h"
#include "wifi_manager.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_idf_version.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

#define TAG              "http"
#define MAX_BODY_LEN     (64 * 1024)  /* 64 KB max request body */
#define QUERY_BUF_LEN    512

/* ─────────────────────────────────────────────────────────── */
/* Shared helpers                                              */
/* ─────────────────────────────────────────────────────────── */

static esp_err_t send_json(httpd_req_t *req, int status, cJSON *payload)
{
    const char *status_str;
    switch (status) {
        case 200: status_str = "200 OK"; break;
        case 201: status_str = "201 Created"; break;
        case 400: status_str = "400 Bad Request"; break;
        case 404: status_str = "404 Not Found"; break;
        case 409: status_str = "409 Conflict"; break;
        case 422: status_str = "422 Unprocessable Entity"; break;
        default:  status_str = "500 Internal Server Error"; break;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, status_str);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    char *body = cJSON_PrintUnformatted(payload);
    if (!body) { httpd_resp_send_500(req); return ESP_FAIL; }

    esp_err_t ret = httpd_resp_send(req, body, (ssize_t)strlen(body));
    cJSON_free(body);
    return ret;
}

static cJSON *ok(cJSON *data)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    if (data) cJSON_AddItemToObject(r, "data", data);
    return r;
}

static cJSON *err(const char *code, const char *msg)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", false);
    cJSON *e = cJSON_AddObjectToObject(r, "error");
    cJSON_AddStringToObject(e, "code",    code);
    cJSON_AddStringToObject(e, "message", msg);
    return r;
}

/* Read entire request body into a malloc'd buffer. Caller must free. */
static esp_err_t read_body(httpd_req_t *req, char **out_buf, int *out_len)
{
    int len = req->content_len;
    if (len <= 0 || len > MAX_BODY_LEN) return ESP_ERR_INVALID_ARG;

    char *buf = malloc(len + 1);
    if (!buf) return ESP_ERR_NO_MEM;

    int received = httpd_req_recv(req, buf, len);
    if (received <= 0) { free(buf); return ESP_FAIL; }

    buf[received] = '\0';
    *out_buf = buf;
    *out_len = received;
    return ESP_OK;
}

/* Parse JSON body; caller must cJSON_Delete the result. */
static esp_err_t parse_json_body(httpd_req_t *req, cJSON **out)
{
    char *buf = NULL; int len = 0;
    esp_err_t ret = read_body(req, &buf, &len);
    if (ret != ESP_OK) return ret;
    *out = cJSON_ParseWithLength(buf, len);
    free(buf);
    return *out ? ESP_OK : ESP_ERR_INVALID_ARG;
}

/* Get a query parameter value. Returns true if found. */
static bool qparam(httpd_req_t *req, const char *key, char *buf, size_t len)
{
    char query[QUERY_BUF_LEN];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) return false;
    return httpd_query_key_value(query, key, buf, (int)len) == ESP_OK;
}

/* Extract the last path segment (after the final '/') */
static const char *path_tail(const char *uri)
{
    const char *p = strrchr(uri, '/');
    return p ? p + 1 : uri;
}

/* Extract path segment at given depth (0-based).
   e.g. /api/v1/inventory/ftype_000001/spools/spool_000001
        depth 3 → "ftype_000001", depth 5 → "spool_000001" */
static bool path_segment(const char *uri, int depth, char *buf, size_t len)
{
    const char *p = uri;
    int d = 0;
    while (*p && d < depth) { if (*p++ == '/') d++; }
    if (!*p) return false;
    const char *end = strchr(p, '/');
    size_t seg_len = end ? (size_t)(end - p) : strlen(p);
    if (seg_len >= len) seg_len = len - 1;
    memcpy(buf, p, seg_len);
    buf[seg_len] = '\0';
    return seg_len > 0;
}

static uint32_t now_ts(void) { time_t t = time(NULL); return (t > 0) ? (uint32_t)t : 0; }

/* ─────────────────────────────────────────────────────────── */
/* 1. Health & System                                          */
/* ─────────────────────────────────────────────────────────── */

static esp_err_t health_handler(httpd_req_t *req)
{
    size_t fs_total = 0, fs_used = 0;
    storage_fs_get_info(&fs_total, &fs_used);

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "status",           "ok");
    cJSON_AddNumberToObject(data, "uptime_seconds",   (double)(esp_timer_get_time() / 1000000ULL));
    cJSON_AddNumberToObject(data, "heap_free",        (double)esp_get_free_heap_size());
    cJSON_AddBoolToObject  (data, "mqtt_connected",   mqtt_service_is_connected());
    cJSON_AddBoolToObject  (data, "storage_mounted",  true);
    cJSON_AddNumberToObject(data, "schema_version",   1);
    cJSON_AddStringToObject(data, "firmware_version", "0.1.0");
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t system_info_handler(httpd_req_t *req)
{
    size_t fs_total = 0, fs_used = 0;
    storage_fs_get_info(&fs_total, &fs_used);
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    cJSON *data = cJSON_CreateObject();
    const app_settings_t *s = settings_service_get();
    cJSON_AddStringToObject(data, "device_name",         s->device_name);
    cJSON_AddStringToObject(data, "chip_model",          "ESP32-S3");
    cJSON_AddNumberToObject(data, "flash_mb",            16);
    cJSON_AddNumberToObject(data, "psram_mb",            8);
    cJSON_AddStringToObject(data, "firmware_version",    "0.1.0");
    cJSON_AddStringToObject(data, "build_date",          __DATE__);
    cJSON_AddNumberToObject(data, "filesystem_total_kb", (double)(fs_total / 1024));
    cJSON_AddNumberToObject(data, "filesystem_used_kb",  (double)(fs_used  / 1024));
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────── */
/* 2. Settings                                                 */
/* ─────────────────────────────────────────────────────────── */

static esp_err_t settings_get_handler(httpd_req_t *req)
{
    cJSON *data = json_encode_settings(settings_service_get(), true);
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t settings_put_handler(httpd_req_t *req)
{
    cJSON *body = NULL;
    if (parse_json_body(req, &body) != ESP_OK) {
        cJSON *r = err("bad_request", "Invalid JSON"); send_json(req, 400, r); cJSON_Delete(r);
        return ESP_OK;
    }

    app_settings_t new_s;
    memcpy(&new_s, settings_service_get(), sizeof(new_s));
    json_decode_settings(body, &new_s);
    cJSON_Delete(body);

    settings_service_update(&new_s);
    mqtt_service_apply_settings(&new_s.mqtt);
    sntp_service_set_timezone(new_s.timezone); /* re-apply TZ if changed */
    ws_broadcast_settings_updated();

    cJSON *data = cJSON_CreateObject(); cJSON_AddBoolToObject(data, "updated", true);
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t settings_theme_handler(httpd_req_t *req)
{
    cJSON *body = NULL;
    if (parse_json_body(req, &body) != ESP_OK) {
        cJSON *r = err("bad_request", "Invalid JSON"); send_json(req, 400, r); cJSON_Delete(r);
        return ESP_OK;
    }

    char theme[THEME_LEN] = {0};
    json_copy_str(body, "theme", theme, sizeof(theme));
    cJSON_Delete(body);

    if (!theme[0]) {
        cJSON *r = err("validation_error", "theme is required"); send_json(req, 422, r); cJSON_Delete(r);
        return ESP_OK;
    }

    settings_service_set_theme(theme);
    ws_broadcast_settings_updated();

    cJSON *data = cJSON_CreateObject(); cJSON_AddStringToObject(data, "theme", theme);
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t settings_restart_handler(httpd_req_t *req)
{
    cJSON *data = cJSON_CreateObject(); cJSON_AddBoolToObject(data, "restarting", true);
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    esp_restart();
    return ESP_OK;
}

/* POST /api/v1/settings/wifi/reset — erase Wi-Fi credentials and reboot into AP mode */
static esp_err_t wifi_reset_handler(httpd_req_t *req)
{
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "resetting", true);
    cJSON_AddStringToObject(data, "message",
        "Wi-Fi credentials erased. Connect to the 'FilamentTracker' AP to re-provision.");
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    wifi_manager_reset_credentials(); /* erases NVS and calls esp_restart() */
    return ESP_OK;
}

static esp_err_t settings_export_handler(httpd_req_t *req)
{
    const app_settings_t *s = settings_service_get();
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "device_name",                s->device_name);
    cJSON_AddStringToObject(data, "theme",                      s->theme);
    cJSON_AddStringToObject(data, "timezone",                   s->timezone);
    cJSON_AddNumberToObject(data, "low_stock_threshold_grams",  s->low_stock_threshold_grams);
    cJSON *mqtt = cJSON_AddObjectToObject(data, "mqtt");
    cJSON_AddBoolToObject  (mqtt, "enabled",    s->mqtt.enabled);
    cJSON_AddStringToObject(mqtt, "broker_host",s->mqtt.broker_host);
    cJSON_AddNumberToObject(mqtt, "broker_port",s->mqtt.broker_port);
    cJSON_AddStringToObject(mqtt, "username",   s->mqtt.username);
    cJSON_AddStringToObject(mqtt, "client_id",  s->mqtt.client_id);
    cJSON_AddStringToObject(mqtt, "topic_root", s->mqtt.topic_root);
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────── */
/* 3. Inventory                                                */
/* ─────────────────────────────────────────────────────────── */

static esp_err_t inventory_options_handler(httpd_req_t *req)
{
    cJSON *data = NULL;
    inventory_service_get_options(&data);
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t inventory_list_handler(httpd_req_t *req)
{
    inventory_query_t q; memset(&q, 0, sizeof(q));
    char tmp[QUERY_BUF_LEN];

    if (qparam(req, "q",             q.q,        sizeof(q.q)))        {}
    if (qparam(req, "material",      q.material,  sizeof(q.material))) {}
    if (qparam(req, "brand",         q.brand,     sizeof(q.brand)))    {}
    if (qparam(req, "color",         q.color,     sizeof(q.color)))    {}
    if (qparam(req, "sort",          q.sort,      sizeof(q.sort)))     {}
    if (qparam(req, "dir",           q.dir,       sizeof(q.dir)))      {}

    q.archived       = -1;
    q.low_stock_only = -1;
    if (qparam(req, "archived",       tmp, sizeof(tmp))) q.archived       = (strcmp(tmp,"true")==0) ? 1 : 0;
    if (qparam(req, "low_stock_only", tmp, sizeof(tmp))) q.low_stock_only = (strcmp(tmp,"true")==0) ? 1 : 0;
    if (qparam(req, "page",           tmp, sizeof(tmp))) q.page      = atoi(tmp);
    if (qparam(req, "page_size",      tmp, sizeof(tmp))) q.page_size = atoi(tmp);
    if (q.page      <= 0) q.page      = 1;
    if (q.page_size <= 0) q.page_size = 25;
    /* Keep pagination math aligned with inventory_service_list() limits. */
    if (q.page_size > 100) q.page_size = 100;

    filament_list_item_t *items = NULL;
    int total = 0;
    inventory_service_list(&q, &items, &total);

    int page_count = ((total + q.page_size - 1) / q.page_size);
    /* Compute the number of items actually on this page (matches what the
       service allocated) — do NOT walk past the end of the items array. */
    int start = (q.page - 1) * q.page_size;
    int count = total - start;
    if (count < 0)         count = 0;
    if (count > q.page_size) count = q.page_size;

    cJSON *data    = cJSON_CreateObject();
    cJSON *arr     = cJSON_AddArrayToObject(data, "items");
    for (int i = 0; i < count; i++) cJSON_AddItemToArray(arr, json_encode_filament_list_item(&items[i]));
    cJSON_AddNumberToObject(data, "page",        q.page);
    cJSON_AddNumberToObject(data, "page_size",   q.page_size);
    cJSON_AddNumberToObject(data, "total_items", total);
    cJSON_AddNumberToObject(data, "total_pages", page_count > 0 ? page_count : 1);
    free(items);

    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t inventory_create_handler(httpd_req_t *req)
{
    cJSON *body = NULL;
    if (parse_json_body(req, &body) != ESP_OK) {
        cJSON *r = err("bad_request", "Invalid JSON"); send_json(req, 400, r); cJSON_Delete(r);
        return ESP_OK;
    }

    filament_type_t ft; memset(&ft, 0, sizeof(ft));
    json_decode_filament_type(body, &ft);
    cJSON_Delete(body);

    /* Validation */
    if (!ft.brand[0] || !ft.material[0] || !ft.color[0]) {
        cJSON *r = err("validation_error", "brand, material and color are required");
        send_json(req, 422, r); cJSON_Delete(r); return ESP_OK;
    }
    if (ft.total_grams <= 0) ft.total_grams = 1000;
    if (ft.price_per_kg < 0) ft.price_per_kg = 0;

    char new_id[FTYPE_ID_LEN];
    if (inventory_service_create_type(&ft, new_id) != ESP_OK) {
        cJSON *r = err("storage_error", "Failed to create filament type");
        send_json(req, 500, r); cJSON_Delete(r); return ESP_OK;
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "id",      new_id);
    cJSON_AddBoolToObject  (data, "created", true);
    cJSON *r = ok(data); send_json(req, 201, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t inventory_reset_handler(httpd_req_t *req)
{
    inventory_service_reset_all();
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "reset", true);
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

/* Wildcard GET /api/v1/inventory/{id}
   Also dispatches GET /api/v1/inventory/{id}/spools and sub-paths */
static esp_err_t inventory_get_handler(httpd_req_t *req)
{
    const char *uri = req->uri;

    /* /api/v1/inventory/{type_id}/spools */
    if (strstr(uri, "/spools")) {
        char type_id[FTYPE_ID_LEN]; char spool_id[SPOOL_ID_LEN] = {0};
        path_segment(uri + strlen("/api/v1/inventory/"), 0, type_id, sizeof(type_id));
        const char *sp = strstr(uri, "/spools/");
        if (sp) snprintf(spool_id, sizeof(spool_id), "%s", sp + 8);

        filament_type_t ft;
        if (inventory_service_get_type(type_id, &ft) != ESP_OK) {
            cJSON *r = err("not_found", "Filament type not found"); send_json(req, 404, r); cJSON_Delete(r);
            return ESP_OK;
        }

        if (spool_id[0]) {
            /* GET /api/v1/inventory/{id}/spools/{spool_id} */
            for (int i = 0; i < ft.spool_count; i++) {
                if (strcmp(ft.spools[i].id, spool_id) == 0) {
                    cJSON *r = ok(json_encode_spool(&ft.spools[i]));
                    send_json(req, 200, r); cJSON_Delete(r); return ESP_OK;
                }
            }
            cJSON *r = err("not_found", "Spool not found"); send_json(req, 404, r); cJSON_Delete(r);
        } else {
            /* GET /api/v1/inventory/{id}/spools */
            cJSON *data = cJSON_CreateObject();
            cJSON *arr  = cJSON_AddArrayToObject(data, "items");
            for (int i = 0; i < ft.spool_count; i++) cJSON_AddItemToArray(arr, json_encode_spool(&ft.spools[i]));
            cJSON_AddNumberToObject(data, "spool_count", ft.spool_count);
            cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
        }
        return ESP_OK;
    }

    /* GET /api/v1/inventory/{id} */
    char id[FTYPE_ID_LEN];
    snprintf(id, sizeof(id), "%s", path_tail(uri));
    filament_type_t ft;
    if (inventory_service_get_type(id, &ft) != ESP_OK) {
        cJSON *r = err("not_found", "Filament type not found"); send_json(req, 404, r); cJSON_Delete(r);
        return ESP_OK;
    }
    cJSON *r = ok(json_encode_filament_type(&ft)); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t inventory_put_handler(httpd_req_t *req)
{
    const char *uri = req->uri;

    /* PUT /api/v1/inventory/{type_id}/spools/{spool_id} */
    if (strstr(uri, "/spools/")) {
        char type_id[FTYPE_ID_LEN]; char spool_id[SPOOL_ID_LEN] = {0};
        path_segment(uri + strlen("/api/v1/inventory/"), 0, type_id, sizeof(type_id));
        const char *sp = strstr(uri, "/spools/");
        if (sp) snprintf(spool_id, sizeof(spool_id), "%s", sp + 8);

        cJSON *body = NULL;
        if (parse_json_body(req, &body) != ESP_OK) {
            cJSON *r = err("bad_request", "Invalid JSON"); send_json(req, 400, r); cJSON_Delete(r); return ESP_OK;
        }
        spool_t s; memset(&s, 0, sizeof(s));
        json_decode_spool(body, &s);
        cJSON_Delete(body);

        if (inventory_service_update_spool(type_id, spool_id, &s) != ESP_OK) {
            cJSON *r = err("not_found", "Spool not found"); send_json(req, 404, r); cJSON_Delete(r); return ESP_OK;
        }
        cJSON *data = cJSON_CreateObject(); cJSON_AddBoolToObject(data, "updated", true);
        cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
        return ESP_OK;
    }

    /* PUT /api/v1/inventory/{id} */
    char id[FTYPE_ID_LEN]; snprintf(id, sizeof(id), "%s", path_tail(uri));
    cJSON *body = NULL;
    if (parse_json_body(req, &body) != ESP_OK) {
        cJSON *r = err("bad_request", "Invalid JSON"); send_json(req, 400, r); cJSON_Delete(r); return ESP_OK;
    }
    filament_type_t ft; memset(&ft, 0, sizeof(ft));
    json_decode_filament_type(body, &ft);
    cJSON_Delete(body);

    esp_err_t ret = inventory_service_update_type(id, &ft);
    if (ret == ESP_ERR_NOT_FOUND) {
        cJSON *r = err("not_found", "Filament type not found"); send_json(req, 404, r); cJSON_Delete(r); return ESP_OK;
    }
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject  (data, "updated",    true);
    cJSON_AddStringToObject(data, "id",         id);
    cJSON_AddNumberToObject(data, "updated_at", (double)now_ts());
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t inventory_patch_handler(httpd_req_t *req)
{
    const char *uri = req->uri;

    /* PATCH /api/v1/inventory/{type_id}/spools/{spool_id}/grams */
    const char *sp = strstr(uri, "/spools/");
    if (sp) {
        char type_id[FTYPE_ID_LEN]; char spool_id[SPOOL_ID_LEN] = {0};
        path_segment(uri + strlen("/api/v1/inventory/"), 0, type_id, sizeof(type_id));
        /* spool_id is between /spools/ and /grams */
        char rest[SPOOL_ID_LEN + 8]; strlcpy(rest, sp + 8, sizeof(rest));
        char *slash = strchr(rest, '/'); if (slash) *slash = '\0';
        strlcpy(spool_id, rest, sizeof(spool_id));

        cJSON *body = NULL;
        if (parse_json_body(req, &body) != ESP_OK) {
            cJSON *r = err("bad_request", "Invalid JSON"); send_json(req, 400, r); cJSON_Delete(r); return ESP_OK;
        }
        int grams = json_get_int(body, "remaining_grams", -1);
        cJSON_Delete(body);
        if (grams < 0) {
            cJSON *r = err("validation_error", "remaining_grams required"); send_json(req, 422, r); cJSON_Delete(r); return ESP_OK;
        }
        uint32_t ts = 0;
        inventory_service_update_spool_grams(type_id, spool_id, grams, &ts);
        cJSON *data = cJSON_CreateObject();
        cJSON_AddBoolToObject  (data, "updated",         true);
        cJSON_AddStringToObject(data, "spool_id",        spool_id);
        cJSON_AddNumberToObject(data, "remaining_grams", grams);
        cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
        return ESP_OK;
    }

    /* PATCH /api/v1/inventory/{id}/grams (shortcut on type, applies to first active spool) */
    cJSON *body = NULL;
    if (parse_json_body(req, &body) != ESP_OK) {
        cJSON *r = err("bad_request", "Invalid JSON"); send_json(req, 400, r); cJSON_Delete(r); return ESP_OK;
    }
    int remaining_grams = json_get_int(body, "remaining_grams", -1);
    int consumed_grams  = json_get_int(body, "consumed_grams", -1);
    cJSON_Delete(body);

    char type_id[FTYPE_ID_LEN];
    snprintf(type_id, sizeof(type_id), "%s", path_tail(uri));

    filament_type_t ft;
    if (inventory_service_get_type(type_id, &ft) != ESP_OK) {
        cJSON *r = err("not_found", "Filament type not found"); send_json(req, 404, r); cJSON_Delete(r); return ESP_OK;
    }

    spool_t *active = NULL;
    for (int i = 0; i < ft.spool_count; i++) {
        if (!ft.spools[i].archived) { active = &ft.spools[i]; break; }
    }
    if (!active && ft.spool_count > 0) active = &ft.spools[0];
    if (!active) {
        cJSON *r = err("invalid_state", "No spool found for this filament type");
        send_json(req, 409, r); cJSON_Delete(r); return ESP_OK;
    }

    int new_grams = -1;
    if (remaining_grams >= 0) {
        new_grams = remaining_grams;
    } else if (consumed_grams > 0) {
        new_grams = active->remaining_grams - consumed_grams;
    } else {
        cJSON *r = err("validation_error", "Provide remaining_grams or consumed_grams");
        send_json(req, 422, r); cJSON_Delete(r); return ESP_OK;
    }

    uint32_t ts = 0;
    esp_err_t ret = inventory_service_update_spool_grams(type_id, active->id, new_grams, &ts);
    if (ret != ESP_OK) {
        cJSON *r = err("storage_error", "Failed to update spool grams");
        send_json(req, 500, r); cJSON_Delete(r); return ESP_OK;
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "updated", true);
    cJSON_AddStringToObject(data, "id", type_id);
    cJSON_AddStringToObject(data, "spool_id", active->id);
    cJSON_AddNumberToObject(data, "remaining_grams", new_grams < 0 ? 0 : new_grams);
    if (consumed_grams > 0) cJSON_AddNumberToObject(data, "consumed_grams", consumed_grams);
    cJSON_AddNumberToObject(data, "updated_at", (double)ts);
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t inventory_post_handler(httpd_req_t *req)
{
    const char *uri = req->uri;

    /* POST /api/v1/inventory/{type_id}/spools */
    if (strstr(uri, "/spools")) {
        char type_id[FTYPE_ID_LEN];
        path_segment(uri + strlen("/api/v1/inventory/"), 0, type_id, sizeof(type_id));

        cJSON *body = NULL;
        if (parse_json_body(req, &body) != ESP_OK) {
            cJSON *r = err("bad_request", "Invalid JSON"); send_json(req, 400, r); cJSON_Delete(r); return ESP_OK;
        }
        spool_t s; memset(&s, 0, sizeof(s));
        json_decode_spool(body, &s);
        cJSON_Delete(body);

        /* Validate tag_uid uniqueness */
        if (s.tag_uid[0]) {
            if (inventory_service_tag_uid_exists(s.tag_uid, NULL, NULL)) {
                cJSON *r = err("conflict", "tag_uid already assigned to another spool");
                send_json(req, 409, r); cJSON_Delete(r); return ESP_OK;
            }
        }

        char new_id[SPOOL_ID_LEN];
        if (inventory_service_create_spool(type_id, &s, new_id) != ESP_OK) {
            cJSON *r = err("not_found", "Filament type not found"); send_json(req, 404, r); cJSON_Delete(r); return ESP_OK;
        }
        cJSON *data = cJSON_CreateObject();
        cJSON_AddStringToObject(data, "id",      new_id);
        cJSON_AddBoolToObject  (data, "created", true);
        cJSON *r = ok(data); send_json(req, 201, r); cJSON_Delete(r);
        return ESP_OK;
    }

    /* POST /api/v1/inventory/{id}/archive */
    if (strstr(uri, "/archive")) {
        char type_id[FTYPE_ID_LEN];
        path_segment(uri + strlen("/api/v1/inventory/"), 0, type_id, sizeof(type_id));

        cJSON *body = NULL;
        parse_json_body(req, &body);
        bool archived = body ? json_get_bool(body, "archived", true) : true;
        if (body) cJSON_Delete(body);

        if (inventory_service_archive_type(type_id, archived) != ESP_OK) {
            cJSON *r = err("not_found", "Not found"); send_json(req, 404, r); cJSON_Delete(r); return ESP_OK;
        }
        cJSON *data = cJSON_CreateObject();
        cJSON_AddStringToObject(data, "id",       type_id);
        cJSON_AddBoolToObject  (data, "archived",  archived);
        cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
        return ESP_OK;
    }

    cJSON *r = err("not_found", "Not found"); send_json(req, 404, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t inventory_delete_handler(httpd_req_t *req)
{
    const char *uri = req->uri;

    /* DELETE /api/v1/inventory/{type_id}/spools/{spool_id} */
    const char *sp = strstr(uri, "/spools/");
    if (sp) {
        char type_id[FTYPE_ID_LEN]; char spool_id[SPOOL_ID_LEN] = {0};
        path_segment(uri + strlen("/api/v1/inventory/"), 0, type_id, sizeof(type_id));
        snprintf(spool_id, sizeof(spool_id), "%s", sp + 8);

        esp_err_t ret = inventory_service_delete_spool(type_id, spool_id);
        if (ret == ESP_ERR_NOT_FOUND) {
            cJSON *r = err("not_found", "Spool not found"); send_json(req, 404, r); cJSON_Delete(r); return ESP_OK;
        }
        cJSON *data = cJSON_CreateObject();
        cJSON_AddBoolToObject  (data, "deleted",  true);
        cJSON_AddStringToObject(data, "spool_id", spool_id);
        cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
        return ESP_OK;
    }

    /* DELETE /api/v1/inventory/{id} */
    char id[FTYPE_ID_LEN]; snprintf(id, sizeof(id), "%s", path_tail(uri));
    char tmp[8] = {0};
    bool force = qparam(req, "force", tmp, sizeof(tmp)) && strcmp(tmp, "true") == 0;

    esp_err_t ret = inventory_service_delete_type(id, force);
    if (ret == ESP_ERR_NOT_FOUND) {
        cJSON *r = err("not_found", "Not found"); send_json(req, 404, r); cJSON_Delete(r); return ESP_OK;
    }
    if (ret == ESP_ERR_INVALID_STATE) {
        cJSON *r = err("conflict", "Filament type has active AMS links. Use force=true to override.");
        send_json(req, 409, r); cJSON_Delete(r); return ESP_OK;
    }
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject  (data, "deleted", true);
    cJSON_AddStringToObject(data, "id",      id);
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────── */
/* 4. AMS                                                      */
/* ─────────────────────────────────────────────────────────── */

static esp_err_t ams_get_handler(httpd_req_t *req)
{
    cJSON *data = NULL; ams_service_list_units(&data);
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t ams_links_handler(httpd_req_t *req)
{
    cJSON *data = NULL; ams_service_list_links(&data);
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t ams_link_create_handler(httpd_req_t *req)
{
    cJSON *body = NULL;
    if (parse_json_body(req, &body) != ESP_OK) {
        cJSON *r = err("bad_request", "Invalid JSON"); send_json(req, 400, r); cJSON_Delete(r); return ESP_OK;
    }
    ams_link_t l; memset(&l, 0, sizeof(l));
    json_decode_ams_link(body, &l);
    cJSON_Delete(body);

    if (!l.ams_id[0] || !l.spool_id[0]) {
        cJSON *r = err("validation_error", "ams_id and spool_id required");
        send_json(req, 422, r); cJSON_Delete(r); return ESP_OK;
    }

    char new_id[AMS_LINK_ID_LEN];
    esp_err_t ret = ams_service_create_link(&l, new_id);
    if (ret == ESP_ERR_INVALID_STATE) {
        cJSON *r = err("conflict", "Slot already occupied"); send_json(req, 409, r); cJSON_Delete(r); return ESP_OK;
    }
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject  (data, "linked", true);
    cJSON_AddStringToObject(data, "id",     new_id);
    cJSON *r = ok(data); send_json(req, 201, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t ams_link_update_handler(httpd_req_t *req)
{
    char id[AMS_LINK_ID_LEN]; snprintf(id, sizeof(id), "%s", path_tail(req->uri));
    cJSON *body = NULL;
    if (parse_json_body(req, &body) != ESP_OK) {
        cJSON *r = err("bad_request", "Invalid JSON"); send_json(req, 400, r); cJSON_Delete(r); return ESP_OK;
    }
    ams_link_t l; memset(&l, 0, sizeof(l));
    json_decode_ams_link(body, &l);
    cJSON_Delete(body);

    if (ams_service_update_link(id, &l) == ESP_ERR_NOT_FOUND) {
        cJSON *r = err("not_found", "Link not found"); send_json(req, 404, r); cJSON_Delete(r); return ESP_OK;
    }
    cJSON *data = cJSON_CreateObject(); cJSON_AddBoolToObject(data, "updated", true); cJSON_AddStringToObject(data, "id", id);
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t ams_link_delete_handler(httpd_req_t *req)
{
    char id[AMS_LINK_ID_LEN]; snprintf(id, sizeof(id), "%s", path_tail(req->uri));
    if (ams_service_delete_link(id) == ESP_ERR_NOT_FOUND) {
        cJSON *r = err("not_found", "Link not found"); send_json(req, 404, r); cJSON_Delete(r); return ESP_OK;
    }
    cJSON *data = cJSON_CreateObject(); cJSON_AddBoolToObject(data, "deleted", true); cJSON_AddStringToObject(data, "id", id);
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t ams_sync_handler(httpd_req_t *req)
{
    int updated = 0; ams_service_sync(&updated);
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject  (data, "synced",        true);
    cJSON_AddNumberToObject(data, "updated_links", updated);
    cJSON_AddNumberToObject(data, "timestamp",     (double)now_ts());
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────── */
/* 5. MQTT                                                     */
/* ─────────────────────────────────────────────────────────── */

static esp_err_t mqtt_status_handler(httpd_req_t *req)
{
    cJSON *r = ok(mqtt_service_status_json()); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t mqtt_runtime_handler(httpd_req_t *req)
{
    mqtt_runtime_t rt; mqtt_service_get_runtime(&rt);
    cJSON *r = ok(json_encode_runtime(&rt)); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t mqtt_connect_handler(httpd_req_t *req)
{
    /* Optional body can override MQTT settings for immediate connect. */
    if (req->content_len > 0) {
        cJSON *body = NULL;
        if (parse_json_body(req, &body) != ESP_OK) {
            cJSON *r = err("bad_request", "Invalid JSON"); send_json(req, 400, r); cJSON_Delete(r); return ESP_OK;
        }

        app_settings_t new_s;
        memcpy(&new_s, settings_service_get(), sizeof(new_s));

        json_copy_str(body, "broker_host", new_s.mqtt.broker_host, sizeof(new_s.mqtt.broker_host));
        int port = json_get_int(body, "broker_port", new_s.mqtt.broker_port);
        if (port > 0 && port <= 65535) new_s.mqtt.broker_port = port;
        json_copy_str(body, "username",   new_s.mqtt.username,   sizeof(new_s.mqtt.username));
        json_copy_str(body, "password",   new_s.mqtt.password,   sizeof(new_s.mqtt.password));
        json_copy_str(body, "client_id",  new_s.mqtt.client_id,  sizeof(new_s.mqtt.client_id));
        json_copy_str(body, "topic_root", new_s.mqtt.topic_root, sizeof(new_s.mqtt.topic_root));
        new_s.mqtt.enabled = true;
        cJSON_Delete(body);

        settings_service_update(&new_s);
        mqtt_service_apply_settings(&new_s.mqtt);
        ws_broadcast_settings_updated();
    } else {
        mqtt_service_connect();
    }

    cJSON *data = cJSON_CreateObject(); cJSON_AddBoolToObject(data, "requested", true);
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t mqtt_disconnect_handler(httpd_req_t *req)
{
    mqtt_service_disconnect();
    cJSON *data = cJSON_CreateObject(); cJSON_AddBoolToObject(data, "requested", true);
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t mqtt_test_handler(httpd_req_t *req)
{
    cJSON *body = NULL;
    if (parse_json_body(req, &body) != ESP_OK) {
        cJSON *r = err("bad_request", "Invalid JSON"); send_json(req, 400, r); cJSON_Delete(r); return ESP_OK;
    }
    mqtt_settings_t cfg; memset(&cfg, 0, sizeof(cfg));
    json_copy_str(body, "broker_host", cfg.broker_host, sizeof(cfg.broker_host));
    cfg.broker_port = json_get_int(body, "broker_port", 1883);
    json_copy_str(body, "username",   cfg.username,    sizeof(cfg.username));
    json_copy_str(body, "password",   cfg.password,    sizeof(cfg.password));
    json_copy_str(body, "topic_root", cfg.topic_root,  sizeof(cfg.topic_root));
    cJSON_Delete(body);

    bool reachable = false, authenticated = false;
    mqtt_service_test(&cfg, &reachable, &authenticated);

    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "reachable",     reachable);
    cJSON_AddBoolToObject(data, "authenticated", authenticated);
    cJSON_AddBoolToObject(data, "subscribed",    reachable && authenticated);
    cJSON_AddStringToObject(data, "note", "Heuristic only: use Connect and status for real broker/auth result");
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────── */
/* 6. Help                                                     */
/* ─────────────────────────────────────────────────────────── */

static esp_err_t help_list_handler(httpd_req_t *req)
{
    cJSON *data = NULL; help_service_list(&data);
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t help_section_handler(httpd_req_t *req)
{
    char sid[64]; snprintf(sid, sizeof(sid), "%s", path_tail(req->uri));
    cJSON *data = NULL;
    if (help_service_get_section(sid, &data) != ESP_OK) {
        cJSON *r = err("not_found", "Section not found"); send_json(req, 404, r); cJSON_Delete(r); return ESP_OK;
    }
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────── */
/* 7. Backup / Import                                          */
/* ─────────────────────────────────────────────────────────── */

static esp_err_t backup_export_handler(httpd_req_t *req)
{
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "schema_version", 1);
    cJSON_AddNumberToObject(data, "exported_at",    (double)now_ts());
    /* Minimal stubs — full export would serialize live stores */
    cJSON_AddObjectToObject(data, "settings");
    cJSON_AddArrayToObject(data, "inventory");
    cJSON_AddArrayToObject(data, "ams_links");
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t backup_import_handler(httpd_req_t *req)
{
    cJSON *r = err("not_implemented", "Backup import not yet implemented");
    send_json(req, 500, r); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t csv_import_handler(httpd_req_t *req)
{
    char *buf = NULL; int len = 0;
    if (read_body(req, &buf, &len) != ESP_OK) {
        cJSON *r = err("bad_request", "Missing CSV body"); send_json(req, 400, r); cJSON_Delete(r); return ESP_OK;
    }

    csv_import_result_t result;
    esp_err_t ret = inventory_service_import_csv(buf, len, &result);
    free(buf);

    if (ret != ESP_OK) {
        cJSON *r = err("storage_error", result.last_error[0] ? result.last_error : "Import failed");
        send_json(req, 500, r); cJSON_Delete(r); return ESP_OK;
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject  (data, "imported",         true);
    cJSON_AddNumberToObject(data, "imported_types",   result.imported_types);
    cJSON_AddNumberToObject(data, "imported_spools",  result.imported_spools);
    cJSON_AddNumberToObject(data, "skipped_rows",     result.skipped_rows);
    cJSON *r = ok(data); send_json(req, 200, r); cJSON_Delete(r);
    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────── */
/* Static file server                                          */
/* ─────────────────────────────────────────────────────────── */

static const char *mime_type(const char *path)
{
    if (strstr(path, ".html")) return "text/html; charset=utf-8";
    if (strstr(path, ".css"))  return "text/css";
    if (strstr(path, ".js"))   return "application/javascript";
    if (strstr(path, ".json")) return "application/json";
    if (strstr(path, ".png"))  return "image/png";
    if (strstr(path, ".ico"))  return "image/x-icon";
    if (strstr(path, ".svg"))  return "image/svg+xml";
    return "application/octet-stream";
}

static esp_err_t static_file_handler(httpd_req_t *req)
{
    const char *uri = req->uri;
    char path[128];

    /* Default to index.html */
    if (strcmp(uri, "/") == 0) {
        snprintf(path, sizeof(path), "index.html");
    } else {
        /* Strip leading slash */
        strlcpy(path, uri[0] == '/' ? uri + 1 : uri, sizeof(path));
    }

    char *buf = NULL; size_t len = 0;
    esp_err_t ret = storage_fs_read_file(path, &buf, &len);
    if (ret != ESP_OK) {
        /* Fallback: try serving index.html (SPA fallback) */
        ret = storage_fs_read_file("index.html", &buf, &len);
        if (ret != ESP_OK) {
            httpd_resp_send_404(req);
            return ESP_OK;
        }
        snprintf(path, sizeof(path), "index.html");
    }

    httpd_resp_set_type(req, mime_type(path));
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_send(req, buf, (ssize_t)len);
    free(buf);
    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────── */
/* 11. OTA                                                     */
/* ─────────────────────────────────────────────────────────── */

/* GET /api/v1/ota/status — returns running partition and firmware info */
static esp_err_t ota_status_handler(httpd_req_t *req)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *update  = esp_ota_get_next_update_partition(NULL);
    esp_app_desc_t app_desc = {0};
    esp_ota_get_partition_description(running, &app_desc);

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "running_partition",  running ? running->label : "unknown");
    cJSON_AddStringToObject(data, "update_partition",   update  ? update->label  : "none");
    cJSON_AddStringToObject(data, "firmware_version",   app_desc.version);
    cJSON_AddStringToObject(data, "project_name",       app_desc.project_name);
    cJSON_AddStringToObject(data, "build_date",         app_desc.date);
    cJSON_AddStringToObject(data, "build_time",         app_desc.time);
    cJSON *r = ok(data);
    esp_err_t ret = send_json(req, 200, r);
    cJSON_Delete(r);
    return ret;
}

/* POST /api/v1/ota/upload — streams raw firmware binary to inactive OTA partition.
   Content-Type: application/octet-stream, body = raw .bin image */
static esp_err_t ota_upload_handler(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0) {
        cJSON *r = err("INVALID", "Content-Length required"); esp_err_t e = send_json(req, 400, r); cJSON_Delete(r); return e;
    }

    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    if (!update) {
        cJSON *r = err("NO_PARTITION", "No OTA partition available"); esp_err_t e = send_json(req, 500, r); cJSON_Delete(r); return e;
    }

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err_code = esp_ota_begin(update, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err_code != ESP_OK) {
        cJSON *r = err("OTA_BEGIN", esp_err_to_name(err_code)); esp_err_t e = send_json(req, 500, r); cJSON_Delete(r); return e;
    }

    ESP_LOGI(TAG, "OTA begin: writing %d bytes to partition '%s'", total, update->label);

    char *chunk = malloc(4096);
    if (!chunk) { esp_ota_abort(ota_handle); cJSON *r = err("OOM", "Out of memory"); esp_err_t e = send_json(req, 500, r); cJSON_Delete(r); return e; }

    int received = 0;
    esp_err_t write_err = ESP_OK;
    while (received < total) {
        int want = (total - received) < 4096 ? (total - received) : 4096;
        int got  = httpd_req_recv(req, chunk, want);
        if (got <= 0) { write_err = ESP_FAIL; break; }
        write_err = esp_ota_write(ota_handle, chunk, got);
        if (write_err != ESP_OK) break;
        received += got;
    }
    free(chunk);

    if (write_err != ESP_OK) {
        esp_ota_abort(ota_handle);
        ESP_LOGE(TAG, "OTA write error: %s", esp_err_to_name(write_err));
        cJSON *r = err("OTA_WRITE", esp_err_to_name(write_err)); esp_err_t e = send_json(req, 500, r); cJSON_Delete(r); return e;
    }

    err_code = esp_ota_end(ota_handle);
    if (err_code != ESP_OK) {
        ESP_LOGE(TAG, "OTA end error: %s", esp_err_to_name(err_code));
        cJSON *r = err("OTA_END", esp_err_to_name(err_code)); esp_err_t e = send_json(req, 500, r); cJSON_Delete(r); return e;
    }

    err_code = esp_ota_set_boot_partition(update);
    if (err_code != ESP_OK) {
        ESP_LOGE(TAG, "OTA set boot error: %s", esp_err_to_name(err_code));
        cJSON *r = err("OTA_BOOT", esp_err_to_name(err_code)); esp_err_t e = send_json(req, 500, r); cJSON_Delete(r); return e;
    }

    ESP_LOGI(TAG, "OTA success (%d bytes), rebooting to '%s'", received, update->label);
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "message",   "OTA complete — rebooting");
    cJSON_AddStringToObject(data, "partition",  update->label);
    cJSON_AddNumberToObject(data, "bytes",      received);
    cJSON *resp = ok(data);
    send_json(req, 200, resp);
    cJSON_Delete(resp);

    vTaskDelay(pdMS_TO_TICKS(400));
    esp_restart();
    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────── */
/* Route table & server start                                  */
/* ─────────────────────────────────────────────────────────── */

#define ROUTE(u, m, h, ws) { .uri=(u), .method=(m), .handler=(h), .is_websocket=(ws), .user_ctx=NULL }

static const httpd_uri_t ROUTES[] = {
    /* Health */
    ROUTE("/api/v1/health",                    HTTP_GET,    health_handler,            false),
    ROUTE("/api/v1/system/info",               HTTP_GET,    system_info_handler,       false),
    /* Settings */
    ROUTE("/api/v1/settings/theme",            HTTP_PATCH,  settings_theme_handler,    false),
    ROUTE("/api/v1/settings/restart",          HTTP_POST,   settings_restart_handler,  false),
    ROUTE("/api/v1/settings/wifi/reset",       HTTP_POST,   wifi_reset_handler,        false),
    ROUTE("/api/v1/inventory/reset",            HTTP_POST,   inventory_reset_handler,    false),
    ROUTE("/api/v1/settings/export",           HTTP_GET,    settings_export_handler,   false),
    ROUTE("/api/v1/settings",                  HTTP_GET,    settings_get_handler,      false),
    ROUTE("/api/v1/settings",                  HTTP_PUT,    settings_put_handler,      false),
    /* Inventory — exact paths first */
    ROUTE("/api/v1/inventory/meta/options",    HTTP_GET,    inventory_options_handler, false),
    ROUTE("/api/v1/inventory",                 HTTP_GET,    inventory_list_handler,    false),
    ROUTE("/api/v1/inventory",                 HTTP_POST,   inventory_create_handler,  false),
    /* Inventory — wildcard (spools and single-item ops dispatched in handler) */
    ROUTE("/api/v1/inventory/*",               HTTP_GET,    inventory_get_handler,     false),
    ROUTE("/api/v1/inventory/*",               HTTP_PUT,    inventory_put_handler,     false),
    ROUTE("/api/v1/inventory/*",               HTTP_PATCH,  inventory_patch_handler,   false),
    ROUTE("/api/v1/inventory/*",               HTTP_POST,   inventory_post_handler,    false),
    ROUTE("/api/v1/inventory/*",               HTTP_DELETE, inventory_delete_handler,  false),
    /* AMS — exact before wildcard */
    ROUTE("/api/v1/ams/links",                 HTTP_GET,    ams_links_handler,         false),
    ROUTE("/api/v1/ams/link",                  HTTP_POST,   ams_link_create_handler,   false),
    ROUTE("/api/v1/ams/sync",                  HTTP_POST,   ams_sync_handler,          false),
    ROUTE("/api/v1/ams",                       HTTP_GET,    ams_get_handler,           false),
    ROUTE("/api/v1/ams/link/*",                HTTP_PUT,    ams_link_update_handler,   false),
    ROUTE("/api/v1/ams/link/*",                HTTP_DELETE, ams_link_delete_handler,   false),
    /* MQTT */
    ROUTE("/api/v1/mqtt/status",               HTTP_GET,    mqtt_status_handler,       false),
    ROUTE("/api/v1/mqtt/runtime",              HTTP_GET,    mqtt_runtime_handler,      false),
    ROUTE("/api/v1/mqtt/connect",              HTTP_POST,   mqtt_connect_handler,      false),
    ROUTE("/api/v1/mqtt/disconnect",           HTTP_POST,   mqtt_disconnect_handler,   false),
    ROUTE("/api/v1/mqtt/test",                 HTTP_POST,   mqtt_test_handler,         false),
    /* Help */
    ROUTE("/api/v1/help",                      HTTP_GET,    help_list_handler,         false),
    ROUTE("/api/v1/help/*",                    HTTP_GET,    help_section_handler,      false),
    /* Backup / Import */
    ROUTE("/api/v1/backup/export",             HTTP_GET,    backup_export_handler,     false),
    ROUTE("/api/v1/backup/import",             HTTP_POST,   backup_import_handler,     false),
    ROUTE("/api/v1/import/csv",                HTTP_POST,   csv_import_handler,        false),
    /* OTA */
    ROUTE("/api/v1/ota/status",                HTTP_GET,    ota_status_handler,        false),
    ROUTE("/api/v1/ota/upload",                HTTP_POST,   ota_upload_handler,        false),
    /* WebSocket */
    { .uri="/ws", .method=HTTP_GET, .handler=ws_handler, .is_websocket=true, .handle_ws_control_frames=true, .user_ctx=NULL },
    /* Static files — catch-all, must be last */
    ROUTE("/*",                                HTTP_GET,    static_file_handler,       false),
};

httpd_handle_t api_http_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers     = 64;
    config.max_open_sockets     = 7;
    config.uri_match_fn         = httpd_uri_match_wildcard;
    config.lru_purge_enable     = true;
    config.stack_size           = 12288;  /* increased for OTA streaming buffer */
    config.recv_wait_timeout    = 60;     /* seconds — allow large OTA uploads */
    config.send_wait_timeout    = 60;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return NULL;
    }

    int n = sizeof(ROUTES) / sizeof(ROUTES[0]);
    for (int i = 0; i < n; i++) {
        esp_err_t ret = httpd_register_uri_handler(server, &ROUTES[i]);
        if (ret != ESP_OK) ESP_LOGW(TAG, "Failed to register route %s", ROUTES[i].uri);
    }

    ws_init(server);
    ESP_LOGI(TAG, "HTTP server started (%d routes)", n);
    return server;
}

void api_http_stop(httpd_handle_t server)
{
    if (server) httpd_stop(server);
}
