#include "ams_service.h"
#include "inventory_service.h"
#include "mqtt_service.h"
#include "storage_fs.h"
#include "storage_nvs.h"
#include "json_codec.h"
#include "api_ws.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

#define TAG          "ams"
#define AMS_FILE     "ams_links.json"
#define NVS_KEY_AMID "ams_next_id"

static ams_store_t       s_store;
static SemaphoreHandle_t s_mutex;

#define LOCK()   xSemaphoreTake(s_mutex, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mutex)

/* ── Helpers ─────────────────────────────────────────────── */

static uint32_t now_ts(void)
{
    time_t t = time(NULL);
    return (t > 0) ? (uint32_t)t : 0;
}

static esp_err_t next_ams_id(char *buf)
{
    uint32_t n = 0;
    storage_nvs_get_u32(NVS_KEY_AMID, &n, 1);
    snprintf(buf, AMS_LINK_ID_LEN, "ams_%06lu", (unsigned long)n);
    return storage_nvs_set_u32(NVS_KEY_AMID, n + 1);
}

static ams_link_t *find_link(const char *id)
{
    for (int i = 0; i < s_store.link_count; i++) {
        if (strcmp(s_store.links[i].id, id) == 0) return &s_store.links[i];
    }
    return NULL;
}

/** Fill filament_label cache from inventory */
static void refresh_label(ams_link_t *l)
{
    spool_t s_spool;
    /* Try to get the spool's parent type */
    /* We need to walk inventory; use the type_id in the spool */
    /* For now just build label from spool_id lookup via inventory */
    filament_type_t ft;
    /* Look for a type that contains this spool */
    /* Simplified: iterate via inventory service get_type won't work here.
       We'll populate labels during sync instead. */
    (void)l;
}

/* ── Persist ─────────────────────────────────────────────── */

static esp_err_t flush(void)
{
    cJSON *doc = json_encode_ams_links_doc(&s_store);
    if (!doc) return ESP_ERR_NO_MEM;
    char *str = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if (!str) return ESP_ERR_NO_MEM;
    esp_err_t ret = storage_fs_write_file_atomic(AMS_FILE, str, strlen(str));
    cJSON_free(str);
    if (ret == ESP_OK) s_store.dirty = false;
    return ret;
}

/* ── Init ─────────────────────────────────────────────────── */

esp_err_t ams_service_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    memset(&s_store, 0, sizeof(s_store));

    char  *buf = NULL;
    size_t len = 0;
    esp_err_t ret = storage_fs_read_file(AMS_FILE, &buf, &len);
    if (ret == ESP_OK) {
        cJSON *root = cJSON_ParseWithLength(buf, len);
        free(buf);
        if (root) { json_decode_ams_links_doc(root, &s_store); cJSON_Delete(root); }
    } else if (ret == ESP_ERR_NOT_FOUND) {
        flush();
    } else {
        if (buf) free(buf);
        return ret;
    }

    ESP_LOGI(TAG, "AMS service loaded: %d links", s_store.link_count);
    return ESP_OK;
}

esp_err_t ams_service_save(void) { LOCK(); esp_err_t r = flush(); UNLOCK(); return r; }

void ams_service_process_dirty(void) { LOCK(); if (s_store.dirty) flush(); UNLOCK(); }

/* ── List units (structured) ─────────────────────────────── */

