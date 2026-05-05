# alpine.min.js — REQUIRED

This file is NOT included in the repository because it is a third-party library.

## How to obtain

Download Alpine.js v3 (minified) and place it here as `alpine.min.js`:

    https://cdn.jsdelivr.net/npm/alpinejs@3.x.x/dist/cdn.min.js

### PowerShell (run from the repo root):

    Invoke-WebRequest `
      -Uri "https://cdn.jsdelivr.net/npm/alpinejs@3.14.9/dist/cdn.min.js" `
      -OutFile "ui/js/alpine.min.js"

### curl / bash:

    curl -L https://cdn.jsdelivr.net/npm/alpinejs@3.14.9/dist/cdn.min.js \
         -o ui/js/alpine.min.js

## Why bundled locally?

The device operates on a LAN with no guaranteed internet access.
Alpine.js must be served from LittleFS so the UI works offline.
