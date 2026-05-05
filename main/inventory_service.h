#pragma once

#include "esp_err.h"
#include "model_filament.h"
#include "cJSON.h"
#include <stdbool.h>

/* ── Lifecycle ───────────────────────────────────────────── */

/** Load inventory from LittleFS into PSRAM-backed store. */
esp_err_t inventory_service_init(void);

/** Force an immediate save regardless of dirty flag. */
esp_err_t inventory_service_save(void);

/**
 * Called from a periodic timer task.
 * Saves if the dirty flag is set; clears the flag on success.
 */
void inventory_service_process_dirty(void);

/* ── Filament type CRUD ──────────────────────────────────── */

/**
 * Apply filter/sort/page to produce a compact list.
 * Caller owns out_items (dynamically allocated array of page_size items).
 * out_total = total matching items (before paging).
 */
esp_err_t inventory_service_list(const inventory_query_t *q,
                                 filament_list_item_t   **out_items,
                                 int                     *out_total);

/** Get a full filament type by ID. Fills *out on success. */
esp_err_t inventory_service_get_type(const char *id, filament_type_t *out);

/** Create a new filament type. Fills new_id (FTYPE_ID_LEN). */
esp_err_t inventory_service_create_type(const filament_type_t *req, char *new_id);

/** Replace a filament type by ID (spools are left untouched). */
esp_err_t inventory_service_update_type(const char *id, const filament_type_t *req);

/** Archive or unarchive a filament type. */
esp_err_t inventory_service_archive_type(const char *id, bool archived);

/**
 * Permanently delete a filament type.
 * Returns ESP_ERR_INVALID_STATE if the type has active AMS links and force=false.
 */
esp_err_t inventory_service_delete_type(const char *id, bool force);
esp_err_t inventory_service_reset_all(void); /* delete every type and spool */

/* ── Spool CRUD ──────────────────────────────────────────── */

/** Add a spool to an existing filament type. Fills new_id (SPOOL_ID_LEN). */
esp_err_t inventory_service_create_spool(const char *type_id,
                                         const spool_t *req,
                                         char *new_id);

/** Update spool fields within a type. */
esp_err_t inventory_service_update_spool(const char *type_id,
                                         const char *spool_id,
                                         const spool_t *req);

/** Update remaining_grams on a specific spool. */
esp_err_t inventory_service_update_spool_grams(const char *type_id,
                                               const char *spool_id,
                                               int remaining_grams,
                                               uint32_t *out_updated_at);

/** Archive or unarchive a spool. */
esp_err_t inventory_service_archive_spool(const char *type_id,
                                          const char *spool_id,
                                          bool archived);

/** Permanently delete a spool. */
esp_err_t inventory_service_delete_spool(const char *type_id, const char *spool_id);

/* ── Helpers ─────────────────────────────────────────────── */

/** Populate the meta/options response (materials, brands, finishes, locations). */
esp_err_t inventory_service_get_options(cJSON **out);

/**
 * Check if any spool with the given tag_uid already exists.
 * Returns its type_id and spool_id if found.
 */
bool inventory_service_tag_uid_exists(const char *tag_uid,
                                      char *out_type_id,
                                      char *out_spool_id);

/**
 * Set the ams_linked flag for the compact list — called by ams_service
 * during sync so the list API can reflect linkage without a join.
 * (Lightweight: just tests whether any spool in a type is AMS-linked.)
 */
bool inventory_service_type_has_ams_link(const char *type_id);

/* ── CSV import ──────────────────────────────────────────── */

typedef struct {
    int imported_types;
    int imported_spools;
    int skipped_rows;
    char last_error[128];
} csv_import_result_t;

/**
 * Parse a CSV body (from the original Blazor app export format) and
 * import into the inventory store. Only 1.75mm rows are imported.
 */
esp_err_t inventory_service_import_csv(const char *csv_body, size_t len,
                                       csv_import_result_t *result);

/* needed by json_codec */
#include "cJSON.h"
