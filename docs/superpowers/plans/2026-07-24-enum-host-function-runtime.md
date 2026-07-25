# Enum Host Function Runtime Implementation Plan

> **For Codex:** Execute this plan in the existing `issue-1119` worktree.
> Follow TDD for each behavior group and keep #1120 persistence explicitly out
> of scope.

**Goal:** Compile named enum functions into stable per-class storage and
dispatch static, instance, nested, cross-file, and async enum calls in the live
Foundry Script VM.

**Architecture:** Each exact owning `FoundryScript` stores enum functions in
separate static/instance maps. Dedicated enum-call opcodes encode only the
#1118 scalar owner tuple; the VM resolves the owner script and owner-class FQCN
before looking up the enum type and function. Instance calls pass the enum
integer as the function's self override.

**Tech stack:** C++17, Foundry Script parser/compiler/bytecode VM, doctest,
script fixture runner, SCons.

---

## Task 1: Add source-runtime fixtures and the bytecode-boundary sentinel

**Files:**

- Modify: `modules/foundry_script/tests/fs_test_runner.cpp`
- Create: `modules/foundry_script/tests/scripts/runtime/features/enum_host_functions.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/enum_host_functions.out`
- Create: `modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_nested.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_nested.out`
- Create: `modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_external.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_external.out`
- Create: `modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_external.notest.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_async.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_async.out`

**Step 1: Add the failing fixtures**

Add:

- same-file static and instance calls, including receiver-as-`self`
- nested class enums with independently resolved owner identities
- a cross-file `enum_name` dependency with static and instance calls
- immediately completing async static and instance calls consumed with `await`

Put `#skip-compiled-bytecode` at the top of each executable fixture and explain
in a nearby comment that #1120 removes the sentinel after enum function-table
persistence lands.

**Step 2: Teach only the repeated bytecode pass to honor the sentinel**

Extend `read_fixture_directives()` and `make_tests_for_dir()` so
`#skip-compiled-bytecode` skips a fixture only when `compiled_bytecode` is
true. Document the narrow #1120 boundary next to the branch.

**Step 3: Run the source-runtime test and verify RED**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*Script compilation and runtime" --force-colors
```

Expected: new enum fixtures fail during compilation or execution because enum
functions are not retained and enum calls still use ordinary Variant dispatch.

**Step 4: Record the RED checkpoint**

Save the exact failing fixture names and diagnostics in the task checkpoint.
Do not change production runtime code before this evidence exists.

## Task 2: Add C++ storage/lifetime contract tests

**Files:**

- Modify: `modules/foundry_script/tests/test_foundry_script.cpp`
- Modify: `modules/foundry_script/foundry_script.h` only if a narrowly scoped
  internal query is required for the test and VM

**Step 1: Add a compile-storage test**

Compile a script containing root and nested enums. Assert:

- the functions are absent from `member_functions`
- the root and nested scripts each resolve only their own enum function
- static and instance functions occupy distinct maps
- clearing/recompiling drops old pointers and installs valid new ones

**Step 2: Add a missing-lookup diagnostic test seam**

Exercise the runtime resolver with an absent owner class, enum type, and
function. Assert the error contains the static/instance kind plus owner path,
owner-class FQCN, enum type, and function.

**Step 3: Run the focused C++ cases and verify RED**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*enum host function*runtime*" --force-colors
```

Expected: compilation fails until the storage/query surface exists, or the
tests fail because no enum function is retained.

## Task 3: Implement per-script enum function storage and lifecycle

**Files:**

- Modify: `modules/foundry_script/foundry_script.h`
- Modify: `modules/foundry_script/foundry_script.cpp`
- Modify: `modules/foundry_script/fs_compiler.h`
- Modify: `modules/foundry_script/fs_compiler.cpp`

**Step 1: Define the table and query**

Add an enum function set with separate static/instance maps and a script-owned
map keyed by enum type. Add an internal lookup that never consults ordinary
members.

**Step 2: Mirror all lifecycle paths**

Include enum functions in:

- `_prepare_compilation` replacement/deletion handling
- `_clear_partial_bytecode_link_state`
- `FoundryScript::clear`
- recursive lambda metadata erasure
- hot-reload lambda replacement collection and pointer replacement

Use the existing deduplicating `RBSet<FSFunction *>` deletion pattern.

**Step 3: Compile enum declarations**

For each enum declared directly on a class, compile each function with
`_parse_function(..., p_skip_member_register = true)` and insert it into the
correct static or instance map. Fail explicitly on duplicate or null output,
even though the analyzer normally prevents those cases.

