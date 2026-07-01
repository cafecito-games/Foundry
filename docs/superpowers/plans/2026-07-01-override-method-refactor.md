# Override Method Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Foundry Script `Override Method...` refactor that shows a picker of valid base methods and inserts one selected override stub.

**Architecture:** Add a reusable override-candidate API to `FSRefactoring`, then wire it into the editor with a small picker dialog. Candidate discovery lives in the refactor layer and reuses existing parser/analyzer helpers, insertion helpers, and abstract-stub rendering so tests can validate behavior without fragile UI automation.

**Tech Stack:** C++17, Godot/Foundry editor UI (`ConfirmationDialog`, `LineEdit`, `ItemList`), Foundry Script parser/analyzer, doctest tests in `modules/foundry_script/tests/test_refactor.h`, SCons test runner.

---

## File Structure

- Modify `modules/foundry_script/editor/fs_refactoring.h`: add `OVERRIDE_METHOD`, candidate/result structs, selected candidate parameter, and public candidate query.
- Modify `modules/foundry_script/editor/fs_refactoring.cpp`: implement candidate discovery, concrete/abstract/native stub rendering, availability, preparation, and cache-safe selected-candidate resolution.
- Modify `modules/foundry_script/language_server/fs_text_document.cpp`: skip generic override picker action in LSP code actions until per-candidate LSP actions are added.
- Modify `editor/script/script_text_editor.h`: add picker UI members and callback declarations.
- Modify `editor/script/script_text_editor.cpp`: add menu enum entry, static assertions, dialog construction, picker filtering, selection confirmation, and edit application.
- Modify `modules/foundry_script/tests/test_refactor.h`: add test helpers and behavior tests for candidates and generated stubs.
- Optionally modify `modules/foundry_script/tests/test_lsp.h`: assert LSP does not expose the picker-only action as a generic code action.

---

### Task 1: Public API And Disabled Availability

**Files:**
- Modify: `modules/foundry_script/editor/fs_refactoring.h`
- Modify: `modules/foundry_script/editor/fs_refactoring.cpp`
- Modify: `editor/script/script_text_editor.h`
- Modify: `editor/script/script_text_editor.cpp`
- Test: `modules/foundry_script/tests/test_refactor.h`

- [ ] **Step 1: Write the failing API/availability test**

Add helpers near the existing `implement_abstract_*` helpers in `modules/foundry_script/tests/test_refactor.h`:

```cpp
inline RefactorOverrideMethodsResult override_method_candidates(const String &p_source, int p_line, int p_column) {
	RefactorContext ctx;
	ctx.path = "user://override_method_refactor.fs";
	ctx.source = p_source;
	return FSRefactoring::get_override_method_candidates(ctx, caret(p_line, p_column));
}

inline const RefactorOverrideMethodCandidate *find_override_candidate(
		const Vector<RefactorOverrideMethodCandidate> &p_candidates,
		const String &p_name) {
	for (const RefactorOverrideMethodCandidate &candidate : p_candidates) {
		if (candidate.name == p_name) {
			return &candidate;
		}
	}
	return nullptr;
}

inline RefactorResult run_override_method(
		const String &p_source,
		int p_line,
		int p_column,
		const String &p_candidate_id,
		String &r_out) {
	RefactorContext ctx;
	ctx.path = "user://override_method_refactor.fs";
	ctx.source = p_source;
	RefactorParams params;
	params.override_method_id = p_candidate_id;
	RefactorResult r = FSRefactoring::prepare(ctx, caret(p_line, p_column), RefactorKind::OVERRIDE_METHOD, params);
	if (r.ok) {
		FSRefactorEdits::apply(ctx.source, r.edits, r_out);
	}
	return r;
}
```

Add this test after the existing “Rename is reported but disabled at a trivial location” test:

```cpp
TEST_CASE("Override method is listed and disabled outside a class") {
	RefactorContext ctx = make_context("modules/foundry_script/tests/scripts/refactor/empty.fs");
	Vector<RefactorAvailability> available = FSRefactoring::get_available_refactors(ctx, caret(0, 0));
	CHECK_EQ(available.size(), 10);
	if (available.size() < 10) {
		return;
	}
	CHECK_EQ(available[6].kind, RefactorKind::OVERRIDE_METHOD);
	CHECK_EQ(available[6].title, String("Override Method..."));
	CHECK_FALSE(available[6].enabled);
	CHECK_EQ(available[6].disabled_reason, String("Place the caret inside a class."));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
```

Expected: compile fails because `RefactorOverrideMethodsResult`, `RefactorOverrideMethodCandidate`, `RefactorKind::OVERRIDE_METHOD`, `RefactorParams::override_method_id`, and `FSRefactoring::get_override_method_candidates()` do not exist.

- [ ] **Step 3: Add the public refactor API**

In `modules/foundry_script/editor/fs_refactoring.h`, insert `OVERRIDE_METHOD` after `IMPLEMENT_ABSTRACT_METHODS` and add the structs:

```cpp
enum class RefactorKind {
	RENAME,
	EXTRACT_VARIABLE,
	EXTRACT_METHOD,
	ADD_TYPE_ANNOTATION,
	INLINE_VARIABLE,
	IMPLEMENT_ABSTRACT_METHODS,
	OVERRIDE_METHOD,
	INSERT_EXPLICIT_CAST,
	WIDEN_TO_NULLABLE,
	SORT_MEMBERS_BY_STYLE_GUIDE,
};

struct RefactorOverrideMethodCandidate {
	String id;
	String name;
	String signature;
	String origin;
	String detail;
};

struct RefactorOverrideMethodsResult {
	bool ok = false;
	String error_message;
	Vector<RefactorOverrideMethodCandidate> candidates;
};
```

Extend `RefactorParams`:

```cpp
struct RefactorParams {
	String new_name;
	String override_method_id;
};
```

Add the public query:

```cpp
class FSRefactoring {
public:
	static Vector<RefactorAvailability> get_available_refactors(const RefactorContext &p_context, const RefactorLocation &p_location);
	static RefactorOverrideMethodsResult get_override_method_candidates(const RefactorContext &p_context, const RefactorLocation &p_location);
	static RefactorResult prepare(const RefactorContext &p_context, const RefactorLocation &p_location, RefactorKind p_kind, const RefactorParams &p_params);
	static RefactorCandidatesResult find_candidates(const RefactorContext &p_context, RefactorKind p_kind);
	static bool validate_extract_method_name(
			const Vector<String> &p_existing_member_names,
			const String &p_name,
			String &r_error_message);
};
```

