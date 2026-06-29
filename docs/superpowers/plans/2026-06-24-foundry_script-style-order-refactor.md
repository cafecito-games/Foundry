# Foundry Script Style Order Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Foundry Script refactor named `Sort Members by Style Guide` that reorders class and trait member declarations into style-guide buckets while preserving relative order inside each bucket.

**Architecture:** Extend the existing Foundry Script refactor API with a new `RefactorKind`, reusing `RefactorAvailability`, `RefactorResult`, script-editor application, and LSP code-action resolution. Implement a syntax-first sorter in `gdscript_refactoring.cpp`: parse the current source, collect class/trait member text blocks, classify blocks into stable style buckets, and emit text edits only for spans whose bucket order changes. Attempt analyzer-enriched classification for custom overrides when analysis succeeds, but keep trait and namespace syntax sortable from parser data alone.

**Tech Stack:** Godot Engine C++ (`TOOLS_ENABLED`), `GDScriptParser`, optional `GDScriptAnalyzer`, existing `GDScriptRefactorEdits`, doctest-style C++ tests in `modules/foundry_script/tests/test_refactor.h`, SCons test-enabled editor build.

---

## File Structure

- Modify `modules/foundry_script/editor/gdscript_refactoring.h`
  - Add `RefactorKind::SORT_MEMBERS_BY_STYLE_GUIDE`.
- Modify `modules/foundry_script/editor/gdscript_refactoring.cpp`
  - Add style-order block collection, classification, candidate preparation, availability, and dispatch.
- Modify `editor/script/script_text_editor.h`
  - Add a matching `EDIT_REFACTOR_SORT_MEMBERS_BY_STYLE_GUIDE` enum value.
- Modify `editor/script/script_text_editor.cpp`
  - Add the static assertion for the new enum mapping. Existing generic refactor dispatch handles the action after the enum mapping is correct.
- Modify `modules/foundry_script/language_server/gdscript_text_document.cpp`
  - Map the new refactor to `refactor.rewrite` and make it resolvable.
- Modify `modules/foundry_script/tests/test_refactor.h`
  - Add helper and focused tests for availability, sorting behavior, comments/annotations, fork syntax, nested types, and failure behavior.
- Read-only verification reference: `docs/superpowers/specs/2026-06-24-gdscript-style-order-refactor-design.md`.

## Implementation Notes

- Keep the existing `RefactorKind` and `ScriptTextEditor` enum values in exact order. `ScriptTextEditor::_run_refactor()` maps menu ids back to `RefactorKind` with `(id - EDIT_REFACTOR_RENAME)`.
- The sorter must not alphabetize members. Stable sort by bucket and original index.
- Header declarations remain outside the sort span: `namespace`, `import`, `class_name`, `trait_name`, `extends`, `uses`, and script-level annotations.
- Core sorting must rely on parser data. Run analysis when possible only to identify custom overrides; if analysis fails because trait analysis is not implemented yet, keep syntax-level sorting available and classify unresolved custom overrides as remaining methods.
- Emit `RefactorTextEdit` ranges against full lines. Use `has_expected_text = true` with the original span text to guard stale buffers.

---

### Task 1: Add Public Refactor Kind And Disabled Availability

**Files:**
- Modify: `modules/foundry_script/tests/test_refactor.h`
- Modify: `modules/foundry_script/editor/gdscript_refactoring.h`
- Modify: `modules/foundry_script/editor/gdscript_refactoring.cpp`
- Modify: `editor/script/script_text_editor.h`
- Modify: `editor/script/script_text_editor.cpp`
- Modify: `modules/foundry_script/language_server/gdscript_text_document.cpp`

- [ ] **Step 1: Write the failing availability test**

In `modules/foundry_script/tests/test_refactor.h`, update the first test case from five refactors to six. Replace the size check and add the new final assertions:

```cpp
		Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, caret(0, 0));
		CHECK_EQ(available.size(), 6);
		if (available.size() < 6) {
			return;
		}
		CHECK_EQ(available[0].kind, RefactorKind::RENAME);
		CHECK_FALSE(available[0].enabled);
		CHECK_FALSE(available[0].disabled_reason.is_empty());
		CHECK_EQ(available[1].kind, RefactorKind::EXTRACT_VARIABLE);
		CHECK_FALSE(available[1].enabled);
		CHECK_FALSE(available[1].disabled_reason.is_empty());
		CHECK_EQ(available[2].kind, RefactorKind::EXTRACT_METHOD);
		CHECK_FALSE(available[2].enabled);
		CHECK_FALSE(available[2].disabled_reason.is_empty());
		CHECK_EQ(available[3].kind, RefactorKind::ADD_TYPE_ANNOTATION);
		CHECK_FALSE(available[3].enabled);
		CHECK_FALSE(available[3].disabled_reason.is_empty());
		CHECK_EQ(available[4].kind, RefactorKind::INLINE_VARIABLE);
		CHECK_FALSE(available[4].enabled);
		CHECK_FALSE(available[4].disabled_reason.is_empty());
		CHECK_EQ(available[5].kind, RefactorKind::SORT_MEMBERS_BY_STYLE_GUIDE);
		CHECK_FALSE(available[5].enabled);
		CHECK_FALSE(available[5].disabled_reason.is_empty());
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="*[Modules][Foundry Script][Refactor]*"
```

Expected: compile fails with an error naming `SORT_MEMBERS_BY_STYLE_GUIDE` as a missing `RefactorKind`, or the test binary runs and reports `available.size()` as `5`.

- [ ] **Step 3: Add the enum and disabled stub**

In `modules/foundry_script/editor/gdscript_refactoring.h`, add the new kind at the end of the enum:

```cpp
enum class RefactorKind {
	RENAME,
	EXTRACT_VARIABLE,
	EXTRACT_METHOD,
	ADD_TYPE_ANNOTATION,
	INLINE_VARIABLE,
	SORT_MEMBERS_BY_STYLE_GUIDE,
};
```

In `modules/foundry_script/editor/gdscript_refactoring.cpp`, add this helper before `GDScriptRefactoring::get_available_refactors()`:

```cpp
RefactorResult prepare_sort_members_by_style_guide_stub() {
	RefactorResult result;
	result.ok = false;
	result.error_message = "Members are already sorted by the Foundry Script style guide.";
	return result;
}
```

Then append a disabled availability entry before `return result;` in `GDScriptRefactoring::get_available_refactors()`:

```cpp
	RefactorAvailability sort_members;
	sort_members.kind = RefactorKind::SORT_MEMBERS_BY_STYLE_GUIDE;
	sort_members.title = "Sort Members by Style Guide";
	sort_members.enabled = false;
	sort_members.disabled_reason = "Members are already sorted by the Foundry Script style guide.";
	result.push_back(sort_members);
```

