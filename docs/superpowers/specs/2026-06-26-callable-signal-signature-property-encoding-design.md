# Encode Callable/Signal method signatures across the PropertyInfo/MethodInfo boundary

Design spec for [#414](https://github.com/cafecito-games/godot/issues/414). Part of epic [#125](https://github.com/cafecito-games/godot/issues/125). Resolves the soundness hole tracked in [#412](https://github.com/cafecito-games/godot/issues/412).

## Problem

Typed `Callable`/`Signal` signatures (`Callable[[int], void]`, `Signal[int]`) are erased whenever a callable or signal crosses the cross-script API boundary. `Script::get_method_info` / `get_script_signal_list` expose only `MethodInfo`/`PropertyInfo`, and `PropertyInfo` has **no field that carries a callable/signal method signature**.

Concretely:

- `GDScriptParser::DataType::to_property_info` (`modules/gdscript/gdscript_parser.cpp`) encodes element types only for `Array` (`PROPERTY_HINT_ARRAY_TYPE`) and `Dictionary` (`PROPERTY_HINT_DICTIONARY_TYPE`). A `CALLABLE`/`SIGNAL` falls through to a bare `result.type` with no hint — the signature is dropped.
- `GDScriptAnalyzer::type_from_property` (`modules/gdscript/gdscript_analyzer.cpp`) rebuilds `Array`/`Dictionary` signatures from their hints but returns a bare, empty-signature `Callable`/`Signal`.
- A bare-signature callable compares compatible with *any* signature (gradual-typing accept), so cross-script mismatches go uncaught.

The signature-aware comparators added in [#327](https://github.com/cafecito-games/godot/issues/327) / [#382](https://github.com/cafecito-games/godot/issues/382) already recurse through `method_parameter_types` / `method_return_type` — they simply never receive that data across the script-API boundary because the serialization format cannot express it.

### Reproducing cases (from #412)

A provider in a separate script:

```gdscript
# provider.gd
func get_cb() -> Callable[[Callable[[int], void]], void]:
    ...
```

Consumer:

```gdscript
const Provider = preload("provider.gd")

func value_boundary() -> void:
    # value-boundary erasure: outer signature lost crossing the API
    var handler: Callable[[Callable[[String], void]], void] = Provider.new().get_cb()  # mismatch not caught

func nested_callable() -> void:
    # nested-callable erasure under a function reference
    var fn := Provider.new().get_cb  # function reference; nested Callable param signature lost
```

Both must be caught once signatures survive the boundary.

## Goals

- A typed `Callable`/`Signal` exported from one script and read in another preserves its **full, recursively-nested** signature for compatibility checks.
- Both #412 repros are caught.
- Existing untyped callables are byte-for-byte unaffected (no hint emitted).
- `.tres`/scene round-trip and Inspector rendering degrade gracefully where the hint is not understood.
- The encoding is documented as the canonical format so C#/GDExtension (epic [#144](https://github.com/cafecito-games/godot/issues/144)) can adopt it later without a format change.

## Non-goals

- No C#/GDExtension implementation in this issue — GDScript-first, format reserved as canonical.
- No new field on `PropertyInfo` (the struct carries a maintainer note reserving additions). The encoding rides on the existing `hint` / `hint_string` fields.
- Arity flexibility (default-argument counts, varargs), exact local-script identity, a shared core decoder, and format versioning are deferred — see [Future extensions](#future-extensions).

## Design

### 1. New property hint — single shared `PROPERTY_HINT_CALLABLE_TYPE`

Append one value immediately before `PROPERTY_HINT_MAX` in `core/object/object.h` (appending preserves existing enum values / binary compatibility):

```cpp
enum PropertyHint {
    ...
    PROPERTY_HINT_FILE_PATH,
    PROPERTY_HINT_CALLABLE_TYPE, // hint_string carries the encoded method signature.
    PROPERTY_HINT_MAX,
};
```

A single hint covers both kinds. The `PropertyInfo.type` field (`Variant::CALLABLE` vs `Variant::SIGNAL`) already disambiguates, and a signal is simply a callable without a return clause. One enum value, one decoder.

### 2. Encoding grammar — mirror the existing `to_string` surface syntax

`DataType::to_string` already renders these recursively:

- Callable → `Callable[[int, String], bool]`
- Signal → `Signal[[int, String]]`
- Array → `Array[int]`
- Dictionary → `Dictionary[String, int]`

`hint_string` carries exactly the **signature suffix** that follows the bare type name — the substring `to_string` already produces. This keeps the encoder nearly free and the format human-readable in `.tres`/debug dumps.

| DataType | `PropertyInfo.type` | `hint` | `hint_string` |
|---|---|---|---|
| `Callable[[int], bool]` | CALLABLE | CALLABLE_TYPE | `[[int], bool]` |
| `Signal[int]` | SIGNAL | CALLABLE_TYPE | `[[int]]` |
| `Callable[[Callable[[int], void]], void]` | CALLABLE | CALLABLE_TYPE | `[[Callable[[int], void]], void]` |
| `Callable[[Array[int]], void]` | CALLABLE | CALLABLE_TYPE | `[[Array[int]], void]` |
| `Callable[[Dictionary[String, int]], void]` | CALLABLE | CALLABLE_TYPE | `[[Dictionary[String, int]], void]` |

Grammar (self-similar, balanced brackets):

```
sig       := "[[" args "]" ["," WS ret] "]"      ; ret clause present for Callable, absent for Signal
args      := ε | type (", " type)*
type      := leaf
           | "Callable" sig
           | "Signal"   sigNoRet                  ; sigNoRet = sig without the ret clause
           | "Array" "[" type "]"
           | "Dictionary" "[" type ", " type "]"
ret       := type                                 ; "void" for a NIL/absent return
leaf      := same name grammar as PROPERTY_HINT_ARRAY_TYPE
             (builtin name / native class / global class name / Enum.Member)
WS        := " "
```

The grammar is **uniformly recursive over every container-bearing type** — `Callable`, `Signal`, `Array`, and `Dictionary` — so a parameter that is itself a typed container preserves its full signature rather than degrading to its bare outer type. (Recursing only on `Callable`/`Signal` would leave `Callable[[Array[int]], void]` erasing its `Array[int]` param to bare `Array` — the same soundness class this issue closes.)

The encoder emits canonical spacing (`", "` between args, `"]"`/`", "` before the return) so the format is stable and matches `to_string`. The decoder tolerates the canonical form; it is not required to accept arbitrary whitespace.

### 3. Emit — `DataType::to_property_info`

Add a `CALLABLE`/`SIGNAL` branch in the `BUILTIN` case, guarded by `has_explicit_method_signature`:

- When `has_explicit_method_signature` is true: set `result.hint = PROPERTY_HINT_CALLABLE_TYPE` and `result.hint_string` to the encoded signature (the `to_string` signature suffix, built from `method_parameter_types` / `method_return_type`; the return clause is included only for `CALLABLE`).
- When false (bare untyped callable/signal): emit no hint — identical to today's behavior.

This is the existing render path factored to write into `hint_string` instead of (or in addition to) the human-readable string. Leaf-name production reuses the same name-selection logic already used by the `ARRAY_TYPE`/`DICTIONARY_TYPE` branches (builtin name, native class, global class name with native-base fallback, `Enum.Member`).

### 4. Parse — `GDScriptAnalyzer::type_from_property`

Add a branch: when `p_property.type` is `CALLABLE`/`SIGNAL` and `p_property.hint == PROPERTY_HINT_CALLABLE_TYPE`, decode `hint_string` into `method_parameter_types` / `method_return_type` and set `has_method_signature` / `has_explicit_method_signature`.

Implement a small recursive-descent decoder:

- Split `args` on commas **at bracket depth zero**; recurse on each `type`.
- A `type` beginning with `Callable`/`Signal`/`Array`/`Dictionary` followed by `[` recurses into the corresponding container reconstruction; otherwise it is a leaf resolved with the existing `ARRAY_TYPE` leaf-resolution logic (builtin / native via `class_exists` / global class via `ScriptServer` + `ResourceLoader`).
- For `Signal`, omit the return clause; for `Callable`, the final element after the inner `]` is the return type (`void` → `NIL`).
- Reuse the leaf-resolution code shared with the `Array`/`Dictionary` branches (extract a helper rather than duplicating the four-way builtin/native/global-class/enum resolution).

Once `type_from_property` recovers nested signatures, `explicit_callable_type_from_info` and `explicit_signal_type_from_info` recover them **automatically** — both already route every argument and the return through `type_from_property` (`gdscript_analyzer.cpp:9664`, `:10619`). No change is needed there beyond what `type_from_property` now returns.

### 5. Comparison — no change

The signature-aware comparators from #327/#382 already recurse through `method_parameter_types` / `method_return_type`. Once the rich data survives the boundary, the mismatches in #412 are caught with no comparator change.

## Round-trip and graceful degradation

An unknown hint is inert everywhere it is not understood:

- **Inspector / `EditorHelp`** — fall back to the plain `Callable`/`Signal` label. Verify rendering does not choke on the new hint value; default-case handling should already ignore unknown hints.
- **Generated class-reference docs** — the doc generator ignores the hint; verify no spurious diffs in generated docs.
- **Resource `.tres` / scene round-trip** — `PropertyInfo.hint_string` is a plain `String` serialized verbatim by the resource format with no hint-specific parsing, so it is structurally opaque-passthrough. In practice typed callable signatures are method-local rather than `@export`ed properties saved to disk, so the path is rarely if ever exercised; existing untyped callables emit no hint and are unchanged. An encode↔decode round-trip unit test guards the format's stability directly.
- **Other language bindings** — see Non-goals; the format is reserved canonical, no binding reads it yet.

## Ripple-review checklist

Per the issue's engine-wide-serialization caution:

- [ ] Inspector / `EditorHelp` rendering of the new hint (graceful fallback).
- [ ] Generated class-reference docs unaffected.
- [ ] Resource `.tres`/scene round-trip of properties carrying the hint.
- [ ] Existing untyped callables emit no hint and are byte-for-byte unchanged.
- [ ] C#/GDExtension parity (epic #144): format documented as canonical before any binding ships it.

## Testing

GDScript fixtures under `modules/gdscript/tests/scripts/` (pair `.gd` with expected-output config where the runner expects it):

- **Both #412 repros** — value-boundary erasure and nested-callable-under-function-reference erasure, each across a separate provider script, asserting the mismatch is now reported.
- **Round-trip parity** — a typed `Callable`/`Signal` exported from one script and consumed in another, with a matching signature, accepts; with a mismatched nested signature, rejects.
- **Container-in-signature** — `Callable[[Array[int]], void]` and `Callable[[Dictionary[String, int]], void]` across the boundary preserve their nested container element types.
- **Untyped-callable regression** — an untyped `Callable`/`Signal` property still emits no hint and behaves exactly as before.
- **Encode/decode unit coverage** — a C++ or script-level check that `to_property_info` → `type_from_property` round-trips representative signatures, including deep nesting.

Regenerate `.out` fixtures after intentional behavior changes via `--gdscript-generate-tests`.

## Future extensions

Deferred; capture as the format/encoding matures.

1. **Arity flexibility — default-arg count & vararg flags.** The signature encodes a fixed parameter list. Callables compare with default/bound-argument tolerance (`transformed_callable_type` juggles default-arg counts locally), but the hint carries none of it, so a cross-script callable with optional params is compared more rigidly than a local one. A future trailing field (e.g. a default-count / vararg marker) would close the asymmetry.
2. **Enum and non-global script/class leaf fidelity (tracked: #446).** Like the existing container hints, an `ENUM`-typed leaf (encoded `Name.Member`) has no decoder branch and a non-global `preload`'d script/class leaf degrades to its native base name — so those nested slots decode to `Variant`/base across the boundary rather than round-tripping precisely. This is a fidelity limitation, not a soundness regression (mismatches on those slots are simply not caught cross-script; they are still caught within a single script). A future resolver shared with the `Array`/`Dictionary` hint paths would add an `ENUM` branch; a path-keyed resolver could preserve exact local-script identity, though embedding resource paths in hint strings is fragile.
3. **Shared core decoder for binding parity.** The decoder lives in the GDScript module. When C#/GDExtension adopt the format (epic #144), lifting it into core avoids two implementations drifting.
4. **Format versioning.** A reserved sentinel/marker would let the grammar gain fields (such as #1) later without ambiguity against the v1 form — cheap insurance for an engine-wide serialization surface.

## Acceptance

- Both repros in #412 are caught (value-boundary and nested-callable cases).
- A typed `Callable`/`Signal` exported from one script and read in another preserves its full (including nested) signature for compatibility checks.
- `.tres` round-trip and Inspector rendering are unaffected for existing untyped callables; the new hint degrades gracefully where unsupported.
- Resolves #412.
