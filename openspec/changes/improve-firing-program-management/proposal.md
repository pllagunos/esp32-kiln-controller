## Why

The kiln controller currently exposes a single manual-entry firing program page, which makes saved-program reuse, editing, and validation cumbersome. This change upgrades the operator experience with a lightweight program library and editor while preserving compatibility with existing program files and adding small navigation/status improvements that reduce friction on both the web UI and device UI.

## What Changes

- Replace manual program-number entry with a lightweight firing program library and editor flow.
- Add firmware-backed list, fetch, and save behavior for firing programs, with optional delete support if it fits the existing handlers cleanly.
- Standardize saved program data around `name`, `created_date`, `duration`, and `segments`, while keeping legacy files readable and converting them when re-saved.
- Generate filesystem-safe program identifiers automatically from program metadata and handle rename collisions safely.
- Add automatic duration calculation, dynamic segment editing, and a live firing-profile preview in the browser without introducing a frontend framework.
- Add consistent back and home navigation controls in the web UI.
- Show the current IP address on the CONFIG TFT subscreen so operators can discover the device address directly from the controller.

## Capabilities

### New Capabilities
- `firing-program-management`: Browse, create, edit, validate, and save firing programs through a lightweight library/editor workflow with legacy-file compatibility.
- `device-navigation-and-network-visibility`: Provide consistent back/home navigation in the UI and expose the active IP address on the CONFIG TFT subscreen.

### Modified Capabilities

None.

## Impact

- Web UI assets for firing program list/editor screens, shared navigation controls, and lightweight preview rendering.
- Firmware routes or handlers responsible for program storage, JSON validation, filename generation, and legacy-program normalization.
- Program files stored in SPIFFS or LittleFS, including rename-on-save behavior and metadata listing.
- TFT configuration-screen rendering for network visibility.
