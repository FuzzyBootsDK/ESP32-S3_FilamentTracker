
# ESP32-S3 Filament Tracker Port Plan (Updated with Complete API Contract)

## Overview
This document expands the original porting plan with practical handling of database-driven patterns for an ESP32-S3 environment, plus a complete API contract for the reduced feature set:

- Inventory
- Add Filament
- MQTT
- Settings
- AMS
- Help

This assumes a rewrite to:
- ESP-IDF firmware on ESP32-S3
- HTML/CSS/JavaScript frontend served from device storage
- REST API for persistent operations
- WebSocket for live state
- NVS for settings
- LittleFS for application data
- RAM as the active runtime data store

---

## Core Architectural Shift

Original server model:

```text
UI -> Service -> EF Core -> SQLite
```

ESP32 model:

```text
Browser UI -> HTTP/WebSocket API -> Firmware Services -> Storage (NVS + LittleFS)
```

The goal is not to port EF Core or the relational database model directly. The goal is to preserve:
- data structures
- business rules
- user workflows
- persistent state
- live printer state

And replace:
- EF Core
- SQLite queries
- server-side Blazor state

with:
- RAM-backed services
- JSON file storage
- NVS settings
- WebSocket live updates

---

## Storage Strategy

## 1. NVS (Key-Value)
Use for:
- Wi-Fi credentials
- MQTT host/port/username/password
- printer identifiers
- app settings
- schema version
- auth password hash or token
- theme preference
- low-stock threshold
- next-id counter

## 2. LittleFS (Filesystem)
Store:
- `/data/inventory.json`
- `/data/ams_links.json`
- `/data/help_content.json` (optional)
- `/data/meta.json`

## 3. RAM (Runtime State)
Keep in memory:
- loaded inventory array
- AMS links array
- live MQTT/printer state
- websocket client state
- indexes for filtering and lookup
- dirty flags for save scheduling

---

## Data Model

## Filament Record

```json
{
  "id": "fil_000123",
  "brand": "Bambu Lab",
  "material": "PLA",
  "color": "Black",
  "finish": "Matte",
  "vendor": "Example Store",
  "spool_type": "Plastic",
  "total_grams": 1000,
  "remaining_grams": 742,
  "price_per_kg": 24.95,
  "location": "Shelf A",
  "tag_uid": "04A1C92B7F",
  "notes": "Primary daily filament",
  "archived": false,
  "created_at": 1776123456,
  "updated_at": 1776124123
}
```

## AMS Link

```json
{
  "id": "ams_000021",
  "ams_id": "AMS_A",
  "slot": 2,
  "filament_id": "fil_000123",
  "tag_uid": "04A1C92B7F",
  "enabled": true,
  "last_sync_weight": 742,
  "last_seen": 1776124123,
  "updated_at": 1776124123
}
```

## Settings Record

```json
{
  "device_name": "filament-tracker-s3",
  "theme": "dark",
  "timezone": "Europe/Copenhagen",
  "low_stock_threshold_grams": 150,
  "mqtt": {
    "enabled": true,
    "broker_host": "192.168.1.50",
    "broker_port": 1883,
    "username": "printer",
    "password": "********",
    "client_id": "filament-tracker-s3",
    "topic_root": "device/3dprinter"
  },
  "printer": {
    "name": "Bambu X1C",
    "serial": "XXXXXXXX",
    "poll_enabled": false
  },
  "ui": {
    "auto_refresh": true,
    "page_size": 25
  },
  "auth": {
    "enabled": true,
    "username": "admin"
  },
  "schema_version": 1
}
```

## Runtime MQTT State

```json
{
  "connected": true,
  "broker_reachable": true,
  "printer_online": true,
  "printer_state": "printing",
  "current_job_name": "gearbox_cover",
  "progress_percent": 46,
  "remaining_minutes": 87,
  "bed_temp_c": 60.0,
  "nozzle_temp_c": 219.5,
  "active_ams_id": "AMS_A",
  "active_slot": 2,
  "updated_at": 1776124123
}
```

---

## Replacing Database Patterns

## Tables -> Arrays
Use arrays of structs or JSON arrays in memory.

## Primary Keys
Use stable string IDs such as:
- `fil_000123`
- `ams_000021`

## Relationships
Use explicit ID references instead of joins.

Example:
- `ams_links[].filament_id -> inventory[].id`

