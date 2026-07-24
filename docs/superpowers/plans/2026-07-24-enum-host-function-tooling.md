# Enum Host Function Tooling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose enum-hosted functions through Foundry Script completion, LSP navigation and
presentation, signature help, and generated script docs.

**Architecture:** Map analyzer-resolved enum datatypes back to their parser `EnumNode`, reuse the
existing function-symbol renderer for every LSP surface, and reuse one method-doc builder for class
and global enum methods. Preserve static/instance receiver filtering and Dictionary metatype
fallback without flattening enum functions into containing-class members.

**Tech Stack:** C++17, Foundry Script parser/analyzer/editor tooling, LSP, DocData, doctest,
completion fixtures, SCons.

---

### Task 1: Add failing completion fixtures

**Files:**
- Modify: `modules/foundry_script/tests/scripts/completion/global_enum/completion_global_enum.notest.fs`
- Create: `modules/foundry_script/tests/scripts/completion/global_enum/enum_name_static_functions.fs`
- Create: `modules/foundry_script/tests/scripts/completion/global_enum/enum_name_static_functions.cfg`
- Create: `modules/foundry_script/tests/scripts/completion/global_enum/enum_name_instance_functions.fs`
- Create: `modules/foundry_script/tests/scripts/completion/global_enum/enum_name_instance_functions.cfg`
- Create: `modules/foundry_script/tests/scripts/completion/enum_host_functions/nested_static.fs`
- Create: `modules/foundry_script/tests/scripts/completion/enum_host_functions/nested_static.cfg`
- Create: `modules/foundry_script/tests/scripts/completion/enum_host_functions/nested_instance.fs`
- Create: `modules/foundry_script/tests/scripts/completion/enum_host_functions/nested_instance.cfg`

- [ ] **Step 1: Add global enum functions and receiver fixtures**

Give `CompletionGlobalEnum` documented instance `label()` and `async next_label()`, plus static
`parse(value: String)` and `static async load(value: String)`. Require only the static functions on
`CompletionGlobalEnum.` and only instance functions on a typed value. Explicitly exclude the wrong
receiver kind and retain a const Dictionary method such as `keys`.

- [ ] **Step 2: Add nested enum receiver fixtures**

Declare a nested `Status` enum with values, documented instance functions, and static functions.
Require values plus static functions on `Status.` and instance functions on `status: Status`.
Exclude receiver-incompatible functions.

- [ ] **Step 3: Prove focused completion red**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
	--case "*Completion*" --force-colors
