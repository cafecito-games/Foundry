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

### Surface 1 — port PR #115958, extended to hint-based properties

`DocData::get_default_value_string()` gains a `const PropertyInfo &` parameter.
When the property usage flags mark an enum/bitfield, it resolves the integer to
constant names via `ClassDB` (or `CoreConstants` for global enums). All
prerequisite APIs (`CoreConstants::is_global_enum`, `get_enum_values`) already
exist in the fork.

**Extension beyond upstream:** PR #115958 only converts values whose
`PropertyInfo` already carries `PROPERTY_USAGE_CLASS_IS_ENUM/BITFIELD` — method
arguments, return values, and enum-typed properties. Registered properties
declared with `PROPERTY_HINT_ENUM`/`PROPERTY_HINT_FLAGS` (e.g.
`Control.size_flags_horizontal`) do *not* set that usage flag, so upstream
leaves them as raw integers. This fork additionally forwards the enum the doc
generator already derives from the property's getter (`doc_tools.cpp`,
`prop.enumeration`/`prop.is_bitfield`) into the resolver, so those properties
also render constant names. This widens the regenerated diff to ~240
`doc/classes/*.xml` files but makes the engine reference fully consistent.

### Surfaces 2 & 3 — resolve against `DataType.enum_values`

Rejected: routing through the engine resolver (`ClassDB`-based) — it does not
know script-defined enums, the common GDScript case.

Chosen: the GDScript analyzer already populates `DataType.enum_values`
(name -> int) on enum types for *both* native and script enums
(`gdscript_parser.h:147`). A small shared helper
(`GDScriptDocGen::docvalue_from_enum_value`) resolves a reduced integer against
that map by **exact match only**: an exact constant match returns the bare
constant name; anything else falls back to the raw integer.

Bitfield OR-decomposition is intentionally *not* performed on the GDScript
surfaces. GDScript's `DataType` carries no authoritative bitfield flag, and a
power-of-two heuristic misreads small sequential enums (e.g. `IDLE=0,
RUNNING=1, PAUSED=2`, where value `3` would wrongly render as
`RUNNING | PAUSED`). Decomposition therefore stays on the engine surface
(`core/doc_data.cpp`), where `PROPERTY_USAGE_CLASS_IS_BITFIELD` is available and
correct. The rare GDScript value that is a native-bitfield combination falls
back to the integer, which is acceptable and never misleading.

## Components

1. **Port PR #115958** — `core/doc_data.{h,cpp}`, `editor/doc/doc_tools.cpp`,
   plus regenerated `doc/classes/*.xml`.
2. **Shared helper** — `GDScriptDocGen::docvalue_from_enum_value(int64_t value,
   const HashMap<StringName,int64_t> &enum_values)`, exact-match to a constant
   name with integer fallback, reused by the LSP path.
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
