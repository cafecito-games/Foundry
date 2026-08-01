# Custom JSON Marshalling — Design

Status: approved design, epic pending implementation.
Scope: a user-implementable trait that lets a Foundry Script class control how it is encoded to and
decoded from JSON, plus the one language prerequisite it depends on.

## 1. Motivation

`JSON::_stringify` (`core/io/json.cpp:57`) has no `Variant::OBJECT` case. An object passed to
`JSON.stringify()` falls into the `default:` branch and is emitted as a quoted `to_string`, e.g.
`"<RefCounted#123>"`. There is no way for a class to say how it should appear in JSON, so every
project hand-writes a `to_dictionary()` convention and calls it manually at every call site —
which breaks down as soon as an object is nested inside a Dictionary, an Array, or another object.

Decoding is worse: `JSON.parse()` yields untyped Dictionaries that each caller re-validates by hand,
with no shared vocabulary for reporting which field of a save file was malformed.

This design adds a trait-based marshalling contract so a class declares its JSON representation once,
and `JSON.stringify()` honors it automatically at any nesting depth.

`JSON.from_native`/`to_native` are unaffected. They remain the full-fidelity reflection-based
serializer for engine round-tripping, which is a different concern from user-defined representation.

## 2. Locked decisions

1. **Scope is encode + decode**, both routed through a single trait.
2. **The trait and its supporting types are built in**, declared in `.fs` source that is compiled
   into the `foundry_script` module and served under a reserved `foundry://builtin/` path scheme.
   They resolve with no import and no file the user can edit or delete. See §3.2 — this replaces an
   earlier "no source at all" position, which is not implementable: global-name resolution requires
   parseable source behind a path.
3. **Non-conforming objects keep today's behavior** — the quoted `to_string`. The feature is purely
   additive; no existing output changes.
4. **`to_json` returns a `JsonNode` tagged union, not a `Variant`**, so the analyzer rejects a
   witness that returns a `Node`, a `Callable`, or any other non-JSON value.
5. **`JsonNode` separates `Int` and `Float`.** Foundry's encoder already formats ints with `itos` and
   floats with precision handling; a single `Number(float)` case would turn `hp: int = 123` into
   `123.0`.
6. **The trait is non-generic and uses `Self`.** Verified: `abstract static func from_json(...) ->
   JsonResult[Self]` in a trait requirement is accepted, and a witness returning another class's
   specialization is rejected with a signature mismatch. Callers write plain `uses JsonSerializable`.
7. **Decode returns `JsonResult[Self]`, not a bare nullable.** Value and structured error travel
   together so the error cannot be silently dropped. Generic *enums* do not exist in the language, so
   this is a generic *class*, checked via `result.error` rather than an exhaustive `match`.
8. **Dispatch goes through a marshaller seam in core, implemented in the module.** `core/io` never
   learns a Foundry Script trait name or a `JsonNode` tag ordinal.
9. **Classes only. Enums, tuples, and builtin value types are out of scope.** Their runtime values
   carry no type identity, so `JSON.stringify` has nothing to dispatch on. See §8.
10. **Self-recursive tagged unions are a prerequisite**, landed as a separate issue before any of
    this work. See §3.1.
11. **`GRAMMAR.md` is updated in the same change as any syntax-affecting PR**, per the repo's
    normative-spec rule.

## 3. Prerequisites

Two pieces of language/module infrastructure must land before any marshalling work.

## 3.1 Self-recursive tagged unions

`JsonNode` needs `Array(items: Array[JsonNode])` and `Object(entries: Dictionary[String, JsonNode])`.
The current analyzer rejects this. Measured against `bin/foundry.macos.editor.dev.arm64` (build
`e6971fb8a`):

| Case | Result |
| --- | --- |
| Inner `enum` — `Nested(child: JsonNode)` | 2 errors — `Could not find type "JsonNode"` |
| Inner `enum` — `Arr(items: Array[JsonNode])` | 2 errors — same |
| Inner `enum` — `Obj(entries: Dictionary[String, JsonNode])` | 2 errors — same |
| `enum_name` whole-file, all three forms, linted alone | 0 errors |
| `enum_name` + consumer, after `project import` | 3 errors — `Could not resolve global enum "JsonNode": Cyclic reference.` |
| Control — non-recursive tagged union | 0 errors |
| Control — class self-reference (`var next: LinkedNode`, `Array[LinkedNode]`) | 0 errors |

Three conclusions:

- The restriction covers **indirect** recursion through typed collections, not just direct payload
  recursion. Neither shape is a workaround for the other.
- The clean lint on a standalone `enum_name` file is a **false negative**. The cyclic-reference error
  surfaces only once a consumer forces global resolution.
- The limitation is **enum-specific**. Classes self-reference correctly, directly and through typed
  collections, so this is not a general type-resolution limit.

