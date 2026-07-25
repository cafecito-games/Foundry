# Name Mangler Keep Escape Hatches Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add compiled `@keep_name` metadata and a deterministic ProGuard-style keep-rules loader that
feed typed `KEEP_RULE` evidence into the #795 whole-program name analysis.

**Architecture:** Reuse the existing passive built-in annotation tables and `.fsb` serialization path.
Add a separate TOOLS-only keep-rules value type that parses/loads the documented subset and applies
matches to `FSNameManglerAnalysis::Input`; keep export path selection and orchestration for #799.

**Tech Stack:** Foundry Script parser/compiler, C++17 engine containers and `FileAccess`, doctest, SCons.

---

## File map

- Modify `modules/foundry_script/fs_parser.h`: declare the no-op built-in annotation action.
- Modify `modules/foundry_script/fs_parser.cpp`: register `@keep_name` and let named enum declarations
  use the existing `CONSTANT` annotation target.
- Modify `modules/foundry_script/fs_compiler.cpp`: persist named enum annotation usages in
  `constant_annotations`.
- Create `modules/foundry_script/fs_name_mangler_keep_rules.h`: public TOOLS-only parser/loader and
  evidence-application API.
- Create `modules/foundry_script/fs_name_mangler_keep_rules.cpp`: line parser, deterministic glob
  matcher, loader, duplicate/unmatched diagnostics, and compiled graph traversal.
- Modify `modules/foundry_script/fs_name_mangler_analysis.cpp`: convert compiled `@keep_name`
  metadata to typed `KEEP_RULE` evidence.
- Modify `modules/foundry_script/tests/test_foundry_script.cpp`: parser/compiler metadata and public
  annotation enumeration tests.
- Modify `modules/foundry_script/tests/test_bytecode_script.h`: `.fsb` metadata round-trip coverage.
- Create `modules/foundry_script/tests/test_name_mangler_keep_rules.h`: parser, loader, glob,
  canonical identity, and diagnostic tests.
- Modify `modules/foundry_script/tests/test_name_mangler_analysis.h`: annotation/rule evidence and
  string-built dispatch contrast.
- Modify `modules/foundry_script/GRAMMAR.md`: normative target and built-in annotation table updates.
- Modify `modules/foundry_script/doc_classes/@FoundryScript.xml`: public annotation documentation.
- Create `modules/foundry_script/KEEP_RULES.md`: user-facing keep-file format and #799-ready API
  contract.

### Task 1: Prove the annotation surface is missing

**Files:**
- Modify: `modules/foundry_script/tests/test_foundry_script.cpp`

- [ ] **Step 1: Write the failing metadata test**

Add one doctest that parses, analyzes, and compiles source containing:

```foundryscript
@keep_name
class_name KeptRoot

@keep_name var kept_member: int
@keep_name signal kept_signal
@keep_name const KEPT_CONSTANT = 1
@keep_name enum KeptEnum:
    VALUE = 0
@keep_name func kept_method() -> void:
    pass
@keep_name class KeptInner:
    pass
```

Assert each direct table contains exactly one built-in usage named `keep_name`; assert the enum is in
`constant_annotations`; assert the inner class has a class usage; and assert
`FSLanguage::get_public_annotations()` includes `@keep_name`.

- [ ] **Step 2: Run the focused test to verify RED**

Build the test binary without production changes:

```sh
scons platform=macos target=editor dev_build=yes tests=yes
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*keep_name*metadata*" --force-colors
```

Expected: the doctest fails because `@keep_name` is unresolved or not allowed on an enum.

- [ ] **Step 3: Record the RED evidence**

Capture the exact failing diagnostic and focused case count in the milestone update.

### Task 2: Implement and verify built-in annotation metadata

**Files:**
- Modify: `modules/foundry_script/fs_parser.h`
- Modify: `modules/foundry_script/fs_parser.cpp`
- Modify: `modules/foundry_script/fs_compiler.cpp`
- Modify: `modules/foundry_script/GRAMMAR.md`
- Modify: `modules/foundry_script/doc_classes/@FoundryScript.xml`

- [ ] **Step 1: Register the annotation and no-op action**

Register:

```cpp
register_annotation(MethodInfo("@keep_name"),
        AnnotationInfo::CLASS | AnnotationInfo::VARIABLE | AnnotationInfo::FUNCTION |
                AnnotationInfo::SIGNAL | AnnotationInfo::CONSTANT,
        &FSParser::keep_name_annotation);
```

Declare and define `keep_name_annotation(...)` to return `true`; it has no runtime/parser side effect.

- [ ] **Step 2: Attach valid enum annotations**

Change only the enum declaration dispatch from `AnnotationInfo::NONE` to
`AnnotationInfo::CONSTANT`. Leave enum values and enum-body declarations unchanged.

- [ ] **Step 3: Persist named enum metadata**

