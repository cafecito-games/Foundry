# Extract Method Name Prompt Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a script editor prompt for naming GDScript Extract Method results while preserving generated-name behavior for headless and LSP callers.

**Architecture:** The GDScript refactor engine will accept `RefactorParams::new_name` for Extract Method and validate custom names in the same place that builds the edits. A tiny editor-side `ExtractMethodNamePromptModel` will validate pending prompt names through the refactor engine, making accepted, invalid, colliding, and canceled name-entry states testable without constructing `ScriptTextEditor`. `ScriptTextEditor` will own the dialog widgets and use the model before applying the chosen name through the existing refactor apply path.

**Tech Stack:** Godot C++, GDScript refactoring helpers in `modules/gdscript/editor/`, editor script UI in `editor/script/`, doctest engine tests in `modules/gdscript/tests/test_refactor.h` and `tests/editor/`.

---

### Task 1: Add Custom Extract Method Name Support In The Core Refactor API

**Files:**
- Modify: `modules/gdscript/tests/test_refactor.h`
- Modify: `modules/gdscript/editor/gdscript_refactoring.cpp`

- [ ] **Step 1: Add a named Extract Method test helper**

In `modules/gdscript/tests/test_refactor.h`, replace the existing `run_extract_method()` helper with this overload pair:

```cpp
inline RefactorResult run_extract_method_named(const String &p_source, const RefactorLocation &p_location, const String &p_new_name, String &r_out) {
	RefactorContext ctx;
	ctx.path = "user://extract_method_refactor.gd";
	ctx.source = p_source;
	RefactorParams params;
	params.new_name = p_new_name;
	RefactorResult r = GDScriptRefactoring::prepare(ctx, p_location, RefactorKind::EXTRACT_METHOD, params);
	if (r.ok) {
		GDScriptRefactorEdits::apply(ctx.source, r.edits, r_out);
	}
	return r;
}

inline RefactorResult run_extract_method(const String &p_source, const RefactorLocation &p_location, String &r_out) {
	return run_extract_method_named(p_source, p_location, String(), r_out);
}
```

- [ ] **Step 2: Write failing core tests**

In `TEST_CASE("Extract method inserts nearby helper and replaces selected statements")`, add these subcases after `"avoids method name collisions"`:

```cpp
		SUBCASE("uses requested method name") {
			const String source =
					"func run() -> void:\n"
					"\tprint(\"ready\")\n"
					"\tprint(\"done\")\n";
			String out;
			RefactorResult r = run_extract_method_named(source, selection(1, 0, 2, 0), "_print_ready", out);
			REQUIRE(r.ok);
			CHECK_EQ(r.suggested_name, "_print_ready");
			CHECK_EQ(out,
					"func run() -> void:\n"
					"\t_print_ready()\n"
					"\tprint(\"done\")\n"
					"\n"
					"func _print_ready() -> void:\n"
					"\tprint(\"ready\")\n");
		}
		SUBCASE("rejects invalid requested method name") {
			const String source =
					"func run() -> void:\n"
					"\tprint(\"ready\")\n"
					"\tprint(\"done\")\n";
			String out;
			RefactorResult r = run_extract_method_named(source, selection(1, 0, 2, 0), "1bad", out);
			CHECK_FALSE(r.ok);
			CHECK(r.error_message.contains("valid identifier"));
			CHECK(out.is_empty());
		}
		SUBCASE("rejects requested method name collision") {
			const String source =
					"func run() -> void:\n"
					"\tprint(\"ready\")\n"
					"\tprint(\"done\")\n"
					"\n"
					"func existing() -> void:\n"
					"\tpass\n";
			String out;
			RefactorResult r = run_extract_method_named(source, selection(1, 0, 2, 0), "existing", out);
			CHECK_FALSE(r.ok);
			CHECK(r.error_message.contains("already exists"));
			CHECK(out.is_empty());
		}
```

- [ ] **Step 3: Run the focused test to verify it fails**

Run:

```bash
./bin/godot.macos.editor.dev.* --headless --test "[Modules][GDScript][Refactor]" --test-case="*Extract method inserts nearby helper*" --force-colors
```

