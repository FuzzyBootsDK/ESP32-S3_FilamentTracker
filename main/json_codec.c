#include "json_codec.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── Utility ─────────────────────────────────────────────── */

void json_copy_str(const cJSON *obj, const char *key, char *dst, size_t dst_len)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring) {
        snprintf(dst, dst_len, "%s", item->valuestring);
    }
}

int json_get_int(const cJSON *obj, const char *key, int default_val)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item)) return (int)item->valuedouble;
    return default_val;
}

float json_get_float(const cJSON *obj, const char *key, float default_val)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item)) return (float)item->valuedouble;
    return default_val;
}

bool json_get_bool(const cJSON *obj, const char *key, bool default_val)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(item)) return cJSON_IsTrue(item);
    return default_val;
}

/* ── Spool ───────────────────────────────────────────────── */

cJSON *json_encode_spool(const spool_t *s)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "id",              s->id);
    cJSON_AddStringToObject(obj, "type_id",         s->type_id);
    cJSON_AddNumberToObject(obj, "remaining_grams", s->remaining_grams);
    cJSON_AddStringToObject(obj, "tag_uid",         s->tag_uid);
    cJSON_AddBoolToObject  (obj, "archived",        s->archived);
    cJSON_AddNumberToObject(obj, "created_at",      (double)s->created_at);
    cJSON_AddNumberToObject(obj, "updated_at",      (double)s->updated_at);
    return obj;
}

int json_decode_spool(const cJSON *obj, spool_t *out)
{
    memset(out, 0, sizeof(*out));
    json_copy_str(obj, "id",              out->id,              sizeof(out->id));
    json_copy_str(obj, "type_id",         out->type_id,         sizeof(out->type_id));
    out->remaining_grams = json_get_int  (obj, "remaining_grams", 0);
    json_copy_str(obj, "tag_uid",         out->tag_uid,         sizeof(out->tag_uid));
    out->archived        = json_get_bool (obj, "archived",        false);
    out->created_at      = (uint32_t)json_get_int(obj, "created_at", 0);
    out->updated_at      = (uint32_t)json_get_int(obj, "updated_at", 0);
    return 0;
}

/* ── Filament type ───────────────────────────────────────── */

cJSON *json_encode_filament_type(const filament_type_t *t)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "id",           t->id);
    cJSON_AddStringToObject(obj, "brand",        t->brand);
    cJSON_AddStringToObject(obj, "material",     t->material);
    cJSON_AddStringToObject(obj, "color",        t->color);
    cJSON_AddStringToObject(obj, "color_hex",    t->color_hex);
    cJSON_AddStringToObject(obj, "finish",       t->finish);
    cJSON_AddStringToObject(obj, "vendor",       t->vendor);
    cJSON_AddStringToObject(obj, "spool_type",   t->spool_type);
    cJSON_AddNumberToObject(obj, "total_grams",  t->total_grams);
    cJSON_AddNumberToObject(obj, "price_per_kg", (double)t->price_per_kg);
    cJSON_AddStringToObject(obj, "location",     t->location);
    cJSON_AddStringToObject(obj, "notes",        t->notes);
    cJSON_AddBoolToObject  (obj, "archived",     t->archived);
    cJSON_AddNumberToObject(obj, "created_at",   (double)t->created_at);
    cJSON_AddNumberToObject(obj, "updated_at",   (double)t->updated_at);

    cJSON *spools = cJSON_AddArrayToObject(obj, "spools");
    for (int i = 0; i < t->spool_count; i++) {
        cJSON_AddItemToArray(spools, json_encode_spool(&t->spools[i]));
    }
    return obj;
}

cJSON *json_encode_filament_list_item(const filament_list_item_t *item)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "id",                    item->id);
    cJSON_AddStringToObject(obj, "brand",                 item->brand);
    cJSON_AddStringToObject(obj, "material",              item->material);
    cJSON_AddStringToObject(obj, "color",                 item->color);
    cJSON_AddStringToObject(obj, "color_hex",             item->color_hex);
    cJSON_AddStringToObject(obj, "finish",                item->finish);
    cJSON_AddNumberToObject(obj, "total_grams",           item->total_grams);
    cJSON_AddNumberToObject(obj, "price_per_kg",          (double)item->price_per_kg);
    cJSON_AddStringToObject(obj, "location",              item->location);
    cJSON_AddBoolToObject  (obj, "archived",              item->archived);
    cJSON_AddNumberToObject(obj, "spool_count",           item->spool_count);
    cJSON_AddNumberToObject(obj, "active_spool_count",    item->active_spool_count);
    cJSON_AddNumberToObject(obj, "total_remaining_grams", item->total_remaining_grams);
    cJSON_AddBoolToObject  (obj, "has_low_stock",         item->has_low_stock);
    cJSON_AddBoolToObject  (obj, "ams_linked",            item->ams_linked);
    cJSON_AddNumberToObject(obj, "updated_at",            (double)item->updated_at);
    return obj;
}

