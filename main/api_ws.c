#include "api_ws.h"
#include "mqtt_service.h"
#include "settings_service.h"
#include "json_codec.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"

#include <time.h>
#include <string.h>

#define TAG         "ws"
#define MAX_CLIENTS  8

static httpd_handle_t s_server   = NULL;
static int            s_fds[MAX_CLIENTS];
static int            s_fd_count = 0;
static SemaphoreHandle_t s_mutex;

/* ── Init ─────────────────────────────────────────────────── */

esp_err_t ws_init(httpd_handle_t server)
{
    s_server  = server;
    s_fd_count = 0;
    s_mutex   = xSemaphoreCreateMutex();
    memset(s_fds, -1, sizeof(s_fds));
    ESP_LOGI(TAG, "WebSocket broadcaster initialised");
    return ESP_OK;
}

/* ── Connection tracking ─────────────────────────────────── */

static void add_client(int fd)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_fds[i] < 0) { s_fds[i] = fd; if (i >= s_fd_count) s_fd_count = i + 1; break; }
    }
    xSemaphoreGive(s_mutex);
}

static void remove_client(int fd)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_fds[i] == fd) { s_fds[i] = -1; break; }
    }
    xSemaphoreGive(s_mutex);
}

/* ── Low-level send ──────────────────────────────────────── */

static void broadcast_json(cJSON *msg)
{
    if (!s_server || !msg) return;

    char *str = cJSON_PrintUnformatted(msg);
    if (!str) return;

    httpd_ws_frame_t frame = {
        .type    = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)str,
        .len     = strlen(str),
    };

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_fds[i] >= 0) {
            esp_err_t ret = httpd_ws_send_frame_async(s_server, s_fds[i], &frame);
            if (ret != ESP_OK) {
                ESP_LOGD(TAG, "Removed stale WS client fd=%d", s_fds[i]);
                s_fds[i] = -1;
            }
        }
    }
    xSemaphoreGive(s_mutex);
    cJSON_free(str);
}

static uint32_t now_ts(void)
{
    time_t t = time(NULL);
    return (t > 0) ? (uint32_t)t : 0;
}

static cJSON *make_msg(const char *type)
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type",      type);
    cJSON_AddNumberToObject(msg, "timestamp", (double)now_ts());
    return msg;
}

/* ── Handler (upgrade + keepalive) ──────────────────────── */

esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* New connection handshake */
        ESP_LOGI(TAG, "WS client connected, fd=%d", httpd_req_to_sockfd(req));
        add_client(httpd_req_to_sockfd(req));

        /* Send hello frame */
        cJSON *msg  = make_msg("system.hello");
        cJSON *data = cJSON_AddObjectToObject(msg, "data");
        const app_settings_t *s = settings_service_get();
        cJSON_AddStringToObject(data, "device_name",      s->device_name);
        cJSON_AddStringToObject(data, "firmware_version", "0.1.0");
        broadcast_json(msg);
        cJSON_Delete(msg);
        return ESP_OK;
    }

    /* Receive frame (keepalive / ping) */
    httpd_ws_frame_t frame = { .type = HTTPD_WS_TYPE_TEXT };
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        remove_client(httpd_req_to_sockfd(req));
        return ret;
    }

    if (frame.len) {
        uint8_t *buf = malloc(frame.len + 1);
        if (buf) {
            frame.payload = buf;
            httpd_ws_recv_frame(req, &frame, frame.len);
            buf[frame.len] = '\0';
            /* Respond to ping */
            if (strstr((char *)buf, "\"ping\"")) {
                cJSON *pong = make_msg("pong");
                cJSON_AddObjectToObject(pong, "data");
                broadcast_json(pong);
                cJSON_Delete(pong);
            }
            free(buf);
        }
    }
    return ESP_OK;
}

/* ── Broadcast helpers ───────────────────────────────────── */

void ws_broadcast_inventory_created(const char *type_id)
{
    cJSON *msg  = make_msg("inventory.created");
    cJSON *data = cJSON_AddObjectToObject(msg, "data");
    cJSON_AddStringToObject(data, "id", type_id);
    broadcast_json(msg);
    cJSON_Delete(msg);
}

void ws_broadcast_inventory_updated(const char *type_id, int remaining_grams, uint32_t ts)
{
    cJSON *msg  = make_msg("inventory.updated");
    cJSON *data = cJSON_AddObjectToObject(msg, "data");
    cJSON_AddStringToObject(data, "id",         type_id);
    if (remaining_grams >= 0) cJSON_AddNumberToObject(data, "remaining_grams", remaining_grams);
    cJSON_AddNumberToObject(data, "updated_at", (double)ts);
    broadcast_json(msg);
    cJSON_Delete(msg);
}

void ws_broadcast_inventory_deleted(const char *type_id)
{
    cJSON *msg  = make_msg("inventory.deleted");
    cJSON *data = cJSON_AddObjectToObject(msg, "data");
    cJSON_AddStringToObject(data, "id", type_id);
    broadcast_json(msg);
    cJSON_Delete(msg);
}

void ws_broadcast_ams_updated(const char *ams_id, int slot)
{
    cJSON *msg  = make_msg("ams.updated");
    cJSON *data = cJSON_AddObjectToObject(msg, "data");
    cJSON_AddStringToObject(data, "ams_id", ams_id);
    cJSON_AddNumberToObject(data, "slot",   slot);
    broadcast_json(msg);
    cJSON_Delete(msg);
}

void ws_broadcast_mqtt_status_updated(void)
{
    cJSON *msg  = make_msg("mqtt.status.updated");
    cJSON *data = cJSON_AddObjectToObject(msg, "data");
    cJSON_AddBoolToObject(data, "connected",     mqtt_service_is_connected());
    broadcast_json(msg);
    cJSON_Delete(msg);
}

void ws_broadcast_runtime_updated(void)
{
    mqtt_runtime_t rt;
    mqtt_service_get_runtime(&rt);
    cJSON *msg  = make_msg("mqtt.runtime.updated");
    cJSON_AddItemToObject(msg, "data", json_encode_runtime(&rt));
    broadcast_json(msg);
    cJSON_Delete(msg);
}

void ws_broadcast_settings_updated(void)
{
    cJSON *msg  = make_msg("settings.updated");
    const app_settings_t *s = settings_service_get();
    cJSON *data = cJSON_AddObjectToObject(msg, "data");
    cJSON_AddStringToObject(data, "theme", s->theme);
    broadcast_json(msg);
    cJSON_Delete(msg);
}

void ws_broadcast_storage_saved(void)
{
    cJSON *msg  = make_msg("storage.save.completed");
    cJSON *data = cJSON_AddObjectToObject(msg, "data");
    cJSON_AddBoolToObject(data, "dirty", false);
    broadcast_json(msg);
    cJSON_Delete(msg);
}
