# Top-Level Enums (`enum_name`) — Design

**Date:** 2026-06-27
**Status:** Approved (brainstorm complete)
**Module:** `modules/foundry_script/` (plus a small `core/object/script_language.*` change)

## Summary

Add a file-scope enum declaration, `enum_name`, that registers a
**project-global enum type** — usable from any other script by name without
`import`/`preload`, exactly like a `class_name`'d class. The feature is modeled
directly on the fork's existing `trait` / `trait_name` work: a global class
entry flagged as an enum, resolved by the analyzer into a standalone enum type,
compiled into a constant-only script, and surfaced in completion, LSP hover, and
the class reference.

```gdscript
namespace my_game.enums   # optional, same rules as class_name

enum_name CharacterState {
    IDLE,
    RUNNING,
    JUMPING,
}
```

Consumed elsewhere:

```gdscript
var state: CharacterState = CharacterState.IDLE

func set_state(s: CharacterState) -> void:
    match s:
        CharacterState.IDLE: ...
        CharacterState.RUNNING: ...
        CharacterState.JUMPING: ...

for name in CharacterState:        # iterates the read-only {IDLE:0, ...} dictionary
    print(name)
```

## Goals

- A top-level enum is a **global named type**, like `class_name`. Bare
  `CharacterState` (or `my_game.enums.CharacterState` when namespaced) resolves
  from any script — no import needed; an `import` may shorten a namespaced name,
  same as for classes.
- Familiar syntax: a new `enum_name` keyword + the existing brace body
  (`{ A, B, C = 5, ... }`), reusing the current enum value parser.
- An `enum_name` file is **exactly one enum and nothing else** (optionally
  preceded by `namespace` / `import` lines), mirroring "one `class_name` per
  file."
- Full editor parity with traits: code completion, LSP hover/symbols, and
  class-reference doc generation.

## Non-Goals

- Multiple top-level enums per file.
- A top-level enum coexisting with a `class_name`/class body in the same file.
- Non-`int` backing types (enums remain `int`, like all Foundry Script enums).
- A new engine-level "global enum registry" distinct from the existing
  path-keyed global-class system.

## Background (current state)

- The fork already supports `namespace`, `import`, `uses`, `trait_name`, and
  `class_name` as top-level concepts. Global types register through
  `ScriptServer::add_global_class()`, keyed by file path, storing
  `{language, path, base, is_abstract, is_tool, is_trait}` — **no enum metadata
  and no non-script globals.**
- Enums today live only inside class bodies. Every enum `DataType` is nested:
  `native_type = "BaseClass" + ENUM_SEPARATOR + "EnumName"`. **No enum stands
  alone.**
- `qualified_global_name` on the root `ClassNode` already encodes
  `namespace + "." + name`; `GDScriptLanguage::get_global_class_name()` extracts
  it from source without the analyzer; `EditorFileSystem` registers it.

Implication: a top-level enum is implemented as **a global class whose
resolution yields an ENUM type instead of a CLASS type**, gated by a new
`is_enum` flag — exactly parallel to how `is_trait` works.

## Design

### 1. Syntax & grammar

- New tokenizer keyword `ENUM_NAME` (`gdscript_tokenizer.{h,cpp}`), alongside
  `CLASS_NAME` / `TRAIT_NAME`.
- Parsed in the **top-level class-declaration phase** of the parser
  (`gdscript_parser.cpp`, where `class_name` / `trait_name` / `extends` are
  handled), **not** in `parse_class_body`. It reuses the existing `parse_enum`
  value parser for the `{ ... }` body, so custom values, trailing commas, and
  doc comments work unchanged.
- Storage: a new field on the root `ClassNode`, e.g.
  `EnumNode *enum_file_decl = nullptr;` plus `bool is_enum_file = false;`,
  parallel to `namespace_name`. `qualified_global_name` / `fqcn` are set to the
  enum's (optionally namespaced) name, identical to the `class_name` path.
- Backing type stays `int`.

### 2. Semantics & global registration

- **Core change** (`core/object/script_language.{h,cpp}`): add `bool is_enum`
  to `GlobalScriptClass`; thread it through `add_global_class(...)`; add
  `static bool ScriptServer::is_global_class_enum(const String &p_class)` — all
  exactly parallel to the existing `is_trait` / `is_global_class_trait`.
- `GDScriptLanguage::get_global_class_name()` (`gdscript.cpp`) extended to detect
  the enum-file case and report `is_enum` (mirroring the `is_trait` out-param at
  the existing extraction site). Base is left empty/sentinel.
- `EditorFileSystem` passes the flag through registration, same as traits.
- Namespace qualification is inherited verbatim: a namespaced enum is referenced
  by its qualified name unless `import`ed.

### 3. Name resolution (analyzer)

The only genuinely new type-system concept: a **standalone `DataType::ENUM`**
(one whose `native_type` has no `ENUM_SEPARATOR`).

- In `reduce_identifier` (`gdscript_analyzer.cpp`, the
  `ScriptServer::is_global_class(name)` branch): if the global is flagged
  `is_enum`, branch to a new helper (`make_global_enum_type_from_path` or
  similar) that:
  1. Obtains a parsed/analyzed view of the target file by path (the same
     dependency-load machinery classes already use).
  2. Reads the single `EnumNode` and builds a `DataType` with `kind == ENUM`,
     `is_meta_type == true`, `builtin_type == DICTIONARY`,
     `enum_type = <EnumName>`, and `enum_values` populated from the resolved
     values.
  3. Sets `native_type` to the **qualified global name standing alone** (no
     `ENUM_SEPARATOR`); `class_type` / `script_path` point at the source for the
     read-only dictionary and source locations.
