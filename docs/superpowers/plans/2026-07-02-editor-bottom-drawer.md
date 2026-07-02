# Editor Bottom Drawer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert the editor's in-flow bottom panel into an overlay drawer that slides up over the workspace, accepts any dock, and can be pinned back into the layout flow per panel.

**Architecture:** Approved approach A ("single overlay host") from `docs/superpowers/specs/2026-07-02-editor-bottom-drawer-design.md`. `center_split` (a vertical `DockSplitContainer`) is deleted; a plain `Control` (`center_overlay`) holds `top_split` (full-rect) and `EditorBottomPanel` (bottom-anchored, height driven by drawer state). Pinning reserves a bottom inset on `top_split` instead of reparenting anything. Geometry math lives in pure header-only helpers so it is unit-testable.

**Tech Stack:** C++ (engine editor code), SCons build, doctest for unit tests. Branch: `csueiras/editor-bottom-drawer`.

**Build command (macOS, this machine):** `scons platform=macos target=editor dev_build=yes tests=yes`
**Binary:** `bin/foundry.macos.editor.dev.arm64`
**Full test suite:** `./bin/foundry.macos.editor.dev.arm64 --test --force-colors` (trust the `[doctest] Status: SUCCESS!` line; ObjectDB leak warnings at exit are known noise)
**Manual runs:** open any local project: `./bin/foundry.macos.editor.dev.arm64 --path <project-with-project.foundry> --editor`

**Key existing code (read before starting):**
- `editor/gui/editor_bottom_panel.{h,cpp}` — the panel being converted (304-line cpp; whole file is in scope)
- `editor/editor_node.cpp:8892-8902` (center_vb/center_split), `:8952-8955` (top_split), `:9334-9353` (default offsets, panel registration), `:8458-8460` (`_bottom_panel_resized`), `:6780-6782` (`set_center_split_offset`, dead), `:1600-1601` (`_vp_resized`, empty)
- `editor/editor_node.h:308` (center_split member), `:761` (get_center_split), `:822` (set_center_split_offset)
- `editor/docks/editor_dock_manager.cpp:131` (drag-hint availability), `:1124-1126` (`DockContextPopup::_is_slot_available`), `:1269-1297` (`_update_buttons`), `:1320-1374` (DockContextPopup constructor)
- `editor/docks/editor_dock.h` — `EditorDock` API (`get_effective_layout_key`, `available_layouts`)

---

### Task 1: BottomDrawerGeometry pure helpers with unit tests

**Goal:** Header-only, dependency-free geometry/migration math for the drawer, fully covered by doctests.

**Files:**
- Create: `editor/gui/bottom_drawer_geometry.h`
- Create: `tests/editor/test_bottom_drawer_geometry.h`
- Modify: `tests/test_main.cpp` (add include, alphabetical order among the `tests/editor/` includes at lines 40-47)

**Acceptance Criteria:**
- [ ] All four helpers implemented as pure static functions with no editor/scene includes
- [ ] Doctests cover: closed/open/expanded heights, pinned/unpinned/expanded insets, clamping at both bounds, legacy-negative / legacy-positive / zero stored values
- [ ] `--test-case="*BottomDrawerGeometry*"` passes

**Verify:** `./bin/foundry.macos.editor.dev.arm64 --test --test-case="*BottomDrawerGeometry*" --force-colors` → all assertions pass

**Steps:**

- [ ] **Step 1: Write the failing test**

Create `tests/editor/test_bottom_drawer_geometry.h` (use the standard engine copyright header from a neighboring test file, e.g. `tests/editor/test_editor_autoload_settings.h`):

