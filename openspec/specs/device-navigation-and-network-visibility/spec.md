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