Add a dispatch case in `GDScriptRefactoring::prepare()`:

```cpp
		case RefactorKind::SORT_MEMBERS_BY_STYLE_GUIDE:
			return prepare_sort_members_by_style_guide_stub();
```

- [ ] **Step 4: Wire the editor enum mapping**

In `editor/script/script_text_editor.h`, add the matching enum value immediately after `EDIT_REFACTOR_INLINE_VARIABLE`:

```cpp
		EDIT_REFACTOR_INLINE_VARIABLE,
		EDIT_REFACTOR_SORT_MEMBERS_BY_STYLE_GUIDE,
```

In `editor/script/script_text_editor.cpp`, add the matching static assertion after the existing `INLINE_VARIABLE` assertion:

```cpp
	static_assert(EDIT_REFACTOR_RENAME + (int)RefactorKind::SORT_MEMBERS_BY_STYLE_GUIDE == EDIT_REFACTOR_SORT_MEMBERS_BY_STYLE_GUIDE, "RefactorKind/EDIT_REFACTOR_* mapping mismatch");
```

- [ ] **Step 5: Wire the LSP kind mapping**

In `modules/foundry_script/language_server/gdscript_text_document.cpp`, update `refactor_kind_to_lsp_kind()` so the new refactor maps to `refactor.rewrite`:

```cpp
		case RefactorKind::ADD_TYPE_ANNOTATION:
		case RefactorKind::SORT_MEMBERS_BY_STYLE_GUIDE:
			return "refactor.rewrite";
```

Update `is_resolvable_code_action_kind()` so the new refactor resolves lazily:

```cpp
		case RefactorKind::ADD_TYPE_ANNOTATION:
		case RefactorKind::INLINE_VARIABLE:
		case RefactorKind::SORT_MEMBERS_BY_STYLE_GUIDE:
			return true;
```

No change is needed in `modules/foundry_script/language_server/godot_lsp.h` because `CodeActionOptions` already advertises `refactor.rewrite`.

- [ ] **Step 6: Run the test to verify it passes**

Run:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="*[Modules][Foundry Script][Refactor]*"
```

Expected: the refactor test suite passes, with the new action present and disabled at the trivial location.

- [ ] **Step 7: Commit**

```bash
git add modules/foundry_script/tests/test_refactor.h \
	modules/foundry_script/editor/gdscript_refactoring.h \
	modules/foundry_script/editor/gdscript_refactoring.cpp \
	editor/script/script_text_editor.h \
	editor/script/script_text_editor.cpp \
	modules/foundry_script/language_server/gdscript_text_document.cpp
git commit -m "Add Foundry Script style order refactor surface"
```

---

### Task 2: Implement Stable Root Member Sorting

**Files:**
- Modify: `modules/foundry_script/tests/test_refactor.h`
- Modify: `modules/foundry_script/editor/gdscript_refactoring.cpp`

- [ ] **Step 1: Add a test helper**

In `modules/foundry_script/tests/test_refactor.h`, add this helper after `run_inline_variable()`:

```cpp
inline RefactorResult run_sort_members_by_style_guide(const String &p_source, String &r_out) {
	RefactorContext ctx;
	ctx.path = "user://style_order_refactor.fs";
	ctx.source = p_source;
	RefactorParams params;
	RefactorResult r = GDScriptRefactoring::prepare(ctx, caret(0, 0), RefactorKind::SORT_MEMBERS_BY_STYLE_GUIDE, params);
	if (r.ok) {
		GDScriptRefactorEdits::apply(ctx.source, r.edits, r_out);
	}
	return r;
}
```

- [ ] **Step 2: Write the failing root sort test**

Add this test case near the other non-LSP refactor tests in `TEST_SUITE("[Modules][Foundry Script][Refactor]")`:

```cpp
	TEST_CASE("Sort members by style guide reorders root members stably") {
		const String source =
				"extends Node\n"
				"\n"
				"func beta() -> void:\n"
				"\tpass\n"
				"\n"
				"var second := 2\n"
				"signal changed\n"
				"var first := 1\n"
				"const LIMIT := 10\n"
				"\n"
				"func alpha() -> void:\n"
				"\tpass\n";
		const String expected =
				"extends Node\n"
				"\n"
				"signal changed\n"
				"\n"
				"const LIMIT := 10\n"
				"\n"
				"var second := 2\n"
				"var first := 1\n"
				"\n"
				"func beta() -> void:\n"
				"\tpass\n"
				"func alpha() -> void:\n"
				"\tpass\n";

		String out;
		RefactorResult r = run_sort_members_by_style_guide(source, out);
		REQUIRE(r.ok);
		CHECK_EQ(out, expected);
	}
```

- [ ] **Step 3: Run the test to verify it fails**

Run:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="*Sort members by style guide reorders root members stably*"
```

Expected: FAIL because `prepare_sort_members_by_style_guide_stub()` returns `ok == false`.

- [ ] **Step 4: Add root sorting data structures**

In `modules/foundry_script/editor/gdscript_refactoring.cpp`, replace `prepare_sort_members_by_style_guide_stub()` with these declarations inside the anonymous namespace:

```cpp
enum StyleOrderBucket {
	STYLE_BUCKET_SIGNAL,
	STYLE_BUCKET_ENUM,
	STYLE_BUCKET_CONSTANT,
	STYLE_BUCKET_STATIC_VARIABLE,
	STYLE_BUCKET_EXPORTED_VARIABLE,
	STYLE_BUCKET_PUBLIC_VARIABLE,
	STYLE_BUCKET_PRIVATE_VARIABLE,
	STYLE_BUCKET_ONREADY_PUBLIC_VARIABLE,
	STYLE_BUCKET_ONREADY_PRIVATE_VARIABLE,
	STYLE_BUCKET_STATIC_INIT,
	STYLE_BUCKET_STATIC_METHOD,
	STYLE_BUCKET_BUILTIN_VIRTUAL_METHOD,
	STYLE_BUCKET_CUSTOM_OVERRIDE_METHOD,
	STYLE_BUCKET_PUBLIC_METHOD,
	STYLE_BUCKET_PRIVATE_METHOD,
	STYLE_BUCKET_INNER_TYPE,
};

struct StyleOrderBlock {
	int original_index = 0;
	StyleOrderBucket bucket = STYLE_BUCKET_PUBLIC_METHOD;
	int start_line = 0;
	int end_line = 0; // Exclusive.
	String text;
};

struct StyleOrderCandidate {
	bool enabled = false;
	String disabled_reason;
	Vector<RefactorTextEdit> edits;
};

struct StyleOrderBlockComparator {
	bool operator()(const StyleOrderBlock &p_left, const StyleOrderBlock &p_right) const {
		if (p_left.bucket != p_right.bucket) {
			return p_left.bucket < p_right.bucket;
		}
		return p_left.original_index < p_right.original_index;
	}
};
```