In the compiler's `Member::ENUM` case, call `_collect_annotations(enum_n->annotations, usages)` and
store non-empty usages in `p_script->constant_annotations[name]`.

- [ ] **Step 4: Update normative/public docs**

Add `@keep_name` to `GRAMMAR.md` with targets `class, variable, function, signal, constant/named
enum`, and document that named enum declarations accept constant-target built-ins. Add an
`@keep_name` entry to `@FoundryScript.xml` explaining the name-mangling escape hatch.

- [ ] **Step 5: Build and rerun the focused metadata test**

```sh
scons platform=macos target=editor dev_build=yes tests=yes
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*keep_name*metadata*" --force-colors
```

Expected: focused test passes with no unexpected warnings.

### Task 3: Prove `.fsb` annotation round-trip is missing

**Files:**
- Modify: `modules/foundry_script/tests/test_bytecode_script.h`

- [ ] **Step 1: Write the round-trip assertions before any bytecode-specific production change**

Extend the whole-script round-trip source with `@keep_name` class/member/method/signal/constant/named
enum declarations. Assert restored tables contain the same built-in `keep_name` usages as the
compiled original.

- [ ] **Step 2: Run the focused bytecode test**

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*BytecodeScript*annotations*round-trip*" --force-colors
```

Expected after Task 2: pass without bytecode-format changes, proving the existing annotation section
is the correct persistence channel. If it fails, diagnose the existing serializer/loader boundary
before editing it.

### Task 4: Define keep-rules behavior with failing tests

**Files:**
- Create: `modules/foundry_script/tests/test_name_mangler_keep_rules.h`

- [ ] **Step 1: Write parser and deterministic diagnostic tests**

Include the wished-for `fs_name_mangler_keep_rules.h` API and test:

```cpp
FSNameManglerKeepRules rules;
Vector<FSNameManglerKeepRules::Diagnostic> diagnostics;
CHECK_EQ(FSNameManglerKeepRules::parse(text, "res://keep-rules.pro", rules, diagnostics), OK);
```

Cover blank lines and comments, `-keep`, `-keepclassmembers`, exact duplicate warnings, unknown
directive, missing `class`, brace/semicolon errors, empty block, and no partial rules after an error.
Assert every diagnostic formats with stable `source:line` and source-order output.

- [ ] **Step 2: Write glob/canonical identity tests**

Compile roots whose identities are:

```text
res://actors/player.fs
game.actors.Player
game.actors.Player::Inventory
```

Apply rules proving `*` cannot cross `/`, `.`, or either `:` in `::`/`res://`, while `**` can, and
`?` consumes one non-separator. Assert member globs match atomic names only.

- [ ] **Step 3: Write unmatched and load tests**

Assert unmatched warnings:

- include the rule's `source:line`;
- follow retained source order;
- appear after applying to `complete_project_graph = true`; and
- do not appear for `complete_project_graph = false`.

Write a temporary valid file under `TestUtils::get_temp_path`, load it, and compare behavior with
`parse`. Assert a missing file returns the `FileAccess` error with an actionable diagnostic.

- [ ] **Step 4: Build to verify RED**

```sh
scons platform=macos target=editor dev_build=yes tests=yes
```

Expected: compile failure for the absent `fs_name_mangler_keep_rules.h` API, proving the tests lead
the implementation.

### Task 5: Implement the keep-rules parser, matcher, and application API

**Files:**
- Create: `modules/foundry_script/fs_name_mangler_keep_rules.h`
- Create: `modules/foundry_script/fs_name_mangler_keep_rules.cpp`
- Create: `modules/foundry_script/KEEP_RULES.md`

- [ ] **Step 1: Define the public value types**

Implement `DiagnosticSeverity`, `Diagnostic::format()`, `parse`, `load`, and `apply_to_input`.
Internally retain each rule's directive, class/member patterns, source, line, and source ordinal.

- [ ] **Step 2: Implement transactional line parsing**

Strip trailing `#` comments and trim whitespace. Parse the exact grammar from the design. Build into
a temporary rule vector and publish it only if no error occurred. Deduplicate exact retained rules,
warning at the duplicate line with the original line in the message.

- [ ] **Step 3: Implement full-string glob matching**

Use a deterministic dynamic-programming or memoized matcher:

```cpp
static bool _glob_match(const String &p_pattern, const String &p_value, bool p_class_pattern);
```

For class patterns, `/`, `.`, and `:` are separators. `?` consumes one non-separator, `*` consumes
zero or more non-separators, and `**` consumes zero or more arbitrary characters. Member matching
uses the same operators over atomic names.

- [ ] **Step 4: Apply rules in stable declaration order**

