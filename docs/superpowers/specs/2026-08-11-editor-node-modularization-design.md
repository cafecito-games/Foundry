# Modularize EditorNode into an Editor Shell and Application Controllers

## Summary

`EditorNode` is simultaneously the editor's root `Node`, UI builder, compatibility API, scene and resource workflow
owner, workspace coordinator, plugin manager, filesystem-change observer, command router, and startup composition root.
The result is a 12,425-line implementation, a 1,320-line header, 549 `EditorNode::` definitions, 179 direct
implementation includes, 155 allocations, and 106 signal connections. Across `editor/`, 188 files include
`editor/editor_node.h` directly.

This epic separates presentation from application policy without replacing Godot's object and signal model. The
long-term design keeps `EditorNode` as the engine lifecycle host, composition root, and compatibility facade while
moving state and behavior into focused components. The migration is incremental: every sub-issue is independently
mergeable, preserves observable behavior, and leaves compatibility forwarding in place until consumers migrate.

The work is deliberately not measured by lines removed. Moving the same coupling into helper files would not satisfy
the design. Completion is measured by clear ownership, directed dependencies, independent construction and behavior
tests, and preserved editor workflows.

## Context

### Responsibilities currently combined in EditorNode

The current class owns all of the following:

- root editor chrome, menus, dialogs, progress presentation, and theme application;
- scene creation, loading, activation, saving, closing, autosave, and recent-scene state;
- resource persistence, subresource traversal, previews, reload, and reimport preservation;
- scene contexts, selection, inspector history, viewport attachment, and editor-plugin state;
- boards, panes, tabs, tile focus, scene-mode hosting, dock binding, drag/drop, and workspace persistence;
- built-in, addon, and extension plugin registration, activation, edit routing, state, and teardown;
- filesystem, project-setting, translation, external-change, and import/reimport reactions;
- menu command routing, confirmations, restart, quit, run, export, and project switching;
- construction of core editor services and dependency-ordered startup phases.

The constructor alone is roughly 1,300 lines. Issue #2108 is already turning that construction into resumable,
measured startup work. This epic must build on that work rather than create a competing construction mechanism.

Recent board and tiled-workspace work also exposes a particularly coherent extraction seam. Context activation,
tile/leaf focus, dock binding, tab routing, board restoration, deferred focus, and workspace persistence are grouped in
`editor_node.cpp`, but their natural owner is a workspace application controller.

### Existing focused components to preserve

The editor already has useful focused units, including `EditorData`, `EditorSceneContext`, `EditorDockManager`,
`EditorMainScreen`, `EditorBoardStrip`, `EditorSceneWorkspace`, and `EditorLayoutStore`. This design does not replace
them. It removes editor-wide coordination from `EditorNode` and gives that coordination a named owner.

## Goals

- Separate root editor presentation from scene, workspace, plugin, filesystem, and command policy.
- Give every mutable editor-wide field exactly one owner.
- Preserve `EditorNode` and `EditorInterface` compatibility while internal consumers migrate.
- Make extracted components understandable, constructible, and testable without unrestricted access to
  `EditorNode`.
- Align construction with the resumable startup sequence introduced by #2108.
- Deliver the work as one GitHub epic with native sub-issues, explicit dependencies, and behavioral acceptance
  criteria.

## Non-goals

- Deleting `EditorNode` or changing its role as the root engine `Node`.
- Replacing Godot signals with a generic event bus, reducer store, or service locator.
- Redesigning editor visuals, menus, shortcuts, boards, panes, save prompts, or plugin APIs.
- Replacing `EditorData`'s internal model or changing resource and layout formats.
- Removing every editor singleton in the repository.
- Using line-count reduction as proof that the architecture improved.

## Approaches considered

### Mechanical extraction

Constructor blocks and method families could move into helper files while retaining access to all `EditorNode` state.
This reduces file size quickly but produces a distributed god object. It is acceptable only as a temporary movement
technique inside a sub-issue; it is not an acceptable final boundary.

### Domain controllers behind a compatibility facade

`EditorNode` remains stable while explicitly owned views, state, and controllers take over coherent responsibilities.
Views emit user intents, controllers execute workflows, and controllers expose results and state changes through typed
calls or signals. This follows existing editor conventions, permits incremental migration, and is the selected design.

