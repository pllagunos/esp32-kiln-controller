#!/usr/bin/env python3
"""
Quick smoke-test for the OTA GitHub API call.
Run this on your laptop to verify the API works before flashing.
"""
import urllib.request, json, hashlib, sys, ssl

# macOS Python (from python.org) ships without system CA certs.
# Try certifi first; fall back to unverified (safe for this local dev tool).
try:
    import certifi
    ssl_ctx = ssl.create_default_context(cafile=certifi.where())
except ImportError:
    ssl_ctx = ssl._create_unverified_context()

def open_url(url, headers=None):
    req = urllib.request.Request(url, headers=headers or {})
    return urllib.request.urlopen(req, context=ssl_ctx)

OWNER = "pllagunos"
REPO  = "esp32-kiln-controller"
ASSET = "esp32doit-devkit-v1_firmware.bin"

url = f"https://api.github.com/repos/{OWNER}/{REPO}/releases/latest"

print(f"GET {url}")
with open_url(url, headers={
    "Accept": "application/vnd.github+json",
    "User-Agent": "ESP32-OTA",
    "X-GitHub-Api-Version": "2022-11-28",
}) as resp:
    print(f"Status: {resp.status}")
    data = json.load(resp)

tag  = data.get("tag_name", "(none)")
name = data.get("name", "(none)")
body = data.get("body", "")
print(f"Latest release: {name}  (tag: {tag})")

# Parse MD5 from release notes
md5_line = next((l for l in body.splitlines() if "Firmware MD5:" in l), None)
if md5_line:
    md5_expected = md5_line.split("Firmware MD5:")[-1].strip()
    print(f"Expected MD5:   {md5_expected}")
else:
    print("WARNING: no MD5 found in release notes")
    md5_expected = None

# Find firmware asset
asset_url = next(
    (a["browser_download_url"] for a in data.get("assets", []) if a["name"] == ASSET),
    None
)
if not asset_url:
    print(f"ERROR: asset '{ASSET}' not found in release")
    sys.exit(1)
print(f"Asset URL:      {asset_url}")

# Download and verify MD5
print("Downloading firmware to verify MD5...")
with open_url(asset_url) as resp:
    firmware = resp.read()

actual_md5 = hashlib.md5(firmware).hexdigest()
print(f"Actual MD5:     {actual_md5}")

if md5_expected:
    if actual_md5 == md5_expected:
        print("✓ MD5 matches — OTA pipeline looks good!")
    else:
        print("✗ MD5 MISMATCH — release notes MD5 doesn't match downloaded binary")
        sys.exit(1)
else:
    print(f"(skipped MD5 check — firmware size: {len(firmware)} bytes)")