- [ ] **Step 4: Add minimal disabled implementation**

In `modules/foundry_script/editor/fs_refactoring.cpp`, add a minimal internal candidate function before `prepare_sort_members_by_style_guide()`:

```cpp
RefactorOverrideMethodsResult find_override_method_candidates(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const FSParseResultProvider *p_parse_results) {
	(void)p_context;
	(void)p_location;
	(void)p_parse_results;
	RefactorOverrideMethodsResult result;
	result.ok = false;
	result.error_message = "Place the caret inside a class.";
	return result;
}

RefactorResult prepare_override_method(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const RefactorParams &p_params,
		const FSParseResultProvider *p_parse_results) {
	(void)p_context;
	(void)p_location;
	(void)p_params;
	(void)p_parse_results;
	RefactorResult result;
	result.ok = false;
	result.error_message = "Selected override method is no longer available.";
	return result;
}
```

Add the public wrapper after `FSRefactoring::validate_extract_method_name()`:

```cpp
RefactorOverrideMethodsResult FSRefactoring::get_override_method_candidates(const RefactorContext &p_context, const RefactorLocation &p_location) {
#ifndef FOUNDRY_SCRIPT_NO_LSP
	RefactorParseResultProviderScope parse_results(p_context);
	const FSParseResultProvider *parse_result_provider = parse_results.get();
#else
	const FSParseResultProvider *parse_result_provider = nullptr;
#endif
	return find_override_method_candidates(p_context, p_location, parse_result_provider);
}
```

In `FSRefactoring::get_available_refactors()`, insert the availability block after `IMPLEMENT_ABSTRACT_METHODS`:

```cpp
	RefactorAvailability override_method;
	override_method.kind = RefactorKind::OVERRIDE_METHOD;
	override_method.title = "Override Method...";
	const RefactorOverrideMethodsResult override_candidates = find_override_method_candidates(p_context, p_location, parse_result_provider);
	override_method.enabled = override_candidates.ok && !override_candidates.candidates.is_empty();
	if (!override_method.enabled) {
		override_method.disabled_reason = override_candidates.error_message.is_empty()
				? String("No overridable methods found.")
				: override_candidates.error_message;
	}
	result.push_back(override_method);
```

In `FSRefactoring::prepare()`, add:

```cpp
		case RefactorKind::OVERRIDE_METHOD:
			return prepare_override_method(p_context, p_location, p_params, parse_result_provider);
```

- [ ] **Step 5: Keep editor enum mapping compiling**

In `editor/script/script_text_editor.h`, insert `EDIT_REFACTOR_OVERRIDE_METHOD` after `EDIT_REFACTOR_IMPLEMENT_ABSTRACT_METHODS`.

In `editor/script/script_text_editor.cpp`, include the new enum in both `_edit_option()` and `_run_refactor()` static assertions:

```cpp
		case EDIT_REFACTOR_OVERRIDE_METHOD:
```

```cpp
	static_assert(EDIT_REFACTOR_RENAME + (int)RefactorKind::OVERRIDE_METHOD == EDIT_REFACTOR_OVERRIDE_METHOD, "RefactorKind/EDIT_REFACTOR_* mapping mismatch");
```

- [ ] **Step 6: Run test to verify it passes**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="[Modules][FoundryScript][Refactor]*"
```

Expected: build succeeds and the refactor suite reports `[doctest] Status: SUCCESS!`.

- [ ] **Step 7: Commit**

```bash
git add modules/foundry_script/editor/fs_refactoring.h modules/foundry_script/editor/fs_refactoring.cpp editor/script/script_text_editor.h editor/script/script_text_editor.cpp modules/foundry_script/tests/test_refactor.h
git commit -m "Add override method refactor API"
```

---

### Task 2: Script Base Concrete Override Candidates

**Files:**
- Modify: `modules/foundry_script/editor/fs_refactoring.cpp`
- Test: `modules/foundry_script/tests/test_refactor.h`

- [ ] **Step 1: Write failing script-base candidate and stub tests**

Add these tests after the Task 1 override-method test:

```cpp
TEST_CASE("Override method lists concrete script base methods") {
	const String source =
			"class Base:\n"
			"\tfunc configure(speed: float = 1.0) -> int:\n"
			"\t\treturn int(speed)\n"
			"class Child extends Base:\n"
			"\tvar marker := 0\n";

	RefactorOverrideMethodsResult result = FSTests::override_method_candidates(source, 4, 1);
	REQUIRE_MESSAGE(result.ok, result.error_message);
	const RefactorOverrideMethodCandidate *candidate = FSTests::find_override_candidate(result.candidates, "configure");
	REQUIRE(candidate != nullptr);
	CHECK(candidate->signature.contains("configure(speed: float = 1.0) -> int"));
	CHECK(candidate->origin.contains("Base"));
}

