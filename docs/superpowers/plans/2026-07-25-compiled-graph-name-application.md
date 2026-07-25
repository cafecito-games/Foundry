# Compiled-Graph Name Application Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an atomic, rollback-safe compiled-graph stage that applies one whole-project name map
to every Foundry Script root before unchanged `.fsb` serialization.

**Architecture:** A TOOLS-only `FSNameManglerApplication::Transaction` validates a closed compiled
graph, snapshots all mutable serialized surfaces and conformance entries, stages one map across all
roots, and restores the exact editor graph from explicit or destructor rollback. The existing
writer/loader remain unchanged; #795/#796 are corrected so class decisions are atomic and composite
identities are rebuilt segment by segment.

**Tech Stack:** C++17, Godot `HashMap`/`HashSet`/`RBMap`/`Vector`/`Variant`, Foundry Script compiled
graph and conformance registry, `.fsb` writer/loader, doctest, SCons.

---

## File map

- Create `modules/foundry_script/fs_name_mangler_application.h`: public diagnostic and scoped
  transaction API.
- Create `modules/foundry_script/fs_name_mangler_application.cpp`: graph indexing, preflight,
  snapshots, rewrite helpers, staging, and rollback.
- Modify `modules/foundry_script/foundry_script.h`: TOOLS-only friend declaration for the
  application transaction.
- Modify `modules/foundry_script/fs_function.h`: TOOLS-only friend declaration for function
  metadata and runtime-pointer staging.
- Create `modules/foundry_script/tests/test_name_mangler_application.h`: lifecycle, surface,
  protected-name, closure, parameter, conformance, runtime, serialization, leak, and deterministic
  coverage.
- Modify `modules/foundry_script/tests/test_name_mangler_analysis.h`: enforce atomic class
  candidates and structured-identity reservation.
- Modify `modules/foundry_script/tests/test_name_mangler_keep_rules.h`: class keep evidence targets
  the atomic candidate only.
- Modify `modules/foundry_script/fs_name_mangler_analysis.cpp`: stop classifying registered-global
  composites and update class keep evidence.
- Modify `modules/foundry_script/fs_name_mangler_keep_rules.cpp`: emit class-rule evidence only for
  the atomic class candidate.
- Modify `modules/foundry_script/fs_name_mangler_analysis.h`: sharpen the atomic-class contract.
- Modify `docs/superpowers/specs/2026-07-25-whole-program-name-analysis-design.md`: correct the merged
  #795 global-name ambiguity.
- Modify `docs/superpowers/specs/2026-07-25-name-mangler-keep-escapes-design.md`: describe atomic
  class evidence.
- Modify `modules/foundry_script/fs_bytecode_format.h`: mark application complete while keeping the
  wire format unchanged.

### Task 1: Capture both prerequisite REDs

**Files:**
- Modify: `modules/foundry_script/tests/test_name_mangler_analysis.h`
- Create: `modules/foundry_script/tests/test_name_mangler_application.h`

- [ ] **Step 1: Change the namespaced-class analysis assertion first**

Replace the flat-qualified candidate assertions with:

```cpp
TEST_CASE("[FoundryScript][NameManglerAnalysis] Class candidates are atomic and identities stay structured") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"namespace name_analysis\n"
			"class_name QualifiedCandidate\n"
			"class NestedCandidate[T]:\n"
			"\tpass\n");
	const Ref<FoundryScript> nested = script->get_subclasses()[SNAME("NestedCandidate")];

	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(script);
	const FSNameManglerAnalysis::Result result = FSNameManglerAnalysis::analyze(input);

	REQUIRE_EQ(result.error, OK);
	CHECK(result.rename_map.has(SNAME("QualifiedCandidate")));
	CHECK(result.rename_map.has(SNAME("NestedCandidate")));
	CHECK(result.find(SNAME("name_analysis.QualifiedCandidate")) == nullptr);
	CHECK(result.find(StringName(nested->get_fully_qualified_name())) == nullptr);
	CHECK(result.find(SNAME("T")) == nullptr);
}
```