Expected: FAIL in `"uses requested method name"` because Extract Method still uses `_extracted_method`, and FAIL in the invalid/colliding subcases because custom names are ignored.

If `bin/` is absent, run this build first:

```bash
scons platform=macos target=editor dev_build=yes tests=yes
```

- [ ] **Step 4: Add a name resolver near `make_unique_method_name()`**

In `modules/gdscript/editor/gdscript_refactoring.cpp`, add this helper after `make_unique_method_name()`:

```cpp
String resolve_extract_method_name(const GDScriptParser::ClassNode *p_class, const String &p_requested_name, String &r_disabled_reason) {
	if (p_requested_name.is_empty()) {
		const String generated_name = make_unique_method_name(p_class);
		if (generated_name.is_empty()) {
			r_disabled_reason = "Cannot suggest a safe method name.";
		}
		return generated_name;
	}

	String reason;
	if (!GDScriptRefactorNames::validate_identifier(p_requested_name, reason)) {
		r_disabled_reason = reason;
		return String();
	}

	if (p_class != nullptr && p_class->has_member(StringName(p_requested_name))) {
		r_disabled_reason = vformat("A member named '%s' already exists in this class.", p_requested_name);
		return String();
	}

	r_disabled_reason = String();
	return p_requested_name;
}
```

- [ ] **Step 5: Thread the requested name through Extract Method candidate builders**

In `modules/gdscript/editor/gdscript_refactoring.cpp`, add `const String &p_requested_name` to these function signatures and all call sites:

```cpp
ExtractMethodCandidate build_extract_method_candidate(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		const GDScriptParser::ClassNode *p_class,
		const GDScriptParser::FunctionNode *p_function,
		const GDScriptParser::SuiteNode *p_suite,
		int p_first_statement,
		int p_last_statement,
		const String &p_requested_name)
```

```cpp
bool find_extract_method_in_suite(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		const GDScriptParser::ClassNode *p_class,
		const GDScriptParser::FunctionNode *p_function,
		const GDScriptParser::SuiteNode *p_suite,
		const String &p_requested_name,
		ExtractMethodCandidate &r_candidate)
```

```cpp
bool find_extract_method_in_children(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		const GDScriptParser::ClassNode *p_class,
		const GDScriptParser::FunctionNode *p_function,
		const GDScriptParser::Node *p_statement,
		const String &p_requested_name,
		ExtractMethodCandidate &r_candidate)
```

```cpp
bool find_extract_method_in_function(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		const GDScriptParser::ClassNode *p_class,
		const GDScriptParser::FunctionNode *p_function,
		const String &p_requested_name,
		ExtractMethodCandidate &r_candidate)
```

```cpp
bool find_extract_method_in_class(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		const GDScriptParser::ClassNode *p_class,
		const String &p_requested_name,
		ExtractMethodCandidate &r_candidate)
```

```cpp
ExtractMethodCandidate find_extract_method_candidate_in_tree(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		const GDScriptParser::ClassNode *p_tree,
		const String &p_requested_name)
```

```cpp
ExtractMethodCandidate find_extract_method_candidate_uncached(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		const GDScriptParseResultProvider *p_parse_results,
		const String &p_requested_name)
```

Keep the cached `find_extract_method_candidate()` as the default-name path by passing `String()` into `find_extract_method_candidate_uncached()`.

- [ ] **Step 6: Use the requested name in candidate construction**

Inside `build_extract_method_candidate()`, replace:

```cpp
	const String method_name = make_unique_method_name(p_class);
	if (method_name.is_empty()) {
		candidate.disabled_reason = "Cannot suggest a safe method name.";
		return candidate;
	}
```

with:

```cpp
	String name_error;
	const String method_name = resolve_extract_method_name(p_class, p_requested_name, name_error);
	if (method_name.is_empty()) {
		candidate.disabled_reason = name_error;
		return candidate;
	}
```

- [ ] **Step 7: Use the uncached custom-name path from `prepare_extract_method()`**

Replace `prepare_extract_method()` with:

```cpp
RefactorResult prepare_extract_method(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const RefactorParams &p_params,
		const GDScriptParseResultProvider *p_parse_results) {
	RefactorResult result;

	ExtractMethodCandidate candidate;
	if (p_params.new_name.is_empty()) {
		candidate = find_extract_method_candidate(p_context, p_location, p_parse_results);
	} else if (!p_location.has_selection() || !is_location_ordered(p_location)) {
		candidate.disabled_reason = "Select complete statements to extract.";
	} else {
		const Vector<String> lines = p_context.source.split("\n");
		candidate = find_extract_method_candidate_uncached(p_context, p_location, lines, p_parse_results, p_params.new_name);
	}

	if (!candidate.enabled) {
		result.ok = false;
		result.error_message = candidate.disabled_reason;
		return result;
	}

	result.ok = true;
	result.suggested_name = candidate.suggested_name;
	result.rename_anchor_line = candidate.name_line;
	result.rename_anchor_column = candidate.name_column;
	result.edits.push_back(candidate.replacement_edit);
	result.edits.push_back(candidate.method_edit);
	return result;
}
```

Update the `GDScriptRefactoring::prepare()` dispatch case to call the new signature:

```cpp
		case RefactorKind::EXTRACT_METHOD:
			return prepare_extract_method(p_context, p_location, p_params, parse_result_provider);
```

- [ ] **Step 8: Run the focused core tests to verify they pass**

Run:

```bash
./bin/godot.macos.editor.dev.* --headless --test "[Modules][GDScript][Refactor]" --test-case="*Extract method inserts nearby helper*" --force-colors
```

Expected: all Extract Method subcases in that test case pass.

- [ ] **Step 9: Commit**

```bash
git add modules/gdscript/tests/test_refactor.h modules/gdscript/editor/gdscript_refactoring.cpp
git commit -m "Support custom extract method names"
```

### Task 2: Add A Testable Extract Method Name Prompt Model

**Files:**
- Create: `editor/script/extract_method_name_prompt.h`
- Create: `editor/script/extract_method_name_prompt.cpp`
- Create: `tests/editor/test_extract_method_name_prompt.h`
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Write the editor prompt model tests**

Create `tests/editor/test_extract_method_name_prompt.h`:

```cpp
/**************************************************************************/
/*  test_extract_method_name_prompt.h                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#ifdef TOOLS_ENABLED

#include "editor/script/extract_method_name_prompt.h"
#include "tests/test_macros.h"

namespace TestExtractMethodNamePrompt {

RefactorContext make_extract_context(const String &p_source) {
	RefactorContext ctx;
	ctx.path = "user://extract_method_prompt.gd";
	ctx.source = p_source;
	return ctx;
}

RefactorLocation extract_selection() {
	RefactorLocation loc;
	loc.start_line = 1;
	loc.start_column = 0;
	loc.end_line = 2;
	loc.end_column = 0;
	return loc;
}

TEST_CASE("[Editor][ExtractMethodNamePrompt] Accepts valid names") {
	const String source =
			"func run() -> void:\n"
			"\tprint(\"ready\")\n"
			"\tprint(\"done\")\n";
	ExtractMethodNamePromptModel model;
	model.begin(make_extract_context(source), extract_selection(), "_extracted_method");

	CHECK(model.has_pending_request());
	CHECK(model.is_valid());
	CHECK_EQ(model.get_name(), "_extracted_method");
	CHECK(model.get_error_message().is_empty());

	model.set_name("_print_ready");
	CHECK(model.is_valid());

	String confirmed;
	CHECK(model.confirm(confirmed));
	CHECK_EQ(confirmed, "_print_ready");
}

TEST_CASE("[Editor][ExtractMethodNamePrompt] Rejects invalid identifiers") {
	const String source =
			"func run() -> void:\n"
			"\tprint(\"ready\")\n"
			"\tprint(\"done\")\n";
	ExtractMethodNamePromptModel model;
	model.begin(make_extract_context(source), extract_selection(), "_extracted_method");
	model.set_name("1bad");

	CHECK(model.has_pending_request());
	CHECK_FALSE(model.is_valid());
	CHECK(model.get_error_message().contains("valid identifier"));

	String confirmed;
	CHECK_FALSE(model.confirm(confirmed));
	CHECK(confirmed.is_empty());
}

TEST_CASE("[Editor][ExtractMethodNamePrompt] Rejects member collisions") {
	const String source =
			"func run() -> void:\n"
			"\tprint(\"ready\")\n"
			"\tprint(\"done\")\n"
			"\n"
			"func existing() -> void:\n"
			"\tpass\n";
	ExtractMethodNamePromptModel model;
	model.begin(make_extract_context(source), extract_selection(), "_extracted_method");
	model.set_name("existing");

	CHECK(model.has_pending_request());
	CHECK_FALSE(model.is_valid());
	CHECK(model.get_error_message().contains("already exists"));

	String confirmed;
	CHECK_FALSE(model.confirm(confirmed));
}

TEST_CASE("[Editor][ExtractMethodNamePrompt] Cancel clears pending state") {
	const String source =
			"func run() -> void:\n"
			"\tprint(\"ready\")\n"
			"\tprint(\"done\")\n";
	ExtractMethodNamePromptModel model;
	model.begin(make_extract_context(source), extract_selection(), "_extracted_method");
	REQUIRE(model.has_pending_request());

	model.cancel();

	CHECK_FALSE(model.has_pending_request());
	CHECK_FALSE(model.is_valid());
	CHECK(model.get_name().is_empty());
	CHECK(model.get_error_message().is_empty());

	String confirmed;
	CHECK_FALSE(model.confirm(confirmed));
}

} // namespace TestExtractMethodNamePrompt

#endif // TOOLS_ENABLED
```