int json_decode_filament_type(const cJSON *obj, filament_type_t *out)
{
    memset(out, 0, sizeof(*out));
    json_copy_str(obj, "id",           out->id,         sizeof(out->id));
    json_copy_str(obj, "brand",        out->brand,      sizeof(out->brand));
    json_copy_str(obj, "material",     out->material,   sizeof(out->material));
    json_copy_str(obj, "color",        out->color,      sizeof(out->color));
    json_copy_str(obj, "color_hex",    out->color_hex,  sizeof(out->color_hex));
    json_copy_str(obj, "finish",       out->finish,     sizeof(out->finish));
    json_copy_str(obj, "vendor",       out->vendor,     sizeof(out->vendor));
    json_copy_str(obj, "spool_type",   out->spool_type, sizeof(out->spool_type));
    out->total_grams  = json_get_int  (obj, "total_grams",  1000);
    out->price_per_kg = json_get_float(obj, "price_per_kg", 0.0f);
    json_copy_str(obj, "location",     out->location,   sizeof(out->location));
    json_copy_str(obj, "notes",        out->notes,      sizeof(out->notes));
    out->archived     = json_get_bool (obj, "archived",     false);
    out->created_at   = (uint32_t)json_get_int(obj, "created_at", 0);
    out->updated_at   = (uint32_t)json_get_int(obj, "updated_at", 0);

    cJSON *spools = cJSON_GetObjectItemCaseSensitive(obj, "spools");
    if (cJSON_IsArray(spools)) {
        int count = cJSON_GetArraySize(spools);
        if (count > MAX_SPOOLS_PER_TYPE) count = MAX_SPOOLS_PER_TYPE;
        for (int i = 0; i < count; i++) {
            cJSON *s = cJSON_GetArrayItem(spools, i);
            if (s) {
                json_decode_spool(s, &out->spools[out->spool_count]);
                out->spool_count++;
            }
        }
    }
    return 0;
}

/* ── AMS link ────────────────────────────────────────────── */

cJSON *json_encode_ams_link(const ams_link_t *l)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "id",               l->id);
    cJSON_AddStringToObject(obj, "ams_id",           l->ams_id);
    cJSON_AddNumberToObject(obj, "slot",             l->slot);
    cJSON_AddStringToObject(obj, "spool_id",         l->spool_id);
    cJSON_AddStringToObject(obj, "tag_uid",          l->tag_uid);
    cJSON_AddBoolToObject  (obj, "enabled",          l->enabled);
    cJSON_AddNumberToObject(obj, "last_sync_weight", l->last_sync_weight);
    cJSON_AddNumberToObject(obj, "last_seen",        (double)l->last_seen);
    cJSON_AddNumberToObject(obj, "updated_at",       (double)l->updated_at);
    return obj;
}

int json_decode_ams_link(const cJSON *obj, ams_link_t *out)
{
    memset(out, 0, sizeof(*out));
    json_copy_str(obj, "id",       out->id,       sizeof(out->id));
    json_copy_str(obj, "ams_id",   out->ams_id,   sizeof(out->ams_id));
    out->slot             = json_get_int (obj, "slot",             1);
    json_copy_str(obj, "spool_id", out->spool_id, sizeof(out->spool_id));
    json_copy_str(obj, "tag_uid",  out->tag_uid,  sizeof(out->tag_uid));
    out->enabled          = json_get_bool(obj, "enabled",          true);
    out->last_sync_weight = json_get_int (obj, "last_sync_weight", 0);
    out->last_seen        = (uint32_t)json_get_int(obj, "last_seen",  0);
    out->updated_at       = (uint32_t)json_get_int(obj, "updated_at", 0);
    return 0;
}

/* ── Settings ────────────────────────────────────────────── */

