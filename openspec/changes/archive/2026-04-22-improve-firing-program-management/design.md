## Context

The current web UI exposes a single `data/firingProgram.html` form that posts multipart form data to `POST /program-editor`. The firmware handler in `lib/network/network.cpp` writes `/firingProgram_<programNumber>.json` files containing `name`, `duration`, `createdDate`, `segmentQuantity`, and `segments`, while the TFT workflow in `lib/gui/gui.cpp` loads programs back by numeric filename through `openProgram()`. The CONFIG TFT subscreen only exposes captive-portal controls, and the device IP address is only emitted to serial logs.

This change needs to modernize the program-management UX without introducing a frontend framework or breaking the kiln runtime’s ability to select and execute saved programs. It also needs to cover two smaller UX improvements: consistent back/home navigation in the web UI and direct IP visibility on the TFT config screen.

## Goals / Non-Goals

**Goals:**
- Introduce a lightweight program library and editor flow for saved firing programs.
- Replace manual program numbering with automatic, filesystem-safe identifiers.
- Persist canonical program JSON using the new schema while keeping legacy files loadable.
- Keep TFT program selection and runtime loading functional after the storage migration.
- Add derived duration and live profile preview in the editor with minimal client-side code.
- Add back/home navigation to secondary web UI screens and show the current IP address on the CONFIG TFT subscreen.

**Non-Goals:**
- Rewriting the web UI with a framework or heavy charting dependency.
- Changing kiln heat-control behavior, firing algorithms, or segment execution semantics.
- Requiring a one-time manual migration step before deployment.
- Committing to delete support as part of the initial contract if it complicates the existing handlers.

## Decisions

### 1. Split the firing-program experience into a library page and an editor page

The web UI will move from a single form page to two lightweight views: a library that lists saved programs and an editor that supports both create and edit flows. This keeps state management simple in plain HTML/JS and makes room for metadata listing, edit actions, and derived previews without overloading the existing form.

**Alternatives considered:**
- Extend the current single page with conditional modes. Rejected because it would mix list, edit, and save concerns in one static page and make navigation/state harder to reason about.
- Build a richer SPA-style client. Rejected because it adds complexity and weight that are not appropriate for an ESP32-hosted UI.

### 2. Store programs under canonical slug-based identifiers and maintain a program catalog

New and updated programs will be stored under filesystem-safe identifiers derived from program name plus created date, with collision-safe numeric suffixes. A lightweight catalog/manifest will track the saved programs and expose metadata for list/fetch/save operations.

This catalog becomes the stable bridge between the web UI, firmware APIs, and TFT program selection. It avoids forcing operators to manage numeric IDs while keeping a deterministic lookup surface for the rest of the system.

**Alternatives considered:**
- Keep numeric filenames as the canonical format. Rejected because it preserves the UX problem the change is trying to remove.
- Use raw names as filenames. Rejected because names are not filesystem-safe or collision-safe.

### 3. Normalize program data at the firmware boundary

The save path will validate and normalize incoming payloads into a canonical schema:
- `name`
- `created_date`
- `duration`
- `segments`

Each segment will be saved as:
- `target_temperature`
- `firing_rate`
- `holding_time`

The loader will accept legacy shapes such as `createdDate`, `segmentQuantity`, camelCase segment fields, and `program_number`/`programNumber`, then normalize them into the in-memory representation. Re-saved legacy programs will be written back only in the canonical schema.

**Alternatives considered:**
- Keep mixed old/new field names indefinitely. Rejected because it makes the data contract ambiguous and increases maintenance cost.
- Migrate all legacy files eagerly on boot. Rejected because it adds risk and unnecessary write amplification.

### 4. Preserve runtime compatibility by selecting programs through catalog entries rather than filename conventions

The TFT program-selection flow currently stores a numeric preference and loads `/firingProgram_<programNumber>.json` directly. Instead of preserving that filename convention, the TFT flow will resolve the selected entry through the catalog and then load the referenced program file. Existing legacy files remain selectable because the catalog builder will include both legacy and canonical files.

This keeps the runtime compatible with saved programs after migration while allowing the storage format to evolve independently of the old numbering scheme.

**Alternatives considered:**
- Dual-write legacy numeric aliases for every new save. Rejected because alias files can drift and create unclear source-of-truth behavior.
- Remove TFT program selection from scope. Rejected because the proposal explicitly requires saved programs to remain usable by the kiln runtime.

### 5. Derive duration and preview client-side, then verify duration on save

The editor will recompute ramp time, total duration, and SVG/canvas preview points whenever segment inputs change. The firmware save handler will still validate payload shape and recompute or verify the submitted duration before persisting, so malformed or stale client data cannot become the source of truth.

**Alternatives considered:**
- Compute duration only on the server. Rejected because the editor would lose immediate feedback.
- Trust the client’s duration completely. Rejected because it weakens validation and migration safety.

### 6. Surface navigation and network visibility with minimal shared UI helpers

Secondary web pages will expose explicit Back and Home controls using the existing plain HTML/JS/CSS structure. The CONFIG TFT screen will render the active IP address from station mode (`WiFi.localIP()`) or captive portal/AP mode (`WiFi.softAPIP()`), and it will show an unavailable state when no address is active.

**Alternatives considered:**
- Keep navigation implicit through browser history only. Rejected because these pages are often used on mobile devices or captive-portal flows where explicit navigation is clearer.
- Leave IP visibility to serial logs. Rejected because operators need the address directly on the device.

## Risks / Trade-offs

- **Catalog drift between files and metadata** -> Rebuild or refresh catalog entries during save/load operations and prefer deriving catalog contents from actual program files when possible.
- **Rename collisions or interrupted updates** -> Use deterministic slug generation, collision suffixing, and replace-on-success semantics when renaming an existing program.
- **Legacy schema variants causing partial loads** -> Centralize normalization so both web and TFT paths share the same compatibility rules.
- **Preview logic adding browser overhead** -> Keep the graph implementation to simple SVG/canvas primitives and update only on editor input changes.

## Migration Plan

1. Add program-catalog and normalization helpers that can enumerate existing program files and read both legacy and canonical schemas.
2. Introduce list/fetch/save firmware handlers backed by the catalog while keeping old program files readable.
3. Update TFT selection/loading to resolve through catalog entries instead of assuming numeric filenames.
4. Replace the current program form with library/editor pages, derived duration, preview rendering, and shared back/home controls.
5. Expose IP information on the CONFIG TFT subscreen.

Rollback relies on two safeguards: legacy files remain untouched until a program is edited and re-saved, and the compatibility loader continues to understand legacy data. If the new UI or catalog flow needs to be reverted, the system still has access to the original program files.

## Open Questions

None blocking. Delete support remains intentionally optional and can be implemented only if it fits the existing handlers without expanding the compatibility surface.