- [ ] **Step 2: Build and run the focused analysis case to verify behavioral RED**

Run:

```sh
scons platform=macos target=editor dev_build=yes tests=yes -j8
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerAnalysis*Class candidates are atomic*" --force-colors
```

Expected: the assertion that `name_analysis.QualifiedCandidate` is not classified fails because the
merged #795 implementation still emits that composite key.

- [ ] **Step 3: Add the wished-for missing application API test**

Create the test header with:

```cpp
#pragma once

#ifdef TOOLS_ENABLED

#include "modules/foundry_script/fs_name_mangler_application.h"
#include "modules/foundry_script/tests/test_bytecode_serialization.h"

#include "tests/test_macros.h"

namespace FSTests {

TEST_CASE("[FoundryScript][NameManglerApplication] Scoped transaction exposes lifecycle") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"var private_marker_member: int\n"
			"func private_marker_method(value: int) -> int:\n"
			"\tprivate_marker_member += value\n"
			"\treturn private_marker_member\n");
	FSNameManglerAnalysis::Input analysis_input;
	analysis_input.scripts.push_back(script);
	const FSNameManglerAnalysis::Result analysis = FSNameManglerAnalysis::analyze(analysis_input);

	FSNameManglerApplication::Transaction transaction;
	Vector<FSNameManglerApplication::Diagnostic> diagnostics;
	CHECK_EQ(transaction.get_state(), FSNameManglerApplication::Transaction::STATE_UNUSED);
	CHECK_EQ(transaction.begin(analysis_input.scripts, analysis.rename_map, diagnostics), OK);
	CHECK(transaction.is_active());
	transaction.rollback();
	CHECK_FALSE(transaction.is_active());
	CHECK_EQ(transaction.get_state(), FSNameManglerApplication::Transaction::STATE_FINISHED);
}

} // namespace FSTests

#endif // TOOLS_ENABLED
```

- [ ] **Step 4: Build to verify the missing-header/API compile RED**

Run:

```sh
scons platform=macos target=editor dev_build=yes tests=yes -j8
```

Expected: compilation fails because `fs_name_mangler_application.h` does not exist.

- [ ] **Step 5: Report both exact RED diagnostics**

Record the failed doctest assertion from Step 2 and the absent-header compiler diagnostic from Step
4 before creating production files.

### Task 2: Correct the atomic analysis contract and establish lifecycle GREEN

**Files:**
- Create: `modules/foundry_script/fs_name_mangler_application.h`
- Create: `modules/foundry_script/fs_name_mangler_application.cpp`
- Modify: `modules/foundry_script/fs_name_mangler_analysis.cpp`
- Modify: `modules/foundry_script/fs_name_mangler_keep_rules.cpp`
- Modify: `modules/foundry_script/foundry_script.h`
- Modify: `modules/foundry_script/fs_function.h`
- Test: `modules/foundry_script/tests/test_name_mangler_analysis.h`
- Test: `modules/foundry_script/tests/test_name_mangler_keep_rules.h`
- Test: `modules/foundry_script/tests/test_name_mangler_application.h`

- [ ] **Step 1: Make #795/#796 class evidence atomic**

In `_collect_class`, retain:

```cpp
_add_candidate(p_class->local_name, IDENTIFIER_CLASS, r_state);
r_state.observed_names.insert(p_class->global_name);
r_state.observed_names.insert(StringName(p_class->fully_qualified_name));
```

For `@keep_name` class evidence, add only `p_class->local_name`. In
`FSNameManglerKeepRules::apply_to_input`, add class evidence only for `local_name`; the canonical
FQCN remains the rule-match input and diagnostic detail.

Update the keep-rules test to assert:

```cpp
CHECK(name_analysis_has_reason(result, SNAME("KeptRoot"), FSNameManglerAnalysis::KEEP_RULE));
CHECK(result.find(SNAME("keep_rules.KeptRoot")) == nullptr);
```

- [ ] **Step 2: Declare the single-use transaction**

