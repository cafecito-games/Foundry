# Run Targets Dropdown Modal Design

Date: 2026-07-03

## Context

Run targets are currently configured through `RunTargetsPanel`, an `EditorDock` registered in the editor sidebar. The run bar already has the main Run Project button, pause/stop buttons, and the existing remote deploy menu (`EditorRunNative`). The run-target deploy rows are part of the remote deploy menu, while the detailed configuration/readiness UI lives in the dock.

This placement makes run targets feel like a persistent workspace panel even though they are a run configuration workflow. The desired direction is to access run target configuration from a small dropdown button next to the main Run Project button, similar to other IDEs.

## Goals

- Add a compact run-options dropdown next to the main Run Project button.
- Put a `Run Targets Configuration...` item in that dropdown.
- Open run target configuration in a modal titled `Run Targets`.
- Remove run targets as a dockable/sidebar panel.
- Preserve existing run-target behavior: target persistence, active target selection, signing/device editing, readiness probing, and one-click connected-device setup.
- Keep the dropdown extensible for future run-related options such as multiple-instance controls.
- Preserve the first-open mobile iOS discoverability flow by opening the modal once instead of focusing the old dock.

## Non-Goals

- Do not change the run-target data model or `run_targets.cfg` format.
- Do not change native deploy behavior or the remote deploy dropdown rows.
- Do not implement multiple-instance settings in this change.
- Do not redesign the run-target form beyond what is needed for modal placement.

## UI Design

`EditorRunBar` gains a small `MenuButton` immediately after the main Run Project button and before Pause/Stop. The button uses a dropdown affordance, has the tooltip `Run Options`, and owns a popup menu.

The menu contains one item:

- `Run Targets Configuration...`

Selecting the item opens a modal dialog titled `Run Targets`. The dialog contains the existing run-target configuration surface: target list, Add/Remove/Rename controls, connected-but-unconfigured devices, signing/device fields, and readiness checks.

The button is intentionally named and structured as run options rather than a run-target-only button. Future menu items can be added without changing the toolbar placement.

## Architecture

Refactor the current dock-specific surface into modal-friendly pieces:

- Convert `RunTargetsPanel` away from `EditorDock` inheritance so it can be embedded in a dialog.
- Keep the existing content and helper methods together unless implementation reveals a cleaner low-risk split.
- Add an `AcceptDialog` owner in `EditorRunBar` containing the run-target configuration control.
- Add a run-options menu item enum in `EditorRunBar` so future items can be added without reusing raw ids.
- Remove the old dock registration and focus calls for Run Targets from `EditorNode`.

`EditorRunNative` remains the owner of the shared `RunTargetManager` and platform adapters. The modal consumes that shared manager through `EditorRunNative::get_singleton()->get_run_target_manager()` when available, so the run bar selector and configuration UI share the same source of truth.

## First-Open Flow

The existing one-shot marker currently named around "show dock on first open" should continue to be consumed only in interactive editor opens. Its effect changes from focusing a dock to opening the `Run Targets` modal.

Rename the marker API to modal-neutral wording while preserving the stored config key for backward compatibility, so projects created before this change still open the configuration once.

## Error Handling

- Command-line/headless runs must not consume the first-open marker.
- If the run-target config cannot be loaded, the configuration UI keeps its current read-only behavior and error reporting.
- If no platform adapter is available, the readiness area keeps showing the existing unsupported-platform message.
- Modal opening should be safe when the run target manager has not been installed yet; the existing fallback manager path should remain intact.

## Testing

Use test-first implementation for behavior changes:

- Add or update a headless unit test for the first-open marker so it remains backward-compatible while no longer being dock-specific in naming.
- Add a pure or low-level testable seam for the run-options menu model, asserting that the first item is `Run Targets Configuration...` and maps to the run-target configuration action.
- Keep existing `RunTargetsPanel` helper tests passing.

Verification should include:

- A focused run-target test filter with `./bin/foundry.* --headless test run --case "*RunTarget*" --force-colors` after building.
- A SCons editor build to catch UI compilation errors.

## Open Decisions

- Use the exact menu label `Run Targets Configuration...`.
- Place the dropdown directly after Run Project and before Pause/Stop.
- Keep remote deploy behavior unchanged.
