# Enum Call Resolution Metadata Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Resolve enum-hosted function calls and attributes and record stable, explicit enum dispatch
metadata without implementing runtime dispatch.

**Architecture:** A shared analyzer lookup maps an enum datatype to its resolved declaration through
the datatype's owner class. Enum functions are resolved before the existing metatype Dictionary
fallback. Successful calls store a pointer-free owner/function tuple on `CallNode`; wrong receiver
kinds produce targeted errors and never store dispatch metadata.

**Tech Stack:** C++17, Foundry Script parser/analyzer, doctest, analyzer script fixtures, SCons.

---

### Task 1: Add failing analyzer fixtures

**Files:**
- Create: `modules/foundry_script/tests/scripts/analyzer/features/enum_host_function_calls.norun.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/enum_host_function_calls.norun.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/enum_host_function_static_on_value.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/enum_host_function_static_on_value.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/enum_host_function_instance_on_type.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/enum_host_function_instance_on_type.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/enum_host_function_other_enum.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/enum_host_function_other_enum.out`

- [ ] **Step 1: Add the success fixture**

Declare instance `label()` and static `parse()` functions on `Status`. Call `label()` on a literal and
a typed variable, call `parse()` on `Status`, capture both as typed first-class callables, and call
the existing `Status.keys()` Dictionary method. End with `FS_TEST_OK` and keep the fixture `.norun.fs`
because #1119 owns code generation.

- [ ] **Step 2: Add receiver and isolation diagnostics**

Add one fixture each for `Status.READY.parse()`, `Status.label()`, and calling a method declared on
`Status` against a different enum value. Give each fixture the targeted expected analyzer message.

- [ ] **Step 3: Prove the fixtures red against the unchanged binary**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
	--case "*FoundryScript*" --force-colors
```

Expected: the new success fixture reports that enum values cannot be called and the diagnostic
fixtures differ from their targeted expected output.

### Task 2: Add failing C++ metadata tests

**Files:**
- Modify: `modules/foundry_script/tests/test_foundry_script.cpp`

- [ ] **Step 1: Test same-file calls and callable attributes**

Parse and analyze a script with literal instance, typed instance, static, and Dictionary calls.
Inspect each `CallNode` and assert the expected enum call kind and scalar owner identity. Inspect
`value.label` and `Status.parse` identifiers and assert their explicit callable signatures point to
the enum functions.

- [ ] **Step 2: Test nested owner identity**

Analyze an enum inside a nested class and assert a successful call records the nested class FQCN,
not only the enum name.

- [ ] **Step 3: Test cross-file `enum_name` identity**

Register a temporary namespaced `enum_name` script, analyze a consumer, and assert calls record the
dependency script path, dependency owner FQCN, qualified enum type, function name, and static versus
instance kind. Assert a Dictionary call still records `ENUM_CALL_NONE`.

- [ ] **Step 4: Prove the C++ tests red**

Build the test registration and run:

```sh
scons platform=macos target=editor dev_build=yes tests=yes -j8
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*Analyzer resolves enum host function call*" --force-colors
```

Expected: compilation fails because `CallNode` does not yet expose enum-call metadata.

### Task 3: Add the stable metadata and declaration lookup

**Files:**
- Modify: `modules/foundry_script/fs_parser.h`
- Modify: `modules/foundry_script/fs_analyzer.h`
- Modify: `modules/foundry_script/fs_analyzer.cpp`

- [ ] **Step 1: Add the pointer-free `CallNode` contract**

Add `EnumCallKind` plus owner script path, owner class FQCN, enum type, and function name fields.
Default every node to `ENUM_CALL_NONE` with empty identity fields.

- [ ] **Step 2: Map enum datatypes to declarations**

Add an analyzer helper that rejects non-script enums, resolves the datatype's owner class interface,
then returns either `enum_file_decl` or the matching named enum member. Keep cross-file resolution on
the existing dependency-parser path.

- [ ] **Step 3: Resolve enum signatures before Dictionary fallback**

In `get_function_signature()`, find a declared enum function first and populate the ordinary
Foundry Script signature outputs, including static/async/vararg flags, default arguments, exact enum
`Self` substitution, and the found-function pointer. Only unmatched enum metatype calls fall through
to Dictionary methods.

- [ ] **Step 4: Build and run the focused metadata tests**

Run:

```sh
scons platform=macos target=editor dev_build=yes tests=yes -j8
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*Analyzer resolves enum host function call*" --force-colors
```

Expected: declaration lookup and scalar identity assertions pass; receiver/attribute assertions may
remain until Task 4.

### Task 4: Enforce receiver kind and expose callable attributes

**Files:**
- Modify: `modules/foundry_script/fs_analyzer.cpp`

- [ ] **Step 1: Validate static versus instance receiver kind**

After signature lookup, compare `found_function->is_static` with the original enum datatype's
metatype flag. Emit the targeted static-on-value or instance-on-type diagnostic. Populate enum-call
metadata only for a valid match.

- [ ] **Step 2: Preserve missing and Dictionary diagnostics**

Report a missing function precisely for enum values. For enum metatypes, describe both enum static
functions and Dictionary methods. Keep unmatched `clear()` on the existing Dictionary path so its
non-const error is unchanged.

- [ ] **Step 3: Resolve first-class enum function attributes**

In `reduce_identifier_from_base()`, keep metatype value lookup first, then expose only static
functions on metatypes and instance functions on values. Install `MEMBER_FUNCTION` source metadata
and a precise `Callable` datatype built from the resolved function signature.

- [ ] **Step 4: Run focused and fixture tests**

Run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*Analyzer resolves enum host function call*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*FoundryScript*" --force-colors
```

Expected: all new metadata, callable, receiver-kind, and existing Dictionary regression cases pass.

### Task 5: Regenerate outputs and verify the analyzer slice

**Files:**
- Update only new `.out` fixtures under `modules/foundry_script/tests/scripts/analyzer/`

- [ ] **Step 1: Regenerate intentional fixture outputs**

Run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test generate-fixtures \
	modules/foundry_script/tests/scripts
```

Inspect every changed output and discard any unrelated generated change.

- [ ] **Step 2: Run focused regression coverage**

Run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*enum host function*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*FoundryScript*" --force-colors
git diff --check
```

Expected: focused C++ cases and the Foundry Script fixture suite pass with no whitespace errors.

- [ ] **Step 3: Commit the implementation**

Stage the design/plan, parser/analyzer sources, C++ tests, and only the new analyzer fixtures, then
commit with:

```sh
git commit -m "Resolve enum host function calls"
```

### Task 6: Strict/full verification and independent review

**Files:**
- Verify the complete `origin/develop...HEAD` diff.

- [ ] **Step 1: Run the strict build**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j8
```

Expected: exit 0 with warnings treated as errors.

- [ ] **Step 2: Run the full suite with structured progress**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
	--progress-format=jsonl \
	--progress-file /tmp/foundry-1118-full-progress.jsonl \
	--force-colors
```

Expected: doctest reports success with zero failed test cases.

- [ ] **Step 3: Run read-only Cursor review against `origin/develop`**

Invoke the `cursor-review` skill with this design as the authoritative boundary. Triage every finding
with `superpowers:receiving-code-review`, debug real defects with
`superpowers:systematic-debugging`, fix and verify one item at a time, recommit, and repeat until the
latest valid output is exactly `RESULT: clean`.

- [ ] **Step 4: Publish and monitor**

Push `issue-1118`, open a PR targeting `develop` whose body ends with `Closes #1118`, enable squash
auto-merge, monitor checks and merge, verify the issue and Experiment project item are Done, then
remove the issue worktree and local branch.
