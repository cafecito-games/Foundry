# Issue: Replace Project Manager startup with projectless IDE startup dialog

Issue type: Feature request

Foundry version: develop after `12fc95fb6a`

System information: Editor UX; platform-independent. macOS, Linux, and Windows startup behavior
should remain consistent.

## Description

Foundry should stop presenting the Project Manager as a separate startup mode. Launching Foundry
should feel like launching the IDE.

The new startup behavior should be:

1. If Foundry has a remembered last project and it is valid, open that project immediately with no
   startup dialog flash.
2. If Foundry cannot open a valid last project, open the normal IDE shell in an empty workspace.
3. Center a startup dialog over that empty workspace to guide the user into creating, opening, or
   managing projects.

The dialog should replace the old Project Manager surface. It should keep the default startup path
focused, but still preserve important project-list maintenance workflows behind a secondary
`Manage` tab.

Detailed design spec:

- `docs/superpowers/specs/2026-07-08-projectless-startup-dialog-design.md`

Approved visual direction:

- centered modal dialog inside the IDE shell;
- Foundry Engine logo/name at the top;
- tabs for `Projects`, `Manage`, and `About`;
- `Projects` selected by default;
- `Manage` tab for scan/tags/remove-missing/duplicate;
- `About` tab for version, copyright, license, credits, and third-party notices;
- Asset Library omitted.

## Goals

- Remove the Project Manager as a separate startup experience.
- Auto-open the last valid project immediately on normal launch.
- Support a real projectless editor shell that can show an empty workspace and startup dialog.
- Keep the first startup view simple: create, open, and compact recents.
- Preserve project maintenance workflows through `Manage`, not through the launch-focused
  `Projects` tab.
- Keep command-line behavior predictable for explicit project paths.
- Make the new UI automatable and testable through editor automation.

## Non-goals

- Do not keep or recreate Asset Library in this surface.
- Do not make `Manage` the default startup tab.
- Do not require in-process project switching for the first implementation; a restart into
  `editor open --project <path>` is acceptable.
- Do not silently fall back to another project when the user provided an explicit invalid project
  path.
- Do not migrate the old Project Manager UI wholesale into the editor.

## UX Design

### Default launch

When Foundry starts without an explicit project path:

1. Load global project history.
2. Resolve the last opened project.
3. Validate that it exists, contains `project.foundry`, and is compatible enough to open.
4. If valid, open it immediately.
5. If invalid or missing, open the IDE shell projectless and show the startup dialog.

There should be no "Opening last project..." interstitial when the last project is valid.

### Dialog structure

The dialog is centered over the empty editor workspace.

Header:

- Foundry Engine logo/mark;
- product name;
- short supporting line such as "Create or open a project workspace";
- tabs: `Projects`, `Manage`, `About`.

`Projects` tab:

- selected by default whenever the dialog appears for startup;
- primary action: `Create Project`;
- secondary action: `Open Existing Project`;
- compact recent-project list sorted by most recently opened;
- missing/invalid recent entries marked inline;
- entry open via double-click, Enter, or explicit button/action;
- stale recent entry removal available without opening `Manage`.

`Manage` tab:

- project filter field searching by project name, path, and tag;
- `Scan Folder` toolbar action;
- visually secondary `Remove Missing` bulk cleanup action;
- known-project list with project name, path, tags, and missing status;
- selected-project detail pane;
- selected-project actions: `Open Project`, `Duplicate...`, `Reveal Folder`;
- tag chips for selected project;
- add/remove tag controls;
- row-level removal for missing entries.

`About` tab:

- Foundry Engine product name;
- exact version string, including commit/date when available;
- copyright for Foundry Engine contributors and Godot Engine contributors;
- license summary;
- access to full credits;
- access to third-party notices.

## Project Maintenance Behavior

### Scan Folder

`Scan Folder` should reuse the existing Project Manager scan behavior where possible:

- open a directory picker;
- recursively search for `project.foundry`;
- run asynchronously so the editor remains responsive;
- add discovered projects to global known projects;
- update display-name/version/tag caches from discovered project files;
- show progress and cancellation affordance.

### Tags

Tags should be visible and editable from `Manage`:

- show tags as compact chips in project rows;
- use tags in the filter field;
- allow adding/removing tags for the selected project;
- persist tag changes to the project configuration when possible;
- refresh global cache after edits;
- handle missing projects by disabling tag edits and showing a missing status.

### Remove Missing

Missing project paths should be obvious but not destructive by default:

- validate known project paths before showing rows;
- mark missing rows with an inline badge;
- allow removing one missing entry from its row;
- support a prompted `Remove Missing` bulk cleanup action;
- never delete files from disk; only remove entries from known projects/recents.

