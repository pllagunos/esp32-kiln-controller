
**Implementation Plan**

The goal is to upgrade the ESP32 kiln controller’s firing program workflow from a single manual-entry form into a lightweight program library plus editor, while keeping the implementation simple enough for plain HTML, CSS, JS, and filesystem-backed firmware handlers.

1. Discovery and compatibility
   - Locate the current firing program HTML, JS, CSS, and the firmware handler that writes JSON into SPIFFS or LittleFS.
   - Confirm the current program JSON schema, storage directory, filename pattern, and any existing endpoints used by the page.
   - Identify whether existing saved files contain legacy fields such as program_number and whether those files must remain readable.

2. Data model cleanup
   - Standardize new and edited programs to this schema: name, created_date, duration, segments.
   - Standardize each segment to: target_temperature, firing_rate, holding_time.
   - Remove program_number from the UI and from newly saved JSON.
   - Keep legacy compatibility by ignoring program_number if present in old files and removing it when an old file is re-saved.

3. Program storage and naming
   - Replace manual program numbering with automatic filename generation.
   - Generate a slug from program name and created_date.
   - Ensure filenames are filesystem-safe and collision-safe by adding a numeric suffix when necessary.
   - Support editing existing programs so that if name or created_date changes, the backing filename can be updated cleanly.

4. Firmware API additions
   - Add a list endpoint that returns saved program metadata.
   - Add a get endpoint that returns one full program by id or slug.
   - Add a create or update endpoint that saves a program and returns its final id.
   - Optionally add a delete endpoint if straightforward.
   - Make the save handler validate payload shape and compute or verify duration before writing.

5. Frontend structure
   - Split the current single page into two simple views:
   - A program library page listing saved programs with actions for New and Edit.
   - A program editor page for creating and editing programs.
   - Keep the implementation framework-free and lightweight.

6. Editor behavior
   - The editor should expose only name, created date, and segment rows.
   - Each segment row should include target temperature, firing rate, and hold time.
   - Users should be able to add and remove segments dynamically in the browser before saving.
   - The editor should support create mode and edit mode using a query parameter or route segment.

7. Automatic calculations
   - Recalculate ramp time and total duration whenever a segment changes.
   - Use this logic:
   - Ramp minutes = absolute difference between current target and previous target divided by firing rate times 60, only when firing rate is greater than zero.
   - Total duration = sum of all ramp minutes plus all hold minutes.
   - Duration should be displayed as a derived field, not manually entered.

8. Graph preview
   - Add a live preview graph to the editor.
   - Use a small SVG or canvas implementation instead of a heavy charting library.
   - Compute graph points from ramp and hold segments using an ambient start temperature of 20 °C unless the firmware already has a better source for ambient.
   - Redraw on every segment edit.

9. Migration and backward compatibility
   - Ensure old saved programs can still be loaded.
   - When an old program is edited and saved, convert it to the new schema and new filename pattern.
   - Do not break existing kiln start logic that consumes program files.

10. Validation and finish
   - Test create, edit, reload, and save flows.
   - Verify generated filenames are unique and stable.
   - Verify duration and preview update correctly as segments are edited.
   - Verify saved JSON is valid and still usable by the firmware runtime.

**Agent Checklist**

Use this as the execution checklist.

1. Find the current program editor page assets and firmware save handler.
2. Document the current JSON shape and storage path.
3. Remove program_number from the UI.
4. Make program filenames auto-generated from name plus date.
5. Add collision-safe slug generation.
6. Add a program list endpoint.
7. Add a single-program fetch endpoint.
8. Add a create or update save endpoint.
9. Optionally add delete if low effort.
10. Build a lightweight program library page with New and Edit actions.
11. Build or refactor a plain JS editor page that supports create and edit modes.
12. Add dynamic segment add and remove behavior.
13. Compute per-segment ramp timing client-side.
14. Compute total duration client-side and show it read-only.
15. Save duration in the JSON payload automatically.
16. Add a live graph preview using SVG or canvas.
17. Load legacy program files without failing if program_number exists.
18. Strip program_number when saving edited legacy files.
19. Handle rename-on-save when name or created date changes.
20. Verify old and new program files still work with the kiln runtime.

