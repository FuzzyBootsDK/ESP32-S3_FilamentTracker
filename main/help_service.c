#include "help_service.h"
#include "esp_log.h"
#include <string.h>

#define TAG "help"

/* ── Static help content ─────────────────────────────────── */

typedef struct {
    const char *id;
    const char *title;
    const char *content_html;
} help_section_t;

static const help_section_t SECTIONS[] = {
    {
        .id    = "getting-started",
        .title = "Getting Started",
        .content_html =
            "<p>Connect to Wi-Fi, then open <strong>Settings</strong> to configure MQTT and printer details.</p>"
            "<ol>"
            "<li>Set your device name in Settings.</li>"
            "<li>Enter broker host, credentials (if needed), topic root, and printer serial.</li>"
            "<li>Enable MQTT and click <strong>Save</strong>.</li>"
            "<li>Open <strong>Live View</strong> to verify connection and incoming messages.</li>"
            "<li>Use <strong>Add Filament</strong> to start adding your spools.</li>"
            "</ol>"
    },
    {
        .id    = "adding-filament",
        .title = "Adding Filament",
        .content_html =
            "<p>Go to <strong>Add Filament</strong> and fill in brand, material, colour and weight.</p>"
            "<p>Each entry is a <em>filament type</em>. You can add individual spools to a type from the Inventory page.</p>"
            "<p>If you have an NFC reader, tap a tag to pre-fill the Tag UID field.</p>"
    },
    {
        .id    = "ams-live",
        .title = "AMS (Live from MQTT)",
        .content_html =
            "<p>The <strong>AMS</strong> page now shows slot data directly from MQTT.</p>"
            "<ul>"
            "<li>Shows <strong>No AMS</strong> until the first AMS MQTT message arrives.</li>"
            "<li>Keeps last known AMS slot data until a newer message changes it.</li>"
            "<li>If color/brand is unknown, the UI shows <em>Unknown color • Unknown brand</em>.</li>"
            "</ul>"
            "<p>Manual AMS linking workflow has been removed.</p>"
    },
    {
        .id    = "mqtt-setup",
        .title = "MQTT Setup + Live View",
        .content_html =
            "<p>Configure MQTT from <strong>Settings</strong>, then monitor from <strong>Live View</strong>.</p>"
            "<ul>"
            "<li><strong>Broker host</strong>: Relay IP or printer IP.</li>"
            "<li><strong>Broker port</strong>: Typically 1883 (relay) or 8883 (printer TLS).</li>"
            "<li><strong>Username</strong>: Usually <code>bblp</code> for direct printer, empty for no-auth relay.</li>"
            "<li><strong>Password</strong>: Printer access code for direct printer, empty for no-auth relay.</li>"
            "<li><strong>Topic root</strong>: <code>device/{serial}</code></li>"
            "</ul>"
            "<p>Live View shows connection status, live printer state, and the last 10 incoming MQTT messages.</p>"
    },
    {
        .id    = "inventory-import",
        .title = "Importing from the Blazor App",
        .content_html =
            "<p>If you are migrating from the original FilamentTracker v2 Blazor application:</p>"
            "<ol>"
            "<li>In the old app, go to Settings &rarr; Export to CSV.</li>"
            "<li>In this app, go to Settings &rarr; Import CSV and upload the file.</li>"
            "<li>All 1.75 mm filaments will be imported. 2.85 mm rows are skipped.</li>"
            "</ol>"
    },
    {
        .id    = "backup-restore",
        .title = "Backup &amp; Restore",
        .content_html =
            "<p>Use <strong>Settings &rarr; Export Backup</strong> to download a full JSON backup of your inventory, AMS links and settings.</p>"
            "<p>Backup import is currently not implemented in the API.</p>"
            "<p>Backups include schema version information for future compatibility.</p>"
    },
    {
        .id    = "about",
        .title = "About",
        .content_html =
            "<p><strong>ESP32-S3 Filament Tracker</strong> is a self-hosted filament inventory manager embedded in your ESP32-S3 device.</p>"
            "<p>Features: Inventory management, BambuLab MQTT integration, Live View monitoring, MQTT-driven AMS display, Settings management.</p>"
            "<p>Source: Based on the open-source FilamentTracker v2 project.</p>"
    },
};

#define SECTION_COUNT  (int)(sizeof(SECTIONS) / sizeof(SECTIONS[0]))

/* ── Init ─────────────────────────────────────────────────── */

esp_err_t help_service_init(void)
{
    ESP_LOGI(TAG, "Help service ready (%d sections)", SECTION_COUNT);
    return ESP_OK;
}

/* ── List ─────────────────────────────────────────────────── */

esp_err_t help_service_list(cJSON **out)
{
    cJSON *data     = cJSON_CreateObject();
    cJSON *sections = cJSON_AddArrayToObject(data, "sections");

    for (int i = 0; i < SECTION_COUNT; i++) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "id",           SECTIONS[i].id);
        cJSON_AddStringToObject(s, "title",        SECTIONS[i].title);
        cJSON_AddStringToObject(s, "content_html", SECTIONS[i].content_html);
        cJSON_AddItemToArray(sections, s);
    }

    *out = data;
    return ESP_OK;
}

/* ── Get section ─────────────────────────────────────────── */

esp_err_t help_service_get_section(const char *section_id, cJSON **out)
{
    for (int i = 0; i < SECTION_COUNT; i++) {
        if (strcmp(SECTIONS[i].id, section_id) == 0) {
            cJSON *data = cJSON_CreateObject();
            cJSON_AddStringToObject(data, "id",           SECTIONS[i].id);
            cJSON_AddStringToObject(data, "title",        SECTIONS[i].title);
            cJSON_AddStringToObject(data, "content_html", SECTIONS[i].content_html);
            *out = data;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}
