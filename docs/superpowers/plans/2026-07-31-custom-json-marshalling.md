# Custom JSON Marshalling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a Foundry Script class declare how it is encoded to and decoded from JSON via a `JsonSerializable` trait, honored automatically by `JSON.stringify()` at any nesting depth.

**Architecture:** Four built-in Foundry Script types (`JsonNode`, `JsonDecodeError`, `JsonResult[T]`, `JsonSerializable`) are declared in `.fs` source compiled into the `foundry_script` module and served under a reserved `foundry://builtin/` path scheme. `core/io/json.cpp` gains a `JSONObjectMarshaller` registration seam and a `Variant::OBJECT` case; the module registers a handler that checks the trait, calls `to_json()` through `Object::call`, and lowers the returned `JsonNode` to plain Variants. Two language/module prerequisites land first: self-recursive tagged unions, and the builtin source provider.

**Tech Stack:** C++ (Godot-fork engine conventions, `FOUNDRY_CLASS`, `Ref`/`RefCounted`, doctest via `tests/test_macros.h`), Foundry Script (`.fs`) fixtures, SCons.

**Spec:** `docs/superpowers/specs/2026-07-31-custom-json-marshalling-design.md`

**Build and test commands used throughout:**

```sh
# Build (macOS; use platform=linuxbsd on Linux)
scons platform=macos target=editor dev_mode=yes tests=yes -j$(sysctl -n hw.ncpu)

# Full suite
./bin/foundry.macos.editor.arm64 --headless test run --force-colors

# Scoped
./bin/foundry.macos.editor.arm64 --headless test run --case "*JSONMarshal*" --force-colors

# Foundry Script fixtures
./bin/foundry.macos.editor.arm64 --headless test run --case "*FoundryScript*" --force-colors
```

Trust the `[doctest] Status: SUCCESS!` line. The runner prints `ObjectDB instances leaked` and may exit non-zero at cleanup even when every test passes.

---

## Task Dependency Graph

```
Task 1 (recursive tagged unions) ─┐
Task 2 (standalone-lint fix) ─────┤
Task 3 (builtin source provider) ─┼─→ Task 5 (builtin type declarations) ─→ Task 7 (module encode handler) ─→ Task 8 (decode) ─→ Task 9 (docs)
Task 4 (core marshaller seam) ────┘                                      ↗
```

Tasks 1, 2, 3, and 4 are mutually independent and can proceed in parallel.

---

### Task 1: Allow self-recursive tagged unions

**Goal:** A tagged union's payload field types may reference the enum itself, directly and indirectly through typed collections, in both `enum` and `enum_name` forms.

**Files:**
- Modify: `modules/foundry_script/fs_analyzer.cpp:6954-6961` (`make_global_enum_type_from_current_parser`)
- Modify: `modules/foundry_script/fs_analyzer_surface.cpp:762-822` (`resolve_enum_values`)
- Modify: `modules/foundry_script/GRAMMAR.md` (§4.6 enum rules)
- Create: `modules/foundry_script/tests/scripts/analyzer/features/tagged_union_recursive_direct.fs` + `.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/tagged_union_recursive_array.fs` + `.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/tagged_union_recursive_dictionary.fs` + `.out`
- Create: `modules/foundry_script/tests/scripts/runtime/tagged_union_recursive_tree.fs` + `.out`

**Background — why it currently fails.** `make_global_enum_type_from_current_parser` marks the enum's datatype `RESOLVING` (`fs_analyzer.cpp:6954-6956`), then calls `resolve_enum_values`. Inside that function's payload loop, `payload.field_types.push_back(type_from_metatype(resolve_datatype(field.type)))` (`fs_analyzer_surface.cpp:793`) resolves `Array[JsonNode]`, which re-enters global-enum resolution, sees `is_resolving()`, and reports `Could not resolve global enum "JsonNode": Cyclic reference.` (`fs_analyzer.cpp:6947`).

**The fix.** The enum's *identity* — `kind = ENUM`, its name, `is_tagged_union`, `script_path`, `class_type` — is fully known before any payload type is resolved. Publish that identity shell as the enum's datatype **before** the value loop, so a re-entrant type-position reference resolves to the shell instead of hitting `RESOLVING`. Then finish resolving values and payloads and set the complete datatype.

This is safe for tagged unions specifically because payload fields are **types only** — the grammar's `tuple_field = [identifier ":"] type` admits no default-value expressions, so nothing during payload resolution needs the enum's *values*. Int-backed enums keep the existing `RESOLVING` guard, since their `= expression` values genuinely can form an unresolvable cycle.

**Acceptance Criteria:**
- [ ] `enum_name JsonNode` with `Nested(child: JsonNode)` analyzes with zero errors
- [ ] Same with `Arr(items: Array[JsonNode])`
- [ ] Same with `Obj(entries: Dictionary[String, JsonNode])`
- [ ] All three also work for an inner `enum` declared in a class body
- [ ] A recursive tree can be constructed and `match`-destructured at runtime
- [ ] An int-backed enum whose value expression references its own member is still rejected
- [ ] `GRAMMAR.md` states that self-recursive tagged unions are permitted

**Verify:** `./bin/foundry.macos.editor.arm64 --headless test run --case "*FoundryScript*" --force-colors` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing analyzer fixtures**

`modules/foundry_script/tests/scripts/analyzer/features/tagged_union_recursive_array.fs`:

```
# A tagged union's payload may reference the union itself through a typed collection.
# The payload slot holds another read-only Array at runtime, so recursion costs nothing
# representationally; only name resolution needed to stop treating it as a cycle.
enum Tree:
	Leaf(value: int)
	Branch(children: Array[Tree])

func depth(node: Tree) -> int:
	match node:
		Tree.Leaf(_):
			return 1
		Tree.Branch(var children):
			var best := 0
			for child: Tree in children:
				best = maxi(best, depth(child))
			return best + 1
	return 0

func test():
	print(depth(Tree.Branch([Tree.Leaf(1), Tree.Branch([Tree.Leaf(2)])])))
```

`modules/foundry_script/tests/scripts/analyzer/features/tagged_union_recursive_direct.fs`:

```
# A tagged union's payload may reference the union itself directly.
enum Chain:
	End
	Link(next: Chain)

func length(node: Chain) -> int:
	match node:
		Chain.End:
			return 0
		Chain.Link(var next):
			return 1 + length(next)
	return 0

func test():
	print(length(Chain.Link(Chain.Link(Chain.End))))
```

`modules/foundry_script/tests/scripts/analyzer/features/tagged_union_recursive_dictionary.fs`:

```
# A tagged union's payload may reference the union itself through a typed Dictionary.
enum Config:
	Scalar(value: int)
	Section(entries: Dictionary[String, Config])

func total(node: Config) -> int:
	match node:
		Config.Scalar(var value):
			return value
		Config.Section(var entries):
			var sum := 0
			for key: String in entries:
				sum += total(entries[key])
			return sum
	return 0

func test():
	print(total(Config.Section({"a": Config.Scalar(1), "b": Config.Scalar(2)})))
```

- [ ] **Step 2: Run the fixtures to verify they fail**

Run: `./bin/foundry.macos.editor.arm64 --headless test run --case "*FoundryScript*" --force-colors`
Expected: FAIL — the three new fixtures report `Could not find type "Tree"` / `"Chain"` / `"Config"` in the current scope.

- [ ] **Step 3: Publish the identity shell before resolving values**

In `modules/foundry_script/fs_analyzer_surface.cpp`, in `resolve_enum_values`, after `enum_type.is_tagged_union = p_enum->is_tagged_union;` and **before** the `for` loop over `p_enum->values`, publish the shell so re-entrant type-position lookups resolve:

```cpp
	FSParser::DataType enum_type = p_enum_type;
	enum_type.is_tagged_union = p_enum->is_tagged_union;

	// A tagged union's payload fields are types only (the grammar admits no default-value
	// expressions there), so a payload type that names this same union needs the union's
	// identity, not its values. Publishing the identity before the value loop lets that
	// reference resolve instead of re-entering resolution and reporting a false cycle.
	// Int-backed enums keep the stricter guard: their `= expression` values can form a
	// cycle that genuinely has no resolution.
	if (enum_type.is_tagged_union) {
		p_enum->set_datatype(enum_type);
	}

	Dictionary dictionary;
```

- [ ] **Step 4: Let the global-enum path see the shell**

