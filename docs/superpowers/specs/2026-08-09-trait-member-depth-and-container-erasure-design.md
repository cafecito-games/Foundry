# Trait-Member Depth and Container-Erasure Investigation

## Status

Approved and published on 2026-08-09:

- [Issue #1808](https://github.com/cafecito-games/Foundry/issues/1808) — derived-receiver analyzer lookup.
- [Issue #1966](https://github.com/cafecito-games/Foundry/issues/1966) — class-parameterized typed-container return retyping.

The two items were originally parked together in #1808 after #1712 / PR #1807. Investigation proved both defects, but also proved that they have independent reproductions, causes, implementation boundaries, and test matrices. They are therefore specified as separate issues.

## Investigation baseline

The behavior was reproduced on `develop` at `95c05661ef` using the existing macOS development editor binary and scratch projects under `.test_scratch/issue-1808/`.

The direct generic-trait control rejects a bad method argument during analysis:

```text
Cannot pass a value of type "String" as "int".
Invalid argument for "set_value()" function: argument 1 should be "int" but is "String".
```

The same member reached through `GenericDerived extends GenericBase[int]`, where `GenericBase[T] uses Holder[T]`, analyzes without an error. Correct values execute through both child and grandchild receivers, proving that runtime inheritance exposes the flattened member. Incorrect values reach runtime validation and fail against the correct reified `int` binding.

A separate reproduction showed that both of these direct calls analyze as returning `Array[int]` but produce an untyped runtime array:

```fs
Bag[int].new().singleton(41) # generic class method
IntBag.new().singleton(41)   # method flattened from Bag[int] trait
```

Assigning either result to explicit or inferred `Array[int]` fails with:

```text
Trying to assign an array of type "Array" to a variable of type "Array[int]".
```

The generic-class reproduction uses no trait or inheritance. That isolates the second defect from #1808's analyzer lookup failure.

## Decomposition

| Concern | #1808 | #1966 |
|---|---|---|
| Observable failure | Wrong derived-receiver programs compile, then fail at runtime | Statically concrete typed-container results remain untyped at runtime |
| Minimal shape | `Derived extends Base[int]`, `Base[T] uses Holder[T]` | Direct `Bag[int]` generic class method |
| Primary layer | Analyzer member/call lookup | Analyzer call classification plus existing compiler conversion |
| Existing correct mechanism | `specialize_ancestor_type()` can already project a base-level trait edge | Erased-container marker, conversion opcodes, and compiler consumer matrix |
| Missing connection | Lookup consults only the starting class's traits | Class-scoped type parameters never set the erased-return marker |
| Compiler/VM work | None | None expected; reuse existing opcodes |
| Dependency | None | None |

## Approaches considered

### Shared analyzer lookup and existing conversion marker

This is the selected approach. #1808 gains one hierarchy-trait lookup policy reused by ordinary member reduction, direct call signatures, and explicit generic-method lookup. #1966 extends the existing erased-container classification to class-scoped type parameters before receiver specialization removes the evidence.

This keeps both fixes aligned with existing semantics: analyzer ASTs remain the source of static generic contracts, and runtime-erased typed-container returns continue to be repaired at concrete consumers.

### Patch each lookup and assignment site independently

This could produce smaller first diffs, but it would leave three analyzer inheritance loops with separate ordering and error behavior. For #1966 it would also duplicate array/dictionary conversion decisions across local declarations, arguments, returns, setters, and fields. The existing code already centralizes both policies enough that local patches would create avoidable drift.

### Compile specialized trait/class method bodies

Compiling `Bag[int]` bodies with concrete `Array[int]` metadata could avoid some call-site conversion. It does not generalize to `class ForwardingBag[U] uses Bag[U]`, where the argument remains open until instance construction, and it would introduce a second runtime specialization model for functions. This is disproportionate to both defects and conflicts with the established erasure design.

## #1808 design

Trait members stay on their declaring trait's analyzer `ClassNode`; the compiler alone flattens concrete members into the applying `FoundryScript`. PR #1807 made `specialize_ancestor_type()` capable of walking from a derived receiver through a specialized base and then across that base's trait application. Current lookup simply never supplies the declaring trait when only a base class applies it.

The analyzer will preserve existing ordinary class and lexical-outer lookup, then search traits along the receiver's inheritance chain. Trait search follows only `base_type.class_type` edges, from the most-derived class toward the oldest base, preserves each class's resolved-trait order, and de-duplicates first reach. Ordinary class/base declarations continue to outrank all trait members, matching compiler flattening; a more-derived trait member outranks a base-trait member.

The same ordered policy must serve:

- variables, signals, function/callable values, and bare/`self`/explicit member reduction;
- direct function signature lookup;
- explicit generic-method application.

Once a trait declares the selected member, consumers specialize it using the original receiver datatype and that trait. They do not restart from an unspecialized base node, consult compiled-script reflection as the source of truth, copy trait members into class AST tables, or mutate the shared trait declaration.

Coverage includes wrong variable writes, arguments, returns, signal connections, typed-container static mismatches, and explicit generic-method applications. Positive coverage includes concrete and forwarded base applications, a grandchild, transitive traits, bare/`self`/explicit receivers, callable values, cross-file classes, two independent specializations, precedence, diamonds, and outer-class isolation.

## #1966 design

Containers involving class parameters are intentionally erased in a reusable generic class or forwarded trait method body. The analyzer nevertheless specializes the call's static result, so a consumer can correctly expect `Array[int]` or `Dictionary[String, int]`.

The existing generic-method path records this mismatch on `CallNode::returns_erased_container` before substituting method type arguments. Compiler consumers then use the existing typed-array/dictionary conversion opcodes for declarations, assignments, arguments, returns, setters, and field initializers. Class and trait type parameters are specialized in receiver signature construction outside that generic-method hook, leaving their calls unmarked.

Call-signature construction will detect a declared top-level `Array` or `Dictionary` whose nested element/key/value structure contains a class-scoped type parameter. It will preserve that erased-return fact through receiver specialization and rich callable formation. The compiler will continue converting only when the consumer has concrete typed-container metadata; plain `Array` and `Dictionary` targets remain untyped.

Coverage includes generic classes, direct generic traits, forwarded trait parameters, arrays, dictionaries, nested dependence, explicit and inferred targets, rich callable `call()`/literal `callv()`, every existing conversion consumer position, untyped-target preservation, incompatible content rejection, and compiled-bytecode round-trip.

## Non-goals

- No parser, token, syntax, or grammar-production change.
- No copied or mutated analyzer trait members.
- No compiler flattening change for #1808.
- No per-specialization function compilation or new runtime generic representation for #1966.
- No weakened ordinary container assignment or member-write validation.
- No editor-symbol change: its hierarchy-trait lookup already exists, and no editor defect was reproduced.
- No bytecode format or version change.

## Verification contract

Implementation of either issue must begin with its specified observable failing fixtures. Focused iteration may use the Ninja backend, but handoff requires the repository's native strict build:

```sh
python3 scripts/agent_build.py
```

Runtime fixtures must pass in both source and compiled-bytecode corpus modes. Final validation runs the focused Foundry Script tests and the full suite through the agent wrapper, using the structured progress file for long runs, and must end with `[doctest] Status: SUCCESS!`.
