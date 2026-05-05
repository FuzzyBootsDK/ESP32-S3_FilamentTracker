# Thoughts

## 2026-04-14 — Data model design for multi-spool grouping

The port plan used a flat one-record-per-spool model. Since the user wants multi-spool-per-type grouping (matching the original app), the cleanest ESP32-friendly approach is:
- `FilamentType` record: brand, material, color, color_hex, finish, vendor, spool_type, diameter, price_per_kg, location, notes, archived
- `Spool` records embedded under each FilamentType in `inventory.json` as a nested array
- AMS links reference spool IDs, which are prefixed `spool_`
- This avoids cross-file joins and keeps inventory.json self-contained and atomic on save

Embedding spools is preferred over a separate spools.json because:
- saves require only one atomic write
- no referential integrity issues across files
- simpler API surface

## Alpine.js bundling
Alpine.js must be served from LittleFS (not CDN) since the device operates LAN-only and may not have internet access. Version should be pinned and minified.

## CSV migration
The original CSV format has one row per "filament entry" with a Quantity field. For migration:
- Each CSV row creates one FilamentType
- Quantity > 1 creates that many Spool records, all starting at total_grams (Weight Remaining is the aggregate)
- Alternatively: each row = one spool within an existing or new type group
- Will need to decide on disambiguation strategy when importing identical brand+material+color rows

## Implementation complete (session 1)
All 40+ files were created in order: build system → models → storage → codec →
services (settings, inventory, ams, mqtt, help) → API (http, ws) → app_main →
data seeds → frontend (css, js, 6 HTML pages).

## AMS service conservatism
`ams_service_type_has_links()` returns false to avoid false-blocking deletes.
This means a user can delete a type that still has AMS links — links become orphaned.
Acceptable for initial release; fix when type_id is stored in ams_link_t.

## LittleFS image deployment
The `ui/` directory must be included in the CMake build as a LittleFS image.
Use `littlefs_create_partition_image(storage ../data FLASH_IN_PROJECT)` in main/CMakeLists.txt, BUT the partition image needs to contain both `data/` JSON seeds and the `ui/` folder merged together under one root. Typically done by staging both into a build directory first.