cJSON *json_encode_settings(const app_settings_t *s, bool mask_password)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "device_name",                s->device_name);
    cJSON_AddStringToObject(obj, "theme",                      s->theme);
    cJSON_AddStringToObject(obj, "timezone",                   s->timezone);
    cJSON_AddNumberToObject(obj, "low_stock_threshold_grams",  s->low_stock_threshold_grams);
    cJSON_AddNumberToObject(obj, "schema_version",             s->schema_version);

    cJSON *mqtt = cJSON_AddObjectToObject(obj, "mqtt");
    cJSON_AddBoolToObject  (mqtt, "enabled",      s->mqtt.enabled);
    cJSON_AddStringToObject(mqtt, "broker_host",  s->mqtt.broker_host);
    cJSON_AddNumberToObject(mqtt, "broker_port",  s->mqtt.broker_port);
    cJSON_AddStringToObject(mqtt, "username",     s->mqtt.username);
    if (mask_password) {
        cJSON_AddBoolToObject(mqtt, "password_masked", true);
    } else {
        cJSON_AddStringToObject(mqtt, "password", s->mqtt.password);
    }
    cJSON_AddStringToObject(mqtt, "client_id",   s->mqtt.client_id);
    cJSON_AddStringToObject(mqtt, "topic_root",  s->mqtt.topic_root);

    cJSON *printer = cJSON_AddObjectToObject(obj, "printer");
    cJSON_AddStringToObject(printer, "name",   s->printer.name);
    cJSON_AddStringToObject(printer, "serial", s->printer.serial);

    cJSON *ui = cJSON_AddObjectToObject(obj, "ui");
    cJSON_AddBoolToObject  (ui, "auto_refresh", s->ui.auto_refresh);
    cJSON_AddNumberToObject(ui, "page_size",    s->ui.page_size);

    cJSON *auth = cJSON_AddObjectToObject(obj, "auth");
    cJSON_AddBoolToObject  (auth, "enabled",  s->auth.enabled);
    cJSON_AddStringToObject(auth, "username", s->auth.username);

    return obj;
}

int json_decode_settings(const cJSON *obj, app_settings_t *out)
{
    json_copy_str(obj, "device_name", out->device_name, sizeof(out->device_name));
    json_copy_str(obj, "theme",       out->theme,       sizeof(out->theme));
    json_copy_str(obj, "timezone",    out->timezone,    sizeof(out->timezone));
    out->low_stock_threshold_grams = json_get_int(obj, "low_stock_threshold_grams", 150);
    out->schema_version            = json_get_int(obj, "schema_version", 1);

    cJSON *mqtt = cJSON_GetObjectItemCaseSensitive(obj, "mqtt");
    if (cJSON_IsObject(mqtt)) {
        out->mqtt.enabled     = json_get_bool(mqtt, "enabled",     false);
        json_copy_str(mqtt, "broker_host", out->mqtt.broker_host,  sizeof(out->mqtt.broker_host));
        out->mqtt.broker_port = json_get_int (mqtt, "broker_port", 1883);
        json_copy_str(mqtt, "username",    out->mqtt.username,     sizeof(out->mqtt.username));
        /* Only update password if a plaintext field is present (not masked) */
        cJSON *pw = cJSON_GetObjectItemCaseSensitive(mqtt, "password");
        if (cJSON_IsString(pw) && pw->valuestring) {
            snprintf(out->mqtt.password, sizeof(out->mqtt.password), "%s", pw->valuestring);
        }
        json_copy_str(mqtt, "client_id",  out->mqtt.client_id,    sizeof(out->mqtt.client_id));
        json_copy_str(mqtt, "topic_root", out->mqtt.topic_root,   sizeof(out->mqtt.topic_root));
    }

    cJSON *printer = cJSON_GetObjectItemCaseSensitive(obj, "printer");
    if (cJSON_IsObject(printer)) {
        json_copy_str(printer, "name",   out->printer.name,   sizeof(out->printer.name));
        json_copy_str(printer, "serial", out->printer.serial, sizeof(out->printer.serial));
    }

    cJSON *ui = cJSON_GetObjectItemCaseSensitive(obj, "ui");
    if (cJSON_IsObject(ui)) {
        out->ui.auto_refresh = json_get_bool(ui, "auto_refresh", true);
        out->ui.page_size    = json_get_int (ui, "page_size",    25);
    }

    cJSON *auth = cJSON_GetObjectItemCaseSensitive(obj, "auth");
    if (cJSON_IsObject(auth)) {
        out->auth.enabled = json_get_bool(auth, "enabled", false);
        json_copy_str(auth, "username", out->auth.username, sizeof(out->auth.username));
        cJSON *apw = cJSON_GetObjectItemCaseSensitive(auth, "password");
        if (cJSON_IsString(apw) && apw->valuestring) {
            snprintf(out->auth.password_hash, sizeof(out->auth.password_hash), "%s", apw->valuestring);
        }
    }
    return 0;
}

/* ── Runtime ─────────────────────────────────────────────── */

