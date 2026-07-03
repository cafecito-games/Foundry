# Editor Bottom Drawer — Design

**Date:** 2026-07-02
**Status:** Approved (v1 implemented; presentation v2 approved 2026-07-02, see final section)
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

## Presentation v2 — floating island and status strip

**Approved 2026-07-02** after the user reviewed v1 against reference footage of the desired experience. v1's drawer mechanics (dock slot, per-panel heights, pin persistence, tween, any-dock acceptance, Esc) are kept; the visible presentation changes in three ways:

1. **The open drawer is a floating island, not an edge-to-edge strip.** A horizontally centered card with equal side margins, rounded top corners and a subtle border, hovering directly over the content (no backdrop dim). Island width is 64% of the window width, clamped to `[480 * EDSCALE, window_width - 48 * EDSCALE]`; height is the panel's stored body height. The island's bottom edge sits flush on the status strip.
2. **The island overlays the whole window, not just the center column.** `EditorBottomPanel` reparents from `center_overlay` to `gui_base` (a plain `Panel` that already hosts the dock drag hints), drawn above `main_vbox`, positioned with a manual rect on every geometry update. `center_overlay` remains for the workspace inset only.
3. **The visible tab bar is replaced by a slim full-window status strip.** A new `EditorBottomDrawerStrip` control is the last child of `main_vbox` (below `main_hsplit`, spanning under the dock columns). Left side: one toggle per bottom dock (icon + short title), a close `×` on the active one, and right-click on a toggle opens the existing `DockContextPopup` for that dock. Right side: the `EditorToaster`, the version button, and the pin + expand toggles (these two visible only while a panel is open) — all of which move out of the TabContainer's tab-bar `bottom_hbox`. The TabContainer's own tab bar is hidden (`set_tabs_visible(false)`); the TabContainer remains the registered `DOCK_SLOT_BOTTOM` container and the strip mirrors its tabs via its signals, so `add_item`/`remove_item`/`make_item_visible`/shortcut toggles/auto-raise keep working unmodified.

**Pin semantics (geometry-only, kept from v1):** unpinned + open = the centered island. Pinned + open = the panel's rect aligns flush over the center column (x/width taken from `top_split`'s global rect), square-cornered, with the existing workspace inset reserving its height — visually identical to a docked panel, still no reparenting between modes. Expanded = full height above the strip (island keeps its side margins when unpinned). Closed = the panel hides entirely; the strip is the collapsed representation.

**Geometry:** island/pinned rect math goes into `BottomDrawerGeometry` as pure helpers (testable ints in, rect out). The slide animation tweens the island's y position from the strip's top edge; the existing target-change tween lifecycle carries over. The grabber stays on the island's top edge.

**Drag-and-drop while closed:** `EditorDockDragHint` sizes its drop rect from the slot container's global rect (`editor/docks/editor_dock_manager.cpp:276`). With the panel hidden when closed, the bottom slot's hint must instead cover the strip (union of strip and island rects when open). Without this, docks cannot be dragged into a closed drawer.

**Theme:** two new editor styleboxes — the island card (rounded top corners, border, opaque panel fill) and the strip background — registered alongside the existing `BottomPanel` styles in the editor theme.

**Out of scope for v2:** horizontal island resizing, per-panel island widths, backdrop dim, and any change to persistence keys (heights and pin states carry over unchanged).

### v2 verification additions

| Scenario | Expect |
|---|---|
| Open each panel unpinned | Centered island with margins floats over docks and workspace; strip stays visible |
| Pin a panel | Panel snaps flush over the center column; workspace shrinks; square corners |
| Drag a dock onto the strip while the drawer is closed | Drop accepted into the bottom slot |
| Right-click a strip toggle | DockContextPopup opens for that dock (move/float/close/lock) |
| Toaster + version button | Render in the strip's right side; toasts still appear |
| Very narrow window | Island clamps to `window - 48 * EDSCALE`, never underflows |
