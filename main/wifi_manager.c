#include "wifi_manager.h"
#include "storage_nvs.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include <string.h>
#include <stdlib.h>

#define TAG          "wifi_mgr"

/* ── NVS keys ─────────────────────────────────────────────── */
#define NVS_KEY_SSID "wifi_ssid"
#define NVS_KEY_PASS "wifi_pass"

/* ── AP constants ─────────────────────────────────────────── */
#define AP_SSID         "FilamentTracker"
#define AP_CHANNEL      1
#define AP_MAX_CONN     4
#define AP_IP_STR       "192.168.4.1"  /* default lwIP AP address */

/* ── NVS field limits ─────────────────────────────────────── */
#define SSID_LEN  33   /* 32 chars + NUL */
#define PASS_LEN  65   /* 64 chars + NUL */

/* ── STA retry ────────────────────────────────────────────── */
#define MAX_RETRIES  10
static int s_retry = 0;

/* ═══════════════════════════════════════════════════════════ */
/* STA event handler                                           */
/* ═══════════════════════════════════════════════════════════ */

static void sta_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry < MAX_RETRIES) {
            s_retry++;
            ESP_LOGW(TAG, "Wi-Fi disconnected — retry %d/%d", s_retry, MAX_RETRIES);
            esp_wifi_connect();
        } else {
            /* Give up and keep retrying every 60 s; the user can reset creds
               from the settings page to enter AP/provisioning mode again. */
            s_retry = 0;
            ESP_LOGE(TAG, "Cannot reach Wi-Fi — waiting 60 s before next attempt");
            vTaskDelay(pdMS_TO_TICKS(60000));
            esp_wifi_connect();
        }

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        s_retry = 0;
        ESP_LOGI(TAG, "Connected!  IP: " IPSTR "  (open http://" IPSTR "/ in your browser)",
                 IP2STR(&e->ip_info.ip), IP2STR(&e->ip_info.ip));
    }
}

/* ═══════════════════════════════════════════════════════════ */
/* STA mode start                                              */
/* ═══════════════════════════════════════════════════════════ */

static void start_sta(const char *ssid, const char *pass)
{
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(sta_netif, "filament-tracker");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &sta_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &sta_event_handler, NULL, NULL));

    wifi_config_t wifi_cfg = {0};
    snprintf((char *)wifi_cfg.sta.ssid,     sizeof(wifi_cfg.sta.ssid),     "%s", ssid);
    snprintf((char *)wifi_cfg.sta.password, sizeof(wifi_cfg.sta.password), "%s", pass);
    wifi_cfg.sta.threshold.authmode = (pass && pass[0]) ? WIFI_AUTH_WPA2_PSK
                                                        : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "STA mode — connecting to '%s'", ssid);
}

/* ═══════════════════════════════════════════════════════════ */
/* DNS server (redirects every query to 192.168.4.1 so that   */
/* phones trigger the captive-portal UI automatically)         */
/* ═══════════════════════════════════════════════════════════ */

/**
 * Build a minimal DNS response that answers all A-record queries with
 * the AP's IP address.  The response is constructed in-place in `resp`.
 * Returns the total response length, or 0 on error.
 */
static int build_dns_response(const uint8_t *query, int qlen, uint8_t *resp)
{
    if (qlen < 12 || qlen > 512) return 0;

    memcpy(resp, query, qlen);       /* copy original header + question */

    /* Set QR=1 (response), Opcode=0, AA=1, TC=0, RD=1, RA=1, RCODE=0 */
    resp[2] = 0x85;
    resp[3] = 0x80;
    /* ANCOUNT = 1 */
    resp[6] = 0x00; resp[7] = 0x01;
    /* NSCOUNT = ARCOUNT = 0 */
    resp[8] = 0x00; resp[9] = 0x00;
    resp[10]= 0x00; resp[11]= 0x00;

    int pos = qlen;
    /* Answer: name = pointer to offset 12 (start of question section) */
    resp[pos++] = 0xC0; resp[pos++] = 0x0C;
    /* TYPE A */
    resp[pos++] = 0x00; resp[pos++] = 0x01;
    /* CLASS IN */
    resp[pos++] = 0x00; resp[pos++] = 0x01;
    /* TTL = 60 s */
    resp[pos++] = 0x00; resp[pos++] = 0x00;
    resp[pos++] = 0x00; resp[pos++] = 0x3C;
    /* RDLENGTH = 4 */
    resp[pos++] = 0x00; resp[pos++] = 0x04;
    /* RDATA: 192.168.4.1 */
    resp[pos++] = 192;
    resp[pos++] = 168;
    resp[pos++] = 4;
    resp[pos++] = 1;

    return pos;
}

