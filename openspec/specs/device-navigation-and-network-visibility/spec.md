## ADDED Requirements

### Requirement: Secondary web UI pages provide explicit navigation controls
The system SHALL provide Back and Home navigation controls on secondary web UI pages so operators can return to the main dashboard or the previous relevant UI flow without relying on browser chrome.

#### Scenario: User navigates from a secondary page to the dashboard
- **WHEN** the user is on a secondary web UI page and activates Home
- **THEN** the UI returns the user to the main dashboard

#### Scenario: User navigates back from a secondary page
- **WHEN** the user is on a secondary web UI page and activates Back
- **THEN** the UI returns the user to the previous relevant page or flow

### Requirement: CONFIG TFT screen shows the active IP address
The system SHALL display the active device IP address on the CONFIG TFT subscreen so operators can discover how to reach the device from the controller itself.

#### Scenario: Device is connected in station mode
- **WHEN** the device has an active station-mode network connection
- **THEN** the CONFIG TFT subscreen displays the current station IP address

#### Scenario: Captive portal is active
- **WHEN** the captive portal is active
- **THEN** the CONFIG TFT subscreen displays the active AP IP address

#### Scenario: No IP address is available
- **WHEN** the device does not have an active IP address for the current mode
- **THEN** the CONFIG TFT subscreen shows that the IP address is unavailable

### Requirement: CONFIG TFT screen allows thermocouple-type selection
The system SHALL provide a `Change TC Type` option on the TFT CONFIG subscreen and SHALL allow the operator to open a dedicated thermocouple-type selection subscreen from that option.

#### Scenario: Operator opens thermocouple-type selection
- **WHEN** the operator highlights `Change TC Type` on the CONFIG TFT subscreen and presses select
- **THEN** the UI opens a dedicated thermocouple-type selection subscreen

### Requirement: Thermocouple-type selection uses the standard TFT button flow
The thermocouple-type selection subscreen SHALL use the existing up/down/select button interaction pattern to browse available thermocouple types and confirm the selected value.

#### Scenario: Operator browses thermocouple types
- **WHEN** the operator is on the thermocouple-type selection subscreen and presses the up or down buttons
- **THEN** the UI cycles through the available thermocouple-type options one step at a time

#### Scenario: Operator confirms the highlighted thermocouple type
- **WHEN** the operator presses select on the thermocouple-type selection subscreen
- **THEN** the highlighted thermocouple type becomes the selected runtime thermocouple type
- **AND** the UI returns to the appropriate parent CONFIG flow or otherwise indicates the selection was accepted

### Requirement: Active thermocouple-type changes are highlighted in orange
The thermocouple-type value displayed while the operator is actively toggling through options SHALL render in orange on the TFT.

#### Scenario: Operator is actively changing the thermocouple type
- **WHEN** the operator is browsing thermocouple-type values before confirmation
- **THEN** the currently highlighted thermocouple-type text is shown in orange
