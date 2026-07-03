# Run Targets Dropdown Modal Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move Run Targets configuration out of the sidebar dock and into a modal opened from a small run-options dropdown beside the main Run Project button.

**Architecture:** Keep `EditorRunNative` as the shared `RunTargetManager` owner. Make `RunTargetsPanel` an embeddable control instead of an `EditorDock`, and let `EditorRunBar` own both the new run-options menu and the `Run Targets` modal. Rename the first-open marker API to configuration-oriented wording while preserving the existing on-disk key.

**Tech Stack:** Godot/Foundry editor C++, doctest-style C++ tests under `tests/editor/run`, SCons macOS editor build.

---

## Scope Check

This is one editor run-workflow subsystem. It touches run bar UI placement, the existing run-target configuration control, first-open configuration discovery, and tests around those surfaces.

## File Structure

- `editor/run/editor_run_bar.h`: Declare the run-options menu model, dropdown state, modal state, and public modal-opening method.
- `editor/run/editor_run_bar.cpp`: Build the dropdown, handle menu item selection, lazily construct the modal, and assign theme icons.
- `editor/run/run_targets_panel.h`: Change `RunTargetsPanel` from `EditorDock` to an embeddable `VBoxContainer`.
- `editor/run/run_targets_panel.cpp`: Remove dock-only setup and keep the existing run-target content behavior.
- `editor/run/run_target_manager.h`: Add modal-neutral first-open marker API names while keeping legacy wrappers.
- `editor/run/run_target_manager.cpp`: Implement the renamed first-open marker API and preserve the existing config key.
- `editor/editor_node.h`: Remove the dock field and rename the first-open helper.
- `editor/editor_node.cpp`: Stop registering a Run Targets dock and open the run-target modal on the first-open marker.
- `editor/project_manager/ios_project_template.cpp`: Use the renamed first-open marker request API.
- `tests/editor/run/test_editor_run_bar.h`: New headless tests for the run-options menu model.
- `tests/editor/run/test_run_target.h`: Update and extend first-open marker tests.
- `tests/editor/run/test_run_targets_panel.h`: Add compile-time assertions that `RunTargetsPanel` is embeddable and no longer a dock.
- `tests/test_main.cpp`: Include the new run bar test header.

---

### Task 1: Add a Testable Run Options Menu Model

**Files:**
- Create: `tests/editor/run/test_editor_run_bar.h`
- Modify: `tests/test_main.cpp`
- Modify: `editor/run/editor_run_bar.h`
- Modify: `editor/run/editor_run_bar.cpp`

- [ ] **Step 1: Write the failing run-options menu model test**

Create `tests/editor/run/test_editor_run_bar.h`:

```cpp
/**************************************************************************/
/*  test_editor_run_bar.h                                                 */
/**************************************************************************/

#pragma once

#ifdef TOOLS_ENABLED

#include "editor/run/editor_run_bar.h"

#include "tests/test_macros.h"

namespace TestEditorRunBar {

TEST_CASE("[EditorRunBar] Run options menu exposes run targets configuration") {
	const Vector<EditorRunBar::RunOptionsMenuEntry> entries = EditorRunBar::build_run_options_menu_model();

	REQUIRE_EQ(entries.size(), 1);
	CHECK_EQ(entries[0].id, EditorRunBar::RUN_OPTIONS_CONFIGURE_RUN_TARGETS);
	CHECK_EQ(entries[0].label, String("Run Targets Configuration..."));
}

} // namespace TestEditorRunBar

#endif // TOOLS_ENABLED
```

Add the include to `tests/test_main.cpp` with the other editor run tests:

```cpp
#include "tests/editor/run/test_editor_run_bar.h"
```

- [ ] **Step 2: Run the red build**

Run:

```bash
python3 -m SCons platform=macos target=editor dev_build=yes tests=yes cache_path="$HOME/.scons_cache" -j"$(sysctl -n hw.logicalcpu)"
```

Expected: compile failure mentioning `RunOptionsMenuEntry`, `build_run_options_menu_model`, or `RUN_OPTIONS_CONFIGURE_RUN_TARGETS` is not a member of `EditorRunBar`.

- [ ] **Step 3: Add the minimal menu model API**

In the public section of `editor/run/editor_run_bar.h`, add:

```cpp
	struct RunOptionsMenuEntry {
		int id = 0;
		String label;
	};

	enum RunOptionsMenuItem {
		RUN_OPTIONS_CONFIGURE_RUN_TARGETS = 0,
	};

	static Vector<RunOptionsMenuEntry> build_run_options_menu_model();
```

In `editor/run/editor_run_bar.cpp`, add the implementation after `_get_xr_mode_play_args`:

```cpp
Vector<EditorRunBar::RunOptionsMenuEntry> EditorRunBar::build_run_options_menu_model() {
	Vector<RunOptionsMenuEntry> entries;

	RunOptionsMenuEntry configure_run_targets;
	configure_run_targets.id = RUN_OPTIONS_CONFIGURE_RUN_TARGETS;
	configure_run_targets.label = TTRC("Run Targets Configuration...");
	entries.push_back(configure_run_targets);

	return entries;
}
```

- [ ] **Step 4: Run the focused green test**

Run:

```bash
python3 -m SCons platform=macos target=editor dev_build=yes tests=yes cache_path="$HOME/.scons_cache" -j"$(sysctl -n hw.logicalcpu)"
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*EditorRunBar*" --force-colors
```

Expected: build exits 0 and the test run prints `[doctest] Status: SUCCESS!`.

- [ ] **Step 5: Commit**

Run:

```bash
git add tests/editor/run/test_editor_run_bar.h tests/test_main.cpp editor/run/editor_run_bar.h editor/run/editor_run_bar.cpp
git commit -m "Add run options menu model"
```

---

### Task 2: Rename the First-Open Marker API Without Changing the Stored Key

**Files:**
- Modify: `tests/editor/run/test_run_target.h`
- Modify: `tests/editor/project_manager/test_ios_project_template.h`
- Modify: `editor/run/run_target_manager.h`
- Modify: `editor/run/run_target_manager.cpp`
- Modify: `editor/project_manager/ios_project_template.cpp`

- [ ] **Step 1: Write failing marker tests for the new API and legacy key**

In `tests/editor/run/test_run_target.h`, replace the test named `"[Editor][RunTarget] First-open dock marker round-trips and is consumed once"` with:

```cpp
TEST_CASE("[Editor][RunTarget] First-open configuration marker round-trips and is consumed once") {
	const String path = TestUtils::get_temp_path("run_targets_first_open.cfg");
	if (FileAccess::exists(path)) {
		DirAccess::remove_absolute(path);
	}

	Vector<RunTarget> initial;
	initial.push_back(make_target("My iPhone"));
	REQUIRE_EQ(RunTarget::save_all(path, initial), OK);
	REQUIRE_EQ(RunTargetManager::request_show_configuration_on_first_open(path), OK);

	const Vector<RunTarget> after_request = RunTarget::load_all(path);
	REQUIRE_EQ(after_request.size(), 1);
	CHECK_EQ(after_request[0], initial[0]);

	CHECK(RunTargetManager::consume_show_configuration_on_first_open(path));
	CHECK_FALSE(RunTargetManager::consume_show_configuration_on_first_open(path));

	const Vector<RunTarget> after_consume = RunTarget::load_all(path);
	REQUIRE_EQ(after_consume.size(), 1);
	CHECK_EQ(after_consume[0], initial[0]);
}
```

Add this test after it:

```cpp
TEST_CASE("[Editor][RunTarget] First-open configuration consumes the legacy dock key") {
	const String path = TestUtils::get_temp_path("run_targets_first_open_legacy_key.cfg");
	if (FileAccess::exists(path)) {
		DirAccess::remove_absolute(path);
	}

	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("meta", "show_dock_on_first_open", true);
	REQUIRE_EQ(config->save(path), OK);

	CHECK(RunTargetManager::consume_show_configuration_on_first_open(path));
	CHECK_FALSE(RunTargetManager::consume_show_configuration_on_first_open(path));
}
```

Update the later first-open tests in the same file to call `consume_show_configuration_on_first_open` and `request_show_configuration_on_first_open`.

In `tests/editor/project_manager/test_ios_project_template.h`, rename the test case string to:

```cpp
TEST_CASE("[Editor][IOSProjectTemplate] Seeding requests Run Targets configuration on first open") {
```

and update both consumes to:

```cpp
CHECK(RunTargetManager::consume_show_configuration_on_first_open(targets_path));
CHECK_FALSE(RunTargetManager::consume_show_configuration_on_first_open(targets_path));
```

- [ ] **Step 2: Run the red build**

