# ESP32-S3 Filament Tracker

Self-hosted filament inventory and printer status tracker running on ESP32-S3.

## What it does
- Tracks filament types and spool usage.
- Shows live printer status from MQTT (state, progress, ETA, temperatures).
- Displays live AMS slot material/color metadata from MQTT on the AMS page.
- Works fully on your local network with a web UI served from the device.

## Hardware requirements
- ESP32-S3 board
- 16 MB flash
- 8 MB PSRAM

## Project layout
- Firmware: main/
- Web UI: ui/
- Static data files: data/
- Build config: platformio.ini, sdkconfig.defaults, sdkconfig.filament_tracker

## Build and flash
From VS Code + PlatformIO:
1. Upload firmware
2. Upload filesystem image

Or with CLI:
```bash
pio run -e filament_tracker -t upload
pio run -e filament_tracker -t uploadfs
```

Note: Firmware changes require firmware upload. HTML/CSS/JS changes require filesystem upload.

## First run
If Wi-Fi credentials are missing, device starts AP/captive portal mode.
- SSID: FilamentTracker
- Configure your Wi-Fi, then device reboots.

## MQTT setup
Configure MQTT from the Settings page, then monitor data in Live View.

Flow:
1. Open Settings and configure MQTT broker, credentials, and topic root.
2. Save settings (this applies MQTT settings immediately).
3. Open Live View to monitor printer state and incoming MQTT messages.

### Using a relay (recommended)
If your printer only allows one subscriber, run a relay and connect ESP32 to the relay.

Typical relay settings:
- Broker host: relay IP (for example 192.168.10.180)
- Port: relay MQTT port (for example 1883)
- No authentication (relay mode): enabled (if relay has no auth)
- Topic root: device/<printer-serial>

### Using printer directly
- Broker host: printer IP
- Port: 8883 (usually)
- Username: bblp
- Password: printer access code
- Topic root: device/<printer-serial>

## AMS behavior (current)
- AMS is now MQTT-driven.
- AMS linking workflow was removed from UI.
- AMS page shows:
  - No AMS until first AMS MQTT message arrives.
  - Last known AMS slot data until a newer AMS message changes it.
- Unknown slot metadata fallback:
  - Text: Unknown color • Unknown brand
  - Swatch: white with diagonal red stripe

## Printer status behavior
- Sidebar shows printer state plus progress/ETA.
- Runtime can reconstruct printing state from partial telemetry if start message was missed.
- MQTT fragmented payloads are reassembled before JSON parsing.
- Live View includes a terminal-style incoming MQTT message pane (last 10 messages).

## API overview
Base path: /api/v1

Common endpoints:
- GET /mqtt/status
- GET /mqtt/runtime
- POST /mqtt/connect
- POST /mqtt/disconnect
- POST /mqtt/test (heuristic only)
- PUT /settings (primary way to update MQTT settings)
- GET /inventory
- POST /inventory
- PATCH /inventory/{id}/grams
- GET /settings
- PUT /settings

## Troubleshooting

### UI appears unstyled
- Re-upload filesystem image.
- Hard refresh browser (Ctrl/Cmd+Shift+R).

### MQTT connected but no live updates
- Verify topic root matches printer serial: device/<serial>
- Verify relay forwards device/<serial>/report payloads.
- Check monitor logs for MQTT_EVENT_DATA processing.

### MQTT connect failures
- Connection reset by peer:
  - Usually wrong endpoint/protocol (ws vs tcp) or relay listener/path mismatch.
- Cannot create socket:
  - Check socket pressure and network state.

### HTTP recv error 104 in monitor
Usually benign client disconnect/reset noise from browser refresh/reconnect.

## Development notes
- Keep sdkconfig.defaults as source of truth for defaults.
- Current config enables MQTT websocket transport and SSL transport.
- Current runtime model includes AMS slot snapshot fields for live UI rendering.

## License
Set your preferred license for this repository.