In `tests/test_main.cpp`, add this include under the existing editor test includes:

```cpp
#include "tests/editor/test_extract_method_name_prompt.h"
```

- [ ] **Step 2: Run the editor prompt test to verify it fails to compile**

Run:

```bash
./bin/godot.macos.editor.dev.* --headless --test "[Editor][ExtractMethodNamePrompt]" --force-colors
```

Expected: compile failure because `editor/script/extract_method_name_prompt.h` does not exist.

- [ ] **Step 3: Add the prompt model header**

Create `editor/script/extract_method_name_prompt.h`:

```cpp
/**************************************************************************/
/*  extract_method_name_prompt.h                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#ifdef TOOLS_ENABLED

#include "modules/gdscript/editor/gdscript_refactoring.h"

class ExtractMethodNamePromptModel {
	bool pending = false;
	bool valid = false;
	RefactorContext context;
	RefactorLocation location;
	String name;
	String error_message;

	void _validate();

public:
	void begin(const RefactorContext &p_context, const RefactorLocation &p_location, const String &p_suggested_name);
	void set_name(const String &p_name);
	void cancel();
	void clear();

	bool has_pending_request() const;
	bool is_valid() const;
	String get_name() const;
	String get_error_message() const;
	const RefactorLocation &get_location() const;
	bool confirm(String &r_name) const;
};

#endif // TOOLS_ENABLED
```

- [ ] **Step 4: Add the prompt model implementation**

Create `editor/script/extract_method_name_prompt.cpp`:

```cpp
/**************************************************************************/
/*  extract_method_name_prompt.cpp                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "extract_method_name_prompt.h"

#ifdef TOOLS_ENABLED

void ExtractMethodNamePromptModel::_validate() {
	valid = false;
	error_message = String();

	if (!pending) {
		return;
	}

	RefactorParams params;
	params.new_name = name;
	const RefactorResult result = GDScriptRefactoring::prepare(context, location, RefactorKind::EXTRACT_METHOD, params);
	if (!result.ok) {
		error_message = result.error_message;
		return;
	}

	valid = true;
}

void ExtractMethodNamePromptModel::begin(const RefactorContext &p_context, const RefactorLocation &p_location, const String &p_suggested_name) {
	pending = true;
	context = p_context;
	location = p_location;
	name = p_suggested_name;
	_validate();
}

void ExtractMethodNamePromptModel::set_name(const String &p_name) {
	name = p_name;
	_validate();
}

void ExtractMethodNamePromptModel::cancel() {
	clear();
}

void ExtractMethodNamePromptModel::clear() {
	pending = false;
	valid = false;
	context = RefactorContext();
	location = RefactorLocation();
	name = String();
	error_message = String();
}

bool ExtractMethodNamePromptModel::has_pending_request() const {
	return pending;
}

bool ExtractMethodNamePromptModel::is_valid() const {
	return valid;
}

String ExtractMethodNamePromptModel::get_name() const {
	return name;
}

String ExtractMethodNamePromptModel::get_error_message() const {
	return error_message;
}

const RefactorLocation &ExtractMethodNamePromptModel::get_location() const {
	return location;
}

bool ExtractMethodNamePromptModel::confirm(String &r_name) const {
	if (!pending || !valid) {
		return false;
	}
	r_name = name;
	return true;
}

#endif // TOOLS_ENABLED
```