Run:

```bash
python3 -m SCons platform=macos target=editor dev_build=yes tests=yes cache_path="$HOME/.scons_cache" -j"$(sysctl -n hw.logicalcpu)"
```

Expected: compile failure mentioning `request_show_configuration_on_first_open` or `consume_show_configuration_on_first_open` is not a member of `RunTargetManager`.

- [ ] **Step 3: Add the renamed API with legacy wrappers**

In `editor/run/run_target_manager.h`, replace the first-open marker declarations and comments with:

```cpp
	// First-open marker for Run Targets configuration. The "Mobile (iOS)"
	// project template seeds a half-configured run target and asks the editor to
	// reveal configuration once, on first open, so the run-target workflow is
	// discoverable. The flag is stored in run_targets.cfg's meta section so no
	// extra file is introduced.
	//
	// `request_show_configuration_on_first_open` writes the flag, preserving any
	// targets and active selection already in the file.
	// `consume_show_configuration_on_first_open` reports whether the flag was set
	// and clears it, so configuration is revealed at most once.
	static Error request_show_configuration_on_first_open(const String &p_config_path);
	static bool consume_show_configuration_on_first_open(const String &p_config_path);

	static Error request_show_dock_on_first_open(const String &p_config_path);
	static bool consume_show_dock_on_first_open(const String &p_config_path);
```

In `editor/run/run_target_manager.cpp`, rename the constant to this exact line while keeping the value:

```cpp
constexpr const char *SHOW_CONFIGURATION_ON_FIRST_OPEN_KEY = "show_dock_on_first_open";
```

Rename the two existing method definitions to:

```cpp
Error RunTargetManager::request_show_configuration_on_first_open(const String &p_config_path) {
```

and:

```cpp
bool RunTargetManager::consume_show_configuration_on_first_open(const String &p_config_path) {
```

Inside those methods, replace uses of `SHOW_DOCK_ON_FIRST_OPEN_KEY` with `SHOW_CONFIGURATION_ON_FIRST_OPEN_KEY`.

After `consume_show_configuration_on_first_open`, add the legacy wrappers:

```cpp
Error RunTargetManager::request_show_dock_on_first_open(const String &p_config_path) {
	return request_show_configuration_on_first_open(p_config_path);
}

bool RunTargetManager::consume_show_dock_on_first_open(const String &p_config_path) {
	return consume_show_configuration_on_first_open(p_config_path);
}
```

Change the warning text in the consume method to:

```cpp
		WARN_PRINT(vformat("Could not clear the run-target first-open marker at \"%s\" (error %d); skipping the Run Targets configuration reveal.", p_config_path, save_error));
```

- [ ] **Step 4: Update the iOS project template call site**

In `editor/project_manager/ios_project_template.cpp`, change:

```cpp
	return RunTargetManager::request_show_dock_on_first_open(targets_path);
```

to:

```cpp
	return RunTargetManager::request_show_configuration_on_first_open(targets_path);
```

Update the nearby comment to:

```cpp
	// Ask the editor to reveal Run Targets configuration the first time this
	// project is opened, landing the user on the readiness ladder ("here's what's
	// left to run on your phone") instead of leaving the seeded target
	// undiscovered.
```

- [ ] **Step 5: Run the focused green tests**

Run:

```bash
python3 -m SCons platform=macos target=editor dev_build=yes tests=yes cache_path="$HOME/.scons_cache" -j"$(sysctl -n hw.logicalcpu)"
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*RunTarget*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*IOSProjectTemplate*" --force-colors
```

Expected: build exits 0 and both test runs print `[doctest] Status: SUCCESS!`.

- [ ] **Step 6: Commit**

Run:

```bash
git add tests/editor/run/test_run_target.h tests/editor/project_manager/test_ios_project_template.h editor/run/run_target_manager.h editor/run/run_target_manager.cpp editor/project_manager/ios_project_template.cpp
git commit -m "Rename run target first-open marker API"
```

---

### Task 3: Make RunTargetsPanel Embeddable Instead of Dockable

**Files:**
- Modify: `tests/editor/run/test_run_targets_panel.h`
- Modify: `editor/run/run_targets_panel.h`
- Modify: `editor/run/run_targets_panel.cpp`

- [ ] **Step 1: Write the failing compile-time inheritance assertions**

In `tests/editor/run/test_run_targets_panel.h`, add these includes after the existing includes:

```cpp
#include "editor/docks/editor_dock.h"
#include "scene/gui/box_container.h"

#include <type_traits>
```

Add these assertions inside `namespace TestRunTargetsPanel`, before `make_step`:

```cpp
static_assert(std::is_base_of<VBoxContainer, RunTargetsPanel>::value, "RunTargetsPanel must be embeddable in dialogs as a VBoxContainer.");
static_assert(!std::is_base_of<EditorDock, RunTargetsPanel>::value, "RunTargetsPanel must not be registered as an editor dock.");
```

- [ ] **Step 2: Run the red build**

Run:

```bash
python3 -m SCons platform=macos target=editor dev_build=yes tests=yes cache_path="$HOME/.scons_cache" -j"$(sysctl -n hw.logicalcpu)"
```

Expected: compile failure from the new `static_assert` lines.

- [ ] **Step 3: Change RunTargetsPanel inheritance**

In `editor/run/run_targets_panel.h`, replace:

```cpp
#include "editor/docks/editor_dock.h"
```

with:

```cpp
#include "scene/gui/box_container.h"
```

Replace the class comment opening line with:

```cpp
// Run Targets configuration: configure run targets, edit their signing/device,
// and view the live readiness ladder for the selected target.
```

Replace the class declaration:

```cpp
class RunTargetsPanel : public EditorDock {
	FOUNDRY_CLASS(RunTargetsPanel, EditorDock);
```

with:

```cpp
class RunTargetsPanel : public VBoxContainer {
	FOUNDRY_CLASS(RunTargetsPanel, VBoxContainer);
```

In `editor/run/run_targets_panel.cpp`, remove this line from the constructor:

```cpp
	set_default_slot(EditorDock::DOCK_SLOT_RIGHT_BR);
```

- [ ] **Step 4: Run the focused green test**

Run:

```bash
python3 -m SCons platform=macos target=editor dev_build=yes tests=yes cache_path="$HOME/.scons_cache" -j"$(sysctl -n hw.logicalcpu)"
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*RunTargetsPanel*" --force-colors
```

Expected: build exits 0 and the test run prints `[doctest] Status: SUCCESS!`.

- [ ] **Step 5: Commit**

Run:

```bash
git add tests/editor/run/test_run_targets_panel.h editor/run/run_targets_panel.h editor/run/run_targets_panel.cpp
git commit -m "Make run targets panel embeddable"
```

---

### Task 4: Add the Run Options Dropdown and Run Targets Modal to EditorRunBar

**Files:**
- Modify: `editor/run/editor_run_bar.h`
- Modify: `editor/run/editor_run_bar.cpp`

- [ ] **Step 1: Add EditorRunBar modal/dropdown declarations**

In `editor/run/editor_run_bar.h`, add a forward declaration near the other class declarations:

```cpp
class RunTargetsPanel;
```

Add these members after `Button *play_button = nullptr;`:

```cpp
	MenuButton *run_options_button = nullptr;
	AcceptDialog *run_targets_dialog = nullptr;
	RunTargetsPanel *run_targets_panel = nullptr;
```

Add these private methods near the other run button handlers:

```cpp
	void _run_options_item_pressed(int p_id);
	void _ensure_run_targets_dialog();
```

Add this public method near `play_main_scene`:

```cpp
	void open_run_targets_configuration();
```

- [ ] **Step 2: Add includes and handlers in EditorRunBar**

In `editor/run/editor_run_bar.cpp`, add:

```cpp
#include "editor/run/run_targets_panel.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/popup_menu.h"
```

Add these methods after `_get_xr_mode_play_args` and before `_quick_run_selected`:

```cpp
void EditorRunBar::_run_options_item_pressed(int p_id) {
	switch (p_id) {
		case RUN_OPTIONS_CONFIGURE_RUN_TARGETS: {
			open_run_targets_configuration();
		} break;
	}
}

void EditorRunBar::_ensure_run_targets_dialog() {
	if (run_targets_dialog != nullptr) {
		return;
	}

	run_targets_dialog = memnew(AcceptDialog);
	run_targets_dialog->set_title(TTR("Run Targets"));
	run_targets_dialog->set_min_size(Size2(760, 420) * EDSCALE);
	add_child(run_targets_dialog);

	run_targets_panel = memnew(RunTargetsPanel);
	run_targets_panel->set_custom_minimum_size(Size2(760, 420) * EDSCALE);
	run_targets_panel->set_h_size_flags(SIZE_EXPAND_FILL);
	run_targets_panel->set_v_size_flags(SIZE_EXPAND_FILL);
	run_targets_dialog->add_child(run_targets_panel);
}

void EditorRunBar::open_run_targets_configuration() {
	if (Engine::get_singleton()->is_recovery_mode_hint()) {
		return;
	}

	_ensure_run_targets_dialog();
	run_targets_dialog->popup_centered_clamped(Size2(900, 540) * EDSCALE, 0.8);
}
```

