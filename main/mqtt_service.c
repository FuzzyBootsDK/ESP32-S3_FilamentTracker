#include "mqtt_service.h"
#include "ams_service.h"
#include "inventory_service.h"
#include "api_ws.h"
#include "settings_service.h"

#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

#define TAG "mqtt"
#define MQTT_FRAME_MAX 8192

/* ── State ────────────────────────────────────────────────── */

static esp_mqtt_client_handle_t s_client = NULL;
static mqtt_runtime_t           s_runtime;
static SemaphoreHandle_t        s_mutex;
static mqtt_settings_t          s_cfg;
static char                     s_frame_buf[MQTT_FRAME_MAX];
static int                      s_frame_expected = 0;
static char                     s_frame_topic[128];
static int                      s_frame_topic_len = 0;

#define LOCK()   xSemaphoreTake(s_mutex, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mutex)

static uint32_t now_ts(void)
{
    time_t t = time(NULL);
    return (t > 0) ? (uint32_t)t : 0;
}

static void append_bounded(char *dst, size_t dst_size, const char *src)
{
    if (!dst || !src || dst_size == 0) return;
    size_t used = strlen(dst);
    if (used >= dst_size - 1) return;
    strncat(dst, src, dst_size - used - 1);
}

static bool starts_with(const char *s, const char *prefix)
{
    if (!s || !prefix) return false;
    size_t n = strlen(prefix);
    return strncmp(s, prefix, n) == 0;
}

static bool host_includes_port(const char *host)
{
    if (!host || !host[0]) return false;
    if (strchr(host, '/')) return false;

    const char *colon = strrchr(host, ':');
    if (!colon || !colon[1]) return false;

    const char *p = colon + 1;
    while (*p) {
        if (*p < '0' || *p > '9') return false;
        p++;
    }
    return true;
}

static void build_broker_uri(char *out, size_t out_size, const mqtt_settings_t *cfg)
{
    out[0] = '\0';
    if (!cfg || !cfg->broker_host[0]) return;

    if (starts_with(cfg->broker_host, "mqtt://") ||
        starts_with(cfg->broker_host, "mqtts://") ||
        starts_with(cfg->broker_host, "ws://") ||
        starts_with(cfg->broker_host, "wss://")) {
        snprintf(out, out_size, "%s", cfg->broker_host);
        return;
    }

    const char *scheme = (cfg->broker_port == 8883 || cfg->broker_port == 8884) ? "mqtts" : "mqtt";
    if (host_includes_port(cfg->broker_host)) {
        snprintf(out, out_size, "%s://%s", scheme, cfg->broker_host);
    } else {
        snprintf(out, out_size, "%s://%s:%d", scheme, cfg->broker_host, cfg->broker_port);
    }
}

static int map_ams_index(int raw_id, int fallback_idx)
{
    if (raw_id >= 128) return (raw_id - 128 + 1);
    if (raw_id >= 0 && raw_id <= 26) return raw_id;
    return fallback_idx;
}