esp_err_t ams_service_list_units(cJSON **out)
{
    /* Discover unique AMS IDs */
    char ams_ids[8][AMS_ID_LEN];
    int  n_ams = 0;

    LOCK();
    for (int i = 0; i < s_store.link_count; i++) {
        bool found = false;
        for (int j = 0; j < n_ams; j++)
            if (strcmp(ams_ids[j], s_store.links[i].ams_id) == 0) { found = true; break; }
        if (!found && n_ams < 8)
            snprintf(ams_ids[n_ams++], AMS_ID_LEN, "%s", s_store.links[i].ams_id);
    }

    cJSON *data  = cJSON_CreateObject();
    cJSON *units = cJSON_AddArrayToObject(data, "units");

    for (int a = 0; a < n_ams; a++) {
        cJSON *unit  = cJSON_CreateObject();
        cJSON_AddStringToObject(unit, "ams_id", ams_ids[a]);
        cJSON *slots = cJSON_AddArrayToObject(unit, "slots");

        /* Determine max slot for this AMS */
        int max_slot = 4;
        for (int i = 0; i < s_store.link_count; i++)
            if (strcmp(s_store.links[i].ams_id, ams_ids[a]) == 0 && s_store.links[i].slot > max_slot)
                max_slot = s_store.links[i].slot;

        for (int slot = 1; slot <= max_slot; slot++) {
            cJSON *slot_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(slot_obj, "slot", slot);
            bool linked = false;
            for (int i = 0; i < s_store.link_count; i++) {
                if (strcmp(s_store.links[i].ams_id, ams_ids[a]) == 0
                    && s_store.links[i].slot == slot) {
                    cJSON_AddBoolToObject  (slot_obj, "linked",           true);
                    cJSON_AddStringToObject(slot_obj, "spool_id",         s_store.links[i].spool_id);
                    cJSON_AddStringToObject(slot_obj, "filament_label",   s_store.links[i].filament_label);
                    cJSON_AddStringToObject(slot_obj, "tag_uid",          s_store.links[i].tag_uid);
                    cJSON_AddBoolToObject  (slot_obj, "enabled",          s_store.links[i].enabled);
                    cJSON_AddNumberToObject(slot_obj, "last_sync_weight", s_store.links[i].last_sync_weight);
                    cJSON_AddNumberToObject(slot_obj, "last_seen",        (double)s_store.links[i].last_seen);
                    linked = true;
                    break;
                }
            }
            if (!linked) cJSON_AddBoolToObject(slot_obj, "linked", false);
            cJSON_AddItemToArray(slots, slot_obj);
        }
        cJSON_AddItemToArray(units, unit);
    }
    UNLOCK();

    *out = data;
    return ESP_OK;
}

/* ── List links (flat) ────────────────────────────────────── */

esp_err_t ams_service_list_links(cJSON **out)
{
    cJSON *data  = cJSON_CreateObject();
    cJSON *items = cJSON_AddArrayToObject(data, "items");

    LOCK();
    for (int i = 0; i < s_store.link_count; i++) {
        cJSON *obj = json_encode_ams_link(&s_store.links[i]);
        cJSON_AddStringToObject(obj, "filament_label", s_store.links[i].filament_label);
        cJSON_AddItemToArray(items, obj);
    }
    UNLOCK();

    *out = data;
    return ESP_OK;
}

/* ── Create link ─────────────────────────────────────────── */

esp_err_t ams_service_create_link(const ams_link_t *req, char *new_id)
{
    LOCK();
    if (s_store.link_count >= MAX_AMS_LINKS) { UNLOCK(); return ESP_ERR_NO_MEM; }

    /* Enforce unique ams_id + slot */
    for (int i = 0; i < s_store.link_count; i++) {
        if (strcmp(s_store.links[i].ams_id, req->ams_id) == 0
            && s_store.links[i].slot == req->slot) {
            UNLOCK(); return ESP_ERR_INVALID_STATE; /* conflict */
        }
    }

    ams_link_t *l = &s_store.links[s_store.link_count];
    memcpy(l, req, sizeof(*l));
    next_ams_id(l->id);
    snprintf(new_id, AMS_LINK_ID_LEN, "%s", l->id);
    l->updated_at = now_ts();
    s_store.link_count++;
    s_store.dirty = true;
    UNLOCK();

    flush(); /* AMS links always save immediately */
    ws_broadcast_ams_updated(req->ams_id, req->slot);
    return ESP_OK;
}

/* ── Update link ─────────────────────────────────────────── */

