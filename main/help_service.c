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
            "<li>Enter your BambuLab printer IP, access code and serial number.</li>"
            "<li>Enable MQTT and click <strong>Save</strong>.</li>"
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
        .id    = "ams-linking",
        .title = "AMS Linking",
        .content_html =
            "<p>Open the <strong>AMS</strong> page to link filament spools to physical AMS slots.</p>"
            "<ol>"
            "<li>Click an AMS slot.</li>"
            "<li>Select the spool from your inventory.</li>"
            "<li>Optionally attach the NFC/RFID tag UID.</li>"
            "<li>Save the link.</li>"
            "</ol>"
            "<p>Once linked, the remaining weight is updated automatically from AMS data.</p>"
    },
    {
        .id    = "mqtt-setup",
        .title = "MQTT / BambuLab Setup",
        .content_html =
            "<p>The tracker connects to your BambuLab printer using MQTT.</p>"
            "<ul>"
            "<li><strong>Broker host</strong>: Your printer&rsquo;s IP address.</li>"
            "<li><strong>Broker port</strong>: 1883 for local, 8883 for TLS.</li>"
            "<li><strong>Username</strong>: <code>bblp</code></li>"
            "<li><strong>Password</strong>: Your printer&rsquo;s access code (8 digits, found in Network settings).</li>"
            "<li><strong>Topic root</strong>: <code>device/{serial}</code></li>"
            "</ul>"
            "<p>Use the <strong>Test Connection</strong> button to verify settings before saving.</p>"
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
            "<p>Use <strong>Settings &rarr; Import Backup</strong> to restore from a previously exported JSON file.</p>"
            "<p>Backups include schema version information and are forward-compatible with future firmware updates.</p>"
    },
    {
        .id    = "about",
        .title = "About",
        .content_html =
            "<p><strong>ESP32-S3 Filament Tracker</strong> is a self-hosted filament inventory manager embedded in your ESP32-S3 device.</p>"
            "<p>Features: Inventory management, BambuLab MQTT integration, AMS spool linking, Settings management.</p>"
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