static void dns_server_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS: socket() failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in sv = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port        = htons(53),
    };
    if (bind(sock, (struct sockaddr *)&sv, sizeof(sv)) < 0) {
        ESP_LOGE(TAG, "DNS: bind(:53) failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS server listening on :53");

    uint8_t buf[512], resp[600];
    struct sockaddr_in client;
    socklen_t clen = sizeof(client);

    while (1) {
        int n = recvfrom(sock, buf, sizeof(buf), 0,
                         (struct sockaddr *)&client, &clen);
        if (n <= 0) continue;
        int rlen = build_dns_response(buf, n, resp);
        if (rlen > 0) {
            sendto(sock, resp, rlen, 0,
                   (struct sockaddr *)&client, clen);
        }
    }
}

/* ═══════════════════════════════════════════════════════════ */
/* Captive-portal HTML (self-contained, no external deps)     */
/* ═══════════════════════════════════════════════════════════ */

static const char PORTAL_HTML[] =
"<!DOCTYPE html>"
"<html lang=\"en\"><head>"
"<meta charset=\"UTF-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Filament Tracker \xe2\x80\x94 Wi-Fi Setup</title>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#e0e0e0;"
     "display:flex;align-items:center;justify-content:center;min-height:100vh}"
".card{background:#16213e;border-radius:12px;padding:2rem;width:90%;max-width:440px;"
       "box-shadow:0 4px 24px rgba(0,0,0,.5)}"
"h1{color:#a78bfa;font-size:1.4rem;margin-bottom:.4rem}"
"p{color:#9ca3af;font-size:.9rem;margin-bottom:1.5rem}"
"label{display:block;margin-bottom:1rem}"
"span{display:block;font-size:.85rem;color:#9ca3af;margin-bottom:.3rem}"
"input{width:100%;padding:.6rem .8rem;border-radius:6px;"
       "border:1px solid #374151;background:#0f3460;color:#e0e0e0;font-size:1rem}"
"button{margin-top:1.5rem;width:100%;padding:.75rem;background:#7c3aed;color:#fff;"
        "border:none;border-radius:6px;font-size:1rem;cursor:pointer}"
"button:hover{background:#6d28d9}"
"button:disabled{background:#4b5563;cursor:not-allowed}"
".msg{margin-top:1rem;padding:.75rem;border-radius:6px;text-align:center;font-size:.9rem}"
".err{background:#7f1d1d;color:#fca5a5}"
".ok{background:#14532d;color:#86efac}"
"</style></head><body>"
"<div class=\"card\">"
"<h1>\xf0\x9f\xa7\xb5 Filament Tracker</h1>"
"<p>Connect this device to your Wi-Fi network to get started.</p>"
"<form id=\"f\" onsubmit=\"save(event)\">"
"<label><span>Wi-Fi Network (SSID)</span>"
"<input id=\"s\" type=\"text\" autocomplete=\"off\" required placeholder=\"Network name\"></label>"
"<label><span>Password</span>"
"<input id=\"p\" type=\"password\" autocomplete=\"off\" placeholder=\"Leave blank for open networks\"></label>"
"<button type=\"submit\" id=\"btn\">Connect</button>"
"</form>"
"<div id=\"m\"></div>"
"</div>"
"<script>"
"function save(e){"
  "e.preventDefault();"
  "var btn=document.getElementById('btn');"
  "btn.textContent='Saving\u2026';btn.disabled=true;"
  "fetch('/wifi/save',{"
    "method:'POST',"
    "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
    "body:'ssid='+encodeURIComponent(document.getElementById('s').value)"
        "+'&pass='+encodeURIComponent(document.getElementById('p').value)"
  "})"
  ".then(function(r){return r.json();})"
  ".then(function(d){"
    "var m=document.getElementById('m');"
    "if(d.ok){m.className='msg ok';"
      "m.textContent='Saved! The device will restart and connect in a few seconds.';}"
    "else{m.className='msg err';m.textContent=d.error||'Error saving.';"
      "btn.textContent='Connect';btn.disabled=false;}"
  "})"
  ".catch(function(){"
    "document.getElementById('m').className='msg err';"
    "document.getElementById('m').textContent='Network error \u2014 please try again.';"
    "btn.textContent='Connect';btn.disabled=false;"
  "});"
"}"
"</script></body></html>";

/* ═══════════════════════════════════════════════════════════ */
/* URL-form decoding helpers                                   */
/* ═══════════════════════════════════════════════════════════ */

static void url_decode(const char *src, char *dst, size_t dst_len)
{
    size_t i = 0;
    while (*src && i < dst_len - 1) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], '\0' };
            dst[i++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

static void parse_form_field(const char *body, const char *key,
                              char *out, size_t out_len)
{
    out[0] = '\0';
    size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *val = p + klen + 1;
            const char *end = strchr(val, '&');
            size_t vlen = end ? (size_t)(end - val) : strlen(val);
            char tmp[128] = {0};
            if (vlen >= sizeof(tmp)) vlen = sizeof(tmp) - 1;
            memcpy(tmp, val, vlen);
            url_decode(tmp, out, out_len);
            return;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
}

/* ═══════════════════════════════════════════════════════════ */
/* Captive-portal HTTP handlers                               */
/* ═══════════════════════════════════════════════════════════ */

/* GET /  — serve the Wi-Fi setup page */
static esp_err_t portal_root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, PORTAL_HTML, (ssize_t)strlen(PORTAL_HTML));
    return ESP_OK;
}

