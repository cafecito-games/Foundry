# Foundry Script `Type[T]` Metatype Design

Date: 2026-06-27
Status: Approved brainstorm - spec PR
Fork: CafecitoGames / Godot Engine

## Summary

Add `Type[T]` as the user-facing static type for Foundry Script class handles. A value of
type `Type[T]` represents a runtime type object whose produced instances are
assignable to `T`.

```gdscript
trait Creatable:
	static func create() -> Self

func factory[T: Creatable](factory_type: Type[T]) -> T:
	return factory_type.create()

var user := factory(User)
```

Bare class names remain the value syntax. `User` is the class handle; `Type[User]`
is the annotation that describes that handle. This design intentionally does not
add `User.self` in v1.

Runtime values stay compatible with the existing engine model. A `Type[T]` value is
the existing class-handle value: a `Foundry Script`/`Script` object for script classes, a
native class handle for native classes, and the existing specialized generic class
handle shape for `Box[int]`. The new feature gives those values a precise static
type and a documented reflection contract.

## Goals

- Let functions and variables explicitly accept class handles with `Type[T]`.
- Infer generic method type parameters from class-handle arguments.
- Type-check assignment to `Type[T]` by comparing the represented instance type to
  `T`, including trait bounds.
- Support static member access through `Type[T]`, using the represented type or
  `T`'s bound.
- Preserve reified generic specialization for `Type[Box[int]] = Box[int]` and
  construction through aliased/stored specialized handles.
- Keep reflection explicit through `godot.reflection` rather than adding a large
  method surface to every type object.
- Reuse the existing `DataType::is_meta_type` and specialized-handle machinery as
  much as possible.

## Non-Goals

- No `User.self` type-literal syntax in v1.
- No new runtime wrapper object or new object identity for type handles.
- No direct `.get_methods()`, `.annotations`, or `.name` API on `Type[T]`.
- No builtin scalar metatypes such as `Type[int]` in v1.
- No inspector/export picker UI in the initial feature unless it falls out from
  existing `Script` export behavior.
- No C# or GDExtension language-surface changes.

## Surface Syntax

`Type[T]` is a special annotation form, analogous to `Array[T]` and
`Dictionary[K, V]`, but its single argument is the represented instance type.

```gdscript
var exact: Type[User] = User
var base: Type[Node] = Button
var traited: Type[Creatable] = User
```

Assignment is valid when the right-hand side is a class/type handle and instances
of that handle are assignable to the represented type.

```gdscript
var ok_exact: Type[User] = User
var ok_base: Type[Node] = Button
var ok_trait: Type[Creatable] = User # if User uses/implements Creatable

var bad_instance: Type[User] = User.new() # error: instance, not type handle
var bad_class: Type[User] = Node          # error: Node instances are not User
```

Generic functions bind type parameters from the represented instance type of a
class-handle argument.

```gdscript
func factory[T: Creatable](factory_type: Type[T]) -> T:
	return factory_type.create()

var user := factory(User) # infers T = User
```

Reflection remains explicit:

```gdscript
godot.reflection.get_methods(User)
godot.reflection.get_methods(user_type)
godot.reflection.implements_trait(user_type, Creatable)
```

## Static Type Model

`Type[T]` is a metatype expectation. The stored value is a class handle; the static
type records the instance type represented by that handle.

The implementation should first try to express this with the existing
`GDScriptParser::DataType::is_meta_type` machinery. If that flag cannot
unambiguously represent "this annotation expects a class handle whose instance type
is `T`", add the narrowest possible explicit representation for `Type[T]`.

Conceptually:

```text
Type[T] value type = metatype(T)
represented_instance_type(metatype(T)) = T
```

The analyzer checks compatibility by reducing both layers:

```text
source is assignable to Type[T]
when source is a metatype and represented_instance_type(source) is assignable to T
```

Examples:

```gdscript
Button     -> Type[Node]       # OK: Button instances are Nodes
User       -> Type[Creatable]  # OK if User uses Creatable
Node       -> Type[User]       # error
User.new() -> Type[User]       # error: not a metatype
```

