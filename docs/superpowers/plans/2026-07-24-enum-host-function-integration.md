# Enum Host Function Integration Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a namespaced cross-file enum-host regression that runs from source and from a compiled-bytecode caller, then complete the final #1115 validation.

**Architecture:** A `.notest.fs` provider declares a namespaced global enum with instance and static functions. A normal runtime fixture imports and preloads that provider, exercises qualified enum dispatch and Dictionary fallback, and is automatically run by both existing Foundry Script fixture passes.

**Tech Stack:** Foundry Script, C++ doctest fixture runner, SCons, command-first Foundry CLI.

---

### Task 1: Add the namespaced cross-file integration fixture

**Files:**
- Create: `modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_namespaced_external.notest.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_namespaced_external.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_namespaced_external.out`

- [ ] **Step 1: Add the namespaced enum provider**

Create `enum_host_functions_namespaced_external.notest.fs`:

```fs
namespace issue_1123.enum_integration

enum_name IntegratedStatus:
	UNKNOWN = 0
	READY = 10
	DONE = 20

	func or_else(fallback: Self) -> Self:
		return fallback if self == UNKNOWN else self

	func label(prefix: String = "") -> String:
		return prefix + ("ready" if self == READY else "done")

	static func parse(text: String) -> Self:
		return READY if text == "ready" else DONE
```

- [ ] **Step 2: Add the executable consumer**

Create `enum_host_functions_namespaced_external.fs`:

```fs
import issue_1123.enum_integration

const _NAMESPACED_ENUM_PROVIDER = preload(
	"enum_host_functions_namespaced_external.notest.fs"
)

func test() -> void:
	var ready: IntegratedStatus = IntegratedStatus.parse("ready")
	var done: IntegratedStatus = IntegratedStatus.parse("other")
	var recovered: IntegratedStatus = IntegratedStatus.UNKNOWN.or_else(ready)
	print(ready.label("status:"))
	print(done.label())
	print(recovered.label("fallback:"))
	print(IntegratedStatus.keys())
```

- [ ] **Step 3: Add the exact expected output**

Create `enum_host_functions_namespaced_external.out`:

```text
FS_TEST_OK
status:ready
done
fallback:ready
["UNKNOWN", "READY", "DONE"]
```

- [ ] **Step 4: Build current `develop` plus the new test and run both fixture passes**

Run:

```sh
python3 scripts/agent_build.py
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "Script compilation and runtime" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "Script compilation and runtime with compiled bytecode round-trip" \
	--force-colors
```

Expected: both existing-behavior coverage runs pass. If either fails, stop, use systematic debugging to identify the production root cause, add the smallest focused failing test, and request scope expansion before editing production code.

- [ ] **Step 5: Commit the integration fixture**

```sh
git add modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_namespaced_external.fs \
	modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_namespaced_external.notest.fs \
	modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_namespaced_external.out
git commit -m "Test namespaced enum host function integration"
```

### Task 2: Validate every final integration surface

**Files:**
- Verify all files in `origin/develop...HEAD`

- [ ] **Step 1: Run focused Foundry Script, completion, and LSP suites**

Run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*FoundryScript*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*Completion*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*LSP*" --force-colors
```

Expected: every selected doctest case passes with zero failures.

- [ ] **Step 2: Re-run the strict CI-style build**

Run:

```sh
python3 scripts/agent_build.py
```

Expected: SCons exits zero with `dev_mode=yes`, tests enabled, and warnings treated as errors.

- [ ] **Step 3: Run the full suite with per-test structured progress**

Run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--progress-format=jsonl \
	--progress-file /tmp/foundry-1123-full-test-progress.jsonl \
	--progress-heartbeat-seconds 30 \
	--force-colors
```

Expected: the progress file ends with `run_end`, and doctest reports zero failed test cases.

- [ ] **Step 4: Re-audit repository invariants**

Run:

```sh
rg -n "#skip-compiled-bytecode|#1120 removes this sentinel" \
	modules/foundry_script/tests || true
rg -n "enum_body|enum_function_decl|enum_function_modifier" \
	modules/foundry_script/GRAMMAR.md
git diff --check origin/develop...HEAD
git status --short --untracked-files=all
```

Expected: no live #1120 sentinels, all enum grammar productions are present, no whitespace errors, and only committed #1123 changes differ from `origin/develop`.

### Task 3: Converge independent review and publish

**Files:**
- Review the committed `origin/develop...HEAD` diff

- [ ] **Step 1: Run read-only Cursor review**

Invoke `cursor-review` with `CURSOR_REVIEW_BASE=origin/develop`. The review boundary is the approved test-only integration spec. Require an exact `RESULT: clean`; if findings appear, validate them with `superpowers:receiving-code-review`, use `superpowers:systematic-debugging` for real defects, fix and verify one item at a time, commit, and rerun the review.

- [ ] **Step 2: Push and open the ready PR**

Run:

```sh
git push -u origin issue-1123
```

Open a PR targeting `develop` whose body summarizes the audit and fixture validation and ends with:

```text
Closes #1123
```

- [ ] **Step 3: Enable squash auto-merge and monitor actual merge**

Run:

```sh
gh pr merge --repo cafecito-games/Foundry --squash --auto
```

Wait for required checks and verify the PR reaches `MERGED`, issue #1123 closes, and its Experiment project item is `Done`.

- [ ] **Step 4: Clean up only after merge**

From the main checkout:

```sh
git worktree remove /Users/christian/CafecitoGames/Foundry/.worktrees/issue-1123
git branch -D issue-1123
git push origin --delete issue-1123
```

Expected: the issue worktree and local/remote issue branches are absent after the merged PR.
