# Code Review 1

**Worktree:** `/workspace`
**Diff range:** `6205d6875bf467e11e5c6b85c70fa4c438b8ce61...HEAD` (single commit `79fbafa009 Add set_canvas_2d_zoom automation and truthful shortcut dispatch`; working tree is clean, no staged/unstaged/untracked changes)
**Date:** 2026-08-10

## Summary

The change adds a selectorless `set_canvas_2d_zoom` act action with an EDSCALE-normalized zoom contract, makes standalone-shortcut `run_command` report `shortcut_unhandled` when nothing consumed the dispatched `InputEventShortcut`, and adds unit + acceptance coverage. The core mechanics are sound: validation happens before any mutation, the normalized conversion matches `CanvasItemEditorSceneGeometryState::to_dict()` so `read_editor_state().view_2d.zoom` and `act(set_canvas_2d_zoom)` agree, and view targeting resolves to the tile that actually hosts the scene mode.

Two things need attention before merge. First, satisfying "success fields at the top level" was implemented by promoting **every** `details` key of **every** action into the top-level result dictionary (`editor_automation_action.cpp:74`), which silently rewrites the public MCP contract for all actions and is not reflected in the `act` output schema. Second, the truthfulness fix has a false-negative mode (handlers that act without accepting the event) and a stale-flag mode (`push_input` early-returns) that are not guarded. Test coverage for the new code paths is thinner than the test list suggests: the two headline canvas tests exercise pre-existing pure math, not the new `apply_absolute_zoom_at_center` / `_action_set_canvas_2d_zoom` code.

## Findings

### Critical

**C1. `details` promotion silently rewrites the result contract for every action, and the promoted fields are undeclared in the schema.**
`editor/automation/editor_automation_action.cpp:74-85`

```cpp
if (!details.is_empty()) {
    const Array keys = details.keys();
    for (int i = 0; i < keys.size(); i++) {
        const Variant key = keys[i];
        if (!dict.has(key)) {
            dict[key] = details[key];
        }
    }
    dict["details"] = details;
}
```

This is a global change to `EditorAutomationActionResult::to_dictionary()`, applied so that one action's four fields land at the top level. Concrete consequences:

- Every existing action's diagnostics now also appear top-level and duplicated. From `editor_automation_driver.cpp` alone: `snapshot_generation`, `reconciled`, `requested_reference`, `current_element_id` (lines 106-135), `source_element`, `target_element`, `focused_element_id`, `modal_stack`, `reason` (lines 192-200), `target_window_object_id` (230), `submitted_text` (989), `scrolled` (1109), `dragging`/`button_held` (1343-1344), `holding`, `target_tile_id`, `region`, `source_tile_id`, `source_tab` (1409-1491), `board_index`/`board_title`/`board_id`/`overview_active`/`active_board` (1510-1535). Drag/selector failure payloads now carry a duplicated `modal_stack` and duplicated element summaries in the same response.
- `reason` is a particularly bad promotion: `_action_failure_with_context` sets `details["reason"]` as a *diagnostic* string, and it now sits next to `message` at the top level as if it were part of the stable contract.
- None of the promoted keys are declared in the `act` output schema. `_ok_result_schema()` (`editor_automation_mcp_contracts.cpp:340-345`) declares only `ok`, and `_add_failure_details_output_props()` adds only `details`. An MCP client that validates `structuredContent` against the advertised `outputSchema` sees `changed`, `effective_zoom`, `requested_zoom`, `tile_id`, `minimum`, `maximum` as undeclared, and cannot discover them from `tools/list` at all.

On the question the task raises explicitly (top-level vs nested): top-level is the right shape for an agent-facing success payload, but it should be opt-in per action and declared, not a blanket merge in the shared serializer. Recommended fix:

1. Revert `to_dictionary()` to the previous behaviour.
2. Give `EditorAutomationActionResult` an explicit `Dictionary public_fields` (or an allow-list the action populates), merged at the top level, keeping `details` for diagnostics only.
3. Declare the new keys on the `act` output schema alongside `ok`/`details`, e.g.:

```cpp
Ref<EditorAutomationMCPJsonSchema> act_output = _ok_result_schema();
act_output->add_property("changed", EditorAutomationMCPJsonSchema::boolean("Whether set_canvas_2d_zoom changed the view zoom."));
act_output->add_property("requested_zoom", EditorAutomationMCPJsonSchema::number("Normalized zoom requested by set_canvas_2d_zoom."));
act_output->add_property("effective_zoom", EditorAutomationMCPJsonSchema::number("Normalized zoom in effect after set_canvas_2d_zoom."));
act_output->add_property("tile_id", EditorAutomationMCPJsonSchema::integer("Tile whose 2D canvas view was targeted."));
_add_failure_details_output_props(act_output);
```