- [ ] **Step 5: Add line-span helpers**

Still in `modules/foundry_script/editor/gdscript_refactoring.cpp`, add these helpers near the style-order structs:

```cpp
String get_line_span_text(const Vector<String> &p_lines, int p_start_line, int p_end_line) {
	String text;
	for (int i = p_start_line; i < p_end_line; i++) {
		if (i > p_start_line) {
			text += "\n";
		}
		text += p_lines[i];
	}
	return text;
}

RefactorTextEdit make_line_span_edit(const Vector<String> &p_lines, int p_start_line, int p_end_line, const String &p_new_text) {
	RefactorTextEdit edit;
	edit.start_line = p_start_line;
	edit.start_column = 0;
	if (p_end_line < p_lines.size()) {
		edit.end_line = p_end_line;
		edit.end_column = 0;
	} else {
		edit.end_line = p_lines.size() - 1;
		edit.end_column = p_lines[p_lines.size() - 1].length();
	}
	edit.has_expected_text = true;
	edit.expected_text = get_line_span_text(p_lines, edit.start_line, edit.end_line);
	if (edit.end_column > 0) {
		if (!edit.expected_text.is_empty()) {
			edit.expected_text += "\n";
		}
		edit.expected_text += p_lines[edit.end_line].substr(0, edit.end_column);
	}
	edit.new_text = p_new_text;
	return edit;
}

String normalize_block_text(const String &p_text) {
	String text = p_text;
	while (text.ends_with("\n\n")) {
		text = text.substr(0, text.length() - 1);
	}
	return text.strip_edges(false, true);
}

String join_style_order_blocks(const Vector<StyleOrderBlock> &p_blocks) {
	String text;
	for (int i = 0; i < p_blocks.size(); i++) {
		if (i > 0) {
			text += p_blocks[i - 1].bucket == p_blocks[i].bucket ? "\n" : "\n\n";
		}
		text += normalize_block_text(p_blocks[i].text);
	}
	return text;
}
```

- [ ] **Step 6: Add basic member classification**

Add this helper after the line-span helpers:

```cpp
StyleOrderBucket get_basic_style_order_bucket(const GDScriptParser::ClassNode::Member &p_member) {
	switch (p_member.type) {
		case GDScriptParser::ClassNode::Member::SIGNAL:
			return STYLE_BUCKET_SIGNAL;
		case GDScriptParser::ClassNode::Member::ENUM:
		case GDScriptParser::ClassNode::Member::ENUM_VALUE:
			return STYLE_BUCKET_ENUM;
		case GDScriptParser::ClassNode::Member::CONSTANT:
			return STYLE_BUCKET_CONSTANT;
		case GDScriptParser::ClassNode::Member::VARIABLE:
			return STYLE_BUCKET_PUBLIC_VARIABLE;
		case GDScriptParser::ClassNode::Member::FUNCTION:
			return STYLE_BUCKET_PUBLIC_METHOD;
		case GDScriptParser::ClassNode::Member::CLASS:
			return STYLE_BUCKET_INNER_TYPE;
		case GDScriptParser::ClassNode::Member::GROUP:
			return STYLE_BUCKET_EXPORTED_VARIABLE;
		case GDScriptParser::ClassNode::Member::UNDEFINED:
			break;
	}
	return STYLE_BUCKET_PUBLIC_METHOD;
}
```

- [ ] **Step 7: Add root class edit construction**

Add these helpers after classification:

```cpp
bool is_style_order_sortable_member(const GDScriptParser::ClassNode::Member &p_member) {
	switch (p_member.type) {
		case GDScriptParser::ClassNode::Member::SIGNAL:
		case GDScriptParser::ClassNode::Member::ENUM:
		case GDScriptParser::ClassNode::Member::ENUM_VALUE:
		case GDScriptParser::ClassNode::Member::CONSTANT:
		case GDScriptParser::ClassNode::Member::VARIABLE:
		case GDScriptParser::ClassNode::Member::FUNCTION:
		case GDScriptParser::ClassNode::Member::CLASS:
		case GDScriptParser::ClassNode::Member::GROUP:
			return true;
		case GDScriptParser::ClassNode::Member::UNDEFINED:
			return false;
	}
	return false;
}

bool make_style_order_edit_for_class(
		const Vector<String> &p_lines,
		const GDScriptParser::ClassNode *p_class,
		RefactorTextEdit &r_edit,
		String &r_disabled_reason) {
	Vector<StyleOrderBlock> blocks;
	for (int i = 0; i < p_class->members.size(); i++) {
		const GDScriptParser::ClassNode::Member &member = p_class->members[i];
		if (!is_style_order_sortable_member(member)) {
			continue;
		}
		const int start_line = member.get_line() - 1;
		if (start_line < 0 || start_line >= p_lines.size()) {
			r_disabled_reason = "Cannot map a member declaration back to source text.";
			return false;
		}

		StyleOrderBlock block;
		block.original_index = blocks.size();
		block.bucket = get_basic_style_order_bucket(member);
		block.start_line = start_line;
		blocks.push_back(block);
	}

	if (blocks.size() < 2) {
		r_disabled_reason = "Members are already sorted by the Foundry Script style guide.";
		return false;
	}

	for (int i = 0; i < blocks.size(); i++) {
		const int end_line = i + 1 < blocks.size() ? blocks[i + 1].start_line : p_lines.size();
		blocks.write[i].end_line = end_line;
		blocks.write[i].text = get_line_span_text(p_lines, blocks[i].start_line, end_line);
	}

	Vector<StyleOrderBlock> sorted = blocks;
	sorted.sort_custom<StyleOrderBlockComparator>();

	bool changed = false;
	for (int i = 0; i < blocks.size(); i++) {
		if (blocks[i].original_index != sorted[i].original_index) {
			changed = true;
			break;
		}
	}
	if (!changed) {
		r_disabled_reason = "Members are already sorted by the Foundry Script style guide.";
		return false;
	}

	const int edit_start_line = blocks[0].start_line;
	const int edit_end_line = blocks[blocks.size() - 1].end_line;
	r_edit = make_line_span_edit(p_lines, edit_start_line, edit_end_line, join_style_order_blocks(sorted));
	return true;
}
```

- [ ] **Step 8: Add parsing and candidate preparation**