Nested class recursion must populate the nested script, not the root script.

**Step 4: Rebuild and run the storage tests**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j8
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*enum host function*runtime*" --force-colors
```

Expected: storage/lifetime assertions pass; runtime fixtures remain red until
call codegen and VM dispatch land.

## Task 4: Add scalar enum-call codegen and live opcode plumbing

**Files:**

- Modify: `modules/foundry_script/fs_codegen.h`
- Modify: `modules/foundry_script/fs_byte_codegen.h`
- Modify: `modules/foundry_script/fs_byte_codegen.cpp`
- Modify: `modules/foundry_script/fs_function.h`
- Modify: `modules/foundry_script/fs_compiler.cpp`
- Modify: `modules/foundry_script/fs_vm.cpp`
- Modify: `modules/foundry_script/fs_bytecode_verifier.cpp`
- Modify: `modules/foundry_script/fs_disassembler.cpp`

**Step 1: Add the generator contract**

Add a `write_enum_call` API carrying the target/base/arguments, stable owner
tuple, static/instance kind, and async flag. Encode strings through the
existing global-name table; do not encode a function pointer.

**Step 2: Route annotated calls**

In the call-expression compiler, branch on
`CallNode::enum_call_kind != ENUM_CALL_NONE` after evaluating arguments and
the base. Select no-result, result, or async enum opcode using the same rules
as ordinary calls. Preserve every non-enum call path unchanged.

**Step 3: Implement VM-time resolution**

Resolve:

1. current or foreign owner script path
2. exact owner-class FQCN
3. enum type
4. static/instance table
5. function name

For instance calls, pass the evaluated receiver Variant as the `self` override.
For static calls, call with no self override. Reuse normal `FSFunction::call`
and mirror ordinary async debug behavior.

On any lookup failure, set a precise runtime error naming the stable tuple and
stop the instruction. Never invoke `Variant::callp` as a fallback.

**Step 4: Update every live instruction consumer**

Add the three opcodes to:

- `FSFunction::Opcode`
- computed-goto labels and switch dispatch
- variable-length verifier recognition and exact address/scalar tail layout
- disassembler rendering

Do not modify enum function-table export/load or add a bytecode hardening
corpus; those are #1120.

**Step 5: Rebuild and run all new behavior tests**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j8
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*Script compilation and runtime" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*enum host function*runtime*" --force-colors
```

Expected: source fixtures and C++ storage/diagnostic cases pass.

## Task 5: Verify the #1120 boundary and regressions

**Files:**

- Review all modified files

**Step 1: Run the compiled-bytecode fixture pass**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*compiled bytecode round-trip*" --force-colors
```

Expected: pass. Enum runtime fixtures are visibly skipped by their explicit
sentinel; unrelated fixtures still round-trip.

**Step 2: Run focused enum and bytecode verifier tests**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*enum host function*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*bytecode verifier*" --force-colors
```

**Step 3: Run final strict build and full suite**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j8
./bin/foundry.macos.editor.arm64 --headless test run \
  --progress-format=jsonl \
  --progress-file /tmp/foundry-1119-full-progress.jsonl \
  --force-colors
```

Trust the final doctest summary while noting any known cleanup-only exit noise.

**Step 4: Inspect scope and status**

Run:

```sh
git diff --check
git status --short
git diff --stat origin/develop...
git diff origin/develop... -- modules/foundry_script/fs_bytecode_export.cpp \
  modules/foundry_script/fs_bytecode_loader.cpp
```

Expected: no whitespace errors, intentional files only, and no enum
function-table persistence changes.

## Task 6: Review, publish, and finish

**Step 1: Commit focused changes**

Use imperative subjects under 72 characters. Keep design/tests/runtime commits
cohesive; the PR will squash.

**Step 2: Run read-only Cursor review**

Review against `origin/develop`, triage every finding rigorously, fix valid
issues, rerun affected verification, and repeat until the exact verdict is:

`RESULT: clean`

**Step 3: Push and open the PR**

The PR body must explain live runtime behavior, tests, the explicit #1120
sentinel, and end with:

`Closes #1119`

Enable squash auto-merge.

**Step 4: Monitor the actual merge**

Wait for checks and merge completion. Confirm issue #1119 is closed and the
Experiment project item is Done.

**Step 5: Clean local state**

Remove the `issue-1119` worktree and local branch only after merge is
confirmed. Preserve every unrelated checkout and user change.
