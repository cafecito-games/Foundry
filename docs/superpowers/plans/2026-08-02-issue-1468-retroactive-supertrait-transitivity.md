# Retroactive Conformance Supertrait Transitivity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every valid retroactive conformance register the canonical, de-duplicated identity closure of its
directly declared traits and all transitive supertraits in both parse-time and runtime registries.

**Architecture:** Add one shared helper in `fs_trait_utils` that returns a trait's own identity followed by its
already-resolved transitive supertrait identities. The analyzer clones one provenance-preserving parse entry per
identity, while the compiler clones one runtime entry per identity after compiling the witness functions once.
Existing exact registry lookups and bytecode serialization then consume the expanded entries without
consumer-specific fallbacks or a format change.

**Tech Stack:** Foundry Script analyzer/compiler, `FSConformanceRegistry`, C++ doctest, Foundry Script fixture runner,
compiled-bytecode exporter/loader, command-first Foundry CLI.

---

## File map

- Modify `modules/foundry_script/fs_trait_utils.{h,cpp}`: own canonical trait identity-closure construction.
- Modify `modules/foundry_script/fs_analyzer_conformance.cpp`: validate coherence over implied identities and register
  parse entries for the closure.
- Modify `modules/foundry_script/fs_compiler.cpp`: register one runtime entry per de-duplicated closure identity while
  compiling witnesses once.
- Modify `modules/foundry_script/fs_function.{h,cpp}`: let runtime trait-typed argument validation accept conforming
  builtin values through the registry.
- Modify `modules/foundry_script/fs_utility_functions.cpp`: make `is_instance_of` use registry membership for
  retroactively conformed builtin and native values.
- Modify `modules/foundry_script/fs_conformance_registry.h`: document that stored entries may represent implied
  memberships but retain original provenance and witnesses.
- Create `modules/foundry_script/tests/scripts/runtime/features/retroactive_conformance_supertrait_transitive.{fs,out}`:
  observable analyzer/runtime behavior for script, builtin, native, inherited-native, reverse, unrelated, and diamond
  cases.
- Modify `modules/foundry_script/tests/test_conformance_visibility.h`: loaded/unloaded visibility, parse provenance,
  clear behavior, and explicit-implied coherence.
- Modify `modules/foundry_script/tests/test_bytecode_script.h`: multi-level diamond witness membership and dispatch
  after source registrations are cleared and bytecode is loaded.
- Modify `modules/foundry_script/tests/scripts/json_marshal_host/native_image_ext.notest.fs`: exercise trait-scoped
  native witness lookup through a `JsonSerializable` subtrait.
- Modify `modules/foundry_script/tests/test_fs_json_marshal.h`: describe and assert the subtrait protocol path.
- No `GRAMMAR.md` change: identity closure is semantic registration behavior and changes no syntax or grammar.

### Task 1: Add failing runtime coverage across target kinds

**Files:**
- Create: `modules/foundry_script/tests/scripts/runtime/features/retroactive_conformance_supertrait_transitive.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/retroactive_conformance_supertrait_transitive.out`

- [ ] **Step 1: Write the runtime fixture**

Create a local diamond and conform a script class, builtin `int`, and native `RefCounted` target only to `Leaf`:

```foundry
trait Root:
	abstract func root_value() -> int

trait Left uses Root:
	pass

trait Right uses Root:
	pass

trait Leaf uses Left, Right:
	pass

trait Unrelated:
	pass

class ScriptTarget:
	pass

class RootOnly:
	pass

extend ScriptTarget uses Leaf:
	func root_value() -> int:
		return 11

extend int uses Leaf:
	func root_value() -> int:
		return self

extend RefCounted uses Leaf:
	func root_value() -> int:
		return 30

extend RootOnly uses Root:
	func root_value() -> int:
		return 40

func accepts_root(value: Root) -> int:
	return value.root_value()

func test() -> void:
	var script := ScriptTarget.new()
	var script_root: Root = script
	var script_wide: Variant = script
	print(script is Leaf)
	print(script is Left)
	print(script is Right)
	print(script is Root)
	print(script_wide is Root)
	print((script as Root).root_value())
	print(accepts_root(script_root))

	var number := 7
	var number_root: Root = number
	var number_wide: Variant = number
	print(number is Leaf)
	print(number is Root)
	print(number_wide is Root)
	print((number as Root).root_value())
	print(accepts_root(number_root))

	var resource := Resource.new()
	var resource_root: Root = resource
	var resource_wide: Object = resource
	print(resource is Leaf)
	print(resource is Root)
	print(resource_wide is Root)
	print((resource as Root).root_value())
	print(accepts_root(resource_root))
	print(Node.new() is Root)

	var root_only := RootOnly.new()
	print(root_only is Root)
	print(root_only is Leaf)
	print(root_only is Unrelated)
```

