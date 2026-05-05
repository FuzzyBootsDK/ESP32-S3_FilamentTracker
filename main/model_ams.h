#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "model_filament.h"

/* ── Limits ──────────────────────────────────────────────── */
#define MAX_AMS_LINKS  64
#define AMS_LINK_ID_LEN  16  /* "ams_000001" */
#define AMS_ID_LEN       17  /* "AMS_A" etc.  */

/* ── A single AMS slot→spool binding ────────────────────── */
typedef struct {
    char     id[AMS_LINK_ID_LEN];
    char     ams_id[AMS_ID_LEN];
    int      slot;                     /* 1-based slot number */
    char     spool_id[SPOOL_ID_LEN];   /* references spool_t.id */
    char     tag_uid[TAG_UID_LEN];
    bool     enabled;
    int      last_sync_weight;
    uint32_t last_seen;
    uint32_t updated_at;

    /* Cached label from linked spool — filled at runtime, not persisted */
    char     filament_label[80];       /* "Bambu Lab PLA Black" */
} ams_link_t;

/* ── In-memory AMS store ─────────────────────────────────── */
typedef struct {
    ams_link_t links[MAX_AMS_LINKS];
    int        link_count;
    bool       dirty;
} ams_store_t;