In `modules/foundry_script/fs_analyzer.cpp`, in `make_global_enum_type_from_current_parser`, the `is_resolving()` check at line 6946 must not fire for a tagged union that has already published its shell. Replace the guard so it only rejects when no shell is available:

```cpp
	if (enum_node->get_datatype().is_resolving()) {
		push_error(vformat(R"(Could not resolve global enum "%s": Cyclic reference.)", p_global_name), p_source);
		return error_type;
	}
	if (enum_node->get_datatype().is_set()) {
		// May be the identity shell published by `resolve_enum_values` for a self-recursive
		// tagged union, or the fully resolved type. Either is correct in a type position.
		return enum_node->get_datatype();
	}
```

The existing `is_set()` early return already handles this once Step 3 publishes the shell — confirm the ordering is `is_resolving()` first, then `is_set()`, and that `set_datatype` in Step 3 clears the `RESOLVING` kind rather than layering on top of it. If `RESOLVING` persists, clear it explicitly in Step 3 before publishing.

- [ ] **Step 5: Fix the inner-`enum` form**

The inner form fails differently — `Could not find type "Tree" in the current scope` from `fs_analyzer.cpp:2130` — because the enum's own name is not yet in scope while its members resolve. In `resolve_enum_values`, the shell published in Step 3 must also be reachable by the plain-identifier lookup path used for inner enums. Locate where inner enum names are registered as class members (`resolve_class_interface` in `fs_analyzer_surface.cpp`) and ensure the enum member's datatype is the shell before `resolve_enum_values` runs its payload loop, using the same tagged-union-only condition.

- [ ] **Step 6: Run the fixtures to verify they pass**

Run: `./bin/foundry.macos.editor.arm64 --headless test run --case "*FoundryScript*" --force-colors`
Expected: PASS. Regenerate `.out` files if needed:
`./bin/foundry.macos.editor.arm64 --headless test generate-fixtures modules/foundry_script/tests/scripts`
Then inspect each generated `.out` by eye before committing — a regenerated fixture that bakes in wrong output is worse than a failing one.

- [ ] **Step 7: Add the negative fixture**

`modules/foundry_script/tests/scripts/analyzer/errors/enum_int_backed_self_referential_value.fs`:

```
# An int-backed enum's value expression cannot reference the enum being declared:
# unlike a tagged union payload, this needs the enum's *values*, which do not exist yet.
enum Bad:
	A = Bad.B
	B = 1

func test():
	print(Bad.A)
```

- [ ] **Step 8: Verify the negative fixture still errors**

Run: `./bin/foundry.macos.editor.arm64 --headless test run --case "*FoundryScript*" --force-colors`
Expected: the fixture's `.out` records a resolution error, not a clean analysis.

- [ ] **Step 9: Update GRAMMAR.md**

In `modules/foundry_script/GRAMMAR.md`, in the enum bullet list (around line 484-500), add:

```markdown
- A tagged union's payload field types **may reference the union itself**, directly
  (`Link(next: Chain)`) or indirectly through a typed collection
  (`Branch(children: Array[Tree])`, `Section(entries: Dictionary[String, Config])`). This
  is valid in both the inner `enum` and whole-file `enum_name` forms. Recursion terminates
  at runtime because a value is finite: a payload slot holds another `[tag, payload...]`
  read-only array. Int-backed enums are unaffected — their `= expression` values still
  cannot reference the enum being declared.
```

- [ ] **Step 10: Run the full suite**

Run: `./bin/foundry.macos.editor.arm64 --headless test run --force-colors`
Expected: `[doctest] Status: SUCCESS!`

- [ ] **Step 11: Commit**

```bash
git add modules/foundry_script/fs_analyzer.cpp \
        modules/foundry_script/fs_analyzer_surface.cpp \
        modules/foundry_script/GRAMMAR.md \
        modules/foundry_script/tests/scripts/analyzer/features/tagged_union_recursive_*.fs \
        modules/foundry_script/tests/scripts/analyzer/features/tagged_union_recursive_*.out \
        modules/foundry_script/tests/scripts/analyzer/errors/enum_int_backed_self_referential_value.* \
        modules/foundry_script/tests/scripts/runtime/tagged_union_recursive_tree.*
git commit -m "feat(foundry_script): Allow self-recursive tagged unions"
```

---

### Task 2: Report enum cyclic-reference errors when linting a file alone

**Goal:** A whole-file `enum_name` that will fail global resolution reports its error when linted on its own, not only once a consumer forces resolution.

**Files:**
- Modify: `modules/foundry_script/fs_analyzer.cpp:2364` (`resolve_enum_bodies` call site for `enum_file_decl`)
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/enum_name_file_standalone_diagnostics.fs` + `.out`

**Background.** Measured on build `e6971fb8a`: linting an `enum_name` file alone reports **0 errors**, while the same file plus a consumer (after `project import`) reports the real error. The standalone path never forces resolution of the enum's own global type, so nothing surfaces. After Task 1 the specific recursion error goes away, but the false negative remains for every other class of error in an `enum_name` file, which is why this is a separate task.

**Acceptance Criteria:**
- [ ] An `enum_name` file with a genuinely unresolvable payload type reports an error when linted alone
- [ ] A valid `enum_name` file still reports zero errors when linted alone
- [ ] Diagnostics for a standalone lint match those produced when a consumer forces resolution

**Verify:**
```sh
./bin/foundry.macos.editor.arm64 --headless script lint <file> 2>&1 | grep -c '"severity": "error"'
```
→ non-zero for the bad fixture, `0` for a valid one.

**Steps:**

- [ ] **Step 1: Reproduce the false negative**

```sh
SCRATCH="${FOUNDRY_TEST_SCRATCH:-/tmp}"
printf 'enum_name Broken:\n\tCase(value: NoSuchType)\n' > "$SCRATCH/broken.fs"
./bin/foundry.macos.editor.arm64 --headless script lint "$SCRATCH/broken.fs" 2>&1 | grep -c '"severity": "error"'
```
Expected before the fix: `0` — the bug. Expected after: non-zero.

- [ ] **Step 2: Write the failing fixture**

`modules/foundry_script/tests/scripts/analyzer/errors/enum_name_file_standalone_diagnostics.fs`:

```
enum_name Broken:
	Case(value: NoSuchType)
```

- [ ] **Step 3: Force resolution of an `enum_name` file's own type**

In `modules/foundry_script/fs_analyzer.cpp`, at the `resolve_enum_bodies(p_class->enum_file_decl, p_class)` call site (line 2364), the enum file's own datatype must be resolved before its bodies, so payload type errors surface without an external consumer. Resolve the enum file declaration's type first, guarding on `is_enum_file` so ordinary classes are unaffected:

```cpp
	if (p_class->is_enum_file && p_class->enum_file_decl != nullptr) {
		// A consumer normally forces this through `make_global_enum_type_from_current_parser`.
		// Linting the file alone has no consumer, so resolve it here or payload-type errors
		// never surface and the file appears clean.
		resolve_enum_values(p_class->enum_file_decl,
				make_standalone_global_enum_type(p_class->enum_file_decl->identifier->name, true),
				p_class);
	}
	resolve_enum_bodies(p_class->enum_file_decl, p_class);
```

Confirm `make_standalone_global_enum_type` is reachable from this translation unit; it is declared in `fs_analyzer.h` alongside `make_global_enum_type_from_current_parser`.

- [ ] **Step 4: Verify the fixture now errors**

Run: `./bin/foundry.macos.editor.arm64 --headless test run --case "*FoundryScript*" --force-colors`
Expected: the fixture's `.out` records `Could not find type "NoSuchType"`.

- [ ] **Step 5: Verify no double-reporting**

```sh
./bin/foundry.macos.editor.arm64 --headless script lint "$SCRATCH/broken.fs" 2>&1 | grep -c '"severity": "error"'
```
Expected: the error appears once, not duplicated by the later consumer-driven path.

- [ ] **Step 6: Run the full suite**

Run: `./bin/foundry.macos.editor.arm64 --headless test run --force-colors`
Expected: `[doctest] Status: SUCCESS!`

- [ ] **Step 7: Commit**

```bash
git add modules/foundry_script/fs_analyzer.cpp \
        modules/foundry_script/tests/scripts/analyzer/errors/enum_name_file_standalone_diagnostics.*
git commit -m "fix(foundry_script): Report enum_name file errors when linting alone"
```

---

### Task 3: Builtin source provider

**Goal:** The module can register a global Foundry Script type whose source is compiled into the binary and served under a reserved `foundry://builtin/` path.

