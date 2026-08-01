# Builtin Native Return Hint Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let strict Foundry Script infer `JsonResult[JsonNode]` from `JSON.parse_to_node(text)` without an explicit local annotation.

**Architecture:** Keep the runtime binding and core `JSON` API unchanged. The Foundry Script module owns a small data-driven table mapping native methods to builtin Foundry Script return types; the analyzer consults that table after normal ClassDB signature resolution and replaces only the return type with a resolved, specialized builtin type.

**Tech Stack:** C++, Foundry Script analyzer fixtures, SCons/doctest.

---

### Task 1: Pin inferred parse result behavior

**Files:**
- Modify: `modules/foundry_script/tests/scripts/runtime/features/json_round_trip.fs`
- Modify: `modules/foundry_script/tests/scripts/runtime/features/json_decode_errors.fs`

- [ ] **Step 1: Remove the explicit `JsonResult[JsonNode]` annotations.**

Use `:=` for every `JSON.parse_to_node(...)` local in the two fixtures. Keep the existing calls to `is_ok()`, `value`, and `error`; they prove the inferred type exposes the complete `JsonResult[JsonNode]` surface.

- [ ] **Step 2: Run the focused fixture suite and verify RED.**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run --case "*FoundryScript*" --force-colors
```

Expected before implementation: failure with `INFERENCE_ON_VARIANT` and/or unsafe member access caused by the native binding's `Variant` return metadata.

### Task 2: Add the module-owned native return hint

**Files:**
- Modify: `modules/foundry_script/fs_builtin_types.h`
- Modify: `modules/foundry_script/fs_builtin_types.cpp`
- Modify: `modules/foundry_script/fs_analyzer.h`
- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Test: `modules/foundry_script/tests/scripts/runtime/features/json_round_trip.fs`
- Test: `modules/foundry_script/tests/scripts/runtime/features/json_decode_errors.fs`

- [ ] **Step 1: Describe native builtin return hints in `FSBuiltinTypes`.**

Add a query taking native class and method names and returning a builtin global type name plus its type-argument names. Back it with a static table containing:

```text
JSON.parse_to_node -> JsonResult[JsonNode]
```

- [ ] **Step 2: Resolve a hint through the analyzer's existing global-type machinery.**

After `ClassDB::get_method_info()` supplies the native signature, ask `FSBuiltinTypes` for a richer return hint. Resolve `JsonResult` through `make_global_class_meta_type`, resolve `JsonNode` through `make_global_enum_type_from_path`, convert both metatypes to value types, bind the argument with `bind_class_type_arguments`, and replace `r_return_type`. Leave arguments, defaults, flags, and methods without hints unchanged.

- [ ] **Step 3: Build and verify GREEN.**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j$(sysctl -n hw.ncpu)
./bin/foundry.macos.editor.arm64 --headless test run --case "*FoundryScript*" --force-colors
```

Expected: build exits 0 and the focused suite ends with `[doctest] Status: SUCCESS!`.

### Task 3: Verify and publish

**Files:**
- Verify all changed files above.

- [ ] **Step 1: Run the full suite with structured progress.**

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
  --progress-format=jsonl --progress-file /tmp/foundry-1453-test-progress.jsonl \
  --force-colors
```

Expected: `[doctest] Status: SUCCESS!`.

- [ ] **Step 2: Run the supervised branch review.**

```sh
python3 ~/.claude/scripts/codex_review/await_review.py start-wait \
  --cwd /Users/christian/CafecitoGames/Foundry/.worktrees/issue-1453 \
  --scope branch --base origin/develop --deadline 540
```

Triage every finding and repeat on a new HEAD until clean or only filed follow-ups remain.

- [ ] **Step 3: Commit, push, and open the PR.**

Use an imperative commit subject under 72 characters. Open against `develop`, end the body with `Closes #1453`, then enable squash auto-merge.