Add these helpers after `make_style_order_edit_for_class()`:

```cpp
StyleOrderCandidate find_style_order_candidate(const RefactorContext &p_context) {
	StyleOrderCandidate candidate;
	const Vector<String> lines = p_context.source.split("\n");
	if (lines.is_empty()) {
		candidate.disabled_reason = "Members are already sorted by the Foundry Script style guide.";
		return candidate;
	}

	GDScriptParser parser;
	const Error err = parser.parse(p_context.source, p_context.path, false);
	if (err != OK) {
		candidate.disabled_reason = "Cannot parse this script.";
		return candidate;
	}

	RefactorTextEdit edit;
	String disabled_reason;
	if (!make_style_order_edit_for_class(lines, parser.get_tree(), edit, disabled_reason)) {
		candidate.disabled_reason = disabled_reason;
		return candidate;
	}

	candidate.enabled = true;
	candidate.edits.push_back(edit);
	return candidate;
}

RefactorResult prepare_sort_members_by_style_guide(const RefactorContext &p_context) {
	RefactorResult result;
	const StyleOrderCandidate candidate = find_style_order_candidate(p_context);
	if (!candidate.enabled) {
		result.ok = false;
		result.error_message = candidate.disabled_reason;
		return result;
	}
	result.ok = true;
	result.edits = candidate.edits;
	return result;
}
```

Replace the previous `prepare_sort_members_by_style_guide_stub()` dispatch case with:

```cpp
		case RefactorKind::SORT_MEMBERS_BY_STYLE_GUIDE:
			return prepare_sort_members_by_style_guide(p_context);
```

Update `GDScriptRefactoring::get_available_refactors()` so the new availability uses the candidate:

```cpp
	RefactorAvailability sort_members;
	sort_members.kind = RefactorKind::SORT_MEMBERS_BY_STYLE_GUIDE;
	sort_members.title = "Sort Members by Style Guide";
	const StyleOrderCandidate style_order_candidate = find_style_order_candidate(p_context);
	sort_members.enabled = style_order_candidate.enabled;
	if (!sort_members.enabled) {
		sort_members.disabled_reason = style_order_candidate.disabled_reason;
	}
	result.push_back(sort_members);
```

- [ ] **Step 9: Run the root sort test to verify it passes**

Run:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="*Sort members by style guide reorders root members stably*"
```

Expected: PASS.

- [ ] **Step 10: Run the full refactor test suite**

Run:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="*[Modules][Foundry Script][Refactor]*"
```

Expected: PASS.

- [ ] **Step 11: Commit**

```bash
git add modules/foundry_script/tests/test_refactor.h modules/foundry_script/editor/gdscript_refactoring.cpp
git commit -m "Implement stable Foundry Script member sorting"
```

---

### Task 3: Complete Style Bucket Classification

**Files:**
- Modify: `modules/foundry_script/tests/test_refactor.h`
- Modify: `modules/foundry_script/editor/gdscript_refactoring.cpp`

- [ ] **Step 1: Write the failing bucket classification test**

Add this test after the root sort test:

```cpp
	TEST_CASE("Sort members by style guide classifies all supported member buckets") {
		const String source =
				"extends Node\n"
				"\n"
				"class Inner:\n"
				"\tpass\n"
				"func public_method() -> void:\n"
				"\tpass\n"
				"func _private_method() -> void:\n"
				"\tpass\n"
				"func _ready() -> void:\n"
				"\tpass\n"
				"static func helper() -> void:\n"
				"\tpass\n"
				"static func _static_init() -> void:\n"
				"\tpass\n"
				"@onready var node := Node.new()\n"
				"var _private_value := 2\n"
				"var public_value := 1\n"
				"@export var exported_value := 3\n"
				"static var shared_value := 4\n"
				"const LIMIT := 10\n"
				"enum State { IDLE, RUNNING }\n"
				"signal changed\n";
		const String expected =
				"extends Node\n"
				"\n"
				"signal changed\n"
				"\n"
				"enum State { IDLE, RUNNING }\n"
				"\n"
				"const LIMIT := 10\n"
				"\n"
				"static var shared_value := 4\n"
				"\n"
				"@export var exported_value := 3\n"
				"\n"
				"var public_value := 1\n"
				"\n"
				"var _private_value := 2\n"
				"\n"
				"@onready var node := Node.new()\n"
				"\n"
				"static func _static_init() -> void:\n"
				"\tpass\n"
				"static func helper() -> void:\n"
				"\tpass\n"
				"func _ready() -> void:\n"
				"\tpass\n"
				"func public_method() -> void:\n"
				"\tpass\n"
				"func _private_method() -> void:\n"
				"\tpass\n"
				"class Inner:\n"
				"\tpass\n";

		String out;
		RefactorResult r = run_sort_members_by_style_guide(source, out);
		REQUIRE(r.ok);
		CHECK_EQ(out, expected);
	}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="*Sort members by style guide classifies all supported member buckets*"
```

Expected: FAIL because variables and methods are not classified into their detailed buckets yet.

- [ ] **Step 3: Add member-name helpers and built-in callback ordering**

In `modules/foundry_script/editor/gdscript_refactoring.cpp`, add these helpers near `get_basic_style_order_bucket()`:

```cpp
String style_order_member_name(const GDScriptParser::ClassNode::Member &p_member) {
	switch (p_member.type) {
		case GDScriptParser::ClassNode::Member::CLASS:
			return p_member.m_class != nullptr && p_member.m_class->identifier != nullptr ? String(p_member.m_class->identifier->name) : String();
		case GDScriptParser::ClassNode::Member::CONSTANT:
			return p_member.constant != nullptr && p_member.constant->identifier != nullptr ? String(p_member.constant->identifier->name) : String();
		case GDScriptParser::ClassNode::Member::FUNCTION:
			return p_member.function != nullptr && p_member.function->identifier != nullptr ? String(p_member.function->identifier->name) : String();
		case GDScriptParser::ClassNode::Member::SIGNAL:
			return p_member.signal != nullptr && p_member.signal->identifier != nullptr ? String(p_member.signal->identifier->name) : String();
		case GDScriptParser::ClassNode::Member::VARIABLE:
			return p_member.variable != nullptr && p_member.variable->identifier != nullptr ? String(p_member.variable->identifier->name) : String();
		case GDScriptParser::ClassNode::Member::ENUM:
			return p_member.m_enum != nullptr && p_member.m_enum->identifier != nullptr ? String(p_member.m_enum->identifier->name) : String();
		case GDScriptParser::ClassNode::Member::ENUM_VALUE:
			return p_member.enum_value.identifier != nullptr ? String(p_member.enum_value.identifier->name) : String();
		case GDScriptParser::ClassNode::Member::GROUP:
			return p_member.annotation != nullptr ? String(p_member.annotation->name) : String();
		case GDScriptParser::ClassNode::Member::UNDEFINED:
			break;
	}
	return String();
}

bool is_private_style_order_name(const String &p_name) {
	return p_name.begins_with("_");
}

bool is_builtin_virtual_callback_name(const String &p_name) {
	return p_name == "_init" ||
			p_name == "_enter_tree" ||
			p_name == "_ready" ||
			p_name == "_process" ||
			p_name == "_physics_process" ||
			p_name == "_exit_tree" ||
			p_name == "_input" ||
			p_name == "_unhandled_input" ||
			p_name == "_unhandled_key_input" ||
			p_name == "_notification";
}
```

