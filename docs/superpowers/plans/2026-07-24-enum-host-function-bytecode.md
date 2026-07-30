# Enum Host Function Bytecode Persistence Implementation Plan

> **For Codex:** Execute this plan task by task with test-driven development. Do not broaden the
> slice beyond #1120's persistence and hardening boundary.

**Goal:** Preserve enum-hosted functions and enum calls through compiled-bytecode export/load with
strict validation of the new serialized table and existing enum-call opcode layout.

**Architecture:** Extend each serialized class body with its own enum table. Reuse the existing
function payload codec so pointer and lambda fixups stay class-local, reconstruct both maps on the
exact owner script, and keep cross-file calls resolved from their scalar owner identity at runtime.

**Tech Stack:** C++17, Foundry Script compiler/VM, `FSBytecodeExporter`, `FSBytecodeLoader`,
`FSBytecodeVerifier`, doctest, SCons.

---

### Task 1: Add red persistence and hardening coverage

**Files:**

- Modify: `modules/foundry_script/tests/test_bytecode_script.h`
- Modify: `modules/foundry_script/tests/test_bytecode_serialization.h`
- Modify: `modules/foundry_script/tests/test_bytecode_hardening.h`
- Modify: `modules/foundry_script/tests/fs_test_runner.cpp`
- Modify: `modules/foundry_script/tests/scripts/runtime/features/enum_host_functions.fs`
- Modify: `modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_async.fs`
- Modify: `modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_external.fs`
- Modify: `modules/foundry_script/tests/scripts/runtime/features/enum_host_functions_nested.fs`

1. Add a whole-script round-trip test containing root and nested instance/static enum functions,
   an enum-function lambda, exact owner checks, and post-load runtime calls.
2. Add malformed-table tests that byte-patch equal-length enum/function names into duplicates and
   require `ERR_INVALID_DATA`.
3. Extend named-global export validation with a global referenced only from an enum function and
   its lambda.
4. Add explicit verifier matrices for all three enum-call opcodes: valid layout, truncated tail,
   count mismatch, invalid addresses, invalid name indices, invalid call kind, and empty identities.
5. Update the format-version pin to 3.
6. Remove the four temporary compiled-bytecode skip sentinels and their now-unused runner directive.
7. Build and run the focused tests. Record the expected failures before implementation.

### Task 2: Serialize enum function tables

**Files:**

- Modify: `modules/foundry_script/fs_bytecode_export.h`
- Modify: `modules/foundry_script/fs_bytecode_export.cpp`

1. Add a small helper that validates and writes one instance/static function map.
2. Write each class's enum type and both maps after member functions.
3. Require non-empty identities, correct owner/static kind, and no cross-kind duplicate names.
4. Traverse enum functions in unsupported named-global collection.
5. Rebuild and run the focused tests; confirm loader tests still fail because read support is absent.

### Task 3: Deserialize and validate enum function tables

**Files:**

- Modify: `modules/foundry_script/fs_bytecode_loader.h`
- Modify: `modules/foundry_script/fs_bytecode_loader.cpp`

1. Add a helper that bounds a function count, reads each function against the current class owner,
   validates name/static kind/duplicates, and inserts it into the selected map.
2. Read enum tables after member functions and before optional initializer functions.
3. Accumulate enum-function lambda metadata in the class body's existing lambda-entry vector.
4. Reject duplicate or empty enum identities and malformed counts with contextual errors.
5. Run whole-script, malformed-table, named-global, and compiled-runtime tests until green.

### Task 4: Harden enum-call identity verification and bump the format

**Files:**

- Modify: `modules/foundry_script/fs_bytecode_verifier.cpp`
- Modify: `modules/foundry_script/fs_bytecode_format.h`

1. After each enum identity index passes its bounds check, reject an empty referenced global name.
2. Preserve the #1119 argument/tail/address/call-kind layout unchanged.
3. Bump the bytecode format from 2 to 3 and document the class-body enum table in the format header.
4. Run the verifier matrix and complete focused bytecode hardening tests.

### Task 5: Verify the authoritative slice

1. Build with macOS `dev_mode=yes tests=yes`.
2. Run focused `BytecodeScript`, `BytecodeHardening`, compiled-roundtrip, and enum-host-function
   cases using the supported command-first CLI.
3. Run strict pre-commit checks for touched files.
4. Run the full headless test suite with a JSONL progress file and confirm doctest success.
5. Inspect the diff and commit only #1120 files.

### Task 6: Independent review and delivery

1. Fetch `origin/develop`; reconcile and repeat verification if the base advanced.
2. Run read-only Cursor review against `origin/develop`, explicitly stating #1120's authoritative
   persistence/hardening boundary.
3. Apply only verified in-scope findings, recommit, and repeat until Cursor emits exact
   `RESULT: clean`.
4. Push, open a PR targeting `develop` whose body ends with `Closes #1120`, enable squash
   auto-merge, and monitor required checks through the actual merge.
5. Verify issue #1120 and its Experiment project item are Done, then remove the worktree and local
   branch.