Add the exact public API from the design, with deleted copy/move operations. Forward-declare
`FSNameManglerApplication` under `TOOLS_ENABLED` and friend it from `FoundryScript` and `FSFunction`.

The header's private state contains:

```cpp
	State state = STATE_UNUSED;
	Vector<ClassSnapshot> class_snapshots;
	Vector<FunctionSnapshot> function_snapshots;
	Vector<RegistrySnapshot> registry_snapshots;
```

- [ ] **Step 3: Implement minimal state behavior**

Implement `Diagnostic::format()`, constructor/destructor, `is_active`, `get_state`, single-use
rejection, and idempotent rollback. For this slice, `begin()` validates non-null unique root scripts,
captures their class identities, sets `STATE_ACTIVE`, and rollback restores them.

- [ ] **Step 4: Build and run lifecycle plus analysis/keep-rules tests**

Run:

```sh
scons platform=macos target=editor dev_build=yes tests=yes -j8
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerApplication*Scoped transaction exposes lifecycle*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerAnalysis*Class candidates are atomic*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerKeepRules*" --force-colors
```

Expected: all three filters pass.

- [ ] **Step 5: Commit the contract slice**

```sh
git add modules/foundry_script docs/superpowers
git commit -m "Define atomic compiled graph rename staging"
```

### Task 3: Rewrite and restore every class/function surface

**Files:**
- Modify: `modules/foundry_script/fs_name_mangler_application.cpp`
- Modify: `modules/foundry_script/tests/test_name_mangler_application.h`

- [ ] **Step 1: Add exhaustive staged-surface and rollback RED**

Compile a root with a namespace, nested generic class, instance/static members, custom accessors,
signal, constant/named enum and enum-host method, abstract trait requirement, annotations, a private
method with a lambda, and a kept method. Assert while active:

```cpp
CHECK(script->debug_get_member_indices().has(analysis.rename_map[SNAME("private_marker_member")]));
CHECK_FALSE(script->debug_get_member_indices().has(SNAME("private_marker_member")));
CHECK(script->get_member_functions().has(analysis.rename_map[SNAME("private_marker_method")]));
CHECK(script->get_signals().has(analysis.rename_map[SNAME("private_marker_signal")]));
CHECK(script->get_subclasses().has(analysis.rename_map[SNAME("PrivateMarkerNested")]));
CHECK(script->get_fully_qualified_name().contains(
		String(analysis.rename_map[SNAME("PrivateMarkerRoot")])));
```

Serialize a baseline before begin, roll back, serialize again, and require `baseline == restored`.
Also check every public accessor returns the original key/name after rollback.

- [ ] **Step 2: Run the test to verify RED**

Run:

```sh
scons platform=macos target=editor dev_build=yes tests=yes -j8
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerApplication*rewrites serialized surfaces and rolls back*" --force-colors
```

Expected: the first renamed map/key assertion fails because only lifecycle state exists.

- [ ] **Step 3: Implement identity and container rewrite helpers**

Implement:

```cpp
static StringName rename_atomic(const StringName &p_name, const RBMap<StringName, StringName> &p_map);
static String rewrite_identity(const String &p_identity, const RBMap<StringName, StringName> &p_map,
		const HashSet<String> &p_resource_paths);
static void rewrite_property_info(PropertyInfo &r_info, const RewritePlan &p_plan);
static void rewrite_data_type(FSDataType &r_type, const RewritePlan &p_plan);
static void rewrite_method_info(MethodInfo &r_info, bool p_safe_arguments,
		HashMap<StringName, StringName> *r_parameter_map, const RewritePlan &p_plan);
```

`rewrite_identity` preserves a resource-path prefix through the first `::` and rewrites only valid
identifier tokens elsewhere. `rewrite_property_info` rewrites project class/type identities only in
`class_name` and type-bearing hint grammars, never enum labels or arbitrary hints.

- [ ] **Step 4: Snapshot and rewrite class maps**