- [ ] **Step 4: Replace basic classification with full direct classification**

Replace `get_basic_style_order_bucket()` with:

```cpp
StyleOrderBucket get_style_order_bucket(const GDScriptParser::ClassNode::Member &p_member) {
	const String member_name = style_order_member_name(p_member);
	switch (p_member.type) {
		case GDScriptParser::ClassNode::Member::SIGNAL:
			return STYLE_BUCKET_SIGNAL;
		case GDScriptParser::ClassNode::Member::ENUM:
		case GDScriptParser::ClassNode::Member::ENUM_VALUE:
			return STYLE_BUCKET_ENUM;
		case GDScriptParser::ClassNode::Member::CONSTANT:
			return STYLE_BUCKET_CONSTANT;
		case GDScriptParser::ClassNode::Member::VARIABLE:
			if (p_member.variable != nullptr && p_member.variable->is_static) {
				return STYLE_BUCKET_STATIC_VARIABLE;
			}
			if (p_member.variable != nullptr && p_member.variable->exported) {
				return STYLE_BUCKET_EXPORTED_VARIABLE;
			}
			if (p_member.variable != nullptr && p_member.variable->onready) {
				return is_private_style_order_name(member_name) ? STYLE_BUCKET_ONREADY_PRIVATE_VARIABLE : STYLE_BUCKET_ONREADY_PUBLIC_VARIABLE;
			}
			return is_private_style_order_name(member_name) ? STYLE_BUCKET_PRIVATE_VARIABLE : STYLE_BUCKET_PUBLIC_VARIABLE;
		case GDScriptParser::ClassNode::Member::FUNCTION:
			if (p_member.function != nullptr && p_member.function->is_static && member_name == "_static_init") {
				return STYLE_BUCKET_STATIC_INIT;
			}
			if (p_member.function != nullptr && p_member.function->is_static) {
				return STYLE_BUCKET_STATIC_METHOD;
			}
			if (is_builtin_virtual_callback_name(member_name)) {
				return STYLE_BUCKET_BUILTIN_VIRTUAL_METHOD;
			}
			return is_private_style_order_name(member_name) ? STYLE_BUCKET_PRIVATE_METHOD : STYLE_BUCKET_PUBLIC_METHOD;
		case GDScriptParser::ClassNode::Member::CLASS:
			return STYLE_BUCKET_INNER_TYPE;
		case GDScriptParser::ClassNode::Member::GROUP:
			return STYLE_BUCKET_EXPORTED_VARIABLE;
		case GDScriptParser::ClassNode::Member::UNDEFINED:
			break;
	}
	return STYLE_BUCKET_PUBLIC_METHOD;
}
```

Then update the block construction call:

```cpp
		block.bucket = get_style_order_bucket(member);
```

- [ ] **Step 5: Run the bucket classification test to verify it passes**

Run:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="*Sort members by style guide classifies all supported member buckets*"
```

Expected: PASS.

- [ ] **Step 6: Run existing refactor tests**

Run:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="*[Modules][Foundry Script][Refactor]*"
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add modules/foundry_script/tests/test_refactor.h modules/foundry_script/editor/gdscript_refactoring.cpp
git commit -m "Classify Foundry Script style order buckets"
```

---

### Task 4: Preserve Attached Comments, Annotations, And Export Groups

**Files:**
- Modify: `modules/foundry_script/tests/test_refactor.h`
- Modify: `modules/foundry_script/editor/gdscript_refactoring.cpp`

- [ ] **Step 1: Write the failing attachment test**

Add this test after the bucket classification test:

```cpp
	TEST_CASE("Sort members by style guide moves attached comments and annotations with declarations") {
		const String source =
				"extends Node\n"
				"\n"
				"# Handles the ready callback.\n"
				"func _ready() -> void:\n"
				"\tpass\n"
				"\n"
				"@export_group(\"Stats\")\n"
				"## Health points shown in the inspector.\n"
				"@export var health := 10\n"
				"\n"
				"# Emitted after health changes.\n"
				"signal health_changed\n";
		const String expected =
				"extends Node\n"
				"\n"
				"# Emitted after health changes.\n"
				"signal health_changed\n"
				"\n"
				"@export_group(\"Stats\")\n"
				"## Health points shown in the inspector.\n"
				"@export var health := 10\n"
				"\n"
				"# Handles the ready callback.\n"
				"func _ready() -> void:\n"
				"\tpass\n";

		String out;
		RefactorResult r = run_sort_members_by_style_guide(source, out);
		REQUIRE(r.ok);
		CHECK_EQ(out, expected);
	}
```

- [ ] **Step 2: Run the attachment test to verify it fails**

Run:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="*Sort members by style guide moves attached comments and annotations with declarations*"
```

Expected: FAIL because block start lines do not expand to attached comments and annotations.

- [ ] **Step 3: Add attached block start detection**

In `modules/foundry_script/editor/gdscript_refactoring.cpp`, add this helper near the line-span helpers:

```cpp
bool is_annotation_or_doc_comment_line(const String &p_line) {
	const String stripped = p_line.strip_edges();
	return stripped.begins_with("@") || stripped.begins_with("##");
}

bool is_ordinary_comment_line(const String &p_line) {
	const String stripped = p_line.strip_edges();
	return stripped.begins_with("#") && !stripped.begins_with("##");
}

