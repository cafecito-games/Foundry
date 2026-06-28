# Inline Multi-Caret Rename Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Foundry Script modal rename prompt with inline multi-caret rename for safe single-file renames, while preserving the existing modal and diff-preview fallback paths.

**Architecture:** The refactor engine will expose resolved current-file rename occurrence ranges on `RefactorResult`. `ScriptTextEditor` will use those ranges to enter a small inline rename state that configures `CodeEdit` selections, intercepts Enter/Escape, validates final names against the original source, and falls back to the existing modal flow when the rename is not inline-safe.

**Tech Stack:** Godot C++, Foundry Script refactoring/LSP helpers, `CodeEdit`/`TextEdit` multi-caret APIs, doctest-based engine tests in `modules/foundry_script/tests/test_refactor.h`.

---

### Task 1: Expose Rename Occurrence Ranges

**Files:**
- Modify: `modules/foundry_script/editor/gdscript_refactoring.h`
- Modify: `modules/foundry_script/editor/gdscript_refactoring.cpp`
- Test: `modules/foundry_script/tests/test_refactor.h`

- [ ] **Step 1: Write the failing refactor tests**

Add these subcases inside `TEST_CASE("Rename refactor")`, after the existing `"strings and comments untouched"` subcase:

```cpp
		SUBCASE("local variable exposes current-file occurrence ranges for inline rename") {
			String out;
			RefactorResult r = run_rename("res://refactor/rename_local.fs", 3, 5, "total", out); // caret on `total`
			REQUIRE(r.ok);
			REQUIRE_EQ(r.rename_occurrences.size(), 3);

			CHECK_EQ(r.rename_occurrences[0].start_line, 3);
			CHECK_EQ(r.rename_occurrences[0].start_column, 5);
			CHECK_EQ(r.rename_occurrences[0].end_line, 3);
			CHECK_EQ(r.rename_occurrences[0].end_column, 10);
			CHECK_EQ(r.rename_occurrences[0].expected_text, "total");

			CHECK_EQ(r.rename_occurrences[1].start_line, 4);
			CHECK_EQ(r.rename_occurrences[1].start_column, 1);
			CHECK_EQ(r.rename_occurrences[1].end_line, 4);
			CHECK_EQ(r.rename_occurrences[1].end_column, 6);

			CHECK_EQ(r.rename_occurrences[2].start_line, 5);
			CHECK_EQ(r.rename_occurrences[2].start_column, 8);
			CHECK_EQ(r.rename_occurrences[2].end_line, 5);
			CHECK_EQ(r.rename_occurrences[2].end_column, 13);
		}
		SUBCASE("inline rename occurrence ranges ignore strings and comments") {
			String out;
			RefactorResult r = run_rename("res://refactor/rename_strings_comments.fs", 3, 5, "total", out); // caret on `total`
			REQUIRE(r.ok);
			REQUIRE_EQ(r.rename_occurrences.size(), 2);
			for (const RefactorTextEdit &occurrence : r.rename_occurrences) {
				CHECK_EQ(occurrence.expected_text, "total");
				CHECK(occurrence.start_line != 1); // comment line.
				CHECK(occurrence.start_line != 5); // string line.
			}
		}
```

- [ ] **Step 2: Run the focused test to verify it fails**

Run the smallest available compiled test binary if present:

```bash
./bin/godot.macos.editor.dev.* --test --test-case="*Rename refactor*" --force-colors
```

Expected: compile failure or test failure because `RefactorResult::rename_occurrences` does not exist yet. If no binary exists, run the build command from the repository guide first:

```bash
scons platform=macos target=editor dev_build=yes tests=yes
```

- [ ] **Step 3: Add the result field**

In `modules/foundry_script/editor/gdscript_refactoring.h`, extend `RefactorResult` after `unresolved_references`:

```cpp
	Vector<RefactorTextEdit> rename_occurrences;
```

- [ ] **Step 4: Populate current-file occurrences**

In `prepare_rename()`, after creating each `RefactorTextEdit edit` and before leaving the `path == p_context.path` block, append a range-only copy:

```cpp
			if (path == p_context.path) {
				// The script editor applies rename through file_edits, but tests and
				// direct engine consumers still use edits for the active file.
				result.edits.push_back(edit);

				RefactorTextEdit occurrence = edit;
				occurrence.new_text = String();
				result.rename_occurrences.push_back(occurrence);
			}
```

- [ ] **Step 5: Run the focused test to verify it passes**

Run:

```bash
./bin/godot.macos.editor.dev.* --test --test-case="*Rename refactor*" --force-colors
```

Expected: the rename refactor tests pass.

- [ ] **Step 6: Commit**

```bash
git add modules/foundry_script/editor/gdscript_refactoring.h modules/foundry_script/editor/gdscript_refactoring.cpp modules/foundry_script/tests/test_refactor.h
git commit -m "Expose rename occurrence ranges"
```

### Task 2: Add Inline Rename State And Fallback Helpers

**Files:**
- Modify: `editor/script/script_text_editor.h`
- Modify: `editor/script/script_text_editor.cpp`

- [ ] **Step 1: Add editor state declarations**

In `ScriptTextEditor`, add a small state struct and helper declarations near the existing pending refactor fields:

```cpp
	struct InlineRenameCaretState {
		int line = 0;
		int column = 0;
		bool has_selection = false;
		int selection_from_line = 0;
		int selection_from_column = 0;
		int selection_to_line = 0;
		int selection_to_column = 0;
	};

	bool inline_rename_active = false;
	RefactorContext inline_rename_context;
	RefactorLocation inline_rename_location;
	Vector<RefactorTextEdit> inline_rename_occurrences;
	Vector<InlineRenameCaretState> inline_rename_caret_states;
	int inline_rename_primary_line = -1;
	int inline_rename_primary_column = -1;
```

Declare helpers:

```cpp
	bool _try_start_inline_rename(const RefactorContext &p_context, const RefactorLocation &p_location);
	bool _is_inline_rename_safe(const RefactorResult &p_result, const RefactorContext &p_context) const;
	bool _select_inline_rename_occurrences(const Vector<RefactorTextEdit> &p_occurrences);
	void _save_inline_rename_caret_state();
	void _restore_inline_rename_caret_state();
	String _get_inline_rename_name() const;
	void _commit_inline_rename();
	void _cancel_inline_rename(bool p_restore_text = true);
	void _show_rename_dialog();
```

- [ ] **Step 2: Add fallback dialog helper**

Move the existing modal setup from `_run_refactor()` into `_show_rename_dialog()`:

```cpp
void ScriptTextEditor::_show_rename_dialog() {
	rename_line_edit->set_text(code_editor->get_text_editor()->get_word_under_caret());
	_on_rename_text_changed(rename_line_edit->get_text());
	rename_dialog->popup_centered();
	rename_line_edit->grab_focus();
	rename_line_edit->select_all();
}
```

- [ ] **Step 3: Add inline-safe classification**

Implement `_is_inline_rename_safe()` so inline mode only starts for exactly one active-file edit group, no warnings, no unresolved references, and at least one occurrence:

```cpp
bool ScriptTextEditor::_is_inline_rename_safe(const RefactorResult &p_result, const RefactorContext &p_context) const {
	if (!p_result.ok || !p_result.warning.is_empty() || !p_result.unresolved_references.is_empty()) {
		return false;
	}
	if (p_result.file_edits.size() != 1 || p_result.rename_occurrences.is_empty()) {
		return false;
	}
	return p_result.file_edits[0].path == p_context.path;
}
```

- [ ] **Step 4: Add caret save and restore helpers**

Use public `TextEdit` APIs to preserve the user state:

```cpp
void ScriptTextEditor::_save_inline_rename_caret_state() {
	CodeEdit *text_editor = code_editor->get_text_editor();
	inline_rename_caret_states.clear();
	for (int i = 0; i < text_editor->get_caret_count(); i++) {
		InlineRenameCaretState state;
		state.line = text_editor->get_caret_line(i);
		state.column = text_editor->get_caret_column(i);
		state.has_selection = text_editor->has_selection(i);
		if (state.has_selection) {
			state.selection_from_line = text_editor->get_selection_from_line(i);
			state.selection_from_column = text_editor->get_selection_from_column(i);
			state.selection_to_line = text_editor->get_selection_to_line(i);
			state.selection_to_column = text_editor->get_selection_to_column(i);
		}
		inline_rename_caret_states.push_back(state);
	}
}

void ScriptTextEditor::_restore_inline_rename_caret_state() {
	CodeEdit *text_editor = code_editor->get_text_editor();
	text_editor->remove_secondary_carets();
	for (int i = 0; i < inline_rename_caret_states.size(); i++) {
		const InlineRenameCaretState &state = inline_rename_caret_states[i];
		const int caret = i == 0 ? 0 : text_editor->add_caret(state.line, state.column);
		if (caret < 0) {
			continue;
		}
		text_editor->set_caret_line(state.line, false, false, -1, caret);
		text_editor->set_caret_column(state.column, false, caret);
		if (state.has_selection) {
			text_editor->select(state.selection_from_line, state.selection_from_column, state.selection_to_line, state.selection_to_column, caret);
		}
	}
}
```

