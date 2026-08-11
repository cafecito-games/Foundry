# Board Rail and Tile-Local Scene Modes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the visible main-screen buttons with a centered Board Rail and give every scene tile an independent, persistent 2D/3D editing mode.

**Architecture:** `EditorBoardSwitcher` remains a projection of `EditorBoardStrip`, but becomes the centered, themed title-bar control with one trailing menu. `ScenePaneTile` owns durable `SceneEditorMode`; `EditorNode` synchronizes the existing shared 2D/3D editor to the focused tile and uses existing secondary views for non-focused previews. `EditorMainScreen` remains the compatibility and plugin-selection layer, with its button container hidden rather than deleted.

**Tech Stack:** Godot/Foundry C++, scene GUI controls, editor theme variations, `ConfigFile` layout persistence, doctest C++ tests, editor automation workflows, SCons/Ninja through `scripts/agent_build.py`, and `scripts/review_gallery.py`.

---

## Scope and file map

Implement this as one feature because neither half is independently shippable: centering the Board Rail removes visible 2D/3D navigation, while tile-local mode controls rely on that removal to establish the new hierarchy.

### New files

- `editor/editor_scene_mode.h` — shared `SceneEditorMode` value type and stable persistence names.
- `editor/gui/editor_scene_mode_switcher.h` — stateless two-button scene-mode control interface.
- `editor/gui/editor_scene_mode_switcher.cpp` — mode-button construction, accessibility, and signal emission.
- `tests/editor/test_editor_scene_mode_switcher.h` — control behavior tests.
- `tests/editor/test_scene_pane_tile_mode.h` — inference, sticky state, persistence, and preview-state separation tests.
- `tests/editor/test_editor_tile_local_scene_modes.h` — real-editor subprocess acceptance test.

### Existing files with focused responsibilities

- `editor/editor_scene_pane_tile.{h,cpp}` — own and persist tile mode; mount the local switcher.
- `editor/scene/editor_scene_tabs.{h,cpp}` — accept an extra `Control` before the opened-scenes menu.
- `editor/editor_node.{h,cpp}` — route focus, shortcuts, global-screen return, and live-preview selection through tile mode.
- `editor/editor_main_screen.cpp` — route next/previous scene-mode selections through the focused tile and let tile state win layout restore.
- `editor/editor_interface.cpp` — keep `set_main_screen_editor("2D"|"3D")` compatible by routing it to the focused tile.
- `editor/gui/editor_board_switcher.{h,cpp}` — render the Board Rail, dedicated menu button, rename, and compact presentation.
- `editor/gui/editor_board_actions_menu.{h,cpp}` — add New Board, Overview, and optional compact board-selection entries.
- `editor/gui/editor_title_bar.cpp` — only if final sizing proves the center control cannot receive a usable width budget; do not couple it to board types.
- `editor/themes/theme_classic.cpp` and `editor/themes/theme_modern.cpp` — matching theme variations for the rail and tile mode control.
- `editor/automation/editor_automation_workspace.cpp` — report durable and presentation modes per tile.
- `editor/automation/editor_automation_acceptance_workflow.{h,cpp}` — independent-mode workflow.
- `editor/automation/editor_automation_workflow_registry.cpp` — register the workflow.
- `tests/editor/test_editor_board_switcher.h` — update existing behavior and add menu/compact tests.
- `tests/editor/test_editor_automation_workspace.h` — assert mode fields in captured state.
- `tests/test_main.cpp` — include the three new doctest headers.

Run all implementation work in a dedicated worktree. Preserve the existing untracked `.opencode/`, `.superpowers/`, and `modules/foundry_script/tests/scripts/.foundry/autoload_index_cache.cfg`; they are not part of this feature.

## Task 0: Create the isolated worktree and visual baseline

**Files:**
- Create outside git: `/tmp/foundry-board-rail-review/before-board-switcher.png`

- [ ] **Step 1: Create the dedicated worktree**

Use `superpowers:using-git-worktrees` from the `develop` checkout and name the feature branch `feature/board-rail-tile-modes`. Confirm `git status --short` in the new worktree is empty before editing.

- [ ] **Step 2: Prepare a disposable baseline project**

Run from the unchanged `develop` checkout before building the feature:

```bash
mkdir -p /tmp/foundry-board-rail-review
BASELINE_PROJECT=$(mktemp -d /tmp/foundry-board-rail-baseline.XXXXXX)
cp -R tests/fixtures/editor_automation_workflow/. "$BASELINE_PROJECT"
echo "$BASELINE_PROJECT"
```

Expected: the final line prints an absolute temporary project directory containing `project.foundry`.

- [ ] **Step 3: Capture the current title bar**

Call `foundry_launch_editor` with `project` set to the exact directory printed in Step 2. Use `foundry_capture_screenshot` to save `/tmp/foundry-board-rail-review/before-board-switcher.png`, showing the title bar with the current board buttons and 2D/3D/Game/Script control, then call `foundry_disconnect`.

Expected: the screenshot exists and visibly shows the competing board and main-screen controls. Do not commit the temporary project or screenshot.

## Task 1: Add the scene-mode value and stateless switcher

**Files:**
- Create: `editor/editor_scene_mode.h`
- Create: `editor/gui/editor_scene_mode_switcher.h`
- Create: `editor/gui/editor_scene_mode_switcher.cpp`
- Create: `tests/editor/test_editor_scene_mode_switcher.h`
- Modify: `tests/test_main.cpp:40-110`

- [ ] **Step 1: Write the failing control tests**

Create `tests/editor/test_editor_scene_mode_switcher.h` with observable control behavior:

```cpp
#pragma once

#include "editor/gui/editor_scene_mode_switcher.h"

#include "scene/gui/button.h"

#include "tests/test_macros.h"

namespace TestEditorSceneModeSwitcher {

static int selected_count = 0;
static int selected_mode = -1;

static void record_selected_mode(int p_mode) {
	selected_count++;
	selected_mode = p_mode;
}

TEST_CASE("[Editor][SceneModeSwitcher] reflects mode and emits one user selection") {
	EditorSceneModeSwitcher *switcher = memnew(EditorSceneModeSwitcher);
	CHECK_FALSE(switcher->is_mode_available());
	CHECK(switcher->get_2d_button()->is_disabled());
	CHECK(switcher->get_3d_button()->is_disabled());

	switcher->set_mode_available(true);
	switcher->set_mode(SceneEditorMode::MODE_2D);
	CHECK(switcher->get_2d_button()->is_pressed());
	CHECK_FALSE(switcher->get_3d_button()->is_pressed());

	selected_count = 0;
	selected_mode = -1;
	switcher->connect("mode_selected", callable_mp_static(&record_selected_mode));
	switcher->get_3d_button()->emit_signal("pressed");
	CHECK(selected_count == 1);
	CHECK(selected_mode == int(SceneEditorMode::MODE_3D));

	memdelete(switcher);
}

TEST_CASE("[Editor][SceneModeSwitcher] hides when 3D is unavailable") {
	EditorSceneModeSwitcher *switcher = memnew(EditorSceneModeSwitcher);
	switcher->set_mode_available(true);
	switcher->set_3d_enabled(false);
	CHECK_FALSE(switcher->is_visible());
	switcher->set_3d_enabled(true);
	CHECK(switcher->is_visible());
	memdelete(switcher);
}

} // namespace TestEditorSceneModeSwitcher
```

Add this include to the editor test block in `tests/test_main.cpp`:

```cpp
#include "tests/editor/test_editor_scene_mode_switcher.h"
```

- [ ] **Step 2: Run the test and verify the new API is absent**

Run:

```bash
python3 scripts/agent_build.py --backend ninja --test --case "*SceneModeSwitcher*"
```

Expected: compile failure because `editor/gui/editor_scene_mode_switcher.h` does not exist.

- [ ] **Step 3: Add the shared mode type**

Create `editor/editor_scene_mode.h`:

```cpp
#pragma once

#include "core/string/string_name.h"

enum class SceneEditorMode {
	MODE_2D,
	MODE_3D,
};

inline StringName scene_editor_mode_to_name(SceneEditorMode p_mode) {
	return p_mode == SceneEditorMode::MODE_3D ? StringName("3d") : StringName("2d");
}

inline bool scene_editor_mode_from_name(const StringName &p_name, SceneEditorMode &r_mode) {
	if (p_name == StringName("2d")) {
		r_mode = SceneEditorMode::MODE_2D;
		return true;
	}
	if (p_name == StringName("3d")) {
		r_mode = SceneEditorMode::MODE_3D;
		return true;
	}
	return false;
}
```

- [ ] **Step 4: Implement the stateless control**

Create `editor/gui/editor_scene_mode_switcher.h` with this public contract:

```cpp
#pragma once

#include "editor/editor_scene_mode.h"

#include "scene/gui/box_container.h"

class Button;
class ButtonGroup;

class EditorSceneModeSwitcher : public HBoxContainer {
	FOUNDRY_CLASS(EditorSceneModeSwitcher, HBoxContainer);

	Ref<ButtonGroup> mode_group;
	Button *button_2d = nullptr;
	Button *button_3d = nullptr;
	SceneEditorMode mode = SceneEditorMode::MODE_2D;
	bool mode_available = false;
	bool enabled_3d = true;

	void _on_mode_pressed(SceneEditorMode p_mode);
	void _sync_state();

protected:
	static void _bind_methods();

public:
	void set_mode(SceneEditorMode p_mode);
	SceneEditorMode get_mode() const { return mode; }
	void set_mode_available(bool p_available);
	bool is_mode_available() const { return mode_available; }
	void set_3d_enabled(bool p_enabled);
	bool is_3d_enabled() const { return enabled_3d; }
	Button *get_2d_button() const { return button_2d; }
	Button *get_3d_button() const { return button_3d; }

	EditorSceneModeSwitcher();
};
```

Implement `editor/gui/editor_scene_mode_switcher.cpp` so it emits only user actions and never owns tile state:

```cpp
#include "editor_scene_mode_switcher.h"

#include "scene/gui/base_button.h"
#include "scene/gui/button.h"

void EditorSceneModeSwitcher::_bind_methods() {
	ADD_SIGNAL(MethodInfo("mode_selected", PropertyInfo(Variant::INT, "mode")));
}

void EditorSceneModeSwitcher::_sync_state() {
	button_2d->set_disabled(!mode_available);
	button_3d->set_disabled(!mode_available || !enabled_3d);
	button_2d->set_pressed_no_signal(mode == SceneEditorMode::MODE_2D);
	button_3d->set_pressed_no_signal(mode == SceneEditorMode::MODE_3D);
	set_visible(enabled_3d);
}

void EditorSceneModeSwitcher::_on_mode_pressed(SceneEditorMode p_mode) {
	if (!mode_available || (p_mode == SceneEditorMode::MODE_3D && !enabled_3d)) {
		_sync_state();
		return;
	}
	mode = p_mode;
	_sync_state();
	emit_signal("mode_selected", int(mode));
}

void EditorSceneModeSwitcher::set_mode(SceneEditorMode p_mode) {
	mode = p_mode;
	_sync_state();
}

void EditorSceneModeSwitcher::set_mode_available(bool p_available) {
	mode_available = p_available;
	_sync_state();
}

void EditorSceneModeSwitcher::set_3d_enabled(bool p_enabled) {
	enabled_3d = p_enabled;
	if (!enabled_3d && mode == SceneEditorMode::MODE_3D) {
		mode = SceneEditorMode::MODE_2D;
	}
	_sync_state();
}

EditorSceneModeSwitcher::EditorSceneModeSwitcher() {
	set_theme_type_variation("SceneModeSwitcher");
	add_theme_constant_override("separation", 1);
	mode_group.instantiate();
	mode_group->set_allow_unpress(false);

	button_2d = memnew(Button("2D"));
	button_2d->set_toggle_mode(true);
	button_2d->set_button_group(mode_group);
	button_2d->set_theme_type_variation("SceneModeButton");
	button_2d->set_focus_mode(FOCUS_ACCESSIBILITY);
	button_2d->set_accessibility_name(TTRC("2D Scene Mode"));
	button_2d->connect("pressed", callable_mp(this, &EditorSceneModeSwitcher::_on_mode_pressed).bind(SceneEditorMode::MODE_2D));
	add_child(button_2d);

	button_3d = memnew(Button("3D"));
	button_3d->set_toggle_mode(true);
	button_3d->set_button_group(mode_group);
	button_3d->set_theme_type_variation("SceneModeButton");
	button_3d->set_focus_mode(FOCUS_ACCESSIBILITY);
	button_3d->set_accessibility_name(TTRC("3D Scene Mode"));
	button_3d->connect("pressed", callable_mp(this, &EditorSceneModeSwitcher::_on_mode_pressed).bind(SceneEditorMode::MODE_3D));
	add_child(button_3d);

	_sync_state();
}
```

- [ ] **Step 5: Run the focused tests**

Run:

```bash
python3 scripts/agent_build.py --backend ninja --test --case "*SceneModeSwitcher*"
```

Expected: both `SceneModeSwitcher` cases pass.

- [ ] **Step 6: Commit the value and control**

```bash
git add editor/editor_scene_mode.h editor/gui/editor_scene_mode_switcher.h editor/gui/editor_scene_mode_switcher.cpp tests/editor/test_editor_scene_mode_switcher.h tests/test_main.cpp
git commit -m "Add scene tile mode switcher"
```

## Task 2: Make scene mode durable tile state

**Files:**
- Modify: `editor/scene/editor_scene_tabs.h:105-120`
- Modify: `editor/scene/editor_scene_tabs.cpp:443-446`
- Modify: `editor/editor_scene_pane_tile.h:60-205`
- Modify: `editor/editor_scene_pane_tile.cpp:200-520`
- Create: `tests/editor/test_scene_pane_tile_mode.h`
- Modify: `tests/test_main.cpp:40-115`

- [ ] **Step 1: Write failing tile inference and persistence tests**

Create `tests/editor/test_scene_pane_tile_mode.h`:

```cpp
#pragma once

#include "editor/editor_data.h"
#include "editor/editor_scene_context.h"
#include "editor/editor_scene_pane_tile.h"

#include "core/io/config_file.h"
#include "scene/2d/node_2d.h"
#include "scene/3d/node_3d.h"

#include "tests/test_macros.h"

namespace TestScenePaneTileMode {

TEST_CASE("[Editor][ScenePaneTileMode] first scene infers mode and manual choice is sticky") {
	EditorData editor_data;
	EditorSelection selection;
	ScenePaneTile *tile = memnew(ScenePaneTile);
	tile->setup(7, &selection, editor_data);
	CHECK(tile->get_scene_mode_switcher()->get_accessibility_description().contains("7"));

	EditorSceneContext *context_3d = memnew(EditorSceneContext);
	context_3d->set_scene_root_node(memnew(Node3D));
	tile->initialize_scene_editor_mode(context_3d);
	CHECK(tile->is_scene_editor_mode_initialized());
	CHECK(tile->get_scene_editor_mode() == SceneEditorMode::MODE_3D);

	tile->set_scene_editor_mode(SceneEditorMode::MODE_2D, true);
	EditorSceneContext *context_2d = memnew(EditorSceneContext);
	context_2d->set_scene_root_node(memnew(Node2D));
	tile->initialize_scene_editor_mode(context_2d);
	CHECK(tile->get_scene_editor_mode() == SceneEditorMode::MODE_2D);

	memdelete(context_3d);
	memdelete(context_2d);
	memdelete(tile);
}

TEST_CASE("[Editor][ScenePaneTileMode] layout round-trips mode and missing key stays uninitialized") {
	EditorData editor_data;
	EditorSelection selection;
	ScenePaneTile *source = memnew(ScenePaneTile);
	source->setup(8, &selection, editor_data);
	source->set_scene_editor_mode(SceneEditorMode::MODE_3D, false);

	Ref<ConfigFile> config;
	config.instantiate();
	source->save_layout(config, "Tile");
	CHECK(StringName(config->get_value("Tile", "scene_editor_mode")) == StringName("3d"));

	ScenePaneTile *restored = memnew(ScenePaneTile);
	restored->setup(8, &selection, editor_data);
	restored->load_layout(config, "Tile");
	CHECK(restored->is_scene_editor_mode_initialized());
	CHECK(restored->get_scene_editor_mode() == SceneEditorMode::MODE_3D);

	Ref<ConfigFile> legacy;
	legacy.instantiate();
	ScenePaneTile *missing_tile = memnew(ScenePaneTile);
	missing_tile->setup(9, &selection, editor_data);
	missing_tile->load_layout(legacy, "Tile");
	CHECK_FALSE(missing_tile->is_scene_editor_mode_initialized());

	legacy->set_value("Tile", "scene_editor_mode", "sideways");
	ScenePaneTile *invalid_tile = memnew(ScenePaneTile);
	invalid_tile->setup(10, &selection, editor_data);
	invalid_tile->load_layout(legacy, "Tile");
	CHECK_FALSE(invalid_tile->is_scene_editor_mode_initialized());

	memdelete(source);
	memdelete(restored);
	memdelete(missing_tile);
	memdelete(invalid_tile);
}

TEST_CASE("[Editor][ScenePaneTileMode] durable mode is independent from preview presentation") {
	EditorData editor_data;
	EditorSelection selection;
	ScenePaneTile *tile = memnew(ScenePaneTile);
	tile->setup(11, &selection, editor_data);
	tile->set_scene_editor_mode(SceneEditorMode::MODE_2D, false);
	tile->set_preview_mode(TilePreviewMode::LIVE_3D);
	CHECK(tile->get_scene_editor_mode() == SceneEditorMode::MODE_2D);
	CHECK(tile->get_preview_mode() == TilePreviewMode::LIVE_3D);
	memdelete(tile);
}

TEST_CASE("[Editor][ScenePaneTileMode] disabled 3D feature forces 2D inference") {
	EditorData editor_data;
	EditorSelection selection;
	ScenePaneTile *tile = memnew(ScenePaneTile);
	tile->setup(12, &selection, editor_data);
	tile->set_3d_scene_mode_enabled(false);
	EditorSceneContext *context = memnew(EditorSceneContext);
	context->set_scene_root_node(memnew(Node3D));
	tile->initialize_scene_editor_mode(context);
	CHECK(tile->get_scene_editor_mode() == SceneEditorMode::MODE_2D);
	CHECK_FALSE(tile->get_scene_mode_switcher()->is_visible());
	tile->set_3d_scene_mode_enabled(true);
	CHECK(tile->get_scene_editor_mode() == SceneEditorMode::MODE_2D);
	CHECK(tile->get_scene_mode_switcher()->is_visible());
	memdelete(context);
	memdelete(tile);
}

} // namespace TestScenePaneTileMode
```

Include it from `tests/test_main.cpp`:

```cpp
#include "tests/editor/test_scene_pane_tile_mode.h"
```

- [ ] **Step 2: Run the tests and verify the tile API is absent**

```bash
python3 scripts/agent_build.py --backend ninja --test --case "*ScenePaneTileMode*"
```

Expected: compile failure for missing `ScenePaneTile` scene-mode methods.

- [ ] **Step 3: Generalize the scene-tab extra-control slot**

Add to `EditorSceneTabs`:

```cpp
void add_extra_control(Control *p_control);
void add_extra_button(Button *p_button);
```

Implement insertion before `scene_list` so late mounting has deterministic layout:

```cpp
void EditorSceneTabs::add_extra_control(Control *p_control) {
	ERR_FAIL_NULL(p_control);
	tabbar_container->add_child(p_control);
	if (scene_list) {
		tabbar_container->move_child(p_control, scene_list->get_index());
	}
}

void EditorSceneTabs::add_extra_button(Button *p_button) {
	add_extra_control(p_button);
}
```

- [ ] **Step 4: Add tile-owned mode state and signal**

Add these fields and methods to `ScenePaneTile`:

```cpp
EditorSceneModeSwitcher *scene_mode_switcher = nullptr;
SceneEditorMode scene_editor_mode = SceneEditorMode::MODE_2D;
bool scene_editor_mode_initialized = false;

void _on_scene_editor_mode_selected(int p_mode);

void set_scene_editor_mode(SceneEditorMode p_mode, bool p_user_requested);
void initialize_scene_editor_mode(EditorSceneContext *p_context);
SceneEditorMode get_scene_editor_mode() const { return scene_editor_mode; }
bool is_scene_editor_mode_initialized() const { return scene_editor_mode_initialized; }
EditorSceneModeSwitcher *get_scene_mode_switcher() const { return scene_mode_switcher; }
void set_3d_scene_mode_enabled(bool p_enabled);
```

Add `_bind_methods()` and the request signal:

```cpp
void ScenePaneTile::_bind_methods() {
	ADD_SIGNAL(MethodInfo("scene_editor_mode_requested",
			PropertyInfo(Variant::INT, "tile_id"),
			PropertyInfo(Variant::INT, "mode")));
}
```

Mount and connect the switcher immediately after constructing `scene_tabs`:

```cpp
scene_mode_switcher = memnew(EditorSceneModeSwitcher);
scene_mode_switcher->connect("mode_selected", callable_mp(this, &ScenePaneTile::_on_scene_editor_mode_selected));
scene_tabs->add_extra_control(scene_mode_switcher);
```

In `ScenePaneTile::setup()`, after assigning `tile_id`, expose the owner through the group description:

```cpp
scene_mode_switcher->set_accessibility_description(vformat(TTRC("Editing mode for scene tile %d"), tile_id));
```

Implement state changes without conflating them with `TilePreviewMode`:

```cpp
void ScenePaneTile::_on_scene_editor_mode_selected(int p_mode) {
	set_scene_editor_mode(SceneEditorMode(p_mode), true);
}

void ScenePaneTile::set_scene_editor_mode(SceneEditorMode p_mode, bool p_user_requested) {
	scene_editor_mode = p_mode;
	scene_editor_mode_initialized = true;
	scene_mode_switcher->set_mode_available(get_scene_context() != nullptr);
	scene_mode_switcher->set_mode(scene_editor_mode);
	if (p_user_requested) {
		emit_signal("scene_editor_mode_requested", tile_id, int(scene_editor_mode));
	}
}

void ScenePaneTile::initialize_scene_editor_mode(EditorSceneContext *p_context) {
	if (scene_editor_mode_initialized || p_context == nullptr) {
		return;
	}
	set_scene_editor_mode(
			p_context->scene_has_3d_content() && scene_mode_switcher->is_3d_enabled()
					? SceneEditorMode::MODE_3D
					: SceneEditorMode::MODE_2D,
			false);
}

void ScenePaneTile::set_3d_scene_mode_enabled(bool p_enabled) {
	scene_mode_switcher->set_3d_enabled(p_enabled);
	if (!p_enabled && scene_editor_mode_initialized && scene_editor_mode == SceneEditorMode::MODE_3D) {
		set_scene_editor_mode(SceneEditorMode::MODE_2D, false);
	}
}
```

Whenever tile tabs are synced or a current scene changes, call:

```cpp
EditorSceneContext *context = get_scene_context();
scene_mode_switcher->set_mode_available(context != nullptr);
initialize_scene_editor_mode(context);
```

- [ ] **Step 5: Persist stable string values**

Extend `ScenePaneTile::save_layout()` and `load_layout()`:

```cpp
if (scene_editor_mode_initialized) {
	p_config->set_value(p_section, "scene_editor_mode", scene_editor_mode_to_name(scene_editor_mode));
}
```

```cpp
const StringName saved_mode = p_config->get_value(p_section, "scene_editor_mode", StringName());
SceneEditorMode restored_mode;
if (scene_editor_mode_from_name(saved_mode, restored_mode)) {
	set_scene_editor_mode(restored_mode, false);
}
```

- [ ] **Step 6: Run tile and existing preview tests**

```bash
python3 scripts/agent_build.py --backend ninja --test \
  --case "*ScenePaneTileMode*" \
  --case "*tile-preview-mode-prefers-canvas-view-host*"
```

Expected: all selected tests pass.

- [ ] **Step 7: Commit tile state and persistence**

```bash
git add editor/scene/editor_scene_tabs.h editor/scene/editor_scene_tabs.cpp editor/editor_scene_pane_tile.h editor/editor_scene_pane_tile.cpp tests/editor/test_scene_pane_tile_mode.h tests/test_main.cpp
git commit -m "Persist scene mode per editor tile"
```

## Task 3: Route focused and passive editors through tile mode

**Files:**
- Modify: `editor/editor_node.h:650-805`
- Modify: `editor/editor_node.cpp:440-470,3232-3520,5015-5050,5247-5515,7754-8120,8462-8490,10380-10430`
- Modify: `editor/editor_main_screen.cpp:127-195`
- Modify: `editor/editor_interface.cpp:436-438`
- Modify: `editor/automation/editor_automation_acceptance_workflow.cpp:2600-2750`
- Existing test: `tests/editor/test_editor_board_3d_switch.h:50-105`

- [ ] **Step 1: Add a failing real-editor request-routing assertion**

Add `#include "editor/editor_interface.h"` to the workflow implementation, then extend `run_board_switch_3d_scene()` immediately after `initial_tile` is resolved. This is an acceptance assertion in the existing real-editor workflow, not a source-text test:

```cpp
initial_tile->set_scene_editor_mode(SceneEditorMode::MODE_2D, true);
p_driver.flush_frames(30);
if (EditorNode::get_editor_main_screen()->get_selected_index() != EditorMainScreen::EDITOR_2D) {
	return _failure_with_message(p_driver, result.workflow, "A tile-local 2D request did not select the shared 2D editor.");
}

initial_tile->set_scene_editor_mode(SceneEditorMode::MODE_3D, true);
p_driver.flush_frames(30);
if (EditorNode::get_editor_main_screen()->get_selected_index() != EditorMainScreen::EDITOR_3D) {
	return _failure_with_message(p_driver, result.workflow, "A tile-local 3D request did not restore the shared 3D editor.");
}

EditorInterface::get_singleton()->set_main_screen_editor("2D");
p_driver.flush_frames(30);
if (initial_tile->get_scene_editor_mode() != SceneEditorMode::MODE_2D ||
		EditorNode::get_editor_main_screen()->get_selected_index() != EditorMainScreen::EDITOR_2D) {
	return _failure_with_message(p_driver, result.workflow, "EditorInterface did not route 2D through the focused tile.");
}
EditorInterface::get_singleton()->set_main_screen_editor("3D");
p_driver.flush_frames(30);
if (initial_tile->get_scene_editor_mode() != SceneEditorMode::MODE_3D ||
		EditorNode::get_editor_main_screen()->get_selected_index() != EditorMainScreen::EDITOR_3D) {
	return _failure_with_message(p_driver, result.workflow, "EditorInterface did not route 3D through the focused tile.");
}
```

- [ ] **Step 2: Run the workflow test and verify it fails before wiring**

```bash
python3 scripts/agent_build.py --backend ninja --test --case "*Switching boards with a 3D scene open*"
```

Expected: the workflow returns `ok:false` at the new 2D assertion because `EditorNode` is not connected to the tile request yet.

- [ ] **Step 3: Add EditorNode mode helpers and tile signal wiring**

Declare in `EditorNode`:

```cpp
void _apply_scene_editor_mode(ScenePaneTile *p_tile, EditorSceneContext *p_context, bool p_force_scene_screen = false);
void _on_tile_scene_editor_mode_requested(int p_tile_id, int p_mode);

public:
	void set_focused_tile_scene_editor_mode(SceneEditorMode p_mode);
	void select_main_screen(int p_index);
```

Connect once in `_wire_leaf_tile()`:

```cpp
const Callable mode_requested = callable_mp(this, &EditorNode::_on_tile_scene_editor_mode_requested);
if (!tile->is_connected("scene_editor_mode_requested", mode_requested)) {
	tile->connect("scene_editor_mode_requested", mode_requested);
}
```

Implement the helpers:

```cpp
void EditorNode::_apply_scene_editor_mode(ScenePaneTile *p_tile, EditorSceneContext *p_context, bool p_force_scene_screen) {
	ERR_FAIL_NULL(p_tile);
	p_tile->initialize_scene_editor_mode(p_context);
	if (!p_tile->is_scene_editor_mode_initialized()) {
		return;
	}
	if (editor_main_screen->is_global_screen_selected() && !p_force_scene_screen) {
		return;
	}
	const int target = p_tile->get_scene_editor_mode() == SceneEditorMode::MODE_3D
			? EditorMainScreen::EDITOR_3D
			: EditorMainScreen::EDITOR_2D;
	if (editor_main_screen->is_button_enabled(target) && editor_main_screen->get_selected_index() != target) {
		editor_main_screen->select(target);
	}
}

void EditorNode::set_focused_tile_scene_editor_mode(SceneEditorMode p_mode) {
	ScenePaneTile *tile = get_focused_tile();
	if (!tile || !tile->get_scene_context()) {
		return;
	}
	tile->set_scene_editor_mode(p_mode, false);
	_focus_tile(tile->get_tile_id());
}

void EditorNode::select_main_screen(int p_index) {
	if (p_index == EditorMainScreen::EDITOR_2D) {
		set_focused_tile_scene_editor_mode(SceneEditorMode::MODE_2D);
	} else if (p_index == EditorMainScreen::EDITOR_3D) {
		set_focused_tile_scene_editor_mode(SceneEditorMode::MODE_3D);
	} else {
		editor_main_screen->select(p_index);
	}
}

void EditorNode::_on_tile_scene_editor_mode_requested(int p_tile_id, int p_mode) {
	if (!board_strip || !board_strip->is_leaf_on_active_board(p_tile_id)) {
		return;
	}
	ScenePaneTile *tile = board_strip->find_tile_by_id(p_tile_id);
	if (!tile || !tile->get_scene_context()) {
		return;
	}
	tile->set_scene_editor_mode(SceneEditorMode(p_mode), false);
	_focus_tile(p_tile_id);
}
```

- [ ] **Step 4: Apply mode before every focused reparent**

In `_focus_tile_internal()`, after resolving `tile` and before `_sync_focused_tile_chrome(tile)`, add:

```cpp
_apply_scene_editor_mode(tile, scene_idx >= 0 ? editor_data.get_scene_context(scene_idx) : nullptr, true);
```

