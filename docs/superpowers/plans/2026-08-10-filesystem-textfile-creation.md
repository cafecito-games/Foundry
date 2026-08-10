# FileSystem TextFile Creation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make FileSystem dock text-file creation open and complete successfully even when no script editor view exists.

**Architecture:** Move the New Text File dialog setup and completion path into `ScriptEditorController`, which already owns the global `EditorFileDialog`. Keep view-specific open-file operations routed to `ScriptEditorView`, but make both the FileSystem dock and script editor menu delegate text-file creation to the controller-owned flow.

**Tech Stack:** Godot-style C++17, Foundry editor controls and resource loading, doctest C++ tests, SCons/Ninja through `scripts/agent_build.py`, Foundry editor MCP automation.

---

## File Map

- Modify `tests/editor/test_script_editor_views.h`: add no-view regression coverage for opening and completing the New Text File dialog.
- Modify `editor/script/script_editor_controller.cpp`: configure/show the global dialog and handle its text-file selection before view routing.
- Modify `editor/script/script_editor_view.cpp`: remove the view-owned creation implementation and delegate the script menu action to the controller.
- No change is required in `editor/docks/filesystem_dock.cpp`; it already supplies the selected directory to the controller API.

### Task 1: Add the no-active-view regression tests

**Files:**
- Modify: `tests/editor/test_script_editor_views.h:81-100`
- Test: `tests/editor/test_script_editor_views.h`

- [ ] **Step 1: Add a shared scratch-directory helper**

Insert this helper immediately before `write_temp_text_file()` and update that existing function to use it. This keeps every file created by this test header under `FOUNDRY_TEST_SCRATCH` during agent runs while retaining a cache-directory fallback for direct runs.

```cpp
static String get_script_view_scratch_dir() {
	String root;
	if (OS::get_singleton()->has_environment("FOUNDRY_TEST_SCRATCH")) {
		root = OS::get_singleton()->get_environment("FOUNDRY_TEST_SCRATCH");
	} else {
		root = OS::get_singleton()->get_cache_path().path_join("foundry_tests");
	}

	const String dir = root.path_join("script_editor_views");
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	ERR_FAIL_COND_V(da.is_null(), String());
	ERR_FAIL_COND_V(da->make_dir_recursive(dir) != OK, String());
	return dir;
}

static String write_temp_text_file(const String &p_name, const String &p_source) {
	const String dir = get_script_view_scratch_dir();
	ERR_FAIL_COND_V(dir.is_empty(), String());
	const String path = dir.path_join(p_name);
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	ERR_FAIL_COND_V(file.is_null(), String());
	file->store_string(p_source);
	return path;
}
```

- [ ] **Step 2: Add tests that exercise the real controller and dialog signal with zero registered views**

Insert these cases after the scratch helper and before the existing multi-view cases:

```cpp
TEST_CASE("[Editor][script-controller-textfile-dialog] Opens without an active script view") {
	ScriptControllerHarness h;
	h.mount();
	h.pump();

	const String scratch_dir = get_script_view_scratch_dir();
	REQUIRE_FALSE(scratch_dir.is_empty());
	CHECK(h.controller->get_open_scripts().is_empty());

	h.controller->open_text_file_create_dialog(scratch_dir, "new_notes.txt");
	h.pump();

	EditorFileDialog *dialog = h.controller->get_file_dialog();
	REQUIRE(dialog != nullptr);
	CHECK(dialog->is_visible());
	CHECK(dialog->get_current_dir() == scratch_dir);
	CHECK(dialog->get_current_file() == "new_notes.txt");

	h.unmount();
}

TEST_CASE("[Editor][script-controller-textfile-dialog] Creates a selected file without an active script view") {
	ScriptControllerHarness h;
	h.mount();
	h.pump();

	const String scratch_dir = get_script_view_scratch_dir();
	REQUIRE_FALSE(scratch_dir.is_empty());
	const String file_path = scratch_dir.path_join("created_without_view.txt");
	if (FileAccess::exists(file_path)) {
		REQUIRE(DirAccess::remove_absolute(file_path) == OK);
	}
	REQUIRE_FALSE(FileAccess::exists(file_path));

	h.controller->open_text_file_create_dialog(scratch_dir);
	h.pump();
	h.controller->get_file_dialog()->emit_signal("file_selected", file_path);
	h.pump();

	CHECK(FileAccess::exists(file_path));

	h.unmount();
}
```

