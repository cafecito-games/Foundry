# Code Review 1

**Worktree:** `/workspace`
**Diff range:** `1f97a599afd95e4fab3f9ce0829816e9b94b5b3e...15b45c7c26d63dba408c61c32eb5aad19980df67` (branch `csueiras/board-actions-menu-3902` vs `origin/develop`). Working tree is clean — no uncommitted, staged, or untracked changes.
**Date:** 2026-08-10

## Summary

The change adds a `PopupMenu` subclass (`EditorBoardActionsMenu`) offering rename / close / close-others / move-left / move-right for a board, reachable from the title-bar switcher (press on the active button, right-click on any button) and from an overview caption right-click. It also splits `EditorBoardStrip::close_board()` into a tri-state `_close_board_internal()` and adds an ObjectID-keyed `close_boards()` queue with `abort_pending_closes()` wired to `EditorNode::_cancel_close_scene_tab()`.

The core design decisions are sound and unusually well-reasoned: capturing the target by `ObjectID` and re-resolving at activation, keeping the menu alive across switcher rebuilds, the `CLOSED / DEFERRED / REFUSED` split, and clearing the queue on refusal so it can never spin. The commentary throughout is genuinely useful. Two defects will be visible to users on the first day (a state flag that leaks and permanently swallows one menu open; popup positioning that uses viewport coordinates where screen coordinates are required), and one previously-invariant behavior — that a board is never torn down synchronously from an input-dispatch stack — is silently broken by the new call sites. Test coverage is real behavioral coverage (no source-text assertions), but the two hardest branches in the change are the two that are not tested.

## Findings

### Critical

**C1. `skip_actions_menu` is never cleared after a double-click rename, permanently swallowing the next actions-menu open.**
`editor/gui/editor_board_switcher.cpp:265-268` sets the flag, `editor/gui/editor_board_switcher.cpp:233-252` is the only place that clears it, and the clear only runs when the board button emits `pressed`. For the double-click path that `pressed` never arrives:

1. `Control::_call_gui_input()` emits the `gui_input` **signal before** running `BaseButton::gui_input()`, so `_on_board_button_gui_input()` sets `skip_actions_menu = true` and calls `_begin_rename()` while `BaseButton::status.press_attempt` is still `false`.
2. `_begin_rename()` calls `button->hide()` (`editor/gui/editor_board_switcher.cpp:299`). Hiding a control that owns mouse focus routes through `Viewport::_gui_hide_control()` → `Viewport::_drop_mouse_focus()` (`scene/main/viewport.cpp:2522-2545`, `scene/main/viewport.cpp:2724-2746`), which clears `gui.mouse_focus` and synthesizes a release into the button. `BaseButton` sees that synthetic release with `press_attempt == false` and emits nothing.
3. The real release then has no `gui.mouse_focus` to go to, so the button never emits `pressed`, and nothing consumes the flag.

Result: after any double-click rename, the next left-click on the active board button silently does nothing (the flag is consumed, the menu is suppressed); the click after that works. Nothing in `_rebuild()` or `_cancel_rename()` resets it.

Recommendation: delete the flag and derive the condition instead, so there is no state to leak. A rename that has just been started always has a live `rename_edit`:

```cpp
void EditorBoardSwitcher::_on_board_button_pressed(int p_index) {
	...
	if (p_index == strip->get_active_index()) {
		if (Button *button = _board_button_at(p_index)) {
			button->set_pressed_no_signal(true);
		}
		// A double-click starts an inline rename from gui_input, which runs before the
		// button's own release handling; that same release must not also open the menu.
		if (is_renaming()) {
			return;
		}
		...
	}
}
```

Note this also makes the currently-unreachable `else if (skip_actions_menu)` branch at `editor/gui/editor_board_switcher.cpp:244-246` go away, and it fixes the related gap that when `_board_button_at()` returns null the button is left visually un-pressed.

Add a regression test that drives the real signal order (`gui_input` double-click, then `pressed`) and asserts the *following* `pressed` on the active button still opens the menu.

**C2. Both right-click paths pass viewport coordinates where `Popup::popup()` expects screen coordinates.**
`editor/gui/editor_board_switcher.cpp:261` (`mb->get_global_position()`) and `editor/editor_board_strip.cpp:788` (`mouse_button->get_global_position()`) flow into `EditorBoardActionsMenu::popup_for_board()` → `popup(Rect2i(p_screen_position, Size2i()))` (`editor/gui/editor_board_actions_menu.cpp:75`).

