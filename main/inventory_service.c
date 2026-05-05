#include "inventory_service.h"
#include "ams_service.h"
#include "settings_service.h"
#include "storage_fs.h"
#include "storage_nvs.h"
#include "json_codec.h"
#include "api_ws.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#define TAG              "inventory"
#define INVENTORY_FILE   "inventory.json"
#define NVS_KEY_FTYPE_ID "ftype_next_id"
#define NVS_KEY_SPOOL_ID "spool_next_id"

/* ── Store ────────────────────────────────────────────────── */

static inventory_store_t s_store;
static SemaphoreHandle_t s_mutex;

#define LOCK()   xSemaphoreTake(s_mutex, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mutex)

/* ── ID generation ────────────────────────────────────────── */

static esp_err_t next_ftype_id(char *buf)
{
    uint32_t n = 0;
    storage_nvs_get_u32(NVS_KEY_FTYPE_ID, &n, 1);
    snprintf(buf, FTYPE_ID_LEN, "ftype_%06lu", (unsigned long)n);
    return storage_nvs_set_u32(NVS_KEY_FTYPE_ID, n + 1);
}

static esp_err_t next_spool_id(char *buf)
{
    uint32_t n = 0;
    storage_nvs_get_u32(NVS_KEY_SPOOL_ID, &n, 1);
    snprintf(buf, SPOOL_ID_LEN, "spool_%06lu", (unsigned long)n);
    return storage_nvs_set_u32(NVS_KEY_SPOOL_ID, n + 1);
}

/* ── Timestamp ────────────────────────────────────────────── */

static uint32_t now_ts(void)
{
    time_t t = time(NULL);
    return (t > 0) ? (uint32_t)t : (uint32_t)(esp_timer_get_time() / 1000000ULL);
}

/* ── Find helpers ─────────────────────────────────────────── */

static filament_type_t *find_type(const char *id)
{
    for (int i = 0; i < s_store.type_count; i++) {
        if (strcmp(s_store.types[i].id, id) == 0) return &s_store.types[i];
    }
    return NULL;
}

static spool_t *find_spool(filament_type_t *t, const char *spool_id)
{
    for (int i = 0; i < t->spool_count; i++) {
        if (strcmp(t->spools[i].id, spool_id) == 0) return &t->spools[i];
    }
    return NULL;
}

/* ── Persist ─────────────────────────────────────────────── */

static esp_err_t flush(void)
{
    cJSON *doc = json_encode_inventory_doc(&s_store);
    if (!doc) return ESP_ERR_NO_MEM;

    char *str = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if (!str) return ESP_ERR_NO_MEM;

    esp_err_t ret = storage_fs_write_file_atomic(INVENTORY_FILE, str, strlen(str));
    cJSON_free(str);

    if (ret == ESP_OK) {
        s_store.dirty = false;
        ws_broadcast_storage_saved();
    }
    return ret;
}

/* ── Init ─────────────────────────────────────────────────── */

esp_err_t inventory_service_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    /* Allocate store from PSRAM */
    s_store.types = heap_caps_calloc(MAX_FILAMENT_TYPES, sizeof(filament_type_t),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_store.types) {
        ESP_LOGE(TAG, "Failed to allocate inventory store in PSRAM");
        return ESP_ERR_NO_MEM;
    }
    s_store.type_count = 0;
    s_store.dirty      = false;

    /* Load from file */
    char  *buf = NULL;
    size_t len = 0;
    esp_err_t ret = storage_fs_read_file(INVENTORY_FILE, &buf, &len);
    if (ret == ESP_OK) {
        cJSON *root = cJSON_ParseWithLength(buf, len);
        free(buf);
        if (root) {
            json_decode_inventory_doc(root, &s_store);
            cJSON_Delete(root);
        }
    } else if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "No inventory file — starting fresh");
        flush(); /* write empty file */
    } else {
        if (buf) free(buf);
        return ret;
    }

    ESP_LOGI(TAG, "Inventory loaded: %d types", s_store.type_count);
    return ESP_OK;
}

/* ── Save ─────────────────────────────────────────────────── */

esp_err_t inventory_service_save(void)
{
    LOCK();
    esp_err_t ret = flush();
    UNLOCK();
    return ret;
}

void inventory_service_process_dirty(void)
{
    LOCK();
    if (s_store.dirty) flush();
    UNLOCK();
}

/* ── List helpers ─────────────────────────────────────────── */

