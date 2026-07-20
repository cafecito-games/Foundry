# Editor New Script Command Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a global `editor/new_script` command (Cmd/Ctrl+Alt+N + command palette + File menu) that opens FileSystem’s ScriptCreateDialog with the current FileSystem directory and opens the created script via the existing reuse-script-leaf path.

**Architecture:** Thin public helper on `FileSystemDock` encapsulates the existing `FILE_MENU_NEW_SCRIPT` dialog config/popup. `EditorNode` registers `ED_SHORTCUT_AND_COMMAND("editor/new_script", …)`, adds a File menu item next to Quick Open Script, and routes the menu option to that helper. Creation/open reuse `push_item` → `open_script_leaf(..., false)`.

**Tech Stack:** C++ editor code, doctest, SCons. Spec: `docs/superpowers/specs/2026-07-20-editor-new-script-command-design.md`.

**Build (macOS):** `scons platform=macos target=editor dev_build=yes tests=yes`  
**Binary:** `bin/foundry.macos.editor.dev.arm64`  
**Focused tests:** `./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*NewScript*" --force-colors`

---

## File map

| File | Responsibility |
|------|----------------|
| `editor/docks/filesystem_dock.h` | Declare `open_script_create_dialog()` |
| `editor/docks/filesystem_dock.cpp` | Implement helper; call it from `FILE_MENU_NEW_SCRIPT` |
| `editor/editor_node.h` | Add `SCENE_NEW_SCRIPT` menu enum |
| `editor/editor_node.cpp` | Shortcut/command, File menu item, menu handler |
| `tests/editor/test_editor_new_script_command.h` | Doctest: helper pops dialog; palette command registered |
| `tests/test_main.cpp` | Include the new test header |

---

### Task 1: FileSystemDock `open_script_create_dialog()` helper

**Files:**
- Modify: `editor/docks/filesystem_dock.h`
- Modify: `editor/docks/filesystem_dock.cpp` (around `FILE_MENU_NEW_SCRIPT` ~2626 and public API near `get_script_create_dialog`)

- [ ] **Step 1: Add public method declaration**

In `filesystem_dock.h`, next to `get_script_create_dialog()`:

```cpp
void open_script_create_dialog();
ScriptCreateDialog *get_script_create_dialog() const;
```

- [ ] **Step 2: Implement and reuse from FILE_MENU_NEW_SCRIPT**

In `filesystem_dock.cpp`:

```cpp
void FileSystemDock::open_script_create_dialog() {
	String fpath = current_path;
	if (!fpath.ends_with("/")) {
		fpath = fpath.get_base_dir();
	}
	make_script_dialog->config("Node", fpath.path_join("new_script.fs"), false);
	make_script_dialog->popup_centered();
}
```

Replace the `FILE_MENU_NEW_SCRIPT` case body with:

```cpp
case FILE_MENU_NEW_SCRIPT: {
	open_script_create_dialog();
} break;
```

- [ ] **Step 3: Commit** (only if the user asked to commit; otherwise leave staged work for the session)

```bash
git add editor/docks/filesystem_dock.h editor/docks/filesystem_dock.cpp
git commit -m "$(cat <<'EOF'
Extract FileSystemDock::open_script_create_dialog helper

EOF
)"
```

---

### Task 2: EditorNode command, shortcut, and File menu

**Files:**
- Modify: `editor/editor_node.h` (`MenuOptions` enum near `SCENE_QUICK_OPEN_SCRIPT`)
- Modify: `editor/editor_node.cpp` (`_build_file_menu`, `_menu_option_confirm` switch, `ED_SHORTCUT_AND_COMMAND` block)

- [ ] **Step 1: Add menu enum**

In `editor/editor_node.h` after `SCENE_QUICK_OPEN_SCRIPT`:

```cpp
SCENE_QUICK_OPEN_SCRIPT,
SCENE_NEW_SCRIPT,
SCENE_EXPORT_AS,
```

- [ ] **Step 2: Register shortcut + command**

In `editor/editor_node.cpp` immediately after `editor/quick_open_script`:

```cpp
ED_SHORTCUT_AND_COMMAND("editor/quick_open_script", TTRC("Quick Open Script..."), KeyModifierMask::CMD_OR_CTRL + KeyModifierMask::ALT + Key::O);
ED_SHORTCUT_AND_COMMAND("editor/new_script", TTRC("New Script..."), KeyModifierMask::CMD_OR_CTRL + KeyModifierMask::ALT + Key::N);
```

- [ ] **Step 3: File menu item**

In `_build_file_menu()`, after Quick Open Script:

```cpp
file_menu->add_shortcut(ED_GET_SHORTCUT("editor/quick_open_script"), SCENE_QUICK_OPEN_SCRIPT);
file_menu->add_shortcut(ED_GET_SHORTCUT("editor/new_script"), SCENE_NEW_SCRIPT);
file_menu->add_separator();
```

- [ ] **Step 4: Menu handler**

In `_menu_option_confirm`, after `SCENE_QUICK_OPEN_SCRIPT`:

```cpp
case SCENE_QUICK_OPEN_SCRIPT: {
	quick_open_dialog->popup_dialog({ "Script" }, callable_mp(this, &EditorNode::_quick_opened));
} break;
case SCENE_NEW_SCRIPT: {
	if (FileSystemDock *fs_dock = FileSystemDock::get_singleton()) {
		fs_dock->open_script_create_dialog();
	}
} break;
```

