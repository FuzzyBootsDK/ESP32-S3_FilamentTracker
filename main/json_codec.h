#pragma once

#include "cJSON.h"
#include "model_filament.h"
#include "model_ams.h"
#include "model_settings.h"
#include "model_runtime.h"

/* ── Filament type ───────────────────────────────────────── */
cJSON *json_encode_filament_type(const filament_type_t *t);
cJSON *json_encode_filament_list_item(const filament_list_item_t *item);
int    json_decode_filament_type(const cJSON *obj, filament_type_t *out);

/* ── Spool ───────────────────────────────────────────────── */
cJSON *json_encode_spool(const spool_t *s);
int    json_decode_spool(const cJSON *obj, spool_t *out);

/* ── AMS link ────────────────────────────────────────────── */
cJSON *json_encode_ams_link(const ams_link_t *l);
int    json_decode_ams_link(const cJSON *obj, ams_link_t *out);

/* ── Settings ────────────────────────────────────────────── */
cJSON *json_encode_settings(const app_settings_t *s, bool mask_password);
int    json_decode_settings(const cJSON *obj, app_settings_t *out);

/* ── Runtime state ───────────────────────────────────────── */
cJSON *json_encode_runtime(const mqtt_runtime_t *r);

/* ── Inventory file (root document) ─────────────────────── */
cJSON *json_encode_inventory_doc(const inventory_store_t *store);
int    json_decode_inventory_doc(const cJSON *root, inventory_store_t *store);

/* ── AMS links file ──────────────────────────────────────── */
cJSON *json_encode_ams_links_doc(const ams_store_t *store);
int    json_decode_ams_links_doc(const cJSON *root, ams_store_t *store);

/* ── Utility ─────────────────────────────────────────────── */

/** Safe string copy from cJSON string item; silently truncates. */
void json_copy_str(const cJSON *obj, const char *key, char *dst, size_t dst_len);

/** Safe int from cJSON; returns default_val if missing/wrong type. */
int  json_get_int(const cJSON *obj, const char *key, int default_val);

/** Safe float from cJSON; returns default_val if missing/wrong type. */
float json_get_float(const cJSON *obj, const char *key, float default_val);

/** Safe bool from cJSON; returns default_val if missing/wrong type. */
bool json_get_bool(const cJSON *obj, const char *key, bool default_val);
