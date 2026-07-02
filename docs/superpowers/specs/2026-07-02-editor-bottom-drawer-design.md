# Editor Bottom Drawer — Design

**Date:** 2026-07-02
**Status:** Approved
**Scope:** Convert the editor's in-flow bottom panel into an overlay drawer that slides up over the workspace, can host any dock, and can be pinned back into the layout flow per panel.

## Problem

The bottom panel (`editor/gui/editor_bottom_panel.h`) lives inside `center_split`, a vertical `DockSplitContainer` (`editor/editor_node.cpp:8896`). Opening any bottom panel shrinks the workspace instead of layering over it, and its height is coupled to a split offset. Additionally, the bottom dock slot only accepts docks that declare `DOCK_LAYOUT_HORIZONTAL` (`editor/docks/editor_dock_manager.cpp:1124`), so vertical docks such as the Inspector and Scene Tree can never be placed there.

## Approved behavior

1. **Overlay by default, pin to dock.** Unpinned, the drawer slides up over the workspace. Pinned, it reserves workspace space exactly like today's behavior. Pin state is per panel and persisted.
2. **Any dock allowed.** The horizontal-only gate on the bottom slot is relaxed. Docks with a horizontal presentation (`update_layout()`) keep re-orienting; vertical-only docks render their normal vertical layout at drawer width. Per-dock horizontal polish is follow-up work, not a blocker.
3. **Explicit dismissal only.** The drawer closes via its tab, its shortcut, or Esc while focused (unpinned only). Clicking the workspace never auto-closes it, so drag-from-drawer workflows survive.
4. **Pin button remap.** The pin button becomes the dock/undock (in-flow) toggle. The previous pin behavior — locking tab auto-switching (`lock_panel_switching`) — moves to the drawer tab's context menu.

Approved implementation approach: **single overlay host** (no reparenting between modes; pinning is an inset, not a tree change).

Visual reference (interactive mock of all states): https://claude.ai/code/artifact/d4270f2c-4529-4050-ab09-3b5b5f8cedf6

## Design

### 1. Control tree

`center_split` is removed. In its place, a plain `Control` — `center_overlay` — is added to `center_vb` with expand-fill sizing. It holds two anchored children:

- `top_split` (the workspace): full-rect anchors. When a pinned panel is open, its bottom anchor offset equals the drawer height, reserving the space.
- `EditorBottomPanel`: anchored to the bottom edge, full width, height driven by drawer state. Because the parent is a plain `Control` rather than a container, the panel legitimately draws over the workspace.

`EditorBottomPanel` keeps its name, its `TabContainer` base, and its dock-slot registration. `EditorDockManager::register_dock_slot(DOCK_SLOT_BOTTOM, ...)` keys off the container pointer and metas, not ancestry (`editor/docks/editor_dock_manager.cpp:1003-1037`), so the dock manager, drag hints, slot picker, and floating-window support need no changes.

`EditorNode::get_center_split()` (`editor/editor_node.h:761`) is deleted; its internal call sites (`_bottom_panel_resized` at `editor_node.cpp:8458`, offset restore via `_update_center_split_offset`) move to the new geometry.

### 2. Drawer state machine

Three independent pieces of state:

- **open**: which dock's tab is current, or none (`set_current_tab(-1)` remains the collapsed representation).
- **pinned**: per dock, persisted. Pinned + open ⇒ workspace inset = drawer height. Unpinned + open ⇒ inset 0, drawer overlays.
- **expanded**: drawer height = full workspace height, in both pin modes. This replaces the current expand behavior that hides `top_split` entirely (`editor_bottom_panel.cpp:176-190`); the distraction-free-button reparenting in `_expand_button_toggled` is reworked to match.

Opening/closing animates the drawer height with a short tween (~0.15 s). The animation is skipped when the editor's global animation/reduced-motion setting says so; if no such setting exists, add `interface/editor/animate_bottom_drawer` (default on). Verify during implementation.

Esc closes the drawer when focus is inside it and it is unpinned. Pinned panels ignore Esc, like any docked panel.

Auto-raise events (debugger errors, output on run) open the drawer in whatever mode the target panel's pin state says. Tab auto-switch locking is available from the drawer tab context menu (see behavior item 4).