```cpp
#pragma once

#include "editor/gui/bottom_drawer_geometry.h"

#include "tests/test_macros.h"

namespace TestBottomDrawerGeometry {

TEST_CASE("[Editor][BottomDrawerGeometry] Drawer height") {
	// Closed: only the tab strip is visible.
	CHECK(BottomDrawerGeometry::drawer_height(false, false, 30, 200, 600) == 30);
	// Closed and expanded flag leftover: still only the strip.
	CHECK(BottomDrawerGeometry::drawer_height(false, true, 30, 200, 600) == 30);
	// Open: strip + body.
	CHECK(BottomDrawerGeometry::drawer_height(true, false, 30, 200, 600) == 230);
	// Open + expanded: full area.
	CHECK(BottomDrawerGeometry::drawer_height(true, true, 30, 200, 600) == 600);
	// Open body taller than the area is capped by the area.
	CHECK(BottomDrawerGeometry::drawer_height(true, false, 30, 900, 600) == 600);
}

TEST_CASE("[Editor][BottomDrawerGeometry] Workspace inset") {
	// Closed: workspace reserves only the strip.
	CHECK(BottomDrawerGeometry::workspace_inset(false, false, false, 30, 200, 600) == 30);
	CHECK(BottomDrawerGeometry::workspace_inset(false, true, false, 30, 200, 600) == 30);
	// Open + unpinned: overlay, workspace still reserves only the strip.
	CHECK(BottomDrawerGeometry::workspace_inset(true, false, false, 30, 200, 600) == 30);
	// Open + pinned: workspace reserves strip + body.
	CHECK(BottomDrawerGeometry::workspace_inset(true, true, false, 30, 200, 600) == 230);
	// Expanded covers the workspace in both pin modes; inset stays strip-only so
	// un-expanding restores instantly.
	CHECK(BottomDrawerGeometry::workspace_inset(true, false, true, 30, 200, 600) == 30);
	CHECK(BottomDrawerGeometry::workspace_inset(true, true, true, 30, 200, 600) == 30);
	// Pinned body taller than the area is capped so the inset never exceeds the area.
	CHECK(BottomDrawerGeometry::workspace_inset(true, true, false, 30, 900, 600) == 600);
}

TEST_CASE("[Editor][BottomDrawerGeometry] Clamp body height") {
	// Within range: unchanged.
	CHECK(BottomDrawerGeometry::clamp_body_height(200, 50, 30, 600) == 200);
	// Below content minimum: raised.
	CHECK(BottomDrawerGeometry::clamp_body_height(10, 50, 30, 600) == 50);
	// Above area minus strip: lowered.
	CHECK(BottomDrawerGeometry::clamp_body_height(900, 50, 30, 600) == 570);
	// Degenerate area smaller than the minimum: minimum wins.
	CHECK(BottomDrawerGeometry::clamp_body_height(200, 50, 30, 40) == 50);
}

TEST_CASE("[Editor][BottomDrawerGeometry] Migrate stored layout values") {
	// Legacy split offsets were negative; magnitude is the panel height.
	CHECK(BottomDrawerGeometry::body_height_from_stored(-450, 0) == 450);
	// New-format positive values pass through.
	CHECK(BottomDrawerGeometry::body_height_from_stored(300, 0) == 300);
	// Zero or unset falls back.
	CHECK(BottomDrawerGeometry::body_height_from_stored(0, 120) == 120);
}

} // namespace TestBottomDrawerGeometry
```

Add to `tests/test_main.cpp` (alphabetical among the `tests/editor/` includes):

```cpp
#include "tests/editor/test_bottom_drawer_geometry.h"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `scons platform=macos target=editor dev_build=yes tests=yes`
Expected: compile FAILURE — `editor/gui/bottom_drawer_geometry.h: No such file or directory`

- [ ] **Step 3: Write the implementation**

Create `editor/gui/bottom_drawer_geometry.h` (same engine copyright header):

```cpp
#pragma once

// Pure geometry and migration math for the editor bottom drawer.
// Kept free of scene/editor dependencies so it is unit-testable headlessly.
//
// Terms:
// - strip:  the always-visible tab strip along the bottom of the workspace area.
// - body:   the content region of the drawer above the strip when a tab is open.
// - area:   the full height of the workspace overlay region (center_overlay).
// - inset:  vertical space the workspace reserves at its bottom edge.
struct BottomDrawerGeometry {
	static int drawer_height(bool p_open, bool p_expanded, int p_strip_height, int p_body_height, int p_area_height) {
		if (!p_open) {
			return p_strip_height;
		}
		if (p_expanded) {
			return p_area_height;
		}
		int height = p_strip_height + p_body_height;
		return height < p_area_height ? height : p_area_height;
	}

	// Unpinned drawers overlay the workspace, so only the strip is reserved.
	// Expanded drawers cover the workspace entirely; keeping the inset at the
	// strip lets un-expanding restore the previous workspace size instantly.
	static int workspace_inset(bool p_open, bool p_pinned, bool p_expanded, int p_strip_height, int p_body_height, int p_area_height) {
		if (!p_open || !p_pinned || p_expanded) {
			return p_strip_height;
		}
		int inset = p_strip_height + p_body_height;
		return inset < p_area_height ? inset : p_area_height;
	}

	static int clamp_body_height(int p_body_height, int p_min_body_height, int p_strip_height, int p_area_height) {
		int max_body = p_area_height - p_strip_height;
		int height = p_body_height < max_body ? p_body_height : max_body;
		return height > p_min_body_height ? height : p_min_body_height;
	}