- [ ] **Step 5: Run the editor prompt tests to verify they pass**

Run:

```bash
./bin/godot.macos.editor.dev.* --headless --test "[Editor][ExtractMethodNamePrompt]" --force-colors
```

Expected: all Extract Method name prompt model tests pass.

- [ ] **Step 6: Commit**

```bash
git add editor/script/extract_method_name_prompt.h editor/script/extract_method_name_prompt.cpp tests/editor/test_extract_method_name_prompt.h tests/test_main.cpp
git commit -m "Add extract method name prompt model"
```

### Task 3: Wire The Name Prompt Into ScriptTextEditor

**Files:**
- Modify: `editor/script/script_text_editor.h`
- Modify: `editor/script/script_text_editor.cpp`

- [ ] **Step 1: Add editor state and helper declarations**

In `editor/script/script_text_editor.h`, include the prompt model:

```cpp
#include "extract_method_name_prompt.h"
```

Add fields near the existing rename dialog fields:

```cpp
	ConfirmationDialog *extract_method_dialog = nullptr;
	LineEdit *extract_method_line_edit = nullptr;
	Label *extract_method_error_label = nullptr;
	ExtractMethodNamePromptModel extract_method_name_prompt;
```

Add helper declarations near the rename dialog helpers:

```cpp
	void _show_extract_method_dialog(const RefactorContext &p_context, const RefactorLocation &p_location, const String &p_suggested_name);
	void _on_extract_method_confirmed();
	void _on_extract_method_canceled();
	void _on_extract_method_text_changed(const String &p_text);
```

- [ ] **Step 2: Route Extract Method through the prompt**

In `ScriptTextEditor::_run_refactor()`, after the Rename special-case and before the generic `GDScriptRefactoring::prepare()` call, add:

```cpp
	if (kind == RefactorKind::EXTRACT_METHOD) {
		RefactorParams params;
		const RefactorResult result = GDScriptRefactoring::prepare(ctx, loc, kind, params);
		if (!result.ok) {
			if (!result.error_message.is_empty()) {
				EditorToaster::get_singleton()->popup_str(result.error_message, EditorToaster::SEVERITY_ERROR);
			}
			return;
		}

		_show_extract_method_dialog(ctx, loc, result.suggested_name);
		return;
	}
```

- [ ] **Step 3: Add dialog behavior methods**

Add these methods near `_show_rename_dialog()` in `editor/script/script_text_editor.cpp`:

```cpp
void ScriptTextEditor::_show_extract_method_dialog(const RefactorContext &p_context, const RefactorLocation &p_location, const String &p_suggested_name) {
	extract_method_name_prompt.begin(p_context, p_location, p_suggested_name);
	extract_method_line_edit->set_text(extract_method_name_prompt.get_name());
	_on_extract_method_text_changed(extract_method_line_edit->get_text());
	extract_method_dialog->popup_centered();
	extract_method_line_edit->grab_focus();
	extract_method_line_edit->select_all();
}

void ScriptTextEditor::_on_extract_method_confirmed() {
	String method_name;
	if (!extract_method_name_prompt.confirm(method_name)) {
		return;
	}

	RefactorContext ctx = _make_refactor_context();
	RefactorParams params;
	params.new_name = method_name;
	const RefactorResult result = GDScriptRefactoring::prepare(
			ctx,
			extract_method_name_prompt.get_location(),
			RefactorKind::EXTRACT_METHOD,
			params);
	extract_method_name_prompt.clear();

	if (!result.ok) {
		if (!result.error_message.is_empty()) {
			EditorToaster::get_singleton()->popup_str(result.error_message, EditorToaster::SEVERITY_ERROR);
		}
		return;
	}

	_apply_refactor_result(result, ctx.source);
}

void ScriptTextEditor::_on_extract_method_canceled() {
	extract_method_name_prompt.cancel();
}

void ScriptTextEditor::_on_extract_method_text_changed(const String &p_text) {
	extract_method_name_prompt.set_name(p_text);
	extract_method_error_label->set_text(extract_method_name_prompt.is_valid() ? String() : extract_method_name_prompt.get_error_message());
	extract_method_dialog->get_ok_button()->set_disabled(!extract_method_name_prompt.is_valid());
}
```

