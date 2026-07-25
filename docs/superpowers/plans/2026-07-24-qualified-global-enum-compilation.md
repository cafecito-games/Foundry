# Qualified Global Enum Compilation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Compile fully qualified namespaced global-enum type, value, static-call, and instance-call expressions in source and compiled-bytecode callers.

**Architecture:** Keep namespace resolution in the analyzer. Materialize the exact global-enum prefix as the existing read-only enum dictionary constant so compiler expression lowering matches imported and bare enum names while enum-call metadata remains the dispatch identity.

**Tech Stack:** Foundry Script C++ analyzer/compiler, doctest-backed Foundry Script fixture runner, SCons.

---

### Task 1: Add the failing source and compiled-bytecode regression

**Files:**
- Create: `modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_namespaced.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_namespaced.notest.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_namespaced.out`

- [ ] **Step 1: Add the provider fixture**

```fs
namespace qualified_enum.repro

enum_name Status:
	READY = 1
	BUSY = 2

	func label() -> String:
		return "ready" if self == READY else "busy"

	static func parse(text: String) -> Self:
		return READY if text == "ready" else BUSY
```

- [ ] **Step 2: Add the consumer fixture**

```fs
import qualified_enum.repro

func test() -> void:
	var qualified: qualified_enum.repro.Status = qualified_enum.repro.Status.READY
	print(qualified.label())
	print(qualified_enum.repro.Status.parse("busy").label())

	var imported: Status = Status.READY
	print(imported.label())
	print(Status.parse("busy").label())
```

- [ ] **Step 3: Add the expected output**

```text
ready
busy
ready
busy
```

- [ ] **Step 4: Run the source fixture before rebuilding and verify RED**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run --case "*Script compilation and runtime*" --force-colors
```

Expected: the new fixture fails during compilation with
`Identifier not found: qualified_enum`.

- [ ] **Step 5: Run the compiled-bytecode fixture before rebuilding and verify RED**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run --case "*compiled bytecode round-trip*" --force-colors
```

Expected: the new fixture fails during caller compilation with
`Identifier not found: qualified_enum`.

### Task 2: Materialize the qualified global-enum prefix

**Files:**
- Modify: `modules/foundry_script/fs_analyzer.cpp`

- [ ] **Step 1: Share exact namespace-prefix selection**

In `FSAnalyzer::reduce_subscript()`, select the expression spanning the first
`namespace_type_chain_size` identifiers for both global classes and global
enums.

- [ ] **Step 2: Mark a global-enum prefix as the existing constant representation**

For an enum global class, set the prefix datatype to the resolved enum
metatype, set `is_constant`, and populate `reduced_value` with
`make_enum_dictionary_from_type(namespace_class_type)`. Keep
`resolved_global_class` only for non-enum global classes.

- [ ] **Step 3: Rebuild with strict checks**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j8
```

Expected: exit 0 with no warnings-as-errors.

- [ ] **Step 4: Run source and compiled focused tests and verify GREEN**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run --case "*Script compilation and runtime*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run --case "*compiled bytecode round-trip*" --force-colors
```

Expected: both test cases pass, including the fully qualified and imported
spellings.

### Task 3: Validate regressions and publish

**Files:**
- Verify all modified files from Tasks 1-2.

- [ ] **Step 1: Run focused Foundry Script, namespace, compiler, and runtime tests**

Run the most specific available doctest filters covering Foundry Script
namespace resolution, script compilation/runtime, and compiled-bytecode
round-trip.

Expected: all selected cases pass with zero failures.

- [ ] **Step 2: Run the full structured suite**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
  --progress-format=jsonl \
  --progress-file /tmp/foundry-1198-full-progress.jsonl \
  --force-colors
```

Expected: doctest reports `Status: SUCCESS!`; inspect the JSONL `run_end`
record for zero failed tests.

- [ ] **Step 3: Commit the implementation**

```sh
git add docs/superpowers/specs/2026-07-24-qualified-global-enum-compilation-design.md \
  docs/superpowers/plans/2026-07-24-qualified-global-enum-compilation.md \
  modules/foundry_script/fs_analyzer.cpp \
  modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_namespaced.fs \
  modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_namespaced.notest.fs \
  modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_namespaced.out
git commit -m "Compile qualified namespaced global enums"
```

- [ ] **Step 4: Run independent Cursor review**

Run the `cursor-review` read-only workflow against refreshed
`origin/develop`. Triage every finding, fix and retest real in-scope issues,
commit, and repeat until the exact verdict is `RESULT: clean`.

- [ ] **Step 5: Rebase if `origin/develop` advanced**

Fetch `origin/develop`. If the merge base changed, rebase the branch, rerun
strict/focused verification and the full structured suite, then obtain a new
clean Cursor verdict for the rebased HEAD.

- [ ] **Step 6: Push and open the ready PR**

Push `issue-1198`, open a PR against `develop`, include validation and review
evidence, and end the body with:

```text
Closes #1198
```

- [ ] **Step 7: Enable squash auto-merge and monitor completion**

Enable squash auto-merge, monitor required CI until GitHub reports the PR
merged, verify issue #1198 closed and its Experiment project item is Done,
then remove the worktree and local branch.