Trait targets use the same conformance machinery already used for trait bounds and
`godot.reflection.implements_trait`.

## Generic Inference

When a parameter type contains `Type[T]`, inference binds `T` from the argument's
represented instance type, not from the runtime class-handle object type.

```gdscript
func id_type[T](t: Type[T]) -> Type[T]:
	return t

var t := id_type(User) # T = User, result Type[User]
```

Bounds apply after inference:

```gdscript
func make[T: Creatable](t: Type[T]) -> T:
	return t.create()

make(User) # OK only if User satisfies Creatable
make(Node) # error if Node does not satisfy Creatable
```

Method type parameters are not reified by this feature. `Type[T]` values can still
be runtime values passed into ordinary calls; the type parameter binding itself
remains a static generic-method inference result unless the enclosing generic class
already reifies it.

## Static Member Access

Member lookup on a `Type[T]` base resolves static members of the represented type,
or static members guaranteed by `T`'s bound.

```gdscript
trait Creatable:
	static func create() -> Self

func factory[T: Creatable](factory_type: Type[T]) -> T:
	return factory_type.create()
```

Inside `factory`, `factory_type.create()` is valid because `T: Creatable` promises
a static `create` contract. The return type is substituted to `T`.

Non-static members remain invalid on a type handle:

```gdscript
func bad(t: Type[User]) -> void:
	t.name = "Ada" # error: non-static member on type handle
```

Open implementation detail: existing trait support must represent static trait
requirements clearly enough for member lookup to distinguish static and instance
contracts. If static trait methods are not fully modeled yet, the first
implementation slice should add `Type[T]` assignment/inference and then layer
static trait member calls as a follow-up.

## Runtime Model

There is no new runtime wrapper in v1. Runtime values remain:

- `Foundry Script`/`Script` objects for script classes and traits.
- Existing native class-handle objects for native classes.
- Existing specialized generic class-handle metadata for forms such as `Box[int]`.

This keeps identity and existing behavior stable:

```gdscript
var t: Type[User] = User
print(t == User) # true under existing class-handle equality
```

Calling `new()` through a stored specialized handle must preserve reified type
arguments:

```gdscript
class Box[T]:
	var value: T

var box_type: Type[Box[int]] = Box[int]
var box := box_type.new() # Box[int], with int reified on the instance
```

This should reuse the existing code paths for direct `Box[int].new()` and aliased
specialized handles.

## Reflection Contract

`Type[T]` participates in runtime reflection by documenting that a typed class
handle is a valid reflection target.

```gdscript
var t: Type[User] = User

godot.reflection.get_methods(t)
godot.reflection.get_class_annotations(t)
godot.reflection.implements_trait(t, Creatable)
```

Reflection remains under `godot.reflection`; `Type[T]` does not grow reflection
methods directly. Existing reflection APIs that already accept `Script` or class
handles should accept `Type[T]` naturally. Any API that currently accepts instances
or scripts should document the `Type[T]` path explicitly.

## Nullability And Raw Generics

Nullable type handles follow existing strict-null rules:

```gdscript
var maybe_type: Type[User]? = null
var required_type: Type[User] = null # rejected with strict null checks
```

Raw generic handles mirror raw generic instance typing:

```gdscript
var raw_box: Type[Box] = Box
var int_box: Type[Box[int]] = Box[int]
```

`Type[Box]` accepts the raw generic class handle. `Type[Box[int]]` requires the
specialized class handle and preserves `int` for construction and reflection.

## Parser And Analyzer Integration

`Type` should parse through the existing `TypeNode` collection-type path, then be
recognized by the analyzer as a special single-argument metatype annotation.

Validation:

- `Type` requires exactly one type argument.
- The argument must resolve to an object/script/class/trait type in v1.
- Builtin scalar arguments such as `Type[int]` are rejected in v1 with a clear
  "builtin metatypes are not supported yet" diagnostic.
- `Type` without an argument is invalid as an annotation.

Expression-position class handles already resolve as metatypes. Assignment,
argument passing, and generic inference should use that existing metatype
information rather than evaluating a new literal form.