### Duplicate

`Duplicate...` is a selected-project maintenance action:

- enabled only for one valid selected project;
- copies the project to a chosen target location through the existing duplicate flow where possible;
- validates destination folder and naming;
- after duplicate succeeds, adds the new project to known projects;
- if the user chooses an "open after duplicate" flow, open the duplicated project through the normal
  editor-open path.

## Data Model

Recent and known project state must be global editor data, not per-project metadata, because this UI
can appear before any project is loaded.

Store enough global data to render projectless startup quickly:

```text
KnownProject {
  path
  display_name_cache
  last_opened_unix_time
  last_known_version
  cached_tags
}
```

Rules:

- Successful project open updates `last_opened_unix_time` and makes that project the auto-open
  candidate.
- Known projects are sorted by recency in `Projects` and filterable in `Manage`.
- Cached tags support projectless display, but authoritative tag data should remain in
  `project.foundry` when the project exists.
- Missing projects remain listed until the user removes them or confirms bulk cleanup.

## Implementation Notes

- Startup routing should move from the current editor-vs-project-manager split to:
  - project editor with loaded project;
  - projectless editor shell with startup dialog;
  - existing command-line tool/runtime paths unchanged.
- The projectless editor shell must initialize enough UI to show the empty workspace and modal,
  while disabling or hiding project-specific commands until a project is loaded.
- The startup dialog should be owned by a small controller rather than by the existing monolithic
  `ProjectManager`.
- Reuse/extract existing `ProjectDialog`, scan, duplicate, tag, and missing-project cleanup logic
  where it is practical.
- `editor project-manager` can initially become a compatibility alias that opens the editor shell
  with this dialog visible. User-facing help should describe the new behavior.
- "Quit to Project Manager" should become project-open/switch language in menus and shortcuts.
- Explicit invalid `--project` paths must still report an error instead of silently auto-opening the
  last valid project.

## Acceptance Criteria

### Startup routing

- [ ] Launching Foundry with no explicit project path and a valid remembered last project opens that
      project immediately.
- [ ] The startup dialog does not flash or briefly appear during successful auto-open.
- [ ] Launching Foundry with no remembered project opens the IDE shell in an empty workspace and
      shows the startup dialog.
- [ ] Launching Foundry with a missing remembered last project opens the projectless IDE shell and
      shows the startup dialog.
- [ ] Missing remembered projects are marked as missing in the dialog instead of being silently
      deleted.
- [ ] Launching with an explicit valid `--project` path opens that project directly.
- [ ] Launching with an explicit invalid `--project` path reports that path as invalid and does not
      auto-open another remembered project.
- [ ] Existing command-first CLI help is updated so `editor project-manager` no longer describes the
      old separate Project Manager surface.

### Projectless editor shell

- [ ] The editor can initialize without a loaded project far enough to show the normal IDE shell,
      empty workspace, and startup dialog.
- [ ] Project-specific docks/actions that cannot operate projectless are disabled, hidden, or show a
      clear unavailable state.
- [ ] The empty workspace does not create a fake project or write project metadata into the
      repository root.
- [ ] Closing the dialog without choosing a project leaves a stable projectless editor shell or exits
      according to the chosen UX decision.

### Dialog layout

- [ ] The dialog is centered over the editor workspace.
- [ ] The dialog header shows the Foundry Engine logo/mark and product name.
- [ ] The dialog has `Projects`, `Manage`, and `About` tabs.
- [ ] `Projects` is selected by default when the dialog opens during startup.
- [ ] The dialog is keyboard navigable.
- [ ] Controls have stable accessible names for editor automation.
- [ ] The dialog fits at normal desktop sizes and degrades reasonably on smaller editor windows.

### Projects tab

- [ ] `Create Project` is visible as a primary action.
- [ ] `Open Existing Project` is visible as a secondary action.
- [ ] `Create Project` opens the existing create-project flow or its extracted replacement.
- [ ] `Open Existing Project` opens a folder/project picker and validates `project.foundry`.
- [ ] Recent projects are shown compactly and sorted by most recently opened.
- [ ] A valid recent project can be opened by keyboard and pointer interaction.
- [ ] Opening a recent project updates global recents and last-project state.
- [ ] Missing/invalid recent projects are marked inline.
- [ ] A missing/invalid recent entry can be removed from recents without opening `Manage`.
- [ ] Asset Library is not present.

### Manage tab

