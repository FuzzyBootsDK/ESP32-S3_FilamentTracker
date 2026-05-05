#include "sntp_service.h"

#include "esp_log.h"
#include "esp_sntp.h"

#include <string.h>
#include <time.h>

#define TAG "sntp"

/* NTP server used when none is specified */
#define DEFAULT_NTP_SERVER "pool.ntp.org"

static volatile bool _synced = false;

/* ── SNTP sync notification callback ────────────────────── */
static void sntp_sync_cb(struct timeval *tv)
{
    _synced = true;
    char buf[32];
    time_t now = tv->tv_sec;
    struct tm *tm_info = localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    ESP_LOGI(TAG, "Time synchronised: %s (local)", buf);
}

/* ── Public API ──────────────────────────────────────────── */

esp_err_t sntp_service_start(const char *tz_posix, const char *ntp_server)
{
    /* Apply timezone before starting SNTP so the first sync callback
       already sees the correct local time. */
    sntp_service_set_timezone(tz_posix);

    if (esp_sntp_enabled()) {
        /* Already running (e.g. called twice) — just refresh timezone. */
        ESP_LOGD(TAG, "SNTP already running, timezone refreshed");
        return ESP_OK;
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

    const char *server = (ntp_server && ntp_server[0]) ? ntp_server : DEFAULT_NTP_SERVER;
    esp_sntp_setservername(0, server);

    sntp_set_time_sync_notification_cb(sntp_sync_cb);

    /* ESP-IDF ≥5.x uses esp_sntp_init(); earlier 4.x used sntp_init().
       The esp_sntp_* wrappers are available in both via esp_sntp.h. */
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP started, server=%s tz=%s", server,
             (tz_posix && tz_posix[0]) ? tz_posix : "UTC");
    return ESP_OK;
}

void sntp_service_set_timezone(const char *tz_posix)
{
    if (tz_posix && tz_posix[0]) {
        setenv("TZ", tz_posix, 1);
    } else {
        setenv("TZ", "UTC0", 1);
    }
    tzset();
    ESP_LOGD(TAG, "TZ set to: %s", tz_posix && tz_posix[0] ? tz_posix : "UTC0");
}

bool sntp_service_is_synced(void)
{
    return _synced;
}
