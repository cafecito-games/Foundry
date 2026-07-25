# Whole-Program Name Analysis Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a conservative, deterministic, whole-program classification and rename-map contract for compiled Foundry Script identifiers.

**Architecture:** A TOOLS-only `FSNameManglerAnalysis` utility walks an explicitly supplied closed set of compiled root scripts and their nested classes. Typed keep evidence supplies facts owned by later resource and keep-rule stages; the result is a sorted classification list, stable keep diagnostics, and a collision-free ordered map. The component reads compiled state through narrow friend access and never mutates it.

**Tech Stack:** C++17, Godot core `Vector`/`HashMap`/`HashSet`/`RBMap`, Foundry Script compiled graph, doctest.

---

### Task 1: Lock the public contract with a failing classification test

**Files:**
- Create: `modules/foundry_script/tests/test_name_mangler_analysis.h`
- Create: `modules/foundry_script/fs_name_mangler_analysis.h`

- [ ] **Step 1: Write the failing test**

Create a focused test that compiles a real script and requests scene/resource evidence:

```cpp
TEST_CASE("[FoundryScript][NameManglerAnalysis] Classifies a compiled project conservatively") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"extends Node\n"
			"@export var scene_property: int\n"
			"var private_member: int\n"
			"signal scene_signal\n"
			"signal private_signal\n"
			"enum Mode { IDLE, ACTIVE }\n"
			"class PrivateNested:\n"
			"\tvar nested_member: int\n"
			"@rpc func remote_call() -> void:\n"
			"\tpass\n"
			"func _process(_delta: double) -> void:\n"
			"\tpass\n"
			"func scene_handler() -> void:\n"
			"\tpass\n"
			"func string_named() -> void:\n"
			"\tpass\n"
			"func private_helper() -> void:\n"
			"\tprivate_member += 1\n"
			"func remember_name() -> String:\n"
			"\treturn \"string_named\"\n");

	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(script);
	input.add_keep(SNAME("scene_signal"), FSNameManglerAnalysis::KEEP_SCENE_OR_RESOURCE,
			"res://main.tscn connection signal");
	input.add_keep(SNAME("scene_handler"), FSNameManglerAnalysis::KEEP_SCENE_OR_RESOURCE,
			"res://main.tscn connection method");

	const FSNameManglerAnalysis::Result result = FSNameManglerAnalysis::analyze(input);
	REQUIRE(result.error == OK);
	CHECK(result.rename_map.has(SNAME("private_member")));
	CHECK(result.rename_map.has(SNAME("private_helper")));
	CHECK(result.rename_map.has(SNAME("private_signal")));
	CHECK(result.rename_map.has(SNAME("PrivateNested")));
	CHECK(result.rename_map.has(SNAME("Mode")));
	CHECK_FALSE(result.rename_map.has(SNAME("scene_property")));
	CHECK_FALSE(result.rename_map.has(SNAME("scene_signal")));
	CHECK_FALSE(result.rename_map.has(SNAME("scene_handler")));
	CHECK_FALSE(result.rename_map.has(SNAME("remote_call")));
	CHECK_FALSE(result.rename_map.has(SNAME("_process")));
	CHECK_FALSE(result.rename_map.has(SNAME("string_named")));
	CHECK(name_analysis_has_reason(result, SNAME("remote_call"), FSNameManglerAnalysis::KEEP_RPC));
	CHECK(name_analysis_has_reason(result, SNAME("_process"), FSNameManglerAnalysis::KEEP_NATIVE_VIRTUAL));
	CHECK(name_analysis_has_reason(result, SNAME("string_named"), FSNameManglerAnalysis::KEEP_STRING_LITERAL));
}
```

- [ ] **Step 2: Run the focused test to verify RED**

Run:

```sh
scons platform=macos target=editor dev_build=yes tests=yes
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*NameManglerAnalysis*" --force-colors
```

Expected: the build fails because `fs_name_mangler_analysis.h` and its contract do not exist yet.

- [ ] **Step 3: Add the declaration-only public contract**

Declare these exact public concepts in `fs_name_mangler_analysis.h`:

```cpp
class FSNameManglerAnalysis {
public:
	enum IdentifierKind {
		IDENTIFIER_CLASS,
		IDENTIFIER_MEMBER,
		IDENTIFIER_METHOD,
		IDENTIFIER_SIGNAL,
		IDENTIFIER_ENUM_OR_CONSTANT,
	};
	enum KeepReason {
		KEEP_SCENE_OR_RESOURCE,
		KEEP_RPC,
		KEEP_NATIVE_VIRTUAL,
		KEEP_STRING_LITERAL,
		KEEP_REFLECTION,
		KEEP_EXTERNAL_OR_UNPROVABLE,
		KEEP_RULE,
	};
	struct KeepEvidence {
		StringName name;
		KeepReason reason = KEEP_EXTERNAL_OR_UNPROVABLE;
		String detail;
	};
	struct Input {
		Vector<Ref<FoundryScript>> scripts;
		Vector<KeepEvidence> keep_evidence;
		bool complete_project_graph = true;
		void add_keep(const StringName &p_name, KeepReason p_reason, const String &p_detail = String());
	};
	struct Classification {
		StringName name;
		Vector<IdentifierKind> kinds;
		Vector<KeepEvidence> keep_evidence;
		StringName replacement;
		bool is_kept() const;
	};
	struct Result {
		Error error = OK;
		Vector<Classification> classifications;
		RBMap<StringName, StringName> rename_map;
		Vector<String> keep_log;
		const Classification *find(const StringName &p_name) const;
	};
	static Result analyze(const Input &p_input);
	static String get_keep_reason_label(KeepReason p_reason);
};
```

- [ ] **Step 4: Rebuild to verify the test now fails at link time**

Run the Step 2 build again.

Expected: unresolved `FSNameManglerAnalysis` implementation symbols, proving the test reaches the wished-for API.

- [ ] **Step 5: Commit the RED test and contract**

```sh
git add modules/foundry_script/fs_name_mangler_analysis.h modules/foundry_script/tests/test_name_mangler_analysis.h
git commit -m "Test whole-program name classification contract"
```

### Task 2: Implement compiled-graph candidate and automatic keep collection

**Files:**
- Create: `modules/foundry_script/fs_name_mangler_analysis.cpp`
- Modify: `modules/foundry_script/foundry_script.h`
- Modify: `modules/foundry_script/fs_function.h`
- Test: `modules/foundry_script/tests/test_name_mangler_analysis.h`

- [ ] **Step 1: Add RED cases for recursive constants and NodePath semantics**

Add a fixture with `"literal_kept"` in a nested Array/Dictionary and
`NodePath("Child:nodepath_property")`. Assert the exact string and NodePath subname keep their
matching declarations, while a node-only `NodePath("private_node_segment")` does not keep a method
of that spelling.

- [ ] **Step 2: Run focused tests and confirm the new assertions fail**

Run:

```sh
scons platform=macos target=editor dev_build=yes tests=yes
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*NameManglerAnalysis*" --force-colors
```

Expected: candidate and evidence assertions fail because the implementation is absent.

- [ ] **Step 3: Implement the minimal graph walker**

Add `friend class FSNameManglerAnalysis;` under `TOOLS_ENABLED` in both compiled graph classes.
Implement a private build state that:

```cpp
struct FSNameManglerAnalysis::BuildState {
	struct Aggregate {
		HashSet<int> kinds;
		Vector<KeepEvidence> evidence;
	};
	HashMap<StringName, Aggregate> candidates;
	HashSet<StringName> observed_names;
	HashSet<String> string_evidence;
	HashSet<const FoundryScript *> included_classes;
	HashSet<const FoundryScript *> visited_classes;
	HashSet<const FSFunction *> visited_functions;
	bool enumerates_methods = false;
	bool enumerates_properties = false;
	bool enumerates_signals = false;
};
```

Index all input roots and nested classes before collection. Collect non-synthetic class, direct
instance/static member, method, signal, constant/enum, enum-host, and nested-class keys. Traverse
function/lambda constants, method defaults, class constants, static/default values, annotation
arguments, and RPC dictionaries. Recursively inspect Arrays, Dictionaries, packed strings,
String/StringName, Callable, Signal, and NodePath subnames with a recursion-depth guard.

Mark exported member annotations, RPC keys, matching native virtuals, native/builtin API collisions,
exact string evidence, and external script surfaces with the corresponding reasons.

- [ ] **Step 4: Produce the sorted classification and deterministic map**

Sort original names lexically. Sort kinds by enum value and keep evidence by `(reason, detail)`,
deduplicate both, and allocate `_fsb_<base36 ordinal>` replacements while skipping all observed
names and engine API collisions. Emit one stable keep-log line for every evidence record.

