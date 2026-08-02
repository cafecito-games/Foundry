# Nested `Type[T]`: core class-handle seam design

Design for issue #1524, "Allow Type[T] in nested type positions and preserve class-handle semantics".

## Problem

`Type[T]` is legal at the top level of a slot but rejected everywhere a type may nest. The two
rejection sites are deliberate:

- `modules/foundry_script/fs_analyzer.cpp:1727` — Array/Dictionary element slots and Callable/Signal
  signatures.
- `modules/foundry_script/fs_analyzer_surface.cpp:1113` — generic class type arguments.

Both emit `Type[T] cannot be used as a nested type argument yet.`

The rejection is deliberate because the class-handle distinction cannot survive the trip into core.
The parser/analyzer model (`FSParser::DataType::is_type_handle_annotation`), the runtime model
(`FSDataType::is_type_handle`), and the compiled-bytecode encoding all carry the distinction. Core's
`ContainerType` — the descriptor typed Arrays and Dictionaries actually store — does not. So
`FSDataType::to_container_type()` (`modules/foundry_script/fs_function.h:297`) drops the flag, and
`_gdtype_from_container_type()` (`modules/foundry_script/fs_function.cpp:72`) reconstructs every
nested child as a value/instance type by passing `false` at lines 96 and 99.

Removing the diagnostics without closing that gap would accept source that is either unsound at
runtime or silently downgraded to an instance-typed container.

## Constraint

Core must not gain a dependency on the `foundry_script` module. Everything below is shaped by that.

## Key observation

`FSDataType::is_type_handle_type()` (`modules/foundry_script/fs_function.cpp:154`) is the single
authoritative compatibility rule for top-level `Type[T]` today. Two facts make it relocatable:

1. `FSDataType::from_type_handle_container_type(const ContainerType &)`
   (`modules/foundry_script/fs_function.cpp:245`) already exists. A `ContainerType` plus one boolean
   is provably sufficient input to reconstruct the expected type and run the check. No language-
   specific information beyond the descriptor needs to reach the rule.
2. Every branch the rule takes is expressible with primitives core already calls from
   `ContainerTypeValidate::_internal_validate_object()`:

   | Rule branch | Core primitive | Existing core call site |
   |---|---|---|
   | native handle vs native class | `ClassDB::is_parent_class` | `core/variant/container_type_validate.cpp:222` |
   | script handle vs script base | `Script::inherits_script` | `core/variant/container_type_validate.cpp:241` |
   | handle vs trait target | `Script::has_script_trait` | `core/variant/container_type_validate.cpp:234` |
   | specialization invariance | `Script::project_type_arguments_onto_base` + `_projected_type_arguments_conflict` | `core/variant/container_type_validate.cpp:264` |
   | freed / non-object / non-handle | `Object::get_validated_object_with_check` | `core/variant/container_type_validate.cpp:200` |

The only thing core cannot do today is recognize the two module-side objects that denote a class:
`FSNativeClass` and `FSSpecializedClassHandle` (both `RefCounted`,
`modules/foundry_script/foundry_script.h:52` and `:93`). A bare `Script *` is already core-visible.

## Design

### 1. `ContainerType::is_type_handle`

`ContainerType` (`core/variant/container_type_validate.h:37`) gains:

```cpp
bool is_type_handle = false;
```

`ContainerTypeValidate` mirrors the field. It participates in:

- `ContainerType::operator==` / `operator!=`
- `ContainerTypeValidate::operator==` / `operator!=` and `can_reference()`
- `ContainerType::get_type_name()` and `ContainerTypeValidate::get_type_name()`, rendering `Type[T]`
  at every nesting depth (`Dictionary[String, Type[Factory]]`)
- the `ContainerTypeValidate(const ContainerType &)` constructor and
  `ContainerTypeValidate::get_container_type()`
- `ContainerTypeValidate::make_default()`

Default `false` means every existing construction site, every non-Foundry-Script language, and every
engine-internal typed container keeps today's exact meaning with no edits. This is default-value
semantics, not a compatibility shim: the fork is unshipped, so no versioning or extension marker is
warranted on any transport.

**Legality.** `is_type_handle` is valid only when `builtin_type == Variant::OBJECT`. This is enforced
at descriptor decode (`ContainerTypeDescriptor::from_variant`) and on every transport decode, not
only in the analyzer. `Array[Type[int]]` is therefore rejected at the representation layer, which
makes the `#511` "no builtin scalar handles" non-goal structurally enforced rather than aspirational.

### 2. `ClassHandle`

A new core abstraction for "a value that denotes a class rather than instances of one":