TEST_CASE("Override method renders concrete script base stub with super call") {
	const String source =
			"class Base:\n"
			"\tfunc configure(speed: float = 1.0) -> int:\n"
			"\t\treturn int(speed)\n"
			"class Child extends Base:\n"
			"\tvar marker := 0\n";

	RefactorOverrideMethodsResult candidates = FSTests::override_method_candidates(source, 4, 1);
	REQUIRE(candidates.ok);
	const RefactorOverrideMethodCandidate *candidate = FSTests::find_override_candidate(candidates.candidates, "configure");
	REQUIRE(candidate != nullptr);

	String out;
	RefactorResult r = FSTests::run_override_method(source, 4, 1, candidate->id, out);
	REQUIRE_MESSAGE(r.ok, r.error_message);
	CHECK(out.contains("\tfunc configure(speed: float = 1.0) -> int:\n"));
	CHECK(out.contains("\t\treturn super.configure(speed)\n"));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="[Modules][FoundryScript][Refactor]*"
```

Expected: tests fail because the override candidate query always returns no candidates.

- [ ] **Step 3: Add internal candidate representation**

In the anonymous namespace near `ImplementAbstractCandidate`, add:

```cpp
enum class OverrideMethodOriginKind {
	SCRIPT,
	NATIVE,
	ABSTRACT,
	TRAIT,
};

struct OverrideMethodCandidate {
	bool enabled = false;
	String disabled_reason;
	RefactorOverrideMethodCandidate public_candidate;
	String rendered_block;
	int insertion_line = -1;
	bool depends_on_external_declarations = false;
};

struct OverrideMethodCollection {
	bool ok = false;
	String error_message;
	Vector<OverrideMethodCandidate> candidates;
};
```

Add identity helpers:

```cpp
String make_override_method_id(const char *p_kind, const String &p_origin, const StringName &p_name) {
	return String(p_kind) + "|" + p_origin + "|" + String(p_name);
}

bool override_name_already_declared(const FSParser::ClassNode *p_target, const StringName &p_name) {
	return p_target != nullptr && p_target->has_member(p_name) &&
			p_target->get_member(p_name).type == FSParser::ClassNode::Member::FUNCTION;
}

void collect_declared_override_names(const FSParser::ClassNode *p_target, HashSet<StringName> &r_names) {
	if (p_target == nullptr) {
		return;
	}
	for (const FSParser::ClassNode::Member &member : p_target->members) {
		if (member.type == FSParser::ClassNode::Member::FUNCTION &&
				member.function != nullptr &&
				member.function->identifier != nullptr) {
			r_names.insert(member.function->identifier->name);
		}
	}
}
```

- [ ] **Step 4: Extract reusable insertion helper**

Add this helper near `find_implement_abstract_in_tree()` so both refactors use the same insertion logic:

```cpp
int find_class_method_insertion_line(const FSParser::ClassNode *p_target, const FSParser::ClassNode *p_tree, const Vector<String> &p_lines) {
	int insertion_line = p_target != nullptr ? p_target->start_line : 0;
	bool has_member_line = false;
	if (p_target == nullptr) {
		return insertion_line;
	}
	for (const FSParser::ClassNode::Member &member : p_target->members) {
		const FSParser::Node *node = member.get_source_node();
		if (node != nullptr && node->end_line > 0) {
			has_member_line = true;
			if (node->end_line > insertion_line) {
				insertion_line = node->end_line;
			}
		}
	}
	for (const FSParser::ClassNode::TraitUse &trait_use : p_target->used_traits) {
		const int trait_end = trait_use_end_line(trait_use);
		if (trait_end > p_target->start_line && trait_end > insertion_line) {
			insertion_line = trait_end;
		}
	}
	if (!has_member_line && p_target == p_tree) {
		insertion_line = p_lines.size();
		if (insertion_line > 0 && p_lines[insertion_line - 1].is_empty()) {
			insertion_line -= 1;
		}
	}
	if (insertion_line > p_lines.size()) {
		insertion_line = p_lines.size();
	}
	return insertion_line;
}
```

Replace the matching insertion-line block in `find_implement_abstract_in_tree()` with:

```cpp
	candidate.insertion_line = find_class_method_insertion_line(target, p_tree, p_lines);
```

- [ ] **Step 5: Add script function signature and base-call rendering**

Add a shared function signature renderer by extracting the signature-building part of `render_abstract_stub()`:

```cpp
String render_function_signature(
		const FSParser::FunctionNode *p_function,
		const Vector<String> &p_lines,
		const String &p_class_indent) {
	const String name = String(p_function->identifier->name);
	String signature = p_class_indent;
	if (p_function->is_static) {
		signature += "static ";
	}
	if (p_function->is_declared_async) {
		signature += "async ";
	}
	signature += "func " + name;
	if (!p_function->type_parameters.is_empty()) {
		signature += "[";
		bool first = true;
		for (const FSParser::TypeParameterNode *type_parameter : p_function->type_parameters) {
			if (type_parameter == nullptr || type_parameter->identifier == nullptr) {
				continue;
			}
			if (!first) {
				signature += ", ";
			}
			first = false;
			signature += String(type_parameter->identifier->name);
			if (type_parameter->bound != nullptr) {
				String rendered_bound;
				if (!FSRefactorTypes::render_annotatable_type(type_parameter->resolved_bound, rendered_bound)) {
					FSParser::DataType bound_type = type_parameter->bound->get_datatype();
					bound_type.is_meta_type = false;
					FSRefactorTypes::render_annotatable_type(bound_type, rendered_bound);
				}
				if (!rendered_bound.is_empty()) {
					signature += ": " + rendered_bound;
				}
			}
		}
		signature += "]";
	}
	signature += "(";
	bool first_parameter = true;
	for (const FSParser::ParameterNode *parameter : p_function->parameters) {
		if (!first_parameter) {
			signature += ", ";
		}
		first_parameter = false;
		signature += String(parameter->identifier->name);
		String rendered_type;
		if (FSRefactorTypes::render_annotatable_type(parameter->get_datatype(), rendered_type)) {
			signature += ": " + rendered_type;
		}
		if (parameter->initializer != nullptr) {
			String default_text;
			if (get_multi_line_node_text(p_lines, parameter->initializer, default_text)) {
				signature += " = " + default_text;
			}
		}
	}
	if (p_function->rest_parameter != nullptr && p_function->rest_parameter->identifier != nullptr) {
		if (!first_parameter) {
			signature += ", ";
		}
		signature += "..." + String(p_function->rest_parameter->identifier->name);
		String rendered_rest_type;
		if (FSRefactorTypes::render_annotatable_type(p_function->rest_parameter->get_datatype(), rendered_rest_type)) {
			signature += ": " + rendered_rest_type;
		}
	}
	signature += ")";
	const FSParser::DataType return_type = p_function->get_datatype();
	const bool is_void = return_type.is_set() && !return_type.is_variant() &&
			return_type.kind == FSParser::DataType::BUILTIN &&
			return_type.builtin_type == Variant::NIL;
	String rendered_return;
	if (is_void) {
		signature += " -> void";
	} else if (FSRefactorTypes::render_annotatable_type(return_type, rendered_return)) {
		signature += " -> " + rendered_return;
	}
	return signature;
}
```

Then make `render_abstract_stub()` call `render_function_signature()` and keep its existing body logic.

Add concrete body rendering:

```cpp
String render_super_call_body(
		const FSParser::FunctionNode *p_function,
		const String &p_class_indent) {
	const String body_indent = p_class_indent + "\t";
	const String name = String(p_function->identifier->name);
	String call = "super." + name + "(";
	for (int i = 0; i < p_function->parameters.size(); i++) {
		if (i > 0) {
			call += ", ";
		}
		call += String(p_function->parameters[i]->identifier->name);
	}
	if (p_function->rest_parameter != nullptr && p_function->rest_parameter->identifier != nullptr) {
		if (!p_function->parameters.is_empty()) {
			call += ", ";
		}
		call += "..." + String(p_function->rest_parameter->identifier->name);
	}
	call += ")";

	const FSParser::DataType return_type = p_function->get_datatype();
	const bool is_void = return_type.is_set() && !return_type.is_variant() &&
			return_type.kind == FSParser::DataType::BUILTIN &&
			return_type.builtin_type == Variant::NIL;
	return body_indent + (is_void ? call : "return " + call) + "\n";
}

String render_concrete_script_override_stub(
		const FSParser::FunctionNode *p_function,
		const Vector<String> &p_lines,
		const String &p_class_indent) {
	return render_function_signature(p_function, p_lines, p_class_indent) + ":\n" +
			render_super_call_body(p_function, p_class_indent);
}
```

- [ ] **Step 6: Collect concrete script base candidates**

Add:

```cpp
void add_script_override_candidate(
		const FSParser::FunctionNode *p_function,
		const FSParser::ClassNode *p_declaring_class,
		const Vector<String> *p_declaring_lines,
		const String &p_class_indent,
		int p_insertion_line,
		Vector<OverrideMethodCandidate> &r_candidates) {
	if (p_function == nullptr || p_function->identifier == nullptr || p_function->is_final || p_function->is_abstract) {
		return;
	}
	const Vector<String> empty_lines;
	const Vector<String> &lines = p_declaring_lines != nullptr ? *p_declaring_lines : empty_lines;
	OverrideMethodCandidate candidate;
	candidate.enabled = true;
	candidate.rendered_block = render_concrete_script_override_stub(p_function, lines, p_class_indent);
	candidate.insertion_line = p_insertion_line;
	const String origin = p_declaring_class != nullptr && p_declaring_class->identifier != nullptr
			? String(p_declaring_class->identifier->name)
			: String("base class");
	candidate.public_candidate.name = String(p_function->identifier->name);
	candidate.public_candidate.signature = render_function_signature(p_function, lines, "");
	candidate.public_candidate.origin = origin;
	candidate.public_candidate.detail = candidate.public_candidate.signature + " - " + origin;
	candidate.public_candidate.id = make_override_method_id("script", origin, p_function->identifier->name);
	r_candidates.push_back(candidate);
}
```

Implement `collect_script_base_override_candidates()`:

```cpp
void collect_script_base_override_candidates(
		const FSParser::ClassNode *p_target,
		const String &p_target_path,
		const Vector<String> &p_target_lines,
		const FSParseResultProvider *p_parse_results,
		const HashSet<StringName> &p_declared_names,
		const String &p_class_indent,
		int p_insertion_line,
		Vector<OverrideMethodCandidate> &r_candidates) {
	HashSet<StringName> decided = p_declared_names;
	HashSet<const FSParser::ClassNode *> visited;
	const FSParser::ClassNode *current = p_target;
	String current_path = p_target_path;
	const Vector<String> *current_lines = &p_target_lines;
	bool is_target = true;
	while (current != nullptr) {
		if (visited.has(current)) {
			break;
		}
		visited.insert(current);
		if (!is_target) {
			for (const FSParser::ClassNode::Member &member : current->members) {
				if (member.type != FSParser::ClassNode::Member::FUNCTION ||
						member.function == nullptr ||
						member.function->identifier == nullptr) {
					continue;
				}
				const StringName name = member.function->identifier->name;
				if (decided.has(name)) {
					continue;
				}
				decided.insert(name);
				add_script_override_candidate(member.function, current, current_lines, p_class_indent, p_insertion_line, r_candidates);
			}
		}
		String base_path;
		const Vector<String> *base_lines = nullptr;
		current = resolve_base_class(current, p_parse_results, current_path, current_lines, base_path, &base_lines);
		current_path = base_path;
		current_lines = base_lines;
		is_target = false;
	}
}
```

Replace `find_override_method_candidates()` with parser/analyzer-backed collection:

```cpp
OverrideMethodCollection collect_override_methods_in_tree(
		const RefactorLocation &p_location,
		const String &p_path,
		const Vector<String> &p_lines,
		const FSParser::ClassNode *p_tree,
		const FSParseResultProvider *p_parse_results) {
	OverrideMethodCollection collection;
	const FSParser::ClassNode *target = find_enclosing_class(p_tree, p_location.start_line);
	if (target == nullptr) {
		collection.error_message = "Place the caret inside a class.";
		return collection;
	}
	if (target->is_trait) {
		collection.error_message = "Traits cannot override methods.";
		return collection;
	}

	HashSet<StringName> declared_names;
	collect_declared_override_names(target, declared_names);
	const String class_indent = class_member_indent(target, p_lines, target == p_tree);
	const int insertion_line = find_class_method_insertion_line(target, p_tree, p_lines);
	collect_script_base_override_candidates(target, p_path, p_lines, p_parse_results, declared_names, class_indent, insertion_line, collection.candidates);

	collection.ok = !collection.candidates.is_empty();
	if (!collection.ok) {
		collection.error_message = "No overridable methods found.";
	}
	return collection;
}
```

`find_override_method_candidates()` should parse with LSP provider first, then fallback parser/analyzer, mirroring `find_implement_abstract_candidate_uncached()`.

Convert `OverrideMethodCollection` to `RefactorOverrideMethodsResult` by copying `public_candidate` values.

- [ ] **Step 7: Implement selected candidate preparation**

In `prepare_override_method()`, resolve by id and insert:

```cpp
	OverrideMethodCollection collection = find_override_method_collection(p_context, p_location, p_parse_results);
	if (!collection.ok) {
		result.ok = false;
		result.error_message = collection.error_message;
		return result;
	}
	for (const OverrideMethodCandidate &candidate : collection.candidates) {
		if (candidate.public_candidate.id != p_params.override_method_id) {
			continue;
		}
		const Vector<String> lines = p_context.source.split("\n");
		const bool has_final_newline = p_context.source.ends_with("\n");
		RefactorTextEdit edit;
		set_extract_method_insertion(edit, lines, has_final_newline, candidate.insertion_line, candidate.rendered_block);
		result.rename_anchor_line = edit.start_line;
		int leading_newlines = 0;
		while (leading_newlines < edit.new_text.length() && edit.new_text[leading_newlines] == '\n') {
			leading_newlines++;
		}
		result.rename_anchor_line += leading_newlines + 1;
		result.rename_anchor_column = candidate.rendered_block.find("\n") + 1;
		if (result.rename_anchor_column < 0) {
			result.rename_anchor_column = 0;
		}
		result.ok = true;
		result.edits.push_back(edit);
		return result;
	}
	result.ok = false;
	result.error_message = "Selected override method is no longer available.";
	return result;
```

- [ ] **Step 8: Run tests to verify pass**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="[Modules][FoundryScript][Refactor]*"
```

Expected: script-base candidate and concrete stub tests pass.

- [ ] **Step 9: Commit**

```bash
git add modules/foundry_script/editor/fs_refactoring.cpp modules/foundry_script/tests/test_refactor.h
git commit -m "Add script override method candidates"
```

---

### Task 3: Abstract Candidates And Exclusions

**Files:**
- Modify: `modules/foundry_script/editor/fs_refactoring.cpp`
- Test: `modules/foundry_script/tests/test_refactor.h`

- [ ] **Step 1: Write failing abstract/final/already-declared tests**

Add:

```cpp
TEST_CASE("Override method includes abstract methods as selectable candidates") {
	const String source =
			"abstract class Base:\n"
			"\tabstract func area() -> float\n"
			"class Circle extends Base:\n"
			"\tvar radius := 1.0\n";
	RefactorOverrideMethodsResult result = FSTests::override_method_candidates(source, 3, 1);
	REQUIRE_MESSAGE(result.ok, result.error_message);
	const RefactorOverrideMethodCandidate *candidate = FSTests::find_override_candidate(result.candidates, "area");
	REQUIRE(candidate != nullptr);

	String out;
	RefactorResult r = FSTests::run_override_method(source, 3, 1, candidate->id, out);
	REQUIRE_MESSAGE(r.ok, r.error_message);
	CHECK(out.contains("func area() -> float:"));
	CHECK(out.contains("push_error(\"Not implemented: area\")"));
	CHECK(out.contains("return 0.0"));
}

TEST_CASE("Override method skips already declared methods") {
	const String source =
			"class Base:\n"
			"\tfunc configure() -> void:\n"
			"\t\tpass\n"
			"class Child extends Base:\n"
			"\tfunc configure() -> void:\n"
			"\t\tpass\n";
	RefactorOverrideMethodsResult result = FSTests::override_method_candidates(source, 4, 1);
	CHECK_FALSE(FSTests::find_override_candidate(result.candidates, "configure"));
}

TEST_CASE("Override method skips final base methods") {
	const String source =
			"class Base:\n"
			"\tfinal func locked() -> void:\n"
			"\t\tpass\n"
			"class Child extends Base:\n"
			"\tvar marker := 0\n";
	RefactorOverrideMethodsResult result = FSTests::override_method_candidates(source, 4, 1);
	CHECK_FALSE(FSTests::find_override_candidate(result.candidates, "locked"));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="[Modules][FoundryScript][Refactor]*"
```

Expected: abstract candidate test fails until abstract methods are included.

- [ ] **Step 3: Add abstract candidate collection**

Add:

```cpp
void add_abstract_override_candidate(
		const OwedAbstractMethod &p_owed,
		const String &p_class_indent,
		int p_insertion_line,
		Vector<OverrideMethodCandidate> &r_candidates) {
	if (p_owed.function == nullptr || p_owed.function->identifier == nullptr) {
		return;
	}
	const Vector<String> empty_lines;
	const Vector<String> &lines = p_owed.declaring_lines != nullptr ? *p_owed.declaring_lines : empty_lines;
	OverrideMethodCandidate candidate;
	candidate.enabled = true;
	candidate.rendered_block = render_abstract_stub(p_owed.function, lines, p_class_indent);
	candidate.insertion_line = p_insertion_line;
	const String name = String(p_owed.function->identifier->name);
	candidate.public_candidate.name = name;
	candidate.public_candidate.signature = render_function_signature(p_owed.function, lines, "");
	candidate.public_candidate.origin = "abstract requirement";
	candidate.public_candidate.detail = candidate.public_candidate.signature + " - abstract requirement";
	candidate.public_candidate.id = make_override_method_id("abstract", candidate.public_candidate.origin, p_owed.function->identifier->name);
	r_candidates.push_back(candidate);
}
```

Inside `collect_override_methods_in_tree()`, after concrete script-base collection, call existing owed-abstract collection:

```cpp
	Vector<OwedAbstractMethod> owed_abstract_methods;
	bool depends_on_external_declarations = false;
	collect_owed_abstract_methods(target, p_tree, p_path, p_lines, owed_abstract_methods, &depends_on_external_declarations, p_parse_results);
	for (const OwedAbstractMethod &owed : owed_abstract_methods) {
		if (owed.function == nullptr || owed.function->identifier == nullptr) {
			continue;
		}
		if (declared_names.has(owed.function->identifier->name)) {
			continue;
		}
		add_abstract_override_candidate(owed, class_indent, insertion_line, collection.candidates);
	}
```

- [ ] **Step 4: Run tests to verify they pass**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="[Modules][FoundryScript][Refactor]*"
```

Expected: abstract, already-declared, and final-method tests pass.

- [ ] **Step 5: Commit**

```bash
git add modules/foundry_script/editor/fs_refactoring.cpp modules/foundry_script/tests/test_refactor.h
git commit -m "Support abstract override method candidates"
```

---

### Task 4: Native Virtual Override Candidates

**Files:**
- Modify: `modules/foundry_script/editor/fs_refactoring.cpp`
- Test: `modules/foundry_script/tests/test_refactor.h`

- [ ] **Step 1: Write failing native virtual tests**

Add:

```cpp
TEST_CASE("Override method lists native virtual methods") {
	const String source =
			"extends Control\n"
			"\n"
			"var marker := 0\n";
	RefactorOverrideMethodsResult result = FSTests::override_method_candidates(source, 2, 1);
	REQUIRE_MESSAGE(result.ok, result.error_message);
	const RefactorOverrideMethodCandidate *candidate = FSTests::find_override_candidate(result.candidates, "_draw");
	REQUIRE(candidate != nullptr);
	CHECK(candidate->signature.contains("_draw() -> void"));
	CHECK(candidate->origin.contains("Control"));
}

TEST_CASE("Override method renders native virtual stub with super call") {
	const String source =
			"extends Control\n"
			"\n"
			"var marker := 0\n";
	RefactorOverrideMethodsResult candidates = FSTests::override_method_candidates(source, 2, 1);
	REQUIRE(candidates.ok);
	const RefactorOverrideMethodCandidate *candidate = FSTests::find_override_candidate(candidates.candidates, "_draw");
	REQUIRE(candidate != nullptr);

	String out;
	RefactorResult r = FSTests::run_override_method(source, 2, 1, candidate->id, out);
	REQUIRE_MESSAGE(r.ok, r.error_message);
	CHECK(out.contains("func _draw() -> void:\n"));
	CHECK(out.contains("\tsuper._draw()\n"));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="[Modules][FoundryScript][Refactor]*"
```

Expected: native tests fail because native virtuals are not collected.

- [ ] **Step 3: Add native type resolution and signature rendering**

Add:

```cpp
String native_base_name_for_class(const FSParser::ClassNode *p_class) {
	if (p_class == nullptr) {
		return String();
	}
	FSParser::DataType base_type = p_class->base_type;
	while (true) {
		switch (base_type.kind) {
			case FSParser::DataType::CLASS: {
				const FSParser::ClassNode *base_class = base_type.class_type;
				if (base_class == nullptr) {
					return String();
				}
				base_type = base_class->base_type;
			} break;
			case FSParser::DataType::SCRIPT: {
				Ref<Script> base_script = base_type.script_type;
				StringName native_type = base_type.native_type;
				while (base_script.is_valid()) {
					if (native_type == StringName()) {
						native_type = base_script->get_instance_base_type();
					}
					base_script = base_script->get_base_script();
				}
				return String(native_type);
			}
			case FSParser::DataType::NATIVE:
				return String(base_type.native_type);
			default:
				return String();
		}
	}
}
```

Add render helpers:

```cpp
String render_type_from_property(const PropertyInfo &p_property) {
	FSParser::DataType type = type_from_property(p_property, true);
	String rendered;
	if (type.kind == FSParser::DataType::BUILTIN && type.builtin_type == Variant::NIL) {
		return "void";
	}
	if (FSRefactorTypes::render_annotatable_type(type, rendered)) {
		return rendered;
	}
	if (p_property.type == Variant::NIL) {
		return "Variant";
	}
	return Variant::get_type_name(p_property.type);
}

String render_native_override_signature(const MethodInfo &p_method, const String &p_class_indent) {
	String signature = p_class_indent;
	if ((p_method.flags & METHOD_FLAG_STATIC) != 0) {
		signature += "static ";
	}
	if ((p_method.flags & METHOD_FLAG_ASYNC) != 0) {
		signature += "async ";
	}
	signature += "func " + p_method.name + "(";
	const int first_default = p_method.arguments.size() - p_method.default_arguments.size();
	for (int i = 0; i < p_method.arguments.size(); i++) {
		if (i > 0) {
			signature += ", ";
		}
		const PropertyInfo &argument = p_method.arguments[i];
		const String arg_name = argument.name.is_empty() ? "arg" + itos(i + 1) : String(argument.name);
		signature += arg_name + ": " + render_type_from_property(argument);
		if (i >= first_default) {
			const int default_index = i - first_default;
			if (default_index >= 0 && default_index < p_method.default_arguments.size()) {
				signature += " = " + String(VariantWriter::write_to_string(p_method.default_arguments[default_index]));
			}
		}
	}
	if ((p_method.flags & METHOD_FLAG_VARARG) != 0) {
		if (!p_method.arguments.is_empty()) {
			signature += ", ";
		}
		signature += "...args: Array";
	}
	signature += ")";
	const String return_type = render_type_from_property(p_method.return_val);
	signature += " -> " + return_type;
	return signature;
}
```

If `VariantWriter::write_to_string()` is not available in this translation unit, include the correct header or replace that line with `String(p_method.default_arguments[default_index])` and keep the native-default test coverage limited to methods without defaults.

- [ ] **Step 4: Add native super body and collection**

Add:

```cpp
String render_native_super_call_body(const MethodInfo &p_method, const String &p_class_indent) {
	const String body_indent = p_class_indent + "\t";
	String call = "super." + p_method.name + "(";
	for (int i = 0; i < p_method.arguments.size(); i++) {
		if (i > 0) {
			call += ", ";
		}
		const String arg_name = p_method.arguments[i].name.is_empty() ? "arg" + itos(i + 1) : String(p_method.arguments[i].name);
		call += arg_name;
	}
	if ((p_method.flags & METHOD_FLAG_VARARG) != 0) {
		if (!p_method.arguments.is_empty()) {
			call += ", ";
		}
		call += "...args";
	}
	call += ")";
	const bool is_void = p_method.return_val.type == Variant::NIL;
	return body_indent + (is_void ? call : "return " + call) + "\n";
}

void collect_native_virtual_override_candidates(
		const StringName &p_native_class,
		const HashSet<StringName> &p_declared_names,
		const String &p_class_indent,
		int p_insertion_line,
		Vector<OverrideMethodCandidate> &r_candidates) {
	if (p_native_class == StringName() || !ClassDB::class_exists(p_native_class)) {
		return;
	}
	List<MethodInfo> methods;
	ClassDB::get_virtual_methods(p_native_class, &methods);
	HashSet<StringName> seen;
	for (const MethodInfo &method : methods) {
		const StringName name = method.name;
		if (name == StringName() || p_declared_names.has(name) || seen.has(name)) {
			continue;
		}
		seen.insert(name);
		OverrideMethodCandidate candidate;
		candidate.enabled = true;
		candidate.insertion_line = p_insertion_line;
		candidate.rendered_block = render_native_override_signature(method, p_class_indent) + ":\n" +
				render_native_super_call_body(method, p_class_indent);
		candidate.public_candidate.name = method.name;
		candidate.public_candidate.signature = render_native_override_signature(method, "");
		candidate.public_candidate.origin = String(p_native_class);
		candidate.public_candidate.detail = candidate.public_candidate.signature + " - " + String(p_native_class);
		candidate.public_candidate.id = make_override_method_id("native", String(p_native_class), name);
		r_candidates.push_back(candidate);
	}
}
```

Call it from `collect_override_methods_in_tree()` after script/abstract collection:

```cpp
	const String native_base = native_base_name_for_class(target);
	collect_native_virtual_override_candidates(StringName(native_base), declared_names, class_indent, insertion_line, collection.candidates);
```

- [ ] **Step 5: Run tests to verify they pass**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="[Modules][FoundryScript][Refactor]*"
```

Expected: native virtual tests pass.

- [ ] **Step 6: Commit**

```bash
git add modules/foundry_script/editor/fs_refactoring.cpp modules/foundry_script/tests/test_refactor.h
git commit -m "Add native virtual override candidates"
```

---

### Task 5: Editor Picker UI

**Files:**
- Modify: `editor/script/script_text_editor.h`
- Modify: `editor/script/script_text_editor.cpp`

- [ ] **Step 1: Add editor fields and declarations**

In `editor/script/script_text_editor.h`, include `ItemList`:

```cpp
#include "scene/gui/item_list.h"
```

Add members near the extract-method dialog fields:

```cpp
	ConfirmationDialog *override_method_dialog = nullptr;
	LineEdit *override_method_filter = nullptr;
	ItemList *override_method_list = nullptr;
	Label *override_method_error_label = nullptr;
	Vector<RefactorOverrideMethodCandidate> override_method_candidates;
	Vector<int> override_method_filtered_indices;
	RefactorLocation override_method_location;
```

Add methods near the extract-method callbacks:

```cpp
	void _show_override_method_dialog(
			const RefactorLocation &p_location,
			const Vector<RefactorOverrideMethodCandidate> &p_candidates);
	void _populate_override_method_list(const String &p_filter);
	void _on_override_method_confirmed();
	void _on_override_method_canceled();
	void _on_override_method_filter_changed(const String &p_text);
	void _on_override_method_item_activated(int p_index);
	void _update_override_method_confirm_state();
```

- [ ] **Step 2: Wire the refactor action to the picker**

In `_run_refactor()`, add a branch before the generic `prepare()` path:

```cpp
	if (kind == RefactorKind::OVERRIDE_METHOD) {
		RefactorOverrideMethodsResult candidates = FSRefactoring::get_override_method_candidates(ctx, loc);
		if (!candidates.ok || candidates.candidates.is_empty()) {
			EditorToaster::get_singleton()->popup_str(
					candidates.error_message.is_empty() ? TTR("No overridable methods found.") : candidates.error_message,
					EditorToaster::SEVERITY_WARNING);
			return;
		}
		_show_override_method_dialog(loc, candidates.candidates);
		return;
	}
```

- [ ] **Step 3: Construct the dialog**

In the constructor after the extract-method dialog, add:

```cpp
	override_method_dialog = memnew(ConfirmationDialog);
	override_method_dialog->set_title(TTRC("Override Method"));
	VBoxContainer *override_method_vbox = memnew(VBoxContainer);
	override_method_dialog->add_child(override_method_vbox);

	override_method_filter = memnew(LineEdit);
	override_method_filter->set_placeholder(TTRC("Filter methods"));
	override_method_filter->connect(SceneStringName(text_changed), callable_mp(this, &ScriptTextEditor::_on_override_method_filter_changed));
	override_method_vbox->add_child(override_method_filter);

	override_method_list = memnew(ItemList);
	override_method_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	override_method_list->set_custom_minimum_size(Size2(520, 260) * EDSCALE);
	override_method_list->connect("item_activated", callable_mp(this, &ScriptTextEditor::_on_override_method_item_activated));
	override_method_vbox->add_child(override_method_list);

	override_method_error_label = memnew(Label);
	override_method_vbox->add_child(override_method_error_label);

	override_method_dialog->connect(SceneStringName(confirmed), callable_mp(this, &ScriptTextEditor::_on_override_method_confirmed));
	override_method_dialog->connect(SNAME("canceled"), callable_mp(this, &ScriptTextEditor::_on_override_method_canceled));
	add_child(override_method_dialog);
```

- [ ] **Step 4: Add picker behavior**

Add these methods near the extract-method methods:

```cpp
void ScriptTextEditor::_show_override_method_dialog(
		const RefactorLocation &p_location,
		const Vector<RefactorOverrideMethodCandidate> &p_candidates) {
	override_method_location = p_location;
	override_method_candidates = p_candidates;
	override_method_filter->clear();
	_populate_override_method_list(String());
	override_method_dialog->popup_centered();
	override_method_filter->grab_focus();
}

void ScriptTextEditor::_populate_override_method_list(const String &p_filter) {
	override_method_list->clear();
	override_method_filtered_indices.clear();
	const String filter = p_filter.strip_edges().to_lower();
	for (int i = 0; i < override_method_candidates.size(); i++) {
		const RefactorOverrideMethodCandidate &candidate = override_method_candidates[i];
		const String text = candidate.detail.is_empty() ? candidate.signature : candidate.detail;
		if (!filter.is_empty() &&
				!text.to_lower().contains(filter) &&
				!candidate.name.to_lower().contains(filter)) {
			continue;
		}
		override_method_filtered_indices.push_back(i);
		override_method_list->add_item(text);
		override_method_list->set_item_tooltip(override_method_list->get_item_count() - 1, candidate.origin);
	}
	if (override_method_list->get_item_count() > 0) {
		override_method_list->select(0);
	}
	_update_override_method_confirm_state();
}

void ScriptTextEditor::_update_override_method_confirm_state() {
	const bool has_selection = override_method_list->get_selected_items().size() == 1;
	override_method_dialog->get_ok_button()->set_disabled(!has_selection);
	override_method_error_label->set_text(has_selection ? String() : TTR("Select a method to override."));
}

void ScriptTextEditor::_on_override_method_filter_changed(const String &p_text) {
	_populate_override_method_list(p_text);
}

void ScriptTextEditor::_on_override_method_item_activated(int p_index) {
	if (p_index >= 0 && p_index < override_method_filtered_indices.size()) {
		_on_override_method_confirmed();
	}
}

void ScriptTextEditor::_on_override_method_confirmed() {
	PackedInt32Array selected = override_method_list->get_selected_items();
	if (selected.size() != 1) {
		return;
	}
	const int filtered_index = selected[0];
	if (filtered_index < 0 || filtered_index >= override_method_filtered_indices.size()) {
		return;
	}
	const int candidate_index = override_method_filtered_indices[filtered_index];
	if (candidate_index < 0 || candidate_index >= override_method_candidates.size()) {
		return;
	}

	RefactorContext ctx = _make_refactor_context();
	RefactorParams params;
	params.override_method_id = override_method_candidates[candidate_index].id;
	const RefactorResult result = FSRefactoring::prepare(
			ctx,
			override_method_location,
			RefactorKind::OVERRIDE_METHOD,
			params);
	_on_override_method_canceled();
	if (!result.ok) {
		EditorToaster::get_singleton()->popup_str(
				result.error_message.is_empty() ? TTR("Selected override method is no longer available.") : result.error_message,
				EditorToaster::SEVERITY_ERROR);
		return;
	}
	_apply_refactor_result(result, ctx.source);
}

void ScriptTextEditor::_on_override_method_canceled() {
	override_method_candidates.clear();
	override_method_filtered_indices.clear();
	override_method_location = RefactorLocation();
}
```

- [ ] **Step 5: Build to verify UI compiles**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
```

Expected: build succeeds.

- [ ] **Step 6: Commit**

```bash
git add editor/script/script_text_editor.h editor/script/script_text_editor.cpp
git commit -m "Add override method picker"
```

---

### Task 6: LSP Guard, Cross-File Test, And Final Verification

**Files:**
- Modify: `modules/foundry_script/language_server/fs_text_document.cpp`
- Modify: `modules/foundry_script/tests/test_refactor.h`
- Test fixtures: `modules/foundry_script/tests/scripts/refactor/override_method_xfile_base.fs`, `modules/foundry_script/tests/scripts/refactor/override_method_xfile_child.fs`
- Optional test: `modules/foundry_script/tests/test_lsp.h`

- [ ] **Step 1: Write failing cross-file and stale-selection tests**

Create fixtures:

`modules/foundry_script/tests/scripts/refactor/override_method_xfile_base.fs`

```gdscript
extends RefCounted

func configure(speed: float = 1.0) -> int:
	return int(speed)
```

`modules/foundry_script/tests/scripts/refactor/override_method_xfile_child.fs`

```gdscript
extends "res://refactor/override_method_xfile_base.fs"

var marker := 0
```

Add tests:

```cpp
TEST_CASE("Override method resolves a concrete base defined in another file") {
	EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
	FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
	REQUIRE(protocol);

	FSTests::assert_no_errors_in("res://refactor/override_method_xfile_base.fs");
	const String child_path = "res://refactor/override_method_xfile_child.fs";
	RefactorContext ctx = make_context(child_path);
	RefactorOverrideMethodsResult candidates = FSRefactoring::get_override_method_candidates(ctx, caret(2, 1));
	REQUIRE_MESSAGE(candidates.ok, candidates.error_message);
	const RefactorOverrideMethodCandidate *candidate = FSTests::find_override_candidate(candidates.candidates, "configure");
	REQUIRE(candidate != nullptr);

	RefactorParams params;
	params.override_method_id = candidate->id;
	RefactorResult r = FSRefactoring::prepare(ctx, caret(2, 1), RefactorKind::OVERRIDE_METHOD, params);
	REQUIRE_MESSAGE(r.ok, r.error_message);
	String out;
	REQUIRE(FSRefactorEdits::apply(ctx.source, r.edits, out));
	CHECK(out.contains("func configure(speed: float = 1.0) -> int:"));
	CHECK(out.contains("return super.configure(speed)"));

	memdelete(protocol);
	memdelete(editor_file_system);
}

TEST_CASE("Override method stale selected identity fails without edits") {
	const String source =
			"class Base:\n"
			"\tfunc configure() -> void:\n"
			"\t\tpass\n"
			"class Child extends Base:\n"
			"\tvar marker := 0\n";
	String out;
	RefactorResult r = FSTests::run_override_method(source, 4, 1, "script|Missing|configure", out);
	CHECK_FALSE(r.ok);
	CHECK_EQ(r.error_message, String("Selected override method is no longer available."));
	CHECK(r.edits.is_empty());
}
```

- [ ] **Step 2: Run tests to verify cross-file fails if not complete**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="[Modules][FoundryScript][Refactor]*"
```

Expected: stale selection passes if Task 2 preparation already handles it; cross-file passes if `resolve_base_class()` provider path was wired correctly. If cross-file fails, fix `collect_script_base_override_candidates()` to carry `current_lines` from `resolve_base_class()`.

- [ ] **Step 3: Prevent generic LSP code action exposure**

In `modules/foundry_script/language_server/fs_text_document.cpp`, keep the picker-only action out of LSP until per-candidate actions exist.

Update `refactor_kind_to_lsp_kind()`:

```cpp
		case RefactorKind::OVERRIDE_METHOD:
			return "refactor.rewrite";
```

Update `is_resolvable_code_action_kind()`:

```cpp
		case RefactorKind::OVERRIDE_METHOD:
		case RefactorKind::RENAME:
			return false;
```

Update `FSTextDocument::codeAction()` skip condition:

```cpp
		if (!availability.enabled ||
				availability.kind == RefactorKind::RENAME ||
				availability.kind == RefactorKind::OVERRIDE_METHOD) {
			continue;
		}
```

- [ ] **Step 4: Optional LSP regression test**

In `modules/foundry_script/tests/test_lsp.h`, inside `[textDocument][codeAction] exposes refactors`, add:

```cpp
SUBCASE("does not list Override Method as a generic code action") {
	const String source =
			"extends Control\n"
			"\n"
			"var marker := 0\n";
	const String uri = workspace->get_file_uri("res://lsp/code_action_override_method.fs");
	text_document->didOpen(make_did_open_params(uri, source));

	Array actions = text_document->codeAction(make_code_action_params(uri, range(pos(2, 1), pos(2, 1))));
	for (int i = 0; i < actions.size(); i++) {
		Dictionary action = actions[i];
		CHECK_NE(String(action["title"]), String("Override Method..."));
	}
}
```

- [ ] **Step 5: Run focused and full verification**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case="[Modules][FoundryScript][Refactor]*"
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --force-colors
```

Expected: focused refactor tests pass. Full test run prints `[doctest] Status: SUCCESS!`; cleanup leak output may still make the process exit non-zero per repository notes.

- [ ] **Step 6: Commit**

```bash
git add modules/foundry_script/language_server/fs_text_document.cpp modules/foundry_script/tests/test_refactor.h modules/foundry_script/tests/test_lsp.h modules/foundry_script/tests/scripts/refactor/override_method_xfile_base.fs modules/foundry_script/tests/scripts/refactor/override_method_xfile_child.fs
git commit -m "Verify override method refactor"
```

---

## Self-Review

Spec coverage:

- Picker-based user flow is implemented by Task 5.
- Every valid override source is covered by Tasks 2, 3, and 4.
- Base implementation calls are covered by Tasks 2 and 4.
- Abstract fallback body is covered by Task 3.
- Stale selection and cross-file behavior are covered by Task 6.
- LSP does not expose a broken generic picker action in Task 6.

Placeholder scan:

- No unresolved placeholders are intentionally left in this plan.
- Any branch called optional is safe to skip without breaking the editor feature.

Type consistency:

- Public candidate type is consistently `RefactorOverrideMethodCandidate`.
- Public candidate query is consistently `FSRefactoring::get_override_method_candidates`.
- Selected identity is consistently `RefactorParams::override_method_id`.
- The new refactor enum is consistently `RefactorKind::OVERRIDE_METHOD`.