**Files:**
- Create: `modules/foundry_script/fs_builtin_sources.h`
- Create: `modules/foundry_script/fs_builtin_sources.cpp`
- Modify: `modules/foundry_script/register_types.cpp` (register builtins at init, clear at deinit)
- Modify: `modules/foundry_script/fs_analyzer.cpp:6902-6906` (dependency-parser path resolution)
- Create: `modules/foundry_script/tests/test_fs_builtin_sources.h`
- Modify: `tests/test_main.cpp` (include the new test header)

**Background.** `ScriptServer::add_global_class` (`core/object/script_language.cpp:499`) requires a path, and the analyzer resolves a global name by calling `ScriptServer::get_global_class_path` and then `raise_depended_parser_for(path, ...)` (`fs_analyzer.cpp:6892-6905`). So a builtin type needs source behind a path. This task adds the smallest mechanism for that and registers no types itself — Task 5 supplies the declarations.

**Acceptance Criteria:**
- [ ] `FSBuiltinSources::get_source(path)` returns registered source text and reports absence for unknown paths
- [ ] `FSBuiltinSources::is_builtin_path(path)` is true only for the `foundry://builtin/` prefix
- [ ] A registered builtin path parses through the normal dependency-parser path
- [ ] Every registered builtin analyzes with zero errors
- [ ] A project file cannot shadow or write a `foundry://builtin/` path

**Verify:** `./bin/foundry.macos.editor.arm64 --headless test run --case "*FSBuiltinSources*" --force-colors` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing test**

`modules/foundry_script/tests/test_fs_builtin_sources.h`:

```cpp
#pragma once

#include "modules/foundry_script/fs_builtin_sources.h"

#include "tests/test_macros.h"

namespace TestFSBuiltinSources {

TEST_CASE("[FSBuiltinSources] Registered source is retrievable by path") {
	FSBuiltinSources::register_source("foundry://builtin/test_only.fs", "enum_name TestOnly:\n\tA\n");

	String source;
	CHECK(FSBuiltinSources::get_source("foundry://builtin/test_only.fs", source));
	CHECK(source.contains("enum_name TestOnly"));

	FSBuiltinSources::clear();
}

TEST_CASE("[FSBuiltinSources] Unknown path reports absence") {
	String source;
	CHECK_FALSE(FSBuiltinSources::get_source("foundry://builtin/missing.fs", source));
	CHECK(source.is_empty());
}

TEST_CASE("[FSBuiltinSources] Only the reserved prefix is a builtin path") {
	CHECK(FSBuiltinSources::is_builtin_path("foundry://builtin/json_node.fs"));
	CHECK_FALSE(FSBuiltinSources::is_builtin_path("res://json_node.fs"));
	CHECK_FALSE(FSBuiltinSources::is_builtin_path("user://json_node.fs"));
	CHECK_FALSE(FSBuiltinSources::is_builtin_path("foundry://other/json_node.fs"));
}

} // namespace TestFSBuiltinSources
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `./bin/foundry.macos.editor.arm64 --headless test run --case "*FSBuiltinSources*" --force-colors`
Expected: compile failure — `fs_builtin_sources.h` does not exist.

- [ ] **Step 3: Create the registry header**

`modules/foundry_script/fs_builtin_sources.h`:

```cpp
#pragma once

#include "core/string/ustring.h"
#include "core/templates/hash_map.h"

// Source text for Foundry Script types that ship inside the binary rather than as project
// files. A global type is resolved by path (see `ScriptServer::get_global_class_path`), so a
// built-in type still needs parseable source behind a path; these paths use a reserved scheme
// that no project can write to or shadow.
class FSBuiltinSources {
	static HashMap<String, String> sources;

public:
	static const char *PATH_PREFIX;

	static bool is_builtin_path(const String &p_path);
	static void register_source(const String &p_path, const String &p_source);
	static bool get_source(const String &p_path, String &r_source);
	static void get_registered_paths(List<String> *r_paths);
	static void clear();
};
```

- [ ] **Step 4: Implement the registry**

`modules/foundry_script/fs_builtin_sources.cpp`:

```cpp
#include "fs_builtin_sources.h"

HashMap<String, String> FSBuiltinSources::sources;

const char *FSBuiltinSources::PATH_PREFIX = "foundry://builtin/";

bool FSBuiltinSources::is_builtin_path(const String &p_path) {
	return p_path.begins_with(PATH_PREFIX);
}

void FSBuiltinSources::register_source(const String &p_path, const String &p_source) {
	ERR_FAIL_COND_MSG(!is_builtin_path(p_path),
			vformat("Builtin source path must start with \"%s\", got \"%s\".", PATH_PREFIX, p_path));
	sources[p_path] = p_source;
}

bool FSBuiltinSources::get_source(const String &p_path, String &r_source) {
	const String *found = sources.getptr(p_path);
	if (found == nullptr) {
		r_source = String();
		return false;
	}
	r_source = *found;
	return true;
}

void FSBuiltinSources::get_registered_paths(List<String> *r_paths) {
	ERR_FAIL_NULL(r_paths);
	for (const KeyValue<String, String> &entry : sources) {
		r_paths->push_back(entry.key);
	}
}

void FSBuiltinSources::clear() {
	sources.clear();
}
```

- [ ] **Step 5: Register the test header**

In `tests/test_main.cpp`, add alongside the other Foundry Script test includes:

```cpp
#include "modules/foundry_script/tests/test_fs_builtin_sources.h"
```

- [ ] **Step 6: Build and run the test**

Run:
```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j$(sysctl -n hw.ncpu)
./bin/foundry.macos.editor.arm64 --headless test run --case "*FSBuiltinSources*" --force-colors
```
Expected: `[doctest] Status: SUCCESS!`

- [ ] **Step 7: Serve builtin source from the dependency-parser path**

In `modules/foundry_script/fs_analyzer.cpp`, at the extension check around line 6902, a `foundry://builtin/` path has extension `fs` and so already takes the parser branch. Ensure `raise_depended_parser_for` can load it: find where that function reads file contents (in the parser-ref/cache layer, `fs_cache.cpp`) and add a builtin short-circuit before the filesystem read:

```cpp
	String source;
	if (FSBuiltinSources::get_source(p_path, source)) {
		// Builtin declarations ship inside the binary; there is no file to stat or read.
	} else {
		// existing filesystem read path
	}
```

Match the surrounding error-handling style exactly; a builtin path that is not registered must produce the same "could not find script" diagnostic as a missing file, not a crash.

- [ ] **Step 8: Add the shadowing test**

Append to `modules/foundry_script/tests/test_fs_builtin_sources.h`:

```cpp
TEST_CASE("[FSBuiltinSources] Project paths cannot masquerade as builtin") {
	FSBuiltinSources::register_source("foundry://builtin/shadow_test.fs", "enum_name ShadowTest:\n\tA\n");

	// A res:// path with the same basename is a distinct key and never resolves to builtin source.
	String source;
	CHECK_FALSE(FSBuiltinSources::get_source("res://shadow_test.fs", source));

	ERR_PRINT_OFF;
	FSBuiltinSources::register_source("res://not_builtin.fs", "enum_name NotBuiltin:\n\tA\n");
	ERR_PRINT_ON;
	CHECK_FALSE(FSBuiltinSources::get_source("res://not_builtin.fs", source));

	FSBuiltinSources::clear();
}
```

- [ ] **Step 9: Clear the registry at module deinit**

In `modules/foundry_script/register_types.cpp`, in the module deinitialization function, add:

```cpp
	FSBuiltinSources::clear();
```

- [ ] **Step 10: Run the full suite**

Run: `./bin/foundry.macos.editor.arm64 --headless test run --force-colors`
Expected: `[doctest] Status: SUCCESS!`

- [ ] **Step 11: Commit**

```bash
git add modules/foundry_script/fs_builtin_sources.h \
        modules/foundry_script/fs_builtin_sources.cpp \
        modules/foundry_script/register_types.cpp \
        modules/foundry_script/fs_analyzer.cpp \
        modules/foundry_script/tests/test_fs_builtin_sources.h \
        tests/test_main.cpp
git commit -m "feat(foundry_script): Add builtin script source provider"
```

---

### Task 4: JSONObjectMarshaller seam in core

**Goal:** `core/io/json.cpp` gains an object-marshalling registration point and a `Variant::OBJECT` case, with today's behavior preserved when nothing is registered.