- [ ] **Step 3: Build and run the new cases to verify RED**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*script-controller-textfile-dialog*"
```

Expected: the build succeeds, but both tests fail for the intended lifecycle gap. The first reports that the dialog is not visible; the second reports that `created_without_view.txt` does not exist. Do not change production code until this failure is observed.

### Task 2: Move New Text File ownership into `ScriptEditorController`

**Files:**
- Modify: `editor/script/script_editor_controller.cpp:162-170`
- Modify: `editor/script/script_editor_controller.cpp:588-596`
- Modify: `editor/script/script_editor_view.cpp:778-802`
- Modify: `editor/script/script_editor_view.cpp:891-902`
- Test: `tests/editor/test_script_editor_views.h`

- [ ] **Step 1: Add one controller-local extension reader**

Near the existing `script_path_exists()` helper in `script_editor_controller.cpp`, add:

```cpp
static Vector<String> get_textfile_extensions() {
	return ((String)EDITOR_GET("docks/filesystem/textfile_extensions")).split(",", false);
}
```

This deliberately reads the live editor setting instead of adding a second cached extension set alongside `ScriptEditorView::textfile_extensions`.

- [ ] **Step 2: Handle text-file selections before view-specific dialog routing**

Replace `ScriptEditorController::_on_file_dialog_selected()` with:

```cpp
void ScriptEditorController::_on_file_dialog_selected(const String &p_file) {
	if (file_dialog_option == ScriptEditorView::FILE_MENU_NEW_TEXTFILE) {
		file_dialog_option = -1;

		Error err = OK;
		{
			Ref<FileAccess> file = FileAccess::open(p_file, FileAccess::WRITE, &err);
		}
		if (err != OK) {
			if (EditorNode *editor_node = EditorNode::get_singleton()) {
				editor_node->show_warning(TTR("Error writing TextFile:") + "\n" + p_file, TTR("Error!"));
			}
			return;
		}

		if (EditorFileSystem *editor_file_system = EditorFileSystem::get_singleton()) {
			if (get_textfile_extensions().has(p_file.get_extension())) {
				editor_file_system->update_file(p_file);
			}
		}

		if (EditorNode *editor_node = EditorNode::get_singleton()) {
			editor_node->load_resource(p_file);
		}
		return;
	}

	if (file_dialog_view) {
		file_dialog_view->_file_dialog_action(p_file);
	} else if (focused_view) {
		focused_view->_file_dialog_action(p_file);
	} else if (!views.is_empty()) {
		views[0]->_file_dialog_action(p_file);
	}
}
```

The file handle is scoped so it is closed before `EditorFileSystem` refreshes or `EditorNode` loads the resource.

- [ ] **Step 3: Configure and show the global dialog directly**

Replace `ScriptEditorController::open_text_file_create_dialog()` with:

```cpp
void ScriptEditorController::open_text_file_create_dialog(const String &p_base_path, const String &p_base_name) {
	ERR_FAIL_NULL(file_dialog);

	file_dialog_view = nullptr;
	file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
	file_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	file_dialog_option = ScriptEditorView::FILE_MENU_NEW_TEXTFILE;

	file_dialog->clear_filters();
	for (const String &extension : get_textfile_extensions()) {
		file_dialog->add_filter("*." + extension, extension.to_upper());
	}
	file_dialog->set_title(TTRC("New Text File..."));
	file_dialog->set_current_dir(p_base_path);
	file_dialog->set_current_file(p_base_name);
	file_dialog->popup_file_dialog();
}
```

- [ ] **Step 4: Remove the view-owned file creation branch**

Delete the entire `case FILE_MENU_NEW_TEXTFILE` branch from `ScriptEditorView::_file_dialog_action()`. The controller now consumes that dialog option before any view-specific routing, so leaving the branch would create two sources of truth.

- [ ] **Step 5: Delegate the script editor menu to the controller**

Replace the `FILE_MENU_NEW_TEXTFILE` branch in `ScriptEditorView::_menu_option()` with:

```cpp
		case FILE_MENU_NEW_TEXTFILE: {
			controller->open_text_file_create_dialog(controller->get_file_dialog()->get_current_dir());
		} break;