Make the same ordering explicit after board restore and leaf removal: apply the destination tile mode, then reparent the shared surface.

Replace the two shortcut branches in `shortcut_input()`:

```cpp
} else if (ED_IS_SHORTCUT("editor/editor_2d", p_event)) {
	set_focused_tile_scene_editor_mode(SceneEditorMode::MODE_2D);
} else if (ED_IS_SHORTCUT("editor/editor_3d", p_event)) {
	set_focused_tile_scene_editor_mode(SceneEditorMode::MODE_3D);
```

In `EditorMainScreen::select_next()` and `select_prev()`, replace the final `select(editor)` with:

```cpp
EditorNode::get_singleton()->select_main_screen(editor);
```

In `EditorInterface::set_main_screen_editor()`, route resolved 2D/3D names through the same public entry point and leave Script/global plugins on the existing name path:

```cpp
void EditorInterface::set_main_screen_editor(const String &p_name) {
	EditorNode *editor_node = EditorNode::get_singleton();
	EditorMainScreen *main_screen = editor_node->get_editor_main_screen();
	const int index = main_screen->get_button_index_by_name(p_name);
	if (index == EditorMainScreen::EDITOR_2D || index == EditorMainScreen::EDITOR_3D) {
		editor_node->select_main_screen(index);
	} else {
		main_screen->select_by_name(p_name);
	}
}
```

In `EditorMainScreen::load_layout_from_config()`, replace the final selection condition with:

```cpp
if (selected_main_editor == "Script") {
	callable_mp(this, &EditorMainScreen::select_by_name).call_deferred(selected_main_editor);
	return;
}
const int saved_index = get_button_index_by_name(selected_main_editor);
if (saved_index == EDITOR_2D || saved_index == EDITOR_3D) {
	return;
}
if (saved_index >= 0) {
	callable_mp(this, &EditorMainScreen::select_by_name).call_deferred(selected_main_editor);
}
```

Restored global names and `Script` retain the existing deferred path; the restored focused tile applies its own durable scene mode.

Replace `_set_main_scene_state()`'s selected-node auto-switch block with:

```cpp
if (get_edited_scene()) {
	_apply_scene_editor_mode(get_focused_tile(), editor_data.get_active_scene_context(), false);
}
```

- [ ] **Step 5: Stop node selection from overwriting initialized tile mode**

In `_edit_current()`, after resolving `main_plugin`, constrain cross-mode selection:

```cpp
ScenePaneTile *focused_tile = get_focused_tile();
EditorPlugin *editor_plugin_screen = editor_main_screen->get_selected_plugin();
if (focused_tile && focused_tile->is_scene_editor_mode_initialized() && main_plugin != editor_plugin_screen) {
	main_plugin = nullptr;
}
```

Keep the existing same-plugin `edit(current_obj)` call intact. This lets 2D nodes update the 2D editor and 3D nodes update the 3D editor, while selecting a node from the other dimension updates docks without changing mode.

- [ ] **Step 6: Select passive preview type from tile mode**

In `_update_tile_display_attachments()`, initialize the tile from `ctx`, then replace the content-based branch:

```cpp
tile->initialize_scene_editor_mode(ctx);
const SceneEditorMode tile_mode = tile->get_scene_editor_mode();

if (is_focused_tile) {
	_apply_scene_editor_mode(tile, ctx, false);
	tile->set_preview_mode(TilePreviewMode::FOCUSED_LIVE);
} else if (tile_mode == SceneEditorMode::MODE_3D) {
	tile->set_preview_mode(TilePreviewMode::LIVE_3D);
} else {
	tile->set_preview_mode(TilePreviewMode::LIVE_2D);
}
```

Retain the complete existing 3D and 2D surface setup bodies under the new branches, including viewport update modes, camera state, canvas view state, overview bounds, and passive input policy.

- [ ] **Step 7: Route feature-profile 3D availability to every tile**

In both feature-profile update branches, walk `board_strip->get_workspaces()` and call:

```cpp
for (ScenePaneTile *tile : workspace->get_tiles()) {
	tile->set_3d_scene_mode_enabled(!profile->is_feature_disabled(EditorFeatureProfile::FEATURE_3D));
}
```

Use `true` in the profile-reset branch.

- [ ] **Step 8: Run focused tile and passive-preview regression tests**

```bash
python3 scripts/agent_build.py --backend ninja --test \
  --case "*ScenePaneTileMode*" \
  --case "*tile-preview-mode-prefers-canvas-view-host*" \
  --case "*Switching boards with a 3D scene open*" \
  --case "*passive-pane-sync*" \
  --case "*preview-camera-state*"
```

Expected: all selected tests pass; existing board-switch and passive-preview crash coverage remains green.

- [ ] **Step 9: Commit EditorNode routing**

```bash
git add editor/editor_node.h editor/editor_node.cpp editor/editor_main_screen.cpp editor/editor_interface.cpp editor/automation/editor_automation_acceptance_workflow.cpp
git commit -m "Route scene editors through tile modes"
```

## Task 4: Move board creation and overview into one menu

**Files:**
- Modify: `editor/gui/editor_board_actions_menu.h:45-90`
- Modify: `editor/gui/editor_board_actions_menu.cpp:45-175`
- Modify: `editor/gui/editor_board_switcher.h:45-130`
- Modify: `editor/gui/editor_board_switcher.cpp:45-360`
- Modify: `tests/editor/test_editor_board_switcher.h:60-1020`

- [ ] **Step 1: Replace old add/delayed-menu expectations with failing menu tests**

Update the harness to find `switcher->get_menu_button()` and remove `pump_menu_open_delay()` and `add_board_button()`.

Add these cases:

```cpp
TEST_CASE("[Editor][BoardSwitcher] board menu adds and activates a board") {
	BoardSwitcherHarness harness;
	harness.mount();
	const int before = harness.strip->get_board_count();
	harness.switcher->popup_active_board_menu(Point2());
	harness.activate_menu_item(EditorBoardActionsMenu::ITEM_NEW_BOARD);
	harness.pump();
	CHECK(harness.strip->get_board_count() == before + 1);
	CHECK(harness.strip->get_active_index() == before);
	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] board menu enters overview") {
	BoardSwitcherHarness harness;
	harness.mount();
	harness.switcher->popup_active_board_menu(Point2());
	harness.activate_menu_item(EditorBoardActionsMenu::ITEM_OVERVIEW);
	harness.pump();
	CHECK(harness.strip->is_overview_active());
	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] pressing the active segment does not open the menu") {
	BoardSwitcherHarness harness;
	harness.mount();
	harness.board_button(0)->emit_signal("pressed");
	CHECK_FALSE(harness.switcher->get_actions_menu()->is_visible());
	CHECK(harness.board_button(0)->is_pressed());
	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] dedicated board menu opens immediately") {
	BoardSwitcherHarness harness;
	harness.mount();
	harness.switcher->get_menu_button()->emit_signal("pressed");
	CHECK(harness.switcher->get_actions_menu()->is_visible());
	harness.unmount();
}
```

Delete cases that assert an active-board caret, delayed popup, or delayed-popup cancellation. Keep double-click rename, right-click target safety, reorder, close, and identity tests.

- [ ] **Step 2: Run the BoardSwitcher suite and verify failures**

```bash
python3 scripts/agent_build.py --backend ninja --test --case "*BoardSwitcher*"
```

Expected: compile/test failures for the new item IDs and menu-button API.

- [ ] **Step 3: Extend the actions menu**

Change the enum without relying on numeric compatibility:

```cpp
enum ItemID {
	ITEM_NEW_BOARD,
	ITEM_OVERVIEW,
	ITEM_RENAME,
	ITEM_CLOSE,
	ITEM_CLOSE_OTHERS,
	ITEM_MOVE_LEFT,
	ITEM_MOVE_RIGHT,
	ITEM_BOARD_BASE = 1000,
};
```

Add `bool include_board_list` and `HashMap<int, ObjectID> board_item_targets`. Change the popup API to:

```cpp
void popup_for_board(int p_index, const Point2 &p_screen_position, bool p_include_board_list = false);
```

At the start of `popup_for_board()`, after resolving the target board and before rebuilding, add:

```cpp
target_board_id = board->get_instance_id();
include_board_list = p_include_board_list;
_rebuild_items();
```

Build menu items in this order:

```cpp
clear();
board_item_targets.clear();
const int board_count = strip ? strip->get_board_count() : 0;
const int target_index = strip ? strip->resolve_board_index(target_board_id) : -1;
const bool can_close = board_count > 1;
const bool can_move_left = target_index > 0;
const bool can_move_right = target_index >= 0 && target_index < board_count - 1;
if (include_board_list) {
	for (int i = 0; i < board_count; i++) {
		EditorBoard *board = strip->get_board(i);
		if (!board) {
			continue;
		}
		const int item_id = ITEM_BOARD_BASE + i;
		add_check_item(board->get_title(), item_id);
		set_item_checked(get_item_index(item_id), i == strip->get_active_index());
		board_item_targets[item_id] = board->get_instance_id();
	}
	add_separator();
}
add_item(TTRC("New Board"), ITEM_NEW_BOARD);
add_item(TTRC("Board Overview"), ITEM_OVERVIEW);
add_separator();
add_item(TTRC("Rename Board"), ITEM_RENAME);
add_item(TTRC("Move Left"), ITEM_MOVE_LEFT);
add_item(TTRC("Move Right"), ITEM_MOVE_RIGHT);
add_separator();
add_item(TTRC("Close Board"), ITEM_CLOSE);
add_item(TTRC("Close Other Boards"), ITEM_CLOSE_OTHERS);
set_item_disabled(get_item_index(ITEM_MOVE_LEFT), !can_move_left);
set_item_disabled(get_item_index(ITEM_MOVE_RIGHT), !can_move_right);
set_item_disabled(get_item_index(ITEM_CLOSE), !can_close);
set_item_disabled(get_item_index(ITEM_CLOSE_OTHERS), !can_close);
reset_size();
```

Declare `_add_board_and_activate()`, `_enter_overview()`, and `_activate_board(ObjectID p_board_id)` as private helpers. Implement the helpers with `strip` null checks, and route collection actions before resolving the target board:

```cpp
void EditorBoardActionsMenu::_add_board_and_activate() {
	if (!strip) {
		return;
	}
	EditorBoard *board = strip->add_board();
	if (board) {
		strip->set_active_board(strip->get_board_index(board));
	}
}

void EditorBoardActionsMenu::_enter_overview() {
	if (strip) {
		strip->set_overview(true);
	}
}

void EditorBoardActionsMenu::_activate_board(ObjectID p_board_id) {
	if (!strip) {
		return;
	}
	const int board_index = strip->resolve_board_index(p_board_id);
	if (board_index >= 0) {
		strip->set_active_board(board_index);
	}
}

if (p_id == ITEM_NEW_BOARD) {
	callable_mp(this, &EditorBoardActionsMenu::_add_board_and_activate).call_deferred();
	return;
}
if (p_id == ITEM_OVERVIEW) {
	callable_mp(this, &EditorBoardActionsMenu::_enter_overview).call_deferred();
	return;
}
if (p_id >= ITEM_BOARD_BASE) {
	const ObjectID *board_id = board_item_targets.getptr(p_id);
	if (board_id) {
		callable_mp(this, &EditorBoardActionsMenu::_activate_board).bind(*board_id).call_deferred();
	}
	return;
}
```

Deferral matches the existing close/move policy and prevents board-switcher rebuilds from running inside `PopupMenu` input dispatch.

- [ ] **Step 4: Remove the active-segment timer and add a dedicated menu button**

Delete `pending_menu_board_id`, `pending_menu_timer`, `_cancel_pending_menu()`, `_open_pending_actions_menu()`, and `BOARD_ACTIONS_MENU_OPEN_DELAY_SEC` from `EditorBoardSwitcher`.

Create one `menu_button` during rebuild:

```cpp
menu_button = memnew(Button);
menu_button->set_flat(true);
menu_button->set_focus_mode(FOCUS_ACCESSIBILITY);
menu_button->set_theme_type_variation("BoardRailMenuButton");
menu_button->set_accessibility_name(TTRC("Board Menu"));
menu_button->set_tooltip_text(TTR("Board Menu"));
menu_button->set_button_icon(get_editor_theme_icon(SNAME("GuiTabMenuHl")));
menu_button->connect("pressed", callable_mp(this, &EditorBoardSwitcher::_on_menu_pressed));
rail_hbox->add_child(menu_button);
```

Expose and implement:

```cpp
Button *get_menu_button() const { return menu_button; }
void popup_active_board_menu(const Point2 &p_screen_position, bool p_include_board_list = false);
```

```cpp
void EditorBoardSwitcher::_on_menu_pressed() {
	const Rect2 screen_rect = menu_button->get_screen_rect();
	popup_active_board_menu(screen_rect.position + Point2(0, screen_rect.size.y), false);
}

void EditorBoardSwitcher::popup_active_board_menu(const Point2 &p_screen_position, bool p_include_board_list) {
	if (!strip || strip->get_active_index() < 0) {
		return;
	}
	actions_menu->popup_for_board(strip->get_active_index(), p_screen_position, p_include_board_list);
}
```

Active board presses restore `pressed=true`, emit the board request described in Task 5, and do not open any popup. Inactive presses still call `strip->set_active_board(index)` immediately. When overview is active, every segment—including the already-active one—must call `set_active_board(index)` so the strip exits overview through its existing `_exit_overview_to()` path.

- [ ] **Step 5: Run BoardSwitcher tests**

```bash
python3 scripts/agent_build.py --backend ninja --test --case "*BoardSwitcher*"
```

Expected: all updated BoardSwitcher tests pass with no timer waits.

- [ ] **Step 6: Commit menu behavior**

```bash
git add editor/gui/editor_board_actions_menu.h editor/gui/editor_board_actions_menu.cpp editor/gui/editor_board_switcher.h editor/gui/editor_board_switcher.cpp tests/editor/test_editor_board_switcher.h
git commit -m "Consolidate board actions into one menu"
```

## Task 5: Build and center the responsive Board Rail

**Files:**
- Modify: `editor/gui/editor_board_switcher.h:45-130`
- Modify: `editor/gui/editor_board_switcher.cpp:45-360`
- Modify: `editor/editor_node.h:350-500,750-810`
- Modify: `editor/editor_node.cpp:7754-7770,9098-9110,11610-11655`
- Modify: `editor/themes/theme_classic.cpp:1645-1700`
- Modify: `editor/themes/theme_modern.cpp:1690-1745`
- Modify: `tests/editor/test_editor_board_switcher.h`

- [ ] **Step 1: Add failing rail layout and request tests**

Add tests that mount the switcher in a sized host and assert observable segment visibility:

```cpp
TEST_CASE("[Editor][BoardSwitcher] rail compacts without losing active board") {
	BoardSwitcherHarness harness;
	harness.mount();
	harness.strip->add_board("Gameplay");
	harness.strip->add_board("World Building");
	harness.strip->add_board("Interface");
	harness.strip->set_active_board(2);
	harness.host->set_size(Size2(280, 48));
	harness.pump();
	CHECK(harness.board_button(2)->is_visible());
	CHECK_FALSE(harness.board_button(0)->is_visible());
	CHECK_FALSE(harness.board_button(1)->is_visible());
	CHECK(harness.strip->get_active_index() == 2);
	harness.host->set_size(Size2(1400, 48));
	harness.pump();
	CHECK(harness.board_button(0)->is_visible());
	CHECK(harness.board_button(1)->is_visible());
	CHECK(harness.board_button(2)->is_visible());
	CHECK(harness.strip->get_active_index() == 2);
	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] every segment emits a board request") {
	BoardSwitcherHarness harness;
	harness.mount();
	harness.strip->add_board("Second");
	board_requested_index = -1;
	harness.switcher->connect("board_requested", callable_mp_static(&record_board_requested));
	harness.board_button(0)->emit_signal("pressed");
	CHECK(board_requested_index == 0);
	harness.board_button(1)->emit_signal("pressed");
	CHECK(board_requested_index == 1);
	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] compact menu lists and activates every board") {
	BoardSwitcherHarness harness;
	harness.mount();
	harness.strip->add_board("Gameplay");
	harness.strip->add_board("World Building");
	harness.host->set_size(Size2(240, 48));
	harness.pump();
	harness.switcher->get_menu_button()->emit_signal("pressed");
	CHECK(harness.switcher->get_actions_menu()->get_item_index(EditorBoardActionsMenu::ITEM_BOARD_BASE) >= 0);
	CHECK(harness.switcher->get_actions_menu()->get_item_index(EditorBoardActionsMenu::ITEM_BOARD_BASE + 2) >= 0);
	harness.activate_menu_item(EditorBoardActionsMenu::ITEM_BOARD_BASE + 2);
	harness.pump();
	CHECK(harness.strip->get_active_index() == 2);
	harness.unmount();
}
```

Add the recorder beside `board_moved_counter`:

```cpp
static int board_requested_index = -1;

static void record_board_requested(int p_index) {
	board_requested_index = p_index;
}
```

- [ ] **Step 2: Run BoardSwitcher tests and verify failure**

```bash
python3 scripts/agent_build.py --backend ninja --test --case "*BoardSwitcher*"
```

Expected: failures for compact visibility behavior and the missing `board_requested` signal.

- [ ] **Step 3: Convert the switcher to a rail container**

Change `EditorBoardSwitcher` to derive from `PanelContainer`. Give it one owned `HBoxContainer *rail_hbox`, keep `actions_menu` as a non-layout child, and assign:

```cpp
set_theme_type_variation("BoardRail");
rail_hbox = memnew(HBoxContainer);
rail_hbox->add_theme_constant_override("separation", 1);
add_child(rail_hbox);
```

Every board segment uses:

```cpp
button->set_theme_type_variation("BoardRailButton");
button->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
button->set_clip_text(true);
const Ref<Font> font = button->get_theme_font(SceneStringName(font));
const int font_size = button->get_theme_font_size(SceneStringName(font_size));
const int horizontal_padding = get_theme_constant("segment_horizontal_padding");
const int maximum_width = get_theme_constant("segment_maximum_width");
const int title_width = Math::ceil(font->get_string_size(board->get_title(), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x);
button->set_custom_minimum_size(Size2(MIN(title_width + horizontal_padding, maximum_width), 0));
```

Retain the full, unelided title in `tooltip_text` and `accessibility_name`. Rebuild on `NOTIFICATION_THEME_CHANGED` so font, scale, and theme-constant changes recompute every segment width.

Add the signal:

```cpp
ADD_SIGNAL(MethodInfo("board_requested", PropertyInfo(Variant::INT, "board_index")));
```

Replace the segment-press body after index validation with this ordering, preserving the rename guard:

```cpp
if (is_renaming()) {
	return;
}
if (strip->is_overview_active()) {
	strip->set_active_board(p_index);
} else if (p_index == strip->get_active_index()) {
	if (Button *button = _board_button_at(p_index)) {
		button->set_pressed_no_signal(true);
	}
} else {
	strip->set_active_board(p_index);
}
emit_signal("board_requested", p_index);
```

- [ ] **Step 4: Implement compact calculation from title-bar space**

On parent resize and after every rebuild, calculate the center budget from the title bar's visible siblings: sum minimum widths before the switcher, sum minimum widths after it, and reserve twice the larger side so the rail remains visually centered.

Use this exact state transition:

```cpp
void EditorBoardSwitcher::_set_compact(bool p_compact) {
	if (compact == p_compact) {
		return;
	}
	if (rename_edit) {
		_apply_pending_rename(rename_edit->get_text());
	}
	compact = p_compact;
	for (int i = 0; i < board_buttons.size(); i++) {
		if (board_buttons[i]) {
			board_buttons[i]->set_visible(!compact || i == strip->get_active_index());
		}
	}
	update_minimum_size();
}
```

Compute desired full width by summing every board button's combined minimum width, the menu button width, rail separation, and the `BoardRail` stylebox minimum. Compact when that exceeds the symmetric center budget. Connect the title bar's `resized` signal to `_update_compact_mode()` and disconnect it in `NOTIFICATION_EXIT_TREE`.