Recursively gather compiled classes from `Input::scripts`, sort by `fully_qualified_name`, and match
class/member declarations. Sort member names before applying evidence. Use details containing
`source:line`, directive, canonical class identity, and member when applicable. Track each retained
rule's match count and append unmatched warnings in source order only for complete graphs.

- [ ] **Step 5: Document the format**

Write `KEEP_RULES.md` with the exact syntax, separators, canonical identity examples, unsupported
forms, diagnostic behavior, and a note that #799 will supply the selected project/preset path.

- [ ] **Step 6: Build and run focused rules tests**

```sh
scons platform=macos target=editor dev_build=yes tests=yes
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerKeepRules*" --force-colors
```

Expected: all parser, matcher, loader, identity, and diagnostic cases pass.

### Task 6: Prove and implement analysis evidence

**Files:**
- Modify: `modules/foundry_script/tests/test_name_mangler_analysis.h`
- Modify: `modules/foundry_script/fs_name_mangler_analysis.cpp`

- [ ] **Step 1: Write the annotation/rule contrast test**

Compile declarations whose only dynamic uses are:

```foundryscript
@keep_name func annotated_dispatch() -> void:
    pass
func ruled_dispatch() -> void:
    pass
func unkept_dispatch() -> void:
    pass
func invoke(suffix: String) -> void:
    call("annotated_" + suffix)
    call("ruled_" + suffix)
    call("unkept_" + suffix)
```

Use a keep-rule matching `ruled_dispatch`. Assert annotated and ruled declarations have typed
`KEEP_RULE` evidence and are absent from `rename_map`, while `unkept_dispatch` remains present.
Assert stable details/logs name `@keep_name` and the rule's `source:line`.

- [ ] **Step 2: Run the focused test to verify RED**

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerAnalysis*keep_name*keep rules*" --force-colors
```

Expected: rule evidence passes from Task 5, but annotation evidence fails because analysis does not
yet consume the metadata.

- [ ] **Step 3: Add annotation evidence during class collection**

Add a helper that recognizes only `usage.is_builtin && usage.name == SNAME("keep_name")`. Call it for
class annotations and each declaration table. Emit `KEEP_RULE` with stable qualified details. Do not
treat custom annotations with the same short name as built-in.

- [ ] **Step 4: Rerun focused analyzer tests**

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerAnalysis*" --force-colors
```

Expected: all #795 analysis tests plus new #796 evidence tests pass.

### Task 7: Final documentation, formatting, and verification

**Files:**
- Modify as indicated by formatter/doc validation only.

- [ ] **Step 1: Run format/diff checks**

```sh
clang-format -i modules/foundry_script/fs_name_mangler_keep_rules.h \
  modules/foundry_script/fs_name_mangler_keep_rules.cpp \
  modules/foundry_script/fs_parser.h modules/foundry_script/fs_parser.cpp \
  modules/foundry_script/fs_compiler.cpp modules/foundry_script/fs_name_mangler_analysis.cpp \
  modules/foundry_script/tests/test_foundry_script.cpp \
  modules/foundry_script/tests/test_bytecode_script.h \
  modules/foundry_script/tests/test_name_mangler_keep_rules.h \
  modules/foundry_script/tests/test_name_mangler_analysis.h
git diff --check
```

- [ ] **Step 2: Run the required macOS CI-style build**

```sh
scons platform=macos target=editor dev_mode=yes tests=yes
```

Expected: exit 0 with warnings treated as errors.

- [ ] **Step 3: Run focused suites**

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*keep_name*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerKeepRules*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*NameManglerAnalysis*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*BytecodeScript*" --force-colors
```

- [ ] **Step 4: Run the relevant broader Foundry Script suite**

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*FoundryScript*" --progress-format=jsonl \
  --progress-file /tmp/foundry-issue-796-tests.jsonl --force-colors
```

Read the final doctest status and JSONL `run_end`; do not infer success from silence.

- [ ] **Step 5: Commit implementation**

```sh
git add modules/foundry_script docs/superpowers
git commit -m "Add name mangler keep escapes"
```

### Task 8: Independent review and delivery

**Files:**
- Modify only for technically validated Cursor findings.

- [ ] **Step 1: Run read-only Cursor review**

Use `cursor-review` with workspace `.worktrees/issue-796` and base `origin/develop`. Require a valid
`RESULT: clean`; for findings, use `receiving-code-review` and `systematic-debugging`, add a failing
test for each real bug, fix, verify, commit, and rerun Cursor.

- [ ] **Step 2: Push and open the PR**

Push `issue-796`, open a PR to `develop`, and end its body with `Closes #796`.

- [ ] **Step 3: Pause for the separate spec-compliance gate**

Send the parent the PR/head milestone. Do not enable auto-merge until explicit confirmation.

- [ ] **Step 4: Merge and clean up after approval**

Enable squash auto-merge, monitor the actual merge, then remove the worktree and delete local and
remote `issue-796` branches.