static bool str_contains_ci(const char *haystack, const char *needle)
{
    if (!needle || !needle[0]) return true;
    /* Simple case-insensitive search */
    size_t nlen = strlen(needle);
    size_t hlen = strlen(haystack);
    if (nlen > hlen) return false;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        bool match = true;
        for (size_t j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i+j]) != tolower((unsigned char)needle[j])) {
                match = false; break;
            }
        }
        if (match) return true;
    }
    return false;
}

static bool type_matches(const filament_type_t *t, const inventory_query_t *q, int threshold)
{
    if (q->archived == 0 && t->archived)  return false;
    if (q->archived == 1 && !t->archived) return false;

    if (q->material[0] && strcasecmp(q->material, t->material) != 0) return false;
    if (q->brand[0]    && strcasecmp(q->brand,    t->brand)    != 0) return false;
    if (q->color[0]    && strcasecmp(q->color,    t->color)    != 0) return false;

    if (q->q[0]) {
        bool hit = str_contains_ci(t->brand,    q->q) ||
                   str_contains_ci(t->material, q->q) ||
                   str_contains_ci(t->color,    q->q) ||
                   str_contains_ci(t->notes,    q->q) ||
                   str_contains_ci(t->location, q->q);
        if (!hit) return false;
    }

    if (q->low_stock_only == 1) {
        /* Check if any active spool is below threshold */
        bool any_low = false;
        for (int i = 0; i < t->spool_count; i++) {
            if (!t->spools[i].archived && t->spools[i].remaining_grams < threshold) {
                any_low = true; break;
            }
        }
        if (!any_low) return false;
    }

    return true;
}

static int cmp_types_by_field(const void *a, const void *b, const char *field, bool desc)
{
    const filament_type_t *ta = (const filament_type_t *)a;
    const filament_type_t *tb = (const filament_type_t *)b;
    int cmp = 0;
    if      (strcmp(field, "brand")      == 0) cmp = strcmp(ta->brand,    tb->brand);
    else if (strcmp(field, "material")   == 0) cmp = strcmp(ta->material, tb->material);
    else if (strcmp(field, "color")      == 0) cmp = strcmp(ta->color,    tb->color);
    else if (strcmp(field, "updated_at") == 0) cmp = (ta->updated_at > tb->updated_at) ? 1 : -1;
    else if (strcmp(field, "created_at") == 0) cmp = (ta->created_at > tb->created_at) ? 1 : -1;
    return desc ? -cmp : cmp;
}

/* qsort_r not available in all IDF versions — use a module-level context */
static char  s_sort_field[33] = "updated_at";
static bool  s_sort_desc      = true;

static int sort_cmp(const void *a, const void *b)
{
    return cmp_types_by_field(a, b, s_sort_field, s_sort_desc);
}

static void build_list_item(const filament_type_t *t, filament_list_item_t *item, int threshold)
{
    memset(item, 0, sizeof(*item));
    snprintf(item->id,        sizeof(item->id),        "%s", t->id);
    snprintf(item->brand,     sizeof(item->brand),     "%s", t->brand);
    snprintf(item->material,  sizeof(item->material),  "%s", t->material);
    snprintf(item->color,     sizeof(item->color),     "%s", t->color);
    snprintf(item->color_hex, sizeof(item->color_hex), "%s", t->color_hex);
    snprintf(item->finish,    sizeof(item->finish),    "%s", t->finish);
    snprintf(item->location,  sizeof(item->location),  "%s", t->location);
    item->total_grams  = t->total_grams;
    item->price_per_kg = t->price_per_kg;
    item->archived     = t->archived;
    item->spool_count  = t->spool_count;
    item->updated_at   = t->updated_at;

    for (int i = 0; i < t->spool_count; i++) {
        if (!t->spools[i].archived) {
            item->active_spool_count++;
            item->total_remaining_grams += t->spools[i].remaining_grams;
            if (t->spools[i].remaining_grams < threshold) item->has_low_stock = true;
        }
    }

    item->ams_linked = inventory_service_type_has_ams_link(t->id);
}

/* ── List ─────────────────────────────────────────────────── */

