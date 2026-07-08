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
- The dialog has three tabs: `Projects`, `Manage`, and `About`.
- `Projects` contains two primary actions: `Create Project` and `Open Existing Project`.
- `Projects` also contains a compact recent-project list.
- `Manage` contains lower-frequency project maintenance: scan for projects, filter projects by
  name/path/tag, edit project tags, duplicate a selected project, remove stale missing entries, and
  open/reveal selected projects.
- `About` shows the current Foundry version, copyright text, license information, and links or
  buttons for full credits and third-party notices.
- Asset Library is explicitly out of scope. It is expected to be removed and should not be carried
  into the new startup surface.

## Current Project Manager Feature Disposition

The existing Project Manager includes more than startup choice. Those features need explicit
disposition so they are not accidentally lost or accidentally rebuilt into the primary startup path.

- `Scan`: recursively searches a selected folder for `project.foundry` files and adds discovered
  projects to the known project list. This lives in the `Manage` toolbar as `Scan Folder`.
- `Tags`: labels projects and filters the full project list. This is useful for users with many
  projects. Tags are visible on managed project rows, editable in the selected-project detail pane,
  and included in the `Manage` filter.
- `Remove Missing`: prunes entries whose project path no longer exists. The new recent list should
  mark missing entries inline and allow single-entry removal. The `Manage` toolbar also exposes a
  secondary bulk `Remove Missing` action.
- `Duplicate`: copies an existing project into a new location. This remains useful, but it is a
  selected-project maintenance workflow. It appears in the `Manage` selected-project detail pane and
  row actions, not on the `Projects` startup landing tab.
- Existing project rename, run, recovery/verbose open, and similar maintenance actions should also
  stay out of the startup landing view unless they are required to recover from a failed open.

The first implementation should keep the `Projects` tab lean and put maintenance behind the
secondary `Manage` tab so launch remains fast and clear.

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

[ Projects ] [ Manage ] [ About ]

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

## Manage Tab

The `Manage` tab incorporates project-list maintenance without making it the default startup view.
It is available when the dialog is opened from projectless startup or from an explicit project
switch/open command.

The tab uses a denser management layout:

```text
[ Filter by name, path, or tag... ] [ Scan Folder ] [ Remove Missing ]

Known projects
> Moonlight Courier          ~/Games/moonlight       client 2d
  Foundry RPG Prototype      ~/Work/foundry-rpg      rpg prototype
  Old Jam Build              ~/Archive/jam-build     missing archive

Selected project
  Open Project
  Duplicate...
  Reveal Folder

  Tags: [client] [2d] [+ Add Tag]
```

Rules:

- `Projects` remains the startup tab. `Manage` is never selected by default during ordinary startup.
- `Scan Folder` opens a folder picker and runs the existing async project-discovery behavior.
- The filter searches project name, path, and tag text.
- Tags are shown as compact chips and can be added/removed for the selected project.
- `Duplicate...` copies the selected project through the existing duplicate-project flow.
- Missing projects show an inline status badge and expose row-level removal.
- `Remove Missing` is visually secondary and prompts before bulk cleanup.
- Asset Library does not appear in `Manage`.

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
  cached_tags
}
```

Tags remain project metadata when they are present in `project.foundry`; the global store may cache
them for fast projectless display. Tag edits from `Manage` update the project configuration for that
project and refresh the cache.

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
- manages the `Manage` tab project list, scan flow, tag edits, duplicate flow, and missing-project
  cleanup;
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
- `Manage > Scan Folder` discovers projects and adds them to known projects;
- `Manage` filters projects by name, path, and tag;
- `Manage` can add and remove tags for a selected project;
- `Manage > Duplicate...` reaches the existing duplicate-project flow for the selected project;
- missing projects show inline status, support single-entry removal, and support prompted bulk
  removal through `Remove Missing`;
- `About` tab displays version and copyright data;
- legacy `editor project-manager` behavior routes to the new startup dialog or documented alias;
- explicit invalid `--project` path does not silently auto-open another project.

For editor-facing behavior, use the editor automation MCP surface instead of screenshot or
coordinate-driven tests.

## Acceptance Criteria

- A normal launch with a valid last project opens that project immediately with no startup dialog.
- A launch without a valid last project opens the IDE shell in an empty workspace and shows the
  centered startup dialog.
- The startup dialog contains the Foundry Engine logo, `Projects`, `Manage`, and `About` tabs,
  create/open actions, and a compact recent-project list.
- The `Projects` tab is selected by default whenever the dialog appears for startup.
- The `Manage` tab provides scan, tag filtering/editing, selected-project duplicate, row-level
  missing-project removal, and prompted bulk missing-project cleanup.
- The About tab shows version, copyright, license, and credits/third-party notice access.
- Asset Library is absent.
- Missing recent projects are visible and removable from recents.
- Existing command-line behavior for explicit project paths remains predictable and test-covered.