Snapshot every field listed in the design, rebuild each key container from the saved value, and
update mirrored `PropertyInfo.name`, member setter/getter names, recursive types, class identities,
subclass keys, constants/enums, signals, abstract requirements, annotation maps, and default maps.

- [ ] **Step 5: Snapshot and rewrite all functions**

Visit member, enum-host, implicit, witness, and lambda functions once. Rewrite project names,
`MethodInfo`, recursive types, and mapped/known-identity `global_names`; leave every fixup and
builtin-name table untouched. Call `setup_runtime_pointers()` after staging and restoration.

- [ ] **Step 6: Rerun exhaustive surface test to verify GREEN**

Run the Step 2 commands.

Expected: staged assertions pass and the pre/post rollback buffers are byte-identical.

### Task 4: Enforce argument safety and protected-name preflight

**Files:**
- Modify: `modules/foundry_script/fs_name_mangler_application.cpp`
- Modify: `modules/foundry_script/tests/test_name_mangler_application.h`

- [ ] **Step 1: Add argument-policy RED**

Compile private, reflected/string-kept, RPC, native virtual, signal, abstract requirement, witness,
and lambda signatures. Assert:

```cpp
CHECK(private_info.arguments[0].name.begins_with("_fsb_arg_"));
CHECK(signal_info.arguments[0].name.begins_with("_fsb_arg_"));
CHECK(kept_info.arguments[0].name == SNAME("kept_parameter_marker"));
CHECK(rpc_info.arguments[0].name == SNAME("rpc_parameter_marker"));
CHECK(lambda_info.arguments[0].name.begins_with("_fsb_arg_"));
```

Include an original `_fsb_arg_0` parameter and require all generated signature names to remain
unique. Assert matching parameter-annotation inner keys follow the safe signature map only.

- [ ] **Step 2: Add protected-map RED**

Copy a valid map and insert sources found in a Variant getter/builtin method, ClassDB method bind,
utility, global store/autoload, RPC key, and resource path. For each case require:

```cpp
CHECK_NE(transaction.begin(roots, invalid_map, diagnostics), OK);
CHECK_FALSE(transaction.is_active());
CHECK_EQ(transaction.get_state(), Transaction::STATE_FINISHED);
CHECK_EQ(serialize_script(script), baseline);
CHECK(diagnostics[0].format().contains("protected"));
```

- [ ] **Step 3: Run both filters to verify RED**

Run:

```sh
scons platform=macos target=editor dev_build=yes tests=yes -j8
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerApplication*argument names*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerApplication*protected*" --force-colors
```

Expected: private/lambda arguments remain original or invalid protected maps are accepted.

- [ ] **Step 4: Implement deterministic per-signature allocation**

For safe owners and every lambda, reserve all original argument names, then allocate
`_fsb_arg_<base36 ordinal>` in argument order while skipping reserved/selected names. Apply the same
mapping to parameter annotation keys. Kept owners do not allocate or rewrite.

- [ ] **Step 5: Implement stable protected-surface scanning**

Before snapshots or mutation, scan RPC dictionaries, native/Variant fixups, MethodBind keys,
utilities, global stores, named globals, builtin names, native class/trait identities, and resource
paths. If a source-map key occurs, append a surface-specific diagnostic and fail. Sort diagnostics by
source and surface.

- [ ] **Step 6: Run both filters to verify GREEN**

Run the Step 3 commands.

Expected: both filters pass and no validation failure changes serialized bytes.

### Task 5: Validate closed roots and restore conformance registry state

**Files:**
- Modify: `modules/foundry_script/fs_name_mangler_application.cpp`
- Modify: `modules/foundry_script/tests/test_name_mangler_application.h`

- [ ] **Step 1: Add omitted-dependency RED**

Compile separate base, subclass, preload/user, trait, and conformance-declaring roots. Omit each
referenced root in turn. Require `ERR_INVALID_PARAMETER`, a diagnostic naming the referring class
and omitted path/FQCN, inactive finished state, baseline bytes, and unchanged registry entries.