cJSON *json_encode_runtime(const mqtt_runtime_t *r)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddBoolToObject  (obj, "connected",        r->mqtt_connected);
    cJSON_AddBoolToObject  (obj, "broker_reachable",  r->broker_reachable);
    cJSON_AddBoolToObject  (obj, "printer_online",    r->printer_online);
    cJSON_AddStringToObject(obj, "printer_state",     r->printer_state);
    cJSON_AddStringToObject(obj, "current_job_name",  r->current_job_name);
    cJSON_AddNumberToObject(obj, "progress_percent",  r->progress_percent);
    cJSON_AddNumberToObject(obj, "remaining_minutes", r->remaining_minutes);
    cJSON_AddNumberToObject(obj, "bed_temp_c",     (double)r->bed_temp_c);
    cJSON_AddNumberToObject(obj, "nozzle_temp_c",  (double)r->nozzle_temp_c);
    cJSON_AddStringToObject(obj, "active_ams_id",  r->active_ams_id);
    cJSON_AddNumberToObject(obj, "active_slot",    r->active_slot);
    cJSON *slots = cJSON_AddArrayToObject(obj, "ams_slots");
    for (int i = 0; i < r->ams_slot_count; i++) {
        const mqtt_ams_slot_runtime_t *s = &r->ams_slots[i];
        cJSON *it = cJSON_CreateObject();
        cJSON_AddStringToObject(it, "ams_id", s->ams_id);
        cJSON_AddNumberToObject(it, "slot", s->slot);
        cJSON_AddBoolToObject(it, "known", s->known);
        cJSON_AddStringToObject(it, "brand", s->brand);
        cJSON_AddStringToObject(it, "color_hex", s->color_hex);
        cJSON_AddItemToArray(slots, it);
    }
    cJSON_AddNumberToObject(obj, "updated_at",     (double)r->updated_at);
    if (r->last_error[0]) {
        cJSON_AddStringToObject(obj, "last_error", r->last_error);
    } else {
        cJSON_AddNullToObject(obj, "last_error");
    }
    cJSON_AddNumberToObject(obj, "last_message_at", (double)r->last_message_at);
    return obj;
}

/* ── Inventory document ──────────────────────────────────── */

cJSON *json_encode_inventory_doc(const inventory_store_t *store)
{
    cJSON *root  = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schema_version", 1);
    cJSON *types = cJSON_AddArrayToObject(root, "types");
    for (int i = 0; i < store->type_count; i++) {
        cJSON_AddItemToArray(types, json_encode_filament_type(&store->types[i]));
    }
    return root;
}

int json_decode_inventory_doc(const cJSON *root, inventory_store_t *store)
{
    store->type_count = 0;
    cJSON *types = cJSON_GetObjectItemCaseSensitive(root, "types");
    if (!cJSON_IsArray(types)) return -1;

    int count = cJSON_GetArraySize(types);
    if (count > MAX_FILAMENT_TYPES) count = MAX_FILAMENT_TYPES;

    for (int i = 0; i < count; i++) {
        cJSON *t = cJSON_GetArrayItem(types, i);
        if (!t) continue;
        filament_type_t *slot = &store->types[store->type_count];
        json_decode_filament_type(t, slot);
        /* Skip entries with no ID, no brand, or no material — they are corrupt */
        if (!slot->id[0] || !slot->brand[0] || !slot->material[0]) continue;
        /* Skip entries whose ID doesn't start with "ftype_" */
        if (strncmp(slot->id, "ftype_", 6) != 0) continue;
        store->type_count++;
    }
    return 0;
}

/* ── AMS links document ──────────────────────────────────── */

cJSON *json_encode_ams_links_doc(const ams_store_t *store)
{
    cJSON *root  = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schema_version", 1);
    cJSON *links = cJSON_AddArrayToObject(root, "links");
    for (int i = 0; i < store->link_count; i++) {
        cJSON_AddItemToArray(links, json_encode_ams_link(&store->links[i]));
    }
    return root;
}

int json_decode_ams_links_doc(const cJSON *root, ams_store_t *store)
{
    store->link_count = 0;
    cJSON *links = cJSON_GetObjectItemCaseSensitive(root, "links");
    if (!cJSON_IsArray(links)) return -1;

    int count = cJSON_GetArraySize(links);
    if (count > MAX_AMS_LINKS) count = MAX_AMS_LINKS;

    for (int i = 0; i < count; i++) {
        cJSON *l = cJSON_GetArrayItem(links, i);
        if (l) {
            json_decode_ams_link(l, &store->links[store->link_count]);
            store->link_count++;
        }
    }
    return 0;
}