static bool parse_tray_color_hex(const cJSON *tray_color, char *out, size_t out_len)
{
    if (!cJSON_IsString(tray_color) || !tray_color->valuestring || out_len < 8) return false;

    const char *src = tray_color->valuestring;
    size_t n = strlen(src);
    if (n < 6) return false;

    out[0] = '#';
    for (int i = 0; i < 6; i++) {
        char c = src[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
        if (c >= 'a' && c <= 'f') c = (char)(c - 'a' + 'A');
        out[1 + i] = c;
    }
    out[7] = '\0';
    return true;
}

/* ── BambuLab message parsing ────────────────────────────── */

static void parse_bambu_print(const cJSON *print)
{
    mqtt_ams_slot_runtime_t new_slots[MQTT_AMS_SLOT_MAX];
    int new_slot_count = 0;
    bool ams_payload_seen = false;

    LOCK();
    /* Any valid print payload means the printer is online. */
    s_runtime.printer_online = true;

    bool state_updated = false;

    /* State */
    cJSON *state = cJSON_GetObjectItemCaseSensitive(print, "gcode_state");
    if (!state) state = cJSON_GetObjectItemCaseSensitive(print, "state");
    if (cJSON_IsString(state)) {
        snprintf(s_runtime.printer_state, sizeof(s_runtime.printer_state), "%s", state->valuestring);
        state_updated = true;
    }

    /* Progress / ETA */
    cJSON *pct = cJSON_GetObjectItemCaseSensitive(print, "mc_percent");
    if (cJSON_IsNumber(pct)) s_runtime.progress_percent = (int)pct->valuedouble;
    else if (cJSON_IsString(pct) && pct->valuestring) s_runtime.progress_percent = atoi(pct->valuestring);

    cJSON *rem = cJSON_GetObjectItemCaseSensitive(print, "mc_remaining_time");
    if (cJSON_IsNumber(rem)) s_runtime.remaining_minutes = (int)rem->valuedouble;
    else if (cJSON_IsString(rem) && rem->valuestring) s_runtime.remaining_minutes = atoi(rem->valuestring);

    /* Temperatures */
    cJSON *bed = cJSON_GetObjectItemCaseSensitive(print, "bed_temper");
    if (cJSON_IsNumber(bed)) s_runtime.bed_temp_c = (float)bed->valuedouble;
    else if (cJSON_IsString(bed) && bed->valuestring) s_runtime.bed_temp_c = (float)atof(bed->valuestring);

    cJSON *noz = cJSON_GetObjectItemCaseSensitive(print, "nozzle_temper");
    if (cJSON_IsNumber(noz)) s_runtime.nozzle_temp_c = (float)noz->valuedouble;
    else if (cJSON_IsString(noz) && noz->valuestring) s_runtime.nozzle_temp_c = (float)atof(noz->valuestring);

    /* Job name */
    cJSON *job = cJSON_GetObjectItemCaseSensitive(print, "subtask_name");
    if (!job) job = cJSON_GetObjectItemCaseSensitive(print, "gcode_filename");
    if (!job) job = cJSON_GetObjectItemCaseSensitive(print, "project_name");
    if (!job) job = cJSON_GetObjectItemCaseSensitive(print, "task_name");
    if (cJSON_IsString(job))
        snprintf(s_runtime.current_job_name, sizeof(s_runtime.current_job_name), "%s", job->valuestring);

    /* Reconstruct runtime state when the start packet was missed.
       Keep this conservative and only infer printing when we have clear hints. */
    if (!state_updated) {
        if (s_runtime.progress_percent > 0 && s_runtime.progress_percent < 100) {
            snprintf(s_runtime.printer_state, sizeof(s_runtime.printer_state), "printing");
        } else if (s_runtime.remaining_minutes > 0 &&
                   (s_runtime.nozzle_temp_c > 45.0f || s_runtime.bed_temp_c > 35.0f)) {
            snprintf(s_runtime.printer_state, sizeof(s_runtime.printer_state), "printing");
        } else if (s_runtime.progress_percent >= 100) {
            snprintf(s_runtime.printer_state, sizeof(s_runtime.printer_state), "finish");
        }
    }

    /* AMS data */
    cJSON *ams_arr = cJSON_GetObjectItemCaseSensitive(print, "ams");
    if (cJSON_IsObject(ams_arr)) {
        cJSON *ams_list = cJSON_GetObjectItemCaseSensitive(ams_arr, "ams");
        if (cJSON_IsArray(ams_list)) {
            ams_payload_seen = true;
            int ams_unit_idx = 0;
            /* Find active slot from tray_now */
            cJSON *tray_now = cJSON_GetObjectItemCaseSensitive(ams_arr, "tray_now");
            if (cJSON_IsString(tray_now)) {
                int tray_id = atoi(tray_now->valuestring);
                int ams_idx = tray_id / 4;
                int slot    = (tray_id % 4) + 1;
                snprintf(s_runtime.active_ams_id, sizeof(s_runtime.active_ams_id), "AMS_%c", 'A' + ams_idx);
                s_runtime.active_slot = slot;
            }

            /* Update spool weights from AMS report */
            cJSON *ams_unit;
            cJSON_ArrayForEach(ams_unit, ams_list) {
                cJSON *ams_id_item = cJSON_GetObjectItemCaseSensitive(ams_unit, "id");
                int raw_id = cJSON_IsString(ams_id_item) ? atoi(ams_id_item->valuestring) : ams_unit_idx;
                int ams_idx = map_ams_index(raw_id, ams_unit_idx);
                char ams_id_str[AMS_ID_LEN];
                snprintf(ams_id_str, sizeof(ams_id_str), "AMS_%c", 'A' + ams_idx);

                cJSON *trays = cJSON_GetObjectItemCaseSensitive(ams_unit, "tray");
                if (!cJSON_IsArray(trays)) continue;

                cJSON *tray;
                int slot = 1;
                cJSON_ArrayForEach(tray, trays) {
                    cJSON *remain_pct = cJSON_GetObjectItemCaseSensitive(tray, "remain");
                    cJSON *tag_uid    = cJSON_GetObjectItemCaseSensitive(tray, "tag_uid");

                    if (new_slot_count < MQTT_AMS_SLOT_MAX) {
                        mqtt_ams_slot_runtime_t *rs = &new_slots[new_slot_count++];
                        memset(rs, 0, sizeof(*rs));
                        snprintf(rs->ams_id, sizeof(rs->ams_id), "%s", ams_id_str);
                        rs->slot = slot;
                        rs->known = false;
                        snprintf(rs->brand, sizeof(rs->brand), "Unknown brand");
                        snprintf(rs->color_hex, sizeof(rs->color_hex), "#FFFFFF");

                        cJSON *tray_sub = cJSON_GetObjectItemCaseSensitive(tray, "tray_sub_brands");
                        cJSON *tray_type = cJSON_GetObjectItemCaseSensitive(tray, "tray_type");
                        bool has_brand = false;
                        if (cJSON_IsString(tray_sub) && tray_sub->valuestring && tray_sub->valuestring[0]) {
                            snprintf(rs->brand, sizeof(rs->brand), "%s", tray_sub->valuestring);
                            has_brand = true;
                        } else if (cJSON_IsString(tray_type) && tray_type->valuestring && tray_type->valuestring[0]) {
                            snprintf(rs->brand, sizeof(rs->brand), "%s", tray_type->valuestring);
                            has_brand = true;
                        }

                        cJSON *tray_color = cJSON_GetObjectItemCaseSensitive(tray, "tray_color");
                        bool has_color = parse_tray_color_hex(tray_color, rs->color_hex, sizeof(rs->color_hex));
                        rs->known = has_brand && has_color;
                    }

                    if (cJSON_IsString(tag_uid) && tag_uid->valuestring[0]) {
                        char type_id[FTYPE_ID_LEN] = {0};
                        char spool_id[SPOOL_ID_LEN] = {0};
                        if (inventory_service_tag_uid_exists(tag_uid->valuestring, type_id, spool_id)) {
                            if (cJSON_IsNumber(remain_pct)) {
                                /* BambuLab reports remaining as 0-100%.
                                   We need total_grams to compute actual grams.
                                   First get the type to find total_grams. */
                                filament_type_t ft;
                                if (inventory_service_get_type(type_id, &ft) == ESP_OK) {
                                    int grams = (int)(remain_pct->valuedouble / 100.0 * ft.total_grams);
                                    uint32_t ts = 0;
                                    inventory_service_update_spool_grams(type_id, spool_id, grams, &ts);
                                    ams_service_update_weight(spool_id, grams);
                                }
                            }
                        }
                    }
                    slot++;
                }
                ams_unit_idx++;
            }
        }
    }

    if (ams_payload_seen && new_slot_count > 0) {
        memset(s_runtime.ams_slots, 0, sizeof(s_runtime.ams_slots));
        memcpy(s_runtime.ams_slots, new_slots, sizeof(new_slots[0]) * new_slot_count);
        s_runtime.ams_slot_count = new_slot_count;
    }

    s_runtime.last_message_at = now_ts();
    s_runtime.updated_at      = now_ts();
    UNLOCK();
}

static void handle_message(const char *topic, int topic_len,
                            const char *data, int data_len)
{
    ws_broadcast_mqtt_message_received(topic, topic_len, data, data_len);

    cJSON *root = cJSON_ParseWithLength(data, data_len);
    if (!root) return;

    cJSON *print = cJSON_GetObjectItemCaseSensitive(root, "print");
    if (cJSON_IsObject(print)) {
        parse_bambu_print(print);
        ws_broadcast_runtime_updated();
    } else if (cJSON_IsObject(root)) {
        cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "gcode_state");
        cJSON *pct   = cJSON_GetObjectItemCaseSensitive(root, "mc_percent");
        if (cJSON_IsString(state) || cJSON_IsNumber(pct)) {
            parse_bambu_print(root);
            ws_broadcast_runtime_updated();
        }
    }

    cJSON_Delete(root);
}