- [ ] **Step 3: Add the dropdown next to Run Project**

In `EditorRunBar::EditorRunBar()`, immediately after the `play_button->connect(...)` line, add:

```cpp
	run_options_button = memnew(MenuButton);
	main_hbox->add_child(run_options_button);
	run_options_button->set_theme_type_variation("RunBarButton");
	run_options_button->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	run_options_button->set_tooltip_text(TTRC("Run Options"));
	run_options_button->set_accessibility_name(TTRC("Run Options"));
	run_options_button->set_flat(false);

	PopupMenu *run_options_popup = run_options_button->get_popup();
	for (const RunOptionsMenuEntry &entry : build_run_options_menu_model()) {
		run_options_popup->add_item(entry.label, entry.id);
	}
	run_options_popup->connect(SceneStringName(id_pressed), callable_mp(this, &EditorRunBar::_run_options_item_pressed));
```

- [ ] **Step 4: Add the dropdown icon on theme changes**

In the non-recovery `NOTIFICATION_THEME_CHANGED` block, after `_update_play_buttons();`, add:

```cpp
			run_options_button->set_button_icon(get_editor_theme_icon(SNAME("GuiDropdown")));
```

- [ ] **Step 5: Build and run menu-model test**

Run:

```bash
python3 -m SCons platform=macos target=editor dev_build=yes tests=yes cache_path="$HOME/.scons_cache" -j"$(sysctl -n hw.logicalcpu)"
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*EditorRunBar*" --force-colors
```

Expected: build exits 0 and the test run prints `[doctest] Status: SUCCESS!`.

- [ ] **Step 6: Commit**

Run:

```bash
git add editor/run/editor_run_bar.h editor/run/editor_run_bar.cpp
git commit -m "Add run targets configuration modal"
```

---

### Task 5: Remove Run Targets Dock Registration and Open the Modal on First Open

**Files:**
- Modify: `editor/editor_node.h`
- Modify: `editor/editor_node.cpp`

- [ ] **Step 1: Remove the dock field and rename the first-open helper**

In `editor/editor_node.h`, remove:

```cpp
class RunTargetsPanel;
```

Remove this member:

```cpp
	RunTargetsPanel *run_targets_dock = nullptr;
```

Replace this declaration:

```cpp
	void _show_run_targets_dock_on_first_open();
```

with:

```cpp
	void _show_run_targets_configuration_on_first_open();
```

- [ ] **Step 2: Update EditorNode implementation**

In `editor/editor_node.cpp`, remove:

```cpp
#include "editor/run/run_targets_panel.h"
```

Replace the first-scan call and comment:

```cpp
		// Reveal Run Targets configuration the first time a freshly seeded
		// "Mobile (iOS)" project is opened, so the run-target readiness ladder is
		// discoverable. Runs after the layout load so it wins over restored UI.
		_show_run_targets_configuration_on_first_open();
```

Replace the helper implementation with:

```cpp
void EditorNode::_show_run_targets_configuration_on_first_open() {
	// Command-line/headless runs (export, import, tests) must not consume the
	// one-shot marker: there is no modal to reveal, and burning it here would
	// mean the first interactive open never shows Run Targets configuration.
	if (cmdline_mode || project_run_bar == nullptr) {
		return;
	}

	// The marker is seeded into the project's run_targets.cfg by the "Mobile (iOS)"
	// project template and consumed here, so Run Targets configuration is revealed
	// exactly once on the first editor open of a freshly created project.
	if (RunTargetManager::consume_show_configuration_on_first_open("res://run_targets.cfg")) {
		project_run_bar->open_run_targets_configuration();
	}
}
```

Remove these lines from the dock setup block:

```cpp
	run_targets_dock = memnew(RunTargetsPanel);
	editor_dock_manager->add_dock(run_targets_dock);
```

- [ ] **Step 3: Build**