**Files:**
- Modify: `core/io/json.h:37-90` (class declaration)
- Modify: `core/io/json.cpp:57-190` (`_stringify`), `core/io/json.cpp:618-625` (`stringify`)
- Create: `tests/core/io/test_json_marshaller.h`
- Modify: `tests/test_main.cpp`

**Background.** `_stringify` has no `Variant::OBJECT` case today; objects fall into `default:` at `core/io/json.cpp:186-190` and emit a quoted `to_string`. This task is independent of Tasks 1-3 and testable on its own, because the no-marshaller path *is* the current behavior.

**Acceptance Criteria:**
- [ ] With no marshaller registered, object output is byte-identical to today's
- [ ] A registered marshaller returning false leaves output unchanged
- [ ] A registered marshaller returning true has its Variant tree encoded, honoring `indent`, `sort_keys`, and `full_precision`
- [ ] A freed object encodes as `null`
- [ ] An object cycle is caught and reported, not infinite-looped

**Verify:** `./bin/foundry.macos.editor.arm64 --headless test run --case "*JSONMarshal*" --force-colors` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing test**

`tests/core/io/test_json_marshaller.h`:

```cpp
#pragma once

#include "core/io/json.h"

#include "tests/test_macros.h"

namespace TestJSONMarshaller {

class RecordingMarshaller : public JSONObjectMarshaller {
public:
	bool handled = false;
	bool should_handle = true;
	Variant payload;

	virtual bool marshal_object(Object *p_object, Variant &r_result) override {
		handled = true;
		if (!should_handle) {
			return false;
		}
		r_result = payload;
		return true;
	}
};

TEST_CASE("[JSONMarshal] No marshaller preserves existing object output") {
	JSON::set_object_marshaller(nullptr);

	Object *object = memnew(Object);
	const String result = JSON::stringify(object);
	// Today objects stringify as a quoted to_string; this must not change.
	CHECK(result.begins_with("\""));
	CHECK(result.ends_with("\""));
	memdelete(object);
}

TEST_CASE("[JSONMarshal] Marshaller declining leaves output unchanged") {
	RecordingMarshaller marshaller;
	marshaller.should_handle = false;
	JSON::set_object_marshaller(&marshaller);

	Object *object = memnew(Object);
	const String result = JSON::stringify(object);
	CHECK(marshaller.handled);
	CHECK(result.begins_with("\""));

	memdelete(object);
	JSON::set_object_marshaller(nullptr);
}

TEST_CASE("[JSONMarshal] Marshalled tree is encoded") {
	RecordingMarshaller marshaller;
	Dictionary payload;
	payload["level"] = 3;
	payload["name"] = "Captain";
	marshaller.payload = payload;
	JSON::set_object_marshaller(&marshaller);

	Object *object = memnew(Object);
	CHECK(JSON::stringify(object) == "{\"level\":3,\"name\":\"Captain\"}");

	memdelete(object);
	JSON::set_object_marshaller(nullptr);
}

TEST_CASE("[JSONMarshal] Marshalled object nested in a container is encoded") {
	RecordingMarshaller marshaller;
	marshaller.payload = 7;
	JSON::set_object_marshaller(&marshaller);

	Object *object = memnew(Object);
	Array array;
	array.push_back(object);
	CHECK(JSON::stringify(array) == "[7]");

	memdelete(object);
	JSON::set_object_marshaller(nullptr);
}

TEST_CASE("[JSONMarshal] Freed object encodes as null") {
	RecordingMarshaller marshaller;
	marshaller.payload = 1;
	JSON::set_object_marshaller(&marshaller);

	Variant variant;
	{
		Object *object = memnew(Object);
		variant = object;
		memdelete(object);
	}
	CHECK(JSON::stringify(variant) == "null");

	JSON::set_object_marshaller(nullptr);
}

} // namespace TestJSONMarshaller
```

- [ ] **Step 2: Run to verify it fails**

Run: `scons platform=macos target=editor dev_mode=yes tests=yes -j$(sysctl -n hw.ncpu)`
Expected: compile failure — `JSONObjectMarshaller` is not declared.

- [ ] **Step 3: Declare the interface**

In `core/io/json.h`, above `class JSON`:

```cpp
// Lets a scripting module decide how an Object is represented in JSON without core/io
// learning any language-specific concept. Core keeps behaving exactly as before when no
// marshaller is registered.
class JSONObjectMarshaller {
public:
	// Returns true if this object was handled; r_result is then a plain Variant tree.
	virtual bool marshal_object(Object *p_object, Variant &r_result) = 0;
	virtual ~JSONObjectMarshaller() {}
};
```

Inside `class JSON`, in the private section alongside the other statics:

```cpp
	static JSONObjectMarshaller *object_marshaller;
```

And in the public section:

```cpp
	static void set_object_marshaller(JSONObjectMarshaller *p_marshaller);
```

- [ ] **Step 4: Thread the object marker set through `_stringify`**

In `core/io/json.h`, change the `_stringify` declaration to carry an object-identity marker set, because the existing `p_markers` keys on `Array`/`Dictionary` ids and cannot detect a cycle that runs through an object:

```cpp
	static void _stringify(String &r_result, const Variant &p_var, const String &p_indent, int p_cur_indent, bool p_sort_keys, HashSet<const void *> &p_markers, bool p_full_precision, HashSet<uint64_t> &p_object_markers);
```

Update all four recursive call sites in `core/io/json.cpp` (lines 143, 182, 184, and the new object case) and the top-level call at line 621 to pass it through.

- [ ] **Step 5: Implement the OBJECT case**

In `core/io/json.cpp`, add before `default:` in the `switch (p_var.get_type())`:

```cpp
		case Variant::OBJECT: {
			Object *object = p_var.get_validated_object();
			if (object == nullptr) {
				r_result += "null";
				return;
			}

			if (object_marshaller == nullptr) {
				break; // Falls through to the default quoted to_string.
			}

			const uint64_t object_id = object->get_instance_id();
			if (p_object_markers.has(object_id)) {
				r_result += "\"{...}\"";
				ERR_FAIL_MSG("Converting circular structure to JSON.");
			}

			Variant marshalled;
			p_object_markers.insert(object_id);
			const bool handled = object_marshaller->marshal_object(object, marshalled);
			if (handled) {
				_stringify(r_result, marshalled, p_indent, p_cur_indent, p_sort_keys, p_markers, p_full_precision, p_object_markers);
			}
			p_object_markers.erase(object_id);

			if (handled) {
				return;
			}
		} break; // Not handled: fall through to the default quoted to_string.
```

Place this case so that falling out of it reaches the same code as `default:`. If the surrounding `switch` structure makes fallthrough awkward, extract the `default:` body into a small static helper and call it from both places rather than duplicating the escape logic.

- [ ] **Step 6: Define the static and the setter**

In `core/io/json.cpp`, near the other static definitions:

```cpp
JSONObjectMarshaller *JSON::object_marshaller = nullptr;

void JSON::set_object_marshaller(JSONObjectMarshaller *p_marshaller) {
	object_marshaller = p_marshaller;
}
```

In `JSON::stringify` (line 618), declare the object marker set and pass it:

```cpp
String JSON::stringify(const Variant &p_var, const String &p_indent, bool p_sort_keys, bool p_full_precision) {
	String result;
	HashSet<const void *> markers;
	HashSet<uint64_t> object_markers;
	_stringify(result, p_var, p_indent, 0, p_sort_keys, markers, p_full_precision, object_markers);
	return result;
}
```

- [ ] **Step 7: Register the test header**

In `tests/test_main.cpp`, add:

```cpp
#include "tests/core/io/test_json_marshaller.h"
```

- [ ] **Step 8: Build and run**

Run:
```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j$(sysctl -n hw.ncpu)
./bin/foundry.macos.editor.arm64 --headless test run --case "*JSONMarshal*" --force-colors
```
Expected: `[doctest] Status: SUCCESS!`

- [ ] **Step 9: Run the full suite to confirm no output regressions**

Run: `./bin/foundry.macos.editor.arm64 --headless test run --force-colors`
Expected: `[doctest] Status: SUCCESS!` — existing JSON tests must be untouched.

- [ ] **Step 10: Commit**

```bash
git add core/io/json.h core/io/json.cpp tests/core/io/test_json_marshaller.h tests/test_main.cpp
git commit -m "feat(json): Add object marshaller seam to JSON.stringify"
```

---

### Task 5: Declare the builtin marshalling types

**Goal:** `JsonNode`, `JsonDecodeError`, `JsonResult[T]`, and `JsonSerializable` exist as builtin global types usable from user scripts.