/* ── MQTT event handler ───────────────────────────────────── */

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            LOCK();
            s_runtime.mqtt_connected   = true;
            s_runtime.broker_reachable = true;
            s_runtime.last_error[0]    = '\0';
            UNLOCK();
            /* Subscribe to BambuLab report topic */
            {
                const app_settings_t *cfg = settings_service_get();
                char root[TOPIC_ROOT_LEN] = {0};
                char topic[128];

                if (cfg->mqtt.topic_root[0]) {
                    strncpy(root, cfg->mqtt.topic_root, sizeof(root) - 1);
                    root[sizeof(root) - 1] = '\0';
                } else if (cfg->printer.serial[0]) {
                    strncpy(root, cfg->printer.serial, sizeof(root) - 1);
                    root[sizeof(root) - 1] = '\0';
                }

                topic[0] = '\0';
                if (!root[0]) {
                    append_bounded(topic, sizeof(topic), "device/");
                    append_bounded(topic, sizeof(topic), cfg->printer.serial);
                    append_bounded(topic, sizeof(topic), "/report");
                } else if (strchr(root, '/') == NULL) {
                    /* Serial-only value: normalize to canonical Bambu root. */
                    append_bounded(topic, sizeof(topic), "device/");
                    append_bounded(topic, sizeof(topic), root);
                    append_bounded(topic, sizeof(topic), "/report");
                } else {
                    append_bounded(topic, sizeof(topic), root);
                    append_bounded(topic, sizeof(topic), "/report");
                }

                ESP_LOGI(TAG, "Subscribing to topic: %s", topic);
                esp_mqtt_client_subscribe(s_client, topic, 0);

                if (strcmp(topic, "device/+/report") != 0) {
                    ESP_LOGI(TAG, "Subscribing fallback topic: device/+/report");
                    esp_mqtt_client_subscribe(s_client, "device/+/report", 0);
                }
            }
            ws_broadcast_mqtt_status_updated();
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            LOCK();
            s_runtime.mqtt_connected = false;
            s_runtime.printer_online = false;
            UNLOCK();
            ws_broadcast_mqtt_status_updated();
            break;

        case MQTT_EVENT_DATA:
            if (!event || event->total_data_len <= 0 || event->data_len <= 0) break;

            if (event->total_data_len >= MQTT_FRAME_MAX) {
                ESP_LOGW(TAG, "MQTT payload too large: %d", event->total_data_len);
                break;
            }

            if (event->current_data_offset == 0) {
                s_frame_expected = event->total_data_len;
                s_frame_topic_len = 0;
                if (event->topic && event->topic_len > 0) {
                    int n = event->topic_len;
                    if (n > (int)sizeof(s_frame_topic) - 1) n = (int)sizeof(s_frame_topic) - 1;
                    memcpy(s_frame_topic, event->topic, n);
                    s_frame_topic[n] = '\0';
                    s_frame_topic_len = n;
                }
            }

            if (s_frame_expected != event->total_data_len) {
                s_frame_expected = 0;
                s_frame_topic_len = 0;
                break;
            }

            if (event->current_data_offset + event->data_len > MQTT_FRAME_MAX - 1) {
                s_frame_expected = 0;
                s_frame_topic_len = 0;
                break;
            }

            memcpy(&s_frame_buf[event->current_data_offset], event->data, event->data_len);

            if (event->current_data_offset + event->data_len == event->total_data_len) {
                s_frame_buf[event->total_data_len] = '\0';
                handle_message(s_frame_topic, s_frame_topic_len,
                               s_frame_buf, event->total_data_len);
                s_frame_expected = 0;
                s_frame_topic_len = 0;
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            LOCK();
            s_runtime.mqtt_connected = false;
            if (event && event->error_handle) {
                esp_mqtt_error_codes_t *err = event->error_handle;
                if (err->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                    snprintf(s_runtime.last_error, sizeof(s_runtime.last_error),
                             "MQTT_REFUSED_%d", err->connect_return_code);
                } else if (err->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                    snprintf(s_runtime.last_error, sizeof(s_runtime.last_error),
                             "MQTT_TCP esp=%s tls=0x%x sock=%d",
                             esp_err_to_name(err->esp_tls_last_esp_err),
                             err->esp_tls_stack_err,
                             err->esp_transport_sock_errno);
                } else {
                    snprintf(s_runtime.last_error, sizeof(s_runtime.last_error), "MQTT_ERROR_%d", err->error_type);
                }
            } else {
                snprintf(s_runtime.last_error, sizeof(s_runtime.last_error), "MQTT_ERROR");
            }
            UNLOCK();
            ws_broadcast_mqtt_status_updated();
            break;

        default:
            break;
    }
}