## Queries
Replace SQL queries with:
- in-memory filters
- lightweight sorting
- pagination at API level

## Transactions
Use atomic snapshot saves:
1. write temp file
2. flush/close
3. rename over main file

## Migrations
Use `schema_version` in meta/settings and run upgrade routines at boot.

---

## Persistence Flow

## Boot
1. Initialize NVS
2. Mount LittleFS
3. Load settings
4. Load inventory
5. Load AMS links
6. Build indexes
7. Start HTTP server
8. Start WebSocket
9. Start MQTT client

## Runtime
- update RAM state
- validate all mutations through services
- mark storage dirty
- debounce save operations

## Save
- save after meaningful user changes
- save at periodic intervals if dirty
- do not write every MQTT event to flash

---

## API Design Principles

- Use REST for persistent CRUD operations
- Use WebSocket for live updates
- Keep responses compact
- Support optimistic UI updates
- Version the API
- Return consistent error objects
- Never expose raw internal storage layout directly

Base path:

```text
/api/v1
```

Content type:

```text
application/json
```

Authentication:
- simplest option: session token or basic login
- lightweight option for LAN-only use: optional auth
- recommended: cookie or bearer token after login

---

## Standard Response Envelope

Success:

```json
{
  "ok": true,
  "data": {}
}
```

Error:

```json
{
  "ok": false,
  "error": {
    "code": "validation_error",
    "message": "remaining_grams cannot exceed total_grams",
    "fields": {
      "remaining_grams": "must be <= total_grams"
    }
  }
}
```

Recommended error codes:
- `bad_request`
- `unauthorized`
- `not_found`
- `validation_error`
- `conflict`
- `storage_error`
- `mqtt_error`
- `internal_error`

---

# Complete REST API Contract

## 1. Health and System

### GET `/api/v1/health`
Purpose:
- quick system health check

Response:

```json
{
  "ok": true,
  "data": {
    "status": "ok",
    "uptime_seconds": 15322,
    "heap_free": 184320,
    "wifi_connected": true,
    "mqtt_connected": true,
    "storage_mounted": true,
    "schema_version": 1,
    "firmware_version": "0.1.0"
  }
}
```

### GET `/api/v1/system/info`
Purpose:
- device metadata for Settings and Help pages

Response:

```json
{
  "ok": true,
  "data": {
    "device_name": "filament-tracker-s3",
    "chip_model": "ESP32-S3",
    "flash_mb": 16,
    "psram_mb": 8,
    "firmware_version": "0.1.0",
    "build_date": "2026-04-14",
    "filesystem_total_kb": 4096,
    "filesystem_used_kb": 712
  }
}
```

---

## 2. Authentication

### POST `/api/v1/auth/login`

Request:

```json
{
  "username": "admin",
  "password": "secret"
}
```

Response:

```json
{
  "ok": true,
  "data": {
    "token": "jwt-or-random-token",
    "expires_in": 86400,
    "user": {
      "username": "admin"
    }
  }
}
```

### POST `/api/v1/auth/logout`

Response:

```json
{
  "ok": true,
  "data": {
    "logged_out": true
  }
}
```

### GET `/api/v1/auth/me`

Response:

```json
{
  "ok": true,
  "data": {
    "authenticated": true,
    "user": {
      "username": "admin"
    }
  }
}
```

---

## 3. Inventory

## Inventory List Item Shape

```json
{
  "id": "fil_000123",
  "brand": "Bambu Lab",
  "material": "PLA",
  "color": "Black",
  "finish": "Matte",
  "remaining_grams": 742,
  "total_grams": 1000,
  "price_per_kg": 24.95,
  "location": "Shelf A",
  "tag_uid": "04A1C92B7F",
  "archived": false,
  "ams_linked": true,
  "updated_at": 1776124123
}
```

### GET `/api/v1/inventory`
Purpose:
- list inventory with filtering and paging

Query params:
- `q`
- `material`
- `brand`
- `color`
- `archived`
- `low_stock_only`
- `sort`
- `dir`
- `page`
- `page_size`

Example:
```text
/api/v1/inventory?q=black&material=PLA&low_stock_only=false&page=1&page_size=25&sort=updated_at&dir=desc
```

Response:

```json
{
  "ok": true,
  "data": {
    "items": [
      {
        "id": "fil_000123",
        "brand": "Bambu Lab",
        "material": "PLA",
        "color": "Black",
        "finish": "Matte",
        "remaining_grams": 742,
        "total_grams": 1000,
        "price_per_kg": 24.95,
        "location": "Shelf A",
        "tag_uid": "04A1C92B7F",
        "archived": false,
        "ams_linked": true,
        "updated_at": 1776124123
      }
    ],
    "page": 1,
    "page_size": 25,
    "total_items": 1,
    "total_pages": 1
  }
}
```

### GET `/api/v1/inventory/{id}`
Purpose:
- load one filament record for edit/details

Response:

```json
{
  "ok": true,
  "data": {
    "id": "fil_000123",
    "brand": "Bambu Lab",
    "material": "PLA",
    "color": "Black",
    "finish": "Matte",
    "vendor": "Example Store",
    "spool_type": "Plastic",
    "total_grams": 1000,
    "remaining_grams": 742,
    "price_per_kg": 24.95,
    "location": "Shelf A",
    "tag_uid": "04A1C92B7F",
    "notes": "Primary daily filament",
    "archived": false,
    "created_at": 1776123456,
    "updated_at": 1776124123
  }
}
```

### POST `/api/v1/inventory`
Purpose:
- add filament

Request:

```json
{
  "brand": "Bambu Lab",
  "material": "PLA",
  "color": "Black",
  "finish": "Matte",
  "vendor": "Example Store",
  "spool_type": "Plastic",
  "total_grams": 1000,
  "remaining_grams": 1000,
  "price_per_kg": 24.95,
  "location": "Shelf A",
  "tag_uid": "04A1C92B7F",
  "notes": "Fresh spool"
}
```

Validation rules:
- `brand` required
- `material` required
- `color` required
- `total_grams > 0`
- `remaining_grams >= 0`
- `remaining_grams <= total_grams`
- `price_per_kg >= 0`
- `tag_uid` optional but unique if present

Response:

```json
{
  "ok": true,
  "data": {
    "id": "fil_000124",
    "created": true
  }
}
```

### PUT `/api/v1/inventory/{id}`
Purpose:
- update filament

Request:

```json
{
  "brand": "Bambu Lab",
  "material": "PLA",
  "color": "Black",
  "finish": "Matte",
  "vendor": "Example Store",
  "spool_type": "Cardboard",
  "total_grams": 1000,
  "remaining_grams": 690,
  "price_per_kg": 24.95,
  "location": "AMS A Slot 2",
  "tag_uid": "04A1C92B7F",
  "notes": "Linked to AMS"
}
```

Response:

```json
{
  "ok": true,
  "data": {
    "updated": true,
    "id": "fil_000123",
    "updated_at": 1776124999
  }
}
```

### PATCH `/api/v1/inventory/{id}/grams`
Purpose:
- fast remaining-weight update

Request:

```json
{
  "remaining_grams": 658,
  "source": "mqtt"
}
```

Response:

```json
{
  "ok": true,
  "data": {
    "updated": true,
    "id": "fil_000123",
    "remaining_grams": 658
  }
}
```

### POST `/api/v1/inventory/{id}/archive`
Purpose:
- archive filament instead of deleting

Request:

```json
{
  "archived": true
}
```

Response:

```json
{
  "ok": true,
  "data": {
    "id": "fil_000123",
    "archived": true
  }
}
```

### DELETE `/api/v1/inventory/{id}`
Purpose:
- permanent delete
- only allow if not AMS-linked, or require force flag

Query params:
- `force=true|false`

Response:

```json
{
  "ok": true,
  "data": {
    "deleted": true,
    "id": "fil_000123"
  }
}
```

### GET `/api/v1/inventory/meta/options`
Purpose:
- populate form dropdowns

Response:

```json
{
  "ok": true,
  "data": {
    "materials": ["PLA", "PETG", "ABS", "TPU"],
    "finishes": ["Matte", "Glossy", "Silk", "Carbon"],
    "spool_types": ["Plastic", "Cardboard", "Refill"],
    "locations": ["Shelf A", "Shelf B", "AMS A", "Drawer"]
  }
}
```

---

## 4. AMS

## AMS Link Shape

```json
{
  "id": "ams_000021",
  "ams_id": "AMS_A",
  "slot": 2,
  "filament_id": "fil_000123",
  "filament_label": "Bambu Lab PLA Black",
  "tag_uid": "04A1C92B7F",
  "enabled": true,
  "last_sync_weight": 742,
  "last_seen": 1776124123,
  "printer_reported_color": "Black",
  "printer_reported_material": "PLA"
}
```