**The runtime representation is not the obstacle.** Tagged-union payloads are already
`[tag, payload...]` read-only Arrays, so a recursive value is just another array in a payload slot —
zero representational cost, and none of the epic #1253 decisions are reopened. The work is confined
to analyzer name resolution: the enum's own identity must be visible as *in progress* rather than
*cyclic* while its payload field types resolve, which is how class members already behave.

The prerequisite issue delivers:

1. Direct payload recursion (`Nested(child: JsonNode)`).
2. Indirect recursion through `Array[T]` and `Dictionary[K, V]`.
3. Both the inner `enum` and whole-file `enum_name` forms.
4. The false-negative lint fix: a file that will fail global resolution must fail when linted alone.
5. `GRAMMAR.md` updated to state that self-recursive tagged unions are permitted.

Genuinely cyclic constructs that have no valid representation must still be rejected. Recursion
through a payload slot terminates at runtime because a value is finite; the analyzer's job is to stop
treating the type-level reference as an error.

## 3.2 Builtin source provider

A global Foundry Script type is resolved by looking up its path with
`ScriptServer::get_global_class_path` (`fs_analyzer.cpp:6892`) and parsing that path through the
dependency-parser machinery. `ScriptServer::add_global_class`
(`core/object/script_language.cpp:499`) requires a path. There is no embedded- or builtin-source
mechanism in the module today, so a built-in type must have parseable source behind some path.

The prerequisite adds the smallest mechanism that satisfies this:

1. A reserved path scheme, `foundry://builtin/<name>.fs`, that never collides with `res://` or
   `user://` and is not writable from a project.
2. A native registry mapping each builtin path to its `.fs` source text, compiled into the module as
   string data so it ships inside the binary — no export packing, no versioning, no loose files a
   user can edit or delete.
3. Source resolution for that scheme in the parser-ref/dependency layer, so an existing
   `raise_depended_parser_for(path, ...)` call transparently serves builtin source.
4. Registration of each builtin global name at module init through the normal
   `ScriptServer::add_global_class` path, with the correct `is_trait`/`is_enum` flags.

Everything downstream — analysis, codegen, completion, LSP, documentation — then works through the
existing machinery with no special cases. Alternatives considered and rejected: shipping loose `.fs`
files into the project (user-editable, needs export packing), implementing the types via ClassDB
(no generics, no tagged unions), and special-casing them as analyzer intrinsics like `Self`
(hand-built DataTypes plus repeated special-casing across analyzer, codegen, completion, and LSP).

Builtin sources are parsed with the same analyzer as user code, so an error in one is a module bug,
not a user error. A test asserts every registered builtin parses and analyzes cleanly.

## 4. User-facing surface

### 4.1 `JsonNode`

An engine-registered whole-file tagged union with a **fixed, natively-defined ordinal order**:

```
Null                                            # 0
Bool(value: bool)                               # 1
Int(value: int)                                 # 2
Float(value: float)                             # 3
Str(value: String)                              # 4
Array(items: Array[JsonNode])                   # 5
Object(entries: Dictionary[String, JsonNode])   # 6
```

Because the ordinals are defined in C++ rather than in user source, the native lowering in §5 has a
stable contract that cannot be broken by reordering a declaration. It still validates rather than
trusts, since stale bytecode could disagree.

Static helper functions (legal on tagged unions) reduce construction noise:

- `static func array_of(items: Array[JsonNode]) -> JsonNode` — sugar for the `Array` case.
- `static func object_of(entries: Dictionary[String, JsonNode]) -> JsonNode` — sugar for the `Object`
  case.
- `static func of(value: Variant) -> JsonNode` — a recursive escape hatch that wraps a plain Variant
  tree, mapping `null`/`bool`/`int`/`float`/`String`/`Array`/`Dictionary` onto the corresponding
  cases. This is the one place typing weakens: on an unsupported type it `push_error`s and yields
  `JsonNode.Null`. Opt-in convenience, not the primary path.

### 4.2 `JsonDecodeError`

A non-generic engine-registered class:

- `message: String` — what was wrong.
- `path: String` — where, as a JSON path such as `$.party[1].weapon.damage`.

### 4.3 `JsonResult[T]`

An engine-registered generic class:

- `value: T?`
- `error: JsonDecodeError?`
- `static func ok(value: T) -> JsonResult[T]`
- `static func fail(message: String, path: String) -> JsonResult[T]`
- `func is_ok() -> bool`
- `static func nested(error: JsonDecodeError, key: String) -> JsonResult[T]` — builds a failure of
  this specialization from a child's error, prepending `key` to the child's path. Lets a parent's
  `from_json` forward a nested failure without losing location or re-typing the result by hand.