- [ ] **Step 2: Add the intended output**

Create this expected `.out`:

```text
FS_TEST_OK
true
true
true
true
true
11
11
true
true
true
7
7
true
true
true
30
30
false
true
false
false
```

Regenerate the fixture after implementation if the runner emits intentional analyzer warnings.

- [ ] **Step 3: Run the existing binary to verify the fixture fails**

Run:

```bash
/Users/christian/CafecitoGames/Foundry/bin/foundry.macos.editor.dev.arm64 \
  --headless test run --case "*retroactive_conformance_supertrait_transitive*" --force-colors
```

Expected: FAIL because assignments/casts to `Root` and implied-supertrait membership are rejected before closure
registration exists.

### Task 2: Add failing visibility, provenance, coherence, bytecode, and protocol coverage

**Files:**
- Modify: `modules/foundry_script/tests/test_conformance_visibility.h`
- Modify: `modules/foundry_script/tests/test_bytecode_script.h`
- Modify: `modules/foundry_script/tests/scripts/json_marshal_host/native_image_ext.notest.fs`
- Modify: `modules/foundry_script/tests/test_fs_json_marshal.h`

- [ ] **Step 1: Make the visibility fixture consume an implied root**

Add `root_trait_path` to `ConformanceVisibilityFixture`. Register `FsvRoot` as a global trait containing the existing
`abstract static func fsv_mark() -> int`; change `FsvMarkable` to `trait_name FsvMarkable` plus `uses FsvRoot`; keep
the conformance declared only as `extend FsvWidget uses FsvMarkable`. Change both consumers to assign `FsvWidget` to
`FsvRoot`.

- [ ] **Step 2: Assert parse provenance, clear behavior, and explicit-implied coherence**

After analyzing the declaring file, assert that `get_file_conformances(conformance_path)` contains exactly one
`FsvMarkable` entry and one `FsvRoot` entry, both with the same `source_file`, `target_fqcn`, `conformance_index`, and
`fsv_mark` witness pointer. Assert `get_conformance_source()` and `get_witnesses()` work for `FsvRoot`. In a separate
case, analyze a second file containing:

```foundry
extend FsvWidget uses FsvRoot:
	static func fsv_mark() -> int:
		return 9
```

and assert it is rejected as a duplicate/redundant conformance. Clear the original file and assert both direct and
implied identities disappear together.

- [ ] **Step 3: Strengthen the bytecode round-trip**

In `Conformance witnesses re-register with the registry`, replace the two unrelated traits with:

```foundry
trait Pingable:
	abstract func ping() -> int

trait LeftPing uses Pingable:
	pass

trait RightPing uses Pingable:
	pass

trait Trackable uses LeftPing, RightPing:
	pass
```

Declare only `extend Gadget uses Trackable`. Assert compile-time runtime entries contain exactly the four unique
identities and every entry shares the same compiled `ping` pointer. After export, clear parse/runtime registrations,
load bytecode, and assert all four identities, the `Pingable` witness, runtime `is`/`as`, and dispatch are restored.
Retain the existing marker-conformance round-trip as the empty-witness-map case. The native trait-scoped lookup is
covered separately by the JSON marshaling test below.

- [ ] **Step 4: Route JSON marshaling through a subtrait**

Change `native_image_ext.notest.fs` to:

```foundry
trait ResourceJson uses JsonSerializable:
	pass

extend Image uses ResourceJson:
	func to_json() -> JsonNode:
		return JsonNode.Str("image:" + get_class())

	static func from_json(_node: JsonNode) -> JsonResult[Self]:
		return JsonResult[Self].fail("not decodable", "$")
```

Update the corresponding C++ test comments and assert `native_class_conforms(Image, JsonSerializable, true)` plus
`find_native_trait_witness_function(Image, JsonSerializable, to_json)` before checking top-level/array/dictionary JSON
output.

- [ ] **Step 5: Build the test binary and verify the new tests fail for the intended reason**

Run:

```bash
python3 scripts/agent_build.py --backend ninja
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*Conformance*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*JsonMarshal*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*BytecodeScript*Conformance*" --force-colors
```

Expected: the new assertions fail because only direct identities are registered; pre-existing direct-trait tests
remain green.

### Task 3: Add the canonical identity-closure helper

**Files:**
- Modify: `modules/foundry_script/fs_trait_utils.h`
- Modify: `modules/foundry_script/fs_trait_utils.cpp`

- [ ] **Step 1: Declare the helper**

Add:

```cpp
Vector<StringName> fs_trait_identity_closure(const FSParser::ClassNode *p_trait);
```

- [ ] **Step 2: Implement deterministic de-duplication**

Implement the helper as direct identity first, followed by `p_trait->resolved_traits` order, using a
`HashSet<StringName>` to omit empty and repeated identities:

```cpp
Vector<StringName> fs_trait_identity_closure(const FSParser::ClassNode *p_trait) {
	Vector<StringName> identities;
	HashSet<StringName> seen;
	auto append = [&](const FSParser::ClassNode *p_member) {
		const StringName identity = fs_trait_identity_name(p_member);
		if (identity != StringName() && !seen.has(identity)) {
			seen.insert(identity);
			identities.push_back(identity);
		}
	};
	if (p_trait == nullptr) {
		return identities;
	}
	append(p_trait);
	for (const FSParser::ClassNode *supertrait : p_trait->resolved_traits) {
		if (supertrait != nullptr) {
			append(supertrait);
		}
	}
	return identities;
}
```

- [ ] **Step 3: Build to verify the helper compiles**

Run `python3 scripts/agent_build.py --backend ninja`.

Expected: build succeeds; behavior tests still fail because registration does not consume the helper yet.

### Task 4: Expand analyzer registration and coherence over implied identities

**Files:**
- Modify: `modules/foundry_script/fs_analyzer_conformance.cpp`
- Modify: `modules/foundry_script/fs_conformance_registry.h`

- [ ] **Step 1: Track membership provenance per declaration**

Replace the direct-only `seen_pairs` set with a map from `(target FQCN, trait identity)` to `conformance_index`.
Before accepting a direct trait, inspect every identity from `fs_trait_identity_closure(trait)`:

- reject a direct identity already present, including one implied earlier in the same declaration;
- reject an overlapping identity emitted by a different `ConformanceNode` in the same file;
- reject an identity registered by another source file;
- allow and de-duplicate an implied identity reached through two direct paths in the same `ConformanceNode`.

Use the existing duplicate/redundancy diagnostic class and name the colliding identity/source.

- [ ] **Step 2: Register one parse entry per unique identity**

After witness collision and `validate_conformance()` succeed, build the existing provenance/witness entry once, clone
it for each identity in the closure, and append only identities not already emitted by the same conformance index.
Preserve `target_keys`, `target_fqcn`, `source_file`, `conformance_index`, and the exact borrowed witness map in every
clone.

- [ ] **Step 3: Update registry documentation**

Document that `Conformance::trait_name` may be a direct or implied identity and that implied entries keep the original
conformance node and witness provenance. No query logic changes are needed because indexes already use exact
identities.

- [ ] **Step 4: Build and run analyzer/visibility tests**

Run:

```bash
python3 scripts/agent_build.py --backend ninja
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*Conformance*" --force-colors
```

Expected: parse membership, visibility, provenance, clear, coherence, and the analyzer half of the runtime fixture
pass; bytecode/runtime membership still fails until compiler registration expands.

