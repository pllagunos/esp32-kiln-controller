### Requirement: The firmware supports build-time thermocouple driver selection
The system SHALL allow the firmware build to target either the ADS1220 thermocouple path or the MAX31856 thermocouple path from `userSetup.h`, and the configured thermocouple driver SHALL be the only thermocouple hardware path initialized at runtime.

#### Scenario: Firmware is built for ADS1220
- **WHEN** the build is configured to use ADS1220 as the thermocouple driver
- **THEN** the sensor task initializes and reads temperature through the ADS1220 path
- **AND** the MAX31856 hardware path is not initialized

#### Scenario: Firmware is built for MAX31856
- **WHEN** the build is configured to use MAX31856 as the thermocouple driver
- **THEN** the sensor task initializes and reads temperature through the MAX31856 path
- **AND** the ADS1220 hardware path is not initialized

### Requirement: Thermocouple type is runtime-configurable and persisted
The system SHALL maintain a runtime thermocouple-type setting that is initialized from the configured default when no persisted value exists, SHALL persist operator changes, and SHALL reuse the persisted value after restart.

#### Scenario: Device boots without a stored thermocouple type
- **WHEN** the firmware starts and no thermocouple type has been stored previously
- **THEN** the runtime thermocouple type is seeded from the configured default in `userSetup.h`

#### Scenario: Operator changes thermocouple type
- **WHEN** the operator selects and confirms a new thermocouple type from the TFT flow
- **THEN** the system stores the selected thermocouple type for future boots
- **AND** the new thermocouple type becomes the runtime thermocouple type

#### Scenario: Device restarts after a thermocouple type change
- **WHEN** the firmware starts after a thermocouple type has been stored
- **THEN** the runtime thermocouple type uses the stored value instead of reverting to the compile-time default

### Requirement: The active driver applies runtime thermocouple-type changes
The system SHALL apply the current runtime thermocouple type through the configured driver both during initialization and after a confirmed runtime change.

#### Scenario: MAX31856 applies the selected type
- **WHEN** MAX31856 is the configured driver and the runtime thermocouple type is available
- **THEN** the system configures MAX31856 to use the selected thermocouple type before or during temperature acquisition

#### Scenario: ADS1220 applies the selected type
- **WHEN** ADS1220 is the configured driver and the runtime thermocouple type is available
- **THEN** the system loads and uses the lookup-table data required for the selected thermocouple type before temperature acquisition continues

#### Scenario: Runtime type changes after boot
- **WHEN** the operator confirms a different thermocouple type while the device is running
- **THEN** the sensor task reapplies the thermocouple-type configuration for the active driver
- **AND** subsequent temperature reads use the newly selected type

### Requirement: Unsupported thermocouple-type or driver errors are surfaced clearly
The system SHALL not silently coerce an unsupported thermocouple type to another type, and SHALL provide clear fault or initialization feedback when the configured driver cannot initialize or cannot apply the selected thermocouple type.

#### Scenario: Driver initialization fails
- **WHEN** the configured thermocouple driver cannot be initialized
- **THEN** the system surfaces a clear thermocouple-related error to the operator

#### Scenario: ADS1220 lacks data for the selected type
- **WHEN** ADS1220 is configured and the selected thermocouple type does not have usable lookup-table data
- **THEN** the system reports that the selected thermocouple type cannot be applied
- **AND** the thermocouple path is not treated as successfully initialized

#### Scenario: Operator selects a type that cannot be applied
- **WHEN** the operator confirms a thermocouple type that the configured driver cannot use
- **THEN** the system preserves the selected thermocouple type value
- **AND** the system exposes a fault or error state instead of silently substituting another type