When compact, `_on_menu_pressed()` passes `true` for `p_include_board_list`; full mode passes `false`.

- [ ] **Step 5: Center the rail and hide compatibility buttons**

In the title-bar construction:

```cpp
HBoxContainer *main_editor_button_hb = memnew(HBoxContainer);
main_editor_button_hb->set_name("EditorMainScreenButtons");
main_editor_button_hb->hide();
editor_main_screen->add_child(main_editor_button_hb);
editor_main_screen->set_button_container(main_editor_button_hb);

board_switcher = memnew(EditorBoardSwitcher);
board_switcher->setup(board_strip);
title_bar->add_child(board_switcher);
title_bar->set_center_control(board_switcher);
```

Remove the later `title_bar->add_child(board_switcher)` after `project_run_bar`; keep the run bar and renderer on the right.

Connect `board_requested`:

```cpp
board_switcher->connect("board_requested", callable_mp(this, &EditorNode::_on_board_requested));
```

Implement the active-board global-screen return path:

```cpp
void EditorNode::_on_board_requested(int p_index) {
	if (!editor_main_screen->is_global_screen_selected() || !board_strip) {
		return;
	}
	EditorBoard *board = board_strip->get_board(p_index);
	EditorSceneWorkspace *workspace = board ? board->get_workspace() : nullptr;
	const int tile_id = workspace ? workspace->get_effective_focused_tile_id() : -1;
	if (tile_id >= 0 && board_strip->is_leaf_on_active_board(tile_id)) {
		_focus_tile_internal(tile_id, true);
	}
}
```

Inactive segment presses already switch the board and enter `_on_active_board_changed()`, which applies the destination tile mode and exits the global screen.

- [ ] **Step 6: Add theme variations**

Add equivalent variations beside `MainScreenButton` in both theme generators. Derive all colors from `p_config`; use `menu_transparent_style` for classic normal states and `p_config.base_empty_wide_style` for modern normal states:

```cpp
p_theme->set_type_variation("BoardRail", "PanelContainer");
p_theme->set_constant("segment_horizontal_padding", "BoardRail", Math::round(24 * EDSCALE));
p_theme->set_constant("segment_maximum_width", "BoardRail", Math::round(180 * EDSCALE));
Ref<StyleBoxFlat> board_rail = p_config.content_panel_style->duplicate();
board_rail->set_bg_color(p_config.dark_color_2.lerp(p_config.base_color, 0.12));
board_rail->set_border_color(p_config.contrast_color_1);
board_rail->set_border_width_all(MAX(1, int(EDSCALE)));
board_rail->set_corner_radius_all(Math::round(8 * EDSCALE));
board_rail->set_content_margin_all(Math::round(3 * EDSCALE));
p_theme->set_stylebox(SceneStringName(panel), "BoardRail", board_rail);

p_theme->set_type_variation("BoardRailButton", "Button");
p_theme->set_stylebox(CoreStringName(normal), "BoardRailButton", menu_transparent_style);
p_theme->set_stylebox(SceneStringName(hover), "BoardRailButton", p_config.button_style_hover);
p_theme->set_stylebox(SceneStringName(pressed), "BoardRailButton", p_config.button_style_pressed);

p_theme->set_type_variation("BoardRailMenuButton", "FlatMenuButton");
p_theme->set_type_variation("SceneModeSwitcher", "HBoxContainer");
p_theme->set_type_variation("SceneModeButton", "Button");
p_theme->set_stylebox(CoreStringName(normal), "SceneModeButton", menu_transparent_style);
p_theme->set_stylebox(SceneStringName(hover), "SceneModeButton", p_config.button_style_hover);
p_theme->set_stylebox(SceneStringName(pressed), "SceneModeButton", p_config.button_style_pressed);
```

For both button variations, replace the direct pressed style with an accent-bottom duplicate:

```cpp
Ref<StyleBoxFlat> rail_button_pressed = p_config.button_style_pressed->duplicate();
rail_button_pressed->set_border_color(p_config.accent_color);
rail_button_pressed->set_border_width(Side::SIDE_BOTTOM, MAX(2, int(2 * EDSCALE)));
p_theme->set_stylebox(SceneStringName(pressed), "BoardRailButton", rail_button_pressed);
p_theme->set_stylebox("hover_pressed", "BoardRailButton", rail_button_pressed);
p_theme->set_stylebox(SceneStringName(pressed), "SceneModeButton", rail_button_pressed);
p_theme->set_stylebox("hover_pressed", "SceneModeButton", rail_button_pressed);
```

In `theme_modern.cpp`, repeat the variation block with both `menu_transparent_style` references replaced by `p_config.base_empty_wide_style`. Keep all dimensions scaled with `EDSCALE` and validate the classic/modern generators in both light and dark editor themes.

- [ ] **Step 7: Run BoardSwitcher and main-screen layout tests**

```bash
python3 scripts/agent_build.py --backend ninja --test \
  --case "*BoardSwitcher*" \
  --case "*EditorMainScreen*"
```

Expected: all selected tests pass; existing name-based main-screen persistence remains green.

- [ ] **Step 8: Commit the centered rail**

```bash
git add editor/gui/editor_board_switcher.h editor/gui/editor_board_switcher.cpp editor/editor_node.h editor/editor_node.cpp editor/themes/theme_classic.cpp editor/themes/theme_modern.cpp tests/editor/test_editor_board_switcher.h
git commit -m "Center the responsive board rail"
```

## Task 6: Expose tile modes to editor automation

**Files:**
- Modify: `editor/automation/editor_automation_workspace.cpp:90-150`
- Modify: `tests/editor/test_editor_automation_workspace.h:220-310`

- [ ] **Step 1: Add failing automation-state assertions**

Extend `[Editor][Automation][MCP] mcp-workspace-state`. Before capturing state, set:

```cpp
ScenePaneTile *mode_tile_a = h.workspace->get_tile_by_id(0);
ScenePaneTile *mode_tile_b = h.workspace->get_tile_by_id(1);
REQUIRE(mode_tile_a != nullptr);
REQUIRE(mode_tile_b != nullptr);
mode_tile_a->set_scene_editor_mode(SceneEditorMode::MODE_2D, false);
mode_tile_a->set_preview_mode(TilePreviewMode::FOCUSED_LIVE);
mode_tile_b->set_scene_editor_mode(SceneEditorMode::MODE_3D, false);
mode_tile_b->set_preview_mode(TilePreviewMode::LIVE_3D);
```

After the existing tile dictionaries are resolved, add:

```cpp
CHECK(String(tile_a.get("scene_editor_mode", String())) == "2d");
CHECK(String(tile_a.get("preview_mode", String())) == "focused_live");
CHECK(String(tile_b.get("scene_editor_mode", String())) == "3d");
CHECK(String(tile_b.get("preview_mode", String())) == "live_3d");
```

- [ ] **Step 2: Run the focused automation test and verify missing fields**

```bash
python3 scripts/agent_build.py --backend ninja --test --case "*mcp-workspace-state*"
```

Expected: assertions fail because the captured dictionaries omit both mode fields.

- [ ] **Step 3: Serialize durable and presentation modes**

Add helpers near `_tile_state_entry()`:

```cpp
String _scene_editor_mode_name(ScenePaneTile *p_tile) {
	if (!p_tile || !p_tile->is_scene_editor_mode_initialized()) {
		return "uninitialized";
	}
	return String(scene_editor_mode_to_name(p_tile->get_scene_editor_mode()));
}

String _preview_mode_name(ScenePaneTile *p_tile) {
	if (!p_tile) {
		return "unknown";
	}
	switch (p_tile->get_preview_mode()) {
		case TilePreviewMode::FOCUSED_LIVE: return "focused_live";
		case TilePreviewMode::LIVE_2D: return "live_2d";
		case TilePreviewMode::LIVE_3D: return "live_3d";
	}
	return "unknown";
}
```

Write into every tile dictionary:

```cpp
tile["scene_editor_mode"] = _scene_editor_mode_name(p_tile);
tile["preview_mode"] = _preview_mode_name(p_tile);
```

- [ ] **Step 4: Run automation workspace tests**

```bash
python3 scripts/agent_build.py --backend ninja --test \
  --case "*mcp-workspace-state*" \
  --case "*mcp-tile-scoped-selector*" \
  --case "*mcp-dock-action*"
```

Expected: all selected tests pass and existing state shape remains backward compatible.

- [ ] **Step 5: Commit automation state**

```bash
git add editor/automation/editor_automation_workspace.cpp tests/editor/test_editor_automation_workspace.h
git commit -m "Expose scene tile modes to automation"
```

## Task 7: Add real-editor independent-mode acceptance

**Files:**
- Modify: `editor/automation/editor_automation_acceptance_workflow.h:45-90`
- Modify: `editor/automation/editor_automation_acceptance_workflow.cpp:2600-2850`
- Modify: `editor/automation/editor_automation_workflow_registry.cpp:40-180`
- Create: `tests/editor/test_editor_tile_local_scene_modes.h`
- Modify: `tests/editor/test_editor_automation_workflow.h:250-290`
- Modify: `tests/test_main.cpp:40-115`

- [ ] **Step 1: Add the subprocess test before the workflow exists**

Create `tests/editor/test_editor_tile_local_scene_modes.h` using the existing disposable workflow fixture:

```cpp
#pragma once

#include "tests/editor/editor_workflow_test_fixtures.h"
#include "tests/test_macros.h"

namespace TestEditorTileLocalSceneModes {

TEST_CASE("[Editor][SceneModes] two tiles keep independent modes across focus, boards, and restart") {
	const String project_path = EditorWorkflowTestFixtures::prepare_disposable_project();
	REQUIRE_FALSE(project_path.is_empty());

	List<String> arguments;
	arguments.push_back("editor");
	arguments.push_back("open");
	arguments.push_back("--headless");
	arguments.push_back("--project");
	arguments.push_back(project_path);
	arguments.push_back("--automation");
	arguments.push_back("--automation-run-workflow=tile_local_scene_modes");

	int seed_exit_code = -1;
	const String seed_output = EditorWorkflowTestFixtures::workflow_run_subprocess(arguments, seed_exit_code);
	INFO("Seed subprocess output:\n", seed_output);
	CHECK(seed_output.contains("\"workflow\":\"tile_local_scene_modes\""));
	CHECK(seed_output.contains("\"ok\":true"));
	CHECK(seed_exit_code == 0);
	if (seed_exit_code != 0) {
		return;
	}

	arguments.pop_back();
	arguments.push_back("--automation-run-workflow=tile_local_scene_modes_restore");
	int restore_exit_code = -1;
	const String restore_output = EditorWorkflowTestFixtures::workflow_run_subprocess(arguments, restore_exit_code);
	INFO("Restore subprocess output:\n", restore_output);
	CHECK(restore_output.contains("\"workflow\":\"tile_local_scene_modes_restore\""));
	CHECK(restore_output.contains("\"ok\":true"));
	CHECK(restore_exit_code == 0);
}

} // namespace TestEditorTileLocalSceneModes
```