	// Layout configs written before the drawer stored center_split offsets,
	// which were negative when the panel was resized taller. New configs store
	// positive drawer body heights.
	static int body_height_from_stored(int p_stored, int p_fallback) {
		if (p_stored < 0) {
			return -p_stored;
		}
		return p_stored > 0 ? p_stored : p_fallback;
	}
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `scons platform=macos target=editor dev_build=yes tests=yes && ./bin/foundry.macos.editor.dev.arm64 --test --test-case="*BottomDrawerGeometry*" --force-colors`
Expected: `[doctest] Status: SUCCESS!`

- [ ] **Step 5: Commit**

```bash
git add editor/gui/bottom_drawer_geometry.h tests/editor/test_bottom_drawer_geometry.h tests/test_main.cpp
git commit -m "Add bottom drawer geometry helpers with tests"
```

---

### Task 2: Overlay control tree and drawer geometry (open/close/expand/resize)

**Goal:** The bottom panel becomes an overlay drawer: `center_split` is deleted, the panel is bottom-anchored inside a new plain `center_overlay` Control, opens over the workspace, resizes via a top-edge grabber, expands to full height, and persists heights (with legacy-offset migration).

**Files:**
- Modify: `editor/editor_node.h` (members ~line 308, accessors ~761, `set_center_split_offset` ~822, `_bottom_panel_resized` and `_vp_resized` declarations — grep for exact lines)
- Modify: `editor/editor_node.cpp` (`:1600` `_vp_resized`, `:6780` `set_center_split_offset`, `:8458` `_bottom_panel_resized`, `:8896-8902`, `:8952-8955`, `:9334-9338`, `:9344-9353`)
- Modify: `editor/gui/editor_bottom_panel.h`, `editor/gui/editor_bottom_panel.cpp`

**Acceptance Criteria:**
- [ ] Opening any bottom panel draws it over the workspace; the workspace does not resize
- [ ] The tab strip stays visible when collapsed and never overlaps the workspace (workspace reserves strip height)
- [ ] Grabber drag-resizes the open drawer; height is remembered per panel and across restarts
- [ ] Expand button grows the drawer to the full workspace height and back
- [ ] Legacy layout configs (negative offsets) load with sensible heights
- [ ] Full test suite still passes

**Verify:** build; run full suite; launch editor against a test project and check each criterion manually

**Steps:**

- [ ] **Step 1: Rewire the control tree in EditorNode**

`editor/editor_node.h`: replace the member at line ~308:

```cpp
	Control *center_overlay = nullptr;
```

(removing `DockSplitContainer *center_split = nullptr;`). Remove line ~761 `static DockSplitContainer *get_center_split() ...` and line ~822 `void set_center_split_offset(int p_offset);`. Grep and remove the declarations of `_bottom_panel_resized` and `_vp_resized`. Keep `get_top_split()`.

`editor/editor_node.cpp`: delete the bodies of `_vp_resized` (line ~1600), `set_center_split_offset` (~6780), and `_bottom_panel_resized` (~8458).

Replace the block at 8896-8902:

```cpp
	center_overlay = memnew(Control);
	center_overlay->set_name("CenterOverlay");
	center_overlay->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	center_vb->add_child(center_overlay);
```

Replace the `top_split` block at 8952-8955:

```cpp
	top_split = memnew(VSplitContainer);
	center_overlay->add_child(top_split);
	top_split->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	top_split->set_collapsed(true);
```

Replace the bottom-panel block at 9344-9353:

```cpp
	bottom_panel = memnew(EditorBottomPanel);
	editor_dock_manager->register_dock_slot(DockConstants::DOCK_SLOT_BOTTOM, bottom_panel, DockConstants::DOCK_LAYOUT_HORIZONTAL);
	bottom_panel->set_theme_type_variation("BottomPanel");
	center_overlay->add_child(bottom_panel);
	bottom_panel->set_anchors_and_offsets_preset(Control::PRESET_BOTTOM_WIDE);
	center_overlay->connect(SceneStringName(resized), callable_mp(bottom_panel, &EditorBottomPanel::update_drawer_geometry));

	log = memnew(EditorLog);
	editor_dock_manager->add_dock(log);
```

(This drops `center_split->set_dragger_visibility(...)`, the `drag_ended` connection formerly at 8902, and the `resized` → `_vp_resized` connection at 9353.)

Update the default layout dictionary at 9334-9338 to the new positive-height format:

```cpp
	{
		Dictionary offsets;
		offsets["Audio"] = 450;
		default_layout->set_value(EDITOR_NODE_CONFIG_SECTION, "bottom_panel_offsets", offsets);
	}
```

- [ ] **Step 2: Convert EditorBottomPanel to drawer geometry**

`editor/gui/editor_bottom_panel.h` — replace the private members/methods `dock_offsets` block area and the public offset API:

```cpp
	int previous_tab = -1;
	bool lock_panel_switching = false;
	bool drawer_expanded = false;
	bool grabber_dragging = false;
	int drag_start_body_height = 0;
	float drag_start_mouse_y = 0.0f;
	Control *grabber = nullptr;
	LocalVector<EditorDock *> bottom_docks;
	HashMap<String, int> dock_offsets;
```

Replace the declaration `void _update_center_split_offset();` with:

```cpp
	int _get_strip_height() const;
	int _get_body_height() const;
	void _set_body_height(int p_height);
	void _update_drawer_geometry();
	void _grabber_input(const Ref<InputEvent> &p_event);
```

In the public section, remove `set_bottom_panel_offset` / `get_bottom_panel_offset` and add:

```cpp
	void update_drawer_geometry();
```

`editor/gui/editor_bottom_panel.cpp` — add `#include "editor/gui/bottom_drawer_geometry.h"` and `#include "editor/themes/editor_scale.h"` (for `EDSCALE`), and remove the now-unused `#include "scene/gui/split_container.h"`. Replace `set_bottom_panel_offset`/`get_bottom_panel_offset`/`_update_center_split_offset` with:

```cpp
int EditorBottomPanel::_get_strip_height() const {
	int height = get_tab_bar()->get_combined_minimum_size().height;
	Ref<StyleBox> tabbar_style = get_theme_stylebox(SNAME("tabbar_background"));
	if (tabbar_style.is_valid()) {
		height += tabbar_style->get_minimum_size().height;
	}
	return height;
}

int EditorBottomPanel::_get_body_height() const {
	Control *tab_control = get_current_tab_control();
	if (!tab_control) {
		return 0;
	}
	const int min_body = tab_control->get_combined_minimum_size().height;
	int stored = min_body;
	EditorDock *dock = Object::cast_to<EditorDock>(tab_control);
	if (dock) {
		HashMap<String, int>::ConstIterator E = dock_offsets.find(dock->get_effective_layout_key());
		if (E) {
			stored = E->value;
		}
	}
	Control *area = get_parent_control();
	const int area_height = area ? area->get_size().height : stored + _get_strip_height();
	return BottomDrawerGeometry::clamp_body_height(stored, min_body, _get_strip_height(), area_height);
}

void EditorBottomPanel::_set_body_height(int p_height) {
	EditorDock *dock = Object::cast_to<EditorDock>(get_current_tab_control());
	if (!dock) {
		return;
	}
	Control *area = get_parent_control();
	const int area_height = area ? area->get_size().height : p_height + _get_strip_height();
	const int min_body = get_current_tab_control()->get_combined_minimum_size().height;
	dock_offsets[dock->get_effective_layout_key()] = BottomDrawerGeometry::clamp_body_height(p_height, min_body, _get_strip_height(), area_height);
	_update_drawer_geometry();
}

void EditorBottomPanel::update_drawer_geometry() {
	_update_drawer_geometry();
}

void EditorBottomPanel::_update_drawer_geometry() {
	Control *area = get_parent_control();
	if (!area) {
		return;
	}
	const bool open = get_current_tab() != -1;
	const int strip_height = _get_strip_height();
	const int area_height = area->get_size().height;
	const int body_height = open ? _get_body_height() : 0;

	const int drawer_height = BottomDrawerGeometry::drawer_height(open, drawer_expanded, strip_height, body_height, area_height);
	set_offset(SIDE_TOP, -drawer_height);

	const int inset = BottomDrawerGeometry::workspace_inset(open, false, drawer_expanded, strip_height, body_height, area_height);
	VSplitContainer *top_split = EditorNode::get_top_split();
	if (top_split) {
		top_split->set_offset(SIDE_BOTTOM, -inset);
	}
	grabber->set_visible(open && !drawer_expanded);
}

void EditorBottomPanel::_grabber_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		if (mb->is_pressed()) {
			grabber_dragging = true;
			drag_start_mouse_y = grabber->get_global_position().y + mb->get_position().y;
			drag_start_body_height = _get_body_height();
		} else {
			grabber_dragging = false;
			EditorNode::get_singleton()->save_editor_layout_delayed();
		}
	}

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid() && grabber_dragging) {
		const float mouse_y = grabber->get_global_position().y + mm->get_position().y;
		_set_body_height(drag_start_body_height + int(drag_start_mouse_y - mouse_y));
	}
}
```

The `false` literal in the `workspace_inset` call is the pin state; Task 3 replaces it with `_is_current_pinned()`.

- [ ] **Step 3: Update the callers of the old split machinery inside EditorBottomPanel**

`_on_tab_changed` (line 60):

```cpp
void EditorBottomPanel::_on_tab_changed(int p_idx) {
	_update_drawer_geometry();
	_repaint();
}
```

`_repaint` (line 103) — remove the whole `center_split` block (lines 116-120), keeping the rest intact:

```cpp
void EditorBottomPanel::_repaint() {
	bool panel_collapsed = get_current_tab() == -1;

	if (panel_collapsed && get_popup()) {
		set_popup(nullptr);
	} else if (!panel_collapsed && !get_popup()) {
		set_popup(layout_popup);
	}
	if (!panel_collapsed && (previous_tab != -1)) {
		return;
	}
	previous_tab = get_current_tab();

	pin_button->set_visible(!panel_collapsed);
	expand_button->set_visible(!panel_collapsed);
	if (expand_button->is_pressed()) {
		_expand_button_toggled(!panel_collapsed);
	} else {
		_theme_changed();
	}
}
```

`_expand_button_toggled` (line 176) — replace the `top_split` hide with drawer expansion, keeping the distraction-free button handling:

```cpp
void EditorBottomPanel::_expand_button_toggled(bool p_pressed) {
	drawer_expanded = p_pressed;
	_update_drawer_geometry();

	Button *distraction_free = EditorNode::get_singleton()->get_distraction_free_button();
	distraction_free->set_meta("_scene_tabs_owned", !p_pressed);
	EditorNode::get_singleton()->update_distraction_free_button_theme();
	if (p_pressed) {
		distraction_free->reparent(bottom_hbox);
		bottom_hbox->move_child(distraction_free, -2);
	} else {
		distraction_free->get_parent()->remove_child(distraction_free);
		EditorSceneTabs::get_singleton()->add_extra_button(distraction_free);
	}
	_theme_changed();
}
```

`load_layout_from_config` (line 139) — migrate values through the helper:

```cpp
void EditorBottomPanel::load_layout_from_config(Ref<ConfigFile> p_config_file, const String &p_section) {
	const Dictionary offsets = p_config_file->get_value(p_section, "bottom_panel_offsets", Dictionary());
	const LocalVector<Variant> offset_list = offsets.get_key_list();

	for (const Variant &v : offset_list) {
		dock_offsets[v] = BottomDrawerGeometry::body_height_from_stored(offsets[v], 0);
	}
	_update_drawer_geometry();
}
```

(`save_layout_to_config` is unchanged — it already writes `dock_offsets` verbatim, which now holds positive heights.)

In `_notification`'s `NOTIFICATION_THEME_CHANGED` case, append `_update_drawer_geometry();` after the icon updates (strip height depends on theme metrics).

In the constructor (before the `bottom_hbox` block), create the grabber as an internal child so the TabContainer does not treat it as a tab:

```cpp
	grabber = memnew(Control);
	grabber->set_name("DrawerGrabber");
	add_child(grabber, false, Node::INTERNAL_MODE_FRONT);
	grabber->set_anchors_and_offsets_preset(Control::PRESET_TOP_WIDE);
	grabber->set_offset(SIDE_BOTTOM, 6 * EDSCALE);
	grabber->set_default_cursor_shape(Control::CURSOR_VSIZE);
	grabber->hide();
	grabber->connect(SceneStringName(gui_input), callable_mp(this, &EditorBottomPanel::_grabber_input));
```

- [ ] **Step 4: Build and fix fallout**

Run: `scons platform=macos target=editor dev_build=yes tests=yes`
Expected: any remaining reference to `get_center_split`, `set_center_split_offset`, `_bottom_panel_resized`, `_vp_resized`, or the removed offset API fails to compile — remove/adjust each (there should be none outside the files above; `grep -rn "get_center_split\|set_center_split_offset" editor/` must return nothing).

- [ ] **Step 5: Run the full suite and verify manually**

Run: `./bin/foundry.macos.editor.dev.arm64 --test --force-colors`
Expected: `[doctest] Status: SUCCESS!`

Manual (launch the editor with a test project):
1. Click the Output tab — panel slides over the viewport; viewport does not shrink.
2. Drag the grabber — the panel resizes; close and reopen — height retained.
3. Expand button — full-height; un-expand — restores.
4. Restart the editor — height persists.
5. Delete the project's cached layout (or use a fresh profile) and confirm a legacy `editor_layout.cfg` with negative `bottom_panel_offsets` values still produces sane heights.

- [ ] **Step 6: Commit**

```bash
git add editor/editor_node.h editor/editor_node.cpp editor/gui/editor_bottom_panel.h editor/gui/editor_bottom_panel.cpp
git commit -m "Convert bottom panel to overlay drawer geometry"
```

---

### Task 3: Pin-to-dock, pin button remap, and lock-switching relocation

**Goal:** Pinning a panel docks the drawer in-flow (workspace reserves its height); pin state is per panel and persisted with migration defaults; the old lock-tab-switching behavior moves to the dock context popup.

**Files:**
- Modify: `editor/gui/editor_bottom_panel.h`, `editor/gui/editor_bottom_panel.cpp`
- Modify: `editor/docks/editor_dock_manager.h` (DockContextPopup members ~line 195-236), `editor/docks/editor_dock_manager.cpp` (`_update_buttons` ~1269, constructor ~1320)
- Modify: `editor/editor_node.cpp` (default layout block ~9334)

**Acceptance Criteria:**
- [ ] Pin button toggles per-panel docked mode; pinned + open shrinks the workspace, unpinned overlays
- [ ] Pin button state follows the active tab; state persists across restarts
- [ ] Layout configs without `bottom_panel_pinned` (upgrades) default every panel to pinned; fresh installs (default layout) default to unpinned
- [ ] "Lock Tab Switching" check in the dock context popup (bottom docks only) reproduces the old pin behavior; auto-raise (`make_item_visible`) still respects it
- [ ] Full test suite passes

**Verify:** build; full suite; manual pin/unpin + restart + upgrade-config check

**Steps:**

- [ ] **Step 1: Add pin state to EditorBottomPanel**

`editor/gui/editor_bottom_panel.h` — add to the private members:

```cpp
	HashMap<String, bool> dock_pinned;
	bool pinned_by_default = false;
```

Add the private method `bool _is_current_pinned() const;` and the public method `void set_switch_locked(bool p_locked);`.

`editor/gui/editor_bottom_panel.cpp`:

```cpp
bool EditorBottomPanel::_is_current_pinned() const {
	EditorDock *dock = Object::cast_to<EditorDock>(get_current_tab_control());
	if (!dock) {
		return false;
	}
	HashMap<String, bool>::ConstIterator E = dock_pinned.find(dock->get_effective_layout_key());
	return E ? E->value : pinned_by_default;
}

void EditorBottomPanel::set_switch_locked(bool p_locked) {
	lock_panel_switching = p_locked;
}
```

Repurpose `_pin_button_toggled`:

```cpp
void EditorBottomPanel::_pin_button_toggled(bool p_pressed) {
	EditorDock *dock = Object::cast_to<EditorDock>(get_current_tab_control());
	if (dock) {
		dock_pinned[dock->get_effective_layout_key()] = p_pressed;
	}
	_update_drawer_geometry();
	EditorNode::get_singleton()->save_editor_layout_delayed();
}
```

In `_update_drawer_geometry`, replace the `false` pin literal:

```cpp
	const int inset = BottomDrawerGeometry::workspace_inset(open, _is_current_pinned(), drawer_expanded, strip_height, body_height, area_height);
```

In `_on_tab_changed`, sync the button before updating geometry:

```cpp
void EditorBottomPanel::_on_tab_changed(int p_idx) {
	pin_button->set_pressed_no_signal(_is_current_pinned());
	_update_drawer_geometry();
	_repaint();
}
```

In the constructor, update the tooltip:

```cpp
	pin_button->set_tooltip_text(TTRC("Dock the bottom drawer, pushing the workspace up instead of covering it."));
```

In `make_item_visible`, the lock guard referenced the pin button's visibility as an "is open" proxy; keep the same semantics explicitly:

```cpp
	if (!p_ignore_lock && lock_panel_switching && get_current_tab() != -1) {
		return;
	}
```

- [ ] **Step 2: Persist pin state with migration defaults**

`save_layout_to_config` — append after the offsets block. The default is persisted explicitly: inferring it from the presence of the `bottom_panel_pinned` key would decay after one save cycle (the first save writes an empty dictionary and would flip upgraded users to unpinned on the next launch).

```cpp
	Dictionary pinned;
	for (const KeyValue<String, bool> &E : dock_pinned) {
		pinned[E.key] = E.value;
	}
	p_config_file->set_value(p_section, "bottom_panel_pinned", pinned);
	p_config_file->set_value(p_section, "bottom_panel_pinned_by_default", pinned_by_default);
```

`load_layout_from_config` — append before the final `_update_drawer_geometry();`. Configs written before the drawer lack `bottom_panel_pinned_by_default` and read as pinned (familiar in-flow behavior on upgrade):

```cpp
	pinned_by_default = p_config_file->get_value(p_section, "bottom_panel_pinned_by_default", true);
	const Dictionary pinned = p_config_file->get_value(p_section, "bottom_panel_pinned", Dictionary());
	const LocalVector<Variant> pinned_list = pinned.get_key_list();
	for (const Variant &v : pinned_list) {
		dock_pinned[v] = pinned[v];
	}
	pin_button->set_pressed_no_signal(_is_current_pinned());
```

`editor/editor_node.cpp` default layout block (~9334) — mark the built-in default as drawer-aware so fresh installs are unpinned:

```cpp
	{
		Dictionary offsets;
		offsets["Audio"] = 450;
		default_layout->set_value(EDITOR_NODE_CONFIG_SECTION, "bottom_panel_offsets", offsets);
		default_layout->set_value(EDITOR_NODE_CONFIG_SECTION, "bottom_panel_pinned_by_default", false);
	}
```

- [ ] **Step 3: Move lock-switching into DockContextPopup**

`editor/docks/editor_dock_manager.h` — in `DockContextPopup`'s private members add:

```cpp
	CheckButton *bottom_lock_button = nullptr;
```

and the method `void _bottom_lock_toggled(bool p_pressed);`. Forward-declare `class CheckButton;` at the top of the header alongside the other forward declarations.

`editor/docks/editor_dock_manager.cpp` — add `#include "scene/gui/check_button.h"`. In the constructor after `make_float_button` is added:

```cpp
	bottom_lock_button = memnew(CheckButton);
	bottom_lock_button->set_text(TTRC("Lock Tab Switching"));
	bottom_lock_button->set_tooltip_text(TTRC("Prevent other panels from automatically switching the active bottom drawer tab."));
	bottom_lock_button->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	bottom_lock_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	bottom_lock_button->connect(SceneStringName(toggled), callable_mp(this, &DockContextPopup::_bottom_lock_toggled));
	dock_select_popup_vb->add_child(bottom_lock_button);
```

New method:

```cpp
void DockContextPopup::_bottom_lock_toggled(bool p_pressed) {
	EditorNode::get_bottom_panel()->set_switch_locked(p_pressed);
}
```

In `_update_buttons` (before `reset_size();`):

```cpp
	const bool in_bottom_slot = context_dock && context_dock->get_parent() == EditorNode::get_bottom_panel();
	bottom_lock_button->set_visible(in_bottom_slot);
	if (in_bottom_slot) {
		bottom_lock_button->set_pressed_no_signal(EditorNode::get_bottom_panel()->is_locked());
	}
```

- [ ] **Step 4: Build, test, verify**

Run: `scons platform=macos target=editor dev_build=yes tests=yes && ./bin/foundry.macos.editor.dev.arm64 --test --force-colors`
Expected: `[doctest] Status: SUCCESS!`

Manual:
1. Open Output unpinned — overlays. Press pin — workspace shrinks under it. Unpin — overlays again.
2. Pin Output, leave FileSystem unpinned; switch tabs — pin button and inset follow each panel.
3. Restart — pin states restored.
4. Edit the layout config to delete the `bottom_panel_pinned` line, restart — all panels behave pinned (upgrade path).
5. Right-click a bottom tab → context popup shows "Lock Tab Switching"; enable it, run a project with an error — the debugger does not steal the active tab. Side docks' popups don't show the check.

- [ ] **Step 5: Commit**

```bash
git add editor/gui/editor_bottom_panel.h editor/gui/editor_bottom_panel.cpp editor/docks/editor_dock_manager.h editor/docks/editor_dock_manager.cpp editor/editor_node.cpp
git commit -m "Add per-panel pin-to-dock to the bottom drawer"
```

---

### Task 4: Slide animation, Esc-to-close, editor setting

**Goal:** Unpinned open/close animates with a short tween gated by a new editor setting; Esc closes an unpinned drawer when focus is inside it.

**Files:**
- Modify: `editor/gui/editor_bottom_panel.h`, `editor/gui/editor_bottom_panel.cpp`
- Modify: `editor/settings/editor_settings.cpp` (register the setting near the other `interface/editor/` booleans; match the neighbors' macro style, e.g. `EDITOR_SETTING_BASIC`)

**Acceptance Criteria:**
- [ ] Unpinned open/close slides (~0.15 s); pinned and drag-resize apply instantly
- [ ] `interface/editor/animate_bottom_drawer` (default on) disables the slide when off
- [ ] Esc with focus inside an unpinned open drawer closes it and consumes the event; pinned drawers ignore Esc
- [ ] Full test suite passes

**Verify:** build; full suite; manual animation + Esc checks with the setting on and off

**Steps:**

- [ ] **Step 1: Register the editor setting**

In `editor/settings/editor_settings.cpp`, next to the other `interface/editor/` booleans (match the neighboring macro style exactly):

```cpp
	EDITOR_SETTING_BASIC(Variant::BOOL, PROPERTY_HINT_NONE, "interface/editor/animate_bottom_drawer", true, "")
```

- [ ] **Step 2: Animate the drawer height**

`editor/gui/editor_bottom_panel.h` — add private members and a method:

```cpp
	Ref<Tween> drawer_tween;
	int last_drawer_height = -1;

	void _set_drawer_top_offset(float p_offset);
```

`editor/gui/editor_bottom_panel.cpp`:

```cpp
void EditorBottomPanel::_set_drawer_top_offset(float p_offset) {
	set_offset(SIDE_TOP, p_offset);
}
```

In `_update_drawer_geometry`, replace the plain `set_offset(SIDE_TOP, -drawer_height);` with:

```cpp
	if (drawer_tween.is_valid()) {
		drawer_tween->kill();
		drawer_tween.unref();
	}
	const bool animate = !_is_current_pinned() && !grabber_dragging && is_inside_tree() && last_drawer_height != drawer_height && EDITOR_GET("interface/editor/animate_bottom_drawer");
	if (animate) {
		drawer_tween = create_tween();
		drawer_tween->tween_method(callable_mp(this, &EditorBottomPanel::_set_drawer_top_offset), get_offset(SIDE_TOP), -(float)drawer_height, 0.15)->set_trans(Tween::TRANS_CUBIC)->set_ease(Tween::EASE_OUT);
	} else {
		set_offset(SIDE_TOP, -drawer_height);
	}
	last_drawer_height = drawer_height;
```

Add `#include "editor/settings/editor_settings.h"` and `#include "scene/animation/tween.h"` if not already present.

- [ ] **Step 3: Esc-to-close**

`editor/gui/editor_bottom_panel.h` — add to the protected section:

```cpp
	virtual void shortcut_input(const Ref<InputEvent> &p_event) override;
```

`editor/gui/editor_bottom_panel.cpp`:

```cpp
void EditorBottomPanel::shortcut_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> k = p_event;
	if (k.is_null() || !k->is_pressed() || k->is_echo() || k->get_keycode() != Key::ESCAPE) {
		return;
	}
	if (get_current_tab() == -1 || _is_current_pinned()) {
		return;
	}
	Control *focus_owner = get_viewport()->gui_get_focus_owner();
	if (!focus_owner || !is_ancestor_of(focus_owner)) {
		return;
	}
	hide_bottom_panel();
	get_viewport()->set_input_as_handled();
}
```

In the constructor add `set_process_shortcut_input(true);`.

- [ ] **Step 4: Build, test, verify**

Run: `scons platform=macos target=editor dev_build=yes tests=yes && ./bin/foundry.macos.editor.dev.arm64 --test --force-colors`
Expected: `[doctest] Status: SUCCESS!`

Manual:
1. Unpinned: open/close slides smoothly; toggling `interface/editor/animate_bottom_drawer` off makes it instant.
2. Pinned: no slide (instant), no workspace-inset jank.
3. Focus the Output panel's search box, press Esc — drawer closes and the keypress does not also clear a selection elsewhere. Pin it, press Esc — nothing happens.
4. Grabber drag never animates.

- [ ] **Step 5: Commit**

```bash
git add editor/gui/editor_bottom_panel.h editor/gui/editor_bottom_panel.cpp editor/settings/editor_settings.cpp
git commit -m "Animate bottom drawer and add Esc-to-close"
```

---

### Task 5: Relax the bottom-slot dock gate

**Goal:** Any dock — including vertical-only docks like Inspector and Scene Tree — can be dropped into the bottom drawer via drag-and-drop and the context-popup slot picker.

**Files:**
- Modify: `editor/docks/editor_dock_manager.cpp` (`:131` drag-hint check, `:1124-1126` `_is_slot_available`)

**Acceptance Criteria:**
- [ ] The slot picker draws the bottom slot as available for every dock
- [ ] Dragging the Inspector dock onto the bottom drawer's drag hint is accepted; it renders (vertical layout) and can be moved back out
- [ ] FileSystem dock still re-orients horizontally in the drawer
- [ ] A layout with a vertical dock saved in the bottom slot restores it there
- [ ] Full test suite passes

**Verify:** build; full suite; manual drag checks with Inspector and FileSystem docks

**Steps:**

- [ ] **Step 1: Relax both availability checks**

`DockContextPopup::_is_slot_available` (line ~1124):

```cpp
bool DockContextPopup::_is_slot_available(int p_slot) const {
	if (p_slot == DockConstants::DOCK_SLOT_BOTTOM) {
		// The bottom drawer accepts every dock regardless of its declared layouts.
		return true;
	}
	return context_dock->available_layouts & (EditorDock::DockLayout)EditorDockManager::get_singleton()->dock_slots[p_slot].layout;
}
```

`EditorDockDragHint` `NOTIFICATION_DRAG_BEGIN` (line ~131):

```cpp
			can_drop_dock = occupied_slot == DockConstants::DOCK_SLOT_BOTTOM || (dragged_dock->get_available_layouts() & (EditorDock::DockLayout)EditorDockManager::get_singleton()->dock_slots[occupied_slot].layout);
```

- [ ] **Step 2: Confirm the restore path has no third gate**

Read `EditorDockManager::load_docks_from_config` (around lines 714-809): confirm it calls `_move_dock` without an `available_layouts` check, so a vertical dock saved in `dock_9` restores. If a gate exists there, apply the same bottom-slot exemption.

- [ ] **Step 3: Build, test, verify**

Run: `scons platform=macos target=editor dev_build=yes tests=yes && ./bin/foundry.macos.editor.dev.arm64 --test --force-colors`
Expected: `[doctest] Status: SUCCESS!`

Manual:
1. Open the Inspector dock's context menu → slot picker: the bottom rectangle is selectable; click it — Inspector lands in the drawer, renders vertically at drawer width, tab and pin work.
2. Drag it back to a side slot — restored vertical dock.
3. Drag the FileSystem dock into the drawer — it re-orients horizontally (existing behavior preserved).
4. With Inspector in the drawer, restart the editor — it restores into the drawer.

- [ ] **Step 4: Commit**

```bash
git add editor/docks/editor_dock_manager.cpp
git commit -m "Allow any dock in the bottom drawer slot"
```

---

### Task 6: Full verification pass

**Goal:** The complete manual matrix from the spec passes, the suite is green, and lint gates pass.

**Files:** none expected (fixes only if verification fails)

**Acceptance Criteria:**
- [ ] Full C++ test suite: `[doctest] Status: SUCCESS!`
- [ ] `pre-commit run --all-files` clean
- [ ] Every row of the spec's manual verification matrix checked

**Verify:** commands below + the matrix

**Steps:**

- [ ] **Step 1: Run the automated gates**

```bash
scons platform=macos target=editor dev_build=yes tests=yes
./bin/foundry.macos.editor.dev.arm64 --test --force-colors
pre-commit run --all-files
```

- [ ] **Step 2: Run the manual matrix from the spec** (`docs/superpowers/specs/2026-07-02-editor-bottom-drawer-design.md`, "Manual verification matrix"):

- [ ] Toggle each default bottom panel via tab and shortcut → overlay, no workspace resize
- [ ] Pin a panel, toggle it → workspace shrinks/restores like the old behavior
- [ ] Mixed pin states, switch tabs → inset only for pinned panels
- [ ] Drag resize in both modes → height persists per panel
- [ ] Expand in both modes → full height and back
- [ ] Esc focus behavior → closes unpinned, no-op pinned
- [ ] Inspector dock into drawer and back → clean both ways
- [ ] Run a project with errors → auto-raise respects pin state; context-menu lock prevents tab stealing
- [ ] Float a drawer dock and return → unchanged
- [ ] Distraction-free mode → no dangling button reparenting (check the expand button interaction specifically)
- [ ] Restart → open tab, heights, pin states restored; config without `bottom_panel_pinned` loads pinned

- [ ] **Step 3: Fix anything that failed, re-run, commit fixes**

Each fix gets its own focused commit describing the failure it addresses.
