#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "model_ams.h"

#define JOB_NAME_LEN  65
#define PRINTER_STATE_LEN  17  /* "idle","printing","paused","error","finish" */
#define MQTT_AMS_SLOT_MAX 16
#define MQTT_AMS_BRAND_LEN 32
#define MQTT_AMS_COLOR_LEN 10

typedef struct {
    char ams_id[AMS_ID_LEN];
    int  slot;
    bool known;
    char brand[MQTT_AMS_BRAND_LEN];
    char color_hex[MQTT_AMS_COLOR_LEN]; /* "#RRGGBB" */
} mqtt_ams_slot_runtime_t;

/* ── Live MQTT / printer state (RAM-only, never persisted) ── */
typedef struct {
    /* Connection */
    bool     mqtt_connected;
    bool     broker_reachable;
    bool     printer_online;
    uint32_t last_message_at;
    char     last_error[128];

    /* Print job */
    char     printer_state[PRINTER_STATE_LEN];
    char     current_job_name[JOB_NAME_LEN];
    int      progress_percent;
    int      remaining_minutes;

    /* Temperatures */
    float    bed_temp_c;
    float    nozzle_temp_c;

    /* Active AMS slot */
    char     active_ams_id[AMS_ID_LEN];
    int      active_slot;

    /* Current AMS tray snapshot from MQTT */
    mqtt_ams_slot_runtime_t ams_slots[MQTT_AMS_SLOT_MAX];
    int      ams_slot_count;

    uint32_t updated_at;
} mqtt_runtime_t;