Include it from `tests/test_main.cpp`.

- [ ] **Step 2: Run the case and verify the workflow is unknown**

```bash
python3 scripts/agent_build.py --backend ninja --test --case "*two tiles keep independent modes*"
```

Expected: the seed subprocess fails because `tile_local_scene_modes` is not registered; the restore subprocess is not reached because the seed exit code is required to be zero.

- [ ] **Step 3: Register the workflow contract**

Add to `editor_automation_acceptance_workflow.h`:

```cpp
static Result run_tile_local_scene_modes(EditorWorkflowTestDriver &p_driver);
static Result run_tile_local_scene_modes_restore(EditorWorkflowTestDriver &p_driver);
```

Add the registry adapter and registration:

```cpp
EditorAutomationAcceptanceWorkflow::Result _run_tile_local_scene_modes(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_tile_local_scene_modes(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_tile_local_scene_modes_restore(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_tile_local_scene_modes_restore(p_driver);
}
```

```cpp
_register_workflow("tile_local_scene_modes", &_run_tile_local_scene_modes);
_register_workflow("tile_local_scene_modes_restore", &_run_tile_local_scene_modes_restore);
```

Extend the existing registry test in `tests/editor/test_editor_automation_workflow.h`:

```cpp
CHECK(EditorAutomationWorkflowRegistry::has_workflow("tile_local_scene_modes"));
CHECK(EditorAutomationWorkflowRegistry::has_workflow("tile_local_scene_modes_restore"));
CHECK(names.has("tile_local_scene_modes"));
CHECK(names.has("tile_local_scene_modes_restore"));
```

- [ ] **Step 4: Implement the end-to-end workflow**

Use the existing `BOARD_SWITCH_2D_SCENE` and `BOARD_SWITCH_3D_SCENE` fixtures and the workspace split helpers already present in `editor_automation_acceptance_workflow.cpp`. The workflow must perform and assert this exact sequence:

```cpp
Result result;
result.workflow = "tile_local_scene_modes";
p_driver.begin_workflow();

EditorNode *editor_node = EditorNode::get_singleton();
if (!editor_node || !editor_node->is_editor_ready()) {
	return _failure_with_message(p_driver, result.workflow, "EditorNode is not ready.");
}
if (editor_node->load_scene(BOARD_SWITCH_2D_SCENE) != OK) {
	return _failure_with_message(p_driver, result.workflow, "Failed to load the 2D fixture.");
}
if (editor_node->load_scene(BOARD_SWITCH_3D_SCENE) != OK) {
	return _failure_with_message(p_driver, result.workflow, "Failed to load the 3D fixture.");
}
p_driver.flush_frames(30);

EditorData &editor_data = EditorNode::get_editor_data();
EditorSceneWorkspace *workspace = EditorNode::get_scene_workspace();
WorkspaceLeafNode *left_leaf = workspace ? workspace->get_focused_leaf() : nullptr;
WorkspaceLeafNode *right_leaf = left_leaf ? workspace->split(left_leaf, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND) : nullptr;
if (!left_leaf || !right_leaf || !left_leaf->get_pane_tile() || !right_leaf->get_pane_tile()) {
	return _failure_with_message(p_driver, result.workflow, "Could not create two scene tiles.");
}

ScenePaneTile *left = left_leaf->get_pane_tile();
ScenePaneTile *right = right_leaf->get_pane_tile();
int scene_2d = -1;
int scene_3d = -1;
for (int i = 0; i < editor_data.get_edited_scene_count(); i++) {
	if (editor_data.get_scene_path(i) == BOARD_SWITCH_2D_SCENE) {
		scene_2d = i;
	} else if (editor_data.get_scene_path(i) == BOARD_SWITCH_3D_SCENE) {
		scene_3d = i;
	}
}
if (scene_2d < 0 || scene_3d < 0) {
	return _failure_with_message(p_driver, result.workflow, "Could not resolve both loaded scene indexes.");
}
editor_data.set_scene_tile(scene_2d, left->get_tile_id());
editor_data.set_tile_current_scene(left->get_tile_id(), scene_2d);
editor_data.set_scene_tile(scene_3d, right->get_tile_id());
editor_data.set_tile_current_scene(right->get_tile_id(), scene_3d);
workspace->sync_scene_tabs_from_editor_data();
p_driver.flush_frames(30);

left->set_scene_editor_mode(SceneEditorMode::MODE_2D, true);
right->set_scene_editor_mode(SceneEditorMode::MODE_3D, true);
p_driver.flush_frames(60);

workspace->request_leaf_focus(left->get_tile_id());
p_driver.flush_frames(30);
if (EditorNode::get_editor_main_screen()->get_selected_index() != EditorMainScreen::EDITOR_2D ||
		left->get_scene_editor_mode() != SceneEditorMode::MODE_2D ||
		right->get_scene_editor_mode() != SceneEditorMode::MODE_3D ||
		right->get_preview_mode() != TilePreviewMode::LIVE_3D) {
	return _failure_with_message(p_driver, result.workflow, "Focusing the 2D tile did not preserve independent tile modes.");
}

workspace->request_leaf_focus(right->get_tile_id());
p_driver.flush_frames(30);
if (EditorNode::get_editor_main_screen()->get_selected_index() != EditorMainScreen::EDITOR_3D ||
		left->get_preview_mode() != TilePreviewMode::LIVE_2D) {
	return _failure_with_message(p_driver, result.workflow, "Focusing the 3D tile did not restore its mode or demote the 2D tile correctly.");
}

editor_data.set_scene_tile(scene_3d, left->get_tile_id());
editor_data.set_tile_current_scene(left->get_tile_id(), scene_3d);
workspace->sync_scene_tabs_from_editor_data();
p_driver.flush_frames(30);
if (left->get_scene_editor_mode() != SceneEditorMode::MODE_2D) {
	return _failure_with_message(p_driver, result.workflow, "Changing the current scene tab overwrote the destination tile's mode.");
}
editor_data.set_scene_tile(scene_3d, right->get_tile_id());
editor_data.set_tile_current_scene(left->get_tile_id(), scene_2d);
editor_data.set_tile_current_scene(right->get_tile_id(), scene_3d);
workspace->sync_scene_tabs_from_editor_data();
p_driver.flush_frames(30);

Node *root_2d = editor_data.get_edited_scene_root(scene_2d);
EditorSelection *selection = editor_node->get_editor_selection();
if (!root_2d || !selection) {
	return _failure_with_message(p_driver, result.workflow, "The 2D scene root or editor selection is unavailable.");
}
Node3D *foreign_3d_node = memnew(Node3D);
foreign_3d_node->set_name("Foreign3DSelection");
root_2d->add_child(foreign_3d_node);
selection->clear();
selection->add_node(foreign_3d_node);
selection->update();
workspace->request_leaf_focus(left->get_tile_id());
p_driver.flush_frames(30);
if (left->get_scene_editor_mode() != SceneEditorMode::MODE_2D ||
		EditorNode::get_editor_main_screen()->get_selected_index() != EditorMainScreen::EDITOR_2D) {
	return _failure_with_message(p_driver, result.workflow, "Selecting a 3D node overwrote an initialized 2D tile mode.");
}
selection->clear();
selection->update();

EditorBoardStrip *strip = EditorNode::get_board_strip();
EditorBoard *second_board = strip ? strip->add_board("Second") : nullptr;
if (!second_board) {
	return _failure_with_message(p_driver, result.workflow, "Could not add a second board.");
}
strip->set_active_board(1);
p_driver.flush_frames(60);
strip->set_active_board(0);
p_driver.flush_frames(60);
if (left->get_scene_editor_mode() != SceneEditorMode::MODE_2D ||
		right->get_scene_editor_mode() != SceneEditorMode::MODE_3D) {
	return _failure_with_message(p_driver, result.workflow, "Board switching changed a tile-owned scene mode.");
}

EditorMainScreen *main_screen = EditorNode::get_editor_main_screen();
if (!main_screen->is_button_enabled(EditorMainScreen::EDITOR_GAME)) {
	return _failure_with_message(p_driver, result.workflow, "The Game screen is unavailable.");
}
main_screen->select(EditorMainScreen::EDITOR_GAME);
p_driver.flush_frames(30);
Dictionary board_selector;
board_selector["role"] = "button";
board_selector["name"] = strip->get_active_board()->get_title();
if (!p_driver.require_ok(p_driver.act(board_selector, "click"), "return_from_game_with_active_board")) {
	return _failure_from_driver(p_driver, result.workflow);
}
p_driver.flush_frames(30);
if (main_screen->is_global_screen_selected() ||
		main_screen->get_selected_index() != EditorMainScreen::EDITOR_2D) {
	return _failure_with_message(p_driver, result.workflow, "Clicking the active Board Rail segment did not return from Game to the remembered tile mode.");
}

editor_node->save_editor_layout_delayed();
p_driver.flush_frames(120);
if (!p_driver.wait_editor_idle(10000)) {
	return _failure_with_message(p_driver, result.workflow, "The editor did not become idle after saving tile modes.");
}

if (!p_driver.assert_no_new_errors()) {
	return _failure_from_driver(p_driver, result.workflow);
}
result.ok = true;
result.message = "Two scene tiles retain independent modes across focus, tabs, selection, boards, and Game return; the layout was saved.";
result.details = p_driver.read_editor_state();
return result;
```

