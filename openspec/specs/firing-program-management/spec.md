## ADDED Requirements

### Requirement: Users can browse saved firing programs
The system SHALL provide a lightweight firing program library page that lists saved firing programs and exposes actions to create a new program or edit an existing program.

#### Scenario: Program library shows saved programs
- **WHEN** the user opens the firing program library and saved programs exist
- **THEN** the page lists each saved program with enough metadata to distinguish entries
- **AND** each listed program exposes an edit action
- **AND** the page exposes an action to create a new program

#### Scenario: Program library handles an empty catalog
- **WHEN** the user opens the firing program library and no saved programs exist
- **THEN** the page shows an empty state
- **AND** the page still exposes an action to create a new program

### Requirement: Users can create and edit firing programs
The system SHALL provide a firing program editor that supports create mode and edit mode, exposes only program name, created date, and segment rows, and allows users to add or remove segments before saving.

#### Scenario: User creates a new program
- **WHEN** the user opens the editor in create mode
- **THEN** the editor shows blank program metadata fields and at least one editable segment row
- **AND** the user can add or remove segment rows before saving

#### Scenario: User edits an existing program
- **WHEN** the user opens the editor for an existing saved program
- **THEN** the editor loads the saved program metadata and segments
- **AND** the user can modify metadata and segments and save the updated program

### Requirement: The editor derives duration and profile preview automatically
The system SHALL display duration as a derived read-only value and SHALL recalculate duration and preview data whenever the program definition changes.

Ramp duration MUST be computed as `abs(target_temperature - previous_target_temperature) / firing_rate * 60` when `firing_rate` is greater than zero, using an ambient start temperature of 20 C unless the system already has a better configured ambient source. Total duration MUST equal the sum of all ramp minutes and hold minutes.

#### Scenario: Editing a segment updates duration and preview
- **WHEN** the user changes a segment target temperature, firing rate, or holding time
- **THEN** the editor recalculates the derived duration immediately
- **AND** the preview graph redraws from the updated ramp and hold data

#### Scenario: Duration is not manually editable
- **WHEN** the user views the program editor
- **THEN** duration is presented as a derived field rather than a manually entered value

### Requirement: Firmware exposes program listing, retrieval, and save behavior
The firmware SHALL expose handlers that allow the UI to list saved program metadata, fetch one full program by identifier, and create or update a program definition.

#### Scenario: UI requests saved program metadata
- **WHEN** the library page requests the saved program list
- **THEN** the firmware returns metadata for each saved program that is sufficient to render the library and target a specific program for editing

#### Scenario: UI requests a specific saved program
- **WHEN** the editor requests a saved program by identifier
- **THEN** the firmware returns the full normalized program definition for that program

#### Scenario: UI saves a program
- **WHEN** the editor submits a valid program definition
- **THEN** the firmware validates the payload
- **AND** the firmware persists the program
- **AND** the firmware returns the final saved identifier for the program

### Requirement: Saved programs use the canonical schema and automatic identifiers
The system SHALL persist new and updated programs using the canonical schema fields `name`, `created_date`, `duration`, and `segments`. Each saved segment SHALL use `target_temperature`, `firing_rate`, and `holding_time`.

The system SHALL generate a filesystem-safe identifier from program name and created date, SHALL avoid identifier collisions by applying a deterministic numeric suffix, and SHALL update the backing identifier safely when a saved program is renamed.

#### Scenario: Saving a new program creates a canonical file
- **WHEN** the user saves a new program
- **THEN** the stored program uses the canonical schema
- **AND** the stored file does not include `program_number`
- **AND** the persisted identifier is generated automatically without requiring manual numbering

#### Scenario: Saving colliding metadata produces a unique identifier
- **WHEN** two programs would resolve to the same generated identifier
- **THEN** the later save receives a deterministic collision-safe suffix
- **AND** both programs remain addressable independently

#### Scenario: Renaming an existing program updates its identifier safely
- **WHEN** the user changes a saved program's name or created date and saves it
- **THEN** the system updates the backing identifier without leaving the program in an ambiguous state

### Requirement: Legacy program files remain interoperable
The system SHALL load legacy saved program files that contain legacy field names or numeric-file conventions, and SHALL normalize them when they are re-saved.

#### Scenario: Legacy program loads in the editor
- **WHEN** the user opens a legacy saved program that includes fields such as `createdDate`, camelCase segment fields, or `program_number`/`programNumber`
- **THEN** the editor loads the program without failing
- **AND** the returned program data is normalized to the canonical field names

#### Scenario: Re-saving a legacy program converts it to the new format
- **WHEN** the user saves changes to a legacy program
- **THEN** the saved result uses the canonical schema and automatic identifier rules
- **AND** the saved result no longer includes `program_number`

### Requirement: Saved programs remain usable by the kiln runtime
The system SHALL preserve the kiln controller's ability to select and load saved firing programs after the storage migration.

#### Scenario: Runtime loads a migrated program
- **WHEN** an operator selects a saved firing program after the new storage flow is deployed
- **THEN** the kiln runtime resolves the selection to the correct saved program
- **AND** the program loads with the same segment semantics needed for firing execution