```

This preserves the script menu's existing current-directory behavior while using the same controller-owned operation as the FileSystem dock.

- [ ] **Step 6: Run the focused cases to verify GREEN**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*script-controller-textfile-dialog*" \
  --case "*focused-view-global-actions*"
```

Expected: all selected cases pass. The new cases prove the no-view path; the existing focused-view case guards controller routing behavior.

- [ ] **Step 7: Inspect the focused diff and commit the implementation**

Run:

```sh
git diff --check
git diff -- editor/script/script_editor_controller.cpp \
  editor/script/script_editor_view.cpp tests/editor/test_script_editor_views.h
git add editor/script/script_editor_controller.cpp \
  editor/script/script_editor_view.cpp tests/editor/test_script_editor_views.h
git commit -m "Fix FileSystem TextFile creation without script views"
```

Expected: `git diff --check` prints nothing; the commit contains only the controller, view, and regression-test changes.

### Task 3: Final strict and editor-facing verification

**Files:**
- Verify only; no source changes expected.

- [ ] **Step 1: Run the required native strict validation**

Run:

```sh
python3 scripts/agent_build.py --test \
  --case "*script-controller-textfile-dialog*" \
  --case "*focused-view-global-actions*"
```

Expected: the native SCons build completes with `dev_mode=yes dev_build=yes tests=yes`, and every selected test passes.

- [ ] **Step 2: Verify the real editor workflow with no script view open**

Launch the editor automation fixture through `foundry_launch_editor` using `tests/fixtures/editor_automation_mvp`. Confirm with `foundry_read_editor_state` that `script.supported` is `false`, then operate the FileSystem dock as follows:

1. Right-click the `res://` directory.
2. Open **Create New**.
3. Select **TextFile...**.
4. Verify the modal stack contains the **New Text File...** save dialog.
5. Verify the workspace still reports no script surface merely from opening the dialog.
6. Save `controller_created.txt` and verify the FileSystem dock reports `res://controller_created.txt`.
7. Delete the temporary file through the FileSystem dock so the fixture remains clean.
8. Call `foundry_poll_events` and confirm there are no new editor errors.
9. Call `foundry_disconnect`.

The current automation API can open the FileSystem context menu but cannot semantically expand/select its nested **Create New** submenu. If that limitation is still present when this plan is executed, record the exact `unsupported_action` result and complete only the nested submenu selection through an explicitly documented local UI fallback; do not claim fully automated end-to-end coverage.

- [ ] **Step 3: Confirm repository hygiene**

Run:

```sh
git status --short
git diff --check HEAD^
```

Expected: no tracked fixture or generated cache changes belong to the implementation commit, and the implementation diff has no whitespace errors. Preserve any unrelated pre-existing worktree changes.

## Completion Criteria

- The New Text File save dialog opens from the FileSystem dock with no active script editor view.
- The dialog starts in the selected FileSystem directory.
- Confirming the dialog creates the file and routes it through the normal editor resource-opening path.
- Opening the dialog does not create or reveal a script workspace.
- The script editor's New Text File menu continues to work through the same controller flow.
- The no-view regression tests and focused controller-routing tests pass under the native strict build.
- Editor-facing verification is recorded, including the known nested-submenu automation limitation if it still applies.