### GET `/api/v1/ams`
Purpose:
- list AMS state and links

Response:

```json
{
  "ok": true,
  "data": {
    "units": [
      {
        "ams_id": "AMS_A",
        "slots": [
          {
            "slot": 1,
            "linked": false
          },
          {
            "slot": 2,
            "linked": true,
            "filament_id": "fil_000123",
            "filament_label": "Bambu Lab PLA Black",
            "tag_uid": "04A1C92B7F",
            "enabled": true,
            "last_sync_weight": 742,
            "last_seen": 1776124123
          }
        ]
      }
    ]
  }
}
```

### GET `/api/v1/ams/links`
Purpose:
- flattened AMS link list

Response:

```json
{
  "ok": true,
  "data": {
    "items": [
      {
        "id": "ams_000021",
        "ams_id": "AMS_A",
        "slot": 2,
        "filament_id": "fil_000123",
        "filament_label": "Bambu Lab PLA Black",
        "tag_uid": "04A1C92B7F",
        "enabled": true,
        "last_sync_weight": 742,
        "last_seen": 1776124123
      }
    ]
  }
}
```

### POST `/api/v1/ams/link`
Purpose:
- link a filament to an AMS slot

Request:

```json
{
  "ams_id": "AMS_A",
  "slot": 2,
  "filament_id": "fil_000123",
  "tag_uid": "04A1C92B7F",
  "enabled": true
}
```

Validation rules:
- `ams_id` required
- `slot` required
- `filament_id` must exist
- unique per `ams_id + slot`

Response:

```json
{
  "ok": true,
  "data": {
    "linked": true,
    "id": "ams_000021"
  }
}
```

### PUT `/api/v1/ams/link/{id}`
Purpose:
- update an existing link

Request:

```json
{
  "filament_id": "fil_000130",
  "enabled": true
}
```

Response:

```json
{
  "ok": true,
  "data": {
    "updated": true,
    "id": "ams_000021"
  }
}
```

### DELETE `/api/v1/ams/link/{id}`
Purpose:
- unlink a filament from an AMS slot

Response:

```json
{
  "ok": true,
  "data": {
    "deleted": true,
    "id": "ams_000021"
  }
}
```

### POST `/api/v1/ams/sync`
Purpose:
- force a sync pass between runtime printer data and stored AMS links

Request:

```json
{
  "mode": "reconcile"
}
```

Response:

```json
{
  "ok": true,
  "data": {
    "synced": true,
    "updated_links": 1,
    "timestamp": 1776125000
  }
}
```

---

## 5. MQTT

### GET `/api/v1/mqtt/status`
Purpose:
- current MQTT client and printer status

Response:

```json
{
  "ok": true,
  "data": {
    "enabled": true,
    "connected": true,
    "broker_host": "192.168.1.50",
    "broker_port": 1883,
    "topic_root": "device/3dprinter",
    "printer_online": true,
    "last_message_at": 1776124123,
    "last_error": null
  }
}
```

### POST `/api/v1/mqtt/connect`
Purpose:
- connect using saved settings

Response:

```json
{
  "ok": true,
  "data": {
    "requested": true
  }
}
```

### POST `/api/v1/mqtt/disconnect`
Purpose:
- disconnect client

Response:

```json
{
  "ok": true,
  "data": {
    "requested": true
  }
}
```

### POST `/api/v1/mqtt/test`
Purpose:
- validate broker settings

Request:

```json
{
  "broker_host": "192.168.1.50",
  "broker_port": 1883,
  "username": "printer",
  "password": "secret",
  "topic_root": "device/3dprinter"
}
```

Response:

```json
{
  "ok": true,
  "data": {
    "reachable": true,
    "authenticated": true,
    "subscribed": true
  }
}
```

### GET `/api/v1/mqtt/runtime`
Purpose:
- latest parsed printer state snapshot

Response:

```json
{
  "ok": true,
  "data": {
    "connected": true,
    "broker_reachable": true,
    "printer_online": true,
    "printer_state": "printing",
    "current_job_name": "gearbox_cover",
    "progress_percent": 46,
    "remaining_minutes": 87,
    "bed_temp_c": 60.0,
    "nozzle_temp_c": 219.5,
    "active_ams_id": "AMS_A",
    "active_slot": 2,
    "updated_at": 1776124123
  }
}
```

