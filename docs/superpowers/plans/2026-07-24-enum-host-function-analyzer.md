# Enum Host Function Analyzer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Resolve enum-hosted function declarations and bodies with exact enum `self` typing and
clear namespace/isolation diagnostics, without implementing enum call dispatch.

**Architecture:** Shared analyzer helpers resolve enum values, signatures, and bodies for both named
class enums and `enum_name` files. Enum function analysis installs the owning enum as the active scope,
derives concrete `self`/`Self` from the resolved enum datatype, and validates conflicts against the
live Dictionary builtin-method registry.

**Tech Stack:** C++17, Foundry Script parser/analyzer, Variant builtin metadata, doctest, analyzer
script fixtures, SCons.

---

### Task 1: Lock exact enum function types with failing C++ tests

**Files:**
- Modify: `modules/foundry_script/tests/test_foundry_script.cpp`

- [ ] **Step 1: Add a named-enum analyzer metadata test**

Parse and analyze a named enum containing an instance function whose declared return is `Self` and
whose body returns `self`, plus a static function with enum-typed parameters/return. Assert:

```cpp
CHECK_EQ(identity->get_datatype().kind, FSParser::DataType::ENUM);
CHECK_EQ(identity->get_datatype().builtin_type, Variant::INT);
CHECK_FALSE(identity->get_datatype().is_meta_type);
CHECK_EQ(identity->get_datatype().enum_type, SNAME("Status"));
CHECK_EQ(identity->get_datatype().class_type, root);
CHECK(identity->resolved_signature);
CHECK(identity->resolved_body);
```

Inspect the `ReturnNode` expression and assert the `SelfNode` carries the same enum identity, owner,
script path, and value map.

- [ ] **Step 2: Add an `enum_name` analyzer metadata test**

Register a temporary `enum_name GlobalStatus` script, analyze it, and assert its instance/static
function signatures and bodies resolve against the standalone global enum datatype.

- [ ] **Step 3: Run the focused C++ case against the unchanged analyzer**

Build the test registration only if required, then run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*Analyzer resolves enum host function*" --force-colors
```

Expected: FAIL because enum functions remain unresolved and enum `self` is still typed as the outer
class.

### Task 2: Add failing analyzer fixtures for semantics and diagnostics

**Files:**
- Create: `modules/foundry_script/tests/scripts/analyzer/features/enum_host_function_declarations.norun.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/enum_host_function_declarations.norun.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/enum_host_function_static_self.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/enum_host_function_static_self.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/enum_host_function_value_conflict.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/enum_host_function_value_conflict.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/enum_host_function_duplicate.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/enum_host_function_duplicate.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/enum_host_function_dictionary_conflict.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/enum_host_function_dictionary_conflict.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/enum_host_function_outer_instance_access.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/enum_host_function_outer_instance_access.out`

- [ ] **Step 1: Add the declaration/body success fixture**

Use `Self`, concrete enum annotations, `match self`, an unqualified value, and a qualified value
without calling the enum functions:

```fs
enum Status:
	READY = 1
	DONE = 2

	func identity() -> Self:
		match self:
			READY:
				return self
			_:
				return Status.DONE

	static func normalize(value: Status) -> Status:
		return value
```

End the fixture with `FS_TEST_OK`. Keep it `.norun.fs` because runtime compilation belongs to #1119.

- [ ] **Step 2: Add one diagnostic fixture per namespace/isolation rule**

Cover a function reusing a value name, instance/static duplicates, a static `keys` function, and a
bare outer-class instance variable from an enum function. Add a static function containing `self` to
lock the existing static-self diagnostic.

- [ ] **Step 3: Prove the fixtures red**

Run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*FoundryScript*" --force-colors
```

Expected: the success fixture fails analysis because enum function bodies are not resolved, while the
namespace fixtures fail their expected-output checks because the targeted diagnostics do not exist.

### Task 3: Centralize enum interface and body resolution

**Files:**
- Modify: `modules/foundry_script/fs_analyzer.h`
- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Modify: `modules/foundry_script/fs_analyzer_surface.cpp`

- [ ] **Step 1: Declare focused enum helpers**

Add private helpers with explicit ownership:

```cpp
FSParser::DataType resolve_enum_values(FSParser::EnumNode *p_enum,
		const FSParser::DataType &p_enum_type, FSParser::ClassNode *p_owner);
void resolve_enum_interface(FSParser::EnumNode *p_enum,
		const FSParser::DataType &p_enum_type, FSParser::ClassNode *p_owner);
void resolve_enum_bodies(FSParser::EnumNode *p_enum, FSParser::ClassNode *p_owner);
FSParser::DataType enum_self_type(const FSParser::FunctionNode *p_function) const;
```

- [ ] **Step 2: Move duplicated value resolution into `resolve_enum_values()`**

Preserve explicit-constant checks, integer validation, `enum_values`, the read-only Dictionary, and
the exact class/script/global identity supplied in `p_enum_type`. Use this helper from both the class
member `ENUM` case and `make_global_enum_type_from_current_parser()`.