**Blocked by:** Tasks 1 and 3.

**Files:**
- Create: `modules/foundry_script/builtin/json_node.fs`
- Create: `modules/foundry_script/builtin/json_decode_error.fs`
- Create: `modules/foundry_script/builtin/json_result.fs`
- Create: `modules/foundry_script/builtin/json_serializable.fs`
- Modify: `modules/foundry_script/SCsub` (embed the `.fs` sources as string data)
- Modify: `modules/foundry_script/register_types.cpp` (register sources and global classes)
- Create: `modules/foundry_script/tests/scripts/analyzer/features/json_serializable_conformance.fs` + `.out`
- Create: `modules/foundry_script/tests/test_fs_builtin_types.h`
- Modify: `tests/test_main.cpp`

**Acceptance Criteria:**
- [ ] A user script can write `uses JsonSerializable` with no import
- [ ] A witness with the wrong `from_json` return type is rejected with a signature mismatch
- [ ] `JsonNode` case ordinals are exactly `Null=0, Bool=1, Int=2, Float=3, Str=4, Array=5, Object=6`
- [ ] Every registered builtin analyzes with zero errors
- [ ] `JsonNode.of` wraps a plain Variant tree and `push_error`s on an unsupported type

**Verify:** `./bin/foundry.macos.editor.arm64 --headless test run --case "*FSBuiltinTypes*" --force-colors` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing conformance fixture**

`modules/foundry_script/tests/scripts/analyzer/features/json_serializable_conformance.fs`:

```
class_name FixturePlayer extends RefCounted
uses JsonSerializable

var player_name: String
var level: int

func to_json() -> JsonNode:
	return JsonNode.object_of({
		"name": JsonNode.Str(player_name),
		"level": JsonNode.Int(level),
	})

static func from_json(node: JsonNode) -> JsonResult[FixturePlayer]:
	match node:
		JsonNode.Object(var entries):
			if not entries.has("name"):
				return JsonResult[FixturePlayer].fail("missing field", "$.name")
			if not entries.has("level"):
				return JsonResult[FixturePlayer].fail("missing field", "$.level")
			var result := FixturePlayer.new()
			match entries["name"]:
				JsonNode.Str(var value):
					result.player_name = value
				_:
					return JsonResult[FixturePlayer].fail("expected a string", "$.name")
			match entries["level"]:
				JsonNode.Int(var value):
					result.level = value
				_:
					return JsonResult[FixturePlayer].fail("expected an int", "$.level")
			return JsonResult[FixturePlayer].ok(result)
		_:
			return JsonResult[FixturePlayer].fail("expected an object", "$")

func test():
	print(FixturePlayer.new().to_json())
```

- [ ] **Step 2: Run to verify it fails**

Run: `./bin/foundry.macos.editor.arm64 --headless test run --case "*FoundryScript*" --force-colors`
Expected: FAIL — `Could not find type "JsonSerializable"`.

- [ ] **Step 3: Write the builtin declarations**

`modules/foundry_script/builtin/json_node.fs`:

```
enum_name JsonNode:
	Null
	Bool(value: bool)
	Int(value: int)
	Float(value: float)
	Str(value: String)
	Array(items: Array[JsonNode])
	Object(entries: Dictionary[String, JsonNode])

	static func array_of(items: Array[JsonNode]) -> JsonNode:
		return JsonNode.Array(items)

	static func object_of(entries: Dictionary[String, JsonNode]) -> JsonNode:
		return JsonNode.Object(entries)

	static func of(value: Variant) -> JsonNode:
		match typeof(value):
			TYPE_NIL:
				return JsonNode.Null
			TYPE_BOOL:
				return JsonNode.Bool(value)
			TYPE_INT:
				return JsonNode.Int(value)
			TYPE_FLOAT:
				return JsonNode.Float(value)
			TYPE_STRING:
				return JsonNode.Str(value)
			TYPE_ARRAY:
				var items: Array[JsonNode] = []
				for item: Variant in value:
					items.append(JsonNode.of(item))
				return JsonNode.Array(items)
			TYPE_DICTIONARY:
				var entries: Dictionary[String, JsonNode] = {}
				for key: Variant in value:
					entries[str(key)] = JsonNode.of(value[key])
				return JsonNode.Object(entries)
		push_error("JsonNode.of() cannot represent a value of type %d." % typeof(value))
		return JsonNode.Null
```

The case order is the wire contract for the native lowering in Task 7. Do not reorder.

`modules/foundry_script/builtin/json_decode_error.fs`:

```
class_name JsonDecodeError extends RefCounted

var message: String
var path: String

static func create(message: String, path: String) -> JsonDecodeError:
	var error := JsonDecodeError.new()
	error.message = message
	error.path = path
	return error
```

`modules/foundry_script/builtin/json_result.fs`:

```
class_name JsonResult[T] extends RefCounted

var value: T?
var error: JsonDecodeError?

static func ok(value: T) -> JsonResult[T]:
	var result := JsonResult[T].new()
	result.value = value
	return result

static func fail(message: String, path: String) -> JsonResult[T]:
	var result := JsonResult[T].new()
	result.error = JsonDecodeError.create(message, path)
	return result

static func nested(error: JsonDecodeError, key: String) -> JsonResult[T]:
	var child_path := error.path
	if child_path.begins_with("$"):
		child_path = child_path.substr(1)
	return JsonResult[T].fail(error.message, "$." + key + child_path)

func is_ok() -> bool:
	return error == null and value != null
```

`modules/foundry_script/builtin/json_serializable.fs`:

```
trait_name JsonSerializable

abstract func to_json() -> JsonNode
abstract static func from_json(node: JsonNode) -> JsonResult[Self]
```

- [ ] **Step 4: Embed the sources at build time**

In `modules/foundry_script/SCsub`, generate a C++ file that turns each `builtin/*.fs` into a string constant. Follow the existing generated-file pattern in the repo (look at how other modules emit generated sources with `env.CommandNoCache` / a Python builder) and produce a header exposing:

```cpp
struct FSBuiltinSourceEntry {
	const char *path;
	const char *source;
};
extern const FSBuiltinSourceEntry FS_BUILTIN_SOURCES[];
extern const int FS_BUILTIN_SOURCE_COUNT;
```

Paths must be `foundry://builtin/<basename>.fs`. Escape the source text for C++ string literals; do not hand-maintain the escaped copies.

- [ ] **Step 5: Register sources and global classes at module init**

In `modules/foundry_script/register_types.cpp`, in the module initialization for `MODULE_INITIALIZATION_LEVEL_SCENE`:

```cpp
	for (int i = 0; i < FS_BUILTIN_SOURCE_COUNT; i++) {
		FSBuiltinSources::register_source(FS_BUILTIN_SOURCES[i].path, FS_BUILTIN_SOURCES[i].source);
	}

	ScriptServer::add_global_class("JsonNode", StringName(), "FoundryScript",
			"foundry://builtin/json_node.fs", false, false, false, true);
	ScriptServer::add_global_class("JsonDecodeError", "RefCounted", "FoundryScript",
			"foundry://builtin/json_decode_error.fs", false, false, false, false);
	ScriptServer::add_global_class("JsonResult", "RefCounted", "FoundryScript",
			"foundry://builtin/json_result.fs", false, false, false, false);
	ScriptServer::add_global_class("JsonSerializable", StringName(), "FoundryScript",
			"foundry://builtin/json_serializable.fs", false, false, true, false);
```

Note the last two boolean arguments of `add_global_class` are `p_is_trait` and `p_is_enum` — `JsonNode` is an enum, `JsonSerializable` is a trait, the other two are neither.

- [ ] **Step 6: Write the builtin-health test**

`modules/foundry_script/tests/test_fs_builtin_types.h`:

```cpp
#pragma once

#include "modules/foundry_script/fs_builtin_sources.h"
#include "modules/foundry_script/fs_analyzer.h"
#include "modules/foundry_script/fs_parser.h"

#include "tests/test_macros.h"

namespace TestFSBuiltinTypes {

TEST_CASE("[FSBuiltinTypes] Every builtin source parses and analyzes cleanly") {
	List<String> paths;
	FSBuiltinSources::get_registered_paths(&paths);
	CHECK_FALSE(paths.is_empty());

	for (const String &path : paths) {
		String source;
		REQUIRE(FSBuiltinSources::get_source(path, source));

		FSParser parser;
		const Error parse_error = parser.parse(source, path, false);
		INFO("builtin source: ", path);
		CHECK(parse_error == OK);

		FSAnalyzer analyzer(&parser);
		const Error analyze_error = analyzer.analyze();
		INFO("builtin source: ", path);
		CHECK(analyze_error == OK);
	}
}

} // namespace TestFSBuiltinTypes
```

