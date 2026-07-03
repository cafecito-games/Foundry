# Editor Agent Automation Design

**Date:** 2026-07-03
**Status:** Approved

## Problem

AI agents can operate the editor today only through expensive and brittle image-processing workflows: screenshot, infer button positions, click pixels, and hope the UI state changed as expected. That is not good enough for day-to-day editor control or workflow regression testing.

The desired system should let an agent fully control the editor without image processing while still exercising the same editor paths a real user exercises. Direct scene or resource mutation is useful for setup and assertions, but it must not be the normal way an agent performs user workflows. The automation layer should catch editor bugs in focus handling, dialogs, validation, undo/redo, inspector refresh, filesystem scans, script editor behavior, and plugin UI code.

## Goals

- Provide a local/test-only editor automation backend that is disabled by default and enabled explicitly at editor startup.
- Let agents observe and operate visible editor UI through semantic metadata: role, name, text, state, focus, bounds, actions, and hierarchy.
- Use a hybrid action model: semantic actions when they preserve user-path behavior, real input events when event routing, focus, viewport picking, shortcuts, drag/drop, or text entry fidelity matters.
- Expose the first client surface as an MCP server so external AI agents can connect with standard tooling.
- Keep the core editor automation backend independent of MCP so future workflow tests can use the same behavior directly.
- Make waits, failures, and diagnostics first-class so agents can detect bugs without relying on screenshots.

## Non-goals

- No remote or production automation mode. The feature is for trusted local developer and test environments only.
- No project setting that silently enables automation. Startup opt-in is required.
- No broad direct-mutation API as the primary automation path. Direct editor-state helpers are limited to setup, assertions, and narrow escape hatches.
- No requirement that the first version covers every editor control. The long-term model is generic full-editor control, but the MVP focuses on high-value common workflows.
- No image-processing dependency. Screenshots may be exposed only as optional failure diagnostics.

## Design

### 1. Scope

The long-term target is generic semantic control of the editor. Every visible `Control` should eventually be discoverable and operable by role, name, state, and hierarchy instead of by pixels.

The MVP covers the highest-value workflows first:

- command palette
- modal dialogs and popups
- scene tree selection and common node operations
- inspector property editing
- filesystem dock file selection and basic operations
- script editor open, save, and navigation
- play, stop, and run-current-scene operations
- editor log and error readback
- generic actions for common controls such as buttons, line edits, trees, item lists, menus, tabs, check boxes, spin boxes, text edits, and code edits

Each workflow improves the same generic automation layer. One-off editor APIs should be avoided unless they are setup or assertion helpers.

### 2. Architecture

The automation system is editor-native, with MCP as the first transport.

`EditorAutomationCore` owns the semantic model. It walks visible editor windows and popups, captures element metadata, assigns opaque snapshot handles, resolves selectors, tracks modal ownership, reports focus, and exposes current editor state.

`EditorAutomationDriver` performs actions. It chooses semantic control actions when available and real synthesized input events when fidelity requires them. Every action result reports the route used, such as `semantic_click`, `accessibility_action`, `control_method`, `input_mouse_click`, `input_key`, `input_drag`, or `command_palette`.

`EditorAutomationMCPServer` exposes the core to external AI agents. It contains transport, tool schemas, session handling, and result formatting only. It should not contain editor-specific workflow logic.

`EditorWorkflowTestDriver` is a later direct test-facing client. It uses `EditorAutomationCore` and `EditorAutomationDriver` without MCP so native tests can validate editor workflows deterministically.

### 3. Activation and safety

Automation is disabled by default. It starts only through an explicit command-first editor launch option, for example:

```bash
./bin/foundry.linuxbsd.editor.dev.x86_64 editor open --project <project> --automation --automation-transport=mcp
```

The backend starts after the editor UI is initialized. For MCP, the default transport binds only to `127.0.0.1` or another local-only endpoint and requires a launch-generated session token or nonce. The editor prints the endpoint and token to stdout for the launching test harness or agent host.

The editor should show a visible "Automation Active" indicator in dev/test builds while automation is enabled. This is not a security boundary; it is an explicit signal that the editor is under tool control.

Streamable HTTP is the preferred MCP transport for a running GUI editor because the editor process owns the server. Stdio is less suitable when the host does not launch the editor as a subprocess dedicated to MCP. The implementation keeps the transport boundary clean so future local transports or stdio bridges can reuse the same automation core without changing editor workflow logic.

### 4. MCP surface

The MCP server exposes a small set of generic tools:

- `observe_ui`: returns windows, focused element, modal stack, and the visible semantic tree.
- `find_elements`: resolves selectors by role, name, text, class, path, state, and containment.
- `act`: performs actions such as `click`, `focus`, `type_text`, `set_text`, `press_key`, `select`, `expand`, `collapse`, `drag`, and `choose_menu_item`.
- `wait_for`: waits for selectors, focus changes, text changes, modal transitions, idle frames, filesystem/import/script-analysis idle states, or new log entries.
- `read_editor_state`: returns selected nodes, open scenes, active scene, current script, playing state, and unsaved state.
- `read_editor_log`: returns editor log entries, warnings, errors, and recent action-related messages.
- `run_command`: executes command palette commands by key or name as a reliable bridge into existing editor actions.

Read-only MCP resources expose snapshots that are useful as context:

- `foundry://ui/tree`
- `foundry://editor/state`
- `foundry://editor/log`
- `foundry://scene/active`

MCP tools are the agent-facing API. Resources are for context and assertions, not mutation.

### 5. Element model

Elements are returned as structured records:

```json
{
  "id": "snapshot:42/control:284",
  "role": "button",
  "name": "Add Child Node",
  "text": "",
  "class": "Button",
  "path": "/root/EditorNode/.../Button",
  "visible": true,
  "enabled": true,
  "focused": false,
  "pressed": false,
  "selected": false,
  "bounds": [120, 44, 32, 32],
  "actions": ["focus", "click"],
  "children": []
}
```

Element IDs are opaque handles for a specific UI snapshot or generation. Agents should prefer selectors over hardcoded IDs:

```json
{
  "role": "button",
  "name": "Add Child Node",
  "within": { "role": "dock", "name": "Scene" }
}
```

Selectors may match zero, one, or many elements. Ambiguous selectors fail with diagnostics instead of choosing arbitrarily.

### 6. Action model

Actions return structured outcomes:

```json
{
  "ok": true,
  "route": "semantic_click",
  "events": ["pressed", "confirmed_dialog_opened"],
  "focus": "snapshot:43/control:91"
}
```

Semantic actions should route through the same control paths used by accessibility or user-facing UI behavior where possible. Input actions should synthesize normal editor input events and pass through viewport/focus routing. The agent can request strict input mode for individual steps when it wants to test event handling rather than simply accomplish the operation.

Direct editor-state mutation is excluded from normal `act` behavior. If direct helpers are added, they should be clearly named as setup or assertion tools.

### 7. Waiting and reliability

The backend should be wait-oriented. Actions and tests should not depend on arbitrary sleeps.

Supported wait conditions include:

- next process frame
- editor idle frame
- modal stack changed or settled
- selector appeared, disappeared, or changed
- focus changed
- filesystem scan idle
- import or reload idle
- script analysis or LSP idle
- no new editor errors after an action
- editor log contains a matching entry

Failures are first-class results:

```json
{
  "ok": false,
  "kind": "ambiguous_selector",
  "matches": []
}
```

```json
{
  "ok": false,
  "kind": "editor_error_after_action",
  "messages": []
}
```

Diagnostics should include the focused element, modal stack, selected nodes or resources, active scene or script, relevant editor log entries, the last action trace, and the visible UI subtree around a failed selector. Screenshots are optional failure attachments, not required context.

### 8. Rollout

Phase 1: implement the core observe/act loop for common controls: `Button`, `LineEdit`, `TextEdit`, `CodeEdit`, `Tree`, `ItemList`, `OptionButton`, `PopupMenu`, `CheckBox`, `SpinBox`, dialogs, tabs, and command palette.

Phase 2: add workflow affordances for the scene tree, inspector, filesystem dock, script editor, play/stop, editor log readback, and robust waits.

Phase 3: add higher-fidelity input paths for drag/drop, viewport clicks, shortcuts, text composition, context menus, and multi-window behavior.

Phase 4: add the native workflow test driver on top of the same backend.

## Testing

Automated coverage should start with C++ tests for selector resolution, element snapshot semantics, action dispatch routing, ambiguity errors, and wait conditions. These tests can run without full editor workflows where possible.

Editor workflow tests should launch the editor with the automation flag in a local/test environment and use the same automation backend that agents use. The MVP acceptance workflow is:

1. Launch the editor with automation enabled.
2. Open a test project.
3. Create or open a scene.
4. Add a node through the Create Node dialog.
5. Edit an inspector property through the inspector UI.
6. Save the scene.
7. Run the current scene.
8. Assert selected node, active scene, unsaved state, undo stack label, and no new editor errors.

Manual validation remains useful for early development, especially around focus, modal behavior, and drag/drop, but the goal is to turn stable workflows into automated tests.

## References

- MCP transports: https://modelcontextprotocol.io/specification/2025-11-25/basic/transports
- MCP tools: https://modelcontextprotocol.io/specification/2025-11-25/server/tools