- [ ] **Step 5: Run focused tests to verify GREEN**

Run the Step 2 commands.

Expected: all `NameManglerAnalysis` cases pass.

- [ ] **Step 6: Commit the collector**

```sh
git add modules/foundry_script/fs_name_mangler_analysis.* modules/foundry_script/foundry_script.h \
  modules/foundry_script/fs_function.h modules/foundry_script/tests/test_name_mangler_analysis.h
git commit -m "Analyze compiled project names for mangling"
```

### Task 3: Prove whole-program conservatism and determinism

**Files:**
- Modify: `modules/foundry_script/fs_name_mangler_analysis.cpp`
- Modify: `modules/foundry_script/tests/test_name_mangler_analysis.h`

- [ ] **Step 1: Add RED tests for reflection declaration sets**

Compile three fixtures that invoke `get_method_list`, `get_property_list`, and `get_signal_list`.
Assert method enumeration keeps all candidate methods but not unrelated members/signals; property
enumeration keeps members but not methods/signals; signal enumeration keeps signals but not
methods/members. Verify `KEEP_REFLECTION` appears on the kept declaration, not only on the native
enumerator method.

- [ ] **Step 2: Add RED tests for project-wide decisions**

Analyze two roots in both orders. Give one occurrence of a shared spelling `KEEP_RULE` evidence and
assert every occurrence is kept. Remove the evidence, then assert both orders yield identical
ordered maps, replacements, classifications, and keep logs. Include a source declaration named
`_fsb_0` and assert generated names skip it.

- [ ] **Step 3: Add RED tests for incomplete/external graphs**

Set `complete_project_graph = false` and assert every candidate is kept with
`KEEP_EXTERNAL_OR_UNPROVABLE`. Add an external script value or base not present in `Input.scripts`
and assert colliding surface names receive the same reason.

- [ ] **Step 4: Run focused tests to confirm RED**

Run:

```sh
scons platform=macos target=editor dev_build=yes tests=yes
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*NameManglerAnalysis*" --force-colors
```

Expected: the new reflection, collision, or external-boundary assertion fails.

- [ ] **Step 5: Implement relevant-set reflection and boundary handling**

Classify enumeration APIs into method/property/signal flags while walking actual function global
names and method-bind fixups. Apply `KEEP_REFLECTION` only to classifications carrying the matching
kind. Collect external script member/method/signal/class/enum surfaces without adding them as
candidates; matching internal spellings receive `KEEP_EXTERNAL_OR_UNPROVABLE`.

- [ ] **Step 6: Run focused tests to verify GREEN**

Run the Step 4 commands.

Expected: all focused cases pass with zero failures.

- [ ] **Step 7: Commit the conservatism coverage**

```sh
git add modules/foundry_script/fs_name_mangler_analysis.cpp \
  modules/foundry_script/tests/test_name_mangler_analysis.h
git commit -m "Cover conservative name analysis boundaries"
```

### Task 4: Verify the contract in relevant and strict builds

**Files:**
- Modify: `modules/foundry_script/fs_bytecode_format.h`

- [ ] **Step 1: Update the residual-information comment**

State that the analysis/map contract now exists while graph mutation and export integration remain
follow-ups, so the format's current serialization behavior is unchanged.

- [ ] **Step 2: Run formatting checks**

Run:

```sh
clang-format -i modules/foundry_script/fs_name_mangler_analysis.h \
  modules/foundry_script/fs_name_mangler_analysis.cpp \
  modules/foundry_script/tests/test_name_mangler_analysis.h
git diff --check
```

Expected: no formatting or whitespace errors.

- [ ] **Step 3: Run focused and broader tests**

Run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*NameManglerAnalysis*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*Bytecode*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*FoundryScript*" --force-colors
```

Expected: doctest reports zero failed test cases for each filter.

- [ ] **Step 4: Run the final warnings-as-errors build**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes
```

Expected: SCons exits `0`.

- [ ] **Step 5: Re-run focused tests with the final binary**

Run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*NameManglerAnalysis*" --force-colors
```

Expected: doctest reports success and zero failures.

- [ ] **Step 6: Commit the verified implementation**

```sh
git add modules/foundry_script/fs_bytecode_format.h
git commit -m "Document name analysis boundary"
```