int find_attached_style_order_block_start(const Vector<String> &p_lines, int p_member_start_line, int p_floor_line) {
	int start_line = p_member_start_line;

	while (start_line > p_floor_line && is_annotation_or_doc_comment_line(p_lines[start_line - 1])) {
		start_line--;
	}

	int comment_start = start_line;
	while (comment_start > p_floor_line && is_ordinary_comment_line(p_lines[comment_start - 1])) {
		comment_start--;
	}
	if (comment_start < start_line) {
		if (comment_start == p_floor_line || p_lines[comment_start - 1].strip_edges().is_empty()) {
			start_line = comment_start;
		}
	}

	return start_line;
}
```

- [ ] **Step 4: Use attached starts while building blocks**

In `make_style_order_edit_for_class()`, track the end of the previous member block and use the new helper:

```cpp
	int previous_block_floor = 0;
	for (int i = 0; i < p_class->members.size(); i++) {
		const GDScriptParser::ClassNode::Member &member = p_class->members[i];
		if (!is_style_order_sortable_member(member)) {
			continue;
		}
		const int member_start_line = member.get_line() - 1;
		if (member_start_line < 0 || member_start_line >= p_lines.size()) {
			r_disabled_reason = "Cannot map a member declaration back to source text.";
			return false;
		}

		StyleOrderBlock block;
		block.original_index = blocks.size();
		block.bucket = get_style_order_bucket(member);
		block.start_line = find_attached_style_order_block_start(p_lines, member_start_line, previous_block_floor);
		previous_block_floor = member_start_line + 1;
		blocks.push_back(block);
	}
```

- [ ] **Step 5: Run the attachment test to verify it passes**

Run:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="*Sort members by style guide moves attached comments and annotations with declarations*"
```

Expected: PASS.

- [ ] **Step 6: Add an ambiguous export group test**

Add this test after the attachment test:

```cpp
	TEST_CASE("Sort members by style guide declines ambiguous export groups") {
		const String source =
				"extends Node\n"
				"\n"
				"func later() -> void:\n"
				"\tpass\n"
				"\n"
				"@export_group(\"Stats\")\n"
				"func unrelated() -> void:\n"
				"\tpass\n"
				"\n"
				"signal changed\n";

		String out;
		RefactorResult r = run_sort_members_by_style_guide(source, out);
		CHECK_FALSE(r.ok);
		CHECK(r.error_message.to_lower().contains("export group"));
	}
```

- [ ] **Step 7: Run the ambiguous export group test to verify it fails**

Run:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="*Sort members by style guide declines ambiguous export groups*"
```

Expected: FAIL because group members are currently sorted as exported-variable blocks without checking the following declaration.

- [ ] **Step 8: Add export group validation**

Add this helper near `is_style_order_sortable_member()`:

```cpp
bool style_order_group_has_exported_variable_target(const GDScriptParser::ClassNode *p_class, int p_member_index) {
	for (int i = p_member_index + 1; i < p_class->members.size(); i++) {
		const GDScriptParser::ClassNode::Member &member = p_class->members[i];
		if (member.type == GDScriptParser::ClassNode::Member::GROUP) {
			continue;
		}
		return member.type == GDScriptParser::ClassNode::Member::VARIABLE &&
				member.variable != nullptr &&
				member.variable->exported;
	}
	return false;
}
```

Inside the member loop in `make_style_order_edit_for_class()`, before building the block, add:

```cpp
		if (member.type == GDScriptParser::ClassNode::Member::GROUP &&
				!style_order_group_has_exported_variable_target(p_class, i)) {
			r_disabled_reason = "Cannot sort members because an export group is not followed by an exported variable.";
			return false;
		}
```

- [ ] **Step 9: Run attachment and export group tests**

Run:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="*Sort members by style guide moves attached comments and annotations with declarations*"
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="*Sort members by style guide declines ambiguous export groups*"
```

Expected: both tests pass.

- [ ] **Step 10: Run full refactor tests**

Run:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="*[Modules][Foundry Script][Refactor]*"
```

Expected: PASS.

- [ ] **Step 11: Commit**

```bash
git add modules/foundry_script/tests/test_refactor.h modules/foundry_script/editor/gdscript_refactoring.cpp
git commit -m "Preserve Foundry Script style order member blocks"
```

---

### Task 5: Support Nested Classes, Inline Traits, And Fork Header Syntax

**Files:**
- Modify: `modules/foundry_script/tests/test_refactor.h`
- Modify: `modules/foundry_script/editor/gdscript_refactoring.cpp`

- [ ] **Step 1: Write the failing fork syntax and nested-type test**

Add this test after the export group test:

```cpp
	TEST_CASE("Sort members by style guide preserves fork headers and sorts nested types") {
		const String source =
				"@tool\n"
				"namespace game.characters\n"
				"import game.shared\n"
				"import game.ui\n"
				"class_name Player\n"
				"extends Node\n"
				"uses Damageable\n"
				"\n"
				"signal spawned\n"
				"\n"
				"func use_player() -> void:\n"
				"\tpass\n"
				"class Inventory:\n"
				"\tfunc build() -> void:\n"
				"\t\tpass\n"
				"\tvar slots := 8\n"
				"\tsignal changed\n"
				"\n"
				"trait Damageable:\n"
				"\tfunc apply_damage() -> void:\n"
				"\t\tpass\n"
				"\tvar health := 10\n"
				"\tsignal damaged\n"
				"\n";
		const String expected =
				"@tool\n"
				"namespace game.characters\n"
				"import game.shared\n"
				"import game.ui\n"
				"class_name Player\n"
				"extends Node\n"
				"uses Damageable\n"
				"\n"
				"signal spawned\n"
				"\n"
				"func use_player() -> void:\n"
				"\tpass\n"
				"class Inventory:\n"
				"\tsignal changed\n"
				"\n"
				"\tvar slots := 8\n"
				"\n"
				"\tfunc build() -> void:\n"
				"\t\tpass\n"
				"trait Damageable:\n"
				"\tsignal damaged\n"
				"\n"
				"\tvar health := 10\n"
				"\n"
				"\tfunc apply_damage() -> void:\n"
				"\t\tpass\n";

		String out;
		RefactorResult r = run_sort_members_by_style_guide(source, out);
		REQUIRE(r.ok);
		CHECK_EQ(out, expected);
	}
```

- [ ] **Step 2: Run the fork syntax test to verify it fails**

Run:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="*Sort members by style guide preserves fork headers and sorts nested types*"
```

Expected: FAIL because nested class and inline trait bodies are not sorted, and trait analysis may report an error if the sorter requires full analysis.

- [ ] **Step 3: Keep style sorting parser-first**

In `find_style_order_candidate()`, keep the existing `GDScriptParser::parse()` call and do not require `GDScriptAnalyzer::analyze()` for syntax-level sorting. This task must not add an analyzer dependency because `trait` and `uses` syntax can parse before full trait analysis is available.

Later custom override detection must only use analyzer-populated data when analysis succeeds.

- [ ] **Step 4: Add recursive class edit collection**

Replace the single root edit construction in `find_style_order_candidate()` with a recursive collector. Add these helpers near `make_style_order_edit_for_class()`:

```cpp
void collect_style_order_class_edits(
		const Vector<String> &p_lines,
		const GDScriptParser::ClassNode *p_class,
		Vector<RefactorTextEdit> &r_edits,
		String &r_disabled_reason) {
	if (p_class == nullptr || !r_disabled_reason.is_empty()) {
		return;
	}

	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		if (member.type == GDScriptParser::ClassNode::Member::CLASS) {
			collect_style_order_class_edits(p_lines, member.m_class, r_edits, r_disabled_reason);
		}
	}

	RefactorTextEdit edit;
	String class_disabled_reason;
	if (make_style_order_edit_for_class(p_lines, p_class, edit, class_disabled_reason)) {
		r_edits.push_back(edit);
	} else if (class_disabled_reason.contains("export group")) {
		r_disabled_reason = class_disabled_reason;
	}
}
```

Then update `find_style_order_candidate()`:

```cpp
	Vector<RefactorTextEdit> edits;
	String disabled_reason;
	collect_style_order_class_edits(lines, parser.get_tree(), edits, disabled_reason);
	if (!disabled_reason.is_empty()) {
		candidate.disabled_reason = disabled_reason;
		return candidate;
	}
	if (edits.is_empty()) {
		candidate.disabled_reason = "Members are already sorted by the Foundry Script style guide.";
		return candidate;
	}

	candidate.enabled = true;
	candidate.edits = edits;
	return candidate;
```

- [ ] **Step 5: Avoid overlapping parent and child edits**

Before assigning `candidate.edits = edits`, filter child edits when a parent edit contains them. Add this helper near the recursive collector:

```cpp
bool style_order_edit_contains(const RefactorTextEdit &p_parent, const RefactorTextEdit &p_child) {
	const bool starts_after_parent =
			p_child.start_line > p_parent.start_line ||
			(p_child.start_line == p_parent.start_line && p_child.start_column >= p_parent.start_column);
	const bool ends_before_parent =
			p_child.end_line < p_parent.end_line ||
			(p_child.end_line == p_parent.end_line && p_child.end_column <= p_parent.end_column);
	return starts_after_parent && ends_before_parent;
}

Vector<RefactorTextEdit> remove_nested_style_order_edits(const Vector<RefactorTextEdit> &p_edits) {
	Vector<RefactorTextEdit> filtered;
	for (int i = 0; i < p_edits.size(); i++) {
		bool contained = false;
		for (int j = 0; j < p_edits.size(); j++) {
			if (i == j) {
				continue;
			}
			if (style_order_edit_contains(p_edits[j], p_edits[i])) {
				contained = true;
				break;
			}
		}
		if (!contained) {
			filtered.push_back(p_edits[i]);
		}
	}
	return filtered;
}
```

Use it in `find_style_order_candidate()`:

```cpp
	candidate.edits = remove_nested_style_order_edits(edits);
	if (candidate.edits.is_empty()) {
		candidate.disabled_reason = "Members are already sorted by the Foundry Script style guide.";
		return candidate;
	}
	candidate.enabled = true;
	return candidate;
```

- [ ] **Step 6: Convert overlapping edits into repeated internal passes**

Add `gdscript_refactoring_edits.h` near the existing refactoring includes:

```cpp
#include "gdscript_refactoring_edits.h"
```

Add this helper near `remove_nested_style_order_edits()`:

```cpp
RefactorTextEdit make_whole_source_style_order_edit(const String &p_original_source, const String &p_transformed_source) {
	const Vector<String> original_lines = p_original_source.split("\n");
	RefactorTextEdit edit;
	edit.start_line = 0;
	edit.start_column = 0;
	edit.end_line = original_lines.size() - 1;
	edit.end_column = original_lines[original_lines.size() - 1].length();
	edit.has_expected_text = true;
	edit.expected_text = p_original_source;
	edit.new_text = p_transformed_source;
	return edit;
}

bool apply_style_order_pass(const String &p_source, String &r_transformed, String &r_disabled_reason) {
	const Vector<String> lines = p_source.split("\n");
	GDScriptParser parser;
	const Error parse_err = parser.parse(p_source, "user://style_order_refactor_pass.fs", false);
	if (parse_err != OK) {
		r_disabled_reason = "Cannot parse this script.";
		return false;
	}

	Vector<RefactorTextEdit> edits;
	String disabled_reason;
	collect_style_order_class_edits(lines, parser.get_tree(), edits, disabled_reason);
	if (!disabled_reason.is_empty()) {
		r_disabled_reason = disabled_reason;
		return false;
	}

	edits = remove_nested_style_order_edits(edits);
	if (edits.is_empty()) {
		r_transformed = p_source;
		return true;
	}

	if (!GDScriptRefactorEdits::apply(p_source, edits, r_transformed)) {
		r_disabled_reason = "Cannot apply style-order edits to this script.";
		return false;
	}
	return true;
}
```

Replace `find_style_order_candidate()` with a repeated-pass implementation that returns one guarded source edit:

```cpp
StyleOrderCandidate find_style_order_candidate(const RefactorContext &p_context) {
	StyleOrderCandidate candidate;
	String transformed = p_context.source;
	String disabled_reason;

	for (int pass = 0; pass < 16; pass++) {
		String next_source;
		if (!apply_style_order_pass(transformed, next_source, disabled_reason)) {
			candidate.disabled_reason = disabled_reason;
			return candidate;
		}
		if (next_source == transformed) {
			break;
		}
		transformed = next_source;
	}

	if (transformed == p_context.source) {
		candidate.disabled_reason = "Members are already sorted by the Foundry Script style guide.";
		return candidate;
	}

	candidate.enabled = true;
	candidate.edits.push_back(make_whole_source_style_order_edit(p_context.source, transformed));
	return candidate;
}
```

This repeated-pass strategy lets a parent class move an unsorted nested type first, then lets the next pass sort the nested type at its new location. The public refactor result stays non-overlapping because it exposes a single expected-text-guarded source edit.

- [ ] **Step 7: Run the fork syntax test to verify it passes**

Run:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="*Sort members by style guide preserves fork headers and sorts nested types*"
```

Expected: PASS.

- [ ] **Step 8: Run full refactor tests**

Run:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="*[Modules][Foundry Script][Refactor]*"
```

Expected: PASS.

- [ ] **Step 9: Commit**

```bash
git add modules/foundry_script/tests/test_refactor.h modules/foundry_script/editor/gdscript_refactoring.cpp
git commit -m "Support nested Foundry Script style order sorting"
```

---

### Task 6: Add Custom Override Classification When Analysis Succeeds