---

## 6. Settings

### GET `/api/v1/settings`
Purpose:
- load full settings page

Response:

```json
{
  "ok": true,
  "data": {
    "device_name": "filament-tracker-s3",
    "theme": "dark",
    "timezone": "Europe/Copenhagen",
    "low_stock_threshold_grams": 150,
    "mqtt": {
      "enabled": true,
      "broker_host": "192.168.1.50",
      "broker_port": 1883,
      "username": "printer",
      "password_masked": true,
      "client_id": "filament-tracker-s3",
      "topic_root": "device/3dprinter"
    },
    "printer": {
      "name": "Bambu X1C",
      "serial": "XXXXXXXX"
    },
    "ui": {
      "auto_refresh": true,
      "page_size": 25
    },
    "auth": {
      "enabled": true,
      "username": "admin"
    },
    "schema_version": 1
  }
}
```

### PUT `/api/v1/settings`
Purpose:
- replace settings object

Request:

```json
{
  "device_name": "filament-tracker-s3",
  "theme": "dark",
  "timezone": "Europe/Copenhagen",
  "low_stock_threshold_grams": 120,
  "mqtt": {
    "enabled": true,
    "broker_host": "192.168.1.50",
    "broker_port": 1883,
    "username": "printer",
    "password": "secret",
    "client_id": "filament-tracker-s3",
    "topic_root": "device/3dprinter"
  },
  "printer": {
    "name": "Bambu X1C",
    "serial": "XXXXXXXX"
  },
  "ui": {
    "auto_refresh": true,
    "page_size": 25
  },
  "auth": {
    "enabled": true,
    "username": "admin",
    "password": "new-password"
  }
}
```

Response:

```json
{
  "ok": true,
  "data": {
    "updated": true
  }
}
```

### PATCH `/api/v1/settings/theme`

Request:

```json
{
  "theme": "light"
}
```

Response:

```json
{
  "ok": true,
  "data": {
    "theme": "light"
  }
}
```

### POST `/api/v1/settings/restart`
Purpose:
- optional controlled reboot after important config changes

Response:

```json
{
  "ok": true,
  "data": {
    "restarting": true
  }
}
```

### GET `/api/v1/settings/export`
Purpose:
- export sanitized config

Response:

```json
{
  "ok": true,
  "data": {
    "device_name": "filament-tracker-s3",
    "theme": "dark",
    "timezone": "Europe/Copenhagen",
    "low_stock_threshold_grams": 150,
    "mqtt": {
      "enabled": true,
      "broker_host": "192.168.1.50",
      "broker_port": 1883,
      "username": "printer",
      "client_id": "filament-tracker-s3",
      "topic_root": "device/3dprinter"
    }
  }
}
```

---

## 7. Help

### GET `/api/v1/help`
Purpose:
- load Help page sections

Response:

```json
{
  "ok": true,
  "data": {
    "sections": [
      {
        "id": "getting-started",
        "title": "Getting Started",
        "content_html": "<p>Connect Wi-Fi, then configure MQTT.</p>"
      },
      {
        "id": "ams-linking",
        "title": "AMS Linking",
        "content_html": "<p>Open AMS page and assign a filament to each slot.</p>"
      }
    ]
  }
}
```

### GET `/api/v1/help/{section_id}`
Purpose:
- load one help section

Response:

```json
{
  "ok": true,
  "data": {
    "id": "getting-started",
    "title": "Getting Started",
    "content_html": "<p>Connect Wi-Fi, then configure MQTT.</p>"
  }
}
```

---

## 8. Backup and Import

### GET `/api/v1/backup/export`
Purpose:
- export the complete persistent dataset

Response:

```json
{
  "ok": true,
  "data": {
    "schema_version": 1,
    "settings": {},
    "inventory": [],
    "ams_links": [],
    "exported_at": 1776125123
  }
}
```

### POST `/api/v1/backup/import`
Purpose:
- import a full dataset

Request:

```json
{
  "schema_version": 1,
  "settings": {},
  "inventory": [],
  "ams_links": []
}
```

Response:

```json
{
  "ok": true,
  "data": {
    "imported": true,
    "inventory_count": 12,
    "ams_link_count": 4
  }
}
```

