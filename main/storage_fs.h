#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

/**
 * Mount LittleFS at /data using the "storage" partition.
 * Formats the partition on first use.  Must be called before any file I/O.
 */
esp_err_t storage_fs_init(void);

/**
 * Read a file from /data/<path> into a malloc'd buffer.
 * Caller must free(*out_buf) when done.
 * Returns ESP_ERR_NOT_FOUND if the file does not exist.
 */
esp_err_t storage_fs_read_file(const char *path, char **out_buf, size_t *out_len);

/**
 * Write len bytes from buf to /data/<path> atomically:
 *   1. Write to /data/<path>.tmp
 *   2. fsync / close
 *   3. rename to /data/<path>
 * This guarantees no half-written files survive a power loss.
 */
esp_err_t storage_fs_write_file_atomic(const char *path, const char *buf, size_t len);

/** Return true if /data/<path> exists. */
bool storage_fs_file_exists(const char *path);

/** Populate *total_bytes and *used_bytes for the storage partition. */
esp_err_t storage_fs_get_info(size_t *total_bytes, size_t *used_bytes);

/** Write a static UI asset bundled as a C string (no need for atomic swap). */
esp_err_t storage_fs_write_ui_file(const char *path, const char *buf, size_t len);