If instead you prefer the simplest correct change, drop the promotion entirely and have the acceptance workflow read `zoom_result["details"]["effective_zoom"]`; nothing else in the tree depends on the promoted shape.

Mitigating note (verified, not a defect): failure screenshots are *not* duplicated, because `EditorAutomationMCPDispatcher::_enrich_action_failure()` replaces `result["details"]` wholesale after `to_dictionary()` has run (`editor_automation_mcp_dispatcher.cpp:719-721`).

### High

**H2. `push_shortcut_event()` can return a stale handled flag when `push_input()` bails out early.**
`editor/automation/editor_automation_commands.cpp:90-98`

```cpp
p_viewport->push_input(ev, false);
return p_viewport->is_input_handled();
```

`Viewport::push_input()` clears `local_input_handled` only *after* three early returns (`viewport.cpp:3461-3474`): `ERR_FAIL_COND(!is_inside_tree())`, `disable_input || disable_input_override`, and the editor-hint/edited-scene-root guard. On any of those, `is_input_handled()` returns whatever a previous real user input left behind, so `run_command` can report `ok:true, route:"shortcut"` for an event that was never delivered. That is the exact class of dishonesty this change exists to remove.

Guard the precondition explicitly:

```cpp
bool EditorAutomationCommands::push_shortcut_event(Viewport *p_viewport, const Ref<Shortcut> &p_shortcut) {
    ERR_FAIL_NULL_V(p_viewport, false);
    ERR_FAIL_COND_V(p_shortcut.is_null(), false);
    ERR_FAIL_COND_V_MSG(!p_viewport->is_inside_tree(), false, "Cannot dispatch a shortcut through a viewport outside the tree.");
    ...
}
```

and consider returning a tri-state (`dispatched`/`handled`) so `execute()` can distinguish "we could not dispatch" (`unavailable`) from "we dispatched and nothing handled it" (`shortcut_unhandled`).

**H3. False negatives: shortcut handlers that act without accepting the event will now report `shortcut_unhandled`.**
`editor/automation/editor_automation_commands.cpp:365-372`

`is_input_handled()` is only true if some node called `accept_event()`/`set_input_as_handled()`. That is not a universal convention in this tree. Both of these run their action and never accept:

- `editor/gui/window_wrapper.cpp:141-145` — `WindowWrapper::shortcut_input()` calls `set_window_enabled(true)` and returns.
- `editor/shader/shader_editor_plugin.cpp:828-836` — `ShaderEditorPlugin::shortcut_input()` calls `make_dock_floating()` and returns.

Neither is reachable through the standalone-shortcut route today (the `WindowWrapper` shortcut is null at both call sites; the shader one is `ED_SHORTCUT_AND_COMMAND`, so it takes the `command_palette` branch), so this is a latent hazard rather than a shipped regression. But an agent that sees `ok:false, kind:shortcut_unhandled` will retry or escalate on a command that already ran, which is a worse failure mode than the one being fixed. Recommendations:

- State the contract in the `shortcut_unhandled` message itself ("no control marked the event handled; the action may still have run if its handler does not accept events") so an agent has a next step other than blind retry.
- Add a `misc/checks` or review note for new `shortcut_input()` overrides in `editor/` requiring `accept_event()`, or fix the two above so the invariant actually holds.

For the record, this is *not* a regression for the acceptance workflows: every `run_command` key they use (`property_editor/expand_all`, `docks/open_scene`, `docks/open_inspector`, `editor/save_scene`, `editor/run_current_scene`, `editor/stop_running_project`, `editor/close_scene`, `editor/new_scene`) resolves through `EditorCommandPalette`, which is checked first in `execute()` and is unaffected.

**H4. The `invalid_parameter` contract the tests assert is not what an MCP client receives for a bad `zoom` type.**
`editor/automation/editor_automation_mcp_contracts.cpp:899-901` and `167-178`; `tests/editor/test_editor_automation_mcp.h:1465-1480`

`EditorAutomationMCPActionArgs::parse()` runs `_read_optional_number(dict, "zoom", ...)` before the driver ever sees the value. `{"action":"set_canvas_2d_zoom","args":{"zoom":true}}` therefore fails at the parse layer with a JSON-RPC `invalid_params` error carrying field `args.zoom`, never reaching `_action_set_canvas_2d_zoom` and never producing `kind:"invalid_parameter"`. The new test calls `EditorAutomationDriver::perform()` directly, so it validates an internal contract that no client can observe.