esp_err_t inventory_service_list(const inventory_query_t *q,
                                 filament_list_item_t **out_items,
                                 int *out_total)
{
    int threshold = settings_service_get_threshold();
    int page      = (q->page > 0)      ? q->page      : 1;
    int page_size = (q->page_size > 0) ? q->page_size : 25;
    if (page_size > 100) page_size = 100;

    /* Determine sort */
    snprintf(s_sort_field, sizeof(s_sort_field), "%s",
             q->sort[0] ? q->sort : "updated_at");
    s_sort_desc = (q->dir[0] && strcasecmp(q->dir, "asc") == 0) ? false : true;

    LOCK();

    /* Collect matching types into a temporary PSRAM buffer for sorting */
    filament_type_t *scratch = heap_caps_malloc(
        s_store.type_count * sizeof(filament_type_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!scratch) { UNLOCK(); return ESP_ERR_NO_MEM; }

    int matched = 0;
    for (int i = 0; i < s_store.type_count; i++) {
        if (type_matches(&s_store.types[i], q, threshold)) {
            memcpy(&scratch[matched++], &s_store.types[i], sizeof(filament_type_t));
        }
    }

    UNLOCK();

    /* Sort */
    if (matched > 1) qsort(scratch, matched, sizeof(filament_type_t), sort_cmp);

    *out_total = matched;

    /* Page */
    int start = (page - 1) * page_size;
    int count = matched - start;
    if (count < 0)        count = 0;
    if (count > page_size) count = page_size;

    filament_list_item_t *items = count > 0
        ? malloc(count * sizeof(filament_list_item_t))
        : NULL;

    for (int i = 0; i < count; i++) {
        build_list_item(&scratch[start + i], &items[i], threshold);
    }

    heap_caps_free(scratch);

    *out_items = items;
    return ESP_OK;
}

/* ── Get type ─────────────────────────────────────────────── */

esp_err_t inventory_service_get_type(const char *id, filament_type_t *out)
{
    LOCK();
    filament_type_t *t = find_type(id);
    if (t) memcpy(out, t, sizeof(*out));
    UNLOCK();
    return t ? ESP_OK : ESP_ERR_NOT_FOUND;
}

/* ── Reset all ───────────────────────────────────────────── */

esp_err_t inventory_service_reset_all(void)
{
    LOCK();
    s_store.type_count = 0;
    s_store.dirty      = true;
    flush();
    UNLOCK();
    ESP_LOGI(TAG, "Inventory reset");
    return ESP_OK;
}

/* ── Create type ─────────────────────────────────────────── */

esp_err_t inventory_service_create_type(const filament_type_t *req, char *new_id)
{
    LOCK();
    if (s_store.type_count >= MAX_FILAMENT_TYPES) { UNLOCK(); return ESP_ERR_NO_MEM; }
    filament_type_t *t = &s_store.types[s_store.type_count];
    memcpy(t, req, sizeof(*t));
    if (next_ftype_id(t->id) != ESP_OK) { UNLOCK(); return ESP_FAIL; }
    snprintf(new_id, FTYPE_ID_LEN, "%s", t->id);
    uint32_t ts = now_ts();
    t->created_at  = ts;
    t->updated_at  = ts;

    /* New filament types start with one full active spool. */
    t->spool_count = 1;
    spool_t *s = &t->spools[0];
    memset(s, 0, sizeof(*s));
    if (next_spool_id(s->id) != ESP_OK) { UNLOCK(); return ESP_FAIL; }
    snprintf(s->type_id, sizeof(s->type_id), "%s", t->id);
    s->remaining_grams = (t->total_grams > 0) ? t->total_grams : 1000;
    s->archived        = false;
    s->created_at      = ts;
    s->updated_at      = ts;

    s_store.type_count++;
    s_store.dirty = true;
    UNLOCK();
    ws_broadcast_inventory_created(new_id);
    return ESP_OK;
}

/* ── Update type ─────────────────────────────────────────── */

esp_err_t inventory_service_update_type(const char *id, const filament_type_t *req)
{
    LOCK();
    filament_type_t *t = find_type(id);
    if (!t) { UNLOCK(); return ESP_ERR_NOT_FOUND; }

    /* Preserve ID, timestamps, and spools */
    int        sc  = t->spool_count;
    spool_t    sp[MAX_SPOOLS_PER_TYPE];
    uint32_t   ca  = t->created_at;
    memcpy(sp, t->spools, sc * sizeof(spool_t));

    memcpy(t, req, sizeof(*t));
    snprintf(t->id, sizeof(t->id), "%s", id);
    t->spool_count = sc;
    memcpy(t->spools, sp, sc * sizeof(spool_t));
    t->created_at  = ca;
    t->updated_at  = now_ts();
    s_store.dirty  = true;
    UNLOCK();
    ws_broadcast_inventory_updated(id, -1, now_ts());
    return ESP_OK;
}

/* ── Archive type ────────────────────────────────────────── */

esp_err_t inventory_service_archive_type(const char *id, bool archived)
{
    LOCK();
    filament_type_t *t = find_type(id);
    if (!t) { UNLOCK(); return ESP_ERR_NOT_FOUND; }
    t->archived   = archived;
    t->updated_at = now_ts();
    s_store.dirty = true;
    UNLOCK();
    return ESP_OK;
}

/* ── Delete type ─────────────────────────────────────────── */

esp_err_t inventory_service_delete_type(const char *id, bool force)
{
    if (!force && ams_service_type_has_links(id)) return ESP_ERR_INVALID_STATE;

    LOCK();
    int idx = -1;
    for (int i = 0; i < s_store.type_count; i++) {
        if (strcmp(s_store.types[i].id, id) == 0) { idx = i; break; }
    }
    if (idx < 0) { UNLOCK(); return ESP_ERR_NOT_FOUND; }

    /* Shift remaining types down */
    for (int i = idx; i < s_store.type_count - 1; i++) {
        memcpy(&s_store.types[i], &s_store.types[i+1], sizeof(filament_type_t));
    }
    s_store.type_count--;
    s_store.dirty = true;
    UNLOCK();
    ws_broadcast_inventory_deleted(id);
    return ESP_OK;
}

/* ── Create spool ─────────────────────────────────────────── */

esp_err_t inventory_service_create_spool(const char *type_id, const spool_t *req, char *new_id)
{
    LOCK();
    filament_type_t *t = find_type(type_id);
    if (!t)                               { UNLOCK(); return ESP_ERR_NOT_FOUND; }
    if (t->spool_count >= MAX_SPOOLS_PER_TYPE) { UNLOCK(); return ESP_ERR_NO_MEM; }

    spool_t *s = &t->spools[t->spool_count];
    memcpy(s, req, sizeof(*s));
    next_spool_id(s->id);
    snprintf(s->type_id, sizeof(s->type_id), "%s", type_id);
    snprintf(new_id, SPOOL_ID_LEN, "%s", s->id);
    uint32_t ts = now_ts();
    s->created_at = ts;
    s->updated_at = ts;
    t->spool_count++;
    t->updated_at = ts;
    s_store.dirty = true;
    UNLOCK();
    ws_broadcast_inventory_updated(type_id, -1, now_ts());
    return ESP_OK;
}

/* ── Update spool ─────────────────────────────────────────── */

esp_err_t inventory_service_update_spool(const char *type_id, const char *spool_id, const spool_t *req)
{
    LOCK();
    filament_type_t *t = find_type(type_id);
    if (!t) { UNLOCK(); return ESP_ERR_NOT_FOUND; }
    spool_t *s = find_spool(t, spool_id);
    if (!s) { UNLOCK(); return ESP_ERR_NOT_FOUND; }

    uint32_t ca = s->created_at;
    char     sid[SPOOL_ID_LEN];
    snprintf(sid, sizeof(sid), "%s", s->id);

    memcpy(s, req, sizeof(*s));
    snprintf(s->id,      sizeof(s->id),      "%s", sid);
    snprintf(s->type_id, sizeof(s->type_id), "%s", type_id);
    s->created_at  = ca;
    s->updated_at  = now_ts();
    t->updated_at  = s->updated_at;
    s_store.dirty  = true;
    UNLOCK();
    return ESP_OK;
}

/* ── Update spool grams ──────────────────────────────────── */

esp_err_t inventory_service_update_spool_grams(const char *type_id,
                                               const char *spool_id,
                                               int remaining_grams,
                                               uint32_t *out_updated_at)
{
    LOCK();
    filament_type_t *t = find_type(type_id);
    if (!t) { UNLOCK(); return ESP_ERR_NOT_FOUND; }
    spool_t *s = find_spool(t, spool_id);
    if (!s) { UNLOCK(); return ESP_ERR_NOT_FOUND; }

    if (remaining_grams < 0)         remaining_grams = 0;
    if (remaining_grams > t->total_grams) remaining_grams = t->total_grams;

    int delta = abs(s->remaining_grams - remaining_grams);
    s->remaining_grams = remaining_grams;
    uint32_t ts = now_ts();
    s->updated_at = ts;
    t->updated_at = ts;

    /* Only mark dirty for meaningful changes (>= 5g) */
    if (delta >= 5) s_store.dirty = true;
    if (out_updated_at) *out_updated_at = ts;
    UNLOCK();
    ws_broadcast_inventory_updated(type_id, remaining_grams, ts);
    return ESP_OK;
}

/* ── Archive spool ───────────────────────────────────────── */

esp_err_t inventory_service_archive_spool(const char *type_id, const char *spool_id, bool archived)
{
    LOCK();
    filament_type_t *t = find_type(type_id);
    if (!t) { UNLOCK(); return ESP_ERR_NOT_FOUND; }
    spool_t *s = find_spool(t, spool_id);
    if (!s) { UNLOCK(); return ESP_ERR_NOT_FOUND; }
    s->archived   = archived;
    s->updated_at = now_ts();
    t->updated_at = s->updated_at;
    s_store.dirty = true;
    UNLOCK();
    return ESP_OK;
}

/* ── Delete spool ─────────────────────────────────────────── */

esp_err_t inventory_service_delete_spool(const char *type_id, const char *spool_id)
{
    LOCK();
    filament_type_t *t = find_type(type_id);
    if (!t) { UNLOCK(); return ESP_ERR_NOT_FOUND; }

    int idx = -1;
    for (int i = 0; i < t->spool_count; i++) {
        if (strcmp(t->spools[i].id, spool_id) == 0) { idx = i; break; }
    }
    if (idx < 0) { UNLOCK(); return ESP_ERR_NOT_FOUND; }

    for (int i = idx; i < t->spool_count - 1; i++) {
        memcpy(&t->spools[i], &t->spools[i+1], sizeof(spool_t));
    }
    t->spool_count--;
    t->updated_at = now_ts();
    s_store.dirty = true;
    UNLOCK();
    return ESP_OK;
}

/* ── Options ─────────────────────────────────────────────── */

esp_err_t inventory_service_get_options(cJSON **out)
{
    /* Collect unique values from existing inventory */
    #define MAX_OPTS 64
    #define OPT_LEN  33

    char materials[MAX_OPTS][OPT_LEN];  int nm = 0;
    char brands   [MAX_OPTS][OPT_LEN];  int nb = 0;
    char finishes [MAX_OPTS][OPT_LEN];  int nf = 0;
    char locations[MAX_OPTS][OPT_LEN];  int nl = 0;

    /* Seed with common defaults */
    const char *default_materials[] = {"PLA","PETG","ABS","ASA","TPU","NYLON","PC","PLA+", NULL};
    const char *default_finishes[]  = {"Matte","Silk","Glossy","Carbon","Metallic", NULL};
    const char *default_spools[]    = {"Plastic","Cardboard","Refill", NULL};

    for (int i = 0; default_materials[i] && nm < MAX_OPTS; i++)
        snprintf(materials[nm++], OPT_LEN, "%s", default_materials[i]);
    for (int i = 0; default_finishes[i]  && nf < MAX_OPTS; i++)
        snprintf(finishes[nf++],  OPT_LEN, "%s", default_finishes[i]);

    /* Add any values present in inventory that aren't in the defaults */
    LOCK();
    for (int i = 0; i < s_store.type_count; i++) {
        filament_type_t *t = &s_store.types[i];

        bool found_m = false;
        for (int j = 0; j < nm; j++)
            if (strcasecmp(materials[j], t->material) == 0) { found_m = true; break; }
        if (!found_m && nm < MAX_OPTS) snprintf(materials[nm++], OPT_LEN, "%s", t->material);

        bool found_b = false;
        for (int j = 0; j < nb; j++)
            if (strcasecmp(brands[j], t->brand) == 0) { found_b = true; break; }
        if (!found_b && nb < MAX_OPTS) snprintf(brands[nb++], OPT_LEN, "%s", t->brand);

        if (t->finish[0]) {
            bool found_f = false;
            for (int j = 0; j < nf; j++)
                if (strcasecmp(finishes[j], t->finish) == 0) { found_f = true; break; }
            if (!found_f && nf < MAX_OPTS) snprintf(finishes[nf++], OPT_LEN, "%s", t->finish);
        }

        if (t->location[0]) {
            bool found_l = false;
            for (int j = 0; j < nl; j++)
                if (strcasecmp(locations[j], t->location) == 0) { found_l = true; break; }
            if (!found_l && nl < MAX_OPTS) snprintf(locations[nl++], OPT_LEN, "%s", t->location);
        }
    }
    UNLOCK();

    cJSON *data  = cJSON_CreateObject();

    cJSON *mat_arr = cJSON_AddArrayToObject(data, "materials");
    for (int i = 0; i < nm; i++) cJSON_AddItemToArray(mat_arr, cJSON_CreateString(materials[i]));

    cJSON *br_arr = cJSON_AddArrayToObject(data, "brands");
    for (int i = 0; i < nb; i++) cJSON_AddItemToArray(br_arr, cJSON_CreateString(brands[i]));

    cJSON *fin_arr = cJSON_AddArrayToObject(data, "finishes");
    for (int i = 0; i < nf; i++) cJSON_AddItemToArray(fin_arr, cJSON_CreateString(finishes[i]));

    cJSON *loc_arr = cJSON_AddArrayToObject(data, "locations");
    for (int i = 0; i < nl; i++) cJSON_AddItemToArray(loc_arr, cJSON_CreateString(locations[i]));

    cJSON *sp_arr = cJSON_AddArrayToObject(data, "spool_types");
    for (int i = 0; default_spools[i]; i++)
        cJSON_AddItemToArray(sp_arr, cJSON_CreateString(default_spools[i]));

    /* diameter: only 1.75 */
    cJSON *dia_arr = cJSON_AddArrayToObject(data, "diameters");
    cJSON_AddItemToArray(dia_arr, cJSON_CreateNumber(1.75));

    *out = data;
    return ESP_OK;
}

/* ── Tag UID check ────────────────────────────────────────── */

bool inventory_service_tag_uid_exists(const char *tag_uid, char *out_type_id, char *out_spool_id)
{
    if (!tag_uid || !tag_uid[0]) return false;
    LOCK();
    for (int i = 0; i < s_store.type_count; i++) {
        for (int j = 0; j < s_store.types[i].spool_count; j++) {
            if (strcasecmp(s_store.types[i].spools[j].tag_uid, tag_uid) == 0) {
                if (out_type_id)  snprintf(out_type_id,  FTYPE_ID_LEN, "%s", s_store.types[i].id);
                if (out_spool_id) snprintf(out_spool_id, SPOOL_ID_LEN, "%s", s_store.types[i].spools[j].id);
                UNLOCK();
                return true;
            }
        }
    }
    UNLOCK();
    return false;
}

/* ── AMS linked helper ────────────────────────────────────── */

bool inventory_service_type_has_ams_link(const char *type_id)
{
    /* Delegated to ams_service to avoid dependency inversion */
    return ams_service_type_has_links(type_id);
}

/* ── CSV import ──────────────────────────────────────────── */

/* Trim in-place: remove leading/trailing whitespace and surrounding quotes */
static void csv_trim(char *s)
{
    if (!s) return;
    /* Remove surrounding double-quotes */
    size_t len = strlen(s);
    if (len >= 2 && s[0] == '"' && s[len-1] == '"') {
        memmove(s, s + 1, len - 2);
        s[len-2] = '\0';
        len -= 2;
    }
    /* Trim leading */
    size_t start = 0;
    while (s[start] && isspace((unsigned char)s[start])) start++;
    if (start) memmove(s, s + start, strlen(s + start) + 1);
    /* Trim trailing */
    len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len-1])) s[--len] = '\0';
}