- Downstream reuses existing nested-enum logic unchanged:
  - `CharacterState.IDLE` → `reduce_identifier_from_base` already handles
    `base.kind == ENUM && is_meta_type` → returns the `int` constant.
  - `var x: CharacterState` as a type annotation flows through `resolve_datatype`
    like any enum.
- Audit and harden the handful of spots that assume an ENUM `native_type` always
  contains `ENUM_SEPARATOR` (base-vs-name splitting) so they tolerate a
  separator-less, globally-named enum.

### 4. Compilation & runtime

A global name resolves at runtime to the compiled `Foundry Script` at that path;
`Name.MEMBER` reads a constant `MEMBER` off that script. So
(`gdscript_compiler.cpp`) for an enum-file:

- Hoist each enum value as a top-level script constant
  (`constants["IDLE"] = 0`, …), reusing the existing **unnamed-enum** hoist path
  (`m_enum->dictionary` already holds the resolved map). This makes
  `CharacterState.IDLE` work at runtime.
- Also store the read-only enum dictionary under the global enum name so the bare
  name used as a *value* (e.g. `for k in CharacterState:`) yields
  `{IDLE:0, ...}`, matching nested named-enum value semantics.
- No instances are ever created; the compiled enum-file is a pure constant
  container.

### 5. Validation rules

An `enum_name` file is "exactly one enum, nothing else." Diagnostics:

- `enum_name` anywhere but file scope (e.g. inside a class body) → error.
- More than one `enum_name` in a file → error ("only one enum_name per file").
- `enum_name` combined with `class_name` / `trait_name` / `extends` / `uses` →
  error (mutually exclusive file kinds).
- Any other top-level member in an enum-file — `func`, `var`, `const`, `signal`,
  `class`, inner `enum`, annotations (`@tool`, `@icon`, …) → error ("an
  enum_name file may only contain its enum declaration"). `namespace` and
  `import` remain allowed.
- Empty body (`enum_name Foo {}`) → match whatever `parse_enum` already does for
  empty enums.
- Duplicate value names / global-name collision with an existing `class_name` →
  reuse existing diagnostics.

### 6. Editor surfaces (parity with traits)

**Code completion** (`gdscript_editor.cpp`):
- Add a global-enum loop alongside the global-class loop (gated on
  `ScriptServer::is_global_class_enum()`) so enum names appear at type-annotation
  and expression positions.
- `.MEMBER` completion after a global enum reuses the existing ENUM-datatype
  completion path.

**LSP hover & symbols** (`language_server/gdscript_workspace.cpp` `resolve_symbol`,
`gdscript_extend_parser.cpp`):
- `resolve_symbol` resolves the global enum name (via the
  `ScriptServer::is_global_class` check) to its defining script and returns the
  enum symbol.
- Emit a root `LSP::SymbolKind::Enum` with `EnumMember` children, reusing the
  existing enum-symbol generation.

**Class-reference docs** (`editor/gdscript_docgen.cpp`):
- Extract the `is_enum` flag in `_get_global_class_name()` (mirror `is_trait`).
- Generate a class-reference doc for the enum-file whose `enums` / `constants`
  come from the existing enum-doc path; attach `## ` doc comments on the enum and
  on each value.

## Testing

Following `modules/foundry_script/tests/scripts/` conventions (`.fs` + `.out` pairs);
regenerate `.out` via `--gdscript-generate-tests` after the behavior lands.

- **analyzer/features**: bare and namespaced `enum_name`; consume from another
  fixture as a type annotation, as `Name.VALUE`, in a `match`, as a function
  param/return, and iterate the bare name as a dictionary.
- **analyzer/errors**: each validation rule above (member in enum-file,
  `enum_name` + `class_name`, `enum_name` inside a class body, two `enum_name`s,
  etc.) with expected `.out`.
- **runtime**: print `CharacterState.IDLE`, the hoisted constant values, and the
  iterated dictionary; assert output.
- **C++**: a global-registration test asserting an enum-file registers via
  `ScriptServer` with `is_enum == true` and resolves by name; an
  `is_global_class_enum()` unit check.
- **editor**: completion candidate for a global enum name and its members; LSP
  hover/symbol for an `enum_name`; doc-gen output for an `enum_name` file.

## Implementation order (epic breakdown)

Intended GitHub epic in the **Experiment** project, with native sub-issues:

1. **Tokenizer + parser**: `ENUM_NAME` keyword; top-level parse into
   `ClassNode.enum_file_decl` / `is_enum_file`; reuse `parse_enum` body.
2. **Validation**: the "one enum, nothing else" rules + diagnostics + error
   fixtures.
3. **Core ScriptServer**: `is_enum` on `GlobalScriptClass`,
   `add_global_class` thread-through, `is_global_class_enum()`.
4. **Global registration**: `get_global_class_name()` enum detection +
   `EditorFileSystem` wiring.
5. **Analyzer resolution**: standalone `DataType::ENUM`,
   `make_global_enum_type_from_path`, `ENUM_SEPARATOR`-assumption hardening +
   feature fixtures.
6. **Compiler/runtime**: constant hoist + dictionary constant + runtime
   fixtures.
7. **Code completion**: global enum names + member completion.
8. **LSP hover & symbols**.
9. **Class-reference doc generation**.
10. **Docs/primer + final fixture regeneration** (update
    `docs/gdscript_language_primer.md` if applicable).

Dependencies: 1 → {2, 5}; 3 → {4, 5, 7, 8, 9}; 5 → {6, 7, 8, 9}.
