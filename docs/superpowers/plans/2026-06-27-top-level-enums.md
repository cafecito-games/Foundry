# Top-Level Enums (`enum_name`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a file-scope `enum_name` declaration that registers a project-global enum type usable from any script by name, modeled directly on the fork's existing `trait_name` feature.

**Architecture:** A new `ENUM_NAME` keyword parsed in the parser's top-level class-declaration phase stores the enum on the root `ClassNode`. The file registers as a `ScriptServer` global class flagged `is_enum`. The analyzer resolves the global name into a new *standalone* `DataType::ENUM` (no `ENUM_SEPARATOR` in `native_type`). The compiler hoists values as script constants. Completion, LSP hover, and docgen mirror the trait wiring.

**Tech Stack:** C++ (Godot engine), GDScript module (`modules/gdscript/`), `core/object/script_language.*`, doctest C++ tests, `.gd`/`.out` script fixtures.

**Reference template:** The `trait` / `trait_name` feature is the canonical pattern. At nearly every hook site there is an existing `is_trait` / `trait_name` / `TRAIT_NAME` line — add the `enum` parallel beside it. The spec is at `docs/superpowers/specs/2026-06-27-top-level-enums-design.md`.

**Build/test commands (this repo):**
- Build: `scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)`
- C++ tests: `./bin/godot.macos.editor.dev.* --headless --test --force-colors`
- GDScript fixtures run as part of the test suite; regenerate `.out` with: `./bin/godot.macos.editor.dev.* --headless --gdscript-generate-tests modules/gdscript/tests/scripts`
- Single-script analyzer check: `./bin/godot.macos.editor.dev.* --headless --check-only --script <file.gd>`

---

### Task 1: Tokenizer keyword `ENUM_NAME`

**Goal:** `enum_name` lexes to a new `Token::ENUM_NAME`.

**Files:**
- Modify: `modules/gdscript/gdscript_tokenizer.h` (Token enum, ~line 115-131, beside `CLASS_NAME`/`TRAIT_NAME`)
- Modify: `modules/gdscript/gdscript_tokenizer.cpp` (token name table ~line 110/126; `KEYWORD(...)` table ~line 529/565)

**Acceptance Criteria:**
- [ ] `Token::ENUM_NAME` exists in the `Type` enum.
- [ ] The token-name string table has `"enum_name"` at the matching index.
- [ ] `KEYWORD("enum_name", Token::ENUM_NAME)` is registered.
- [ ] Project builds.

**Verify:** `scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)` → builds clean.

**Steps:**

- [ ] **Step 1: Add the token type.** In `gdscript_tokenizer.h`, add `ENUM_NAME,` immediately after `CLASS_NAME,` in the `Token::Type` enum.

- [ ] **Step 2: Add the token-name string.** In `gdscript_tokenizer.cpp` token-name table, add `"enum_name", // ENUM_NAME,` at the index matching the enum position (keep table and enum in lockstep — verify alignment against neighbors).

- [ ] **Step 3: Register the keyword.** In the `GDSCRIPT_KEYWORD_LIST`/`KEYWORD(...)` macro block (near `KEYWORD("class_name", Token::CLASS_NAME)`), add:

```cpp
KEYWORD("enum_name", Token::ENUM_NAME) \
```

- [ ] **Step 4: Build.** Run the build command; confirm no enum/table mismatch warnings.

- [ ] **Step 5: Commit.**

```bash
git add modules/gdscript/gdscript_tokenizer.h modules/gdscript/gdscript_tokenizer.cpp
git commit -m "feat(gdscript): add enum_name tokenizer keyword"
```

---

### Task 2: Parser — root-class storage + top-level parse

**Goal:** `enum_name Name { ... }` parses at file scope into a new `ClassNode` field, reusing `parse_enum`.