Match `FSParser::parse` and `FSAnalyzer`'s actual constructor and method signatures — read `modules/foundry_script/fs_parser.h` and `fs_analyzer.h` and adjust the calls if they differ.

- [ ] **Step 7: Register the test header**

In `tests/test_main.cpp`:

```cpp
#include "modules/foundry_script/tests/test_fs_builtin_types.h"
```

- [ ] **Step 8: Add the negative conformance fixture**

`modules/foundry_script/tests/scripts/analyzer/errors/json_serializable_wrong_from_json_return.fs`:

```
class_name FixtureEnemy extends RefCounted
uses JsonSerializable

func to_json() -> JsonNode:
	return JsonNode.Null

static func from_json(node: JsonNode) -> JsonResult[JsonDecodeError]:
	return JsonResult[JsonDecodeError].fail("wrong", "$")

func test():
	print(FixtureEnemy.new())
```

Expected `.out`: `The function "from_json()" signature does not match required trait method "JsonSerializable.from_json()".`

- [ ] **Step 9: Build and run**

Run:
```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j$(sysctl -n hw.ncpu)
./bin/foundry.macos.editor.arm64 --headless test run --case "*FSBuiltinTypes*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run --case "*FoundryScript*" --force-colors
```
Expected: `[doctest] Status: SUCCESS!` for both.

- [ ] **Step 10: Commit**

```bash
git add modules/foundry_script/builtin/ \
        modules/foundry_script/SCsub \
        modules/foundry_script/register_types.cpp \
        modules/foundry_script/tests/test_fs_builtin_types.h \
        modules/foundry_script/tests/scripts/analyzer/features/json_serializable_conformance.* \
        modules/foundry_script/tests/scripts/analyzer/errors/json_serializable_wrong_from_json_return.* \
        tests/test_main.cpp
git commit -m "feat(foundry_script): Add builtin JSON marshalling types"
```

---

### Task 6: Register the `to_json` script-dispatched hook

**Goal:** `to_json` is declared a script-dispatched hook so a script override does not raise `NATIVE_METHOD_OVERRIDE`, and all native invocation goes through one helper.

**Blocked by:** Task 5.

**Files:**
- Modify: `modules/foundry_script/foundry_script.h` (`FSScriptExtensibleNativeHooks`)
- Create: `modules/foundry_script/fs_json_marshal.h`
- Create: `modules/foundry_script/fs_json_marshal.cpp`

**Background.** The repo's script-extensible-native-API rule requires that user-overridable hooks be invoked through `Object::call`, be listed in `FSScriptExtensibleNativeHooks`, and have exactly one native invoker rather than scattered `call()` sites.

**Acceptance Criteria:**
- [ ] `to_json` is listed in `FSScriptExtensibleNativeHooks`
- [ ] A script subclass overriding `to_json` produces no analyzer warning
- [ ] An unrelated native method override still warns
- [ ] One helper, `FSJsonMarshal::call_to_json`, is the only native caller of `to_json`

**Verify:** `./bin/foundry.macos.editor.arm64 --headless test run --case "*FoundryScript*" --force-colors` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Locate the hook registry**

```sh
grep -rn "FSScriptExtensibleNativeHooks" modules/foundry_script/
```
Read the surrounding code to learn the exact registration form before editing.

- [ ] **Step 2: Register the hook**

Add `to_json` to `FSScriptExtensibleNativeHooks` following the existing entries' form exactly.

- [ ] **Step 3: Create the single invoker**

`modules/foundry_script/fs_json_marshal.h`:

```cpp
#pragma once

#include "core/object/object.h"
#include "core/variant/variant.h"

// `to_json` is script-dispatched: a Foundry Script class overrides it, so native callers must
// go through Object::call rather than C++ virtual dispatch. This is the only native call site.
class FSJsonMarshal {
public:
	static bool call_to_json(Object *p_object, Variant &r_node);
};
```

`modules/foundry_script/fs_json_marshal.cpp`:

```cpp
#include "fs_json_marshal.h"

bool FSJsonMarshal::call_to_json(Object *p_object, Variant &r_node) {
	ERR_FAIL_NULL_V(p_object, false);

	Callable::CallError call_error;
	r_node = p_object->callp(SNAME("to_json"), nullptr, 0, call_error);
	if (call_error.error != Callable::CallError::CALL_OK) {
		ERR_PRINT(vformat("Calling to_json() on \"%s\" failed.", p_object->get_class()));
		return false;
	}
	return true;
}
```

- [ ] **Step 4: Add the override-warning fixture**

`modules/foundry_script/tests/scripts/analyzer/features/json_serializable_override_no_warning.fs`:

```
class_name FixtureBase extends RefCounted
uses JsonSerializable

func to_json() -> JsonNode:
	return JsonNode.Null

static func from_json(node: JsonNode) -> JsonResult[FixtureBase]:
	return JsonResult[FixtureBase].ok(FixtureBase.new())

func test():
	print(to_json())
```

Expected `.out`: no `NATIVE_METHOD_OVERRIDE` warning.

- [ ] **Step 5: Build and run**

Run:
```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j$(sysctl -n hw.ncpu)
./bin/foundry.macos.editor.arm64 --headless test run --case "*FoundryScript*" --force-colors
```
Expected: `[doctest] Status: SUCCESS!`

- [ ] **Step 6: Commit**

```bash
git add modules/foundry_script/foundry_script.h \
        modules/foundry_script/fs_json_marshal.h \
        modules/foundry_script/fs_json_marshal.cpp \
        modules/foundry_script/tests/scripts/analyzer/features/json_serializable_override_no_warning.*
git commit -m "feat(foundry_script): Register to_json as a script-dispatched hook"
```

---

### Task 7: Module-side encode handler

**Goal:** `JSON.stringify()` marshals a conforming Foundry Script object through its `to_json()`.

**Blocked by:** Tasks 4, 5, 6.

**Files:**
- Modify: `modules/foundry_script/fs_json_marshal.h` / `.cpp` (add the marshaller)
- Modify: `modules/foundry_script/register_types.cpp` (register/unregister with `JSON`)
- Create: `modules/foundry_script/tests/scripts/runtime/json_stringify_conforming.fs` + `.out`

**Acceptance Criteria:**
- [ ] `JSON.stringify(player)` produces the class's `to_json` representation
- [ ] A conforming object nested in a Dictionary or Array is marshalled
- [ ] A non-conforming object is unchanged
- [ ] A malformed `JsonNode` array `push_error`s and yields `null`
- [ ] Lowering enforces a depth limit mirroring `MAX_RECURSION_DEPTH`

**Verify:** `./bin/foundry.macos.editor.arm64 --headless test run --case "*FoundryScript*" --force-colors` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing runtime fixture**

`modules/foundry_script/tests/scripts/runtime/json_stringify_conforming.fs`:

```
class_name MarshalPlayer extends RefCounted
uses JsonSerializable

var player_name: String
var level: int

func to_json() -> JsonNode:
	return JsonNode.object_of({
		"level": JsonNode.Int(level),
		"name": JsonNode.Str(player_name),
	})

static func from_json(node: JsonNode) -> JsonResult[MarshalPlayer]:
	return JsonResult[MarshalPlayer].fail("not needed in this fixture", "$")

func test():
	var player := MarshalPlayer.new()
	player.player_name = "Captain"
	player.level = 3
	print(JSON.stringify(player))
	print(JSON.stringify([player]))
	print(JSON.stringify({"hero": player}))
```

Expected output:
```
{"level":3,"name":"Captain"}
[{"level":3,"name":"Captain"}]
{"hero":{"level":3,"name":"Captain"}}
```

- [ ] **Step 2: Run to verify it fails**

Run: `./bin/foundry.macos.editor.arm64 --headless test run --case "*FoundryScript*" --force-colors`
Expected: FAIL — output is the quoted `to_string`.

- [ ] **Step 3: Add the marshaller declaration**

Append to `modules/foundry_script/fs_json_marshal.h`:

```cpp
#include "core/io/json.h"

class FSJsonObjectMarshaller : public JSONObjectMarshaller {
	// Tag ordinals are fixed by `modules/foundry_script/builtin/json_node.fs` and must stay in
	// sync with it. Values arrive as `[tag, payload...]` read-only arrays.
	enum Tag {
		TAG_NULL = 0,
		TAG_BOOL = 1,
		TAG_INT = 2,
		TAG_FLOAT = 3,
		TAG_STR = 4,
		TAG_ARRAY = 5,
		TAG_OBJECT = 6,
	};

	static bool _lower(const Variant &p_node, Variant &r_result, int p_depth, const String &p_class_name);

public:
	virtual bool marshal_object(Object *p_object, Variant &r_result) override;
};
```

- [ ] **Step 4: Implement the marshaller**

Append to `modules/foundry_script/fs_json_marshal.cpp`:

```cpp
#include "core/object/script_language.h"

bool FSJsonObjectMarshaller::_lower(const Variant &p_node, Variant &r_result, int p_depth, const String &p_class_name) {
	if (p_depth > Variant::MAX_RECURSION_DEPTH) {
		ERR_PRINT(vformat("JsonNode tree from \"%s\" is too deep.", p_class_name));
		return false;
	}

	if (p_node.get_type() != Variant::ARRAY) {
		ERR_PRINT(vformat("to_json() on \"%s\" did not return a JsonNode.", p_class_name));
		return false;
	}

	const Array node = p_node;
	if (node.is_empty() || node[0].get_type() != Variant::INT) {
		ERR_PRINT(vformat("to_json() on \"%s\" returned a malformed JsonNode.", p_class_name));
		return false;
	}

	const int tag = node[0];
	// Each case checks its own payload arity: a mismatch means stale bytecode disagreeing
	// with the builtin declaration, which must be reported rather than trusted.
	switch (tag) {
		case TAG_NULL: {
			r_result = Variant();
			return true;
		}
		case TAG_BOOL:
		case TAG_INT:
		case TAG_FLOAT:
		case TAG_STR: {
			if (node.size() != 2) {
				break;
			}
			r_result = node[1];
			return true;
		}
		case TAG_ARRAY: {
			if (node.size() != 2 || node[1].get_type() != Variant::ARRAY) {
				break;
			}
			const Array items = node[1];
			Array lowered;
			for (const Variant &item : items) {
				Variant lowered_item;
				if (!_lower(item, lowered_item, p_depth + 1, p_class_name)) {
					return false;
				}
				lowered.push_back(lowered_item);
			}
			r_result = lowered;
			return true;
		}
		case TAG_OBJECT: {
			if (node.size() != 2 || node[1].get_type() != Variant::DICTIONARY) {
				break;
			}
			const Dictionary entries = node[1];
			Dictionary lowered;
			for (const Variant &key : entries.get_key_list()) {
				Variant lowered_value;
				if (!_lower(entries[key], lowered_value, p_depth + 1, p_class_name)) {
					return false;
				}
				lowered[String(key)] = lowered_value;
			}
			r_result = lowered;
			return true;
		}
		default:
			break;
	}

	ERR_PRINT(vformat("to_json() on \"%s\" returned a JsonNode with an unknown tag or wrong payload arity (tag %d).", p_class_name, tag));
	return false;
}

bool FSJsonObjectMarshaller::marshal_object(Object *p_object, Variant &r_result) {
	ERR_FAIL_NULL_V(p_object, false);

	ScriptInstance *instance = p_object->get_script_instance();
	if (instance == nullptr) {
		return false;
	}
	Ref<Script> script = instance->get_script();
	if (script.is_null() || !script->has_script_trait(SNAME("JsonSerializable"))) {
		return false;
	}

	Variant node;
	if (!FSJsonMarshal::call_to_json(p_object, node)) {
		r_result = Variant();
		return true; // Handled: the error is reported, and `null` is written rather than junk.
	}
	if (!_lower(node, r_result, 0, p_object->get_class())) {
		r_result = Variant();
	}
	return true;
}
```

- [ ] **Step 5: Register with JSON at module init**

In `modules/foundry_script/register_types.cpp`, add a file-static instance and register it after the builtin sources:

```cpp
static FSJsonObjectMarshaller *fs_json_object_marshaller = nullptr;
```

At init:

```cpp
	fs_json_object_marshaller = memnew(FSJsonObjectMarshaller);
	JSON::set_object_marshaller(fs_json_object_marshaller);
```

At deinit, before `FSBuiltinSources::clear()`:

```cpp
	JSON::set_object_marshaller(nullptr);
	if (fs_json_object_marshaller != nullptr) {
		memdelete(fs_json_object_marshaller);
		fs_json_object_marshaller = nullptr;
	}
```

- [ ] **Step 6: Build and run the fixture**

Run:
```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j$(sysctl -n hw.ncpu)
./bin/foundry.macos.editor.arm64 --headless test run --case "*FoundryScript*" --force-colors
```
Expected: `[doctest] Status: SUCCESS!` with the three expected output lines.

- [ ] **Step 7: Verify the non-conforming case is unchanged**

Add `modules/foundry_script/tests/scripts/runtime/json_stringify_non_conforming.fs`:

```
class_name PlainThing extends RefCounted

func test():
	# A class without the trait keeps the pre-existing quoted to_string representation.
	print(JSON.stringify(PlainThing.new()).begins_with("\""))
```

Expected output: `true`

- [ ] **Step 8: Run the full suite**

Run: `./bin/foundry.macos.editor.arm64 --headless test run --force-colors`
Expected: `[doctest] Status: SUCCESS!`

- [ ] **Step 9: Commit**

```bash
git add modules/foundry_script/fs_json_marshal.h \
        modules/foundry_script/fs_json_marshal.cpp \
        modules/foundry_script/register_types.cpp \
        modules/foundry_script/tests/scripts/runtime/json_stringify_*.
git commit -m "feat(foundry_script): Marshal JsonSerializable objects in JSON.stringify"
```

---

### Task 8: `JSON.parse_to_node` and the decode path

**Goal:** JSON text can be parsed into a `JsonNode` tree and decoded into a typed instance, with errors carrying a path.

**Blocked by:** Task 7.

**Files:**
- Modify: `core/io/json.h` / `core/io/json.cpp` (add `parse_to_node` plumbing on the marshaller interface)
- Modify: `modules/foundry_script/fs_json_marshal.h` / `.cpp` (lifting)
- Create: `modules/foundry_script/tests/scripts/runtime/json_round_trip.fs` + `.out`
- Create: `modules/foundry_script/tests/scripts/runtime/json_decode_errors.fs` + `.out`

**Acceptance Criteria:**
- [ ] `JSON.parse_to_node(text)` returns a `JsonResult[JsonNode]`
- [ ] A syntax error yields a failure carrying the parser's message and line
- [ ] `Player.from_json(node)` round-trips a value encoded by `JSON.stringify`
- [ ] A shape mismatch yields the expected `path`
- [ ] A nested failure propagates its path through the parent

**Verify:** `./bin/foundry.macos.editor.arm64 --headless test run --case "*FoundryScript*" --force-colors` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing round-trip fixture**

`modules/foundry_script/tests/scripts/runtime/json_round_trip.fs`:

```
class_name RoundTripPlayer extends RefCounted
uses JsonSerializable

var player_name: String
var level: int

func to_json() -> JsonNode:
	return JsonNode.object_of({
		"level": JsonNode.Int(level),
		"name": JsonNode.Str(player_name),
	})

static func from_json(node: JsonNode) -> JsonResult[RoundTripPlayer]:
	match node:
		JsonNode.Object(var entries):
			if not entries.has("name"):
				return JsonResult[RoundTripPlayer].fail("missing field", "$.name")
			if not entries.has("level"):
				return JsonResult[RoundTripPlayer].fail("missing field", "$.level")
			var result := RoundTripPlayer.new()
			match entries["name"]:
				JsonNode.Str(var value):
					result.player_name = value
				_:
					return JsonResult[RoundTripPlayer].fail("expected a string", "$.name")
			match entries["level"]:
				JsonNode.Int(var value):
					result.level = value
				_:
					return JsonResult[RoundTripPlayer].fail("expected an int", "$.level")
			return JsonResult[RoundTripPlayer].ok(result)
		_:
			return JsonResult[RoundTripPlayer].fail("expected an object", "$")

func test():
	var original := RoundTripPlayer.new()
	original.player_name = "Captain"
	original.level = 3

	var text := JSON.stringify(original)
	var parsed := JSON.parse_to_node(text)
	print(parsed.is_ok())

	var decoded := RoundTripPlayer.from_json(parsed.value)
	print(decoded.is_ok())
	print(decoded.value.player_name)
	print(decoded.value.level)
```

