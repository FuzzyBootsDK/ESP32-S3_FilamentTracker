"""
PlatformIO pre-build script — download Alpine.js v3 if not already present.

Runs automatically before every build.  Requires internet access on first run.
If the download fails (offline machine), the build continues; you will see a
warning and must download the file manually (see ui/js/ALPINE_DOWNLOAD.md).
"""

Import("env")  # noqa: F821 — injected by PlatformIO

import os
import urllib.request
import urllib.error

ALPINE_VERSION = "3.14.9"
ALPINE_URL = (
    f"https://cdn.jsdelivr.net/npm/alpinejs@{ALPINE_VERSION}/dist/cdn.min.js"
)

project_dir = env.subst("$PROJECT_DIR")  # noqa: F821
dest = os.path.join(project_dir, "ui", "js", "alpine.min.js")

if os.path.isfile(dest):
    print(f"[alpine] alpine.min.js already present — skipping download.")
else:
    print(f"[alpine] alpine.min.js not found — downloading v{ALPINE_VERSION} …")
    try:
        urllib.request.urlretrieve(ALPINE_URL, dest)
        size = os.path.getsize(dest)
        print(f"[alpine] Downloaded successfully ({size:,} bytes).")
    except (urllib.error.URLError, OSError) as exc:
        print(
            f"\n[alpine] WARNING: Could not download Alpine.js: {exc}\n"
            f"         The UI will not work until you place the file at:\n"
            f"           {dest}\n"
            f"         Download URL:\n"
            f"           {ALPINE_URL}\n"
            f"         PowerShell one-liner:\n"
            f'           Invoke-WebRequest -Uri "{ALPINE_URL}" -OutFile "ui/js/alpine.min.js"\n'
        )