**Acceptance Checklist**

The work is done only if all of these are true.

1. A user can open a list of saved firing programs.
2. A user can create a new program without entering a program number.
3. A user can open an existing program for editing.
4. A user can add and remove segments in the browser.
5. Total duration updates automatically as the program changes.
6. The preview graph updates automatically as the program changes.
7. Saving creates a JSON file with an automatic filename.
8. Editing an existing program updates the correct file, including rename behavior if needed.
9. Legacy files with program_number still load successfully.
10. Re-saved legacy files are converted to the new schema.
11. Saved files remain compatible with the firmware logic that starts firing programs.
12. The page stays lightweight and uses plain HTML, CSS, and JS only.

**Prompt For The External Agent**

Paste this as-is to the external agent:

```text
Implement a firing program library and editor for my standalone ESP32 kiln controller project.

Context:
- This project does not use Svelte or any frontend framework.
- The UI is plain HTML, CSS, and JavaScript served by the ESP32.
- Programs are stored as JSON files in SPIFFS.
- There is already an existing firing program page that manually creates a program and submits it to the firmware for saving.
- I want the new implementation to stay lightweight and simple, suitable for an ESP32-hosted web UI.

Goals:
- Add the ability to view saved programs.
- Add the ability to edit existing programs.
- Add a live preview graph of the firing profile.
- Remove the need for program_number.
- Generate program JSON filenames automatically.
- Calculate duration automatically.

Requirements:
- First inspect the current project and identify:
  - The existing firing program HTML, JS, and CSS files.
  - The firmware handlers or routes that save and load program files.
  - The current JSON schema and storage directory.
  - Whether the project uses SPIFFS or LittleFS.
- Keep legacy compatibility. If old files include program_number, they must still load.
- New and updated program files should use this schema:
  - name
  - created_date
  - duration
  - segments
- Each segment should use:
  - target_temperature
  - firing_rate
  - holding_time
- Remove program_number from the UI and from newly saved JSON.
- Use automatic filename generation based on a filesystem-safe slug from program name plus created_date.
- Avoid filename collisions by adding a numeric suffix when needed.
- If an existing program is edited and its name or created_date changes, handle file renaming safely.

Frontend expectations:
- Use plain HTML, CSS, and JS only.
- Create a lightweight program library page that lists saved programs and allows New and Edit.
- Create or refactor a program editor page that supports create mode and edit mode.
- Allow adding and removing segments dynamically.
- Show a read-only derived duration field.
- Add a live preview graph using lightweight SVG or canvas, not a heavy chart library.
- Recompute duration and graph data whenever segment inputs change.

Calculation rules:
- Use an ambient start temperature of 20 C unless the project already has a better configured ambient value.
- Ramp minutes = abs(target - previous_target) / firing_rate * 60 when firing_rate > 0.
- Total duration = sum of all ramp minutes plus all hold minutes.
- Duration should be saved automatically, not entered manually.

Firmware expectations:
- Add or adapt endpoints or handlers for:
  - listing saved programs
  - fetching one program
  - creating or updating a program
- Delete is optional if easy.
- The save handler should validate input and return the final saved id or slug.

Migration expectations:
- Legacy files with program_number must remain loadable.
- When a legacy program is edited and saved, remove program_number and save it in the new format.
- Do not break the firmware’s existing logic that consumes saved program files for firing.

Deliverables:
- Implement the feature end-to-end in the project.
- Summarize the routes, files, and handlers you changed.
- Explain the final JSON format.
- Note any assumptions you had to make.
- Report whether delete was implemented or intentionally deferred.

Acceptance criteria:
- I can view saved programs.
- I can create a new program without entering a program number.
- I can edit an existing program.
- Duration updates automatically.
- The preview graph updates automatically.
- Saved filenames are generated automatically.
- Legacy program files still load.
- Saved programs remain usable by the kiln controller runtime.

Do not introduce a frontend framework. Keep the solution simple, robust, and resource-conscious.
```


** Additional implementations **
- Have back and home buttons in the web UI
- Add IP address to CONFIG TFT subscreen