The compiler should continue emitting the underlying class-handle value. Runtime
validation is only needed when the source is dynamic (`Variant`) or when strict
dynamic checks require a defensive runtime guard.

## Error Messages

Diagnostics should name both the handle layer and the represented instance layer:

```text
Cannot assign "Node" to "Type[User]": instances of "Node" are not assignable to "User".
Cannot assign "User.new()" to "Type[User]": expected a type handle, got an instance of "User".
Cannot call non-static member "name" on "Type[User]".
Type[T] expects exactly one type argument.
Builtin metatypes such as "Type[int]" are not supported yet.
```

## Implementation Breakdown

This feature is large enough to track as an epic with native subissues:

1. **Parser and type representation**
   - Recognize `Type[T]` as a special annotation form.
   - Add any narrow `DataType` metadata needed beyond `is_meta_type`.
   - Reject invalid arity and unsupported builtin metatype arguments.

2. **Compatibility and assignment checking**
   - Check class-handle assignment to `Type[T]`.
   - Support native/script class inheritance and trait conformance.
   - Add strict dynamic/null behavior and focused diagnostics.

3. **Generic inference through `Type[T]`**
   - Bind method type parameters from represented instance types.
   - Enforce bounds after inference.
   - Preserve `Type[T]` in return types.

4. **Static member lookup and calls on `Type[T]`**
   - Resolve static members through represented concrete types.
   - Resolve static members promised by trait/class bounds.
   - Substitute `Self`/`T` return types for factory patterns.

5. **Runtime/codegen preservation for specialized handles**
   - Ensure stored `Type[Box[int]]` values construct `Box[int]` instances.
   - Keep existing class-handle identity and runtime value shape.
   - Add dynamic runtime validation where required.

6. **Reflection documentation and API polish**
   - Document `Type[T]` as a valid reflection target.
   - Adjust reflection signatures/docs where needed.
   - Keep the method surface under `godot.reflection`.

7. **Editor, LSP, and refactoring**
   - Completion for `Type[` type arguments.
   - Hover/signature rendering for `Type[T]`.
   - Type annotation insertion/rendering for class handles.

8. **Tests and fixtures**
   - Parser/analyzer success and error fixtures.
   - Generic factory inference fixtures.
   - Trait-bound factory fixtures.
   - Runtime specialized-handle construction fixtures.
   - Reflection target fixtures.

## Testing Strategy

Use Foundry Script fixture tests for the language surface and analyzer behavior:

- `Type[User] = User` success.
- `Type[Base] = Derived` success.
- `Type[Trait] = Implementer` success.
- Instance assigned to `Type[T]` error.
- Incompatible class handle assigned to `Type[T]` error.
- Invalid `Type` arity and unsupported builtin metatype errors.
- `factory[T: Creatable](Type[T]) -> T` inference and static call behavior.
- `Type[Box[int]] = Box[int]` followed by `.new()` preserving reified arguments.

Add runtime tests where codegen or reified generic construction is involved. Add
LSP/refactor fixtures for completion, hover, and rendered type annotations once the
core analyzer behavior is in place.

Run the full headless suite before merging implementation work:

```sh
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors
```

For this spec-only PR, `git diff --check` is sufficient validation.

## Risks And Open Questions

- Existing `is_meta_type` may be too overloaded to represent `Type[T]` cleanly.
  If so, add a narrow explicit representation rather than spreading more boolean
  special cases through the analyzer.
- Static trait methods must be modeled precisely for the factory pattern to be
  sound. If current trait static-method support is incomplete, split that into its
  own subissue.
- Dynamic `Variant` values assigned to `Type[T]` need a runtime guard if strict
  dynamic checks require it. The guard should validate that the value is a class
  handle and that its represented instance type satisfies `T`.
- `Type[int]` is intentionally deferred. Supporting builtin metatypes later should
  be a separate design because constructor/static-method semantics differ from
  object class handles.
- Exported `Type[T]` inspector UI is valuable but not required for the first
  language slice.