Success is explicit state established by `ok(...)`; a successful nullable payload may have both
`value` and `error` null. A default-constructed object with both fields null is malformed and
`is_ok()` is false. When `error` is present it is authoritative and `is_ok()` is false even if the
result was previously successful.

### 4.4 `JsonSerializable`

```
trait_name JsonSerializable

abstract func to_json() -> JsonNode
abstract static func from_json(node: JsonNode) -> JsonResult[Self]
```

### 4.5 Usage

```
class_name Player extends RefCounted
uses JsonSerializable

var name: String
var level: int

func to_json() -> JsonNode:
    return JsonNode.object_of({
        "name": JsonNode.Str(name),
        "level": JsonNode.Int(level),
    })

static func from_json(node: JsonNode) -> JsonResult[Player]:
    match node:
        JsonNode.Object(var entries):
            # read and validate entries; return JsonResult[Player].fail(...) on any
            # missing or mistyped field, with the field name as the path
        _:
            return JsonResult[Player].fail("expected an object", "$")
```

Encoding: `JSON.stringify(player)` produces `{"level":3,"name":"Captain"}`, and works at any nesting
depth — `JSON.stringify({"party": [player_a, player_b]})` marshals both.

Decoding:

```
var parsed := JSON.parse_to_node(text)
if not parsed.is_ok():
    return
var result := Player.from_json(parsed.value)
if not result.is_ok():
    return
```

Nested conforming types compose by **explicit delegation** in both directions: a parent's `to_json`
calls `weapon.to_json()`, and its `from_json` calls `Weapon.from_json(child_node)` and forwards any
failure with `"weapon"` prepended to the path. The type system forces this rather than hiding it —
the `Object` payload accepts only `JsonNode` values.

## 5. Encode dispatch

### 5.1 Core seam

`core/io/json.h` gains a minimal abstract interface and a static registration point:

```cpp
class JSONObjectMarshaller {
public:
    // Returns true if this object was handled; r_result is then a plain Variant tree.
    virtual bool marshal_object(Object *p_object, Variant &r_result) = 0;
    virtual ~JSONObjectMarshaller() {}
};
// JSON::set_object_marshaller(JSONObjectMarshaller *p_marshaller);
```

`_stringify` gains a `Variant::OBJECT` case that:

1. Resolves `p_var.get_validated_object()`. A freed or null object writes `null`.
2. Falls through to today's quoted `to_string` when no marshaller is registered, or when
   `marshal_object` returns false. A build without the `foundry_script` module, and any
   non-conforming class, behave exactly as they do now.
3. Guards cycles with an `ObjectID` marker set held alongside `p_markers`. The existing markers key
   on `Array`/`Dictionary` ids and would miss `a.to_json()` returning something that reaches `a`.
4. On success, recurses `_stringify` over the returned Variant tree, so `indent`, `sort_keys`, and
   `full_precision` all keep applying to marshalled output.

### 5.2 Module handler

Registered at `foundry_script` module init, cleared at deinit. It:

1. Reads `obj->get_script()` and checks `has_script_trait("JsonSerializable")`.
2. Calls `to_json()` through `Object::call`, per the repo's script-extensible-native-API rule — never
   C++ virtual dispatch. The hook is documented as script-dispatched and added to
   `FSScriptExtensibleNativeHooks`, and all invocation goes through one native helper rather than
   scattered `call()` sites.
3. Lowers the returned `[tag, payload...]` read-only Array into plain Variants, validating tag
   ordinal range and payload arity at every level, and enforcing a depth limit mirroring
   `MAX_RECURSION_DEPTH`.
4. On a malformed return, `push_error`s naming the class and the offending node, and reports failure
   so the encoder writes `null` rather than truncated output.

Both directions of the `JsonNode` conversion — lowering for encode, lifting for decode — live in this
module-side handler.

## 6. Decode path

`JSON.parse_to_node(text: String) -> JsonResult[JsonNode]` is the only decode addition. It parses with
the existing tokenizer, then builds a `JsonNode` tree. Because it constructs Foundry Script enum
values, it goes through the **same marshaller seam** as encoding — the module supplies the inverse
"lift a plain Variant tree into a `JsonNode`" operation. Core keeps owning the parse; the module owns
the FS-type construction.

On a lexical or syntax error, it returns a failure carrying the existing parser's message and line.
The existing `get_error_message`/`get_error_line` surface is unchanged for current callers.

`JsonResult[T]` specialized on a tagged union is verified to work, so `JsonResult[JsonNode]` is valid.

## 7. Error handling

**Encode has no failure channel by construction** — `to_json()` returns a `JsonNode`, so a witness
cannot produce invalid JSON. The remaining failures are structural, handled natively, and none are
silent:

| Condition | Behavior |
| --- | --- |
| Freed or null object | Writes `null`, matching other dead references |
| No marshaller registered | Today's quoted `to_string`, unchanged |
| Script does not conform | Today's quoted `to_string`, unchanged |
| Malformed `JsonNode` array (bad ordinal or arity) | `push_error` naming class and node; writes `null` |
| Cycle through `to_json` | `ObjectID` marker set; existing circular-structure error |
| Excessive depth | Module-side limit mirroring `MAX_RECURSION_DEPTH` |

**Decode carries errors in the value.** `parse_to_node` reports lexical and syntax failures;
`from_json` reports shape failures with a `path`. A parent propagates a child's failure with its own
key prepended, so a deeply nested mismatch names its exact location. A result with neither a value
nor an error is treated as a malformed implementation and reported.

**Threading.** Encoding calls into the script VM from a static native method, so `JSON.stringify` on a
conforming object inherits the thread-affinity rules of script execution. Stated explicitly rather
than left implied.

## 8. Out of scope

- **Enums, tuples, and builtin value types.** Tagged-union and tuple values are read-only Arrays and
  plain enums are ints; none carry runtime type identity, so `JSON.stringify` sees a bare `Array` or
  `int` with nothing to dispatch on. Supporting them requires either giving those values runtime type
  identity — reopening epic #1253 — or a second, statically-typed encode entry point. Both are
  deliberately deferred.
- **Retroactive conformance on scriptless native classes.** Dispatch keys off `obj->get_script()`, so
  `extend Node2D uses JsonSerializable` is not picked up: the instance carries no script to query.
  Conformance on *scripted* classes works normally. Consulting the module's conformance registry
  directly for scriptless targets is a clean follow-up, but it is a different lookup mechanism and is
  not folded into the first change.
- **Automatic marshalling of non-conforming classes.** Reflecting declared properties into JSON by
  default was considered and rejected: cycles, engine subobjects, and `Node` references make it hard
  to keep predictable.
- **`@export` of `JsonNode`-typed properties**, which inherits the existing tagged-union export
  restriction.

## 9. Testing

Per the repo's test-authoring rules, every test executes code and asserts on results. No test asserts
on the source text of another file, on documentation prose, or on the existence of another test.

**Prerequisite — recursive tagged unions (§3.1)**

- Analyzer fixtures for direct recursion, indirect recursion through `Array[T]`, and indirect
  recursion through `Dictionary[K, V]`, in both `enum` and `enum_name` forms.
- A runtime fixture constructing a recursive tree and `match`-destructuring it back.
- An error fixture pinning the standalone-lint regression, so a file that will fail global resolution
  fails when linted alone.
- A negative fixture proving genuinely unrepresentable cycles are still rejected.

**Prerequisite — builtin source provider (§3.2)**

- A C++ doctest asserting a registered builtin path resolves to its source and parses.
- A test asserting every registered builtin analyzes with zero errors, so a malformed builtin fails
  the suite rather than surfacing as a confusing user-facing error.
- A test asserting a project file cannot shadow or write a `foundry://builtin/` path.

**Encode**

C++ doctests covering the marshaller seam:

- No marshaller registered reproduces today's exact output.
- Non-conforming object unchanged.
- Conforming object.
- Conforming object nested inside a Dictionary and inside an Array.
- `indent`, `sort_keys`, and `full_precision` applied to marshalled output.
- Freed object.
- Object cycle through `to_json`.
- Malformed `JsonNode` array (bad ordinal, wrong arity).

**Decode**

`.fs` fixtures under `modules/foundry_script/tests/scripts/`:

- Successful decode.
- Shape mismatch producing the expected `path`.
- Nested failure propagating its path through a parent.
- Text parse error surfaced through `parse_to_node`.

**Round-trip**

A fixture asserting that a value encoded with `JSON.stringify`, parsed with `parse_to_node`, and
decoded with `from_json` equals the original.

Tests that generate or persist files write under `FOUNDRY_TEST_SCRATCH`; no fixture writes into
tracked directories.

## 10. Implementation order

1. Self-recursive tagged unions, plus the false-negative lint fix (§3.1). Blocks everything else.
2. The builtin source provider (§3.2). Independent of step 1; both must land before step 3.
3. `JsonNode`, `JsonDecodeError`, `JsonResult[T]`, and `JsonSerializable` declared as builtin
   sources, with `JsonNode`'s static helpers.
4. The `JSONObjectMarshaller` seam in `core/io/json.cpp`, with the `Variant::OBJECT` case and the
   `ObjectID` cycle guard. Landable and testable before any module handler exists, since the
   no-marshaller path is the current behavior, and independent of steps 1-3.
5. The module-side encode handler: trait check, `Object::call` dispatch, `JsonNode` lowering.
6. `JSON.parse_to_node` and the module-side lifting.
7. Round-trip fixtures and documentation.