### Central state store and event bus

A single editor state tree with commands and reducers would create strong conceptual separation, but it conflicts with
the mutable scene tree, engine objects, plugins, and existing signal-based APIs. Its migration risk outweighs the
benefit, so it is rejected.

## Target architecture

```text
EditorNode
|-- EditorStartupCoordinator
|-- EditorShell                     presentation
|-- EditorSession                   editor-wide state
|-- EditorSceneController           scene lifecycle and decisions
|   `-- EditorScenePersistence      load/save/reload/reimport mechanics
|-- EditorWorkspaceController       boards, panes, focus, context binding
|-- EditorPluginManager             plugin lifecycle and edit routing
|-- EditorProjectChangeCoordinator  filesystem and external changes
|-- EditorCommandController         command and confirmation policy
`-- existing focused components
    |-- EditorDockManager
    |-- EditorMainScreen
    |-- EditorBoardStrip
    |-- EditorSceneWorkspace
    `-- EditorLayoutStore
```

### EditorNode

`EditorNode` remains the composition root, lifecycle host, and compatibility facade. It may:

- own top-level component lifetimes and dependency-ordered startup wiring;
- receive engine notifications or input that must enter through the root node;
- forward retained public APIs to a controller, session, or view;
- adapt a compatibility return type without duplicating policy.

It must not retain business logic merely because callers still enter through `EditorNode`. Compatibility methods must
perform validation, delegation, and result adaptation only.

### EditorShell

`EditorShell` is a `Control` that owns root visual composition: title bar, menus, global dialogs, progress and warning
presentation, board-switcher and run-bar placement, and top-level workspace visibility. It exposes narrow presentation
methods and emits user intents.

The shell does not load or save resources, mutate `EditorData`, register plugins, activate scenes, or decide workspace
policy. It can be constructed and destroyed without an `EditorNode` singleton.

### EditorSession

`EditorSession` owns `EditorData`, the persistent no-scene context, the active scene context, active selection and
history references, and other authoritative editor-wide state. After the session extraction lands, controllers receive
an explicit `EditorSession &` and do not cache competing scene or focus state.

Moving `EditorData` happens late in the epic because its current address is exposed by compatibility getters. Those
getters remain and forward to the session.

### EditorWorkspaceController

The workspace controller owns application policy that connects session state to boards, workspaces, panes, scene
contexts, main screens, and per-leaf docks. It owns focus generations, pending board-close identity, restoration
brackets, tab routing, and workspace persistence coordination.

It consumes specific dependencies such as `EditorBoardStrip *` and `EditorMainScreen *`; it never receives an
unrestricted `EditorNode *`. Before `EditorSession` lands, it receives `EditorData &` plus narrow context accessors.
Sub-issue 9 replaces those transitional dependencies with `EditorSession &`.

### EditorSceneController and EditorScenePersistence

`EditorSceneController` owns user-visible scene workflows and their pending decisions. `EditorScenePersistence` owns
mechanical resource and scene IO, validation, preview generation, reload preparation, and reimport preservation.

Persistence returns structured outcomes. It never opens a dialog or chooses a workspace. The scene controller maps an
outcome to the next workflow state, while `EditorShell` presents the requested decision and reports the response.

### EditorPluginManager

The plugin manager owns registration, initialization order, addon enablement, extension plugins, active edit routing,
plugin state, and teardown. Plugin UI contributions are attached through explicit registration points; the manager
does not own shell controls.

### EditorProjectChangeCoordinator

The project-change coordinator owns subscriptions and policy for filesystem changes, imports, external modifications,
translation resources, and project-setting reloads. It delegates scene mechanics to the scene controller and
persistence service.

### EditorCommandController

The command controller owns dispatch and confirmation continuations. `EditorShell` owns menu labels, icons, ordering,
shortcuts, and dialog presentation. Existing `MenuOptions` values and `trigger_menu_option()` remain compatible.

## Dependency and data-flow rules

- There is one owner for each mutable state field.
- Extracted components may not keep an unrestricted `EditorNode *`, become friends solely to reach its internals, or
  use `EditorNode::get_singleton()` for their core work.
- Commands use direct typed calls. Notifications and presentation updates use signals where one-to-many delivery is
  appropriate.
- Views send intents to controllers and receive presentation state or outcomes. Views do not reach through the facade
  to perform business work.
- Controllers depend on the session, narrow collaborators, or existing focused managers. Controllers do not own
  presentation controls.
- No generic service locator or global event bus is introduced.
- Internal consumers migrate to `EditorInterface`, the session, a controller, or an existing focused manager when
  they do not require the compatibility facade.

Typical command flow:

```text
menu/button intent
    -> EditorCommandController
    -> domain controller or service
    -> structured result / pending decision
    -> EditorShell presentation
    -> user response, when required
    -> controller continuation
    -> EditorSession mutation and signals
    -> shell/workspace refresh
