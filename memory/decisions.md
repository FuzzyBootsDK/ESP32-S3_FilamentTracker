# Decisions

## 2026-04-15 — UI Alignment to 90% (original Blazor app)

### Theme system
- Changed from `data-theme` attribute on `<html>` to **body class** approach
  (`body.dark`, `body.light`, `body.starbucks`, `body.harmony`, `body.spring`)
  matching the original app exactly.
- 5 themes: dark (default), light, starbucks (forest green), harmony (navy+orange),
  spring (deep teal).
- CSS variable names changed to match original: `--bg`, `--panel`, `--panel2`,
  `--panelSolid`, `--border`, `--border2`, `--text`, `--muted`, `--accent`,
  `--danger`, `--warn`, `--ok`, `--shadow`, `--focus`.
- Anti-flash inline script added to every HTML page body (before Alpine initializes).
- `model_settings.h` THEME_LEN raised from 9 → 16 (longest: "starbucks\0" = 10).

### Tile inventory layout
- Replaced `.filament-card`/`.card-grid` with `.tile`/`.grid` exactly matching the
  original 200px-minimum tile grid.
- `.swatch` is 110px tall with overlaid `.brand` (top-left), `.name` (center),
  `.pct` (bottom-right), `.badge` (top-right), `.amsMismatchBadge` (below badge).
- Left 4px status rail via `tile::before` colored by `.ok`/`.low`/`.critical` class.
- `.stacked` class adds a `::after` shadow-card for multi-spool types.

### KPI bar + low-stock strip
- `.kpis` section: Total, Low (amber), Critical (red), Add button.
- `.strip` warning banner appears when `lowCount > 0`.
- `toggleLowFilter()` toggles between "Show All" and "Show Low Only".
- KPI counts computed client-side from loaded items (pageSize=200, loads all).

### Sort options
- 8 sort options: newest, oldest, remainingAsc/Desc, nameAsc/Desc, colorDark/Light.
- Mapped to API `sort`/`dir` params via `_sortMap` const in app.js.

### Theme cards in settings
- Settings appearance section replaced 3 buttons with 5 `.themeCard` selectors.
- Each card shows `.themeSwatches` (4 color preview divs) + `.themeName`.
- `themes` array defined in `settingsPage` Alpine component.

## 2026-04-15 — Wi-Fi Provisioning & Alpine.js

### Captive portal (wifi_manager.c)
- Replaced hardcoded SSID/password with `wifi_manager.c` / `wifi_manager.h`.
- First boot (no NVS credentials): AP mode, SSID "FilamentTracker", open network.
  - DNS server task (UDP :53) redirects all queries to 192.168.4.1 so browsers
    auto-open the captive portal.
  - Minimal HTTPD on :80 with GET / (styled Wi-Fi setup form) and POST /wifi/save.
  - Creds saved to NVS keys `wifi_ssid` / `wifi_pass`; device reboots into STA.
  - Main API HTTP server is NOT started in AP mode.
- Subsequent boots: STA mode, connects with stored credentials (retries with back-off).
- `POST /api/v1/settings/wifi/reset` erases creds and reboots back to AP mode.
  UI button "Re-configure Wi-Fi" added to settings.html danger zone.
- `main/CMakeLists.txt` gains `wifi_manager.c`, `esp_netif`, and `lwip` REQUIRES.
- `main/app_main.c` removes the old static `wifi_init()` and calls `wifi_manager_init()`.

### Alpine.js auto-download
- `scripts/download_alpine.py` is a PlatformIO pre-build script that downloads
  Alpine.js v3.14.9 from jsDelivr if `ui/js/alpine.min.js` is absent.
- `platformio.ini` wired via `extra_scripts = pre:scripts/download_alpine.py`.
- If the download fails (offline), a clear warning is printed and the build continues.
  The UI will be non-functional until the file is present.
- No change to Alpine.js usage in the HTML/JS files — full Vanilla JS rewrite was
  deferred (not needed given build-time download).

## 2026-04-15 — Build Toolchain

### PlatformIO as the upload/build tool
- User wants to upload via PlatformIO in VS Code (ESP is not yet arrived).
- Added `platformio.ini` targeting `framework = espidf` + `board = esp32-s3-devkitc-1`.
- No source files moved — existing `main/` + root `CMakeLists.txt` structure is fully
  compatible with PlatformIO's ESP-IDF framework mode.
- `board_upload.flash_size = 16MB` + `board_upload.maximum_size` set for esptool.py.
- `board_build.filesystem = littlefs` enables "Upload Filesystem Image" for data/ folder.
- `sdkconfig.defaults` continues to own all chip/PSRAM/flash/peripheral config.
- Board can be changed to `lolin_s3` once the exact module variant is known.

## 2026-04-14 — Initial Scoping Decisions

### Feature scope
- Print Cost Calculator: **omitted** — keep the reduced scope defined in the port plan.
- Multi-spool model: **preserve** multi-spool-per-type grouping from the original Blazor app.
  - There will be a FilamentType concept (brand/material/color/settings) and individual Spool records linked to it.
- Color hex code and filament diameter: **included** in the filament model.
- CSV import: **included** as a one-time migration endpoint (`POST /api/v1/import/csv`) to allow migrating data from the original app.
- Authentication: **deferred** — LAN-only initially, auth added later.

### Hardware
- ESP32-S3 with 16 MB Flash, 8 MB PSRAM.

### Frontend
- Alpine.js for lightweight reactivity (bundled into LittleFS, not loaded from CDN).

### Diameter
- Only 1.75mm is supported. 2.85mm rows are silently skipped during CSV import.
- The meta/options endpoint returns only `[1.75]` for diameters.

### CSV import
- Endpoint: `POST /api/v1/import/csv`
- Handles the exact CSV export format from the original Blazor app.
- Column mapping: Brand→brand, Type→material, Finish→finish, Color Name→color,
  Color Code→color_hex, Total Weight→total_grams, Weight Remaining→remaining_grams,
  Spool Type + Spool Material → spool_type, Location→location, Notes→notes,
  Purchase Price Per Kg→price_per_kg, Date Added→created_at.
- Each CSV row creates one FilamentType (or adds a Spool to an existing match by
  brand+material+color+finish) and one Spool record.
- Rows where Diameter (mm) != 1.75 are skipped.

### Storage layout (extended from port plan)
- `/data/inventory.json` — filament types with embedded spool arrays
- `/data/ams_links.json` — AMS slot assignments to specific spool IDs
- `/data/meta.json` — schema version and counters
- NVS — Wi-Fi, MQTT credentials, device settings

### AMS links reference spool IDs (not filament type IDs)
- Since each physical spool has its own tag_uid, AMS links point to `spool_id`.
- `ams_links[].spool_id → inventory.types[].spools[].id`
