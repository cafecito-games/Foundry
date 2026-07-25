# Namespaced Global Enum Preload Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Resolve namespaced global `enum_name` scripts loaded through
`preload()` without weakening existing class identity checks.

**Architecture:** Make `FSParser::find_class()` accept the parser head's exact
fully qualified identity. Protect the behavior with an isolated script fixture
that runs from source and with a compiled-bytecode caller.

**Tech Stack:** C++17, Foundry Script fixtures, doctest, SCons.

---

### Task 1: Add the failing namespaced preload fixture

**Files:**
- Create: `modules/foundry_script/tests/scripts/runtime/features/enum_preload_namespaced/enum_preload_namespaced.notest.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/enum_preload_namespaced/enum_preload_namespaced.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/enum_preload_namespaced/enum_preload_namespaced.out`

- [ ] **Step 1: Write the provider**

```fs
namespace enum_preload.repro

enum_name Status:
	READY = 1
	DONE = 2
```

- [ ] **Step 2: Write the consumer and output**

```fs
import enum_preload.repro

const Provider = preload("enum_preload_namespaced.notest.fs")

func test() -> void:
	var ready: Status = Provider.READY
	var done: Status = Status.DONE
	print(ready)
	print(done)
```

Expected output:

```text
FS_TEST_OK
1
2
```

- [ ] **Step 3: Run the fixture with the existing binary**

Run:

```sh
./bin/foundry.* --headless test run --case "*enum_preload_namespaced*" --force-colors
```

Expected: failure with `Could not resolve script` on the preload expression.

- [ ] **Step 4: Commit the regression coverage**

```sh
git add modules/foundry_script/tests/scripts/runtime/features/enum_preload_namespaced
git commit -m "Add namespaced enum preload regression coverage"
```

### Task 2: Recognize the parser head's canonical identity

**Files:**
- Modify: `modules/foundry_script/fs_parser.cpp`

- [ ] **Step 1: Add the exact head identity branch**

At the start of `FSParser::find_class()`, return `head` when
`p_qualified_name == head->fqcn`. Leave the existing short-name, script-path,
and nested `::` lookup branches unchanged.

- [ ] **Step 2: Build with strict development checks**

Run:

```sh
python3 scripts/agent_build.py
```

Expected: exit 0 with no warning-as-error failures.

- [ ] **Step 3: Run the namespaced and unnamespaced regressions**

Run:

```sh
./bin/foundry.* --headless test run --case "*enum_preload_namespaced*" --force-colors
./bin/foundry.* --headless test run --case "*enum_host_functions_external*" --force-colors
```

Expected: both source and compiled-bytecode fixture passes succeed.

- [ ] **Step 4: Commit the root-cause fix**

```sh
git add modules/foundry_script/fs_parser.cpp
git commit -m "Resolve namespaced parser head identities"
```

### Task 3: Broader verification and publication

**Files:**
- Verify all committed files from Tasks 1 and 2.

- [ ] **Step 1: Run focused Foundry Script coverage**

Run:

```sh
python3 scripts/agent_build.py --test --case "*FoundryScript*"
```

Expected: all selected analyzer, runtime, source, and compiled-bytecode tests
pass, including ordinary class preload fixtures.

- [ ] **Step 2: Run the full structured suite**

Run:

```sh
DISPLAY=:1 ./bin/foundry.* --headless test run --progress-format=jsonl --progress-file /tmp/issue-1197-full.jsonl --force-colors
```

Expected: doctest reports zero failed cases.

- [ ] **Step 3: Run independent Cursor review**

Run a read-only Cursor Agent review against refreshed `origin/develop`. Address
all verified in-scope findings, rerun relevant verification, commit, and repeat
until the exact verdict is `RESULT: clean`.

- [ ] **Step 4: Publish and merge**

Push `issue-1197`, open a PR against `develop` whose body ends with
`Closes #1197`, enable squash auto-merge, and monitor checks until GitHub
reports the PR merged.
