## Why

The controller currently assumes the ADS1220 thermocouple path at build time and a fixed thermocouple type from `userSetup.h`, which blocks MAX31856-based hardware and forces thermocouple-type changes to require a rebuild. This change adds hardware flexibility and lets operators adjust the configured thermocouple type from the device UI when their installed driver supports it.

## What Changes

- Add build-time thermocouple driver selection in `userSetup.h` so the firmware can be compiled for either ADS1220 or MAX31856 hardware.
- Add MAX31856 initialization, read, fault-handling, and thermocouple-type application alongside the existing ADS1220 path.
- Introduce a shared runtime thermocouple-type setting that can be applied after boot and persisted across restarts.
- Extend the TFT CONFIG flow with a `Change TC Type` entry and a dedicated selection subscreen that uses up/down arrows plus select confirmation.
- Render the thermocouple-type value in orange while the user is actively changing it.
- Preserve existing kiln-control behavior when the selected driver or thermocouple type is unavailable, including clear fault or initialization feedback.

## Capabilities

### New Capabilities
- `thermocouple-driver-and-type-management`: Select the thermocouple driver at build time, support MAX31856 hardware, and allow runtime thermocouple-type selection and persistence through the TFT UI.

### Modified Capabilities
- `device-navigation-and-network-visibility`: Expand the CONFIG TFT flow so operators can enter a dedicated thermocouple-type selection screen from CONFIG in addition to the existing IP-visibility behavior.

## Impact

- `src/userSetup.h` thermocouple configuration constants and driver-selection compile-time switches.
- `src/common.h` and shared runtime state for thermocouple type and initialization or fault status.
- `lib/task/sensor_task.cpp` hardware initialization, reading logic, thermocouple-type application, and error handling for both ADS1220 and MAX31856.
- `lib/gui/gui.cpp` CONFIG submenu structure, thermocouple-type selection screen, button handling, and highlight color behavior.
- Preferences or persistent settings storage used by the TFT flow to remember the selected thermocouple type across restarts.