- [ ] **Step 2: Add conformance staging/rollback RED**

With all roots supplied, capture `get_runtime_witnesses(source)`. While active, assert transformed
target aliases, trait name, witness key/function metadata, and working
`find_witness_function(transformed_target, transformed_method)`. After rollback require the saved
entry vector and original dispatch key/function to be restored exactly.

- [ ] **Step 3: Run focused closure/conformance cases to verify RED**

Run:

```sh
scons platform=macos target=editor dev_build=yes tests=yes -j8
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerApplication*closed graph*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerApplication*conformance registry*" --force-colors
```

Expected: omitted roots are accepted or registry keys remain unstaged.

- [ ] **Step 4: Implement reference closure traversal**

Traverse bases, subclasses, recursive Variants, specialized handles, `FSDataType`, type bindings,
ancestor keys, witness target scripts, witness function targets, trait identities, and conformance
identities. Convert each Foundry Script reference to its root and require it in the supplied-root
set; non-Foundry external resources remain allowed.

- [ ] **Step 5: Implement registry snapshot/staging/restore**

For each unique non-empty `registered_conformance_source`, save the full vector, build a transformed
copy using known included identities and witness maps, and register it only after all validation.
Rollback re-registers the saved vector; empty saved vectors are restored with
`clear_runtime_witnesses`.

- [ ] **Step 6: Rerun closure/conformance filters to verify GREEN**

Run the Step 3 commands.

Expected: both pass with exact registry and buffer restoration.

### Task 6: Prove collision atomicity, determinism, and multi-file runtime parity

**Files:**
- Modify: `modules/foundry_script/tests/test_name_mangler_application.h`
- Modify: `modules/foundry_script/fs_name_mangler_application.cpp`

- [ ] **Step 1: Add collision/state misuse RED**

Cover empty/non-identifier/composite/identity keys, duplicate targets, target-to-observed-name
collisions, missing sources, per-map/FQCN collisions, duplicate/inner/null roots, a second `begin`,
repeated rollback, and destructor rollback. Each failure compares baseline serialization and
registry state.

- [ ] **Step 2: Add the multi-file acceptance fixture**

Use source overrides under `TestUtils::get_temp_path` for:

- a generic base with private/exported members, private method/signal, enum and enum-host function;
- a subclass using a trait and calling inherited surfaces;
- a caller/preloader that instantiates the subclass and invokes it across files; and
- a retroactive conformance with a witness method.

Analyze all roots once. Execute the kept entry point before staging, while staging is active, and
after rollback. Serialize every root while active, load all `.fsb` buffers through a resolver keyed
by unchanged path plus transformed FQCN, and execute the restored caller.

Require equal result/signal behavior in all four modes.

- [ ] **Step 3: Add byte-leak and protected-fixup assertions**

Search every staged buffer as raw bytes/String for original private marker identifiers and require
absence. Require exported, `@keep_name`, RPC, string-dispatch, engine `queue_free`/Variant API, and
resource-path markers to remain. Inspect restored fixup descriptors and run the engine API calls.

- [ ] **Step 4: Add deterministic transaction assertions**

Serialize all roots under a transaction, roll back, repeat with reversed root order, and require
identical per-path buffers. Compare baseline buffers and registry entries after both rollbacks.

- [ ] **Step 5: Run acceptance filters to verify RED**

Run:

```sh
scons platform=macos target=editor dev_build=yes tests=yes -j8
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerApplication*collision*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerApplication*multi-file*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerApplication*deterministic*" --force-colors
```

Expected: at least one new collision, parity, leak, or determinism assertion fails.

- [ ] **Step 6: Complete preflight and rewrite gaps**

Implement the minimal missing validation/rewrite behavior exposed by Step 5. Do not change the
writer/loader format or add export orchestration.

- [ ] **Step 7: Rerun acceptance filters to verify GREEN**

Run the Step 5 commands.

Expected: all application cases pass with zero failed doctest cases.

- [ ] **Step 8: Commit the complete application**