Expected output:
```
true
true
Captain
3
```

- [ ] **Step 2: Run to verify it fails**

Run: `./bin/foundry.macos.editor.arm64 --headless test run --case "*FoundryScript*" --force-colors`
Expected: FAIL — `parse_to_node` does not exist.

- [ ] **Step 3: Extend the marshaller interface with lifting**

In `core/io/json.h`, add to `JSONObjectMarshaller`:

```cpp
	// Builds the language's JSON tree value from a plain parsed Variant. Returns false if the
	// language cannot represent it, in which case core reports a generic failure.
	virtual bool lift_variant(const Variant &p_parsed, Variant &r_node) = 0;
	// Builds the language's result value for a parse failure.
	virtual bool make_parse_failure(const String &p_message, int p_line, Variant &r_result) = 0;
	// Wraps a successfully lifted tree in the language's success result value.
	virtual bool make_parse_success(const Variant &p_node, Variant &r_result) = 0;
```

- [ ] **Step 4: Implement `JSON::parse_to_node`**

In `core/io/json.h`, in the public section:

```cpp
	static Variant parse_to_node(const String &p_json);
```

In `core/io/json.cpp`:

```cpp
Variant JSON::parse_to_node(const String &p_json) {
	ERR_FAIL_NULL_V_MSG(object_marshaller, Variant(),
			"JSON.parse_to_node() requires a scripting module to provide the JsonNode type.");

	Variant parsed;
	String error_message;
	int error_line = 0;
	const Error error = _parse_string(p_json, parsed, error_message, error_line);

	Variant result;
	if (error != OK) {
		object_marshaller->make_parse_failure(error_message, error_line, result);
		return result;
	}

	Variant node;
	if (!object_marshaller->lift_variant(parsed, node)) {
		object_marshaller->make_parse_failure("Value cannot be represented as a JsonNode.", 0, result);
		return result;
	}

	object_marshaller->make_parse_success(node, result);
	return result;
}
```

Bind it near the existing `stringify` binding at `core/io/json.cpp:634`:

```cpp
	ClassDB::bind_static_method("JSON", D_METHOD("parse_to_node", "json_text"), &JSON::parse_to_node);
```

- [ ] **Step 5: Implement lifting in the module**

Add to `FSJsonObjectMarshaller` in `modules/foundry_script/fs_json_marshal.cpp` the three overrides. Lifting is the inverse of `_lower`: build `[tag, payload]` arrays and `make_read_only()` them, mapping `NIL→TAG_NULL`, `BOOL→TAG_BOOL`, `INT→TAG_INT`, `FLOAT→TAG_FLOAT`, `STRING→TAG_STR`, `ARRAY→TAG_ARRAY`, `DICTIONARY→TAG_OBJECT`, and returning false for anything else. Enforce the same `MAX_RECURSION_DEPTH` limit.

`make_parse_failure` and `make_parse_success` construct a `JsonResult` instance by loading the builtin script at `foundry://builtin/json_result.fs` and calling its `fail`/`ok` static through `Object::call`, exactly as `call_to_json` does for the instance hook. Do not construct the object by hand — going through the script keeps one source of truth for the type's shape.

- [ ] **Step 6: Write the decode-error fixture**

`modules/foundry_script/tests/scripts/runtime/json_decode_errors.fs`:

```
class_name DecodeErrorPlayer extends RefCounted
uses JsonSerializable

var level: int

func to_json() -> JsonNode:
	return JsonNode.object_of({"level": JsonNode.Int(level)})

static func from_json(node: JsonNode) -> JsonResult[DecodeErrorPlayer]:
	match node:
		JsonNode.Object(var entries):
			if not entries.has("level"):
				return JsonResult[DecodeErrorPlayer].fail("missing field", "$.level")
			match entries["level"]:
				JsonNode.Int(var value):
					var result := DecodeErrorPlayer.new()
					result.level = value
					return JsonResult[DecodeErrorPlayer].ok(result)
				_:
					return JsonResult[DecodeErrorPlayer].fail("expected an int", "$.level")
		_:
			return JsonResult[DecodeErrorPlayer].fail("expected an object", "$")

func test():
	var wrong_type := JSON.parse_to_node('{"level": "three"}')
	var decoded := DecodeErrorPlayer.from_json(wrong_type.value)
	print(decoded.is_ok())
	print(decoded.error.message)
	print(decoded.error.path)

	var missing := JSON.parse_to_node('{}')
	print(DecodeErrorPlayer.from_json(missing.value).error.path)

	var bad_text := JSON.parse_to_node('{not json')
	print(bad_text.is_ok())
```

Expected output:
```
false
expected an int
$.level
$.level
false
```

- [ ] **Step 7: Build and run**

Run:
```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j$(sysctl -n hw.ncpu)
./bin/foundry.macos.editor.arm64 --headless test run --case "*FoundryScript*" --force-colors
```
Expected: `[doctest] Status: SUCCESS!`

- [ ] **Step 8: Run the full suite**

Run: `./bin/foundry.macos.editor.arm64 --headless test run --force-colors`
Expected: `[doctest] Status: SUCCESS!`

- [ ] **Step 9: Commit**

```bash
git add core/io/json.h core/io/json.cpp \
        modules/foundry_script/fs_json_marshal.h \
        modules/foundry_script/fs_json_marshal.cpp \
        modules/foundry_script/tests/scripts/runtime/json_round_trip.* \
        modules/foundry_script/tests/scripts/runtime/json_decode_errors.*
git commit -m "feat(json): Add parse_to_node and typed decoding"
```

---

### Task 9: Documentation

**Goal:** The class reference documents the new API, and the doc-sync pre-commit check passes.

**Blocked by:** Task 8.

**Files:**
- Modify: `doc/classes/JSON.xml`
- Create: `modules/foundry_script/doc_classes/JsonNode.xml`, `JsonResult.xml`, `JsonDecodeError.xml`, `JsonSerializable.xml` (only if the builtin registration surfaces them to the doc generator; verify first)

**Acceptance Criteria:**
- [ ] `JSON.parse_to_node` is documented with a usage example
- [ ] `pre-commit run --all-files` passes, including the generated-doc dry run

**Verify:** `pre-commit run --all-files` → all hooks pass

**Steps:**

- [ ] **Step 1: Check whether builtins surface to the doc generator**

```sh
./bin/foundry.macos.editor.arm64 --headless docs --help
```
Determine whether builtin global classes appear in generated docs. If they do not, skip the `doc_classes` files and note it in the commit message rather than inventing XML the generator will not consume.

- [ ] **Step 2: Document `parse_to_node`**

In `doc/classes/JSON.xml`, add a `<method name="parse_to_node">` entry alongside `stringify`, describing the `JsonResult[JsonNode]` return and referencing `JsonSerializable` for typed decoding. Match the surrounding XML style exactly.

- [ ] **Step 3: Run the doc check**

Run: `pre-commit run --all-files`
Expected: all hooks pass. If the generated-doc dry run reports a diff, apply it rather than hand-editing.

- [ ] **Step 4: Commit**

```bash
git add doc/classes/JSON.xml modules/foundry_script/doc_classes/
git commit -m "docs: Document JSON custom marshalling API"
```

---

## Self-Review

**Spec coverage:** §3.1 → Task 1; §3.2 → Task 3; §4.1-4.4 → Task 5; §4.5 → Tasks 5, 7, 8; §5.1 → Task 4; §5.2 → Tasks 6, 7; §6 → Task 8; §7 encode → Tasks 4, 7; §7 decode → Task 8; §9 → tests within each task; §8 out-of-scope items are correctly absent. The standalone-lint fix (§3.1 item 4) is broken out as Task 2 because it survives Task 1 and needs its own fixture.

**Known risks, flagged rather than hidden:**

- **Task 1 Step 5** (inner-`enum` form) and **Task 3 Step 7** (parser-ref source hook) name the right files but not exact line numbers, because the correct insertion point depends on code the implementer must read first. Both steps say so explicitly and name what to look for. An implementer should expect to spend real time reading before editing there.
- **Task 5 Step 4** (SCons source embedding) follows an existing repo pattern that must be located before writing; the plan does not invent a builder.
- **Task 8 Step 5** describes lifting and result construction rather than giving complete code, because the exact `Object::call` form for a static on a builtin script depends on the script-loading API. This is the least mechanical step in the plan.