### Task 5: Expand runtime registration without recompiling witnesses

**Files:**
- Modify: `modules/foundry_script/fs_compiler.cpp`
- Modify: `modules/foundry_script/fs_function.{h,cpp}`
- Modify: `modules/foundry_script/fs_utility_functions.cpp`

- [ ] **Step 1: Replace direct-only runtime emission**

Keep the existing witness compilation loop before trait emission. For each resolved direct `trait_use`, iterate
`fs_trait_identity_closure(trait_use.resolved_trait)`. Clone `runtime_entry` for each identity not already in the
per-conformance `emitted_traits` set, assign `trait_name`, and append it. A repeated identity from a diamond is skipped
rather than treated as a compiler error; a repeated direct trait remains an analyzer error.

- [ ] **Step 2: Assert one compilation and shared runtime function pointers**

Use the bytecode test's pre-export runtime entries to verify every closure identity references the same `FSFunction *`
compiled once for the source conformance. Do not add consumer fallbacks in `fs_type.cpp`, `fs_vm.cpp`, or JSON
marshaling.

- [ ] **Step 3: Complete builtin runtime membership consumers**

Route trait-typed `FSDataType` validation for non-object builtin values and `is_instance_of(value, Trait)` through the
expanded registry. These paths already honor direct runtime conformance for object-backed targets but otherwise reject
builtin values before consulting registry membership.

- [ ] **Step 4: Rebuild and regenerate intentional fixture output**

Run:

```bash
python3 scripts/agent_build.py --backend ninja
./bin/foundry.macos.editor.dev.arm64 --headless test generate-fixtures \
  modules/foundry_script/tests/scripts/runtime/features
```

Inspect the generated `.out`; retain only expected observable output and intentional warnings.

- [ ] **Step 5: Run all focused behavior suites**

Run:

```bash
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*retroactive_conformance_supertrait_transitive*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*Conformance*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*JsonMarshal*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*BytecodeScript*Conformance*" --force-colors
```

Expected: all focused suites pass. Source execution and loaded bytecode both report direct and implied membership and
dispatch the original witness.

### Task 6: Validate, commit, review, and publish

**Files:**
- Modify only the files listed above plus this plan.

- [ ] **Step 1: Run formatting and diff hygiene**

Run `git diff --check` and format touched C++ files with the repository's configured formatter/pre-commit hooks if
necessary. Confirm no source-text assertion tests were added and `GRAMMAR.md` is unchanged.

- [ ] **Step 2: Run the required native strict build**

Run:

```bash
python3 scripts/agent_build.py
```

Expected: native SCons strict build succeeds with warnings-as-errors enabled.

- [ ] **Step 3: Run the full suite with structured progress**

Run:

```bash
./bin/foundry.macos.editor.dev.arm64 --headless test run --progress-format=jsonl \
  --progress-file .test_scratch/issue-1468-progress.jsonl --force-colors
```

Expected: doctest reports `Status: SUCCESS!`; inspect the `run_end` progress event for zero failures. On Linux use the
documented `DISPLAY=:1` equivalent.

- [ ] **Step 4: Commit focused changes**

Stage only the plan, helper, analyzer/compiler, registry documentation, and behavioral tests. Commit with:

```bash
git commit -m "fix(foundry_script): Register retroactive supertraits"
```

- [ ] **Step 5: Run supervised Codex review to convergence**

Run:

```bash
python3 ~/.claude/scripts/codex_review/await_review.py start-wait \
  --cwd /Users/christian/CafecitoGames/Foundry/.worktrees/issue-1468 \
  --scope branch --base origin/develop --deadline 540
```

Triage every finding, fix all in-scope blocking issues, rerun focused and strict validation after changes, and start a
fresh review for each new HEAD until the verdict is clean or only filed follow-ups remain.

- [ ] **Step 6: Push, open the PR, and enable squash auto-merge**

Push `issue-1468`, open a PR against `develop` whose body ends with `Closes #1468`, and run
`gh pr merge --squash --auto`. After GitHub reports the PR merged, move the issue to Done, remove
`.worktrees/issue-1468`, and delete the local `issue-1468` branch.
