#pragma once

#include <stdint.h>
#include <stdbool.h>

/* ── Field sizes ─────────────────────────────────────────── */
#define DEVICE_NAME_LEN   33
#define THEME_LEN         16  /* "dark","light","starbucks","harmony","spring" */
#define TIMEZONE_LEN      65
#define BROKER_HOST_LEN   65
#define MQTT_USER_LEN     33
#define MQTT_PASS_LEN     65
#define MQTT_CLIENT_LEN   33
#define TOPIC_ROOT_LEN    65
#define PRINTER_NAME_LEN  33
#define PRINTER_SER_LEN   33
#define AUTH_USER_LEN     33
#define AUTH_PASS_LEN     65

/* ── MQTT sub-settings ───────────────────────────────────── */
typedef struct {
    bool enabled;
    char broker_host[BROKER_HOST_LEN];
    int  broker_port;
    char username[MQTT_USER_LEN];
    char password[MQTT_PASS_LEN];     /* stored as plaintext in NVS, never sent over API */
    char client_id[MQTT_CLIENT_LEN];
    char topic_root[TOPIC_ROOT_LEN];
} mqtt_settings_t;

/* ── Printer sub-settings ─────────────────────────────────- */
typedef struct {
    char name[PRINTER_NAME_LEN];
    char serial[PRINTER_SER_LEN];
} printer_settings_t;

/* ── UI sub-settings ─────────────────────────────────────── */
typedef struct {
    bool auto_refresh;
    int  page_size;  /* 10-100 */
} ui_settings_t;

/* ── Auth sub-settings ───────────────────────────────────── */
typedef struct {
    bool enabled;
    char username[AUTH_USER_LEN];
    char password_hash[AUTH_PASS_LEN]; /* bcrypt or SHA-256 hex; empty = disabled */
} auth_settings_t;

/* ── Top-level settings ──────────────────────────────────── */
typedef struct {
    char               device_name[DEVICE_NAME_LEN];
    char               theme[THEME_LEN];
    char               timezone[TIMEZONE_LEN];
    int                low_stock_threshold_grams;
    mqtt_settings_t    mqtt;
    printer_settings_t printer;
    ui_settings_t      ui;
    auth_settings_t    auth;
    int                schema_version;
} app_settings_t;
