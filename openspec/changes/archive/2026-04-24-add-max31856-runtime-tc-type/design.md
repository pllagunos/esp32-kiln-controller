## Context

The current firmware hard-codes a single thermocouple driver path in `sensor_task.cpp`: ADS1220 setup, ADS1220 reads, and ADS1220 lookup-table loading driven by the compile-time `TCTYPE` string in `userSetup.h`. The TFT already has a CONFIG submenu and already persists operator-selected values with `Preferences`, but thermocouple type is not part of that runtime configuration flow.

This change needs to add a second supported driver (MAX31856), preserve the simple compile-time hardware choice in `userSetup.h`, and introduce a runtime thermocouple-type selection flow that works across both supported drivers. The TFT interaction model must stay consistent with the current up/down/select navigation pattern and the existing orange highlight behavior used for actively adjusted values.

Constraints:
- The project is embedded and resource-constrained; the design should extend the existing sensor task and TFT state machine rather than add a large new abstraction layer.
- ADS1220 and MAX31856 support different thermocouple-type application mechanisms: ADS1220 uses SPIFFS CSV lookup tables, while MAX31856 uses hardware enums/register configuration.
- The current system already has known single-user assumptions in the local UI/task model, so the runtime TC-type flow should stay within that model instead of introducing multi-user configuration paths.

## Goals / Non-Goals

**Goals:**
- Allow the firmware to be compiled for either ADS1220 or MAX31856 hardware from `userSetup.h`.
- Provide one runtime thermocouple-type setting that is persisted and applied by whichever configured driver is active.
- Add a TFT CONFIG entry and a dedicated TC-type selection subscreen with up/down/select controls.
- Keep the UI behavior consistent with the existing TFT screens, including orange text while the value is being changed.
- Preserve clear initialization or fault feedback when the selected thermocouple driver or thermocouple type cannot be applied.

**Non-Goals:**
- Adding web-based thermocouple-driver or thermocouple-type configuration.
- Supporting simultaneous dual-driver operation in one firmware image.
- Reworking the full TFT navigation architecture beyond the new CONFIG branch and selection subscreen.
- Solving unrelated display refresh bugs from the reference document unless directly required by the new flow.

## Decisions

### 1. Use compile-time driver selection in `userSetup.h`
The firmware will expose mutually exclusive compile-time driver-selection macros/constants in `userSetup.h` so the build is explicitly targeted to either ADS1220 or MAX31856 hardware.

**Why:** This matches the current project style, keeps the runtime model simple, and avoids dragging unused driver code and hardware initialization into a single dynamically switched image.

**Alternatives considered:**
- **Runtime driver selection:** rejected because the hardware is fixed per device and the added branching, detection logic, and failure modes do not buy much value.
- **Separate forks/files per driver:** rejected because it would duplicate most of the sensor-task flow and drift over time.

### 2. Introduce one shared runtime thermocouple-type state
The runtime-selected thermocouple type will live in shared state (seeded from `userSetup.h` defaults, persisted through `Preferences`, and read by both the GUI and sensor task).

**Why:** The UI needs to display and update the value, while the sensor task needs to apply it to the active driver. Shared state also lets the system reuse the current task-semaphore approach already used for temperatures and faults.

**Alternatives considered:**
- **Leave `TCTYPE` as compile-time-only:** rejected because it does not satisfy runtime reconfiguration.
- **Store the type only inside `Preferences`:** rejected because the sensor task and GUI need a synchronized in-memory value during operation.

### 3. Apply thermocouple-type changes inside the sensor task
The sensor task remains the owner of thermocouple hardware state. It will initialize the active driver at boot, detect runtime TC-type changes, and apply the new type using driver-specific logic:
- MAX31856: map the selected char to `max31856_thermocoupletype_t` and call the hardware setter.
- ADS1220: reload the corresponding lookup table for the selected type and update readiness/fault state if the type is unsupported or the table cannot be loaded.

**Why:** This keeps hardware mutations in one place and avoids letting the UI task manipulate sensor drivers directly.

**Alternatives considered:**
- **Apply type changes directly from the GUI:** rejected because it crosses task boundaries and couples TFT interaction to SPI/SPIFFS operations.
- **Create a new dedicated thermocouple manager task:** rejected as too much complexity for one sensor path.

### 4. Separate “selection in progress” UI state from “applied” driver state
The GUI will track the currently highlighted thermocouple type while the operator is browsing options, and it will only persist/commit when select is pressed. The highlighted text will render in orange while this browsing state is active.

**Why:** This matches the existing manual-temperature-adjustment pattern in `gui.cpp`, where orange indicates an actively changing value.

**Alternatives considered:**
- **Apply on every up/down press:** rejected because it causes repeated hardware churn and noisier error paths while browsing.
- **Use white text only:** rejected because the request explicitly calls for orange while toggling and the project already uses orange for active edits.

### 5. Persist the public TC type independently from driver-specific implementation details
The persisted value will remain a canonical single-character thermocouple-type code (`B`, `E`, `J`, `K`, `N`, `R`, `S`, `T`). Unsupported combinations will be surfaced as initialization/runtime errors instead of silently remapping to another type.

**Why:** The UI and preferences should reflect what the operator selected, not an implementation-specific fallback. This also keeps behavior explicit if ADS1220 only supports a subset of types via lookup tables.

**Alternatives considered:**
- **Silently coerce unsupported values to `K` or the build default:** rejected because it hides operator intent and makes hardware behavior ambiguous.

## Risks / Trade-offs

- **Unsupported TC type for ADS1220 lookup tables** → The UI may allow a selection that the current ADS1220 data files do not support. Mitigation: specify explicit fault/error behavior and mark the driver unready until a supported type is selected.
- **MAX31856 library integration differences** → The Adafruit API and fault semantics may differ from the reference code. Mitigation: keep the spec at behavioral level and call out the mapping/setup work explicitly in tasks.
- **UI-state growth in `gui.cpp`** → Adding another CONFIG subscreen increases the existing string-based screen-state logic. Mitigation: reuse the established `screen == "..."; selection index; readButtons()` pattern instead of inventing a second navigation model.
- **Persistence mismatch on first boot after upgrade** → Existing devices only have compile-time `TCTYPE`. Mitigation: seed preferences from the compile-time default if no runtime value has been stored yet.
- **More initialization/fault paths** → Two drivers plus runtime type changes create more ways to fail. Mitigation: keep the sensor task as the single owner of readiness/error state and specify clear operator-visible error behavior.

## Migration Plan

1. Add compile-time thermocouple-driver selection and runtime TC-type shared state/defaulting.
2. Extend the sensor task with MAX31856 support and driver-specific TC-type application.
3. Add persistence for the runtime TC type so existing devices bootstrap from the compile-time default once and then reuse the stored value.
4. Extend the TFT CONFIG menu with `Change TC Type` and the selection subscreen.
5. Validate both driver builds and confirm the existing ADS1220 path still behaves correctly when runtime TC-type changes are not used.

Rollback is straightforward: rebuild with the previous single-driver ADS1220 path and ignore the runtime preference key. Existing stored TC-type values can be left in preferences because the older firmware does not read them.

## Open Questions

- Whether the project should constrain the TFT selection list to only the types supported by the configured driver, or expose the full thermocouple-type list and surface unsupported selections as runtime errors.
- Whether MAX31856-specific fault details should be surfaced only through the existing generic TFT error flow or also stored for richer diagnostics elsewhere.