Ensure `filesystem_dock.h` is already included (it is used elsewhere in this file). Projectless shell already blocks unknown scene options by default — `SCENE_NEW_SCRIPT` stays blocked there (correct).

- [ ] **Step 5: Build**

```bash
scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)
```

Expected: link succeeds.

---

### Task 3: Doctests

**Files:**
- Create: `tests/editor/test_editor_new_script_command.h`
- Modify: `tests/test_main.cpp` (include alphabetically with other `tests/editor/` headers)

- [ ] **Step 1: Write tests**

Create `tests/editor/test_editor_new_script_command.h`:

```cpp
/**************************************************************************/
/*  test_editor_new_script_command.h                                      */
/**************************************************************************/

#pragma once

#include "editor/docks/filesystem_dock.h"
#include "editor/editor_node.h"
#include "editor/script/script_create_dialog.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/settings/editor_settings.h"

#include "tests/test_macros.h"

namespace TestEditorNewScriptCommand {

TEST_CASE("[Editor][NewScript] command is registered on the palette") {
	EditorCommandPalette *palette = EditorCommandPalette::get_singleton();
	REQUIRE(palette != nullptr);

	List<String> actions;
	palette->get_actions_list(&actions);
	bool found = false;
	for (const String &action : actions) {
		if (action == "editor/new_script") {
			found = true;
			break;
		}
	}
	CHECK(found);
}

TEST_CASE("[Editor][NewScript] filesystem helper pops create dialog") {
	FileSystemDock *fs = FileSystemDock::get_singleton();
	REQUIRE(fs != nullptr);
	ScriptCreateDialog *dialog = fs->get_script_create_dialog();
	REQUIRE(dialog != nullptr);

	if (dialog->is_visible()) {
		dialog->hide();
	}

	fs->navigate_to_path("res://");
	fs->open_script_create_dialog();
	CHECK(dialog->is_visible());
	dialog->hide();
}

TEST_CASE("[Editor][NewScript] menu option opens the same dialog") {
	EditorNode *editor = EditorNode::get_singleton();
	REQUIRE(editor != nullptr);
	FileSystemDock *fs = FileSystemDock::get_singleton();
	REQUIRE(fs != nullptr);
	ScriptCreateDialog *dialog = fs->get_script_create_dialog();
	REQUIRE(dialog != nullptr);

	if (dialog->is_visible()) {
		dialog->hide();
	}

	editor->trigger_menu_option(EditorNode::SCENE_NEW_SCRIPT, false);
	CHECK(dialog->is_visible());
	dialog->hide();
}

TEST_CASE("[Editor][NewScript] default shortcut is Ctrl/Cmd+Alt+N") {
	Ref<Shortcut> shortcut = ED_GET_SHORTCUT("editor/new_script");
	REQUIRE(shortcut.is_valid());
	Array events = shortcut->get_events();
	REQUIRE(events.size() >= 1);
	Ref<InputEventKey> key = events[0];
	REQUIRE(key.is_valid());
	CHECK(key->get_keycode() == Key::N);
	CHECK(key->is_alt_pressed());
	CHECK(key->is_command_or_control_pressed());
}

} // namespace TestEditorNewScriptCommand
```

Adjust includes/API if `get_actions_list` or `ED_GET_SHORTCUT` need different headers (`editor/editor_settings.h` / `editor/editor_string_names.h` patterns from neighboring tests). If `get_actions_list` is not public, use the same discovery approach as `tests/editor/test_editor_automation_mcp.h` / `inspector_dock.cpp` (they already list palette actions).

- [ ] **Step 2: Include in test_main.cpp**

Add `#include "tests/editor/test_editor_new_script_command.h"` in alphabetical order among editor tests.

- [ ] **Step 3: Run focused tests**

```bash
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*NewScript*" --force-colors
```

Expected: `[doctest] Status: SUCCESS!` for the NewScript cases. If a case is skipped because singletons are null outside a full editor boot, gate with `if (!FileSystemDock::get_singleton()) { return; }` only after confirming how neighboring editor singleton tests behave — prefer failing loudly if the editor test harness normally provides these singletons (same as MCP palette tests).

- [ ] **Step 4: Manual smoke (optional but recommended)**

```bash
./bin/foundry.macos.editor.dev.arm64 editor open --project <any-project-with-project.foundry>
```

- Command palette → “New Script…” → dialog opens under current FileSystem folder  
- Shortcut Cmd+Option+N (macOS) / Ctrl+Alt+N → same  
- Create → script opens in focused pane, reusing an existing script leaf when one is present  

---

## Spec coverage checklist

| Spec requirement | Task |
|------------------|------|
| `editor/new_script` + Cmd/Ctrl+Alt+N | Task 2 |
| Command palette | Task 2 (`ED_SHORTCUT_AND_COMMAND`) + Task 3 |
| FileSystem current-dir default path | Task 1 |
| Reuse script leaf on create | Existing `push_item` path; no new code |
| File menu near Quick Open Script | Task 2 |
| New Scene / Attach Script unchanged | No edits to those paths |

## Placeholder / consistency self-review

- No TBD steps; APIs named `open_script_create_dialog` / `SCENE_NEW_SCRIPT` / `editor/new_script` consistently.
- Leaf reuse intentionally not reimplemented — relies on existing `FileSystemDock::_script_or_shader_created` → `push_item`.