- [ ] **Step 4: Build the dialog in the constructor**

In `ScriptTextEditor::ScriptTextEditor()`, after the existing rename dialog setup, add:

```cpp
	extract_method_dialog = memnew(ConfirmationDialog);
	extract_method_dialog->set_title(TTRC("Extract Method"));
	VBoxContainer *extract_method_vbox = memnew(VBoxContainer);
	extract_method_dialog->add_child(extract_method_vbox);
	extract_method_line_edit = memnew(LineEdit);
	extract_method_line_edit->connect(SceneStringName(text_changed), callable_mp(this, &ScriptTextEditor::_on_extract_method_text_changed));
	extract_method_vbox->add_child(extract_method_line_edit);
	extract_method_dialog->register_text_enter(extract_method_line_edit);
	extract_method_error_label = memnew(Label);
	extract_method_vbox->add_child(extract_method_error_label);
	extract_method_dialog->connect(SceneStringName(confirmed), callable_mp(this, &ScriptTextEditor::_on_extract_method_confirmed));
	extract_method_dialog->connect(SNAME("canceled"), callable_mp(this, &ScriptTextEditor::_on_extract_method_canceled));
	add_child(extract_method_dialog);
```

- [ ] **Step 5: Clear pending prompt state when switching resources**

In `ScriptTextEditor::set_edited_resource()`, after `_cancel_inline_rename(true);`, add:

```cpp
	extract_method_name_prompt.clear();
```

- [ ] **Step 6: Build to catch editor integration errors**

Run:

```bash
scons platform=macos target=editor dev_build=yes tests=yes
```

Expected: build succeeds without C++ compile or link errors.

- [ ] **Step 7: Run focused tests**

Run:

```bash
./bin/godot.macos.editor.dev.* --headless --test "[Modules][GDScript][Refactor]" --test-case="*Extract method inserts nearby helper*" --force-colors
./bin/godot.macos.editor.dev.* --headless --test "[Editor][ExtractMethodNamePrompt]" --force-colors
```

Expected: both focused test commands pass.

- [ ] **Step 8: Commit**

```bash
git add editor/script/script_text_editor.h editor/script/script_text_editor.cpp
git commit -m "Prompt for extract method name in script editor"
```

### Task 4: Final Verification

**Files:**
- No additional files.

- [ ] **Step 1: Run the combined targeted verification**

Run:

```bash
./bin/godot.macos.editor.dev.* --headless --test "[Modules][GDScript][Refactor]" --force-colors
./bin/godot.macos.editor.dev.* --headless --test "[Editor][ExtractMethodNamePrompt]" --force-colors
./bin/godot.macos.editor.dev.* --headless --test "[Editor][RefactorDiffPreview]" --force-colors
```

Expected: all three commands pass.

- [ ] **Step 2: Run pre-commit on touched files**

Run:

```bash
pre-commit run --files \
	modules/gdscript/tests/test_refactor.h \
	modules/gdscript/editor/gdscript_refactoring.cpp \
	editor/script/extract_method_name_prompt.h \
	editor/script/extract_method_name_prompt.cpp \
	tests/editor/test_extract_method_name_prompt.h \
	tests/test_main.cpp \
	editor/script/script_text_editor.h \
	editor/script/script_text_editor.cpp
```

Expected: all hooks pass.

- [ ] **Step 3: Inspect final branch state**

Run:

```bash
git status --short --branch
git log --oneline --decorate -5
```

Expected: working tree is clean and the latest commits are the design, plan, core API, prompt model, and script editor wiring commits.