/* Split a single CSV line into cells; returns number of cells */
static int csv_split_line(char *line, char **cells, int max_cells)
{
    int n = 0;
    char *p = line;
    while (n < max_cells) {
        if (*p == '"') {
            /* Quoted field */
            p++;
            cells[n++] = p;
            while (*p && !(*p == '"' && (*(p+1) == ',' || *(p+1) == '\0' || *(p+1) == '\r' || *(p+1) == '\n'))) p++;
            if (*p == '"') { *p = '\0'; p++; }
        } else {
            cells[n++] = p;
        }
        while (*p && *p != ',' && *p != '\n' && *p != '\r') p++;
        if (*p == ',') { *p++ = '\0'; }
        else           { *p   = '\0'; break; }
    }
    return n;
}

/* Known column header names */
typedef enum {
    COL_BRAND=0, COL_TYPE, COL_FINISH, COL_COLOR_NAME, COL_COLOR_CODE,
    COL_TOTAL_WEIGHT, COL_WEIGHT_REMAINING, COL_QUANTITY, COL_SPOOL_TYPE,
    COL_SPOOL_MATERIAL, COL_REUSABLE_SPOOL, COL_DIAMETER, COL_LOCATION,
    COL_NOTES, COL_DATE_ADDED, COL_PRICE_PER_KG,
    COL_COUNT
} csv_col_t;