```sh
git add modules/foundry_script docs/superpowers
git commit -m "Apply project name maps to compiled graphs"
```

### Task 7: Align docs, format, and run final verification

**Files:**
- Modify: `modules/foundry_script/fs_name_mangler_analysis.h`
- Modify: `modules/foundry_script/fs_bytecode_format.h`
- Modify: `docs/superpowers/specs/2026-07-25-whole-program-name-analysis-design.md`
- Modify: `docs/superpowers/specs/2026-07-25-name-mangler-keep-escapes-design.md`
- Modify as formatter requires.

- [ ] **Step 1: Update prior contracts**

State explicitly that registered-global/FQCN values are structured identities rebuilt from atomic
class decisions; class keep evidence targets the atomic class declaration. In
`fs_bytecode_format.h`, replace the pending-application note with the scoped-transaction boundary
while stating that the wire format remains version 3 and unchanged.

- [ ] **Step 2: Format and inspect**

Run:

```sh
clang-format -i \
  modules/foundry_script/fs_name_mangler_application.h \
  modules/foundry_script/fs_name_mangler_application.cpp \
  modules/foundry_script/fs_name_mangler_analysis.h \
  modules/foundry_script/fs_name_mangler_analysis.cpp \
  modules/foundry_script/fs_name_mangler_keep_rules.cpp \
  modules/foundry_script/foundry_script.h \
  modules/foundry_script/fs_function.h \
  modules/foundry_script/tests/test_name_mangler_application.h \
  modules/foundry_script/tests/test_name_mangler_analysis.h \
  modules/foundry_script/tests/test_name_mangler_keep_rules.h
git diff --check origin/develop...HEAD
git diff --check
```

Expected: no formatting or whitespace errors.

- [ ] **Step 3: Run focused suites**

Run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerApplication*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerAnalysis*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerKeepRules*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*BytecodeScript*" --force-colors
```

Expected: each filter reports zero failed test cases.

- [ ] **Step 4: Run the broader Foundry Script suite**

Run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*FoundryScript*" --progress-format=jsonl \
  --progress-file /tmp/foundry-issue-797-tests.jsonl --force-colors
```

Read the doctest summary and JSONL `run_end`; expected status is success with zero failed cases.

- [ ] **Step 5: Run the final macOS warnings-as-errors build**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j8
```

Expected: SCons exits `0`.

- [ ] **Step 6: Rerun the application suite with the final binary**

Run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerApplication*" --force-colors
```

Expected: doctest reports success and zero failures.

- [ ] **Step 7: Audit requirements and commit verification/docs**

Re-read the #797 issue and design, map every required surface to a test, inspect
`git diff --stat origin/develop...HEAD`, and commit any final documentation/format changes:

```sh
git add modules/foundry_script docs/superpowers
git commit -m "Document compiled graph name application"
```

### Task 8: Independent gates and delivery

**Files:**
- Modify only for technically validated review findings.

- [ ] **Step 1: Send committed HEAD and verification to the parent**

Wait for the independent spec-compliance gate before starting Cursor.

- [ ] **Step 2: Run Cursor review against `origin/develop`**

Use `cursor-review` from `.worktrees/issue-797`. For every real finding, use
`superpowers:receiving-code-review` and `superpowers:systematic-debugging`, add a failing regression
test, fix, verify, commit, and run a fresh Cursor round. Require `RESULT: clean`.

- [ ] **Step 3: Wait for the post-Cursor spec regression gate**

Send the clean Cursor output path, HEAD, and post-fix verification to the parent. Do not push or
open a PR before approval.

- [ ] **Step 4: Publish and merge**

Push `issue-797`, open a PR to `develop` whose body ends with `Closes #797`, monitor checks, and
enable squash auto-merge only after approval. Monitor until GitHub reports the PR merged.

- [ ] **Step 5: Clean up**

After the merge, remove `.worktrees/issue-797`, delete the local and remote `issue-797` branches,
verify the issue/project status, and report any native #786 follow-up issues filed from real
out-of-scope findings.