Run:

```bash
python3 -m SCons platform=macos target=editor dev_build=yes tests=yes cache_path="$HOME/.scons_cache" -j"$(sysctl -n hw.logicalcpu)"
```

Expected: build exits 0.

- [ ] **Step 4: Commit**

Run:

```bash
git add editor/editor_node.h editor/editor_node.cpp
git commit -m "Open run targets configuration from first-open marker"
```

---

### Task 6: Clean Up Run-Target Dock Wording

**Files:**
- Modify: `editor/run/editor_run_native.h`
- Modify: `editor/run/editor_run_native.cpp`
- Modify: `editor/run/run_targets_panel.cpp`
- Modify: `editor/project_manager/ios_project_template.h`

- [ ] **Step 1: Replace stale dock wording**

Run:

```bash
rg -n "Targets dock|Run Targets dock|dock" editor/run editor/project_manager/ios_project_template.h
```

Edit only references that describe the old Run Targets dock. Use these replacement phrases:

```text
Run Targets configuration
configuration UI
modal
```

Specific replacements to make:

In `editor/run/editor_run_native.h`, change:

```cpp
	// Targets dock (and any other surface) reaches the same instance through
```

to:

```cpp
	// Run Targets configuration (and any other surface) reaches the same instance through
```

In `editor/run/editor_run_native.cpp`, change:

```cpp
			// lives in the Targets dock, so only configured targets are enriched here.
```

to:

```cpp
			// lives in Run Targets configuration, so only configured targets are enriched here.
```

In `editor/run/run_targets_panel.cpp`, change this error text:

```cpp
		ERR_PRINT(vformat("Run Targets: could not load \"%s\" (error %d). The dock is read-only until the file is fixed.", String(RUN_TARGETS_CONFIG_PATH), load_error));
```

to:

```cpp
		ERR_PRINT(vformat("Run Targets: could not load \"%s\" (error %d). Configuration is read-only until the file is fixed.", String(RUN_TARGETS_CONFIG_PATH), load_error));
```

In `editor/project_manager/ios_project_template.h`, update comments mentioning the Targets dock to mention Run Targets configuration.

- [ ] **Step 2: Verify no stale Run Targets dock wording remains**

Run:

```bash
rg -n "Targets dock|Run Targets dock|run_targets_dock|show_run_targets_dock" editor tests
```

Expected: no output.

- [ ] **Step 3: Run focused tests**

Run:

```bash
python3 -m SCons platform=macos target=editor dev_build=yes tests=yes cache_path="$HOME/.scons_cache" -j"$(sysctl -n hw.logicalcpu)"
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*RunTarget*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*EditorRunBar*" --force-colors
```

Expected: build exits 0 and both test runs print `[doctest] Status: SUCCESS!`.

- [ ] **Step 4: Commit**

Run:

```bash
git add editor/run/editor_run_native.h editor/run/editor_run_native.cpp editor/run/run_targets_panel.cpp editor/project_manager/ios_project_template.h
git commit -m "Remove stale run targets dock wording"
```

---

### Task 7: Final Verification

**Files:**
- Verify all modified files.

- [ ] **Step 1: Check worktree status**

Run:

```bash
git status --short --branch
```

Expected: current branch is `run-targets-dropdown-modal`; any uncommitted files are intentional implementation changes ready for final commit or already clean after Task 6.

- [ ] **Step 2: Run final build**

Run:

```bash
python3 -m SCons platform=macos target=editor dev_build=yes tests=yes cache_path="$HOME/.scons_cache" -j"$(sysctl -n hw.logicalcpu)"
```

Expected: build exits 0.

- [ ] **Step 3: Run focused editor run tests**

Run:

```bash
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*RunTarget*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*EditorRunBar*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*IOSProjectTemplate*" --force-colors
```

Expected: each run prints `[doctest] Status: SUCCESS!`.

- [ ] **Step 4: Inspect final diff**

Run:

```bash
git diff origin/develop...HEAD -- editor/run editor/editor_node.h editor/editor_node.cpp editor/project_manager tests/editor/run tests/editor/project_manager tests/test_main.cpp docs/superpowers
```

Expected: diff shows the approved run-options dropdown, modal migration, first-open marker rename, tests, design spec, and this implementation plan.

- [ ] **Step 5: Report verification evidence**

Record the exact build command, focused test commands, and their observed status lines in the final response.
