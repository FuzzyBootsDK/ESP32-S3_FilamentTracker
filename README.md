# Filament Tracker — ESP32-S3
### Complete Setup & User Guide

---

## Table of Contents

1. [What Is This Device?](#1-what-is-this-device)
2. [Hardware Requirements](#2-hardware-requirements)
3. [Software & Tools Required](#3-software--tools-required)
4. [Build & Flash the Firmware](#4-build--flash-the-firmware)
5. [First-Boot Wi-Fi Setup](#5-first-boot-wi-fi-setup)
6. [Navigating the Web Interface](#6-navigating-the-web-interface)
7. [Managing Your Filament Inventory](#7-managing-your-filament-inventory)
8. [AMS Linking](#8-ams-linking)
9. [MQTT / Printer Integration](#9-mqtt--printer-integration)
10. [Settings](#10-settings)
11. [Importing Existing Data (CSV)](#11-importing-existing-data-csv)
12. [Over-the-Air Firmware Updates (OTA)](#12-over-the-air-firmware-updates-ota)
13. [Resetting the Device](#13-resetting-the-device)
14. [Themes & Appearance](#14-themes--appearance)
15. [REST API Reference](#15-rest-api-reference)
16. [WebSocket Live Events](#16-websocket-live-events)
17. [File Storage & Data Persistence](#17-file-storage--data-persistence)
18. [Partition Layout](#18-partition-layout)
19. [Troubleshooting](#19-troubleshooting)
20. [Technical Specifications](#20-technical-specifications)

---

## 1. What Is This Device?

The **Filament Tracker** is a self-hosted, network-connected filament inventory manager that runs entirely on an ESP32-S3 microcontroller. It requires no cloud service, no PC running in the background, and no subscription. Once powered on and connected to your home Wi-Fi, it serves a full-featured web application directly from the device itself.

### What it does

- **Tracks every filament spool you own** — brand, material, colour, remaining weight, purchase price, storage location, NFC/RFID tag UID, and more.
- **Groups spools by type** — you define a *filament type* (e.g. "Bambu Lab PLA Black") and attach one or more physical spools to it. Each spool has its own independent weight tracking.
- **Integrates with BambuLab AMS units** — link each AMS slot to a specific spool in your inventory. When your printer is printing, the device can see which AMS slot is active and display real-time printer state.
- **Monitors live printer data over MQTT** — connects to the BambuLab printer's MQTT broker to receive print state, temperatures, job progress, AMS weight reports, and active slot information.
- **Warns about low stock** — a configurable weight threshold triggers visual warnings on the inventory page and the low-stock strip.
- **Serves a responsive web UI** — any browser on your local network can access it. The interface adapts to both desktop and mobile screens.
- **Supports five colour themes** — Dark, Light, Starbucks, Harmony, and Spring.
- **Updates its own firmware over the air** — upload a new `.bin` file directly from the settings page without removing the device from your wall.

---

## 2. Hardware Requirements

| Component | Specification |
|---|---|
| **Microcontroller** | ESP32-S3 |
| **Flash** | 16 MB (SPI NOR Flash) |
| **PSRAM** | 8 MB Octal PSRAM |
| **USB** | USB-C or Micro-USB (for initial flashing) |
| **Power** | 5 V via USB or external regulated supply |

> **Compatible boards (examples):**
> - ESP32-S3 DevKitC-1 (generic)
> - LOLIN S3 (16 MB flash, 8 MB OPI PSRAM)
> - ESP32-S3-BOX
>
> All boards must have 16 MB flash and 8 MB PSRAM. Standard 4 MB boards are **not** compatible — the filesystem partition alone requires the full 16 MB.

---

## 3. Software & Tools Required

### Required (for building from source)

| Tool | Where to get it |
|---|---|
| **Visual Studio Code** | https://code.visualstudio.com |
| **PlatformIO extension** | VS Code Extensions marketplace — search "PlatformIO IDE" |
| **Python 3.8+** | https://www.python.org (required by PlatformIO/esptool) |

PlatformIO automatically downloads and manages the ESP-IDF framework, esptool.py, and all required libraries when you first build the project. No manual SDK installation is needed.

### Required (for use)

| Tool | Notes |
|---|---|
| **A web browser** | Any modern browser: Chrome, Edge, Firefox, Safari |
| **Your Wi-Fi network** | 2.4 GHz network (ESP32 does not support 5 GHz) |

---

## 4. Build & Flash the Firmware

### Step 1 — Open the project

1. Open Visual Studio Code.
2. Select **File → Open Folder** and choose the project folder (the folder containing `platformio.ini`).
3. PlatformIO will detect the project automatically. The first time may take several minutes as it downloads the ESP-IDF framework.

### Step 2 — Connect the ESP32-S3

1. Plug the ESP32-S3 board into your computer via USB.
2. Most boards enter flash mode automatically. If yours does not, hold the **BOOT** button before plugging in, then release it after plugging in.
3. PlatformIO should detect the COM port automatically. If not, check the serial port in your operating system's Device Manager (Windows) or `/dev/tty*` (Linux/macOS).

### Step 3 — Build and upload the firmware

In the VS Code status bar at the bottom, click the **PlatformIO: Upload** button (right-pointing arrow icon), or press `Ctrl+Alt+U`.

This compiles the C firmware and flashes it to the ESP32-S3.

> **Tip:** You can also use the PlatformIO sidebar: click the alien-head icon on the left, then expand "filament_tracker" → "General" → "Upload".

### Step 4 — Upload the filesystem (UI files)

The web interface (HTML, CSS, JS) lives in the `data/` folder and must be flashed separately to the LittleFS filesystem partition.

In the PlatformIO sidebar, expand "filament_tracker" → "Platform" → **"Upload Filesystem Image"**.

> This step is required on every fresh device and every time you change files in `data/` or `ui/`. It is separate from uploading the firmware.

### Step 5 — Open the serial monitor (optional)

Click the **PlatformIO: Monitor** button (plug icon) in the status bar, or press `Ctrl+Alt+M`. Set baud rate to **115200**.

You should see boot messages like:

```
I (xxx) main: Filament Tracker booting…
I (xxx) wifi_mgr: No credentials stored — starting captive portal
I (xxx) wifi_mgr: AP started: SSID=FilamentTracker IP=192.168.4.1
```

---

## 5. First-Boot Wi-Fi Setup

On the very first power-on — or after a Wi-Fi reset — the device has no network credentials. It enters **captive-portal mode**:

### What happens automatically

1. The device broadcasts a Wi-Fi access point named **`FilamentTracker`** (open, no password).
2. A DNS server on the device redirects all domain queries to **192.168.4.1**, so any browser on this temporary network will open the setup page automatically.
3. An HTTP server on port 80 serves the Wi-Fi configuration page.

### Connecting for the first time

1. On your phone or computer, open your Wi-Fi settings.
2. Connect to the network named **`FilamentTracker`**.
3. Your device should automatically open a browser to the setup page (captive portal). If it does not, manually open a browser and navigate to `http://192.168.4.1`.
4. Enter your home Wi-Fi **SSID** (network name) and **password**.
5. Tap **Save & Connect**.
6. The ESP32 saves the credentials to non-volatile storage and **reboots automatically**.
7. After rebooting, the device connects to your home network and is ready to use.

### Finding the device's IP address

After the device connects to your home Wi-Fi, it will log its IP address to the serial monitor:

```
I (xxx) wifi_mgr: Connected!  IP: 192.168.1.xxx
```

If you do not have serial monitor access, check your **router's DHCP client list** — the device will appear with hostname or MAC address matching the ESP32 module.

Once you have the IP address, open a browser and navigate to:

```
http://<device-ip>/
```

Example: `http://192.168.1.105/`

> **Tip:** Assign a static IP or DHCP reservation in your router settings so the address never changes.

---

## 6. Navigating the Web Interface

The interface is a multi-page web application served entirely by the device. Every page shares the same layout:

### Sidebar (left)

| Icon | Page | Purpose |
|---|---|---|
| 📦 | **Inventory** | View, search, filter, and manage all filament types and spools |
| ➕ | **Add Filament** | Form to add a new filament type (and optionally its first spool) |
| 🔗 | **AMS** | Link AMS slots to inventory spools |
| 📡 | **MQTT** | Configure and monitor the MQTT/printer connection |
| ⚙️ | **Settings** | Device settings, Wi-Fi, theme, timezone, and more |
| ❓ | **Help** | Built-in help documentation |

The sidebar also shows:
- **Printer status** — a coloured dot (grey = offline, green = online, animated = printing) with printer state text and a progress bar when printing.
- **WebSocket indicator** — shows "Live" (green dot) when the real-time connection to the device is active, or "Connecting…" when reconnecting.

---

## 7. Managing Your Filament Inventory

### The Inventory page (📦)

The inventory page shows all your filament as a **tile grid**. Each tile represents one **filament type** (a brand + material + colour combination).

#### Tile anatomy

Each tile shows:
- A **colour swatch** filling the tile background (the filament's hex colour).
- **Brand name** in the top-left corner.
- **Material and colour name** centred.
- **Remaining weight** percentage in the bottom-right corner.
- A **left-side status rail** coloured green (healthy), amber (low stock), or red (critical, ≤10%).
- A **stacked shadow** behind tiles that have multiple physical spools.
- A **badge** showing the number of spools if more than one.
- An **AMS badge** if the filament type is currently linked to an AMS slot.

#### KPI bar

At the top of the page:
- **Total Filaments** — total number of active filament types.
- **Low Stock** (amber) — types below the configured threshold.
- **Critical** (red) — types at 10% or less of total capacity.
- **Add Filament** button.

#### Low-stock strip

If any filament is low, an amber warning strip appears listing their names. The "Show Low Only" button toggles the grid to show only low-stock items.

#### Filtering and sorting

Use the controls row to:
- **Search** — free-text search across brand, material, colour, finish, and notes.
- **Filter by material type** — PLA, PETG, ASA, ABS, TPU, NYLON, PA, PC, HIPS, PVA.
- **Sort** — Newest, Oldest, Lowest/Highest Remaining, Name A→Z / Z→A, Colour (darkest/lightest first).
- **Show archived** — toggle between Active only, Active + Archived, or Archived only.

#### Managing a tile

Click on any filament tile to open the **detail/edit view** for that type. From there you can:
- **Edit** the type fields (brand, material, colour, price, location, notes, etc.)
- **Manage spools** — view all physical spools belonging to this type, update their remaining weight, archive them, or delete them.
- **Archive** the entire type (hides it from the main view without deleting data).
- **Delete** the type and all its spools (requires confirmation; blocked if AMS-linked unless "Force delete" is checked).

---

### Adding a new filament type (➕)

Navigate to **Add Filament** and fill in the form.

#### Required fields

| Field | Description |
|---|---|
| **Brand** | Manufacturer name (e.g. "Bambu Lab", "Polymaker") |
| **Material** | Filament type (e.g. "PLA", "PETG", "ASA") |
| **Colour** | Colour name (e.g. "Black", "Galaxy Blue") |

#### Optional fields

| Field | Description |
|---|---|
| **Colour Hex** | HEX colour code for the swatch (e.g. `#1A1A2E`). Use the colour picker. |
| **Finish** | Surface finish (e.g. "Matte", "Silk", "Glitter") |
| **Vendor / Store** | Where you bought it |
| **Spool type** | Plastic, Cardboard, or Refill |
| **Capacity (g)** | Total spool weight in grams (default: 1000 g) |
| **Price / kg (€)** | Cost per kilogram — used for cost tracking |
| **Location** | Physical storage location (e.g. "Shelf A", "Drawer 3") |
| **Notes** | Free-text notes |

#### Adding the first spool

Below the type fields, you can optionally add the first physical spool immediately:
- **Remaining weight (g)** — current weight, in grams.
- **Tag UID** — NFC/RFID tag UID if you tag your spools. Must be unique across all spools.
- **Archived** — mark the spool as archived immediately (unusual for a new spool).

Click **Save** to create the type (and spool if filled in).

---

### Spool management

Each filament type can hold up to **10 physical spools**. To add more spools to an existing type, open the type from the inventory tile, then click **Add Spool** within the spools section.

#### Updating remaining weight

The most common daily action is updating how much filament is left on a spool. Open the type tile → find the spool → edit the **Remaining (g)** field.

If MQTT integration is active, BambuLab AMS units report spool weights automatically — the remaining weight may update without manual input.

---

## 8. AMS Linking

The **AMS page** (🔗) lets you associate each physical AMS slot (e.g. AMS A, Slot 2) with a specific filament entry in your inventory.

### What AMS linking does

- Gives each AMS slot a human-readable label (the filament name) visible in the interface.
- Allows the MQTT service to match incoming AMS weight reports to the correct spool in your inventory and update its remaining weight automatically.
- Protects against accidentally deleting a filament type that is currently loaded in an AMS slot.

### How to create a link

1. Open the **AMS** page.
2. The page shows each detected AMS unit and its slots. If no units appear, your printer is not yet connected via MQTT (see Section 9).
3. Click on an empty slot or the **Edit** button on an existing link.
4. In the modal that appears:
   - **Select the filament** from the dropdown (this lists your inventory entries).
   - Optionally enter a **Tag UID** if the AMS slot reads NFC tags.
   - Check **Enabled** to activate the link.
5. Click **Save Link**.

### Viewing all links

Below the AMS unit visualization, a table shows **All Links** with columns for AMS ID, slot number, the linked spool, filament label, last reported weight, and enabled status.

### Removing a link

Click the **Delete** button (🗑) next to any row in the links table.

### Sync

The **🔄 Sync** button refreshes the AMS view by re-reading all links and their cached data from the device.

---

## 9. MQTT / Printer Integration

The **MQTT page** (📡) connects the Filament Tracker to your BambuLab 3D printer's local MQTT broker.

> BambuLab printers run a local MQTT broker on port 8883 (TLS) or 1883 (plain). The Filament Tracker connects as an MQTT client to receive real-time printer telemetry.

### What is received over MQTT

| Data | Description |
|---|---|
| **Printer state** | idle, printing, paused, error, finish |
| **Job name** | Currently printing file name |
| **Progress** | Percentage complete |
| **Remaining time** | Minutes left in the print job |
| **Bed temperature** | Heated bed temperature (°C) |
| **Nozzle temperature** | Hotend temperature (°C) |
| **Active AMS slot** | Which AMS slot is currently feeding filament |
| **AMS spool weights** | Remaining weight reports from each AMS tray |

### Configuring MQTT

1. Open **Settings** (⚙️) and scroll to the **MQTT** section.
2. Fill in:
   - **Broker host** — your printer's local IP address (e.g. `192.168.1.200`)
   - **Broker port** — `1883` (plain) or `8883` (TLS). BambuLab printers typically use `8883`.
   - **Username** — `bblp` (standard for BambuLab)
   - **Password** — your printer's **access code** (found in the printer's touch screen under Network → Access code)
   - **Client ID** — any unique string (e.g. `filament-tracker-01`)
   - **Topic root** — `device/<printer serial number>/report` (check your printer's documentation)
3. Enable the toggle.
4. Click **Save** then go to the MQTT page and click **Connect**.

### Testing the connection

On the MQTT page, use the **Test Connection** form to verify credentials before saving. Enter the broker details and click **Test** — the result shows whether the broker is reachable and whether authentication succeeded.

### Live state panel

Once connected, the **Live Printer State** card shows all real-time data. The sidebar on every page also shows a live printer status widget.

---

## 10. Settings

The **Settings page** (⚙️) configures all aspects of the device.

### Device section

| Setting | Description |
|---|---|
| **Device name** | Friendly name shown in the UI header (e.g. "My Filament Tracker") |
| **Timezone** | POSIX timezone string used for timestamps. Example: `CET-1CEST,M3.5.0,M10.5.0/3` for Central European Time. Use `UTC` if unsure. |
| **Low stock threshold (g)** | Spools with remaining weight below this value are flagged as low stock (amber). |
| **Page size** | How many items per page in list views (10–100). |

### Appearance section

Choose from five themes. Click any theme card to apply it instantly — no save needed. Themes are:

| Theme | Description |
|---|---|
| **Dark** | Dark background, white text (default) |
| **Light** | White background, dark text |
| **Starbucks** | Forest greens |
| **Harmony** | Navy blue with orange accents |
| **Spring** | Deep teal with warm highlights |

### Printer section

| Setting | Description |
|---|---|
| **Printer name** | Friendly name for your printer |
| **Printer serial** | Serial number (used for MQTT topic construction) |

### MQTT section

See Section 9 for details. All MQTT settings live here.

> The password field is write-only in the UI — it is stored in NVS on the device and never returned by the API.

### Danger zone

| Action | Description |
|---|---|
| **Re-configure Wi-Fi** | Erases stored Wi-Fi credentials and reboots the device into captive-portal / AP mode. Use this if you change your router or Wi-Fi password. |
| **Restart device** | Soft-reboots the ESP32. All settings and inventory are preserved. |

---

## 11. Importing Existing Data (CSV)

If you previously used the original Blazor-based Filament Tracker app, you can import your data using the CSV export from that application.

### How to import

1. Export your data from the original app as a CSV file.
2. Open a browser and send a `POST` request to the import endpoint:

   ```
   POST http://<device-ip>/api/v1/import/csv
   Content-Type: text/csv

   <paste CSV content here>
   ```

   You can also use a tool like **Postman** or **curl**:

   ```bash
   curl -X POST http://192.168.1.105/api/v1/import/csv \
        -H "Content-Type: text/csv" \
        --data-binary @my_filament_export.csv
   ```

3. The device returns a JSON result showing how many types and spools were imported, and how many rows were skipped.

### Import rules

- Only **1.75 mm** filament rows are imported. 2.85 mm rows are silently skipped (the device only supports 1.75 mm).
- Rows that are missing required fields (brand, material, colour) are skipped.
- Duplicate entries are not checked — importing the same file twice will create duplicates.

---

## 12. Over-the-Air Firmware Updates (OTA)

You can update the device firmware without physical access by uploading a new `.bin` firmware file through the browser.

### Building the update binary

In VS Code/PlatformIO, select **Build** (not "Upload"). PlatformIO produces a file at:

```
.pio/build/filament_tracker/firmware.bin
```

### Uploading the update

1. Navigate to **Settings** (⚙️) and scroll to the **Firmware Update** section.
2. Click **Choose file** and select the `firmware.bin` file.
3. Click **Upload Firmware**.
4. A progress indicator appears. The upload streams the binary directly into the inactive OTA partition.
5. When complete, the device automatically reboots into the new firmware.
6. If the new firmware fails to boot, the bootloader automatically rolls back to the previous version.

> **Important:** Uploading a new firmware does **not** erase your inventory data or settings. The filesystem (LittleFS) partition is separate and is unaffected by OTA updates.

> **Do not** upload the filesystem image over OTA — only the firmware `.bin` file. Flashing a new filesystem image requires a wired USB connection and PlatformIO's "Upload Filesystem Image" action.

---

## 13. Resetting the Device

### Soft reset (reboot only)

Go to **Settings** → **Restart device**. All data is preserved.

### Wi-Fi reset (re-configure network)

Go to **Settings** → **Re-configure Wi-Fi**. The device erases stored Wi-Fi credentials and reboots into captive-portal (AP) mode with the SSID `FilamentTracker`. All inventory data and other settings are preserved.

### Factory reset (erase all data)

There is no one-button factory reset in the UI. To completely erase the device:

1. Connect via USB.
2. In PlatformIO terminal, run:
   ```bash
   esptool.py --chip esp32s3 erase_flash
   ```
3. Re-flash both the firmware and the filesystem image.

---

## 14. Themes & Appearance

The theme is stored per-browser in `localStorage` and is applied instantly without a page reload. It is also saved to the device settings so the correct theme loads on any other browser that visits the device.

### Available themes

| Class | Name | Primary colours |
|---|---|---|
| `dark` | Dark | `#0f1117` background, `#e8eaf6` text, `#7c6af5` accent |
| `light` | Light | `#f8f9fb` background, `#1a1a2e` text, `#5c5fef` accent |
| `starbucks` | Starbucks | `#1e3932` background, `#d4e9e2` text, `#cba258` accent |
| `harmony` | Harmony | `#0d1b2a` background, `#e4ecf4` text, `#ff6b35` accent |
| `spring` | Spring | `#0a3d3a` background, `#e8f4f2` text, `#e8a87c` accent |

---

## 15. REST API Reference

The device serves a REST API at `/api/v1/`. All responses are JSON with the envelope:

```json
{ "ok": true, "data": { ... } }
```

or on error:

```json
{ "ok": false, "error": { "code": "error_code", "message": "Human-readable description" } }
```

### Health & System

| Method | Path | Description |
|---|---|---|
| `GET` | `/api/v1/health` | Basic health check: uptime, free heap, MQTT status |
| `GET` | `/api/v1/system/info` | Device name, chip, flash, firmware version, filesystem usage |

### Settings

| Method | Path | Description |
|---|---|---|
| `GET` | `/api/v1/settings` | Get current settings (password is omitted) |
| `PUT` | `/api/v1/settings` | Update all settings |
| `POST` | `/api/v1/settings/theme` | Update theme only: `{ "theme": "dark" }` |
| `POST` | `/api/v1/settings/restart` | Reboot the device |
| `POST` | `/api/v1/settings/wifi/reset` | Erase Wi-Fi credentials and reboot to AP mode |
| `GET` | `/api/v1/settings/export` | Export settings as JSON (excludes password) |

### Inventory — Filament Types

| Method | Path | Description |
|---|---|---|
| `GET` | `/api/v1/inventory` | List types (supports `q`, `material`, `brand`, `color`, `sort`, `dir`, `archived`, `low_stock_only`, `page`, `page_size`) |
| `POST` | `/api/v1/inventory` | Create a new filament type |
| `GET` | `/api/v1/inventory/{id}` | Get full details of one type (includes all spools) |
| `PUT` | `/api/v1/inventory/{id}` | Replace a type's fields (spools unchanged) |
| `DELETE` | `/api/v1/inventory/{id}` | Delete a type. Add `?force=true` to override AMS-link protection. |
| `POST` | `/api/v1/inventory/{id}/archive` | Archive or unarchive: `{ "archived": true }` |
| `GET` | `/api/v1/inventory/options` | Available materials, brands, finishes, and locations |

### Inventory — Spools

| Method | Path | Description |
|---|---|---|
| `GET` | `/api/v1/inventory/{type_id}/spools` | List all spools within a type |
| `POST` | `/api/v1/inventory/{type_id}/spools` | Add a spool to a type |
| `GET` | `/api/v1/inventory/{type_id}/spools/{spool_id}` | Get a single spool |
| `PUT` | `/api/v1/inventory/{type_id}/spools/{spool_id}` | Update spool fields |
| `PATCH` | `/api/v1/inventory/{type_id}/spools/{spool_id}/grams` | Update remaining weight only: `{ "remaining_grams": 450 }` |
| `DELETE` | `/api/v1/inventory/{type_id}/spools/{spool_id}` | Delete a spool |
| `POST` | `/api/v1/inventory/{type_id}/spools/{spool_id}/archive` | Archive/unarchive a spool |

### AMS Links

| Method | Path | Description |
|---|---|---|
| `GET` | `/api/v1/ams` | List AMS units and their linked slots |
| `GET` | `/api/v1/ams/links` | List all raw AMS link records |
| `POST` | `/api/v1/ams/link` | Create a new link: `{ "ams_id": "AMS_A", "slot": 2, "spool_id": "...", "enabled": true }` |
| `PUT` | `/api/v1/ams/link/{id}` | Update a link |
| `DELETE` | `/api/v1/ams/link/{id}` | Remove a link |
| `POST` | `/api/v1/ams/sync` | Trigger a sync between AMS state and inventory weights |

### MQTT

| Method | Path | Description |
|---|---|---|
| `GET` | `/api/v1/mqtt/status` | Connection status (connected, broker host, last error) |
| `GET` | `/api/v1/mqtt/runtime` | Live printer state (temperatures, job name, progress, active AMS slot) |
| `POST` | `/api/v1/mqtt/connect` | Request connection |
| `POST` | `/api/v1/mqtt/disconnect` | Request disconnection |
| `POST` | `/api/v1/mqtt/test` | Test credentials without saving: `{ "broker_host": "192.168.1.x", "broker_port": 1883, "username": "bblp", "password": "..." }` |

### Import

| Method | Path | Description |
|---|---|---|
| `POST` | `/api/v1/import/csv` | Import from original app CSV export. Body: raw CSV text. |

### Backup (partial — see notes)

| Method | Path | Description |
|---|---|---|
| `GET` | `/api/v1/backup/export` | Export backup (currently returns schema envelope; full data export in development) |
| `POST` | `/api/v1/backup/import` | Import backup (not yet implemented — returns 501) |

### OTA Firmware Update

| Method | Path | Description |
|---|---|---|
| `GET` | `/api/v1/ota/status` | Running partition, firmware version, build date |
| `POST` | `/api/v1/ota/upload` | Stream firmware binary. Content-Type: `application/octet-stream`. Device reboots on success. |

### Help

| Method | Path | Description |
|---|---|---|
| `GET` | `/api/v1/help` | List all help sections |
| `GET` | `/api/v1/help/{section_id}` | Get a specific help section's content |

---

## 16. WebSocket Live Events

Connect to `ws://<device-ip>/ws` to receive real-time push events. All messages are JSON:

```json
{ "type": "event.type.name", "data": { ... } }
```

| Event type | Triggered when |
|---|---|
| `inventory.created` | A new filament type is added |
| `inventory.updated` | A type or spool is modified (includes `remaining_grams` and `updated_at`) |
| `inventory.deleted` | A filament type is deleted |
| `ams.updated` | An AMS link is created, updated, or deleted |
| `mqtt.status.updated` | The MQTT connection state changes |
| `mqtt.runtime.updated` | New printer telemetry arrives (temperatures, job state, progress) |
| `settings.updated` | Settings are saved (includes new `theme` if changed) |
| `storage.saved` | A periodic dirty-save to LittleFS completes |

The web UI uses WebSocket events to update the page in real time without polling. The sidebar's printer widget and live stats panels refresh automatically when events arrive.

---

## 17. File Storage & Data Persistence

### Non-Volatile Storage (NVS)

NVS is a key-value store in a dedicated flash partition. The device stores:

- Wi-Fi SSID and password
- All application settings (device name, timezone, MQTT config, theme, thresholds)

NVS data survives firmware updates (OTA). It is erased only by a full `erase_flash` command.

### LittleFS Filesystem

LittleFS is mounted in the `storage` partition (approximately 10 MB). It stores:

- `inventory.json` — all filament types and spools
- `ams_links.json` — all AMS slot-to-spool bindings
- `meta.json` — internal ID counters (for generating sequential IDs)
- All UI files: HTML pages, CSS, JavaScript

File writes use an **atomic write strategy**: data is first written to a `.tmp` file, then renamed over the target. This ensures no half-written or corrupted data file survives a power loss mid-save.

The inventory and AMS stores are held **entirely in RAM** (PSRAM) at runtime. A periodic timer checks every **5 seconds** whether either store has been modified (the "dirty flag") and flushes changes to LittleFS if needed. Changes are also flushed immediately after AMS link operations.

---

## 18. Partition Layout

The 16 MB flash is divided as follows:

| Partition | Type | Size | Description |
|---|---|---|---|
| `nvs` | NVS | 24 KB | Settings and Wi-Fi credentials |
| `phy_init` | PHY | 4 KB | RF calibration data |
| `otadata` | OTA data | 8 KB | Tracks which OTA slot is active |
| `ota_0` | App (OTA 0) | 3 MB | Firmware slot A |
| `ota_1` | App (OTA 1) | 3 MB | Firmware slot B (OTA target) |
| `storage` | LittleFS | ~10 MB | Inventory data + UI files |

The two app partitions (`ota_0` and `ota_1`) enable safe over-the-air updates: the device always boots from one while writing the new firmware to the other. If the new firmware boots successfully, it marks itself as valid. If it fails to boot, the bootloader rolls back to the previous partition.

---

## 19. Troubleshooting

### The device does not appear on my network after setup

- Confirm that your home Wi-Fi is 2.4 GHz. The ESP32-S3 does not support 5 GHz bands.
- Check your router's DHCP client list for a new device.
- Open the serial monitor in PlatformIO and watch the boot messages. The assigned IP address is logged on successful connection.
- If the device keeps retrying Wi-Fi connection and never connects, it will retry every 60 seconds up to 10 times. If credentials are wrong, use the captive portal reset (see Section 13).

### The web page does not load

- Confirm you are on the same Wi-Fi network as the device.
- Confirm the device IP address is correct (check serial monitor or router DHCP list).
- Try adding the trailing slash: `http://192.168.1.105/` not `http://192.168.1.105`.
- If you see a blank page or "Not Found" error, the filesystem image may not have been uploaded. In PlatformIO, run "Upload Filesystem Image" (see Section 4, Step 4).

### The inventory is empty after flashing

The filesystem image (UI files and initial `data/` JSON) must be flashed separately. Run **Upload Filesystem Image** in PlatformIO. This is a separate step from uploading the firmware.

### Printer data is not updating on the MQTT page

- Verify the broker host is the printer's local IP (not a cloud address).
- Verify the access code in the printer's touchscreen matches the password in settings.
- Use the **Test Connection** button on the MQTT page to confirm credentials without saving.
- BambuLab printers must be on the same local network. Check that firewall rules are not blocking port 1883 or 8883.
- The MQTT topic must match the printer's serial number. Format: `device/<serial>/report`.

### Spool weight is not updating automatically from the AMS

- The AMS link must be created on the AMS page and the link must be **Enabled**.
- The MQTT service must be connected.
- AMS weight reports come in the `ams` object inside BambuLab MQTT print messages. These arrive during active print jobs, not necessarily when idle.

### The OTA upload fails or the device does not reboot

- Ensure you selected the `firmware.bin` file (not `bootloader.bin` or `partitions.bin`).
- The binary must be the raw app binary — the file produced by PlatformIO at `.pio/build/filament_tracker/firmware.bin`.
- Firmware images larger than approximately 3 MB will not fit in the OTA partition. If the project has grown, check partition sizes.

### After an OTA update, the device rolls back to the old firmware

The bootloader will roll back if the new firmware does not call the OTA validity confirmation within a few seconds of boot. This is done automatically in `app_main.c`. If rollback keeps happening, the firmware binary may be corrupt or mismatched. Reflash via USB.

### The serial monitor shows garbage characters

Confirm the baud rate is set to **115200** in PlatformIO's monitor settings (`monitor_speed = 115200` in `platformio.ini`).

---

## 20. Technical Specifications

### Hardware

| Spec | Value |
|---|---|
| Processor | ESP32-S3, dual-core Xtensa LX7 @ 240 MHz |
| Flash | 16 MB SPI NOR |
| PSRAM | 8 MB Octal |
| Wi-Fi | 802.11 b/g/n (2.4 GHz only) |
| Power | 5 V USB, ~200–400 mA typical |

### Firmware

| Spec | Value |
|---|---|
| Framework | ESP-IDF (via PlatformIO) |
| Firmware version | 0.1.0 |
| Data schema version | 1 |
| HTTP server | ESP-IDF `esp_http_server` |
| WebSocket | Integrated with `esp_http_server` (CONFIG_HTTPD_WS_SUPPORT) |
| Filesystem | LittleFS |
| JSON library | cJSON |
| MQTT client | `esp-mqtt` (part of ESP-IDF) |
| NTP | `esp_sntp` with configurable POSIX timezone strings |

### Inventory limits

| Limit | Value |
|---|---|
| Maximum filament types | 200 |
| Maximum spools per type | 10 |
| Maximum AMS links | 64 |
| Maximum request body | 64 KB |

### Field length limits

| Field | Maximum length |
|---|---|
| Brand | 32 characters |
| Material | 16 characters |
| Colour name | 32 characters |
| Colour hex | 7 characters (`#RRGGBB`) |
| Finish | 16 characters |
| Vendor | 32 characters |
| Location | 24 characters |
| Notes | 256 characters |
| Tag UID | 24 characters |
| Device name | 32 characters |
| Timezone string | 64 characters |
| MQTT broker host | 64 characters |
| MQTT username | 32 characters |
| MQTT password | 64 characters |

---

*Filament Tracker — ESP32-S3 Edition*
*Open-source. Built with ESP-IDF, LittleFS, cJSON, and Vanilla JS.*