`InputEventMouse::global_position` is *viewport*-relative, not screen-relative: `Viewport::_make_input_local()` explicitly resets it to the viewport-local position (`scene/main/viewport.cpp:1439-1447`). `Window::_popup_base()` calls `set_position(p_screen_rect.position)` and `DisplayServer::get_screen_from_rect()` on that rect (`scene/main/window.cpp:2146-2157`), which is screen space for a non-embedded window. The editor defaults to non-embedded subwindows (`interface/editor/single_window_mode` defaults to `false`, `editor/settings/editor_settings.cpp:507`, consumed at `main/main.cpp:5142`). So the menu is offset by the editor window's screen position and can land on the wrong monitor.

The `pressed` path is already correct — it uses `button->get_screen_rect()` (`editor/gui/editor_board_switcher.cpp:242`) — so the feature is internally inconsistent. Every other editor context menu uses `get_screen_position() + get_local_mouse_position()` or `get_screen_transform().xform(...)`; see `editor/docks/scene_tree_dock.cpp:3823`, `editor/scene/2d/tiles/tile_set_atlas_source_editor.cpp:1558`.

Recommendation, switcher side:

```cpp
if (mb->get_button_index() == MouseButton::RIGHT) {
	_popup_actions_menu(p_index, get_screen_transform().xform(get_local_mouse_position()));
	return;
}
```

Strip side, where `mouse_button->get_position()` is caption-local rather than strip-local, convert straight from the viewport-space value already in hand:

```cpp
const Point2 screen_position = get_viewport()->get_screen_transform().xform(mouse_button->get_global_position());
board_context_menu_handler.call(index, screen_position);
```

Also fix the test at `tests/editor/test_editor_board_strip.h:202`, which currently asserts `screen_position == Point2(100, 0)` and therefore locks in the wrong coordinate space.

### High

**H1. Double-click-to-rename on the *active* board button is now unreachable.**
Board buttons use `BaseButton`'s default `ACTION_MODE_BUTTON_RELEASE` (`scene/gui/base_button.h:58`), so the first release of a double-click emits `pressed`, which now opens the menu (`editor/gui/editor_board_switcher.cpp:233-243`). `PopupMenu` inherits `Popup`, which sets `FLAG_POPUP`; the second press of the double-click is consumed by closing the popup and never reaches the button. Double-click rename therefore only survives on *inactive* buttons (where the first click switches boards and the second lands on the freshly rebuilt, now-active button).

The existing double-click tests do not catch this because `BoardSwitcherHarness::double_click()` (`tests/editor/test_editor_board_switcher.h:140-147`) emits only `gui_input` and never the `pressed` signal that a real first click produces.

This may be an intentional trade (the menu exposes Rename), but it is an undocumented behavior removal with tests that pass only because they bypass the real event order. Recommendation: decide explicitly and record it. If double-click rename should survive on the active button, the established editor pattern is a dedicated caret button next to the label rather than overloading the whole toggle — compare `editor/run/editor_run_bar.cpp:100`, which uses a separate `run_options_button` with the same `GuiDropdown` icon. That also removes C1's root cause and the need for any suppression state.

**H2. `_close_board_internal()` treats every falsey handler return as `DEFERRED`, and two of those cases stall the queue permanently.**
`editor/editor_board_strip.cpp:135-137` maps `board_scene_close_handler.call(...) == false` to `CloseOutcome::DEFERRED`, and `_advance_pending_closes()` responds by leaving the head in place and returning, waiting for a `close_board()` that resolves it (`editor/editor_board_strip.cpp:236-239`). But `EditorNode::_close_board_scenes()` returns `false` from three places, only one of which is a genuine deferral:

- `ERR_FAIL_COND_V(pending_board_close_id.is_valid(), false)` (`editor/editor_node.cpp:8507`) — a hard refusal because another board close already owns the prompt queue.
- `ERR_FAIL_NULL_V(board, false)` (`editor/editor_node.cpp:8510`).
- the real async path (`editor/editor_node.cpp:8522`).

In the first two cases no prompt is in flight, so nothing will ever call `close_board()` for that head, and `pending_close_ids` stays non-empty indefinitely with the remaining boards silently never closing. It is recoverable (a later `close_boards()` overwrites the vector, cancel clears it) but there is no signal to the user or the log that a bulk close stopped halfway.

