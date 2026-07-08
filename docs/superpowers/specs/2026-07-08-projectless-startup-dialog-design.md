# Design: Projectless Startup Dialog

Status: ready for user review
Date: 2026-07-08

## Motivation

Foundry should stop presenting the Project Manager as a separate startup mode. Launching Foundry
should feel like launching the IDE. If the user has an existing working context, Foundry should
return to it immediately; if it needs a project choice, it should ask from inside the editor shell.

The desired mental model is:

- Foundry opens the last valid project automatically.
- If no valid project can be opened, Foundry opens the IDE in an empty workspace.
- A small centered startup dialog guides the user to create a project, open an existing project, or
  choose a recent project.

This dialog is not a replacement Project Manager. It is a project decision surface.

## Product Decisions

- Normal launch opens the last valid project immediately with no startup dialog flash.
- The startup dialog appears only when a project decision is required or explicitly requested:
  first launch, no remembered project, missing or invalid remembered project, user bypass of
  auto-open, or a project-switch/open command from the editor.
- The dialog is centered over the normal editor shell in an empty workspace.
- The dialog header shows the Foundry Engine logo and name.
- The dialog has two tabs: `Projects` and `About`.
- `Projects` contains two primary actions: `Create Project` and `Open Existing Project`.
- `Projects` also contains a compact recent-project list.
- `About` shows the current Foundry version, copyright text, license information, and links or
  buttons for full credits and third-party notices.
- Asset Library is explicitly out of scope. It is expected to be removed and should not be carried
  into the new startup surface.

## Current Project Manager Feature Disposition

The existing Project Manager includes more than startup choice. Those features need explicit
disposition so they are not accidentally lost or accidentally rebuilt into the startup dialog.

- `Scan`: recursively searches a selected folder for `project.foundry` files and adds discovered
  projects to the project list. This is useful only for bulk discovery. It should not appear in the
  startup dialog. If retained, it belongs in a future Project Browser or an editor command such as
  `Project > Find Projects...`.
- `Tags`: labels projects and filters the full project list. This is useful for users with many
  projects, but it depends on a full project browser. It should not appear in the startup dialog.
- `Remove Missing`: prunes entries whose project path no longer exists. The new recent list should
  handle this mostly automatically by marking missing entries inline and offering `Remove from
  Recents`. A bulk remove command can be deferred.
- `Duplicate`: copies an existing project into a new location. This remains useful, but it is a
  project creation workflow rather than startup choice. If retained, it should be exposed from the
  editor project menu or from a future Project Browser, not from the startup dialog.
- Existing project rename, run, recovery/verbose open, and similar maintenance actions should also
  stay out of the startup dialog unless they are required to recover from a failed open.

The first implementation should keep the startup dialog lean and preserve room for a later Project
Browser if these maintenance workflows remain important.

## Startup Flow

### Default Launch

On launch without an explicit project path:

1. Load the global recent-project state.
2. Resolve the last opened project path.
3. If that path exists, contains a valid `project.foundry`, and is compatible enough to open, launch
   directly into the project editor.
4. If the path is missing, invalid, or incompatible, launch the editor shell in projectless mode and
   show the startup dialog.

There is no intermediate "Opening last project..." dialog in the successful path.

### Explicit Project Path

When the user launches with an explicit project path, Foundry should preserve current command-line
semantics:

- A valid path opens directly.
- An invalid explicit path reports an error instead of silently falling back to another project.
- The invalid-path handling may offer the startup dialog only after making the failure clear and only
  in interactive editor launches.

### Explicit Project Choice

An editor command should open the same centered startup dialog while the IDE is already running.
This replaces "Quit to Project Manager" as a user-facing concept. The command can be named around
opening or switching projects, for example `Project > Open Project...`.

For compatibility, the existing command-first CLI entry for `editor project-manager` can initially
act as an alias for opening the editor shell with the startup dialog visible. User-facing help should
describe the new behavior rather than the old Project Manager.

## Dialog Layout

The dialog uses a compact centered modal shape:

```text
Foundry Engine logo + name
Create or open a project workspace

[ Projects ] [ About ]

Projects tab:
  Start a project
  [ Create Project          Ctrl+N ]
  [ Open Existing Project   Ctrl+O ]

  Recent
  > Moonlight Courier        ~/Games/moonlight
    Foundry RPG Prototype    ~/Work/foundry-rpg
    Platformer Testbed       ~/Games/platformer
```

The recent list is intentionally compact:

- show a small fixed number of entries, enough for quick selection without becoming a management UI;
- sort by most recently opened;
- indicate missing entries inline;
- allow opening an entry by double-click, Enter, or an obvious button/action;
- offer a lightweight way to remove a single stale entry from recents.

The dialog should use the real Foundry logo asset where available, with accessible text fallback.

## About Tab

The About tab is part of the startup dialog because the Project Manager previously provided a place
for About access before a project was open.

It should show:

- Foundry Engine product name;
- exact version string, including commit hash/date when available in development builds;
- copyright for Foundry Engine contributors and Godot Engine contributors;
- license summary;
- access to full credits and third-party notices.

The About tab should reuse existing About/Credits data sources where possible rather than
duplicating legal text.

## Data Model

Recent-project and last-project state must be global editor data, not per-project metadata. The
startup dialog can appear before any project is loaded, so it cannot depend on
`project_metadata.cfg`.

Store a compact global record per recent project:

```text
RecentProject {
  path
  display_name_cache
  last_opened_unix_time
  last_known_version
  favorite_or_pinned flag, only if needed later
}
```

The first implementation does not need tags. It should avoid migrating the old tag model into the
new recents store unless a later Project Browser needs it.

When a project opens successfully:

- update `last_opened_unix_time`;
- move it to the front of recents;
- set it as the default last project for auto-open;
- refresh cached name/version data from `project.foundry`.

When a project fails validation:

- do not remove it automatically;
- mark it as missing/invalid in the dialog;
- let the user remove the entry from recents.

## Architecture

### Startup Routing

Startup routing should move from a binary editor-vs-project-manager decision to:

- project editor with a loaded project;
- projectless editor shell with startup dialog;
- command-line tool or runtime paths unchanged.

The projectless editor shell should initialize enough editor UI to present the workspace and modal,
but it should avoid project-specific services that require a loaded `ProjectSettings` resource path.
Unavailable docks or commands should be disabled or hidden until a project is opened.

### Startup Dialog Controller

Introduce a small editor-side controller for the startup surface:

- owns show/hide behavior;
- reads and writes the global recent-project store;
- validates recent project paths;
- launches create/open flows;
- dispatches to the About tab;
- exposes automation-friendly semantic names for tests.

It should reuse existing project creation and open/import validation logic where possible. The
existing `ProjectDialog` creation flow is a candidate for reuse or extraction. The new dialog should
not depend on `ProjectManager` as a monolithic UI owner.

### Project Opening

Selecting a recent or existing project should restart or transition into a normal project-loaded
editor. The simplest robust first implementation may reuse the existing process-restart path:

- gather forwardable editor CLI arguments;
- launch `editor open --project <path>`;
- close the projectless editor instance.

A future in-process project switch can be considered separately if the editor becomes safe to unload
and reload project state without restart.

## Error Handling

- Missing last project: open projectless editor shell and show dialog with the missing project marked.
- Invalid `project.foundry`: show an inline status in recents and keep `Open Existing Project`
  available.
- Incompatible project version: show a clear message and do not auto-open repeatedly on next launch
  unless the user explicitly chooses it.
- Failed create/open flow: keep the dialog open and surface the error near the relevant action.
- Empty recents: show only the primary actions and a small empty-state line; do not show a blank list.

## Testing

Add native and automation coverage for:

- default launch auto-opens the last valid project without showing the dialog;
- no remembered project opens the projectless editor shell and shows the startup dialog;
- missing remembered project shows the dialog and marks the recent entry missing;
- selecting a recent project opens it and updates global recents;
- `Create Project` reaches the existing create-project flow;
- `Open Existing Project` reaches the existing open-folder/project validation flow;
- `About` tab displays version and copyright data;
- legacy `editor project-manager` behavior routes to the new startup dialog or documented alias;
- explicit invalid `--project` path does not silently auto-open another project.

For editor-facing behavior, use the editor automation MCP surface instead of screenshot or
coordinate-driven tests.

## Acceptance Criteria

- A normal launch with a valid last project opens that project immediately with no startup dialog.
- A launch without a valid last project opens the IDE shell in an empty workspace and shows the
  centered startup dialog.
- The startup dialog contains the Foundry Engine logo, `Projects` and `About` tabs, create/open
  actions, and a compact recent-project list.
- The About tab shows version, copyright, license, and credits/third-party notice access.
- Asset Library is absent.
- Scan, tags, duplicate, and bulk missing-project maintenance are not present in the startup dialog.
- Missing recent projects are visible and removable from recents.
- Existing command-line behavior for explicit project paths remains predictable and test-covered.
