# Typed JSON Restoration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore the typed JSON decode path removed by #1458 and integrate #1453's inferred `JsonResult[JsonNode]` return type on current `develop`.

**Architecture:** Restore the language-neutral core marshaller seam and merge Foundry decode helpers into #1466's current encoder. Reapply the module-owned analyzer hint without reverting #1472 builtin export or #1474 tooling-host work.

**Tech Stack:** C++, Foundry Script fixtures, SCons, doctest, pre-commit.

---

### Task 1: Rebase the reviewed inference work

**Files:**
- Modify through conflict resolution: `modules/foundry_script/tests/scripts/runtime/features/json_decode_errors.fs`
- Modify through conflict resolution: `modules/foundry_script/tests/scripts/runtime/features/json_round_trip.fs`

- [ ] **Step 1: Preserve the reviewed commit and rebase onto current `origin/develop`.**

Create a temporary safety reference at `af9c6f8836`, then rebase `issue-1453` onto
`origin/develop`. Resolve the two delete/modify conflicts by retaining the inference-enabled fixture
contents from `af9c6f8836`; all unrelated files come from current `develop`.

- [ ] **Step 2: Confirm only the intended #1453 delta remains.**

Run `git diff --stat origin/develop...HEAD` and inspect every changed file. Expected: the analyzer,
builtin-type registry, two runtime fixtures, bootstrap regression coverage, and existing plan only.

### Task 2: Restore tests and observe RED

**Files:**
- Restore: `tests/core/io/test_json_marshaller.h`
- Restore: `modules/foundry_script/tests/test_fs_json_marshal.h`
- Restore: `modules/foundry_script/tests/scripts/runtime/features/json_decode_errors.fs`
- Restore: `modules/foundry_script/tests/scripts/runtime/features/json_decode_errors.out`
- Restore: `modules/foundry_script/tests/scripts/runtime/features/json_round_trip.fs`
- Restore: `modules/foundry_script/tests/scripts/runtime/features/json_round_trip.out`

- [ ] **Step 1: Restore observable coverage before production code.**

Reinsert the core parse delegation cases and Foundry decode/lift cases from `31946646cb^`. Preserve
all #1466 native-conformance cases in the Foundry test header. Restore the runtime fixtures with
their #1453 `:=` inference edits.

- [ ] **Step 2: Build to verify RED.**

Run `scons platform=macos target=editor dev_mode=yes tests=yes -j$(sysctl -n hw.ncpu)`.
Expected: compilation fails because `JSONObjectMarshaller` lacks decode hooks and `JSON` lacks
`parse_to_node`, proving the restored tests exercise the missing production contract.

### Task 3: Restore core and Foundry decode production code

**Files:**
- Modify: `core/io/json.h`
- Modify: `core/io/json.cpp`
- Modify: `modules/foundry_script/fs_json_marshal.h`
- Modify: `modules/foundry_script/fs_json_marshal.cpp`

- [ ] **Step 1: Restore the language-neutral core seam.**

Re-add the three decode virtual methods to `JSONObjectMarshaller`, re-add
`JSON::parse_to_node(const String &)`, and bind it as a static `JSON` method. The implementation
parses through `_parse_string`, delegates lifting and result creation, and returns `Variant`.

- [ ] **Step 2: Merge Foundry decode helpers with #1466.**

Restore builtin-script loading/calling, `make_json_node`, `make_result_ok`,
`make_result_failure`, `prepare_parsed_value`, and the three marshaller decode overrides. Keep
#1466's conformance registry includes, native witness lookup, and encoding dispatch unchanged.

- [ ] **Step 3: Build and run focused GREEN tests.**

Run the strict macOS build, then focused `*JSONMarshal*`, `*JsonMarshal*`,
`*FSBuiltinTypes*`, `*BuildTaskBootstrap*`, and Foundry Script fixture cases. Expected: zero doctest
failures and the restored runtime fixtures match their checked-in outputs.

### Task 4: Restore and reconcile documentation

**Files:**
- Modify: `doc/classes/JSON.xml`

- [ ] **Step 1: Restore the decode API documentation.**

Reinsert #1454's `parse_to_node` method and stringify/trait explanation. Retain #1466's native
retroactive-conformance example and remove only its obsolete wording that implied there was no
parse counterpart.

- [ ] **Step 2: Run touched documentation and formatting hooks.**

Run `pre-commit run --files` with every changed file. Expected: all selected hooks pass.

### Task 5: Verify, review, and publish

**Files:**
- Verify all changed files above.

- [ ] **Step 1: Run final strict build and focused suites.**

Run `scons platform=macos target=editor dev_mode=yes tests=yes -j$(sysctl -n hw.ncpu)` and the
focused suites from Task 3. Expected: build exit 0 and doctest success.

- [ ] **Step 2: Run the full structured-progress suite.**

Run `./bin/foundry.macos.editor.arm64 --headless test run --progress-format=jsonl
--progress-file /tmp/foundry-1453-1465-progress.jsonl --force-colors`. Expected: `run_end` records
zero failures and doctest reports success.

- [ ] **Step 3: Converge supervised review.**

Run `python3 ~/.claude/scripts/codex_review/await_review.py start-wait --cwd
/Users/christian/CafecitoGames/Foundry/.worktrees/issue-1453 --scope branch --base origin/develop
--deadline 540`. Verify and fix every in-scope finding, rerun affected tests, and start a fresh
review for each new HEAD until the verdict is clean.

- [ ] **Step 4: Publish and monitor merge.**

Push `issue-1453`, open one PR against `develop` whose body ends with `Closes #1465\n\nCloses
#1453`, enable squash auto-merge, monitor until actually merged, then remove the issue worktree and
safe local restoration branches.