Validation rules:
- reject unsupported schema versions unless migration exists
- reject duplicate IDs
- reject broken AMS references
- reject invalid grams relationships

---

# WebSocket Contract

Endpoint:

```text
/ws
```

Purpose:
- push live printer state
- push MQTT status
- push inventory changes
- push AMS link changes
- avoid aggressive polling

## Message Envelope

```json
{
  "type": "mqtt.runtime.updated",
  "timestamp": 1776125123,
  "data": {}
}
```

## Server -> Client Events

### `system.hello`
Sent after socket opens

```json
{
  "type": "system.hello",
  "timestamp": 1776125123,
  "data": {
    "device_name": "filament-tracker-s3",
    "firmware_version": "0.1.0"
  }
}
```

### `mqtt.status.updated`

```json
{
  "type": "mqtt.status.updated",
  "timestamp": 1776125123,
  "data": {
    "connected": true,
    "printer_online": true,
    "last_message_at": 1776125120
  }
}
```

### `mqtt.runtime.updated`

```json
{
  "type": "mqtt.runtime.updated",
  "timestamp": 1776125123,
  "data": {
    "printer_state": "printing",
    "progress_percent": 46,
    "remaining_minutes": 87,
    "bed_temp_c": 60.0,
    "nozzle_temp_c": 219.5,
    "active_ams_id": "AMS_A",
    "active_slot": 2
  }
}
```

### `inventory.created`

```json
{
  "type": "inventory.created",
  "timestamp": 1776125123,
  "data": {
    "id": "fil_000124"
  }
}
```

### `inventory.updated`

```json
{
  "type": "inventory.updated",
  "timestamp": 1776125123,
  "data": {
    "id": "fil_000123",
    "remaining_grams": 658,
    "updated_at": 1776125123
  }
}
```

### `inventory.deleted`

```json
{
  "type": "inventory.deleted",
  "timestamp": 1776125123,
  "data": {
    "id": "fil_000123"
  }
}
```

### `ams.updated`

```json
{
  "type": "ams.updated",
  "timestamp": 1776125123,
  "data": {
    "ams_id": "AMS_A",
    "slot": 2
  }
}
```

### `settings.updated`

```json
{
  "type": "settings.updated",
  "timestamp": 1776125123,
  "data": {
    "theme": "light"
  }
}
```

### `storage.save.completed`

```json
{
  "type": "storage.save.completed",
  "timestamp": 1776125123,
  "data": {
    "dirty": false
  }
}
```

## Client -> Server Events (Optional)
You can keep the socket server-only, but if bidirectional control is wanted:

### `ping`

```json
{
  "type": "ping",
  "timestamp": 1776125123,
  "data": {}
}
```

### `subscribe`

```json
{
  "type": "subscribe",
  "timestamp": 1776125123,
  "data": {
    "topics": ["mqtt", "inventory", "ams"]
  }
}
```

---

# JSON Schema Guidance

These are simplified developer-oriented schemas, not full formal JSON Schema documents.

## Inventory Create Schema

```json
{
  "brand": "string, required, max 32",
  "material": "string, required, max 16",
  "color": "string, required, max 32",
  "finish": "string, optional, max 16",
  "vendor": "string, optional, max 32",
  "spool_type": "string, optional, max 16",
  "total_grams": "integer, required, > 0",
  "remaining_grams": "integer, required, >= 0, <= total_grams",
  "price_per_kg": "number, required, >= 0",
  "location": "string, optional, max 24",
  "tag_uid": "string, optional, unique, max 24",
  "notes": "string, optional, max 256"
}
```

## AMS Link Schema

```json
{
  "ams_id": "string, required, max 16",
  "slot": "integer, required, 1-4 or target printer-supported range",
  "filament_id": "string, required, must exist",
  "tag_uid": "string, optional",
  "enabled": "boolean, required"
}
```

## Settings Schema

```json
{
  "device_name": "string, required, max 32",
  "theme": "enum: dark | light | auto",
  "timezone": "string, required, max 64",
  "low_stock_threshold_grams": "integer, >= 0",
  "mqtt": {
    "enabled": "boolean",
    "broker_host": "string, required if enabled",
    "broker_port": "integer, 1-65535",
    "username": "string, optional",
    "password": "string, optional",
    "client_id": "string, max 32",
    "topic_root": "string, max 64"
  },
  "printer": {
    "name": "string, max 32",
    "serial": "string, max 32"
  },
  "ui": {
    "auto_refresh": "boolean",
    "page_size": "integer, 10-100"
  },
  "auth": {
    "enabled": "boolean",
    "username": "string, max 32",
    "password": "string, optional for update"
  }
}
```