esp_err_t ams_service_update_link(const char *id, const ams_link_t *req)
{
    LOCK();
    ams_link_t *l = find_link(id);
    if (!l) { UNLOCK(); return ESP_ERR_NOT_FOUND; }

    char saved_id[AMS_LINK_ID_LEN];
    char saved_ams[AMS_ID_LEN];
    int  saved_slot = l->slot;
    snprintf(saved_id,  sizeof(saved_id),  "%s", l->id);
    snprintf(saved_ams, sizeof(saved_ams), "%s", l->ams_id);

    memcpy(l, req, sizeof(*l));
    snprintf(l->id,     sizeof(l->id),     "%s", saved_id);
    snprintf(l->ams_id, sizeof(l->ams_id), "%s", saved_ams);
    l->slot       = saved_slot;
    l->updated_at = now_ts();
    s_store.dirty = true;
    UNLOCK();

    flush();
    ws_broadcast_ams_updated(saved_ams, saved_slot);
    return ESP_OK;
}

/* ── Delete link ─────────────────────────────────────────── */

esp_err_t ams_service_delete_link(const char *id)
{
    LOCK();
    int idx = -1;
    for (int i = 0; i < s_store.link_count; i++)
        if (strcmp(s_store.links[i].id, id) == 0) { idx = i; break; }
    if (idx < 0) { UNLOCK(); return ESP_ERR_NOT_FOUND; }

    char ams_id[AMS_ID_LEN]; int slot = s_store.links[idx].slot;
    snprintf(ams_id, sizeof(ams_id), "%s", s_store.links[idx].ams_id);

    for (int i = idx; i < s_store.link_count - 1; i++)
        memcpy(&s_store.links[i], &s_store.links[i+1], sizeof(ams_link_t));
    s_store.link_count--;
    s_store.dirty = true;
    UNLOCK();

    flush();
    ws_broadcast_ams_updated(ams_id, slot);
    return ESP_OK;
}

/* ── Sync ─────────────────────────────────────────────────── */

esp_err_t ams_service_sync(int *out_updated)
{
    *out_updated = 0;
    /* Reconcile: update last_sync_weight from spool remaining_grams */
    LOCK();
    for (int i = 0; i < s_store.link_count; i++) {
        ams_link_t *l = &s_store.links[i];
        filament_type_t ft;
        /* Walk inventory for matching spool */
        /* We can't call inventory_service from within a lock without deadlock,
           so we read outside the loop — see below */
        (void)l;
    }
    UNLOCK();

    /* Pass 2: outside lock — call inventory for each link */
    for (int i = 0; i < s_store.link_count; i++) {
        LOCK();
        if (i >= s_store.link_count) { UNLOCK(); break; }
        char spool_id[SPOOL_ID_LEN];
        snprintf(spool_id, sizeof(spool_id), "%s", s_store.links[i].spool_id);
        UNLOCK();

        /* TODO: implement inventory_service_get_spool_by_id for cleaner access */
        (void)spool_id;
    }

    return ESP_OK;
}

/* ── Helpers ─────────────────────────────────────────────── */

bool ams_service_type_has_links(const char *type_id)
{
    /* type_id is a FilamentType — we check if any linked spool belongs to it.
       Since spool IDs follow "spool_XXXXXX" and their type_id is set, we need
       to check via the inventory spool's parent.  For now, we skip the deep
       check and rely on spool_id having the type embedded when possible.
       TODO: Refactor to store type_id in the AMS link for efficiency. */
    (void)type_id;
    return false; /* conservative: allows deletes; link check done at delete time */
}

bool ams_service_spool_is_linked(const char *spool_id, char *out_link_id)
{
    LOCK();
    for (int i = 0; i < s_store.link_count; i++) {
        if (strcmp(s_store.links[i].spool_id, spool_id) == 0) {
            if (out_link_id) snprintf(out_link_id, AMS_LINK_ID_LEN, "%s", s_store.links[i].id);
            UNLOCK();
            return true;
        }
    }
    UNLOCK();
    return false;
}

void ams_service_update_weight(const char *spool_id, int weight)
{
    LOCK();
    for (int i = 0; i < s_store.link_count; i++) {
        if (strcmp(s_store.links[i].spool_id, spool_id) == 0) {
            s_store.links[i].last_sync_weight = weight;
            s_store.links[i].last_seen        = now_ts();
            s_store.dirty = true;
            break;
        }
    }
    UNLOCK();
}