The assignment above uses the same `EditorData` and workspace synchronization path as existing split workflows and does not mutate fixture files.

- [ ] **Step 5: Implement restart restoration checks**

Add the second workflow body:

```cpp
EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_tile_local_scene_modes_restore(
		EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "tile_local_scene_modes_restore";
	p_driver.begin_workflow();
	p_driver.flush_frames(90);

	EditorSceneWorkspace *workspace = EditorNode::get_scene_workspace();
	if (!workspace || workspace->get_tile_count() != 2) {
		return _failure_with_message(p_driver, result.workflow, "The restored active board does not contain two scene tiles.");
	}

	ScenePaneTile *tile_2d = nullptr;
	ScenePaneTile *tile_3d = nullptr;
	for (ScenePaneTile *tile : workspace->get_tiles()) {
		if (!tile->is_scene_editor_mode_initialized()) {
			return _failure_with_message(p_driver, result.workflow, "A restored scene tile has no durable mode.");
		}
		if (tile->get_scene_editor_mode() == SceneEditorMode::MODE_2D) {
			tile_2d = tile;
		} else if (tile->get_scene_editor_mode() == SceneEditorMode::MODE_3D) {
			tile_3d = tile;
		}
	}
	if (!tile_2d || !tile_3d) {
		return _failure_with_message(p_driver, result.workflow, "The restored layout did not preserve one 2D tile and one 3D tile.");
	}

	workspace->request_leaf_focus(tile_2d->get_tile_id());
	p_driver.flush_frames(30);
	if (EditorNode::get_editor_main_screen()->get_selected_index() != EditorMainScreen::EDITOR_2D) {
		return _failure_with_message(p_driver, result.workflow, "The restored 2D tile did not activate the shared 2D editor.");
	}
	workspace->request_leaf_focus(tile_3d->get_tile_id());
	p_driver.flush_frames(30);
	if (EditorNode::get_editor_main_screen()->get_selected_index() != EditorMainScreen::EDITOR_3D ||
			tile_2d->get_preview_mode() != TilePreviewMode::LIVE_2D) {
		return _failure_with_message(p_driver, result.workflow, "The restored 3D tile did not activate independently of the 2D preview.");
	}

	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	result.ok = true;
	result.message = "Independent tile modes restored across an editor restart.";
	result.details = p_driver.read_editor_state();
	return result;
}
```

- [ ] **Step 6: Run new and existing real-editor regressions**

```bash
python3 scripts/agent_build.py --backend ninja --test \
  --case "*two tiles keep independent modes*" \
  --case "*Switching boards with a 3D scene open*" \
  --case "*3D preview context lifetime*"
```

Expected: both seed and restore results report `ok:true`; the two existing 3D regressions also pass.

- [ ] **Step 7: Commit acceptance coverage**

```bash
git add editor/automation/editor_automation_acceptance_workflow.h editor/automation/editor_automation_acceptance_workflow.cpp editor/automation/editor_automation_workflow_registry.cpp tests/editor/test_editor_automation_workflow.h tests/editor/test_editor_tile_local_scene_modes.h tests/test_main.cpp
git commit -m "Test independent scene tile modes"
```

## Task 8: Verify accessibility, visual quality, and full integration

**Files:**
- Modify if verification finds a real defect: files already listed in Tasks 1-7
- Create outside git: screenshots and review-gallery entries under the configured Foundry gallery directory

- [ ] **Step 1: Run formatting and focused suites**

```bash
pre-commit run --files \
  editor/editor_scene_mode.h \
  editor/editor_scene_pane_tile.h \
  editor/editor_scene_pane_tile.cpp \
  editor/gui/editor_scene_mode_switcher.h \
  editor/gui/editor_scene_mode_switcher.cpp \
  editor/gui/editor_board_switcher.h \
  editor/gui/editor_board_switcher.cpp \
  editor/gui/editor_board_actions_menu.h \
  editor/gui/editor_board_actions_menu.cpp \
  editor/scene/editor_scene_tabs.h \
  editor/scene/editor_scene_tabs.cpp \
  editor/editor_node.h \
  editor/editor_node.cpp \
  editor/editor_main_screen.cpp \
  editor/editor_interface.cpp \
  editor/themes/theme_classic.cpp \
  editor/themes/theme_modern.cpp \
  editor/automation/editor_automation_workspace.cpp \
  editor/automation/editor_automation_acceptance_workflow.h \
  editor/automation/editor_automation_acceptance_workflow.cpp \
  editor/automation/editor_automation_workflow_registry.cpp \
  tests/editor/test_editor_scene_mode_switcher.h \
  tests/editor/test_scene_pane_tile_mode.h \
  tests/editor/test_editor_tile_local_scene_modes.h \
  tests/editor/test_editor_automation_workflow.h \
  tests/editor/test_editor_board_switcher.h \
  tests/editor/test_editor_automation_workspace.h \
  tests/test_main.cpp

python3 scripts/agent_build.py --backend ninja --test \
  --case "*BoardSwitcher*" \
  --case "*SceneModeSwitcher*" \
  --case "*ScenePaneTileMode*" \
  --case "*two tiles keep independent modes*"
```

Expected: pre-commit passes; all focused doctest cases pass.

- [ ] **Step 2: Build the native strict editor**

```bash
python3 scripts/agent_build.py
```

Expected: exit code 0 with `dev_mode=yes dev_build=yes tests=yes` in the printed invocation and a successful `build_summary`.

- [ ] **Step 3: Prepare a disposable review project**

Run:

```bash
REVIEW_PROJECT=$(mktemp -d /tmp/foundry-board-rail-review-project.XXXXXX)
cp -R tests/fixtures/editor_automation_workflow/. "$REVIEW_PROJECT"
echo "$REVIEW_PROJECT"
```

Expected: the final line prints an absolute temporary project directory containing `project.foundry`.

- [ ] **Step 4: Exercise the real GUI through Foundry automation**

Call `foundry_launch_editor` with `project` set to the exact directory printed in Step 3. Let the bridge discover the strict editor binary in this worktree. Use semantic calls only:

1. `foundry_observe_ui` and find `Board Menu`, board-name buttons, `2D Scene Mode`, and `3D Scene Mode`.
2. Create or restore two tiles.
3. Set one to 2D and one to 3D, then capture `/tmp/foundry-board-rail-review/independent-tile-modes.png`.
4. Switch focus and boards, checking `read_editor_state` mode fields after each action.
5. Open Game, click the already-active board, and verify the scene workspace returns.
6. Open the Board Menu and capture `/tmp/foundry-board-rail-review/board-menu.png`.
7. Restore the normal-width dark theme and capture `/tmp/foundry-board-rail-review/after-board-rail.png`.
8. Resize to force compact mode, verify the Board Menu lists all boards, and capture `/tmp/foundry-board-rail-review/compact-board-rail.png`.
9. Repeat the interaction checks in the light editor theme.
10. Poll events and require no new editor errors.

If a control lacks a stable semantic name or state, fix the automation metadata in the control rather than using coordinates.

- [ ] **Step 5: Capture the required review gallery**

Capture full-editor PNGs with `foundry_capture_screenshot` at the paths below, then disconnect the editor and add them:

```bash
python3 scripts/review_gallery.py add /tmp/foundry-board-rail-review/before-board-switcher.png \
  --board "Board Rail" --mode design \
  --caption "Before: verify boards and editor modes compete as separate title-bar controls." \
  --pair before

python3 scripts/review_gallery.py add /tmp/foundry-board-rail-review/after-board-rail.png \
  --board "Board Rail" --mode design \
  --caption "After: verify the centered Board Rail is the only persistent workspace navigation." \
  --pair after

python3 scripts/review_gallery.py add /tmp/foundry-board-rail-review/independent-tile-modes.png \
  --board "Independent tile modes" --mode proof \
  --caption "Verify one tile shows 2D and the other 3D, with each local switch matching its surface."

python3 scripts/review_gallery.py add /tmp/foundry-board-rail-review/board-menu.png \
  --board "Board Rail menu" --mode proof \
  --caption "Verify New Board, Overview, rename, reorder, and close actions live behind one trailing menu."

python3 scripts/review_gallery.py add /tmp/foundry-board-rail-review/compact-board-rail.png \
  --board "Compact Board Rail" --mode proof \
  --caption "Verify narrow width keeps the active board visible and lists all boards in the menu."
```

Serve an authenticated remote gallery for handoff; the helper falls back to a local URL if ngrok is unavailable:

```bash
GALLERY_PASSWORD=$(openssl rand -hex 12)
echo "$GALLERY_PASSWORD"
python3 scripts/review_gallery.py serve --public --basic-auth "reviewer:$GALLERY_PASSWORD"
```

Expected: the command prints a public ngrok URL or an explicit local fallback URL. Keep the generated credential out of git and include it only in the private handoff.

- [ ] **Step 6: Run the full test suite with structured progress**

```bash
python3 scripts/agent_build.py --test
```

Expected: the wrapper's native strict build is current and the final doctest summary reports success. Trust the doctest summary if cleanup prints the documented ObjectDB/resource warnings.

- [ ] **Step 7: Run repository test-authoring policy checks**

```bash
git grep -nE '\.split\("(void|bool|String|private fun|const val|def )' -- '*.py'
git grep -n "docs/superpowers" -- '*/test_*.py' '*/check_*.py'
git grep -n "hasattr(" -- '*/test_*.py'
git grep -l "assert" -- 'misc/checks/check_*.py'
git diff --check
```

Expected: no new output attributable to this change; `git diff --check` exits 0.

- [ ] **Step 8: Commit verification-only fixes, if any**

If GUI or full-suite verification required code changes, stage only those fixes and their tests:

Inspect `git status --short`. If GUI or full-suite verification required code changes, stage only the changed files already listed in Tasks 1-7 plus their matching tests, rerun the failed check, and commit them with `git commit -m "Polish board rail integration"`. If no fixes were required, do not create an empty commit.

- [ ] **Step 9: Prepare the implementation handoff**

Report:

- focused and full test commands with their final summaries;
- native strict build result and printed log/progress paths;
- review-gallery URL;
- final commit list;
- remaining unrelated untracked files, explicitly excluding them from the feature.