/* ── Init & connect ──────────────────────────────────────── */

esp_err_t mqtt_service_init(const mqtt_settings_t *cfg)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    memset(&s_runtime, 0, sizeof(s_runtime));
    snprintf(s_runtime.printer_state, sizeof(s_runtime.printer_state), "idle");
    memcpy(&s_cfg, cfg, sizeof(s_cfg));

    if (cfg->enabled) {
        return mqtt_service_connect();
    }
    return ESP_OK;
}

esp_err_t mqtt_service_connect(void)
{
    LOCK();
    s_runtime.last_error[0] = '\0';
    s_runtime.mqtt_connected = false;
    s_runtime.printer_online = false;
    UNLOCK();

    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }

    char uri[160];
    build_broker_uri(uri, sizeof(uri), &s_cfg);
    if (!uri[0]) {
        LOCK();
        s_runtime.mqtt_connected = false;
        snprintf(s_runtime.last_error, sizeof(s_runtime.last_error), "MQTT invalid broker_host");
        UNLOCK();
        return ESP_ERR_INVALID_ARG;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri      = uri,
        .credentials.username    = s_cfg.username[0]  ? s_cfg.username  : NULL,
        .credentials.authentication.password = s_cfg.password[0] ? s_cfg.password : NULL,
        .credentials.client_id   = s_cfg.client_id[0] ? s_cfg.client_id : NULL,
        .session.keepalive        = 60,
        .network.reconnect_timeout_ms = 5000,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client) {
        LOCK();
        s_runtime.mqtt_connected = false;
        snprintf(s_runtime.last_error, sizeof(s_runtime.last_error), "MQTT init failed");
        UNLOCK();
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_err_t ret = esp_mqtt_client_start(s_client);
    if (ret != ESP_OK) {
        LOCK();
        s_runtime.mqtt_connected = false;
        snprintf(s_runtime.last_error, sizeof(s_runtime.last_error), "MQTT start failed: %s", esp_err_to_name(ret));
        UNLOCK();
    }
    return ret;
}