static const char *CSV_HEADERS[COL_COUNT] = {
    "brand", "type", "finish", "color name", "color code",
    "total weight (g)", "weight remaining (g)", "quantity", "spool type",
    "spool material", "reusable spool", "diameter (mm)", "location",
    "notes", "date added", "purchase price per kg"
};

static int map_header(const char *h)
{
    for (int i = 0; i < COL_COUNT; i++) {
        if (strcasecmp(h, CSV_HEADERS[i]) == 0) return i;
    }
    return -1;
}

/* Deduce spool_type from Spool Type + Spool Material CSV columns */
static void csv_decode_spool_type(const char *spool_type_csv, const char *spool_material, char *out, size_t out_len)
{
    if (strcasecmp(spool_type_csv, "refill") == 0) {
        snprintf(out, out_len, "Refill");
    } else if (strcasecmp(spool_material, "cardboard") == 0) {
        snprintf(out, out_len, "Cardboard");
    } else {
        snprintf(out, out_len, "Plastic");
    }
}

esp_err_t inventory_service_import_csv(const char *csv_body, size_t len, csv_import_result_t *result)
{
    memset(result, 0, sizeof(*result));

    /* Make a mutable copy */
    char *buf = malloc(len + 1);
    if (!buf) return ESP_ERR_NO_MEM;
    memcpy(buf, csv_body, len);
    buf[len] = '\0';

    /* Parse header row */
    int col_map[COL_COUNT];
    for (int i = 0; i < COL_COUNT; i++) col_map[i] = -1;

    char *line = strtok(buf, "\n");
    if (!line) { free(buf); return ESP_FAIL; }

    char *cells[32];
    int ncols = csv_split_line(line, cells, 32);
    for (int i = 0; i < ncols; i++) {
        csv_trim(cells[i]);
        int idx = map_header(cells[i]);
        if (idx >= 0) col_map[idx] = i;
    }

    /* Process data rows */
    while ((line = strtok(NULL, "\n")) != NULL) {
        /* Skip blank lines */
        bool blank = true;
        for (char *p = line; *p; p++) if (!isspace((unsigned char)*p)) { blank = false; break; }
        if (blank) continue;

        char *row[32];
        int nr = csv_split_line(line, row, 32);

        #define CELL(col) ((col_map[(col)] >= 0 && col_map[(col)] < nr) ? row[col_map[(col)]] : "")

        /* Skip 2.85mm rows */
        char dia_str[8] = "1.75";
        snprintf(dia_str, sizeof(dia_str), "%s", CELL(COL_DIAMETER));
        csv_trim(dia_str);
        float dia = strtof(dia_str, NULL);
        if (dia > 1.9f) { result->skipped_rows++; continue; } /* 2.85 or unknown */

        /* Extract fields */
        char brand[BRAND_LEN]         = {0};
        char material[MATERIAL_LEN]   = {0};
        char finish[FINISH_LEN]       = {0};
        char color[COLOR_LEN]         = {0};
        char color_hex[COLOR_HEX_LEN] = {0};
        char location[LOCATION_LEN]   = {0};
        char notes[NOTES_LEN]         = {0};
        char spool_type[SPOOL_TYPE_LEN]= {0};
        char price_str[16]            = {0};
        char total_str[16]            = {0};
        char remain_str[16]           = {0};
        char date_str[24]             = {0};

        snprintf(brand,      sizeof(brand),      "%s", CELL(COL_BRAND));         csv_trim(brand);
        snprintf(material,   sizeof(material),   "%s", CELL(COL_TYPE));          csv_trim(material);
        snprintf(finish,     sizeof(finish),     "%s", CELL(COL_FINISH));        csv_trim(finish);
        snprintf(color,      sizeof(color),      "%s", CELL(COL_COLOR_NAME));    csv_trim(color);
        snprintf(color_hex,  sizeof(color_hex),  "%s", CELL(COL_COLOR_CODE));    csv_trim(color_hex);
        snprintf(location,   sizeof(location),   "%s", CELL(COL_LOCATION));      csv_trim(location);
        snprintf(notes,      sizeof(notes),      "%s", CELL(COL_NOTES));         csv_trim(notes);
        snprintf(price_str,  sizeof(price_str),  "%s", CELL(COL_PRICE_PER_KG));  csv_trim(price_str);
        snprintf(total_str,  sizeof(total_str),  "%s", CELL(COL_TOTAL_WEIGHT));  csv_trim(total_str);
        snprintf(remain_str, sizeof(remain_str), "%s", CELL(COL_WEIGHT_REMAINING)); csv_trim(remain_str);
        snprintf(date_str,   sizeof(date_str),   "%s", CELL(COL_DATE_ADDED));    csv_trim(date_str);

        char spool_type_csv[17] = {0};
        char spool_mat     [17] = {0};
        snprintf(spool_type_csv, sizeof(spool_type_csv), "%s", CELL(COL_SPOOL_TYPE));
        snprintf(spool_mat,      sizeof(spool_mat),      "%s", CELL(COL_SPOOL_MATERIAL));
        csv_trim(spool_type_csv); csv_trim(spool_mat);
        csv_decode_spool_type(spool_type_csv, spool_mat, spool_type, sizeof(spool_type));

        if (!brand[0] || !material[0] || !color[0]) { result->skipped_rows++; continue; }

        int total_grams    = (int)strtol(total_str, NULL, 10);
        int remain_grams   = (int)strtol(remain_str, NULL, 10);
        float price_per_kg = strtof(price_str, NULL);
        if (total_grams <= 0)  total_grams  = 1000;
        if (remain_grams < 0)  remain_grams = 0;
        if (remain_grams > total_grams) remain_grams = total_grams;

        /* Parse date — expect YYYY-MM-DD */
        uint32_t created_ts = now_ts();
        if (strlen(date_str) >= 10) {
            struct tm tm = {0};
            if (sscanf(date_str, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) == 3) {
                tm.tm_year -= 1900;
                tm.tm_mon  -= 1;
                time_t t = mktime(&tm);
                if (t > 0) created_ts = (uint32_t)t;
            }
        }

        /* Find existing type or create it */
        LOCK();
        filament_type_t *found = NULL;
        for (int i = 0; i < s_store.type_count; i++) {
            filament_type_t *t = &s_store.types[i];
            if (strcasecmp(t->brand,    brand)    == 0 &&
                strcasecmp(t->material, material) == 0 &&
                strcasecmp(t->color,    color)    == 0 &&
                strcasecmp(t->finish,   finish)   == 0) {
                found = t; break;
            }
        }

        bool new_type = false;
        if (!found) {
            if (s_store.type_count >= MAX_FILAMENT_TYPES) { UNLOCK(); result->skipped_rows++; continue; }
            found = &s_store.types[s_store.type_count++];
            memset(found, 0, sizeof(*found));
            next_ftype_id(found->id);
            snprintf(found->brand,      sizeof(found->brand),      "%s", brand);
            snprintf(found->material,   sizeof(found->material),   "%s", material);
            snprintf(found->color,      sizeof(found->color),      "%s", color);
            snprintf(found->color_hex,  sizeof(found->color_hex),  "%s", color_hex);
            snprintf(found->finish,     sizeof(found->finish),     "%s", finish);
            snprintf(found->spool_type, sizeof(found->spool_type), "%s", spool_type);
            snprintf(found->location,   sizeof(found->location),   "%s", location);
            snprintf(found->notes,      sizeof(found->notes),      "%s", notes);
            found->total_grams  = total_grams;
            found->price_per_kg = price_per_kg;
            found->created_at   = created_ts;
            found->updated_at   = created_ts;
            new_type = true;
        }

        /* Add spool */
        if (found->spool_count < MAX_SPOOLS_PER_TYPE) {
            spool_t *s = &found->spools[found->spool_count++];
            memset(s, 0, sizeof(*s));
            next_spool_id(s->id);
            snprintf(s->type_id, sizeof(s->type_id), "%s", found->id);
            s->remaining_grams = remain_grams;
            s->created_at      = created_ts;
            s->updated_at      = created_ts;
            found->updated_at  = now_ts();
            s_store.dirty      = true;
            result->imported_spools++;
            if (new_type) result->imported_types++;
        }

        UNLOCK();
    }

    free(buf);
    flush();
    return ESP_OK;
}