```cpp
// core/object/class_handle.h
class ClassHandle : public RefCounted {
	GDCLASS(ClassHandle, RefCounted);

public:
	virtual StringName get_represented_native_class() const { return StringName(); }
	virtual Ref<Script> get_represented_script() const { return Ref<Script>(); }
	virtual void get_represented_type_arguments(Vector<ContainerType> &r_arguments) const {}
};
```

Module changes:

- `FSNativeClass` changes base from `RefCounted` to `ClassHandle`, implementing
  `get_represented_native_class()` from its existing `name` field.
- `FSSpecializedClassHandle` changes base from `RefCounted` to `ClassHandle`, implementing
  `get_represented_script()` and `get_represented_type_arguments()` from its existing `script` and
  `type_arguments` fields. It already stores `Vector<ContainerType>`, so no conversion is needed.
- A bare `Script *` value is treated by core as a handle representing itself. It does not inherit
  `ClassHandle`; core recognizes it directly, as it already does throughout
  `_internal_validate_object()`.

Core learns nothing language-specific from this. It learns that some objects denote a class — the
same category of engine-level knowledge that `Script` itself already represents in core.

### 3. One validation rule, owned by core

`ContainerTypeValidate` gains `_internal_validate_class_handle()`, reached from
`_internal_validate()` whenever `is_type_handle` is set. It implements the table in "Key
observation" above, resolving the value through `ClassHandle` or a bare `Script *`.

`FSDataType::is_type_handle_type()` becomes a thin delegation into that core routine via
`to_container_type()`. There is exactly one algorithm, and it is the one top-level `Type[T]` already
uses, so the parity requirement in #1524 is structural rather than a test obligation.

Two behaviors are preserved exactly as they are today, not redesigned:

- **A native class handle never satisfies a trait-typed `Type[Factory]`.**
  `modules/foundry_script/fs_function.cpp:172` returns only when `kind == NATIVE`. Consequently core
  needs no access to `FSConformanceRegistry`; retroactive conformance reaches core entirely through
  the `Script::has_script_trait()` virtual, which `FoundryScript` already overrides.
- **The runtime predicate accepts NIL.** `modules/foundry_script/fs_function.cpp:156` returns `true`
  for NIL. This is not a nullability decision specific to `Type[T]`: the `NATIVE` case at
  `modules/foundry_script/fs_function.h:157` does the same, and core's `_internal_validate_object()`
  returns `true` for a null object id (`core/variant/container_type_validate.cpp:201`). The
  convention is that runtime type predicates are permissive about null for all object-shaped types,
  with static rejection in a non-nullable slot left to the analyzer. Nothing about static
  nullability changes; `Type[T]` alone still does not accept null in analyzed source, and `Type[T]?`
  is still required to allow it.

### 4. Module-side propagation

Two edits close the `FSDataType -> ContainerType -> FSDataType` round-trip at every recursive node:

- `FSDataType::to_container_type()` (`modules/foundry_script/fs_function.h:297`) writes
  `is_type_handle` instead of dropping it.
- `_gdtype_from_container_type()` (`modules/foundry_script/fs_function.cpp:72`) reads each child's
  flag instead of passing `false` at lines 96 and 99.

No new conversion path is introduced.

### 5. Transport

The flag is carried recursively through:

| Surface | Site |
|---|---|
| Descriptor Variant | `ContainerTypeDescriptor::to_variant` / `from_variant`, `core/variant/container_type_validate.cpp:433-569` |
| Text Variant | `_write_container_type` and the parse path, `core/variant/variant_parser.cpp:744-780`, `:2066-2074` |
| Binary Variant | `_encode_container_type` / `_decode_container_type[_extended]`, `core/io/marshalls.cpp:130-230`, `:1417-1467` |
| Native JSON | `_encode_container_type_value` / `_decode_container_type_value`, `core/io/json.cpp:753-766`, `:1202-1208` |
| Resource binary | `_find_resources_in_container_type`, `core/io/resource_format_binary.cpp:2047` |
| GDExtension | `core/extension/foundry_extension_interface.cpp:1255-1318` |
| Foundry Script bytecode | already encodes `is_type_handle`; nested descriptors need verifying, not building |

Text spelling reuses the language's own form rather than inventing a descriptor-only name:

```
Array[Type[Node]]([Node, Button])
Dictionary[String, Type[Factory]]({...})
```

An omitted flag decodes as `false` on every surface.

### 6. Diagnostics

The two `push_error` sites are removed last, after every supported nested position is sound. The
fixture `modules/foundry_script/tests/scripts/analyzer/errors/type_metatype_nested_container.out` is
replaced by positive fixtures.

Runtime rejection distinguishes four cases, which falls out of the core rule's branch structure:

1. value is not a class handle;
2. handle's represented instance type is incompatible;
3. handle is invalid or freed;
4. specialized generic argument mismatch.

Because the handle path never reaches `_internal_validate_object()`, the misleading "does not
inherit" phrasing — which describes the handle object rather than the represented instance type — is
structurally impossible on this path.

## Rejected alternatives

**Registered validator hook.** Core holds an abstract `ClassHandleValidator *` installed by the
module at init, `ScriptServer`-style, and `ContainerTypeValidate` calls through it. Keeps core dumb
and leaves the algorithm in the module, but behavior depends on registration order, it is hard to
unit-test in core alone, it has no defined answer when unregistered (headless, tool, and
other-language contexts), and multiple languages would contend for the single slot.

**`ScriptLanguage` virtual.** Add a virtual to core's `ScriptLanguage` and have
`ContainerTypeValidate` loop over registered languages asking each whether the value is a valid
handle for the descriptor. No new object hierarchy, but the loop has no principled way to pick a
language, cross-language answers can disagree, and it drags per-validation iteration into a hot
path.

**Overloading an existing field.** Explicitly rejected by #1524 and by this design: no encoding via
`class_name`, no reserved fake script class, and no treating all Object-typed container values as
handles. Instance and handle descriptors must remain independently representable.

## Pre-existing gap discovered: `type_arguments` transport

`ContainerType::type_arguments` — the field that distinguishes `Box[int]` from `Box[String]` —
appears in core in only four files:

```
core/object/script_instance.h
core/object/script_language.h
core/variant/container_type_validate.h
core/variant/container_type_validate.cpp
```

It is absent from `core/io/marshalls.cpp`, `core/variant/variant_parser.cpp`, `core/io/json.cpp`,
and `core/io/resource_format_binary.cpp`. Generic specialization therefore already does not survive
binary Variant, text Variant, native JSON, or resource save/load — with or without `Type[T]`.

This collides with two of #1524's acceptance criteria (`Array[Type[Box[int]]]` distinguishing
`Box[int]` from `Box[String]`, and "resource text and binary save/load preserve … specializations").
Neither can be met by transporting the handle flag; both require building `type_arguments` transport
that has never existed.

It is filed as an independent prerequisite issue rather than folded into this epic: it is a
pre-existing bug with no `Type[T]` involvement, it is independently testable, and folding it in would
roughly double the size of the transport work while making review harder to scope.

## Work breakdown

Prerequisite, filed outside the epic:

- **P0** — `ContainerType::type_arguments` must round-trip through Variant text/binary, native JSON,
  resource save/load, and GDExtension. Blocks S6's specialization coverage.

Epic children:

| # | Sub-issue | Depends on |
|---|---|---|
| S1 | Core: `ClassHandle`, `is_type_handle` on `ContainerType`/`ContainerTypeValidate`, and the single validation rule; module handles rebase onto `ClassHandle`; `FSDataType::is_type_handle_type()` delegates | — |
| S2 | Module: flag preserved at every recursive node of `FSDataType <-> ContainerType` | S1 |
| S3 | Analyzer: nested `Type[T]` in Array/Dictionary slots — literal inference, reads/iteration/destructuring retain `Type[T]`, diagnostics; removes `fs_analyzer.cpp:1727` | S2 |
| S4 | Analyzer: generic class arguments — `Slot[Type[Factory]]` distinct from `Slot[Factory]` through construction, aliasing, inheritance projection, dynamic member validation; removes `fs_analyzer_surface.cpp:1113` | S2 |
| S5 | Callable and Signal signatures carrying `Type[T]`, including method references, MethodInfo / Foundry Script signature transport, and connect/emit validation | S2 |
| S6 | Transport of the flag: descriptor Variant, text Variant, binary Variant, native JSON, resource, GDExtension | S1, P0 |
| S7 | Tooling: formatter, completion inside nested `Type[`, hover and signature rendering, semantic tokens, rename of T inside nested handles | S3, S4, S5 |
| S8 | End-to-end `.fsb` verification with parser/analyzer state cleared, across members, arguments, returns, nested generic bindings, and Callable/Signal signatures | S3, S4, S5, S6 |

S1 touches core's object model and lands with C++ doctests alone, with no language-surface change, so
it is reviewable in isolation. S3, S4, and S5 proceed in parallel once S2 lands. The motivating
trait-registry fixture from the issue body belongs to S3.

## Verification

```sh
python3 scripts/agent_build.py
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --progress-format=jsonl \
  --progress-file .test_scratch/type-handle-container-progress.jsonl \
  --force-colors
```

On Linux, prefix with `DISPLAY=:1` so GUI-dependent subprocess tests do not silently skip.
