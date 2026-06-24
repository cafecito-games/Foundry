# Enum/Bitfield Default Values as Constant Names

**Date:** 2026-06-23
**Status:** Approved design
**Upstream reference:** godotengine/godot PR #115958 (open)

## Goal

Display enum and bitfield default values as readable constant names (or an OR
combination of names for bitfields) instead of raw magic numbers, everywhere a
default value is shown in this fork:

1. Engine class-reference XML and the in-editor Help panel.
2. GDScript-generated class documentation.
3. GDScript language-server hover tooltips and signature help.

Surfaces 2 and 3 are this fork's value-add beyond the upstream PR, which only
addresses surface 1.

## The problem

An enum default value is rendered as a raw integer in three independent code
paths that share no logic:

| # | Surface | File | Current rendering |
|---|---------|------|-------------------|
| 1 | Engine class-ref XML | `core/doc_data.cpp` | `itos(value)` |
| 2 | GDScript class docs | `modules/gdscript/editor/gdscript_docgen.cpp` | `Variant::get_construct_string()` |
| 3 | GDScript LSP hover / symbols | `modules/gdscript/language_server/gdscript_extend_parser.cpp` | `reduced_value.to_json_string()` |

## Outcome

```
Control.size_flags_horizontal = 1            ->  = SIZE_FILL
default = 2305843009213693953                ->  = KEY_MASK_CMD_OR_CTRL | KEY_MASK_SHIFT
@export var mode: Mode = 1                    ->  = RUNNING       (GDScript docs)
set_mode(mode: Mode = 0) -> void             ->  = IDLE          (LSP hover)
```

Purely a display/documentation improvement; no runtime behavior change.

## Approach

### Surface 1 — port PR #115958 verbatim

`DocData::get_default_value_string()` gains a `const PropertyInfo &` parameter.
When the property usage flags mark an enum/bitfield, it resolves the integer to
constant names via `ClassDB` (or `CoreConstants` for global enums). All
prerequisite APIs (`CoreConstants::is_global_enum`, `get_enum_values`) already
exist in the fork. Three code files change; ~40 `doc/classes/*.xml` files are
regenerated to match.

### Surfaces 2 & 3 — resolve against `DataType.enum_values`

Rejected: routing through the engine resolver (`ClassDB`-based) — it does not
know script-defined enums, the common GDScript case.

Chosen: the GDScript analyzer already populates `DataType.enum_values`
(name -> int) on enum types for *both* native and script enums
(`gdscript_parser.h:147`). A small shared helper resolves a reduced integer
against that map:

- exact single match -> constant name;
- else, if the value is a clean OR of known constants -> `A | B`;
- else, raw number fallback.

GDScript's `DataType` has no explicit bitfield flag, so decomposition is
attempted generically with a numeric fallback rather than flag-gated.

## Components

1. **Port PR #115958** — `core/doc_data.{h,cpp}`, `editor/doc/doc_tools.cpp`,
   plus regenerated `doc/classes/*.xml`.
2. **Shared helper** — `gdscript_docgen` static method resolving
   `(int64_t value, const HashMap<StringName,int64_t> &enum_values)` to a name
   or OR-combination, reused by the LSP path.
3. **GDScript docgen (surface 2)** — thread the variable/parameter `DataType`
   into `docvalue_from_expression`/`_docvalue_from_variant`; call the helper for
   enum-typed integer constants.
4. **GDScript LSP (surface 3)** — at `gdscript_extend_parser.cpp` member and
   parameter rendering sites, call the helper for enum datatypes instead of
   `to_json_string()`.
5. **Tests** — GDScript doc fixtures under `modules/gdscript/tests/scripts/`
   covering a script enum default, a native enum default, a native bitfield
   combination, and a non-matching integer (fallback).

## Risks / caveats

- Upstream PR #115958 is still open; reviewers noted that changing the XML
  `default=` format from numbers to names may affect third-party doc parsers.
  Surfaces 2/3 are carried as a permanent fork patch (consistent with the
  fork's charter).
- Regenerating the engine doc XML requires a full editor build and a
  `--doctool` run.