Also note `Math::NaN` / `Math::INF` cannot appear in JSON at all, so the `is_finite` branch (`editor_automation_driver.cpp:1550-1552`) is only reachable from in-process callers such as `EditorWorkflowTestDriver::act()`. That is fine as defence in depth, but the *documented* client-visible error surface should be written down.

Add an MCP-level test through `EditorAutomationMCPDispatcher` for `{"zoom": true}` and for a missing `zoom` (which *does* reach the driver and does return `invalid_parameter`), and document which failures surface as JSON-RPC errors versus structured `kind`s.

### Medium

**M5. The two headline canvas tests do not exercise any new code.**
`tests/editor/test_canvas_item_editor_view_state.h:145-175`

`Idempotent zoom leaves offset unchanged` and `Focused view zoom leaves a secondary view untouched` operate on `CanvasItemEditorViewMath::apply_zoom_at_point()` and two independent `CanvasItemEditorViewState` structs. `apply_zoom_at_point` is pre-existing and unmodified; two local structs are trivially independent of each other. Neither test touches `CanvasItemEditorView::apply_absolute_zoom_at_center()`, `CanvasItemEditor::get_focused_view()` routing, or `_action_set_canvas_2d_zoom`. Combined with the display-gated subprocess workflow, the new production code has effectively zero coverage on a headless CI run.

At minimum, add a deterministic test that builds a `CanvasItemEditorView` (or refactors the center-anchor computation into `CanvasItemEditorViewMath` so it is testable) and asserts that the scene point under the viewport centre is invariant and that a second view's state is untouched by the *routing*, not by construction.

**M6. `value_out_of_range` is a new error kind that nothing publishes.**
`editor/automation/editor_automation_driver.cpp:1592`

Every other action failure in the driver uses `unsupported_action` or `invalid_parameter`. `value_out_of_range` appears exactly once in the tree and is not in any schema enum, so an agent cannot discover it before hitting it. Either reuse `invalid_parameter` (the value *is* an invalid parameter, and the `minimum`/`maximum` fields already carry the discriminating detail) or add the kind to a published enum in `editor_automation_mcp_contracts.cpp` alongside the wait-status/route enums.

**M7. `read_editor_state().view_2d.supported` is true in states where `set_canvas_2d_zoom` refuses.**
`editor/automation/editor_automation_state.cpp:429-443` vs `editor_automation_driver.cpp:1569-1583`

`view_2d.supported` is `true` whenever `CanvasItemEditor::get_singleton()` is non-null, which is from plugin construction onward. The action additionally requires `canvas_editor->is_visible_in_tree()` and `main_screen->get_selected_index() == EDITOR_2D`. An agent that reads `view_2d.supported == true` while the 3D or Game screen is selected and then calls `set_canvas_2d_zoom` gets `unsupported_action` with no way to have predicted it. Add a readback field (`view_2d.active` or `view_2d.zoom_settable`) that mirrors the action's precondition, and reference it in the `set_canvas_2d_zoom` schema description.

**M8. The newly published "1.0 = 100%" convention exposes an existing inconsistency in the default zoom, and contradicts a neighbouring comment.**
`editor/scene/canvas_item_editor_plugin.cpp:2173` and `2762`; `editor/scene/canvas_item_editor_view_state.h:120-121`

`CanvasItemEditor::clear()` and `_init_secondary_view_state()` both set the **absolute** zoom to `1.0 / MAX(1, EDSCALE)`, whose normalized value is `1 / EDSCALE²`. `EditorZoomWidget::_button_zoom_reset()` uses `1.0 * MAX(1, EDSCALE)` (normalized `1.0`). So at EDSCALE 2, a freshly cleared canvas reports `view_2d.zoom == 0.25` and `set_canvas_2d_zoom(1.0)` is a 4x change from the default. That is pre-existing, but this change makes it externally visible and load-bearing, so it should be fixed or explicitly called out.

Relatedly, `canvas_item_editor_view_state.h:120` still documents `zoom` as "Current viewport zoom factor (1.0 = 100%)", which the new `CanvasItemEditorNormalizedZoom` block 100 lines below directly contradicts ("Absolute zoom stored on `CanvasItemEditorViewState` includes `MAX(1, EDSCALE)`"). Fix the older comment in the same change.

**M9. The `editor != nullptr` guards added to `build_ui()` are inconsistent half-measures and appear unrelated to the fix.**
`editor/scene/canvas_item_editor_view.cpp:176-184`