```

## Error and cancellation handling

- Persistence errors are structured values containing the operation, path, error code, and required decision type.
- A failed save must not mark a scene saved, replace its path, or clear unsaved state.
- A failed load must not partially insert a scene into the session.
- Save, close, board-close, quit, and external-change decisions are explicit controller continuations. Canceling a
  decision leaves the affected scene, board, focus, and unsaved state intact.
- Deferred focus and close operations carry stable identity plus a generation where required; stale continuations are
  ignored safely.
- Component teardown disconnects subscriptions before dependencies are destroyed. No queued callback may target a
  destroyed controller.
- Startup failure and cancellation semantics remain those established by #2108. This epic must not reintroduce
  partially constructed access or duplicate phase execution.

## Migration strategy

### Phase 0: contracts and characterization

Each sub-issue begins by identifying the observable behavior it moves and adding missing characterization coverage.
Tests execute the behavior; they do not assert on source text or documentation. No general framework is introduced in
advance of a concrete extraction.

### Phase 1: shell and startup composition

After #2108, extract `EditorShell` from the measured main-chrome, dock, bottom-panel, and finalization work without
changing startup order. `EditorNode` initially continues to handle intents through forwarding callbacks.

### Phase 2: workspace coordination

Extract focus and context activation first, then board lifecycle, tab routing, and persistence. This is the first
business-logic migration because the seam is coherent and under active development.

### Phase 3: scene persistence and lifecycle

Separate mechanical IO from scene workflow policy, then route workspace operations through the scene controller.

### Phase 4: plugin, project-change, and command workflows

Move plugin lifecycle, filesystem subscriptions, external-change decisions, and menu/confirmation policy to their
focused owners.

### Phase 5: session ownership and facade contraction

Move authoritative state into `EditorSession`, migrate internal consumers, audit remaining direct includes, and leave
only justified compatibility forwarding in `EditorNode`.

Every phase is mergeable on its own. The sub-issues use a serial landing order because they intentionally move state
out of the same header and implementation; parallel edits would create conflicts and obscure ownership transitions.

## Testing strategy

- Focused C++ tests construct extracted components without an `EditorNode` when the component contract permits it.
- Controller tests assert state transitions, emitted outcomes, ordering, cancellation, and stale-continuation handling.
- Persistence tests perform real round trips in the shared test scratch space.
- Existing scene-context, dock-binding, board, workspace, automation, projectless, startup, and editor workflow tests
  remain regression coverage.
- Editor-facing workflows use the existing automation layer and a real editor subprocess where unit tests cannot
  establish the integration contract.
- GUI-dependent verification runs with `DISPLAY=:1` on Linux so acceptance tests do not self-skip.
- Before each PR handoff, use the strict native wrapper build required by `AGENTS.md`. The epic closes only after the
  full test suite reports `[doctest] Status: SUCCESS!`.

### Common sub-issue verification contract

Every implementation sub-issue must run the strict native build before handoff:

```sh
python3 scripts/agent_build.py
```

Focused iteration may use the Ninja backend, but it does not replace that native validation. Test invocations use the
command-first CLI and `--case` for case-name patterns. When a focused case launches an editor subprocess on Linux, it
must run with `DISPLAY=:1`. Each PR records the exact binary, filters, and results it used; it does not claim coverage
from a test that self-skipped.

The expected primary code areas and minimum focused coverage are:

| Sub-issue | Expected primary code areas | Minimum focused coverage |
| --- | --- | --- |
| 1 | `editor/editor_node.*`, new `editor/gui/editor_shell.*`, editor build registration, shell tests | `*ProjectlessShell*`, `*EditorAutomation*` smoke/snapshot cases, and #2108 startup equivalence cases |
| 2 | `editor/editor_node.*`, new `editor/editor_workspace_controller.*`, scene-context/workspace integration tests | `*EditorSceneContext*`, `*Scene contexts resolve*`, `*perscene-docks-bind*`, `*Scene routing*`, and `*3D preview*` |
| 3 | workspace controller, `editor_board*`, `editor_scene_workspace*`, board/workspace tests | `*[Editor][Boards]*`, cross-board workflow subprocesses, and `*mixed-workspace-restart-restore*` |
| 4 | `editor/editor_node.*`, new `editor/scene/editor_scene_persistence.*`, persistence/reimport tests | new persistence round trips plus packed-scene, built-in-resource, and representative import/export cases |
| 5 | `editor/editor_node.*`, new `editor/scene/editor_scene_controller.*`, workflow tests | `*basic scene-editing*`, `*close-last-scene*`, `*mixed-workspace*`, `*Scene routing*`, and `*ProjectlessShell*` load cases |
| 6 | `editor/editor_node.*`, `editor/editor_data.*`, new `editor/plugins/editor_plugin_manager.*`, plugin tests | new plugin lifecycle/routing cases plus extension reload and launched-editor smoke coverage |
| 7 | `editor/editor_node.*`, `editor/file_system/*`, new project-change coordinator, reload/reimport tests | `*EditorFileSystem*`, new external-change/reimport cases, and projectless in-process-load cases |
| 8 | `editor/editor_node.*`, new `editor/editor_command_controller.*`, `editor/gui/editor_shell.*`, command tests | command-palette, menu-driver, projectless restriction, confirmation, run/export, and quit workflow cases |
| 9 | `editor/editor_node.*`, new `editor/editor_session.*`, `editor/editor_data.*`, context tests | `*EditorSceneContext*`, dock binding, board routing, restart restore, and projectless transition cases |
| 10 | facade and all migrated consumers, final responsibility audit, end-to-end workflow | strict native build, full suite, and the final projectless-to-quit acceptance workflow with GUI coverage enabled |

Names in the expected-area column establish ownership and location intent, not a prohibition against touching required
adjacent files. Each issue body will repeat its relevant row so it remains actionable outside the epic.

## GitHub epic structure

The parent receives the `epic` and `area:editor` labels. The ten issues below are attached through GitHub's native
`subIssues` relationship and are also listed in the parent body for readable sequencing. Each issue body contains its
problem, required design, non-goals, dependencies, observable invariants, acceptance criteria, tests, expected code
areas, and verification commands.

### Parent epic acceptance criteria

- [ ] All ten native sub-issues are closed.
- [ ] `EditorNode` is limited to engine lifecycle integration, component composition, and compatibility forwarding.
- [ ] Root visual construction and presentation behavior live in `EditorShell`.
- [ ] Workspace, scene, persistence, plugin, project-change, and command policy each have one named owner.
- [ ] `EditorData`, active contexts, selection, and history are owned by `EditorSession`.
- [ ] Extracted components do not retain an unrestricted `EditorNode *`, use friendship to reach its internals, or call
      `EditorNode::get_singleton()` for their core work.
- [ ] Commands flow from views to controllers; state and results flow back through typed calls or signals.
- [ ] Existing `EditorNode` and `EditorInterface` behavior used by plugins remains compatible unless a child documents
      an approved API change.
- [ ] Remaining internal `editor_node.h` consumers are audited and callers needing only a focused dependency migrate.
- [ ] Projectless startup, in-process project loading, ordinary startup, scene editing, multi-board workspaces,
      save/reload, run/export, plugin registration, and shutdown continue to work.
- [ ] The strict native validation build passes.
- [ ] The full C++ and Foundry Script suite reports `[doctest] Status: SUCCESS!`, including GUI-dependent tests with a
      display.

## Native sub-issues

### 1. Extract the editor shell view from EditorNode

Depends on #2108.

Acceptance criteria:

- [ ] `EditorShell` owns the root layout, title-bar placement, menus, global dialogs, progress presentation, and
      top-level visibility state.
- [ ] `EditorNode` no longer individually allocates or destroys those controls.
- [ ] The shell exposes narrow presentation methods and emits user intents; it does not load/save resources, mutate
      `EditorData`, manage plugins, or make scene/workspace decisions.
- [ ] `EditorShell` constructs and destroys in a focused test without an `EditorNode` singleton.
- [ ] Existing menu IDs, shortcuts, accessibility names, modal behavior, and command-palette entries remain stable.
- [ ] Projectless and project-backed shells produce the same observable UI states as before.
- [ ] The startup phase log, plugin construction order, and synchronous/deferred equivalence from #2108 remain
      unchanged.
- [ ] Existing automation snapshot, menu interaction, projectless-shell, and launched-editor smoke tests pass.
- [ ] New tests prove representative shell actions emit exactly one intent and presentation updates do not invoke
      business operations.

Non-goal: changing layout, styling, menu content, or startup performance.

### 2. Extract workspace focus and scene-context coordination

Depends on sub-issue 1.

Acceptance criteria:

- [ ] `EditorWorkspaceController` owns active context attachment, tile/leaf focus, scene-mode hosting, viewport
      attachment, and per-leaf dock binding.
- [ ] Pending focus identity and generations move out of `EditorNode`.
- [ ] The controller receives explicit dependencies and has no unrestricted `EditorNode *`.
- [ ] Focusing a scene tile activates its scene, selection, history, docks, and presentation exactly once.
- [ ] Focusing a script/help/text leaf does not corrupt the last active scene context.
- [ ] Dormant-board leaves cannot claim editor-wide focus.
- [ ] Deferred focus cannot activate a deleted or superseded leaf.
- [ ] Context teardown detaches every bound viewport and dock without use-after-free.
- [ ] Existing scene-context, dock-binding, scene-routing, board-switch, passive-preview, and 3D-board tests pass.
- [ ] New focused tests compare the observable focus/context transition log before and after extraction.

Non-goal: changing board lifecycle, tab-drop rules, or persistence format.

### 3. Move board lifecycle and workspace persistence into the workspace controller

Depends on sub-issue 2.

Acceptance criteria:

- [ ] The controller owns board activation, requested-board focus, close queues, restoration brackets, deferred close
      completion, tab routing, cross-pane/cross-board drops, and workspace save/restore coordination.
- [ ] Board-restoration and pending-board-close mutable state leave `EditorNode`.
- [ ] Closing a board with unsaved scenes uses the standard scene-close workflow and remains atomically cancelable.
- [ ] An interleaved board removal cannot redirect a pending close to the wrong board.
- [ ] Restoring multiple boards cannot allow a dormant board to steal focus.
- [ ] Scene, script, help, and text tabs retain established move and reorder behavior.
- [ ] Board and workspace persistence remain backward compatible; existing keys are not renamed or reinterpreted.
- [ ] Existing board-strip, persistence, overview, cross-board-drop, routing, and mixed-workspace restart tests pass.
- [ ] New tests cover stale focus, canceled unsaved-board closure, and invalid persisted focus.

Non-goal: redesigning boards, panes, or their presentation.

### 4. Extract scene and resource persistence mechanics

Depends on sub-issue 3 for landing order.

Acceptance criteria:

- [ ] `EditorScenePersistence` owns validation, resource traversal, previews, save/load mechanics, external-resource
      saves, reload preparation, and reimport-preservation helpers.
- [ ] Persistence returns structured outcomes for success, path/error details, dependency failure, imported-scene
      restrictions, and required decisions.
- [ ] Persistence never opens dialogs, changes menus, selects workspace tabs, or mutates view controls.
- [ ] Focused save/load round trips execute without an `EditorNode` singleton.
- [ ] Round trips preserve ownership, subresources, built-in identifiers, editable-instance state, and editor metadata.
- [ ] Failed saves do not mark scenes saved, replace paths, or lose unsaved state.
- [ ] Persistence failure returns before the scene controller mutates `EditorData`; an integration test proves a
      failed load leaves the edited-scene collection unchanged.
- [ ] Reimport preparation and restoration preserve modified properties and node references.
- [ ] Generated test files use the shared test scratch space.
- [ ] New tests cover successful round trips, validation failure, missing dependencies, imported-scene outcomes,
      external-resource failure, and reimport restoration.
- [ ] Existing packed-scene, built-in-resource, scene-context, and representative import/export tests pass.

Non-goal: changing resource formats, serialization, or user-facing error text.

### 5. Extract scene lifecycle workflows

Depends on sub-issue 4.

Acceptance criteria:

- [ ] `EditorSceneController` owns new/open/close/activate, instantiation, recent/previous scenes, unsaved queues,
      autosave/save-before-run, and scene-related continuation state.
- [ ] It coordinates persistence and workspaces through public contracts rather than private state.
- [ ] UI decisions are explicit pending requests; dialogs present requests and return a response.
- [ ] Canceling a save/close prompt leaves the relevant scene open, selected, and unsaved.
- [ ] Save, discard, save-as, close-one, close-all, close-board, quit, and run-before-save retain existing behavior.
- [ ] Opening an existing scene reveals its owning tile according to current option semantics.
- [ ] Failed loads and canceled imported-scene warnings leave ordering, ownership, focus, and history unchanged.
- [ ] Scene signals emit once and in the current order.
- [ ] Existing basic editing, close-last-scene, mixed-workspace, routing, and projectless load workflows pass.
- [ ] New tests drive the full decision matrix with a fake presentation responder and assert final session/workspace
      state.

Non-goal: changing save prompts, autosave policy, recent-scene limits, or scene-tab UX.

### 6. Extract plugin lifecycle and edit routing

Depends on sub-issue 5 for landing order.

Acceptance criteria:

- [ ] `EditorPluginManager` owns built-in/extension registration, addon enablement, activation, edit routing, plugin
      state, and teardown.
- [ ] Registration and initialization remain observably ordered through a lifecycle log.
- [ ] The manager does not own shell controls; contributions attach through explicit registration points.
- [ ] Enabling a valid addon registers it once; disabling removes it once and persists expected config state.
- [ ] Failed addon activation leaves no half-registered plugin or incorrect enabled entry.
- [ ] Object editing selects the same main editor and sub-editors, including script/text routing.
- [ ] Extension reload leaves no stale active-plugin entries.
- [ ] Shutdown removes plugins safely and delivers no callbacks after teardown.
- [ ] Tests cover ordering, duplicate prevention, failed activation, routing, state round trips, reload, and teardown.
- [ ] Existing plugin, extension, and full editor automation tests pass.

Non-goal: changing the `EditorPlugin` API or discovery format.

### 7. Extract project and filesystem change coordination

Depends on sub-issue 6 for landing order and sub-issue 4 functionally.

Acceptance criteria:

- [ ] `EditorProjectChangeCoordinator` owns filesystem subscriptions, source changes, resource reloads, reimport
      transitions, external modifications, translation tracking, and project-settings reload decisions.
- [ ] Subscription setup and teardown happen exactly once and leave no callbacks targeting destroyed objects.
- [ ] Clean filesystem polls do not produce dialogs or mutate scenes.
- [ ] Changed resources refresh only affected state and preserve unsaved edits according to current behavior.
- [ ] External scene changes offer existing reload/resave choices and honor cancellation atomically.
- [ ] Project-setting changes refresh affected services without duplicate reloads.
- [ ] Translation updates remain coalesced and propagate once per queued change set.
- [ ] Reimport callbacks use public scene persistence/controller contracts.
- [ ] Tests cover clean polls, affected/unaffected resources, external decisions, translation coalescing, settings reload,
      and destruction with queued work.
- [ ] Existing filesystem, reimport, reload, and projectless/in-process-load tests pass.

Non-goal: changing import scheduling, filesystem scanning, or reload policy.

### 8. Separate command policy from menus and dialogs

Depends on sub-issues 1 through 7.

Acceptance criteria:

- [ ] `EditorCommandController` owns dispatch and confirmation continuations; `EditorShell` owns presentation only.
- [ ] Existing `MenuOptions` numeric values and `trigger_menu_option()` behavior remain compatible.
- [ ] `_menu_option_confirm()` is removed or reduced to compatibility forwarding.
- [ ] Every command has one authoritative handler and cannot execute twice through overlapping signals.
- [ ] Projectless-unavailable commands remain unavailable through every menu, palette, and direct route.
- [ ] Commands emit structured success, cancellation, and error outcomes suitable for automation.
- [ ] Representative scene, project, editor, tools, layout, run/export, and quit commands retain current behavior.
- [ ] Menu construction contains presentation data but no resource, scene, or plugin policy.
- [ ] Tests cover lookup, disabled commands, confirmation/cancellation, duplicate prevention, and forwarding.
- [ ] Existing command-palette, menu automation, projectless, run/export, and workflow tests pass.

Non-goal: renaming commands, reorganizing menus, or changing shortcuts.

### 9. Move mutable editor-wide state into EditorSession

Depends on sub-issue 8.

Acceptance criteria:

- [ ] `EditorSession` owns `EditorData`, no-scene/active contexts, active selection/history, and related state.
- [ ] Every moved mutable field has exactly one owner.
- [ ] Existing data, selection/history, and scene getters forward to the session.
- [ ] Controllers receive `EditorSession &` and do not cache duplicate authoritative state.
- [ ] Session construction and destruction work without a full `EditorNode`.
- [ ] Active-context changes update selection, history, viewport, and scene roots atomically to observers.
- [ ] A no-scene session always provides valid empty selection and history state.
- [ ] Scene removal, reorder, reload, and board reassignment preserve context identity and tile ownership.
- [ ] Repeated projectless-to-project transitions leak no state between projects.
- [ ] Existing `EditorData`, context, dock-binding, routing, restore, and projectless tests pass.
- [ ] New tests cover lifetime, no-scene transitions, reorder/removal, and observer ordering.

Non-goal: replacing `EditorData` internals or changing persisted scene identities.

### 10. Contract the EditorNode facade and migrate internal consumers

Depends on sub-issue 9.

Acceptance criteria:

- [ ] `EditorNode` contains only root engine routing, composition/startup wiring, compatibility methods, and genuinely
      cross-component coordination.
- [ ] Every remaining nontrivial method is assigned to `EditorNode` or a component with a documented reason.
- [ ] Internal callers use `EditorInterface`, `EditorSession`, a controller/manager, or an existing focused component
      when they do not need the facade.
- [ ] No new internal `EditorNode::get_singleton()` dependency is introduced during the epic.
- [ ] Remaining direct `editor_node.h` consumers are inventoried in the PR and each needs a retained facade operation.
- [ ] Compatibility methods contain only validation, delegation, and result adaptation.
- [ ] Ownership and shutdown order are explicit and leak-free.
- [ ] Retained public/plugin behavior is documented near the facade API.
- [ ] Strict native build and full test suite pass.
- [ ] GUI-dependent workflows run with a display and do not self-skip.
- [ ] A final projectless start, in-process project load, multi-scene edit, save, run, close, and quit workflow passes.
- [ ] The PR records the final responsibility map and links separately approved follow-ups for deferred work.

Non-goal: deleting `EditorNode`, breaking plugin compatibility, or converting every editor singleton.

## Epic sequencing

```text
#2108 resumable startup
    -> 1 EditorShell
    -> 2 Workspace focus/context
    -> 3 Boards/layout/tab routing
    -> 4 Scene persistence
    -> 5 Scene lifecycle
    -> 6 Plugin manager
    -> 7 Project-change coordinator
    -> 8 Command boundary
    -> 9 EditorSession ownership
    -> 10 Facade cleanup
```

Although some domains are conceptually independent, the serial native dependency chain is intentional. Every issue
moves fields and methods out of the same two files. Serial landing keeps ownership changes reviewable and avoids
parallel worktrees repeatedly resolving the same header and constructor conflicts.

## Definition of done

The epic is complete when every native sub-issue is closed, all parent acceptance criteria pass, and the final
responsibility audit shows that `EditorNode` is a lifecycle/composition/compatibility boundary rather than the owner of
editor presentation and business workflows. A smaller file alone is not completion.