/* POST /wifi/save  — body: ssid=...&pass=... (URL-encoded) */
static esp_err_t portal_save_handler(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > 256) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Bad request\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char body[257] = {0};
    int n = httpd_req_recv(req, body, (int)req->content_len);
    if (n <= 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Receive error\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    body[n] = '\0';

    char ssid[SSID_LEN] = {0};
    char pass[PASS_LEN] = {0};
    parse_form_field(body, "ssid", ssid, sizeof(ssid));
    parse_form_field(body, "pass", pass, sizeof(pass));

    if (!ssid[0]) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"SSID cannot be empty\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    storage_nvs_set_str(NVS_KEY_SSID, ssid);
    storage_nvs_set_str(NVS_KEY_PASS, pass);
    ESP_LOGI(TAG, "Credentials saved for '%s' — rebooting into STA mode", ssid);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);

    /* Short delay so the HTTP response can be flushed before reboot */
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return ESP_OK;
}

/**
 * 404 handler: redirect everything to the portal root.
 * This catches captive-portal probe URLs from iOS, Android, and Windows
 * (e.g. /hotspot-detect.html, /generate_204, /ncsi.txt) so browsers
 * automatically open the Wi-Fi setup page.
 */
static esp_err_t portal_redirect_handler(httpd_req_t *req,
                                          httpd_err_code_t err_code)
{
    (void)err_code;
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" AP_IP_STR "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static httpd_handle_t start_portal_httpd(void)
{
    httpd_config_t cfg     = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers   = 8;
    cfg.uri_match_fn       = httpd_uri_match_wildcard;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Portal HTTPD start failed");
        return NULL;
    }

    static const httpd_uri_t rt_get  = {
        .uri = "/",     .method = HTTP_GET,  .handler = portal_root_handler  };
    static const httpd_uri_t rt_save = {
        .uri = "/wifi/save", .method = HTTP_POST, .handler = portal_save_handler };

    httpd_register_uri_handler(server, &rt_get);
    httpd_register_uri_handler(server, &rt_save);
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, portal_redirect_handler);

    ESP_LOGI(TAG, "Captive-portal HTTP server running on :80");
    return server;
}

/* ═══════════════════════════════════════════════════════════ */
/* AP mode start                                               */
/* ═══════════════════════════════════════════════════════════ */

static void ap_event_handler(void *arg, esp_event_base_t base,
                              int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(TAG, "AP: client connected   %02x:%02x:%02x:%02x:%02x:%02x", MAC2STR(e->mac));
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)data;
        ESP_LOGI(TAG, "AP: client disconnected %02x:%02x:%02x:%02x:%02x:%02x", MAC2STR(e->mac));
    }
}

static void start_ap(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &ap_event_handler, NULL, NULL));

    wifi_config_t ap_cfg = {0};
    memcpy(ap_cfg.ap.ssid, AP_SSID, strlen(AP_SSID));
    ap_cfg.ap.ssid_len      = (uint8_t)strlen(AP_SSID);
    ap_cfg.ap.channel       = AP_CHANNEL;
    ap_cfg.ap.authmode      = WIFI_AUTH_OPEN;
    ap_cfg.ap.max_connection= AP_MAX_CONN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP mode started — SSID: \"%s\"  IP: " AP_IP_STR, AP_SSID);
    ESP_LOGI(TAG, "Connect your phone/PC to \"%s\" and open http://" AP_IP_STR, AP_SSID);
}

/* ═══════════════════════════════════════════════════════════ */
/* Public API                                                  */
/* ═══════════════════════════════════════════════════════════ */

bool wifi_manager_has_credentials(void)
{
    char ssid[SSID_LEN];
    esp_err_t ret = storage_nvs_get_str(NVS_KEY_SSID, ssid, sizeof(ssid));
    return (ret == ESP_OK && ssid[0] != '\0');
}

void wifi_manager_reset_credentials(void)
{
    storage_nvs_erase_key(NVS_KEY_SSID);
    storage_nvs_erase_key(NVS_KEY_PASS);
    ESP_LOGI(TAG, "Wi-Fi credentials erased — rebooting into provisioning mode");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

wifi_manager_mode_t wifi_manager_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    char ssid[SSID_LEN] = {0};
    char pass[PASS_LEN] = {0};

    bool has = (storage_nvs_get_str(NVS_KEY_SSID, ssid, sizeof(ssid)) == ESP_OK
                && ssid[0] != '\0');

    if (has) {
        storage_nvs_get_str(NVS_KEY_PASS, pass, sizeof(pass));
        start_sta(ssid, pass);
        return WIFI_MANAGER_MODE_STA;
    }

    /* ── No credentials: start AP + captive portal ── */
    start_ap();
    xTaskCreate(dns_server_task, "dns_srv", 4096, NULL, 5, NULL);
    start_portal_httpd();

    return WIFI_MANAGER_MODE_AP;
}