`editor` is dereferenced unconditionally elsewhere in the same class, including on the exact path the new action calls: `_zoom_on_position()` reads `editor->auto_resampling_enabled` and `editor->resample_timer` (`canvas_item_editor_view.cpp:3407-3409`), and `_update_oversampling()` reads `editor->auto_resampling_enabled` (3425). `build_ui()` itself passes `editor` straight into `memnew(CanvasItemEditorViewport(editor, this))` at line 198. So if `editor` can be null the class still crashes moments later, and if it cannot, these guards are dead code that silently disables the Center View button and the zoom shortcut context. Remove them, or make the null case an explicit `ERR_FAIL_NULL(editor)` at the top of `build_ui()` and drop the per-site checks.

**M10. The MCP unit test for shortcut dispatch accepts either outcome, so it cannot fail on a regression.**
`tests/editor/test_editor_automation_mcp.h:1443-1450`

```cpp
const String kind = run_structured.get("kind", String());
CHECK((kind == "shortcut_unhandled" || kind == "unavailable"));
if (kind == "shortcut_unhandled") { ... }
```

In the unit-test process there is no `EditorNode` viewport, so `_editor_viewport_available()` is false and the result is always `unavailable` — the pre-existing test at line 1394 asserts exactly that. The `shortcut_unhandled` branch never runs, and the disjunction means the test would still pass if the whole feature were reverted. The direct `push_shortcut_event()` assertions above it (lines 1425 and 1437) are the only real coverage.

Make the MCP-level assertion unconditional by asserting `kind == "unavailable"` in this environment, and cover `execute()`'s shortcut branch separately — either by injecting a viewport (extract the `execute()` shortcut route into a helper that takes `Viewport *`) or by leaving the end-to-end proof to the workflow and saying so in a comment.

**M11. Nothing verifies that `canvas_2d_zoom_automation` is registered except the display-gated subprocess test.**
`editor/automation/editor_automation_workflow_registry.cpp:136`; `tests/editor/test_editor_automation_workflow.h:413-449`

The registry tests at `test_editor_automation_workflow.h:253-274` already assert `has_workflow(...)` / `list_workflow_names().has(...)` for the other workflows and run without a display. Add `canvas_2d_zoom_automation` there so a registration mistake fails on a headless run instead of silently skipping.

**M12. The acceptance workflow never confirms its own setup, and never asserts the anchoring guarantee.**
`editor/automation/editor_automation_acceptance_workflow.cpp:703-741`

- `main_screen->select(EditorMainScreen::EDITOR_2D)` returns silently when `EditorNode::is_changing_scene()` is true (`editor_main_screen.cpp:221-224`) or when the button is hidden. If that happens, the failure surfaces three steps later as `unsupported_action: "The focused tile has no active 2D canvas view."` Add an explicit check that `get_selected_index() == EDITOR_2D` after the flush.
- The whole point of the "zoom around viewport center" design is untested end to end. `effective_zoom` and `view_2d.zoom` are anchor-independent. Capture `view_2d.view_offset` (already exposed at `editor_automation_state.cpp:437-439`) before and after and assert the scene point under the viewport centre is invariant.
- The workflow asserts `changed:true` for zoom 2.0, which assumes the fixture scene's restored 2D state is not already 2.0. That is fine today but is an unnecessary coupling to fixture contents; asserting `effective_zoom == 2.0` is sufficient, or set a different zoom first and assert the transition.

### Low / Nitpicks

**L13. Include ordering.** `editor/automation/editor_automation_acceptance_workflow.cpp:44` puts `editor/settings/editor_settings.h` between `editor/editor_script_leaf.h` and `editor/file_system/editor_paths.h`. The rest of the block is alphabetical; `SortIncludes` is commented out in `.clang-format:195` so formatting will not fix it. Move it after `editor/script/script_editor_view.h`.

**L14. `Validation::NON_FINITE` is unreachable from its only production caller.** `_action_set_canvas_2d_zoom` checks `Math::is_finite()` itself at `editor_automation_driver.cpp:1550` before calling `validate()`, so the enum arm exists only for the helper's own unit test. Either drop the pre-check and map `NON_FINITE` to `invalid_parameter`, or drop the arm.

**L15. `apply_absolute_zoom_at_center()` silently anchors at the origin if the view has not been laid out.** `canvas_item_editor_view.cpp:3416-3422` uses `viewport_scrollable->get_size() / 2.0`; a zero-size control makes the "zoom around viewport center" guarantee degenerate to "zoom around the top-left" with no signal to the caller. Consider reporting the anchor in the result, or failing when the size is zero.

**L16. `act` input schema description still implies a selector is mandatory.** `editor_automation_mcp_contracts.cpp:1160` — "Provide action and selector (the element to act on) for a new interaction." The tool description was updated to mention selectorless actions; the input schema description (which is what a strict client surfaces alongside the properties) was not. Pre-existing for `activate_board`/`set_board_overview`, worth fixing now.

