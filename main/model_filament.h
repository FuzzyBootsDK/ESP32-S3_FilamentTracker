#pragma once

#include <stdint.h>
#include <stdbool.h>

/* ── Limits ──────────────────────────────────────────────── */
#define MAX_FILAMENT_TYPES     200
#define MAX_SPOOLS_PER_TYPE     10

/* ── Field sizes (incl. null terminator) ─────────────────── */
#define FTYPE_ID_LEN    16   /* "ftype_000001" */
#define SPOOL_ID_LEN    16   /* "spool_000001"  */
#define BRAND_LEN       33
#define MATERIAL_LEN    17
#define COLOR_LEN       33
#define COLOR_HEX_LEN    8   /* "#RRGGBB" */
#define FINISH_LEN      17
#define VENDOR_LEN      33
#define SPOOL_TYPE_LEN  17   /* "Plastic","Cardboard","Refill" */
#define LOCATION_LEN    25
#define TAG_UID_LEN     25
#define NOTES_LEN      257

/* ── Individual physical spool ───────────────────────────── */
typedef struct {
    char     id[SPOOL_ID_LEN];
    char     type_id[FTYPE_ID_LEN]; /* back-reference to parent type */
    int      remaining_grams;
    char     tag_uid[TAG_UID_LEN];  /* NFC/RFID tag UID, may be empty */
    bool     archived;
    uint32_t created_at;            /* Unix epoch seconds */
    uint32_t updated_at;
} spool_t;

/* ── Filament type (groups one or more physical spools) ───── */
typedef struct {
    char     id[FTYPE_ID_LEN];
    char     brand[BRAND_LEN];
    char     material[MATERIAL_LEN];
    char     color[COLOR_LEN];
    char     color_hex[COLOR_HEX_LEN]; /* "#RRGGBB" */
    char     finish[FINISH_LEN];
    char     vendor[VENDOR_LEN];
    char     spool_type[SPOOL_TYPE_LEN];
    int      total_grams;               /* default capacity per spool */
    float    price_per_kg;
    char     location[LOCATION_LEN];
    char     notes[NOTES_LEN];
    bool     archived;
    uint32_t created_at;
    uint32_t updated_at;

    /* Embedded spools */
    int      spool_count;
    spool_t  spools[MAX_SPOOLS_PER_TYPE];
} filament_type_t;

/* ── In-memory inventory store ───────────────────────────── */
typedef struct {
    filament_type_t *types;   /* PSRAM-allocated array[MAX_FILAMENT_TYPES] */
    int              type_count;
    bool             dirty;
} inventory_store_t;

/* ── Compact list item for API responses ─────────────────── */
typedef struct {
    char     id[FTYPE_ID_LEN];
    char     brand[BRAND_LEN];
    char     material[MATERIAL_LEN];
    char     color[COLOR_LEN];
    char     color_hex[COLOR_HEX_LEN];
    char     finish[FINISH_LEN];
    int      total_grams;
    float    price_per_kg;
    char     location[LOCATION_LEN];
    bool     archived;
    int      spool_count;
    int      active_spool_count;
    int      total_remaining_grams;
    bool     has_low_stock;
    bool     ams_linked;
    uint32_t updated_at;
} filament_list_item_t;

/* ── Query parameters for list endpoint ──────────────────── */
typedef struct {
    char q[129];
    char material[MATERIAL_LEN];
    char brand[BRAND_LEN];
    char color[COLOR_LEN];
    int  archived;        /* -1 = unset, 0 = false, 1 = true */
    int  low_stock_only;  /* -1 = unset, 0 = false, 1 = true */
    char sort[33];
    char dir[5];
    int  page;
    int  page_size;
} inventory_query_t;
