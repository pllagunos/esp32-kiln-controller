## 1. Program storage and compatibility

- [ ] 1.1 Add program catalog and normalization helpers that can enumerate saved programs and read both legacy and canonical JSON shapes
- [ ] 1.2 Implement automatic slug generation, collision-safe naming, and rename-on-save behavior for firing program files
- [ ] 1.3 Add firmware handlers for listing saved program metadata, fetching a saved program by identifier, and creating or updating a program
- [ ] 1.4 Validate incoming program payloads in the save path and recompute or verify derived duration before persisting
- [ ] 1.5 Update the kiln runtime/TFT program-selection flow to resolve saved programs through the catalog instead of numeric filename assumptions

## 2. Web UI program management

- [ ] 2.1 Create a lightweight firing program library page that lists saved programs and exposes New and Edit actions
- [ ] 2.2 Refactor the firing program editor into create and edit modes with canonical program fields and dynamic segment add/remove behavior
- [ ] 2.3 Add client-side duration calculation and a read-only derived duration display in the editor
- [ ] 2.4 Add a lightweight live firing-profile preview using SVG or canvas and refresh it on segment edits
- [ ] 2.5 Add consistent Back and Home controls to the program-management pages and other secondary web UI screens that share the navigation pattern

## 3. Device UI updates and verification

- [ ] 3.1 Update the CONFIG TFT subscreen to display the active station or captive-portal IP address, with an unavailable fallback
- [ ] 3.2 Confirm legacy saved programs still load in the editor and remain usable by the kiln runtime after re-save
- [ ] 3.3 Confirm new-format programs can be created, edited, renamed, reloaded, and executed with correct duration and preview behavior