**Files:**
- Modify: `modules/gdscript/gdscript_parser.h` (`ClassNode`: add fields near `is_trait`/`namespace_name`, ~line 964-976; declare `parse_enum_name()`)
- Modify: `modules/gdscript/gdscript_parser.cpp` (top-level guard ~line 839; dispatch switch ~line 942-961; add `parse_enum_name()` near `parse_trait_name()` ~line 1343; `qualified_global_name` computation sites ~line 1320/1348)

**Acceptance Criteria:**
- [ ] `ClassNode` has `bool is_enum_file = false;` and `EnumNode *enum_file_decl = nullptr;`.
- [ ] `enum_name Foo { A, B }` at file scope parses without error and populates both fields on the root class.
- [ ] `qualified_global_name` is set for an enum-file the same way as for `class_name` (namespace-aware).
- [ ] `enum_name` is accepted in the top-level phase, not in the class body.

**Verify:** `./bin/godot.macos.editor.dev.* --headless --check-only --script /tmp/enum_ok.gd` where the file is a valid `enum_name` → no errors.

**Steps:**

- [ ] **Step 1: Add fields to `ClassNode`** in `gdscript_parser.h`, beside `is_trait`:

```cpp
bool is_enum_file = false; // Root class only. File declares a top-level enum_name.
EnumNode *enum_file_decl = nullptr; // The single enum of an enum_name file.
```

- [ ] **Step 2: Declare the parse method** near `EnumNode *parse_enum(...)`:

```cpp
void parse_enum_name();
```

- [ ] **Step 3: Allow `enum_name` in the top-level phase.** In `gdscript_parser.cpp` line 839, add `|| current.type == GDScriptTokenizer::Token::ENUM_NAME` to the guard condition.

- [ ] **Step 4: Add the dispatch case** in the top-level switch (beside the `TRAIT_NAME` case ~line 953):

```cpp
case GDScriptTokenizer::Token::ENUM_NAME:
    advance();
    parse_enum_name();
    break;
```

Also add `ENUM_NAME` to the `next_type` validation lists at lines 902/927-928 so an annotation/modifier before `enum_name` is rejected the same way as before `class_name`.

- [ ] **Step 5: Implement `parse_enum_name()`** near `parse_trait_name()`. It sets the flag, parses the identifier as the global name (reusing the same identifier + `qualified_global_name` logic as `parse_class_name`), then reuses `parse_enum` for the brace body and stores it:

```cpp
void GDScriptParser::parse_enum_name() {
    if (current_class->is_enum_file) {
        push_error(R"(Only one "enum_name" is allowed per file.)");
    }
    current_class->is_enum_file = true;

    // Reuse the enum parser for the "Name { ... }" body. parse_enum reads the
    // identifier after the keyword and the brace body.
    DeclarationModifiers no_modifiers;
    EnumNode *enum_node = parse_enum(no_modifiers);
    current_class->enum_file_decl = enum_node;

    if (enum_node != nullptr && enum_node->identifier != nullptr) {
        current_class->identifier = enum_node->identifier;
        current_class->qualified_global_name = current_class->namespace_name.is_empty()
            ? String(enum_node->identifier->name)
            : current_class->namespace_name + "." + String(enum_node->identifier->name);
        current_class->fqcn = current_class->qualified_global_name;
    }
}
```

(Confirm against `parse_class_name()` at line 1317 and the `qualified_global_name` sites at 1320/1348 — match their exact field assignments. If `parse_enum` requires the `enum` token already consumed, advance/synthesize as `parse_class_name` does for its identifier.)