```

Expected: the new function suggestions are missing while existing enum values and Dictionary
methods remain available.

### Task 2: Add failing LSP and docgen tests

**Files:**
- Create: `modules/foundry_script/tests/scripts/lsp/enum_host_functions.fs`
- Modify: `modules/foundry_script/tests/scripts/lsp/global_enum.fs`
- Modify: `modules/foundry_script/tests/test_lsp.h`
- Modify: `modules/foundry_script/tests/test_foundry_script.cpp`

- [ ] **Step 1: Test root and nested enum symbols**

Extend the global enum fixture with documented instance/static/async functions. Assert those
functions are children after enum values with `Method`/`Function` kinds and canonical details.
Add a nested enum fixture and assert the same child structure.

- [ ] **Step 2: Test hover, definition, resolved completion, and signature help**

Add static and instance call sites. Resolve both identifiers to their enum declarations, verify
hover renders the exact signature/docs, verify definition ranges select the declaration
identifiers, resolve enum function completion items, and verify signature help parameters.

- [ ] **Step 3: Test generated docs**

Extend the existing `enum_name` docgen case with instance and `static async` functions. Assert
`DocData::MethodDoc` names, qualifiers, return/argument types, defaults, and descriptions. Add a
nested enum assertion proving its functions are not flattened into containing-class methods because
`DocData::EnumDoc` cannot own methods.

- [ ] **Step 4: Prove the focused tests red**

Rebuild test registration, then run:

```sh
scons platform=macos target=editor dev_build=yes tests=yes -j8
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*LSP*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*Docgen*enum_name*" --force-colors
```

Expected: enum function symbol/lookup/detail/signature assertions fail and `enum_name` docs contain
no methods.

### Task 3: Render enum function symbols

**Files:**
- Modify: `modules/foundry_script/language_server/fs_extend_parser.h`
- Modify: `modules/foundry_script/language_server/fs_extend_parser.cpp`

- [ ] **Step 1: Centralize enum child construction**

Add `append_enum_symbol_children(const EnumNode *, DocumentSymbol &)` that emits values using the
existing `EnumMember` details, then calls `parse_function_symbol()` for every enum function.

- [ ] **Step 2: Use the helper for global and nested enums**

Replace the duplicated root `enum_name` and nested enum value loops with the helper. Keep the
global symbol name/detail and nested enum multiline detail unchanged.

- [ ] **Step 3: Run symbol-focused LSP coverage**

Run:

```sh
scons platform=macos target=editor dev_build=yes tests=yes -j8
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*LSP*" --force-colors
```

Expected: document-symbol assertions pass; lookup/completion assertions may remain red.

### Task 4: Complete and resolve enum functions

**Files:**
- Modify: `modules/foundry_script/fs_editor.cpp`

- [ ] **Step 1: Add enum declaration/function lookup helpers**

Resolve `enum_file_decl` or a named enum member from a script enum datatype. Find a function by
`functions_indices`, validating the index and receiver kind.

- [ ] **Step 2: Add receiver-compatible completion**

In `_find_identifiers_in_base()`, add enum values only for metatypes and add functions whose
`is_static` matches `base_type.is_meta_type`. Reuse the normal function option and brace display.
Allow only metatypes to fall through to Dictionary methods.

- [ ] **Step 3: Add enum function type guessing**

Teach `_guess_identifier_type_from_base()` and `_guess_method_return_type_from_base()` to use enum
function signatures/return datatypes so callable and chained completion stay precise.

- [ ] **Step 4: Add enum function lookup locations**

In `_lookup_symbol_from_base()`, resolve compatible enum functions before enum values/Dictionary
methods and return the owner script path plus function identifier line.

- [ ] **Step 5: Run completion and LSP coverage**

Run:

```sh
scons platform=macos target=editor dev_build=yes tests=yes -j8
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*Completion*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*LSP*" --force-colors
```

Expected: completion, hover, definition, resolved completion, and signature help pass.

### Task 5: Generate global enum method docs

**Files:**
- Modify: `modules/foundry_script/editor/fs_docgen.cpp`
- Modify: `modules/foundry_script/tests/test_foundry_script.cpp`

- [ ] **Step 1: Extract the shared method-doc builder**

Move the current class function `MethodDoc` construction into a local lambda accepting a
`FunctionNode *`. Preserve every qualifier, return rule, argument/default, and rest parameter.

- [ ] **Step 2: Add `enum_name` methods**

After adding the root enum docs, append every enum function through the shared builder. Do not add
nested enum functions to containing-class methods.

- [ ] **Step 3: Run focused docgen and Foundry Script tests**

Run:

```sh
scons platform=macos target=editor dev_build=yes tests=yes -j8
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*Docgen*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*FoundryScript*" --force-colors
```

Expected: enum docs and existing class docs pass without presentation regressions.

### Task 6: Verify, commit, and converge review

**Files:**
- Verify the complete `origin/develop...HEAD` diff.

- [ ] **Step 1: Run focused and broad verification**

Run Completion, LSP, Docgen, strict Foundry Script, `git diff --check`, then:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j8
./bin/foundry.macos.editor.arm64 --headless test run \
	--progress-format=jsonl \
	--progress-file /tmp/foundry-1122-full-progress.jsonl \
	--force-colors
```

Expected: strict build exits zero and the doctest summary reports success.

- [ ] **Step 2: Commit the tooling slice**

Stage only #1122 design/plan, tests/fixtures, completion/LSP sources, and docgen, then commit:

```sh
git commit -m "Expose enum functions in editor tooling"
```

- [ ] **Step 3: Converge read-only Cursor review**

Use `cursor-review` against `origin/develop` and state that runtime (#1119), bytecode (#1120), and
nested enum method docs unsupported by `DocData::EnumDoc` are authoritative slice boundaries.
Triage with `superpowers:receiving-code-review`, debug real defects systematically, fix, verify,
recommit, and rerun until the latest valid output is exactly `RESULT: clean`.

- [ ] **Step 4: Publish, merge, and clean up**

Push `issue-1122`, open a PR targeting `develop` whose body ends with `Closes #1122`, enable squash
auto-merge, monitor checks and actual merge, verify the issue and Experiment project item are Done,
then remove the issue worktree and local branch.