**L17. The normalized zoom *range* is scale-dependent even though the value is not.** `EditorZoomWidget::min_zoom`/`max_zoom` are absolute (`editor_zoom_widget.h:44-45`), so the normalized bounds reported at `editor_automation_driver.cpp:1588-1589` are `[1/(128·S), 128/S]`. This correctly mirrors what the widget will actually clamp to, but the design spec's "independent of editor UI scale" wording invites the opposite assumption. One sentence in the spec would close it.

**L18. `tile_id` is the *effective* focused tile.** `EditorNode::get_focused_tile()` falls back to the last scene tile when a script leaf holds focus (`editor_node.cpp:7738-7742`). That happens to match where the scene mode is reparented, so the value is correct, but "focused tile" in the result means "effective focused tile" and should say so in the schema description.

**L19. `scripts/exercise_editor_mcp.py:260` picks the first `runnable_by_run_command` entry and asserts `ok`.** Safe today because `list_commands()` emits all palette entries before any standalone shortcuts (`editor_automation_commands.cpp:219-252`), so the first hit is always palette-routed. Pin the intent with `c.get("source") == "command_palette"` so a future ordering change does not turn this smoke script into a flake.

**L20. Dead `#else` branch.** `editor_automation_acceptance_workflow.cpp:775-777` declares `const Dictionary shortcut_result;` for non-`TOOLS_ENABLED` builds, but this translation unit only builds for editor targets, and the following checks would fail on the default-constructed dictionary anyway. Harmless, but it reads as if the non-tools path is supported.

**L21. Duplicate schema assertions.** The new `set_canvas_2d_zoom is advertised with a numeric zoom arg` case (`test_editor_automation_mcp.h:1495-1514`) asserts the same four facts already added to `mcp-board-actions-are-published-in-tool-schemas` (`test_editor_automation_workspace.h:815, 1016, 1024-1025`). Keep one.

## Test Coverage

What is genuinely covered:

- `EditorAutomationCommands::push_shortcut_event()` handled vs unhandled, against a real `Button` consumer on the scene-tree root. This is the strongest new test and it is deterministic.
- `CanvasItemEditorNormalizedZoom` conversion and validation, including EDSCALE 1 and 2 via the RAII `EditorScaleGuard`, and the round-trip through `CanvasItemEditorSceneGeometryState`.
- MCP schema publication of the action name and the `zoom` arg type.
- Driver-level rejection of missing / boolean / NaN / Inf `zoom`, plus the `unsupported_action` path with no editor.

What is missing:

- Any deterministic exercise of `CanvasItemEditorView::apply_absolute_zoom_at_center()` or the `_action_set_canvas_2d_zoom` success path (M5). The two tests that read as if they cover this only exercise pre-existing pure math.
- The `shortcut_unhandled` result itself. The MCP test's disjunction (M10) never reaches it, and the only real proof is the subprocess workflow, which self-skips without `DISPLAY`.
- The client-visible error contract for a wrongly-typed `zoom` (H4) — the MCP parse layer intercepts it and no test covers that surface.
- Registration of `canvas_2d_zoom_automation` in a headless-runnable test (M11).
- The anchoring guarantee end to end (M12).
- Any assertion that the `details`-promotion change (C1) does not alter existing action payloads. If the promotion stays in any form, it needs a test pinning the exact top-level key set for at least one pre-existing action.

Net effect: on `foundry --headless test run` without a display, the new feature's production code is close to untested. Everything that fails today would still pass.

## Overall Assessment

Request changes. The design intent matches issue #2050 and the tricky parts (EDSCALE normalization, no-mutation-on-reject, focused-view targeting through the reparented scene mode) are right. The blockers are contract-level rather than logic-level:

1. Replace the blanket `details` → top-level promotion with an explicit, schema-declared set of public result fields (C1). This is the "top-level vs nested" question the task flags, and the current answer changes far more than intended.
2. Guard `push_shortcut_event()` against `push_input()`'s early returns so a stale handled flag cannot be reported as success (H2), and give agents an actionable message for the accept-less-handler false negative (H3).
3. Land at least one deterministic test that runs the new zoom code and one that pins the `shortcut_unhandled` result, so the feature is not protected solely by a display-gated subprocess workflow (M5, M10, M11).

Then the medium items (M6-M9, M12) are worth folding into the same change while the context is fresh, particularly the `view_2d.supported` asymmetry (M7) and the `clear()` default-zoom inconsistency (M8), both of which an agent will hit on its first real session.