- [ ] **Step 5: Commit**

```bash
git add editor/script/script_text_editor.h editor/script/script_text_editor.cpp
git commit -m "Add inline rename editor state"
```

### Task 3: Start Inline Rename From F2

**Files:**
- Modify: `editor/script/script_text_editor.cpp`

- [ ] **Step 1: Implement occurrence selection**

Add `_select_inline_rename_occurrences()`:

```cpp
bool ScriptTextEditor::_select_inline_rename_occurrences(const Vector<RefactorTextEdit> &p_occurrences) {
	if (p_occurrences.is_empty()) {
		return false;
	}

	CodeEdit *text_editor = code_editor->get_text_editor();
	text_editor->remove_secondary_carets();
	text_editor->deselect();

	for (int i = 0; i < p_occurrences.size(); i++) {
		const RefactorTextEdit &occurrence = p_occurrences[i];
		const int caret = i == 0 ? 0 : text_editor->add_caret(occurrence.end_line, occurrence.end_column);
		if (caret < 0) {
			text_editor->remove_secondary_carets();
			text_editor->deselect();
			return false;
		}
		text_editor->select(occurrence.start_line, occurrence.start_column, occurrence.end_line, occurrence.end_column, caret);
	}
	return true;
}
```

- [ ] **Step 2: Implement inline start**

Add `_try_start_inline_rename()`:

```cpp
bool ScriptTextEditor::_try_start_inline_rename(const RefactorContext &p_context, const RefactorLocation &p_location) {
	RefactorParams params;
	params.new_name = code_editor->get_text_editor()->get_word_under_caret();

	const RefactorResult result = GDScriptRefactoring::prepare(p_context, p_location, RefactorKind::RENAME, params);
	if (!_is_inline_rename_safe(result, p_context)) {
		return false;
	}

	_save_inline_rename_caret_state();
	if (!_select_inline_rename_occurrences(result.rename_occurrences)) {
		_restore_inline_rename_caret_state();
		return false;
	}

	inline_rename_active = true;
	inline_rename_context = p_context;
	inline_rename_location = p_location;
	inline_rename_occurrences = result.rename_occurrences;
	inline_rename_primary_line = result.rename_occurrences[0].start_line;
	inline_rename_primary_column = result.rename_occurrences[0].start_column;
	code_editor->get_text_editor()->begin_complex_operation();
	return true;
}
```

- [ ] **Step 3: Route F2 through inline start first**

In the rename branch of `_run_refactor()`, replace the direct dialog show with:

```cpp
		if (_try_start_inline_rename(ctx, loc)) {
			return;
		}

		_show_rename_dialog();
		return;
```

- [ ] **Step 4: Build to catch integration errors**

Run:

```bash
scons platform=macos target=editor dev_build=yes tests=yes
```

Expected: the editor target compiles.

- [ ] **Step 5: Commit**

```bash
git add editor/script/script_text_editor.cpp
git commit -m "Start inline rename for local symbols"
```

### Task 4: Commit And Cancel Inline Rename

**Files:**
- Modify: `editor/script/script_text_editor.cpp`

- [ ] **Step 1: Add current-name extraction**

Add `_get_inline_rename_name()`:

```cpp
String ScriptTextEditor::_get_inline_rename_name() const {
	CodeEdit *text_editor = code_editor->get_text_editor();
	if (inline_rename_primary_line < 0 || inline_rename_primary_line >= text_editor->get_line_count()) {
		return String();
	}
	const String line = text_editor->get_line(inline_rename_primary_line);
	const int end_column = text_editor->get_caret_line(0) == inline_rename_primary_line ? text_editor->get_caret_column(0) : inline_rename_primary_column;
	if (end_column < inline_rename_primary_column || end_column > line.length()) {
		return String();
	}
	return line.substr(inline_rename_primary_column, end_column - inline_rename_primary_column);
}
```

- [ ] **Step 2: Add cancel**

Add `_cancel_inline_rename()`:

```cpp
void ScriptTextEditor::_cancel_inline_rename(bool p_restore_text) {
	if (!inline_rename_active) {
		return;
	}

	CodeEdit *text_editor = code_editor->get_text_editor();
	if (p_restore_text) {
		text_editor->set_text(inline_rename_context.source);
	}
	_restore_inline_rename_caret_state();
	text_editor->end_complex_operation();

	inline_rename_active = false;
	inline_rename_context = RefactorContext();
	inline_rename_location = RefactorLocation();
	inline_rename_occurrences.clear();
	inline_rename_caret_states.clear();
	inline_rename_primary_line = -1;
	inline_rename_primary_column = -1;
}
```

- [ ] **Step 3: Add commit**

Add `_commit_inline_rename()`:

```cpp
void ScriptTextEditor::_commit_inline_rename() {
	if (!inline_rename_active) {
		return;
	}

	const String new_name = _get_inline_rename_name();
	String reason;
	if (!GDScriptRefactorNames::validate_identifier(new_name, reason)) {
		EditorToaster::get_singleton()->popup_str(reason, EditorToaster::SEVERITY_ERROR);
		return;
	}

	RefactorParams params;
	params.new_name = new_name;
	const RefactorResult result = GDScriptRefactoring::prepare(inline_rename_context, inline_rename_location, RefactorKind::RENAME, params);
	if (!result.ok) {
		EditorToaster::get_singleton()->popup_str(result.error_message, EditorToaster::SEVERITY_ERROR);
		return;
	}
	if (!_is_inline_rename_safe(result, inline_rename_context)) {
		_cancel_inline_rename(true);
		_show_rename_dialog();
		return;
	}

	CodeEdit *text_editor = code_editor->get_text_editor();
	text_editor->remove_secondary_carets();
	text_editor->deselect();
	text_editor->set_caret_line(result.rename_anchor_line);
	text_editor->set_caret_column(result.rename_anchor_column + new_name.length());
	text_editor->end_complex_operation();

	inline_rename_active = false;
	inline_rename_context = RefactorContext();
	inline_rename_location = RefactorLocation();
	inline_rename_occurrences.clear();
	inline_rename_caret_states.clear();
	inline_rename_primary_line = -1;
	inline_rename_primary_column = -1;
}
```

- [ ] **Step 4: Intercept Enter and Escape**

At the start of `_text_edit_gui_input()`, before context-menu handling:

```cpp
	if (inline_rename_active && k.is_valid() && k->is_pressed() && !k->is_echo()) {
		if (k->get_keycode() == Key::ENTER || k->get_keycode() == Key::KP_ENTER) {
			_commit_inline_rename();
			code_editor->get_text_editor()->accept_event();
			return;
		}
		if (k->get_keycode() == Key::ESCAPE) {
			_cancel_inline_rename(true);
			code_editor->get_text_editor()->accept_event();
			return;
		}
	}
```

- [ ] **Step 5: Run focused build/tests**

Run:

```bash
scons platform=macos target=editor dev_build=yes tests=yes
./bin/godot.macos.editor.dev.* --test --test-case="*Rename refactor*" --force-colors
```

Expected: build succeeds and rename refactor tests pass.

- [ ] **Step 6: Commit**

```bash
git add editor/script/script_text_editor.cpp
git commit -m "Commit and cancel inline rename"
```

### Task 5: Final Verification

**Files:**
- Check all modified files.

- [ ] **Step 1: Inspect changed files**

Run:

```bash
git diff origin/develop...HEAD --stat
git diff origin/develop...HEAD --check
```

Expected: only the design doc, refactor tests/API, and script editor files changed; no whitespace errors.

- [ ] **Step 2: Run formatting hooks for touched files**

Run:

```bash
pre-commit run --files \
  docs/superpowers/specs/2026-06-23-inline-multi-caret-rename-design.md \
  docs/superpowers/plans/2026-06-23-inline-multi-caret-rename.md \
  modules/foundry_script/editor/gdscript_refactoring.h \
  modules/foundry_script/editor/gdscript_refactoring.cpp \
  modules/foundry_script/tests/test_refactor.h \
  editor/script/script_text_editor.h \
  editor/script/script_text_editor.cpp
```

Expected: hooks pass.

- [ ] **Step 3: Run the focused runtime test**

Run:

```bash
./bin/godot.macos.editor.dev.* --test --test-case="*Rename refactor*" --force-colors
```

Expected: rename refactor tests pass.

- [ ] **Step 4: Commit the plan if it is still uncommitted**

```bash
git add docs/superpowers/plans/2026-06-23-inline-multi-caret-rename.md
git commit -m "docs: add inline rename implementation plan"
```