### 3. Resize and persistence

- A grabber on the drawer's top edge provides drag-resize in both modes, replacing the split dragger (`DRAGGER_VISIBLE` logic in `editor_bottom_panel.cpp:103-129`).
- Per-dock heights keep the existing `dock_offsets` map keyed by `get_effective_layout_key()` and the `bottom_panel_offsets` config key (`editor_bottom_panel.cpp:88-147`). Only the source of truth changes from split offset to drawer height.
- Pin state persists as a parallel dictionary (`bottom_panel_pinned`) plus an explicit `bottom_panel_pinned_by_default` bool in the same layout section, saved/loaded alongside `bottom_panel_offsets`.
- **Migration:** old layouts load cleanly — a stored offset becomes the drawer height. The pin default is persisted explicitly as `bottom_panel_pinned_by_default`: configs written before the drawer lack the key and read as **pinned** (preserves familiar behavior on upgrade, stable across save/load cycles); the built-in default layout writes the key as **false**, so fresh installs get the unpinned overlay experience. Inferring the default from the presence of the `bottom_panel_pinned` dictionary is not sufficient — the first layout save would write an empty dictionary and silently flip upgraded users to unpinned.

### 4. Dock eligibility and the pin remap

- `EditorDockManager::_is_slot_available` (`editor_dock_manager.cpp:1124-1126`) is relaxed so the bottom slot accepts every dock regardless of `available_layouts`. Side-slot eligibility is unchanged.
- Docks implementing `update_layout()` re-orientation (e.g. FileSystemDock, `filesystem_dock.cpp:4150`) keep doing so; vertical-only docks render vertically at drawer width.
- The pin button becomes the dock/undock toggle (icon/position reused). `lock_panel_switching` moves to the drawer tab context menu as a check item.

### 5. Out of scope

- Per-dock horizontal layouts for vertical docks (follow-ups as needed).
- Any change to side dock slots, floating docks, or the layout config schema beyond the one added `bottom_panel_pinned` key.
- Main-screen or scene-tab behavior (tracked separately in #820 / #821).

## Testing

**Automated.** Factor the geometry state machine (open/pin/expand transitions, inset math, offset + pin persistence round-trip) so it is assertable in C++ doctests where an editor instance is available. At minimum: persistence round-trip of heights and pin states through a `ConfigFile`, and inset math for all state combinations. *Status: inset math and legacy migration are covered by `tests/editor/test_bottom_drawer_geometry.h`; the `ConfigFile` round-trip test is deferred to #838 because `EditorBottomPanel`'s constructor requires editor scaffolding that headless unit tests cannot instantiate today.*

**Manual verification matrix** (run before merge):

| Scenario | Expect |
|---|---|
| Toggle each default bottom panel via tab and shortcut | Drawer slides over workspace; workspace does not resize |
| Pin a panel, toggle it | Workspace shrinks/restores exactly like the old behavior |
| Mixed pin states, switch tabs | Inset appears only for pinned panels |
| Drag resize, both modes | Height persists per panel across close/reopen |
| Expand, both modes | Drawer covers full workspace height and restores |
| Esc with focus in drawer | Closes when unpinned; no-op when pinned |
| Drag Inspector (vertical-only dock) into the drawer | Accepted, renders vertically, moves back out cleanly |
| Inspector in drawer with a very short editor window | Degenerate clamping stays usable; no clipped strip or stranded grabber |
| Run project with errors | Debugger/output auto-raises per its pin state; context-menu lock prevents tab stealing |
| Make a drawer dock floating and return | Unchanged from today |
| Distraction-free mode | Drawer and workspace behave; no dangling button reparenting |
| Restart editor | Open tab, heights, pin states restored; pre-existing layout loads pinned |

## Key code references

- `editor/gui/editor_bottom_panel.{h,cpp}` — panel, `_repaint`, expand, offsets (`.cpp:88-147,103-129,176-197,203-223`)
- `editor/editor_node.cpp:8863-8982,9344-9351` — center layout construction and panel registration
- `editor/docks/editor_dock_manager.cpp:467,1003,1124` — dock moves, slot registration, availability gate
- `editor/docks/dock_constants.h:35-53` — slots and layout flags