This is reachable: activate "Close Other Boards", then while the first unsaved-scene prompt is on screen, right-click another board and pick Close or Close Other Boards again. Recommendation: make the deferral explicit rather than inferred. Either have the handler return a tri-state, or have `close_boards()` refuse to start while a queue is live:

```cpp
void EditorBoardStrip::close_boards(const Vector<ObjectID> &p_board_ids) {
	ERR_FAIL_COND_MSG(!pending_close_ids.is_empty(), "A bulk board close is already in progress.");
	pending_close_ids = p_board_ids;
	_advance_pending_closes();
}
```

Related: the abort surface is narrower than the set of ways the prompt flow can end. `_cancel_close_scene_tab()` is only wired to `save_confirmation`'s `canceled` signal (`editor/editor_node.cpp:11832`); a flow that terminates through a canceled Save As, for example, leaves `pending_board_close_id` set and now additionally strands the whole queue. That hazard pre-dates this change for a single board, but the queue multiplies its blast radius.

**H3. The menu is the first caller that tears a board down synchronously from an input-dispatch stack.**
Before this change `close_board()` had exactly one production caller, `EditorNode::_finish_pending_board_close()`, which is always reached through `call_deferred()` precisely for this reason — the comment at `editor/editor_node.cpp:8518-8520` spells out the invariant ("The close is always finished from `_proceed_closing_scene_tabs`, never from here"). `EditorBoardActionsMenu::_on_id_pressed()` now calls `strip->close_board(index)` (`editor/gui/editor_board_actions_menu.cpp:95`) and `strip->close_boards(others)` (`editor/gui/editor_board_actions_menu.cpp:108`) directly from `PopupMenu::activate_item()`'s `id_pressed` emission (`scene/gui/popup_menu.cpp:3081`), which runs inside window input dispatch. `close_boards()` also runs its first close synchronously (`editor/editor_board_strip.cpp:208-211`) while every subsequent one is deferred, which is an odd asymmetry on its own.

For a board with no scenes this reaches `remove_child(board); memdelete(board);` (`editor/editor_board_strip.cpp:152-154`) and then `_on_board_removed()`, which reparents docks and the scene-mode control across the editor (`editor/editor_node.cpp:8451-8460`), all from inside the popup's input handling. The popup itself survives (the `_rebuild()` skip is correct and load-bearing), but nothing else about this stack was designed for it.

Recommendation: defer at the menu, keeping the ObjectID as the unit of identity so a shifted index cannot be acted on:

```cpp
case ITEM_CLOSE: {
	callable_mp(strip, &EditorBoardStrip::close_boards).bind(Vector<ObjectID>{ target_board_id }).call_deferred();
} break;
```

and make `close_boards()` defer its first `_advance_pending_closes()` too, so the entry point has one behavior instead of two. (That changes the timing the test at `tests/editor/test_editor_board_strip.h:159` relies on — it would need a `h.pump()` before asserting `call_count == 1`.)

### Medium

**M1. `_board_button_at()` resolves a button by raw child index, which is the fragile thing the rest of the class avoids.**
`editor/gui/editor_board_switcher.cpp:191-199` assumes child index *i* is board *i*. Three things can break that, and only one of them fails safely:

- During a rename, `rename_edit` is moved to index `p_index` (`editor/gui/editor_board_switcher.cpp:306`) and the button shifts to `p_index + 1`. `_board_button_at(p_index)` fails safe (the `cast_to<Button>` returns null), but `_board_button_at(p_index + 1)` now silently returns the *wrong* board's button.
- `_rebuild()` skips null boards with `continue` (`editor/gui/editor_board_switcher.cpp:138-140`) without skipping the index, so a single null board makes every later `_board_button_at()` return a valid-but-wrong `Button`.
- The whole `move_child(actions_menu, get_child_count() - 1)` bookkeeping at `editor/gui/editor_board_switcher.cpp:159-162`, `:173-175`, `:186-188`, and `:307-309` exists solely to preserve this positional invariant.

The rest of this feature is careful to key on identity rather than position; this is the one place that does not. Recommendation: bind the button into the callable (`.bind(button)` alongside the index, or bind the board `ObjectID` and look the button up from a small map), or store the board id in `button->set_meta()` and search. That removes all four `move_child` calls and the `chrome_child_count()` coupling in the tests.

A lighter alternative that fixes the ordering fragility only: parent the popup somewhere other than the `HBoxContainer` (popups do not need to be children of the control that opens them), keeping a raw pointer for ownership.