---

# Frontend Page Mapping

## Inventory Page
Uses:
- `GET /api/v1/inventory`
- `GET /api/v1/inventory/meta/options`
- `DELETE /api/v1/inventory/{id}`
- WebSocket `inventory.updated`

## Add Filament Page
Uses:
- `POST /api/v1/inventory`
- `GET /api/v1/inventory/meta/options`

## Edit Filament Modal/Page
Uses:
- `GET /api/v1/inventory/{id}`
- `PUT /api/v1/inventory/{id}`
- `PATCH /api/v1/inventory/{id}/grams`

## MQTT Page
Uses:
- `GET /api/v1/mqtt/status`
- `GET /api/v1/mqtt/runtime`
- `POST /api/v1/mqtt/connect`
- `POST /api/v1/mqtt/disconnect`
- `POST /api/v1/mqtt/test`
- WebSocket `mqtt.status.updated`
- WebSocket `mqtt.runtime.updated`

## Settings Page
Uses:
- `GET /api/v1/settings`
- `PUT /api/v1/settings`
- `PATCH /api/v1/settings/theme`
- `POST /api/v1/settings/restart`

## AMS Page
Uses:
- `GET /api/v1/ams`
- `GET /api/v1/ams/links`
- `POST /api/v1/ams/link`
- `PUT /api/v1/ams/link/{id}`
- `DELETE /api/v1/ams/link/{id}`
- `POST /api/v1/ams/sync`
- WebSocket `ams.updated`

## Help Page
Uses:
- `GET /api/v1/help`
- `GET /api/v1/help/{section_id}`

---

# Service Layer Recommendation

Keep storage logic out of route handlers.

Use these firmware services:
- `inventory_service`
- `ams_service`
- `mqtt_service`
- `settings_service`
- `help_service`
- `storage_service`
- `auth_service`

Example flow for `POST /api/v1/inventory`:
1. API parses JSON
2. API calls `inventory_service_create()`
3. service validates payload
4. service generates ID and timestamps
5. service adds record to RAM
6. service marks inventory dirty
7. API returns success
8. background save writes snapshot
9. WebSocket broadcasts `inventory.created`

---

# Save Strategy

Use a debounced save strategy:
- save within 2 to 5 seconds after user changes
- save immediately for high-risk actions like import
- do not save every incoming MQTT packet
- only persist meaningful filament weight changes

Recommended rules:
- persist filament weight only if change >= 5g or print finished
- persist AMS link changes immediately
- persist settings immediately
- persist inventory create/update/delete on debounce timer

---

# Suggested File Layout

```text
/data
  inventory.json
  ams_links.json
  meta.json

/main
  app_main.c
  api_http.c
  api_ws.c
  auth_service.c
  inventory_service.c
  ams_service.c
  mqtt_service.c
  settings_service.c
  help_service.c
  storage_fs.c
  storage_nvs.c
  json_codec.c
  model_filament.h
  model_ams.h
  model_settings.h
  model_runtime.h

/ui
  index.html
  inventory.html
  add-filament.html
  mqtt.html
  settings.html
  ams.html
  help.html
  css/
  js/
```

---

# Implementation Order

1. Set up ESP-IDF project and partition table
2. Mount LittleFS and initialize NVS
3. Define C structs for inventory, AMS, settings, runtime
4. Implement JSON encode/decode
5. Implement storage read/write with temp-file swap
6. Implement settings service
7. Implement inventory service
8. Implement AMS service
9. Implement MQTT service
10. Implement HTTP routing
11. Implement WebSocket event broadcasting
12. Implement auth
13. Implement frontend pages
14. Add import/export
15. Add schema version migration hooks
16. Optimize RAM usage and response sizes

---

# Final Recommendation

For your reduced scope, the database-driven nature of the original application is still fully manageable on an ESP32-S3, as long as you handle it this way:

- keep the data model
- keep the services
- replace the relational DB with RAM + JSON + NVS
- use REST for persistence
- use WebSocket for live state
- keep save logic controlled and atomic

That gives you a practical architecture that still feels very close to the original app from a user perspective, while being realistic for the ESP32-S3.
