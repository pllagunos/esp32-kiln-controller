## ADDED Requirements

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
