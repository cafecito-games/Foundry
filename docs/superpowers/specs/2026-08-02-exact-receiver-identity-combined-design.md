# Exact Receiver Identity: Combined Design for #1482 and #1538

## Issue metadata

- **Issues:** [#1482](https://github.com/cafecito-games/Foundry/issues/1482) and
  [#1538](https://github.com/cafecito-games/Foundry/issues/1538)
- **Base design:**
  [Inherited static `Self` specialization](2026-08-02-inherited-static-self-specialization-design.md)
- **Delivery:** One pull request with separate regression boundaries for membership identity and
  invocation-time specialization.

## Summary

Both issues require the runtime to preserve the exact class handle involved in an operation, but they fail at different
stages of the conformance pipeline. #1538 asks whether an exact script class is a member of a trait and currently lets a
shared script-path alias transfer membership between sibling inner classes. #1482 begins after lookup has selected an
inherited static implementation and currently executes runtime uses of `Self` against the declaration or conformance
target instead of the exact receiver used for the call.

The implementation will keep these fixes focused. Membership queries will distinguish an exact class identity from
aliases that are only canonical for a root script. Static calls will carry an immutable exact-receiver descriptor into
the selected `FSFunction`, retain it across callable extraction and suspension, and use it to resolve symbolic `Self`
metadata recursively. Existing dispatch, witness precedence, trait closure, and declaration-time validation will not
change.

## Root causes

### Ambiguous conformance membership aliases (#1538)

Conformance entries intentionally expose several target keys: fully qualified class name, optional global class name,
and resource path. The first two identify a class. The resource path identifies a file and is shared by the root and all
inner classes in that file.

`FoundryScript::_has_script_trait()` and `FSDataType::_script_conforms_to_trait()` currently try the resource path after
the exact names for every script class. A lookup for sibling `B` can therefore find a conformance registered for sibling
`A` through their common path. The same false positive can affect source-backed and bytecode-backed membership because
both indexes store the aliases.

### Lost invocation-time `Self` receiver (#1482)

Static analysis generally specializes an inherited method signature to the visible receiver. Runtime witness
compilation correctly validates and reifies a witness against its declared conformance target. Neither operation gives
the selected implementation the exact class handle that began a later static call.

Consequently, a method declared on `Base` and invoked through `Derived` can expose `Derived` statically while executing
`Self.new()`, nested `Crate[Self]` metadata, argument checks, or return checks as `Base`. The function object cannot be
mutated or cloned per receiver because the same compiled implementation may execute concurrently through several class
handles.

## Design

### 1. Exact script identity for membership

Add one shared script-conformance query path used by both `FoundryScript::_has_script_trait()` and
`FSDataType::_script_conforms_to_trait()`. For each script in the inheritance walk it will:

1. query the Foundry Script fully qualified class name;
2. query a distinct global class name when present; and
3. query the resource path only when that path is the script class's canonical root identity.

A root class without a more specific identity continues to use its path, preserving external/root-script conformances.
An inner class never falls back to the containing file's path. Native-base conformance lookup remains the final fallback
after the script inheritance walk.

The parser registry and serialized runtime registry may continue storing path aliases for compatibility and root lookup.
Correctness is enforced at the query boundary, where the caller knows whether it represents a root or inner class. If a
small registry helper can encode the authoritative-key rule without changing stored formats, both callers will use it;
the change will not migrate unrelated registry consumers.

### 2. Static `Self` receiver descriptor

Use the approved #1482 call-frame architecture. Introduce an immutable runtime descriptor capable of representing:

- a native class handle;
- a Foundry Script class handle;
- a specialized Foundry Script handle with all concrete type arguments; and
- a built-in type handle.

The descriptor is separate from instance `self` and from the conformance target used to validate a declaration. It is
provided only for static execution and is owned by the invocation or retained call state, never by a shared
`FSFunction`, AST node, constant pool, or bytecode object.

### 3. Dispatch boundaries

Every boundary that starts a static call supplies the exact descriptor before method or witness lookup walks to a base:

- `FoundryScript::callp` supplies the original script handle;
- `FSNativeClass::callp` supplies the exact native class;
- `FSSpecializedClassHandle::callp` supplies the script and concrete arguments;
- built-in handles supply the exact built-in type; and
- direct or optimized VM call paths supply the same context as dynamic `callp`.

Lookup continues selecting the same implementation as today. When it delegates to a base implementation or witness, it
passes the incoming descriptor unchanged. An explicit call through a named base handle starts a new call with that base
descriptor.

An extracted static callable captures both the selected callable target and descriptor. `FSFunction::CallState` retains
the descriptor across suspension and resumption. Nested inherited static calls preserve it unless source code explicitly
begins a call through another class handle.

### 4. Invocation-time `Self` resolution

Extend `FSFunction::call` with the static receiver descriptor and use one recursive resolver for runtime metadata marked
as originating from `Self`. The resolver handles:

- bare argument and return types;
- type handles and specialized generic handles;
- array, dictionary, and user-defined generic element types;
- callable argument and return signatures;
- `Self.new()` and equivalent construction;
- runtime casts and type tests; and
- validation metadata used by typed writes and defaults.

The symbolic `is_self_type` provenance must survive compiler lowering and bytecode serialization through every nested
form that is resolved at runtime. Declaration-time witness substitution remains target-bound and strict. Invocation-time
substitution happens later against the exact descriptor and does not weaken an invalid declaration.

If runtime code requires late-bound `Self` but no static receiver descriptor was supplied, execution reports an internal
runtime error. It does not fall back to the declaration target or `Variant`, because either fallback would hide a broken
dispatch path.

## Data flow

```text
exact class handle
    -> exact trait-membership query                    (#1538)
    -> existing method/witness selection               (unchanged)
    -> immutable static receiver on the call frame     (#1482)
    -> recursive runtime Self specialization           (#1482)
    -> construction, validation, callable, or resume
```

The combined PR keeps the first correction independently testable from the later execution stages. A membership test
does not need to execute a witness, and a plain inherited static method tests receiver specialization without involving
the conformance registry.

## Bytecode compatibility

Source and loaded-bytecode behavior must match for both fixes. Runtime conformance registrations loaded from bytecode
will obey the same authoritative identity queries as live registrations. Existing serialized `FSDataType::is_self_type`
flags will be reused where they preserve the required nested provenance; the bytecode format will change only if current
encoding cannot represent a required symbolic marker or captured callable receiver.

A bytecode format bump is permitted when necessary but is not a goal. Any bump must update loader/exporter round-trip
tests and reject incompatible data using the existing version checks.

## Tests

### Membership isolation (#1538)

Add a Foundry Script fixture containing two unrelated inner classes in one file. Give one class a leaf-trait conformance
and the other a root-trait conformance, then assert observable behavior for:

- direct leaf, root, reverse-supertrait, and unrelated `is` checks;
- `as` casts and trait-typed assignment;
- `is_instance_of` through `Variant`;
- both sibling directions so registration order cannot hide the leak; and
- source execution and a compiled-bytecode round trip.

Retain coverage proving that a root script whose canonical identity is its resource path, an external root script,
target inheritance, exact FQCN lookup, and exact global-name lookup still work.

### Static `Self` specialization (#1482)

Add focused fixtures and C++ runtime assertions for:

- an ordinary Foundry Script base static method invoked through a script subclass;
- a witness declared on a Foundry Script base and invoked through a script subclass;
- a witness declared on a native base and invoked through both a native subclass and native-backed script class;
- bare `Self`, `Self.new()`, `Type[Self]`, `Array[Self]`, `Dictionary[String, Self]`, `Crate[Self]`, and callable
  signatures;
- direct and dynamic calls, extracted callables, inherited/nested delegation, explicit base qualification, and async
  suspension/resumption;
- specialized generic class handles retaining concrete arguments;
- exact-specialization diagnostics for incompatible dynamic arguments, returns, and typed writes; and
- source/bytecode parity for ordinary methods, direct witnesses, inherited witnesses, and implied-supertrait witnesses.

The #1468 cross-product fixture will prove that supertrait membership finds the existing witness and that executing that
witness uses the derived receiver. Dispatch precedence, shadowing, and declaration-time invalid-witness fixtures remain
non-regressions.

Tests assert returned values, exact constructed classes, runtime generic/container descriptors, casts, membership, and
diagnostics. They do not inspect implementation source text.

## Documentation

Update `modules/foundry_script/GRAMMAR.md` semantic prose for `Self` to state that the exact class handle used for a
static call determines every nested runtime occurrence of `Self`, including when the selected method or witness is
inherited. No syntax or EBNF production changes are expected.

## Acceptance criteria

- Sibling or unrelated inner classes sharing a script path cannot acquire each other's retroactive conformances.
- Exact FQCN/global identity and canonical root-script path identity continue to work.
- Parse-time and bytecode-loaded conformance registrations produce identical membership behavior.
- Every nested runtime occurrence of static `Self` uses the exact invoking class handle.
- Native, script, native-backed script, built-in, and specialized generic handles use one receiver rule.
- Callables, inherited delegation, async state, direct calls, dynamic calls, and bytecode retain the receiver context.
- Existing method/witness selection, trait closure, coherence, and declaration validation remain unchanged.
- Runtime mismatches report the exact expected specialization instead of erasing it.
- Focused tests, the native strict validation build, and the full test suite pass.

## Non-goals

- Replacing all conformance-registry keys with a new universal identity system.
- Changing trait inheritance or the supertrait closure delivered by #1468.
- Changing method/witness lookup precedence, shadowing, coherence, or visibility.
- Adding syntax or a source-visible hidden receiver parameter.
- Cloning or mutating compiled functions for each receiver.
- Broadly refactoring runtime type representation outside the paths required to resolve symbolic static `Self`.

## Alternatives rejected

### Universal identity migration

Replacing registry aliases, class handles, serialized targets, and runtime type descriptors with one new identity object
would remove several historical seams, but it expands both issues into a repository-wide migration. The focused query
rule and call-frame descriptor deliver the required semantics with smaller compatibility and review risk.

### Narrow special cases

Removing every script-path alias would break valid root-script conformances. Patching only `Self.new()` or a function's
top-level return type would leave nested generics, typed containers, callable signatures, casts, async state, and loaded
bytecode inconsistent. These symptom fixes do not satisfy the issues' acceptance criteria.