- [ ] `Manage` shows a filter field for project name, path, and tag.
- [ ] Filtering by project name narrows the known project list.
- [ ] Filtering by project path narrows the known project list.
- [ ] Filtering by tag narrows the known project list.
- [ ] `Scan Folder` opens a folder picker.
- [ ] `Scan Folder` discovers `project.foundry` files recursively.
- [ ] Scan runs asynchronously and exposes progress/cancel state.
- [ ] Discovered projects are added to known projects without duplicating existing entries.
- [ ] Project rows show name, path, tags, and missing status.
- [ ] Selecting a project updates the detail pane.
- [ ] `Open Project` opens the selected valid project.
- [ ] `Reveal Folder` reveals the selected valid project folder through the platform file manager.
- [ ] `Duplicate...` is enabled for one valid selected project.
- [ ] `Duplicate...` runs the existing duplicate-project behavior or an equivalent extracted flow.
- [ ] A successful duplicate is added to known projects.
- [ ] Tag chips are visible for tagged projects.
- [ ] Tags can be added to a selected valid project.
- [ ] Tags can be removed from a selected valid project.
- [ ] Tag edits persist to the project configuration and refresh the global cache.
- [ ] Missing projects disable actions that require a valid project folder.
- [ ] Missing projects expose row-level removal from known projects/recents.
- [ ] `Remove Missing` prompts before bulk cleanup.
- [ ] `Remove Missing` removes only stale entries from known projects/recents and does not delete
      user files.

### About tab

- [ ] `About` shows Foundry Engine product name.
- [ ] `About` shows the exact current version string.
- [ ] Development builds include commit/date information when available.
- [ ] `About` shows Foundry Engine and Godot Engine copyright text.
- [ ] `About` shows license information.
- [ ] `About` exposes full credits.
- [ ] `About` exposes third-party notices.
- [ ] About data is reused from existing About/Credits sources where practical.

### Persistence and data

- [ ] Global known-project data is stored outside per-project `project_metadata.cfg`.
- [ ] Successful project open updates the last-project auto-open candidate.
- [ ] Successful project open moves the project to the front of recents.
- [ ] Project display-name/version/tag cache is refreshed from `project.foundry`.
- [ ] Missing projects are not automatically removed without user action.
- [ ] The new data store does not require tags for projects that do not use them.

### Compatibility and cleanup

- [ ] Existing project creation behavior remains compatible with current templates/settings.
- [ ] Existing project open/import validation remains compatible with `project.foundry`.
- [ ] Existing project scan behavior is preserved or intentionally replaced with equivalent
      behavior.
- [ ] Existing duplicate behavior is preserved or intentionally replaced with equivalent behavior.
- [ ] Existing tag behavior is preserved or intentionally replaced with equivalent behavior.
- [ ] Old Project Manager-specific UI code is removed or isolated so it is not still reachable as a
      separate startup mode.
- [ ] Menus, shortcuts, and help text no longer refer to "Quit to Project Manager" as the primary
      concept.

### Testing

- [ ] Add C++ startup-routing tests for valid remembered project auto-open.
- [ ] Add C++ startup-routing tests for missing remembered project fallback.
- [ ] Add C++ tests for explicit invalid `--project` path behavior.
- [ ] Add tests for global known-project persistence.
- [ ] Add tests for recents ordering and last-project update.
- [ ] Add tests for missing-project marking/removal.
- [ ] Add tests for scan discovery deduplication.
- [ ] Add tests for tag add/remove persistence and cache refresh.
- [ ] Add tests for duplicate flow integration or extracted duplicate helper.
- [ ] Add editor automation coverage for opening the startup dialog in projectless mode.
- [ ] Add editor automation coverage for `Projects` create/open/recent interactions.
- [ ] Add editor automation coverage for `Manage` scan/filter/tags/duplicate/remove-missing.
- [ ] Add editor automation coverage for `About` version/copyright visibility.

## Reproduction steps

This is a feature request, not a bug. Current behavior:

1. Launch Foundry without an explicit project.
2. Foundry falls back to the separate Project Manager when no project is found.
3. Project creation/opening/management happens in that separate Project Manager surface.

Expected behavior after this issue:

1. Launch Foundry without an explicit project.
2. If a valid last project exists, Foundry opens it immediately.
3. Otherwise, Foundry opens the IDE shell projectless and shows the centered startup dialog.
4. Project creation/opening happens from `Projects`; maintenance happens from `Manage`.

## Minimal project, logs, or screenshots

No minimal project required. Visual mockups were created during design brainstorming under the local
`.superpowers/brainstorm/` session; the canonical design is captured in:

- `docs/superpowers/specs/2026-07-08-projectless-startup-dialog-design.md`
