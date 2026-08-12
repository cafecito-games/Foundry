# Cross-Pane Node Drop Lifetime Design

## Problem

A node reference dropped from a scene tree into a script editor in another workspace pane can synchronously focus or reveal workspace content while the mouse-release/drop stack still owns the receiving `CodeEdit`. The pure script-pane route currently defers content activation through `call_deferred()`, which can run while related GUI dispatch is still unwinding. Workspace activation may then unmount the active script surface and destroy the `CodeEdit` or its scrollbars before `Control::_call_gui_input()` and `Viewport` finish using their raw pointers.

The existing helper tests validate node-path construction and direct workspace focus, but do not execute the real `EditorNode` focus path, a real viewport drag/drop, or the lifetime of a live `CodeEdit`.

## Scope

The fix has two layers:

1. Prevent workspace script activation from replacing the drop target during input dispatch.
2. Harden the generic GUI dispatch boundaries that currently reuse a `Control *` after user-extensible callbacks.

The change will not replace every viewport GUI pointer with `ObjectID`, and it will not introduce global destruction deferral. Those broader alternatives require a separate architectural investigation.

## Workspace Focus Lifecycle

Workspace focus itself must land synchronously so the receiving pane becomes the focused leaf before the node reference is applied. Content activation, script reveal, and any mount/unmount work must wait until the next `process_frame`, matching the existing scene-tile activation boundary.

`EditorNode` will maintain a generation-checked pending script-leaf activation alongside its pending scene-tile activation. A new request supersedes an older request. The next-frame callback will resolve the active workspace and leaf by ID, verify that the request generation is current and that the leaf still hosts the expected script surface, then run `_complete_script_leaf_focus()`.

The pure script-pane drag path will synchronously call `set_focused_leaf()` and queue this next-frame activation. Non-drag focus remains synchronous. Mixed scene/script panes already route through the scene-tile next-frame queue; the end-to-end test will verify that activating their script resource tab also preserves the drop target.

## Generic GUI Dispatch Hardening

`Control::_call_gui_input()` will capture its own `ObjectID` before each user-extensible boundary. After emitting the `gui_input` signal and after invoking the script `_gui_input` virtual, it will resolve the identity through `ObjectDB`. If the control no longer exists, dispatch returns immediately without dereferencing the stale `this` pointer. If it still exists, subsequent state checks use the re-resolved pointer.

`Viewport::_gui_call_input()` will similarly capture the current control identity before calling `_call_gui_input()`. If the callback destroys the control, the viewport stops that propagation branch. It will not attempt to read the old mouse filter, tree state, transform, or parent chain because the hierarchy may have changed.

`Viewport::_gui_drop()` will validate the target identity after `can_drop_data()` and after `drop_data()`. Destruction during validation cancels that target and stops propagation. Destruction during an accepted `drop_data()` callback still counts as an accepted drop, but no target state is read afterward. Drag-state cleanup continues through `gui_perform_drop_at()` using viewport-owned state.

These guards contain invalid lifetime behavior; they do not make synchronous destruction during signal emission a recommended pattern. The workspace fix prevents that behavior in the reported workflow, while the engine guards prevent a stale-pointer crash if another callback violates the same lifetime assumption.

## End-to-End Workflow

A checked-in editor automation acceptance workflow will open a disposable project in a real editor subprocess and exercise input-routed pointer drags from the live scene tree to the live script `CodeEdit`.

It will cover:

- a scene pane and a separate pure script pane;
- a scene-capable mixed pane whose active resource tab is a script;
- same-scene insertion of the expected node reference;
- focus moving to the receiving pane;
- repeated drops followed immediately by another text-input event;
- cross-scene rejection without modifying the script;
- survival of the target editor identity through release and the following input;
- absence of new editor errors, object-destruction-during-signal warnings, hangs, and crashes.

The workflow result will report which layouts and repetitions completed so the subprocess test can assert observable behavior without inspecting source text.

## Focused Engine Tests

Focused C++ tests will drive real viewport input and drag/drop callbacks:

- a `gui_input` signal callback destroys the receiving control, and native/script input handling is not resumed on that identity;
- a control destroys itself during `_gui_input`, and viewport bubbling stops without reusing the old hierarchy;
- a drop target destroyed during `can_drop_data()` is not called again and the drop is unsuccessful;
- a target destroyed during an accepted `drop_data()` completes cleanup without another target dereference.

Tests will assert callback counts, drop success, identity invalidation, and clean dispatch completion. Existing drag/drop propagation tests remain the regression boundary for unchanged behavior when controls survive.

## Follow-Up Architecture Investigation

A separate issue under workspace epic #942 will compare:

1. converting viewport GUI focus, hover, drag, and propagation tracking to `ObjectID` with resolution at every use; and
2. introducing a dispatch-scoped mechanism that defers destruction until GUI callbacks unwind.

The investigation will audit all callback boundaries, prototype both approaches, measure complexity and input-path overhead, define destruction semantics, and recommend whether either approach should replace the targeted guards added here. It will reference #2131 but remain independent of this crash fix.
