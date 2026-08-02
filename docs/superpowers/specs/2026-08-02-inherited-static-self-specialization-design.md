# Inherited Static `Self` Specialization Design

## Issue metadata

- **Issue:** [#1482](https://github.com/cafecito-games/Foundry/issues/1482)
- **Recommended title:** Inherited static `Self`: receiver specialization across methods and conformance witnesses
- **Related issue:** [#1468](https://github.com/cafecito-games/Foundry/issues/1468), retroactive conformance trait
  membership over supertraits
- **Relationship:** Keep the issues separate. #1468 defines whether a trait requirement and witness are reachable;
  #1482 defines which exact type `Self` denotes once an inherited implementation is invoked. #1482 depends on #1468
  for the cross-product acceptance case described below.

## Summary

Every occurrence of `Self` in an inherited static implementation must specialize to the exact class handle used for
the call, even when the selected implementation was declared on an ancestor or as a retroactive conformance witness.
The rule must hold at analysis time and at runtime, including reified generic types, constructed values, extracted
callables, asynchronous calls, and loaded bytecode.

Today the analyzer generally substitutes inherited static `Self` against the call receiver, but runtime conformance
witness compilation and dispatch anchor reified `Self` to the conformance target or declaration context. Ordinary
inherited static methods have the same underlying runtime problem. This can make a call type-check as
`Crate[ImageTexture]` while the runtime value behaves as `Crate[Resource]` or another ancestor specialization.

The fix is an immutable **static `Self` context** carried by each static call frame. It is separate from instance
`self` and from the conformance target used to validate a witness declaration. Static dispatch chooses an
implementation as it does today, then invokes that implementation with the original receiver's exact runtime type
descriptor.

## Status of the original report

The issue is still current, but its original framing is partly out of date:

- #1467 / PR #1480 fixed bytecode lowering for native conformance shims.
- #1481 / PR #1487 fixed the dual declaration-file scope used to analyze witnesses.
- #1488 / PR #1506 fixed witness reification against the conformance target instead of the declaring script.
- None of those changes supplies the exact derived receiver to an inherited static implementation at runtime.
- The remaining inconsistency also affects ordinary inherited static methods, so #1482 must cover the language rule,
  not only native retroactive witnesses.

## Language contract

For a static call made through a class handle whose exact runtime type is `R`, every occurrence of `Self` in the
invoked implementation denotes `R`. This is true even when method lookup selects an implementation declared for an
ancestor `B`.

The rule applies to:

- ordinary inherited static methods and retroactive conformance witnesses;
- native receivers, Foundry Script receivers, and Foundry Script classes whose base is native;
- bare `Self` parameters and returns;
- nested and reified forms such as `Crate[Self]`, `Array[Self]`, `Dictionary[String, Self]`, callable signatures,
  `Type[Self]`, and specialized generic class handles;
- `Self.new()`, runtime casts and type tests, argument and return validation, typed-container writes, and default
  arguments whose types contain `Self`;
- direct calls, dynamically dispatched calls, extracted callables, nested static calls, asynchronous calls, and
  serialized bytecode.

The class handle used at the call boundary determines `Self`:

- `Derived.make()` uses `Derived`, even when `make` is inherited from `Base`.
- An explicit `Base.make()` uses `Base`.
- An inherited implementation that delegates through `super` or another inherited static path preserves the incoming
  receiver context. It does not silently reset `Self` to the file or class containing the delegated implementation.
- An extracted callable captures the exact receiver context from which it was obtained.
- If a `Type[Base]` value contains an exact `Derived` handle, static analysis may expose only the safe `Base` upper
  bound, but execution uses `Derived` as the runtime `Self`.
- A specialized generic receiver retains all of its concrete type arguments in the `Self` descriptor.

Runtime validation must use the exact specialization. It must not erase `Self` to `Variant`, the declaring class, the
conformance target, or a common base merely to make the call succeed.

### Dispatch is unchanged

This issue does not change which implementation wins. Existing method lookup, nearest-witness selection,
method-before-witness precedence, shadowing, and coherence rules remain authoritative. Receiver specialization occurs
after dispatch has selected the implementation.

## Primary reproduction

This case proves both static typing and runtime construction:

```foundry
trait Factory:
    abstract static func make() -> Self

extend RefCounted uses Factory:
    static func make() -> Self:
        return Self.new()

func test() -> void:
    var made: Resource = Resource.make()
    print(made is Resource)
```

`Resource.make()` selects a witness declared for `RefCounted`, but `Self.new()` must construct `Resource` and the
return value must be validated as `Resource`.

## Reified-generic reproduction

This case rejects fixes that adjust only the returned signature while leaving runtime generic metadata bound to the
ancestor:

```foundry
class Crate[T]:
    var value: T

trait Packing:
    abstract static func pack(value: Self) -> Crate[Self]

extend Resource uses Packing:
    static func pack(value: Self) -> Crate[Self]:
        var crate := Crate[Self].new()
        crate.value = value
        return crate
```

Calling `ImageTexture.pack(image)` must create a runtime `Crate[ImageTexture]`. After widening the crate itself to
`Variant`, attempting to store an unrelated `Resource` subclass in `value` must fail against `ImageTexture`. A runtime
`Crate[Resource]` or `Crate[Variant]` is incorrect even if the call's static return type appears specialized.

## Architecture

### 1. Static call context

Add an immutable runtime descriptor for the exact static receiver to each `FSFunction` call frame. The descriptor must
represent native classes, Foundry Script classes, and specialized generic class handles without losing concrete type
arguments.

This context is distinct from:

- instance `self`, which remains absent from a static call; and
- the witness/conformance target, which remains useful for declaration-time validation but is not necessarily the
  final runtime meaning of `Self`.

The descriptor is per invocation. Implementations must not mutate shared `FSFunction`, AST, constant-pool, or bytecode
state, because the same inherited function can be called concurrently through different receivers.

### 2. Analysis and lowering

The analyzer continues to calculate a call signature by substituting inherited `Self` against the receiver visible at
the call site. In addition, lowering must preserve the fact that runtime-relevant types originated from `Self`.

`FSDataType::is_self_type` or an equivalent symbolic marker must survive transitively through nested generics,
containers, callable signatures, type handles, runtime validation metadata, construction, casts, and type tests.
Lowering must not permanently replace runtime `Self` with the witness target merely because the witness was compiled
there.

There are therefore two deliberate substitutions:

1. **Declaration substitution:** validate that a witness is legal for its declared conformance target.
2. **Invocation substitution:** resolve every runtime use of `Self` against the exact call-frame receiver.

PR #1506's target-bound declaration behavior remains correct for the first operation. #1482 adds the second operation
rather than reverting the first.

### 3. Dispatch boundaries

Every path that begins a static call must supply the exact descriptor:

- `FoundryScript::callp` uses the original script handle on which the call began, even if lookup walks a base script.
- `FSNativeClass::callp` uses the exact native class handle, even if it finds a witness on a native ancestor.
- `FSSpecializedClassHandle::callp` supplies the script and its concrete type arguments.
- Built-in type handles supply their exact built-in type.
- Optimized/direct VM opcodes supply the same context as dynamic `callp`; optimization must not change semantics.

When dispatch walks to a base implementation, it passes the original descriptor unchanged. An explicit call through a
named base handle starts a new call with that base descriptor.

### 4. Function execution

Extend `FSFunction::call` and `FSFunction::CallState` with the static `Self` context. `CallState` must retain it across
suspension and resumption so coroutines cannot resume with an absent or different specialization.

Use one recursive resolver for all runtime metadata that can contain `Self`. The resolver substitutes the call-frame
descriptor in:

- argument and return checks;
- reified generic arguments and specialized handles;
- typed array and dictionary descriptors;
- callable parameter and return descriptors;
- `Self.new()` and equivalent construction paths;
- casts, type tests, and nested calls whose declared types contain `Self`.

If a function requires late-bound `Self` and no static receiver context exists, report an internal/runtime error. Do
not fall back to the declaration target or `Variant`, because either fallback hides a broken dispatch path.

### 5. Callables and bytecode

An extracted static callable is the pair of the selected callable target and its exact static receiver context. Calling
it later must produce the same specialization as calling the class handle directly.

Compiled bytecode must preserve symbolic `Self` wherever runtime substitution is needed. Source execution and loaded
bytecode must be behaviorally identical. A bytecode format bump is allowed only if the existing serialization cannot
represent the required marker or callable context; a format bump is not itself a goal.

## Relationship to #1468

#1468 and #1482 meet at one dispatch pipeline but repair different stages:

```text
exact receiver
    -> trait membership / requirement closure       (#1468)
    -> witness or method selection                   (existing dispatch rules)
    -> call-frame Self specialization                (#1482)
    -> construction and runtime type validation      (#1482)
```

Example composition:

```foundry
trait Root:
    abstract static func make() -> Self

trait Leaf uses Root:
    pass

extend RefCounted uses Leaf:
    static func make() -> Self:
        return Self.new()

func test() -> void:
    var made: Resource = Resource.make()
```

Because `Resource` inherits `RefCounted`, #1468 makes `Resource` a member of both `Leaf` and `Root` and allows the
`Root` requirement to find the witness declared by the `Leaf` conformance. #1482 then executes that witness with
`Self == Resource`.

The issues should not be merged:

- #1468 is a conformance-registry and trait-closure change. It can be tested without executing a `Self`-dependent
  function.
- #1482 changes compiler/VM call semantics and also affects ordinary inherited static methods with no traits.
- Separate issues give each behavior a focused regression boundary and make failures easier to diagnose.

Add reciprocal links. #1468 should mention that its inherited-supertrait witness must preserve the exact receiver when
executed, with #1482 owning that behavior. #1482 should list #1468 as required for its supertrait cross-product test.
This is an acceptance dependency, not a required implementation order: the core #1482 runtime work can land first,
but #1482 should not close until the combined case passes.

Suggested note to add to #1468:

> Related runtime contract: #1482 owns late binding of `Self` to the exact class handle used to invoke an inherited
> witness. The cross-product acceptance case must prove both supertrait witness discovery here and derived-receiver
> execution there.

## Edge cases and invariants

- A derived declaration that shadows or overrides the ancestor still wins under existing lookup rules.
- Native conformances apply to native subclasses and to Foundry Script classes whose native base inherits the target.
- `Self.new()` through a Foundry Script receiver constructs the script class, not only its native base.
- A conformance declared on a Foundry Script base works for Foundry Script subclasses under the same receiver rule.
- Specialized generic class receivers preserve their concrete arguments when used as `Self`.
- Generic factories such as `func make[T: Factory](type: Type[T]) -> T` remain valid and use the exact runtime `T`
  handle when invoked.
- Declaration-time errors remain strict: an implementation must still satisfy the requirement for the conformance
  target. Late binding does not excuse an invalid witness declaration.
- Missing conformances, incompatible construction, and incompatible dynamic arguments or returns continue to produce
  errors; diagnostics should name the exact expected receiver specialization.
- No syntax, reachability, coherence, shadowing, or overload rule changes are part of this issue.

## Test plan

### Foundry Script behavior fixtures

Cover the following receiver and declaration combinations:

- ordinary Foundry Script base static method called through a Foundry Script subclass;
- Foundry Script base witness called through a Foundry Script subclass;
- native base witness called through a native subclass;
- native base witness called through a Foundry Script class with that native base;
- specialized generic class handles with retained concrete type arguments;
- the #1468 `Root` / `Leaf` cross-product case.

For each applicable combination, cover:

- bare `Self` parameters and returns;
- nested generic `Crate[Self]`;
- `Array[Self]`, `Dictionary[String, Self]`, and `Type[Self]`;
- `Self.new()` and runtime casts/type tests;
- direct calls, a handle stored behind `Type[Base]`, extracted callables, and nested inherited static calls;
- coroutine suspension and resumption;
- incompatible dynamic arguments, returns, and generic writes that must report the exact specialization.

Retain or extend the existing ordinary-method fixtures such as `type_self_inherited_static_parameter.fs` and
`type_self_inherited_static_return.fs` so analyzer behavior and runtime behavior are tested together.

### Dispatch non-regressions

Prove that:

- an explicit base handle uses the base as `Self`;
- inherited and `super` delegation preserve the original receiver;
- nearest implementation, real-method-before-witness behavior, and shadowing are unchanged;
- declaration-time invalid witnesses remain rejected;
- calls without a conformance and normal incompatible construction still fail as before.

### Bytecode round-trip

For ordinary inherited static methods and for direct, inherited, and implied conformance witnesses:

1. Compile functions containing nested `Self` uses.
2. Serialize/export them.
3. Clear source-derived conformance and runtime state so the test cannot reuse the live compiler result.
4. Load the compiled artifact.
5. Repeat exact-receiver, callable, generic, negative-validation, and coroutine assertions.

Source and bytecode paths must report the same observable values and errors.

### C++ runtime assertions

Inspect the returned object's exact class and any runtime generic/container descriptors before and after function
invocation. Asserting only the function's declared return metadata is insufficient: that can pass while the created
value is still specialized to an ancestor.

Tests must assert observable behavior, not implementation source text.

## Documentation

Update `modules/foundry_script/GRAMMAR.md` semantic prose for `Self` to state the inherited static receiver rule for
ordinary methods and conformance witnesses. No EBNF or syntax change is expected.

## Acceptance criteria

- [ ] The exact class handle used for a static call is the runtime meaning of every nested occurrence of `Self`.
- [ ] Analysis and runtime agree for ordinary inherited static methods and conformance witnesses.
- [ ] Native, script, native-backed script, and specialized generic receivers follow the same rule.
- [ ] `Self.new()`, argument/return validation, typed containers, generics, callables, casts, and type tests use the
      exact specialization.
- [ ] Extracted callables and suspended calls preserve their receiver context.
- [ ] Direct/optimized calls, dynamic calls, source execution, and loaded bytecode are behaviorally equivalent.
- [ ] Explicit base qualification uses the base; inherited delegation preserves the original receiver.
- [ ] Existing dispatch precedence and declaration validation do not change.
- [ ] The #1468 supertrait composition test finds the original witness and executes it with the derived receiver.
- [ ] Diagnostics for dynamic mismatches name the exact expected specialization.
- [ ] `GRAMMAR.md` documents the language contract.
- [ ] Focused conformance, `Self`, generic, callable, coroutine, and bytecode tests pass.
- [ ] The native strict validation build and full test suite complete under the repository's standard agent commands.

## Non-goals

- Merging #1468 and #1482 into one implementation issue.
- Changing trait membership closure in #1482; #1468 owns that work.
- Changing method/witness lookup precedence, coherence, or shadowing.
- Adding syntax or exposing a source-level hidden receiver parameter.
- Cloning and recompiling an inherited function separately for every receiver.
- Mutating shared function or bytecode objects at call time.

## Implementation direction considered and rejected

Two alternatives were considered:

1. Add a hidden source-level `Type[Self]` parameter to static methods and witnesses. This makes ABI, reflection,
   callables, defaults, and bytecode signatures more invasive and leaks an implementation detail into many surfaces.
2. Clone or cache a separately specialized function for every receiver. This increases code and cache complexity,
   complicates invalidation and recursion, and is unnecessary when only runtime type resolution varies.

The call-frame context is preferred because it preserves one compiled implementation, makes concurrency safe, and
gives every execution path one explicit source of truth for late-bound `Self`.