- [ ] **Step 6: Add fixtures** (these drive Task 3's validation too — create the valid one now):

`modules/gdscript/tests/scripts/parser/features/top_level_enum.gd`:
```gdscript
enum_name TrafficLight {
	RED,
	YELLOW,
	GREEN,
}
```
With its `.out` (generate in Step 8).

- [ ] **Step 7: Build.**

- [ ] **Step 8: Generate `.out` and verify.**

Run: `./bin/godot.macos.editor.dev.* --headless --gdscript-generate-tests modules/gdscript/tests/scripts/parser`
Expected: `top_level_enum.out` shows a clean parse (GDTEST_OK).

- [ ] **Step 9: Commit.**

```bash
git add modules/gdscript/gdscript_parser.h modules/gdscript/gdscript_parser.cpp modules/gdscript/tests/scripts/parser/features/top_level_enum.*
git commit -m "feat(gdscript): parse top-level enum_name declarations"
```

---

### Task 3: Validation — "exactly one enum, nothing else"

**Goal:** Reject every malformed enum-file shape with a clear diagnostic.

**Files:**
- Modify: `modules/gdscript/gdscript_parser.cpp` (`parse_enum_name`; class-body `ENUM_NAME` case ~line 1906; end-of-file/root checks)
- Modify: `modules/gdscript/gdscript_analyzer.cpp` (cross-cutting checks that need resolved state, if any)
- Test: `modules/gdscript/tests/scripts/analyzer/errors/enum_name_*.gd` + `.out`

**Acceptance Criteria:**
- [ ] `enum_name` inside a class body → error.
- [ ] Two `enum_name` in one file → error.
- [ ] `enum_name` together with `class_name`/`trait_name`/`extends`/`uses` → error.
- [ ] Any `func`/`var`/`const`/`signal`/`class`/inner `enum`/annotation in an enum-file → error ("an enum_name file may only contain its enum declaration").
- [ ] `namespace`/`import` before `enum_name` remain allowed.

**Verify:** `./bin/godot.macos.editor.dev.* --headless --test --force-colors` runs the analyzer error fixtures and they match `.out`.

**Steps:**

- [ ] **Step 1: Reject `enum_name` in class body.** Add a `case GDScriptTokenizer::Token::ENUM_NAME:` to `parse_class_body` (near the `TRAIT_NAME` rejection ~line 1906) that pushes: `"enum_name" can only be used at the top of a file.`

- [ ] **Step 2: Mutual exclusivity.** In `parse_class_name`, `parse_trait_name`, and the `extends`/`uses` parse paths, if `current_class->is_enum_file` is set, push an error: `An "enum_name" file cannot also declare class_name/trait_name/extends/uses.` Symmetrically, in `parse_enum_name`, error if `current_class->is_trait` or `current_class->identifier` (from class_name) or extends already set.

- [ ] **Step 3: Forbid other members in an enum-file.** In `parse_class_body`, at entry, if `current_class->is_enum_file` is true, push error for any member token (`VAR`/`CONST`/`FUNC`/`SIGNAL`/`CLASS`/`TRAIT`/`ENUM`/`ANNOTATION`): `An "enum_name" file may only contain its enum declaration.` (Allow `PASS`.)

- [ ] **Step 4: Write error fixtures.** Each is a `.gd` + a `.out` whose first line is `GDTEST_ANALYZER_ERROR` (match neighbors in `analyzer/errors/`):

`enum_name_in_class_body.gd`:
```gdscript
class Inner:
	enum_name Bad { A, B }
```
`enum_name_with_class_name.gd`:
```gdscript
class_name Foo
enum_name Bar { A, B }
```
`enum_name_extra_member.gd`:
```gdscript
enum_name Color { RED, GREEN }
func oops() -> void:
	pass
```
`enum_name_duplicate.gd`:
```gdscript
enum_name A { X }
enum_name B { Y }
```

- [ ] **Step 5: Build, generate `.out`, verify each error message.**

Run: `./bin/godot.macos.editor.dev.* --headless --gdscript-generate-tests modules/gdscript/tests/scripts/analyzer`
Then inspect each `.out` to confirm the intended message; adjust wording in code if needed and regenerate.

- [ ] **Step 6: Commit.**

```bash
git add modules/gdscript/gdscript_parser.cpp modules/gdscript/tests/scripts/analyzer/errors/enum_name_*
git commit -m "feat(gdscript): validate enum_name file shape"
```

---

### Task 4: Core — `ScriptServer` `is_enum` flag

**Goal:** The global-class registry can mark and report an entry as an enum.

**Files:**
- Modify: `core/object/script_language.h` (`GlobalScriptClass` ~line 61-68; `add_global_class` sig ~line 93; new `is_global_class_enum` ~near 106; `get_global_class_name` virtual sig ~line 480 — add `bool *r_is_enum = nullptr`)
- Modify: `core/object/script_language.cpp` (`add_global_class` impl ~line 466-495; cache (de)serialization ~line 316-332; new `is_global_class_enum`)

**Acceptance Criteria:**
- [ ] `GlobalScriptClass` has `bool is_enum = false;`.
- [ ] `add_global_class(...)` takes a trailing `bool p_is_enum` and stores it.
- [ ] `ScriptServer::is_global_class_enum(const String &)` returns the flag.
- [ ] Cache serialization round-trips `is_enum` (absent in old caches → defaults false, same pattern as `is_trait`).
- [ ] All existing `add_global_class` call sites updated (compile-clean).

**Verify:** Build succeeds; a C++ unit test (Task 11) asserts the round-trip.

**Steps:**

- [ ] **Step 1: Add the field** in `GlobalScriptClass` beside `is_trait`: `bool is_enum = false;`.

- [ ] **Step 2: Extend `add_global_class` signature** (header + impl) with a trailing `bool p_is_enum`. In the impl, mirror every `is_trait` line: the change-detection comparison (~line 477), the `existing->is_enum = p_is_enum;` update (~483), and `g.is_enum = p_is_enum;` (~495).

- [ ] **Step 3: Cache (de)serialization.** At lines ~316-318 and ~330-332, mirror the `is_trait` back-compat read and pass `is_enum` to `add_global_class`. At the serialization site (search for where `"is_trait"` is written into the cache dictionary), write `"is_enum"`.

- [ ] **Step 4: Add the lookup.** Mirror `is_global_class_trait`:

```cpp
bool ScriptServer::is_global_class_enum(const String &p_class) {
    ERR_FAIL_COND_V(!global_classes.has(p_class), false);
    return global_classes[p_class].is_enum;
}
```
Declare it in the header near `is_global_class_trait`.

- [ ] **Step 5: Extend the `get_global_class_name` virtual** with `bool *r_is_enum = nullptr` (header line ~480).

- [ ] **Step 6: Fix all call sites.** Build; for each compile error at an `add_global_class` / `get_global_class_name` call, pass the new arg (`false` / `nullptr` where not yet enum-aware; GDScript ones wired in Task 5).

- [ ] **Step 7: Commit.**

```bash
git add core/object/script_language.h core/object/script_language.cpp
git commit -m "feat(core): add is_enum flag to ScriptServer global classes"
```

---

### Task 5: Global registration — GDScript reports enum-files

**Goal:** A `.gd` enum-file registers as a global class flagged `is_enum`.

**Files:**
- Modify: `modules/gdscript/gdscript.cpp` (`GDScriptLanguage::get_global_class_name` ~line 3156-3282; mirror the `r_is_trait` out-param at line ~3275)
- Modify: `editor/file_system/editor_file_system.cpp` (~line 2617, the `add_global_class` call — thread `is_enum` through)
- Modify: `modules/gdscript/gdscript.h` (override sig to add `bool *r_is_enum`)

**Acceptance Criteria:**
- [ ] `get_global_class_name` on an `enum_name` file returns its `qualified_global_name` and sets `*r_is_enum = true` (and an empty/sentinel base).
- [ ] EditorFileSystem registers the enum-file via `add_global_class(..., /*is_enum*/ true)`.
- [ ] A normal class/trait file reports `is_enum = false`.

**Verify:** C++ test (Task 11) registers a fixture enum-file and asserts `ScriptServer::is_global_class_enum("TrafficLight") == true`.

**Steps:**

- [ ] **Step 1: Set the out-param.** In `get_global_class_name`, after parsing the head class, mirror the `r_is_trait` block (~line 3275):

```cpp
if (r_is_enum) {
    *r_is_enum = c->is_enum_file;
}
```
Ensure the returned name still uses `qualified_global_name` (line ~3281) — already correct for enum-files since Task 2 set it.

- [ ] **Step 2: Update the override signatures** in `gdscript.h`/`gdscript.cpp` to include `bool *r_is_enum = nullptr` matching the base virtual.

- [ ] **Step 3: Thread through EditorFileSystem.** At the registration call (~line 2617), capture `is_enum` from `get_global_class_name` and pass it to `add_global_class`. Mirror the existing `is_trait` capture exactly.

- [ ] **Step 4: Build.**

- [ ] **Step 5: Commit.**

```bash
git add modules/gdscript/gdscript.cpp modules/gdscript/gdscript.h editor/file_system/editor_file_system.cpp
git commit -m "feat(gdscript): register enum_name files as global enums"
```

---

### Task 6: Analyzer — standalone enum type resolution

**Goal:** Referencing a global enum name from another script yields a usable `DataType::ENUM`.

**Files:**
- Modify: `modules/gdscript/gdscript_analyzer.cpp` (`reduce_identifier` global-class branch ~line 10241; add `make_global_enum_type_from_path` near the other `make_*_enum_type` helpers ~line 777-866; audit `ENUM_SEPARATOR` split sites)
- Test: `modules/gdscript/tests/scripts/analyzer/features/top_level_enum_*.gd` + `.out`

**Acceptance Criteria:**
- [ ] In another script, `TrafficLight` resolves (meta enum type), `TrafficLight.RED` resolves to an `int` constant `0`.
- [ ] `var x: TrafficLight = TrafficLight.GREEN` type-checks.
- [ ] A `match` over a global-enum value type-checks.
- [ ] Namespaced enum requires the qualified name (or an `import`), same as classes.
- [ ] No regression: nested enums and native enums still resolve.

**Verify:** `--headless --test` runs the feature fixtures; they match `.out`.

**Steps:**

- [ ] **Step 1: Add the resolver helper.** Near `make_class_enum_type`/`make_global_enum_type` (~line 777-866):

```cpp
// Builds a standalone ENUM DataType for a top-level enum_name file.
// native_type is the global name itself (no ENUM_SEPARATOR), distinguishing it
// from class-nested enums.
static GDScriptParser::DataType make_global_enum_type_from_path(
        const StringName &p_global_name, const String &p_path, GDScriptParser *p_owner);
```
Implementation: obtain a parsed+analyzed view of `p_path` (use the same dependency-load path classes use — e.g. `get_parser_for`/`GDScriptCache::get_parser` as used elsewhere in this file for global classes), read `head->enum_file_decl`, build a `DataType` with `kind = ENUM`, `is_meta_type = true`, `builtin_type = DICTIONARY`, `enum_type = p_global_name`, `native_type = p_global_name` (no separator), and populate `enum_values` from the resolved `EnumNode` (mirror how `resolve_enum`/`make_class_enum_type` fills `enum_values` and the read-only `dictionary`). Set `script_path`/`class_type` to the source for locations.

- [ ] **Step 2: Branch in `reduce_identifier`.** At the `ScriptServer::is_global_class(name)` branch (~line 10241), before falling into `make_global_class_meta_type`, add:

```cpp
if (ScriptServer::is_global_class_enum(name)) {
    const String path = ScriptServer::get_global_class_path(name);
    p_identifier->set_datatype(make_global_enum_type_from_path(name, path, parser));
    return;
}
```
(Confirm the global-class-path accessor name in this file; classes already look it up here.)

- [ ] **Step 3: Harden `ENUM_SEPARATOR` assumptions.** Grep `gdscript_analyzer.cpp` for `ENUM_SEPARATOR` and `native_type` splits on `.`; guard each so a separator-less, globally-named enum is handled (e.g. when `native_type` has no separator, treat `enum_type`/`native_type` as the whole name). The completion path (Task 9) has a similar split — note it there.

- [ ] **Step 4: Write feature fixtures.** A defining file plus a consumer:

`analyzer/features/top_level_enum_consumer.gd`:
```gdscript
# Assumes a sibling enum_name file registered as global "TrafficLight".
func _ready() -> void:
	var x: TrafficLight = TrafficLight.GREEN
	print(x)
	print(TrafficLight.RED)
	match x:
		TrafficLight.RED:
			print("stop")
		TrafficLight.GREEN:
			print("go")
		_:
			print("other")
```
Plus the defining `enum_name` file in the same fixture dir (or reuse the runtime fixture from Task 8). If cross-file global registration is unavailable in the analyzer test harness, model the fixture the way existing global-class cross-file analyzer tests do (check `analyzer/features/` for an existing `class_name` cross-file fixture and copy its structure).

- [ ] **Step 5: Build, generate `.out`, verify.**

- [ ] **Step 6: Commit.**

```bash
git add modules/gdscript/gdscript_analyzer.cpp modules/gdscript/tests/scripts/analyzer/features/top_level_enum_*
git commit -m "feat(gdscript): resolve top-level enums as standalone enum types"
```

---

### Task 7: Compiler / runtime

**Goal:** A compiled enum-file exposes values as script constants so `Name.MEMBER` works at runtime.

**Files:**
- Modify: `modules/gdscript/gdscript_compiler.cpp` (enum handling ~line 3770-3775; add enum-file path)
- Test: `modules/gdscript/tests/scripts/runtime/features/top_level_enum.gd` + `.out`

**Acceptance Criteria:**
- [ ] At runtime, `TrafficLight.RED == 0`, `TrafficLight.GREEN == 2`.
- [ ] Iterating the bare name yields the read-only dictionary `{RED:0, YELLOW:1, GREEN:2}`.
- [ ] Custom values (`A = 5`) are honored.

**Verify:** runtime fixture prints expected values and matches `.out`.

**Steps:**

- [ ] **Step 1: Compile the enum-file.** In the compiler's class-member/constant pass, when the script's parsed root has `is_enum_file`, hoist each value as a script constant (reuse the unnamed-enum hoist: `p_script->constants.insert(value_name, value_int)` for each entry of `enum_file_decl->dictionary`) AND insert the read-only dictionary under the global name (`p_script->constants.insert(global_name, enum_file_decl->dictionary)`) so the bare name used as a value yields the dictionary. Reference the existing named-enum case at ~line 3770-3775 and the unnamed-enum hoist path.

- [ ] **Step 2: Runtime fixture.**

`runtime/features/top_level_enum.gd`:
```gdscript
enum_name TrafficLight {
	RED,
	YELLOW,
	GREEN = 5,
}

func test():
	print(TrafficLight.RED)
	print(TrafficLight.GREEN)
	for key in TrafficLight:
		print(key)
```

- [ ] **Step 3: Build, generate `.out`, verify** the printed values (`0`, `5`, then the three keys).

- [ ] **Step 4: Commit.**

```bash
git add modules/gdscript/gdscript_compiler.cpp modules/gdscript/tests/scripts/runtime/features/top_level_enum.*
git commit -m "feat(gdscript): compile top-level enums to script constants"
```

---

### Task 8: Code completion

**Goal:** Global enum names complete at type/expression positions; their members complete after `.`.

**Files:**
- Modify: `modules/gdscript/gdscript_editor.cpp` (global-class completion loop ~line 1720-1731; member-access ENUM path ~line 2093-2115)

**Acceptance Criteria:**
- [ ] A global enum name appears as a completion candidate (kind: enum/class) at type-annotation and expression positions.
- [ ] After `GlobalEnum.`, its values complete as constants.

**Verify:** Manual completion test via an LSP/completion fixture if present in `tests/`, else a focused C++ completion test mirroring existing global-class completion tests.

**Steps:**

- [ ] **Step 1: Surface global enum names.** In the global-classes completion loop (~line 1720-1731), the existing loop over `ScriptServer::get_global_class_list` already includes enum-files. Tag enum entries with `CODE_COMPLETION_KIND_ENUM` when `ScriptServer::is_global_class_enum(class_name)` (mirror the trait branch at ~line 1794). Ensure they are not filtered out by a class-only guard.

- [ ] **Step 2: Member completion after `.`.** The ENUM member-completion path (~line 2093-2115) splits `native_type` on `.`. For a standalone global enum, `native_type` has no separator; add a branch: when there's no `.`, source members from the analyzer's resolved `enum_values` (or load the defining script's constants) instead of `ClassDB::get_enum_constants`.

- [ ] **Step 3: Build; verify** completion candidates via the chosen test path.

- [ ] **Step 4: Commit.**

```bash
git add modules/gdscript/gdscript_editor.cpp
git commit -m "feat(gdscript): complete top-level enum names and members"
```

---

### Task 9: LSP hover & symbols

**Goal:** Hovering a global enum name resolves to its definition; the enum-file emits an Enum symbol with EnumMember children.

**Files:**
- Modify: `modules/gdscript/language_server/gdscript_workspace.cpp` (`resolve_symbol` ~line 736-810, global-class branch ~line 760)
- Modify: `modules/gdscript/language_server/gdscript_extend_parser.cpp` (symbol generation; root-class kind ~line 215; enum-symbol code ~line 399-437)

**Acceptance Criteria:**
- [ ] `textDocument/hover` over a global enum name returns the enum's symbol/documentation.
- [ ] An `enum_name` file's document symbols include a root `SymbolKind::Enum` with `EnumMember` children for each value.

**Verify:** Existing LSP test harness under `modules/gdscript/tests/` (or manual LSP request). Mirror an existing class/trait hover test if present.

**Steps:**

- [ ] **Step 1: Resolve global enum in hover.** In `resolve_symbol`, the `ScriptServer::is_global_class(symbol_identifier)` branch (~line 760) already fetches the script symbol via `get_script_symbol`. Confirm an enum-file's script symbol exposes the enum; if the enum-file produces no class symbol, add handling so `get_script_symbol` returns the enum as the document's primary symbol.

- [ ] **Step 2: Emit the root enum symbol.** In `gdscript_extend_parser.cpp`, where the root class symbol is built (~line 215), when `p_class->is_enum_file`, set the root symbol kind to `SymbolKind::Enum`, name to the global name, and build `EnumMember` children from `enum_file_decl->values` (reuse the nested-enum symbol code at ~line 399-437). Detail line: `enum <Name>`.

- [ ] **Step 3: Build; verify** hover + symbols via the test path.

- [ ] **Step 4: Commit.**

```bash
git add modules/gdscript/language_server/gdscript_workspace.cpp modules/gdscript/language_server/gdscript_extend_parser.cpp
git commit -m "feat(gdscript): LSP hover and symbols for top-level enums"
```

---

### Task 10: Class-reference doc generation

**Goal:** An `enum_name` file produces a class-reference doc entry documenting the enum and its values.

**Files:**
- Modify: `modules/gdscript/editor/gdscript_docgen.cpp` (`_generate_docs` ~line 401-725; enum member doc ~line 630-660; `is_trait`/`is_enum` flag extraction)
- Modify: `core/doc_data.h` (`ClassDoc`: add `bool is_enum = false;` beside `is_trait` ~line 720, if docgen/UI needs it)

**Acceptance Criteria:**
- [ ] Generating docs for an enum-file yields a `ClassDoc` whose `enums`/`constants` describe the enum, with `## ` doc comments on the enum and values attached.
- [ ] The class reference renders the enum-file like other GDScript globals.

**Verify:** Run docgen over the runtime fixture (or a dedicated fixture) and inspect the produced `DocData`/XML for the enum + constants + descriptions.

**Steps:**

- [ ] **Step 1: (If needed) add `is_enum` to `ClassDoc`** in `core/doc_data.h` beside `is_trait`.

- [ ] **Step 2: Handle enum-files in `_generate_docs`.** Near the trait flag handling (~line 407), set `doc.is_enum = p_class->is_enum_file;`. When `is_enum_file`, run the existing enum-member doc logic (~line 630-660) against `p_class->enum_file_decl` so `doc.enums[name]` and the value `constants` (with `enumeration = name`, `type = "int"`, descriptions from `doc_data`) are populated — even though the enum is the file root rather than a class member.

- [ ] **Step 3: Doc-comment attachment.** Confirm `## ` comments above the `enum_name` and above each value are parsed into `enum_file_decl->doc_data` / each value's `doc_data` (the parser's doc-comment attachment should already cover this since `parse_enum` is reused; add a fixture comment to verify).

- [ ] **Step 4: Build; verify** generated docs over a fixture with doc comments.

- [ ] **Step 5: Commit.**

```bash
git add modules/gdscript/editor/gdscript_docgen.cpp core/doc_data.h
git commit -m "feat(gdscript): generate class reference docs for top-level enums"
```

---

### Task 11: C++ unit tests + final fixture sweep + primer

**Goal:** Lock behavior with C++ tests, regenerate all fixtures, document the feature.

**Files:**
- Create/Modify: a GDScript C++ test (e.g. `modules/gdscript/tests/test_gdscript.h` or the existing global-class test) asserting registration + resolution
- Modify: `docs/gdscript_language_primer.md` (add an `enum_name` section)
- Regenerate: all touched `.out` fixtures

**Acceptance Criteria:**
- [ ] A C++ test registers a fixture enum-file and asserts `ScriptServer::is_global_class_enum("...") == true` and that `add_global_class`/cache round-trips `is_enum`.
- [ ] Full test suite passes: `--headless --test`.
- [ ] The language primer documents `enum_name` syntax, semantics, and the one-enum-per-file rule.

**Verify:** `./bin/godot.macos.editor.dev.* --headless --test --force-colors` → all pass; `--gdscript-generate-tests` produces no unexpected diffs afterward.

**Steps:**

- [ ] **Step 1: Add the C++ test** mirroring an existing global-class registration test (search `tests/` and `modules/gdscript/tests/` for `is_global_class` / `add_global_class` usage). Assert flag set on registration and survives a serialize→deserialize cache cycle.

- [ ] **Step 2: Run the full suite**, fix any regressions.

- [ ] **Step 3: Regenerate fixtures** across `parser`, `analyzer`, `runtime`: `--gdscript-generate-tests modules/gdscript/tests/scripts`; commit only intended diffs (beware spurious diffs — see project memory on generate-tests).

- [ ] **Step 4: Update the primer** with an `enum_name` section (syntax, global-type semantics, namespace behavior, validation rules), matching the existing `class_name`/`trait` sections' style.

- [ ] **Step 5: Commit.**

```bash
git add modules/gdscript/tests docs/gdscript_language_primer.md
git commit -m "test(gdscript): cover top-level enums end-to-end; document enum_name"
```

---

## Self-Review

**Spec coverage:** Every spec section maps to a task — syntax/grammar → T1-T2; semantics & registration → T4-T5; analyzer resolution → T6; compilation/runtime → T7; validation → T3; editor surfaces (completion/LSP/docgen) → T8/T9/T10; testing → fixtures throughout + T11. No gaps.

**Dependency graph:** T1 → T2 → T3; T2 → T6; T4 → T5 → {T6, T8, T9, T10}; T6 → {T7, T8, T9}; all → T11. Encoded in `.tasks.json`.

**Known soft spots flagged for the implementer (not placeholders):** exact dependency-load accessor in T6 Step 1 and the global-class-path accessor in T6 Step 2 must be confirmed against existing call sites in `gdscript_analyzer.cpp`; the cross-file analyzer fixture shape in T6 Step 4 must copy an existing `class_name` cross-file test. These are "confirm against the canonical trait/class site," not unspecified work.
