# FoundryScript Generic Tagged Unions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add bounded, invariant generic parameters to named FoundryScript tagged unions, preserve the erased
`[tag, payload...]` runtime model, and ship builtin `Result[T, E]`.

**Architecture:** Store parameters on `EnumNode`, add enum-scoped parameter identity, extract declaration-neutral
argument binding from the class generic path, and specialize each union payload map exactly once. Type and expression
applications retain concrete arguments in analyzer/compiler metadata, while construction and runtime tests continue to
use the existing Array representation and opcodes.

**Tech Stack:** C++17 engine/module code, FoundryScript `.fs` integration fixtures, doctest C++ tests, LSP fixtures,
SCons/Ninja through `scripts/agent_build.py`, and the normative `modules/foundry_script/GRAMMAR.md` grammar.

**Design:** `docs/superpowers/specs/2026-08-02-foundry-script-generic-tagged-unions-design.md`

**GitHub epic:** [#1595](https://github.com/cafecito-games/Foundry/issues/1595)

---

## Execution rules

- Implement each numbered task as one native GitHub subissue and one focused PR.
- Start every task from current `develop` in its own worktree.
- Do not regenerate unrelated `.out` or formatter fixtures.
- Tests that create files use the wrapper-provided `.test_scratch`; never write generated files into tracked fixtures.
- Use the command-first Foundry CLI. Do not introduce legacy `--test` or `--path` invocation forms.
- During iteration, use `python3 scripts/agent_build.py --backend ninja`; before PR handoff, rerun the native strict
  backend with no `--backend` flag.
- Stage only files owned by the current task. Each task ends with the exact focused commit listed below.
- If a task discovers a runtime-reification requirement, stop and link the external reification follow-up; do not change
  the `[tag, payload...]` representation inside this epic.

## File responsibility map

- Grammar and AST: `modules/foundry_script/GRAMMAR.md` and `fs_parser.{h,cpp}` own declaration syntax and storage.
- Formatting: `modules/foundry_script/fs_format.cpp` and format fixtures own canonical source spelling.
- Type model: `fs_parser_data_type.cpp` and `fs_type.{h,cpp}` own display, substitution, and invariance.
- Analyzer surface: `fs_analyzer_surface.cpp` and `fs_analyzer.h` own declaration resolution, scope, and bounds.
- Analyzer expressions: `fs_analyzer.cpp` and `fs_analyzer_flow_finality.cpp` own applications, cases, patterns,
  methods, and narrowing.
- Compiler and runtime metadata: `fs_compiler.cpp`, `fs_function.h`, and bytecode files preserve specialized static
  metadata over erased values.
- Globals and dependencies: `foundry_script.cpp`, `fs_cache.cpp`, and analyzer global-enum helpers own `enum_name`,
  namespaces, imports, and preloads.
- Authoring tools: `fs_editor.cpp`, `language_server/*`, and `editor/fs_docgen.cpp` own completion, hover, symbols,
  semantic tokens, and documentation.
- Builtins: `builtin/result.fs`, `fs_builtin_source_builders.py`, and `fs_builtin_types.*` own global `Result[T, E]`.
- Focused C++ tests: `test_generic_tagged_union.h` and `tests/test_main.cpp` own pure type-model and API acceptance.
- Script tests: parser, analyzer, runtime, completion, LSP, and format fixtures own observable language behavior.

## Task 1: Parse and format generic tagged-union declarations

**GitHub issue:** [#1596](https://github.com/cafecito-games/Foundry/issues/1596)

**Issue boundary:** Grammar, AST, parser, formatter, and declaration-shape diagnostics only. Do not implement use-site
specialization in this task.

**Files:**

- Modify: `modules/foundry_script/GRAMMAR.md`
- Modify: `modules/foundry_script/fs_parser.h`
- Modify: `modules/foundry_script/fs_parser.cpp`
- Modify: `modules/foundry_script/fs_format.cpp`
- Create: `modules/foundry_script/tests/test_generic_tagged_union.h`
- Modify: `tests/test_main.cpp`
- Create: `modules/foundry_script/tests/scripts/parser/features/generic_tagged_union_declarations.norun.fs`
- Create: `modules/foundry_script/tests/scripts/parser/errors/generic_tagged_union_unnamed.fs`
- Create: `modules/foundry_script/tests/scripts/parser/errors/generic_tagged_union_unnamed.out`
- Create: `modules/foundry_script/tests/scripts/parser/errors/generic_tagged_union_integer_backed.fs`
- Create: `modules/foundry_script/tests/scripts/parser/errors/generic_tagged_union_integer_backed.out`
- Create: `modules/foundry_script/tests/scripts/format/generic_tagged_union/input.fs`
- Create: `modules/foundry_script/tests/scripts/format/generic_tagged_union/expected.fs`

- [ ] **Step 1: Add the failing parser doctest**

Add the new header to `tests/test_main.cpp`, then create this test:

```cpp
TEST_CASE("[Modules][FoundryScript][GenericTaggedUnion] Parser stores enum type parameters") {
	FSParser parser;
	const Error err = parser.parse(
			"enum Result[T, E: Resource]:\n"
			"\tOk(value: T)\n"
			"\tErr(error: E)\n",
			"user://generic_tagged_union_parser.fs", false);
	REQUIRE_EQ(err, OK);
	REQUIRE(parser.head->has_member(SNAME("Result")));
	const FSParser::EnumNode *result = parser.head->get_member(SNAME("Result")).m_enum;
	REQUIRE(result != nullptr);
	CHECK_EQ(result->type_parameters.size(), 2);
	CHECK_EQ(result->type_parameters[0]->identifier->name, SNAME("T"));
	CHECK_EQ(result->type_parameters[1]->identifier->name, SNAME("E"));
	CHECK(result->type_parameters[1]->bound != nullptr);
}
```

- [ ] **Step 2: Run the focused test and verify the syntax fails**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*GenericTaggedUnion*"
```

Expected: the new doctest fails because `EnumNode` has no `type_parameters`, or parsing reports `Expected ":" after enum
name` at `[T, E: Resource]`.

- [ ] **Step 3: Add enum parameter storage and parse it after a name**

Add this field beside `EnumNode::identifier`:

```cpp
Vector<TypeParameterNode *> type_parameters;
```

In `parse_enum`, call the existing parser only for named enums:

```cpp
if (named) {
	parse_type_parameters(enum_node->type_parameters);
} else if (check(FSTokenizer::Token::BRACKET_OPEN)) {
	push_error("Type parameters require a named tagged union.");
	Vector<TypeParameterNode *> discarded_parameters;
	parse_type_parameters(discarded_parameters);
}
```

After the existing second pass determines `is_tagged_union`, reject a parameterized integer enum:

```cpp
if (!enum_node->type_parameters.is_empty() && !enum_node->is_tagged_union) {
	push_error(vformat(R"(Generic enum "%s" must contain at least one payload-bearing case.)",
			String(enum_node->identifier->name)), enum_node->identifier);
}
```

- [ ] **Step 4: Make the tree printer and formatter emit the stored parameters**

Call the existing type-parameter printer between the enum identifier and `:` in both tree-print and source-format paths:

```cpp
if (p_enum->identifier != nullptr) {
	push_text(String(p_enum->identifier->name));
	print_type_parameters(p_enum->type_parameters);
}
```

Use the formatter's existing class/function parameter-list helper rather than implementing a second bound formatter.

- [ ] **Step 5: Add exact parser and formatter fixtures**

The positive fixture contains both declaration forms:

```foundry
enum Result[T, E: Resource]:
	Ok(value: T)
	Err(error: E)
```

The `enum_name` form lives in the same parser fixture only if that runner accepts a second virtual file. Otherwise,
create `generic_tagged_union_enum_name.norun.fs` containing:

```foundry
enum_name GlobalResult[T, E]:
	Ok(value: T)
	Err(error: E)
```

The formatter fixture additionally covers a trailing comma and multiline bound:

```foundry
enum Result[
	T,
	E: Resource,
]:
	Ok(value: T)
	Err(error: E)
```

- [ ] **Step 6: Verify parser errors and canonical formatting**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*GenericTaggedUnion*"
./bin/foundry.* --headless test generate-format-fixtures \
  modules/foundry_script/tests/scripts/format/generic_tagged_union
git diff --exit-code -- modules/foundry_script/tests/scripts/format/generic_tagged_union/expected.fs
```

Expected: focused doctests pass; fixture generation leaves the checked-in expected file unchanged.

- [ ] **Step 7: Commit Task 1**

```sh
git add modules/foundry_script/GRAMMAR.md \
  modules/foundry_script/fs_parser.h modules/foundry_script/fs_parser.cpp \
  modules/foundry_script/fs_format.cpp modules/foundry_script/tests/test_generic_tagged_union.h \
  modules/foundry_script/tests/scripts/parser modules/foundry_script/tests/scripts/format/generic_tagged_union \
  tests/test_main.cpp
git commit -m "feat(foundry_script): Parse generic tagged unions"
```

## Task 2: Resolve enum parameter scope and recursive open specialization

**GitHub issue:** [#1597](https://github.com/cafecito-games/Foundry/issues/1597)

**Issue boundary:** Declaration-site parameter identity, bounds, shadowing, bare self, and finite recursive identity. Do
not accept external `Result[int, E]` applications until Task 3.

**Files:**

- Modify: `modules/foundry_script/fs_parser.h`
- Modify: `modules/foundry_script/fs_analyzer.h`
- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Modify: `modules/foundry_script/fs_analyzer_surface.cpp`
- Modify: `modules/foundry_script/fs_parser_data_type.cpp`
- Modify: `modules/foundry_script/tests/test_generic_tagged_union.h`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/generic_tagged_union_scope.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/generic_tagged_union_recursive_open.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/generic_tagged_union_bare_external.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/generic_tagged_union_bare_external.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/generic_tagged_union_recursive_arity.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/generic_tagged_union_recursive_arity.out`

- [ ] **Step 1: Add failing scope and recursive fixtures**

Use this positive recursive shape:

```foundry
enum Tree[T]:
	Leaf(value: T)
	Branch(children: Array[Tree])

	static func singleton(value: T) -> Tree:
		return Tree.Leaf(value)
```

Use a three-level shadowing fixture:

```foundry
class Outer[T]:
	enum Choice[T]:
		Value(value: T)

		static func identity[T](value: T) -> T:
			return value
```

Expected: current analysis cannot resolve enum `T`, and bare `Tree` does not become `Tree[T]`.

- [ ] **Step 2: Add enum parameter identity**

Extend the scope enum without renumbering unrelated `DataType::Kind` values:

```cpp
enum TypeParameterScope {
	TYPE_PARAMETER_NONE,
	TYPE_PARAMETER_CLASS,
	TYPE_PARAMETER_ENUM,
	TYPE_PARAMETER_METHOD,
};
```

Add an enum handle constructor mirroring the class helper:

```cpp
static FSParser::DataType _enum_type_parameter_handle(
		const FSParser::TypeParameterNode *p_parameter, int p_index) {
	FSParser::DataType type;
	type.kind = FSParser::DataType::TYPE_PARAMETER;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.type_parameter_scope = FSParser::DataType::TYPE_PARAMETER_ENUM;
	type.type_parameter_index = p_index;
	if (p_parameter != nullptr && p_parameter->identifier != nullptr) {
		type.type_parameter_name = p_parameter->identifier->name;
	}
	return type;
}
```

- [ ] **Step 3: Search enum parameters between method and class scopes**

Update `resolve_type_parameter` so the method walk remains first, then:

```cpp
if (!found_method_parameter && current_enum != nullptr &&
		match_in(current_enum->type_parameters, FSParser::DataType::TYPE_PARAMETER_ENUM)) {
	// The current enum owns the matching parameter.
} else if (!found_method_parameter) {
	for (FSParser::ClassNode *script_class = parser->current_class;
			script_class != nullptr; script_class = script_class->outer) {
		if (match_in(script_class->type_parameters, FSParser::DataType::TYPE_PARAMETER_CLASS)) {
			declaring_class = script_class;
			break;
		}
	}
}
```

Resolve an enum parameter bound with `current_enum` set to its declaring enum and `current_function = nullptr`.

- [ ] **Step 4: Publish the open enum handle before payload resolution**

When `resolve_enum_values` creates a generic tagged union, fill `type_arguments` with its parameter handles before
publishing the identity shell:

```cpp
enum_type.type_arguments.clear();
for (int i = 0; i < p_enum->type_parameters.size(); i++) {
	enum_type.type_arguments.push_back(_enum_type_parameter_handle(p_enum->type_parameters[i], i));
}
if (enum_type.is_tagged_union) {
	p_enum->set_datatype(enum_type);
}
```

- [ ] **Step 5: Resolve bare self only for `current_enum`**

In datatype resolution, when the resolved declaration is `current_enum` and has parameters but the source has no
arguments, return its open handle. For every other generic enum with no arguments, emit:

```cpp
push_error(vformat(R"(Generic tagged union "%s" expects %d type argument(s).)",
		result.to_string(), declaration->type_parameters.size()), p_type);
```

Do not treat bare self as `Variant` and do not mutate the declaration's cached payload map.

- [ ] **Step 6: Keep recursive completion finite**

Update `complete_self_referential_enum_type` to copy the completed tag/payload table onto the current shell while
retaining `completed.type_arguments`. Do not invoke completion recursively on `completed.enum_case_payloads`.

Add a C++ assertion that `Tree[T]`'s recursive field has `enum_values` complete but its nested payload map does not grow
with repeated completion calls.

- [ ] **Step 7: Verify and commit Task 2**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*GenericTaggedUnion*"
git add modules/foundry_script/fs_parser.h modules/foundry_script/fs_parser_data_type.cpp \
  modules/foundry_script/fs_analyzer.h modules/foundry_script/fs_analyzer.cpp \
  modules/foundry_script/fs_analyzer_surface.cpp modules/foundry_script/tests/test_generic_tagged_union.h \
  modules/foundry_script/tests/scripts/analyzer
git commit -m "feat(foundry_script): Resolve generic union scope"
```

Expected: focused doctests and new analyzer fixtures pass; existing recursive tagged-union fixtures stay green.

## Task 3: Apply generic union arguments and enforce invariance

**GitHub issue:** [#1598](https://github.com/cafecito-games/Foundry/issues/1598)

**Issue boundary:** Shared argument binding, type/expression applications, arity/bounds, display, and static
compatibility. Case payload use remains Task 4.

**Files:**

- Modify: `modules/foundry_script/fs_analyzer.h`
- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Modify: `modules/foundry_script/fs_analyzer_surface.cpp`
- Modify: `modules/foundry_script/fs_parser_data_type.cpp`
- Modify: `modules/foundry_script/fs_type.cpp`
- Modify: `modules/foundry_script/tests/test_generic_tagged_union.h`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/generic_tagged_union_application.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/generic_tagged_union_bounds.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/generic_tagged_union_arity.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/generic_tagged_union_arity.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/generic_tagged_union_bound.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/generic_tagged_union_bound.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/generic_tagged_union_invariance.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/generic_tagged_union_invariance.out`

- [ ] **Step 1: Add failing type/value application and invariance tests**

The feature fixture must analyze these forms:

```foundry
var typed: Result[int, String]
var nested: Array[Result[int, String]] = []
var nullable: Result[String?, int]
var handle = Result[int, String]
```

The error fixture must reject `Result[int]`, `Result[int, String, float]`, a bound violation, applying arguments to a
non-generic tagged union, and assigning `Result[int, String]` to `Result[float, String]`.

- [ ] **Step 2: Extract declaration-neutral binding helpers**

Keep class wrappers, but route them through helpers with the parameter vector and declaration label explicit:

```cpp
bool apply_type_arguments(FSParser::DataType &r_type,
		const Vector<FSParser::TypeParameterNode *> &p_parameters,
		const Vector<FSParser::TypeNode *> &p_argument_nodes,
		const FSParser::Node *p_source, const String &p_declaration_kind,
		const String &p_declaration_name, FSParser::ClassNode *p_declaring_class,
		FSParser::EnumNode *p_declaring_enum, bool p_check_bounds = true,
		Vector<bool> *r_argument_failed = nullptr);
```

The extracted code must preserve positional failures, nullable argument markers, dependent sibling bounds, deferred
class inheritance checks, and every existing generic-class diagnostic.

- [ ] **Step 3: Add enum argument application in type position**

Dispatch `DataType::ENUM` before checking `builtin_type == DICTIONARY`. Resolve the enum declaration, call the shared
helper with `enum_node->type_parameters`, then return a specialized metatype.

For an enum with no parameters, report:

```cpp
push_error(vformat(R"(Enum "%s" is not generic and cannot take type arguments.)",
		result.to_string()), p_type);
```

- [ ] **Step 4: Add enum argument application in expression position**

Generalize the current class-only branch in `reduce_subscript` to recognize a constant generic enum metatype. Preserve
the declaration Dictionary as the reduced constant:

```cpp
specialized.is_meta_type = true;
p_subscript->set_datatype(specialized);
p_subscript->is_constant = p_subscript->base->is_constant;
p_subscript->reduced_value = p_subscript->base->reduced_value;
return;
```

- [ ] **Step 5: Enforce invariant enum arguments before nominal compatibility succeeds**

In `FSTypeCompatibility::check`, after nominal enum identity matches:

```cpp
if (p_source.type_arguments.size() != p_target.type_arguments.size()) {
	return result;
}
for (int i = 0; i < p_target.type_arguments.size(); i++) {
	if (!_datatype_invariant_equal(p_target.type_arguments[i], p_source.type_arguments[i])) {
		return result;
	}
}
result.compatible = true;
return result;
```

Keep the non-tagged int compatibility branch unchanged.

- [ ] **Step 6: Verify generic-class regression safety**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*GenericTaggedUnion*"
python3 scripts/agent_build.py --backend ninja --test --case "*FoundryScript*"
```

Expected: new focused tests pass; existing generic class/method/trait and tagged-union suites pass without fixture
changes.

- [ ] **Step 7: Commit Task 3**

```sh
git add modules/foundry_script/fs_analyzer.h modules/foundry_script/fs_analyzer.cpp \
  modules/foundry_script/fs_analyzer_surface.cpp modules/foundry_script/fs_parser_data_type.cpp \
  modules/foundry_script/fs_type.cpp modules/foundry_script/tests/test_generic_tagged_union.h \
  modules/foundry_script/tests/scripts/analyzer
git commit -m "feat(foundry_script): Apply generic union arguments"
```

## Task 4: Specialize cases, patterns, binds, and narrowing

**GitHub issue:** [#1599](https://github.com/cafecito-games/Foundry/issues/1599)

**Issue boundary:** One-level payload-map substitution and every case-oriented analyzer path.

**Files:**

- Modify: `modules/foundry_script/fs_analyzer.h`
- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Modify: `modules/foundry_script/fs_analyzer_flow_finality.cpp`
- Modify: `modules/foundry_script/fs_parser_data_type.cpp`
- Modify: `modules/foundry_script/tests/test_generic_tagged_union.h`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/generic_tagged_union_cases.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/generic_tagged_union_patterns.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/generic_tagged_union_case_argument.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/generic_tagged_union_case_argument.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/generic_tagged_union_pattern_specialization.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/generic_tagged_union_pattern_specialization.out`
- Create: `modules/foundry_script/tests/scripts/runtime/features/generic_tagged_union_cases.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/generic_tagged_union_recursive.fs`

- [ ] **Step 1: Add failing construction and bind fixtures**

The positive fixture constructs and binds:

```foundry
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)

func inspect(value: Result[int, String]) -> int:
	if value is Result[int, String].Ok(number):
		return number
	match value:
		Result[int, String].Err(var message):
			return message.length()
		_:
			return 0
```

The error fixture passes `String` to `Ok` and matches a `Result[int, String]` subject with a
`Result[float, String].Ok` pattern.

- [ ] **Step 2: Implement one-level payload specialization**

Add an analyzer helper:

```cpp
FSParser::DataType specialize_enum_type(
		const FSParser::DataType &p_type,
		const FSParser::EnumNode *p_declaration,
		const HashMap<StringName, FSParser::DataType> &p_bindings);
```

Its payload loop is exactly one declaration level:

```cpp
FSParser::DataType result = p_type;
for (KeyValue<StringName, FSParser::DataType::EnumCasePayload> &entry :
		result.enum_case_payloads) {
	for (int i = 0; i < entry.value.field_types.size(); i++) {
		entry.value.field_types.write[i] = FSParser::DataType::substitute(
				entry.value.field_types[i], p_bindings);
	}
}
return result;
```

Do not call `specialize_enum_type` recursively on a nested enum's payload map.

- [ ] **Step 3: Attach specialized maps to type/value applications**

Task 3's binding path calls `specialize_enum_type` after arity/bounds succeed. Both metatype and value conversions keep
the concrete arguments and specialized payload map.

- [ ] **Step 4: Consume specialized fields in construction and literal propagation**

`reduce_call_enum_case_construction` must read the specialized map from `p_enum_meta_type`. Keep the existing
`complete_self_referential_enum_type` call, then use the concrete field type for compatibility and Array/Dictionary
literal typing.

- [ ] **Step 5: Preserve specialization in case resolution, patterns, and binds**

Update case pseudo-types, payload-less case expressions, `resolve_match_case_pattern`,
`resolve_type_test_case_binds`, exhaustiveness comparisons, and flow-finality case narrowing. Every path copies the
subject/type handle's `type_arguments`; none reconstructs an unspecialized declaration type by name.

- [ ] **Step 6: Run observable runtime checks**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*GenericTaggedUnion*"
./bin/foundry.* --headless test run --case "*FoundryScript*" --force-colors
```

Expected runtime output from the new fixtures proves concrete integer/string binds, recursive construction, and
read-only case values. The incompatible fixtures fail during analysis, before code generation.

- [ ] **Step 7: Commit Task 4**

```sh
git add modules/foundry_script/fs_analyzer.h modules/foundry_script/fs_analyzer.cpp \
  modules/foundry_script/fs_analyzer_flow_finality.cpp modules/foundry_script/fs_parser_data_type.cpp \
  modules/foundry_script/tests/test_generic_tagged_union.h \
  modules/foundry_script/tests/scripts/analyzer modules/foundry_script/tests/scripts/runtime
git commit -m "feat(foundry_script): Specialize generic union cases"
```

## Task 5: Specialize enum functions and callable signatures

**GitHub issue:** [#1600](https://github.com/cafecito-games/Foundry/issues/1600)

**Issue boundary:** Static, instance, async, and generic enum functions plus method references and call validation.

**Files:**

- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Modify: `modules/foundry_script/fs_analyzer_call_validation.cpp`
- Modify: `modules/foundry_script/fs_compiler.cpp`
- Modify: `modules/foundry_script/tests/test_generic_tagged_union.h`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/generic_tagged_union_methods.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/generic_tagged_union_method_argument.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/generic_tagged_union_method_argument.out`
- Create: `modules/foundry_script/tests/scripts/runtime/features/generic_tagged_union_methods.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/generic_tagged_union_async_methods.fs`

- [ ] **Step 1: Add failing specialized-method fixtures**

Use:

```foundry
enum Option[T]:
	None
	Some(value: T)

	static func some(value: T) -> Option:
		return Option.Some(value)

	func is_some() -> bool:
		return self is Option.Some(_)

	static func echo[U](value: U) -> U:
		return value
```

The error fixture calls `Option[int].some("wrong")`; the runtime fixture calls static, instance, async, and generic
methods and prints/asserts their observable results.

- [ ] **Step 2: Build enum binding maps from receiver arguments**

Add a helper local to analyzer method lookup:

```cpp
HashMap<StringName, FSParser::DataType> enum_bindings;
const int binding_count = MIN(enum_declaration->type_parameters.size(),
		p_base_type.type_arguments.size());
for (int i = 0; i < binding_count; i++) {
	enum_bindings.insert(enum_declaration->type_parameters[i]->identifier->name,
			p_base_type.type_arguments[i]);
}
```

Erase names declared by the enum function's own `type_parameters` before substituting its signature.

- [ ] **Step 3: Specialize enum function parameters and returns**

In `get_function_signature`, substitute enum bindings and then `@Self` for every parameter and return. Bare open
`Option` returns become the concrete receiver `Option[int]`, while method-scoped `U` remains available to generic
method inference/application.

- [ ] **Step 4: Specialize callable references and named arguments**

Apply the same signature path when reducing `Option[int].some` or an instance method reference. Confirm named-argument
canonicalization sees concrete parameter names/types and async functions wrap the substituted return in
`Coroutine[...]`.

- [ ] **Step 5: Preserve concrete result metadata in compiler enum dispatch**

Enum dispatch metadata continues to identify the same declaration and function. The call expression datatype retains
receiver arguments; no new runtime dispatch key includes type arguments.

- [ ] **Step 6: Verify and commit Task 5**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*GenericTaggedUnion*"
git add modules/foundry_script/fs_analyzer.cpp \
  modules/foundry_script/fs_analyzer_call_validation.cpp modules/foundry_script/fs_compiler.cpp \
  modules/foundry_script/tests/test_generic_tagged_union.h \
  modules/foundry_script/tests/scripts/analyzer modules/foundry_script/tests/scripts/runtime
git commit -m "feat(foundry_script): Specialize generic union methods"
```

Expected: specialized method diagnostics and runtime fixtures pass; ordinary enum-method and generic-method fixtures
remain unchanged.

## Task 6: Preserve erased generic unions through compiler and bytecode metadata

**GitHub issue:** [#1601](https://github.com/cafecito-games/Foundry/issues/1601)

**Issue boundary:** Code generation, datatype conversion, bytecode export/load, and explicit Variant-boundary
acceptance. No value reification.

**Files:**

- Modify: `modules/foundry_script/fs_compiler.cpp`
- Modify: `modules/foundry_script/fs_function.h`
- Modify: `modules/foundry_script/fs_function.cpp`
- Modify: `modules/foundry_script/fs_bytecode_export.cpp`
- Modify: `modules/foundry_script/fs_bytecode_loader.cpp`
- Modify: `modules/foundry_script/tests/test_bytecode_script.h`
- Modify: `modules/foundry_script/tests/test_generic_tagged_union.h`
- Create: `modules/foundry_script/tests/scripts/runtime/features/generic_tagged_union_variant_erasure.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/generic_tagged_union_typed_containers.fs`

- [ ] **Step 1: Add failing datatype round-trip and bytecode tests**

Build a `DataType` for `Result[int, String]`, convert it through `FSDataType` and `ContainerType`, and assert both
arguments survive. Add a bytecode test that compiles a script constructing/matching `Result[int, String]`, exports it,
clears parser/analyzer cache, reloads it, and invokes a method returning an observable integer.

- [ ] **Step 2: Keep enum arguments during parser-to-runtime datatype conversion**

The existing generic tail in `_gdtype_from_datatype` must run for `ENUM` after lowering its value kind to Array. Confirm
the reverse conversion reconstructs `type_arguments` recursively and retains tagged-union/static identity metadata
available in compiled code.

- [ ] **Step 3: Compile specialized payload conversions**

In case construction, use the already-specialized `call->get_datatype().enum_case_payloads` field types. A
`Result[Array[int], String].Ok([1, 2])` bytecode fixture must construct a typed Array payload and run without a runtime
conversion error.

- [ ] **Step 4: Prove the value remains erased**

Use a script fixture that passes a value through `Variant` and tests it against two specializations of the same union.
The expected output records that both runtime predicates accept the same valid tag-domain shape. Also assert
`typeof(value) == TYPE_ARRAY`, `value.is_read_only()`, and deep equality/hash behavior.

- [ ] **Step 5: Round-trip nested descriptors**

Cover `Array[Result[int, String]]`, `Dictionary[String, Result[int, String]]`, and
`Callable[[Result[int, String]], Result[String, int]]` through bytecode metadata. Reuse existing recursive
`type_arguments` encoders; do not add a union-specific serialized value record.

- [ ] **Step 6: Verify and commit Task 6**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*GenericTaggedUnion*"
python3 scripts/agent_build.py --backend ninja --test --case "*Bytecode*"
git add modules/foundry_script/fs_compiler.cpp modules/foundry_script/fs_function.h \
  modules/foundry_script/fs_function.cpp modules/foundry_script/fs_bytecode_export.cpp \
  modules/foundry_script/fs_bytecode_loader.cpp modules/foundry_script/tests/test_bytecode_script.h \
  modules/foundry_script/tests/test_generic_tagged_union.h modules/foundry_script/tests/scripts/runtime
git commit -m "feat(foundry_script): Preserve generic unions in bytecode"
```

## Task 7: Complete global `enum_name` and dependency integration

**GitHub issue:** [#1602](https://github.com/cafecito-games/Foundry/issues/1602)

**Issue boundary:** Cross-file names, namespaces/imports/preloads, cache isolation, and persistence integration.

**Files:**

- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Modify: `modules/foundry_script/fs_analyzer_surface.cpp`
- Modify: `modules/foundry_script/fs_cache.cpp`
- Modify: `modules/foundry_script/foundry_script.cpp`
- Modify: `modules/foundry_script/tests/test_generic_tagged_union.h`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/generic_tagged_union_global.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/generic_tagged_union_global_values.notest.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/generic_tagged_union_namespaced.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/generic_tagged_union_namespaced_values.notest.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/generic_tagged_union_cache_isolation.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/generic_tagged_union_global.fs`

- [ ] **Step 1: Add failing cross-file and cache-isolation fixtures**

Declare this whole-file union:

```foundry
namespace generic_union_fixture

enum_name GlobalResult[T, E]:
	Ok(value: T)
	Err(error: E)
```

Consume it directly, namespace-qualified, through `import`, and through the supported preload/script-handle path. In one
consumer, construct both `[int, String]` and `[String, int]` and verify neither specialization mutates the other's
payload schema.

- [ ] **Step 2: Return the declaration's open handle from global resolution**

`make_global_enum_type_from_current_parser` must preserve the declaration's parameter list and open `type_arguments`
when returning an identity shell or completed type. `make_global_enum_type_from_path` must return a copy suitable for
use-site specialization; it must not write concrete arguments into the depended declaration cache.

- [ ] **Step 3: Specialize copies, never cached declarations**

Audit namespace, import, global-class, preload, and dependency-parser paths. Every application starts from a copied
datatype and attaches a fresh payload map. Add a C++ test that resolves two specializations sequentially and verifies
their field types remain distinct.

- [ ] **Step 4: Preserve recursive whole-file self resolution**

A same-file `enum_name Tree[T]` with `Branch(children: Array[Tree])` must receive its open self shell without
re-entering dependency resolution. The fully qualified global name remains the nominal identity for every
specialization.

- [ ] **Step 5: Verify persistence and name-mangling regressions**

Run generic-union, global enum, namespace, resource, and name-mangler cases. Runtime values remain Arrays; typed-slot
descriptors retain arguments through transports already covered by Task 6.

- [ ] **Step 6: Verify and commit Task 7**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*GenericTaggedUnion*"
python3 scripts/agent_build.py --backend ninja --test --case "*NameMangler*"
git add modules/foundry_script/fs_analyzer.cpp modules/foundry_script/fs_analyzer_surface.cpp \
  modules/foundry_script/fs_cache.cpp modules/foundry_script/foundry_script.cpp \
  modules/foundry_script/tests/test_generic_tagged_union.h \
  modules/foundry_script/tests/scripts/analyzer modules/foundry_script/tests/scripts/runtime
git commit -m "feat(foundry_script): Resolve global generic unions"
```

## Task 8: Add LSP, editor, refactoring, and docgen support

**GitHub issue:** [#1603](https://github.com/cafecito-games/Foundry/issues/1603)

**Issue boundary:** All authoring surfaces consume analyzer-owned specialization; no duplicate type system.

**Files:**

- Modify: `modules/foundry_script/fs_editor.cpp`
- Modify: `modules/foundry_script/language_server/fs_extend_parser.cpp`
- Modify: `modules/foundry_script/language_server/fs_semantic_tokens.cpp`
- Modify: `modules/foundry_script/editor/fs_docgen.cpp`
- Modify: `modules/foundry_script/editor/fs_refactoring.cpp`
- Modify: `modules/foundry_script/tests/test_lsp.h`
- Modify: `modules/foundry_script/tests/test_foundry_script.cpp`
- Create: `modules/foundry_script/tests/scripts/completion/tagged_union_cases/generic_case_constructors.fs`
- Create: `modules/foundry_script/tests/scripts/completion/tagged_union_cases/generic_case_constructors.cfg`
- Create: `modules/foundry_script/tests/scripts/completion/tagged_union_cases/generic_global_case_constructors.fs`
- Create: `modules/foundry_script/tests/scripts/completion/tagged_union_cases/generic_global_case_constructors.cfg`
- Create: `modules/foundry_script/tests/scripts/lsp/generic_tagged_unions.fs`

- [ ] **Step 1: Add failing completion and semantic-token assertions**

Completion after `Result[int, String].➡` must include:

```ini
[output]
include=[{"display":"Ok(value: int)"},{"display":"Err(error: String)"}]
```

Add semantic-token assertions for enum parameter definitions, payload references, recursive self arguments, and a
method parameter shadowing an enum parameter.

- [ ] **Step 2: Walk enum parameter nodes in symbols and semantic classification**

Extend `DocumentClassifier::walk_enum` to classify each parameter identifier as a definition, walk its bound, then walk
payloads/functions. Extend document-symbol detail to render the existing canonical parameter-list spelling.

- [ ] **Step 3: Present specialized case and method signatures**

When completion has a specialized enum base, fetch payloads/functions from that datatype. Use their concrete field
types in display text, call hints, hover, and signature help. The inheritance-solved path must complete the depended
declaration before specialization without forcing full body analysis.

- [ ] **Step 4: Keep references and rename scope-correct**

Teach refactoring traversal that `EnumNode::type_parameters` declares symbols. Match parameter references by
`TYPE_PARAMETER_ENUM` plus index/declaration identity so renaming enum `T` does not rename an outer class or method `T`.

- [ ] **Step 5: Preserve parameters in generated docs**

Extend the enum doc model or its rendered signature field to carry the parameter list and bounds. Add a doctest that
parses a generic tagged union, runs docgen, and asserts the structured doc result exposes `T`/`E` and the specialized
payload signature; do not assert on `fs_docgen.cpp` source text.

- [ ] **Step 6: Verify and commit Task 8**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*GenericTaggedUnion*"
python3 scripts/agent_build.py --backend ninja --test --case "*LSP*"
git add modules/foundry_script/fs_editor.cpp modules/foundry_script/language_server \
  modules/foundry_script/editor/fs_docgen.cpp modules/foundry_script/editor/fs_refactoring.cpp \
  modules/foundry_script/tests/test_lsp.h modules/foundry_script/tests/test_foundry_script.cpp \
  modules/foundry_script/tests/scripts/completion/tagged_union_cases \
  modules/foundry_script/tests/scripts/lsp
git commit -m "feat(foundry_script): Present generic unions in tooling"
```

## Task 9: Ship builtin `Result[T, E]` and close integrated acceptance

**GitHub issue:** [#1604](https://github.com/cafecito-games/Foundry/issues/1604)

**Issue boundary:** Builtin source, registration, docs, stable tags, export/runtime acceptance, and full strict gate.

**Files:**

- Create: `modules/foundry_script/builtin/result.fs`
- Modify: `modules/foundry_script/fs_builtin_source_builders.py`
- Modify: `modules/foundry_script/fs_builtin_sources.cpp`
- Modify: `modules/foundry_script/fs_builtin_types.cpp`
- Modify: `modules/foundry_script/fs_builtin_types.h`
- Modify: `modules/foundry_script/tests/test_fs_builtin_types.h`
- Modify: `modules/foundry_script/tests/test_fs_builtin_bytecode_export.h`
- Modify: `modules/foundry_script/tests/fixtures/builtin_export_runtime/main.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/result_builtin.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/result_builtin_nullable.fs`
- Modify: `modules/foundry_script/GRAMMAR.md`
- Modify: `modules/foundry_script/README.md`

- [ ] **Step 1: Add failing builtin registration and behavior tests**

Extend `test_fs_builtin_types.h` with observable registration and tag behavior:

```cpp
TEST_CASE("[FSBuiltinTypes][GenericTaggedUnion] Result is a builtin generic union") {
	CHECK(ScriptServer::is_global_class(SNAME("Result")));
	CHECK(ScriptServer::is_global_class_enum(SNAME("Result")));
	CHECK_EQ(ScriptServer::get_global_class_path(SNAME("Result")),
			String("foundry://builtin/result.fs"));
}
```

Use a compiled probe script to construct `Ok` and `Err`, return their tags/payload behavior, and assert `Ok` is 0 and
`Err` is 1. Do not inspect the builtin source string for those literals.

- [ ] **Step 2: Add the builtin declaration**

Create exactly:

```foundry
# A successful value or an error value.
#
# Case order is a stable runtime and serialization contract: Ok is tag 0 and Err is tag 1.
enum_name Result[T, E]:
	Ok(value: T)
	Err(error: E)
```

Register it through the existing builtin-source generator/registry so source and exported bytecode paths are
`foundry://builtin/result.fs` and the corresponding `.fsb` path.

- [ ] **Step 3: Add runtime acceptance for concrete, nullable, nested, and pattern uses**

The script fixtures cover:

```foundry
var ok: Result[int, String] = Result[int, String].Ok(7)
var nullable: Result[String?, int] = Result[String?, int].Ok(null)
var nested: Array[Result[int, String]] = [ok]

match ok:
	Result[int, String].Ok(var value):
		Utils.check(value == 7)
	Result[int, String].Err(_):
		Utils.check(false)
```

Also cover `is Result[int, String].Err(error)` and a bytecode-exported consumer.

- [ ] **Step 4: Document the shipped feature and erased boundary**

The language reference documents declaration syntax, bounds, invariance, recursion, explicit specialization,
construction, payload-less cases, match/`is` binds, enum functions, builtin `Result`, stable case order, and the fact
that runtime tests cannot distinguish erased specializations. Keep `GRAMMAR.md` normative and free of implementation
issue numbers.

- [ ] **Step 5: Prove `JsonResult[T]` is unchanged**

Run all JSON builtin/runtime fixtures and the builtin export acceptance project. No `json_result.fs` source or expected
output changes belong in this task.

- [ ] **Step 6: Run native strict focused validation**

```sh
python3 scripts/agent_build.py --test --case "*GenericTaggedUnion*"
python3 scripts/agent_build.py --test --case "*FSBuiltinTypes*"
```

Expected: both commands complete successfully with no compiler warnings or doctest failures.

- [ ] **Step 7: Run the epic full-suite gate**

```sh
python3 scripts/agent_build.py --compiler-cache ccache --jobs 4 --test
```

Expected: wrapper `build_summary` reports success and the test log contains the final doctest `Status: SUCCESS!`. On
Linux, ensure `DISPLAY=:1` is available so GUI-dependent subprocess tests run rather than skip.

- [ ] **Step 8: Run repository policy checks for touched areas**

```sh
git diff --check origin/develop...HEAD
git grep -nE '\.split\("(void|bool|String|private fun|const val|def )' -- '*.py'
git grep -n "docs/superpowers" -- '*/test_*.py' '*/check_*.py'
git grep -n "hasattr(" -- '*/test_*.py'
git grep -l "assert" -- 'misc/checks/check_*.py'
```

Expected: `git diff --check` is empty. The three policy searches add no new violations attributable to this epic.

- [ ] **Step 9: Commit Task 9**

```sh
git add modules/foundry_script/builtin/result.fs \
  modules/foundry_script/fs_builtin_source_builders.py modules/foundry_script/fs_builtin_sources.cpp \
  modules/foundry_script/fs_builtin_types.cpp modules/foundry_script/fs_builtin_types.h \
  modules/foundry_script/tests/test_fs_builtin_types.h \
  modules/foundry_script/tests/test_fs_builtin_bytecode_export.h \
  modules/foundry_script/tests/fixtures/builtin_export_runtime/main.fs \
  modules/foundry_script/tests/scripts/runtime/features/result_builtin.fs \
  modules/foundry_script/tests/scripts/runtime/features/result_builtin_nullable.fs \
  modules/foundry_script/GRAMMAR.md
git add modules/foundry_script/README.md
git commit -m "feat(foundry_script): Add builtin Result union"
```

## Epic acceptance matrix

| Requirement | Owning task | Mechanical proof |
|---|---|---|
| `enum`/`enum_name` parameters | 1 | AST doctest + parser/format fixtures |
| Tagged-union-only restriction | 1 | two declaration error fixtures |
| Enum parameter scope/bounds | 2 | shadowing and dependent-bound fixtures |
| Bare recursive self | 2 | direct/Array recursion fixtures + finite-map doctest |
| Type/value application | 3 | annotation and constant-metatype fixtures |
| Exact arity/bounds | 3 | dedicated error fixtures with counts/types |
| Invariant identity | 3 | assignment/return/call/container/comparison failures |
| Specialized case fields | 4 | construction and literal-propagation fixtures |
| Match/`is` bind types | 4 | analyzer + runtime fixtures |
| Specialized enum functions | 5 | static/instance/async/generic method fixtures |
| Erased runtime values | 6 | Variant-boundary, Array shape, equality/hash fixtures |
| Bytecode metadata | 6 | cache-cleared export/load doctest |
| Global/namespace/import/preload | 7 | cross-file fixture family |
| Cache isolation | 7 | two-specialization C++/script proof |
| LSP/editor/docgen | 8 | completion, token, hover, symbol, rename, doc tests |
| Builtin `Result[T, E]` | 9 | registry, tag, runtime, export tests |
| Documentation and strict gate | 9 | reviewed docs + native full-suite success |

## External follow-up issues

These are linked from the epic but are not native children and never block Task 9:

1. [**Design runtime-reified generic tagged unions (#1605).**](https://github.com/cafecito-games/Foundry/issues/1605)
   Produce a representation design and a new implementation epic
   covering runtime identity, `is`/`as`, assignment validation, equality/hash, serialization, JSON, reflection,
   GDExtension, and bytecode migration.
2. [**Contextual tagged-union case syntax (#1606).**](https://github.com/cafecito-games/Foundry/issues/1606)
   Support `.Ok(1)` and `.None` only where one complete
   expected union specialization is available; cover ambiguity and every expected-type context.
3. [**Evaluate migrating `JsonResult[T]` (#1607).**](https://github.com/cafecito-games/Foundry/issues/1607)
   Compare the current wrapper with `Result[T, JsonDecodeError]`, preserve its
   helpers and nullable-success semantics, and record an explicit migrate/retain decision.

## Publication and implementation handoff check

Before implementation begins, verify:

- every design section maps to an epic task or named external follow-up;
- G1-G9 dependencies match Tasks 1-9;
- no task changes the runtime Array representation;
- no task makes contextual `.Case` syntax or `JsonResult` migration part of v1;
- every child has a focused failing test, focused verification, native pre-PR verification, and a commit boundary;
- issue bodies copy the relevant task's file list, steps, and acceptance criteria rather than paraphrasing them loosely.