**M2. Duplicate deferred `_advance_pending_closes()` calls are possible.**
Both `close_board()` (`editor/editor_board_strip.cpp:203`) and `_advance_pending_closes()` (`editor/editor_board_strip.cpp:233`) push the same deferred callable. If both land in one frame the queue simply advances faster than the one-close-per-frame pacing the comment describes, so there is no corruption — but the pacing is the stated reason for deferring at all. A `bool advance_queued` guard set on push and cleared at the top of `_advance_pending_closes()` would make the invariant hold.

**M3. Coverage gaps on the two hardest branches.**
See the Test Coverage section; the resume-the-queue branch in `close_board()` and the `skip_actions_menu` interaction are both untested, and they are the two places this review found defects.

**M4. `reset_size()` runs before the items are added.**
`editor/gui/editor_board_actions_menu.cpp:67-68` calls `clear()` then `reset_size()`, then populates. `reset_size()` on an empty menu shrinks to nothing and the subsequent `add_item()` calls re-grow the content minimum size, so `popup()`'s `_update_window_size()` is what actually produces the final size. The usual idiom is to reset after populating. Harmless today, but it reads as if it were sizing the populated menu.

**M5. `close_boards()` silently clobbers an in-flight queue.**
`editor/editor_board_strip.cpp:209` assigns unconditionally. Combined with `ITEM_CLOSE_OTHERS` building a possibly-empty `others` vector (`editor/gui/editor_board_actions_menu.cpp:99-108`), activating the item when no other board survives cancels a queue that is still running. The `ERR_FAIL_COND_MSG` suggested in H2 covers this too.

**M6. Test asserts the caret via a proxy that would not catch its removal.**
`tests/editor/test_editor_board_switcher.h:395-397` checks `get_icon_alignment()` and that the inactive button's icon is null, with an honest comment about headless icon availability. But deleting the `set_button_icon()` call at `editor/gui/editor_board_switcher.cpp:153` while keeping `set_icon_alignment()` would leave the test green. If `get_editor_theme_icon()` resolves in this harness, assert `active->get_button_icon().is_valid()` as well; if it does not, say so explicitly so the limitation is recorded rather than implied.

### Low / Nitpicks

**L1.** `editor/editor_board_strip.cpp:778` computes `to_strip = get_global_transform().affine_inverse()` before the new right-click early-return at `:784-792`, so every right-click and every non-mouse event pays for an unused matrix inverse. Moving the declaration below the right-click block is free.

**L2.** `popup(Rect2i(p_screen_position, Size2i()))` (`editor/gui/editor_board_actions_menu.cpp:75`) degenerates when the position is exactly `(0, 0)`: `Window::popup()` compares the whole rect against `Rect2i()` (`scene/main/window.cpp:2120`), so a zero position with a zero size skips positioning entirely and the menu appears wherever it last was. Reachable from a right-click on the top-left pixel of the editor window, and it is the state every `popup_for_board(i, Point2())` call in the tests exercises. Passing `Size2i(1, 1)` or calling `set_position()` + `popup()` avoids the special case.

**L3.** Rename started from an overview caption right-click opens the inline `LineEdit` in the *title bar* switcher, not on the caption the user right-clicked. `_apply_pending_rename()` does call `refresh_overview_captions()` (`editor/gui/editor_board_switcher.cpp:339`), so this is clearly intentional, but the edit field appearing somewhere other than where the user clicked is worth a deliberate confirmation.

**L4.** The active button gains a menu affordance but its accessibility name is still just the board title (`editor/gui/editor_board_switcher.cpp:147`), and there is no keyboard route to the menu (no context-menu key / Shift+F10 handling). Screen-reader users get no indication the button now opens a menu.

**L5.** The strip's public surface list in `docs/superpowers/specs/2026-08-09-editor-boards-design.md:76-79` does not mention `close_boards()`, `abort_pending_closes()`, or `set_board_context_menu_handler()`. That list was already drifting (it omits `resolve_board_index()` and `refresh_overview_captions()`), so this is a good moment to refresh it.

**L6.** `renaming_button = nullptr;` at `editor/gui/editor_board_switcher.cpp:130` is a genuine latent-dangling-pointer fix (previously, a `_rebuild()` with `rename_edit == nullptr` but `renaming_button` set would free the button and leave `_cancel_rename()` to call `show()` on freed memory) but it is bundled in without comment. Worth a one-line note or a separate commit so it is not mistaken for incidental churn.

