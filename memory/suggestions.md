# Suggestions

## Future Implementation Items

### Wi-Fi provisioning  ✅ DONE 2026-04-15
- Implemented via `wifi_manager.c` with captive portal AP mode.
- See decisions.md for full details.

### Alpine.js auto-download  ✅ DONE 2026-04-15 — SUPERSEDED
- Was implemented as `scripts/download_alpine.py`. Now irrelevant.

### Vanilla JS migration  ✅ DONE 2026-04-15
- All 6 HTML pages rewritten to plain Vanilla JS.
- `app.js` now exports shared utilities (API, WS, toast, theme, initSidebar, helpers).
- No framework, no build step, no external download required.
- `extra_scripts` line in platformio.ini commented out.

### OTA Firmware Update  ✅ DONE 2026-04-15
- `POST /api/v1/ota/upload` streams firmware binary to inactive OTA partition.
- `GET /api/v1/ota/status` returns running partition, version, build date.
- `esp_ota_mark_app_valid_cancel_rollback()` called at boot (prevents rollback loops).
- Settings UI has OTA section with XHR progress bar.
- Partition table already had ota_0 / ota_1 (3 MB each) + otadata.
- `app_update` was already in CMakeLists REQUIRES — no partition changes needed.

### Backup import endpoint
- `POST /api/v1/backup/import` is stubbed and returns 501.
- Full implementation: validate schema_version, merge or replace inventory/ams/settings.

### AMS link type_id tracking
- ams_service.c `ams_service_type_has_links()` is conservative (always false).
- Store type_id in the AMS link struct for accurate deletion guard.

### spool_id in AMS link from inventory list
- The AMS page uses inventory list items (which don't include individual spool IDs)
  as link targets. A proper implementation should let users pick a specific spool
  within a type when multiple spools exist.

### SNTP time sync  ✅ DONE 2026-04-15
- `sntp_service.c/h` created. `sntp_service_start(tz, server)` called in `app_main.c`
  after Wi-Fi STA connects (step 9, before MQTT).
- Uses `settings.timezone` (POSIX TZ string, e.g. `"CET-1CEST,M3.5.0,M10.5.0/3"`).
  Falls back to UTC0 if empty.
- `sntp_service_set_timezone()` called from `settings_put_handler` so TZ updates
  take effect immediately when settings are saved, without reboot.
- Sync callback logs local time at first sync. `sntp_service_is_synced()` available
  for callers that need to guard on time validity.
- `esp_sntp` added to CMakeLists REQUIRES; `sntp_service.c` added to SRCS.

### OTA firmware update  ✅ SUPERSEDED 2026-04-15
- Implemented as `POST /api/v1/ota/upload` (raw `application/octet-stream`, not multipart).
  See the `### OTA Firmware Update` entry above for full details.

### auth_service
- Auth is deferred. When added: cookie-based session token,
  login page, and a middleware check in api_http.c before routing.

