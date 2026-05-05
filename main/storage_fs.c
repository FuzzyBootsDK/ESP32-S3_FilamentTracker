#include "storage_fs.h"

#include "esp_littlefs.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>

#define TAG        "fs"
#define MOUNT_POINT "/data"
#define PARTITION   "storage"
#define MAX_PATH    128

static bool s_mounted = false;

/* ── Init ─────────────────────────────────────────────────── */

esp_err_t storage_fs_init(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path            = MOUNT_POINT,
        .partition_label      = PARTITION,
        .format_if_mount_failed = true,
        .dont_mount           = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount LittleFS: %s", esp_err_to_name(ret));
        return ret;
    }

    s_mounted = true;

    size_t total = 0, used = 0;
    esp_littlefs_info(PARTITION, &total, &used);
    ESP_LOGI(TAG, "LittleFS mounted — %zu KB total, %zu KB used", total / 1024, used / 1024);
    return ESP_OK;
}

/* ── Helpers ─────────────────────────────────────────────── */

static void build_path(char *dst, size_t dst_len, const char *path)
{
    /* If path already starts with '/', don't double-up the mount point */
    if (path[0] == '/') {
        snprintf(dst, dst_len, "%s%s", MOUNT_POINT, path);
    } else {
        snprintf(dst, dst_len, "%s/%s", MOUNT_POINT, path);
    }
}

/* ── Read ─────────────────────────────────────────────────── */

esp_err_t storage_fs_read_file(const char *path, char **out_buf, size_t *out_len)
{
    char full[MAX_PATH];
    build_path(full, sizeof(full), path);

    FILE *f = fopen(full, "rb");
    if (!f) {
        ESP_LOGD(TAG, "File not found: %s", full);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        return ESP_FAIL;
    }

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t nread = fread(buf, 1, (size_t)size, f);
    fclose(f);

    buf[nread] = '\0';
    *out_buf   = buf;
    *out_len   = nread;
    return ESP_OK;
}

/* ── Atomic write ─────────────────────────────────────────── */

esp_err_t storage_fs_write_file_atomic(const char *path, const char *buf, size_t len)
{
    char full[MAX_PATH];
    char tmp[MAX_PATH + 4];
    build_path(full, sizeof(full), path);
    snprintf(tmp, sizeof(tmp), "%s.tmp", full);

    FILE *f = fopen(tmp, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open temp file %s: %d", tmp, errno);
        return ESP_FAIL;
    }

    size_t written = fwrite(buf, 1, len, f);
    fflush(f);
    fclose(f);

    if (written != len) {
        ESP_LOGE(TAG, "Short write on %s (%zu of %zu)", tmp, written, len);
        remove(tmp);
        return ESP_FAIL;
    }

    if (rename(tmp, full) != 0) {
        ESP_LOGE(TAG, "rename %s -> %s failed: %d", tmp, full, errno);
        remove(tmp);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Saved %s (%zu bytes)", full, len);
    return ESP_OK;
}

/* ── Exists ───────────────────────────────────────────────── */

bool storage_fs_file_exists(const char *path)
{
    char full[MAX_PATH];
    build_path(full, sizeof(full), path);
    struct stat st;
    return (stat(full, &st) == 0);
}

/* ── Info ─────────────────────────────────────────────────── */

esp_err_t storage_fs_get_info(size_t *total_bytes, size_t *used_bytes)
{
    return esp_littlefs_info(PARTITION, total_bytes, used_bytes);
}

/* ── UI file write (non-atomic, used at first boot) ──────── */

esp_err_t storage_fs_write_ui_file(const char *path, const char *buf, size_t len)
{
    char full[MAX_PATH];
    build_path(full, sizeof(full), path);

    FILE *f = fopen(full, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s: %d", full, errno);
        return ESP_FAIL;
    }
    fwrite(buf, 1, len, f);
    fclose(f);
    return ESP_OK;
}