**L7.** At least one test leaves the popup window visible at teardown ("Pressing the active board opens the actions menu and stays pressed", `tests/editor/test_editor_board_switcher.h:415-436`) and `unmount()` then `memdelete(host)` (`tests/editor/test_editor_board_switcher.h:78-82`). Confirm the full-suite run does not add an entry to the `ObjectDB instances leaked` report; an explicit `menu->hide()` before `unmount()` costs nothing.

**L8.** No formatting problems found. `.clang-format` uses no column limit and the long lines flagged by a naive scan are all pre-existing. Run `pre-commit run --all-files` as usual before handoff — `clang-format` was not available in this environment to verify the two new files.

## Test Coverage

The added tests are real behavioral tests. They assert on board counts, active-board identity, menu item disabled state, captured `ObjectID`s, emitted signals, and handler invocation counts — never on source text, YAML/Markdown formatting, or the existence of other tests. That satisfies the `AGENTS.md` test-authoring rules, and the self-check greps stay clean. `BoardStripHarness`/`BoardSwitcherHarness` are reused correctly, `SIGNAL_WATCH`/`SIGNAL_CHECK_FALSE` are paired with `SIGNAL_UNWATCH`, and the `REQUIRE` + early-`unmount()` guard pattern matches the surrounding file. The `abort_pending_closes` test is the strongest of the set: it puts a real scene on a board, forces the handler to defer, aborts, then resumes and proves the survivor list.

What is missing, roughly in order of risk:

1. **The resume-the-queue branch of `close_board()`** (`editor/editor_board_strip.cpp:201-204`) is never executed. The abort test reaches `DEFERRED` and then clears the queue, so the head-matching path that drops the completed board and re-arms the advance — the trickiest few lines in the change — has no coverage. A test should defer on board A, let the handler succeed on the retry, and assert board B closes afterwards without a second `close_boards()` call.
2. **The `skip_actions_menu` / double-click interaction** (defect C1). No test drives `gui_input(double_click)` followed by `pressed`, which is the real event order and the order that exposes the leak. `BoardSwitcherHarness::double_click()` should gain a `pressed`-emitting variant.
3. **`ITEM_CLOSE_OTHERS` end-to-end from the menu.** `close_boards()` is covered at the strip level and `ITEM_CLOSE` is covered at the menu level, but the item that connects the menu to the new queue is not exercised at all.
4. **Explicit assertion that the menu survives a rebuild.** The "target board was freed" test proves it implicitly by activating the menu after a `close_board()`, but nothing asserts `get_actions_menu()` returns the same object across `board_added` / `board_removed` / `boards_restored`. That is the invariant the whole `_rebuild()` skip-and-`move_child` design exists to protect.
5. **`_board_button_at()` during a rename** — that it returns null rather than the `LineEdit`, and (once M1 is addressed) that `_board_button_at(p_index + 1)` does not return the wrong board's button.
6. **Popup positioning.** Hard to assert directly, but once C2 is fixed the strip-side test at `tests/editor/test_editor_board_strip.h:202` should assert the *converted* value rather than the raw event position.
7. The refused-queue test at `tests/editor/test_editor_board_strip.h:83-114` uses a fixed three-`pump()` sequence. If the pacing in M2/H3 changes, the count changes with it; a loop with a bounded iteration cap would be less brittle.

## Overall Assessment

Request changes. The architecture is right and the identity-over-index discipline is applied consistently everywhere except `_board_button_at()`, but C1 and C2 are both user-visible on the first day and neither is caught by the current tests. C1 in particular makes the feature's primary affordance dead on the click following any rename.

Suggested order of work:

1. Fix C1 by replacing `skip_actions_menu` with an `is_renaming()` check, and add a test that emits `gui_input` and `pressed` in the real order.
2. Fix C2 in both right-click paths and update the strip test to assert the converted coordinate.
3. Decide H1 explicitly — either keep double-click rename by moving the menu trigger to a dedicated caret button (which also dissolves C1 and M1), or record the removal in the commit message and the switcher's class comment.
4. Address H2 by making refusal distinguishable from deferral, and H3 by deferring the menu's close calls so the "never tear a board down from an input stack" invariant holds again.
5. Fill the two coverage gaps that map to defects: the `close_board()` resume branch and the double-click interaction.

M1 is worth doing while the file is open; the positional-index assumption is the one remaining place where this feature can act on the wrong board without any diagnostic.
