## 1. Driver selection and shared thermocouple state

- [x] 1.1 Add build-time thermocouple-driver selection in `src/userSetup.h` for ADS1220 vs MAX31856, including any driver-specific pin/configuration constants that must stay aligned with existing hardware settings
- [x] 1.2 Add shared runtime thermocouple-type state and persistence bootstrapping so the firmware starts from the stored type when present and otherwise seeds from the configured default

## 2. Sensor task thermocouple support

- [x] 2.1 Integrate MAX31856 initialization and temperature-reading support into `lib/task/sensor_task.cpp` while preserving the existing ADS1220 path
- [x] 2.2 Implement driver-specific runtime thermocouple-type application logic for both ADS1220 and MAX31856, including explicit error handling for unsupported or failed type application
- [x] 2.3 Ensure thermocouple driver initialization and runtime type changes surface clear fault or initialization feedback instead of silently coercing to another type

## 3. TFT CONFIG thermocouple-type flow

- [x] 3.1 Extend the CONFIG TFT submenu in `lib/gui/gui.cpp` with a `Change TC Type` option and adjust selection bounds/navigation accordingly
- [x] 3.2 Add a dedicated thermocouple-type selection subscreen that uses the existing up/down/select interaction model to browse and confirm available thermocouple types
- [x] 3.3 Render the actively toggled thermocouple-type value in orange and persist the confirmed selection through the existing `Preferences` flow

## 4. Verification

- [x] 4.1 Verify both build configurations still compile cleanly and the existing ADS1220 path remains functional after introducing MAX31856 support
- [x] 4.2 Verify runtime thermocouple-type changes are applied after confirmation, survive restart, and surface clear errors when the configured driver cannot use the selected type
- [x] 4.3 Verify the TFT CONFIG flow enters and exits the new thermocouple-type screen correctly and shows the orange active-selection state while browsing