esp_err_t mqtt_service_disconnect(void)
{
    if (!s_client) return ESP_OK;
    esp_err_t ret = esp_mqtt_client_stop(s_client);
    LOCK();
    s_runtime.mqtt_connected = false;
    s_runtime.printer_online = false;
    s_runtime.last_error[0] = '\0';
    UNLOCK();
    return ret;
}

esp_err_t mqtt_service_apply_settings(const mqtt_settings_t *cfg)
{
    memcpy(&s_cfg, cfg, sizeof(s_cfg));
    if (cfg->enabled) return mqtt_service_connect();
    return mqtt_service_disconnect();
}

/* ── Test ─────────────────────────────────────────────────── */

esp_err_t mqtt_service_test(const mqtt_settings_t *cfg, bool *reachable, bool *authenticated)
{
    /* Heuristic-only test. This does not perform a real broker handshake. */
    *reachable     = cfg->broker_host[0] != '\0';
    *authenticated = false;
    return ESP_OK;
}

/* ── Getters ─────────────────────────────────────────────── */

void mqtt_service_get_runtime(mqtt_runtime_t *out)
{
    LOCK();
    memcpy(out, &s_runtime, sizeof(*out));
    UNLOCK();
}

bool mqtt_service_is_connected(void)
{
    LOCK();
    bool c = s_runtime.mqtt_connected;
    UNLOCK();
    return c;
}

cJSON *mqtt_service_status_json(void)
{
    LOCK();
    const app_settings_t *s = settings_service_get();
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject  (data, "enabled",         s->mqtt.enabled);
    cJSON_AddBoolToObject  (data, "connected",        s_runtime.mqtt_connected);
    cJSON_AddStringToObject(data, "broker_host",      s->mqtt.broker_host);
    cJSON_AddNumberToObject(data, "broker_port",      s->mqtt.broker_port);
    cJSON_AddStringToObject(data, "topic_root",       s->mqtt.topic_root);
    cJSON_AddBoolToObject  (data, "printer_online",   s_runtime.printer_online);
    cJSON_AddNumberToObject(data, "last_message_at",  (double)s_runtime.last_message_at);
    if (s_runtime.last_error[0])
        cJSON_AddStringToObject(data, "last_error", s_runtime.last_error);
    else
        cJSON_AddNullToObject(data, "last_error");
    UNLOCK();
    return data;
}