- [ ] **Step 3: Resolve signatures and namespace conflicts in `resolve_enum_interface()`**

After values are installed, use a `HashSet<StringName>` for function names. Emit one targeted error
for each conflict and use:

```cpp
if (function->is_static &&
		Variant::has_builtin_method(Variant::DICTIONARY, function_name)) {
	push_error(vformat(
			R"*(Static enum function "%s" conflicts with Dictionary method "%s()".)*",
			function_name, function_name),
			function->identifier);
}
```

Resolve annotations and each function signature with the owner class and enum active. Skip
containing-class constructor and override checks when `owner_enum != nullptr`.

- [ ] **Step 4: Resolve bodies through `resolve_enum_bodies()`**

Call the helper from `resolve_class_body()` for each named member enum and for the root
`enum_file_decl`. Keep per-function body resolution idempotent.

- [ ] **Step 5: Build and run the focused analyzer C++ tests**

Run:

```sh
scons platform=macos target=editor dev_build=yes tests=yes -j8
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*Analyzer resolves enum host function*" --force-colors
```

Expected: signature/body flags are set; remaining failures are limited to exact enum `self`/`Self`
typing or isolation rules handled in Task 4.

### Task 4: Install exact enum `self`/`Self` and outer-instance isolation

**Files:**
- Modify: `modules/foundry_script/fs_analyzer.cpp`

- [ ] **Step 1: Derive the concrete enum receiver datatype**

Implement `enum_self_type()` as:

```cpp
if (p_function == nullptr || p_function->owner_enum == nullptr) {
	return FSParser::DataType();
}
return type_from_metatype(p_function->owner_enum->get_datatype());
```

Use it in `reduce_self()`, bare `Self` type resolution, and explicit `Self` type arguments before the
ordinary class/trait receiver-relative path.

- [ ] **Step 2: Reject containing-class instance binding**

When a bare identifier in an enum function resolves to a containing-class instance variable,
function, or signal, emit:

```cpp
push_error(vformat(
		R"*(Enum function "%s()" cannot access containing class instance member "%s".)*",
		parser->current_function->identifier->name, p_identifier->name),
		p_identifier);
```

Ensure enum `self.member` continues to resolve against the enum datatype rather than the outer class.

- [ ] **Step 3: Run the focused C++ and fixture suites**

Run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*Analyzer resolves enum host function*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*FoundryScript*" --force-colors
```

Expected: all enum analyzer metadata and fixture cases pass, including unchanged existing enum
Dictionary fixtures.

### Task 5: Regenerate intentional outputs and verify the analyzer slice

**Files:**
- Update only the new `.out` files under `modules/foundry_script/tests/scripts/analyzer/`

- [ ] **Step 1: Regenerate fixture outputs**

Run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test generate-fixtures \
	modules/foundry_script/tests/scripts
```

Inspect every changed `.out` and discard any unrelated generated change.

- [ ] **Step 2: Re-run focused regression coverage**

Run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*enum host function*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*FoundryScript*" --force-colors
git diff --check
```

Expected: focused cases and the full Foundry Script fixture suite pass; no whitespace errors appear.

- [ ] **Step 3: Commit the analyzer implementation**

Stage the design/plan, analyzer sources, C++ tests, and only the new analyzer fixtures:

```sh
git add docs/superpowers/specs/2026-07-24-enum-host-function-analyzer-design.md \
	docs/superpowers/plans/2026-07-24-enum-host-function-analyzer.md \
	modules/foundry_script/fs_analyzer.h \
	modules/foundry_script/fs_analyzer.cpp \
	modules/foundry_script/fs_analyzer_surface.cpp \
	modules/foundry_script/tests/test_foundry_script.cpp \
	modules/foundry_script/tests/scripts/analyzer
git commit -m "Analyze host functions in enums"
```

### Task 6: Strict/full verification and independent review

**Files:**
- Verify the complete `origin/develop...HEAD` diff.

- [ ] **Step 1: Run the strict build**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j8
```

Expected: exit 0 with warnings treated as errors.

- [ ] **Step 2: Run the full suite with structured progress**

Run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--progress-format=jsonl \
	--progress-file /tmp/foundry-1117-full-progress.jsonl \
	--force-colors
```

Expected: doctest reports success with zero failed test cases.

- [ ] **Step 3: Run read-only Cursor review against `origin/develop`**

Invoke the `cursor-review` skill with the authoritative boundary from the design document. Triage
every finding with `superpowers:receiving-code-review`, debug real defects with
`superpowers:systematic-debugging`, fix and verify one item at a time, recommit, and repeat until the
latest valid output is exactly `RESULT: clean`.

- [ ] **Step 4: Publish and monitor**

Push `issue-1117`, open a PR targeting `develop` whose body ends with `Closes #1117`, enable squash
auto-merge, monitor checks and merge, verify the issue and Experiment project item are Done, then
remove the issue worktree and local branch.