**Files:**
- Modify: `modules/foundry_script/tests/test_refactor.h`
- Modify: `modules/foundry_script/editor/gdscript_refactoring.cpp`

- [ ] **Step 1: Write the failing custom override test**

Add this test after the nested syntax test:

```cpp
	TEST_CASE("Sort members by style guide places resolved custom overrides before remaining methods") {
		const String source =
				"class Base:\n"
				"\tfunc configure() -> void:\n"
				"\t\tpass\n"
				"\n"
				"class Derived extends Base:\n"
				"\tfunc helper() -> void:\n"
				"\t\tpass\n"
				"\tfunc configure() -> void:\n"
				"\t\tpass\n";
		const String expected =
				"class Base:\n"
				"\tfunc configure() -> void:\n"
				"\t\tpass\n"
				"\n"
				"class Derived extends Base:\n"
				"\tfunc configure() -> void:\n"
				"\t\tpass\n"
				"\tfunc helper() -> void:\n"
				"\t\tpass\n";

		String out;
		RefactorResult r = run_sort_members_by_style_guide(source, out);
		REQUIRE(r.ok);
		CHECK_EQ(out, expected);
	}
```

- [ ] **Step 2: Run the custom override test to verify it fails**

Run:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="*Sort members by style guide places resolved custom overrides before remaining methods*"
```

Expected: FAIL because `configure()` and `helper()` are both remaining public methods.

- [ ] **Step 3: Thread analysis state into classification**

Extend `make_style_order_edit_for_class()` to receive an analysis flag:

```cpp
bool make_style_order_edit_for_class(
		const Vector<String> &p_lines,
		const GDScriptParser::ClassNode *p_class,
		bool p_analysis_ok,
		RefactorTextEdit &r_edit,
		String &r_disabled_reason)
```

Update `apply_style_order_pass()` to compute the flag after parsing:

```cpp
	bool analysis_ok = false;
	{
		GDScriptAnalyzer analyzer(&parser);
		analysis_ok = analyzer.analyze() == OK;
	}
```

Update both call sites:

```cpp
make_style_order_edit_for_class(p_lines, p_class, p_analysis_ok, edit, class_disabled_reason)
```

and:

```cpp
collect_style_order_class_edits(lines, parser.get_tree(), analysis_ok, edits, disabled_reason);
```

Update `collect_style_order_class_edits()` to accept and pass `bool p_analysis_ok`.

The final recursive collector signature must be:

```cpp
void collect_style_order_class_edits(
		const Vector<String> &p_lines,
		const GDScriptParser::ClassNode *p_class,
		bool p_analysis_ok,
		Vector<RefactorTextEdit> &r_edits,
		String &r_disabled_reason)
```

The recursive child call must be:

```cpp
collect_style_order_class_edits(p_lines, member.m_class, p_analysis_ok, r_edits, r_disabled_reason);
```

- [ ] **Step 4: Add custom override detection**

Add this helper near `is_builtin_virtual_callback_name()`:

```cpp
bool class_has_base_function(const GDScriptParser::ClassNode *p_class, const StringName &p_function_name) {
	if (p_class == nullptr) {
		return false;
	}
	const GDScriptParser::ClassNode *base_class = p_class->base_type.class_type;
	while (base_class != nullptr) {
		if (base_class->has_member(p_function_name) &&
				base_class->get_member(p_function_name).type == GDScriptParser::ClassNode::Member::FUNCTION) {
			return true;
		}
		base_class = base_class->base_type.class_type;
	}
	Ref<Script> base_script = p_class->base_type.script_type;
	while (base_script.is_valid()) {
		if (base_script->has_method(p_function_name)) {
			return true;
		}
		base_script = base_script->get_base_script();
	}
	if (p_class->base_type.native_type != StringName() && ClassDB::class_exists(p_class->base_type.native_type)) {
		return ClassDB::has_method(p_class->base_type.native_type, p_function_name);
	}
	return false;
}
```

Add the required include near the top of `modules/foundry_script/editor/gdscript_refactoring.cpp`:

```cpp
#include "core/object/class_db.h"
```

- [ ] **Step 5: Use the override detector in function classification**

Change `get_style_order_bucket()` to accept class and analysis context:

```cpp
StyleOrderBucket get_style_order_bucket(
		const GDScriptParser::ClassNode *p_class,
		const GDScriptParser::ClassNode::Member &p_member,
		bool p_analysis_ok)
```

In the function branch, insert this after the built-in virtual callback check:

```cpp
			if (p_analysis_ok && p_member.function != nullptr && class_has_base_function(p_class, p_member.function->identifier->name)) {
				return STYLE_BUCKET_CUSTOM_OVERRIDE_METHOD;
			}
```

Update the block construction call:

```cpp
		block.bucket = get_style_order_bucket(p_class, member, p_analysis_ok);
```

- [ ] **Step 6: Run the custom override test to verify it passes**

Run:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="*Sort members by style guide places resolved custom overrides before remaining methods*"
```

Expected: PASS.

- [ ] **Step 7: Run full refactor tests**

Run:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="*[Modules][Foundry Script][Refactor]*"
```

Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add modules/foundry_script/tests/test_refactor.h modules/foundry_script/editor/gdscript_refactoring.cpp
git commit -m "Classify Foundry Script custom override methods"
```

---

### Task 7: Final Verification

**Files:**
- Verify only.

- [ ] **Step 1: Build test-enabled editor**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes -j$(nproc)
```

Expected: exit code `0`, producing `bin/godot.linuxbsd.editor.dev.x86_64`.

- [ ] **Step 2: Run targeted refactor tests**

Run:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="*[Modules][Foundry Script][Refactor]*"
```

Expected: exit code `0`, all Foundry Script refactor tests pass.

- [ ] **Step 3: Run full engine tests when build time allows**

Run:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors
```

Expected: exit code `0`, full C++ and Foundry Script test suite passes.

- [ ] **Step 4: Check formatting and local diff**

Run:

```bash
git diff --check
git status --short
```

Expected: `git diff --check` exits `0`; `git status --short` lists only intentional files if changes remain uncommitted.

- [ ] **Step 5: Commit verification cleanup if needed**

If verification required a small corrective edit, commit it:

```bash
git add modules/foundry_script/tests/test_refactor.h \
	modules/foundry_script/editor/gdscript_refactoring.h \
	modules/foundry_script/editor/gdscript_refactoring.cpp \
	editor/script/script_text_editor.h \
	editor/script/script_text_editor.cpp \
	modules/foundry_script/language_server/gdscript_text_document.cpp
git commit -m "Polish Foundry Script style order refactor"
```

If no corrective edit was needed, do not create an empty commit.
