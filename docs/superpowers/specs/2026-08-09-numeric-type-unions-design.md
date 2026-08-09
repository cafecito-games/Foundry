# Numeric Constraints and User-Defined Type Unions

## Status

Design approved in conversation; implementation is intentionally out of scope for this document.

## Goal

Allow Foundry Script code to name static unions of types and use those unions as generic bounds. This supports scalar numeric constraints such as:

```text
type Number = int | uint | long | ulong | float

func add[X: Number, Y: Number](left: X, right: Y) -> long:
    # Narrow or convert as needed for the chosen mixed-type policy.
    ...
```

The feature must preserve the existing generic-bound syntax and support both generic arithmetic and explicit type-based specialization.

## Syntax

Add a dedicated type-alias declaration:

```text
type MyType = int | uint
type Number = int | uint | long | ulong | float
```

`const` remains a value declaration and is not reused for type aliases. The `|` token is interpreted as a type-union operator only in type contexts; expression-level `|` remains bitwise OR.

Union members may be ordinary built-in types, native/script classes, traits, generic specializations, nullable types, or other aliases, subject to the normal validity rules for type expressions.

## Static semantics

### Alias transparency

Aliases are transparent. The analyzer expands them for compatibility, assignment, generic inference, reflection metadata, and bound checking. A named alias does not create a nominal subtype or distinct runtime type.

Unions are flattened recursively and duplicate members are removed. The resulting member order is canonicalized for equality and diagnostics.

### Generic bounds

The existing upper-bound syntax applies to aliases and unions:

```text
func add[X: Number, Y: Number](left: X, right: Y) -> long:
    ...
```

A concrete type argument satisfies a union bound when it satisfies at least one member. A type parameter forwarded into another bound is accepted only when its own bound proves that it satisfies the target union, preserving the existing strict bound behavior.

`Number` is a compiler-provided scalar numeric alias containing exactly `int`, `uint`, `long`, `ulong`, and `float`. It is closed initially: user-defined types do not implicitly become numeric by belonging to a union or by defining operators.

### Operators on bounded values

For a bounded generic expression such as `left + right`, the analyzer checks the operation against the possible members of the operands' bounds using the existing numeric promotion matrix. The operation is valid without narrowing only if every permitted operand combination has a valid result and a common statically representable result type. Otherwise, the implementation must narrow or convert the operands first; this permits a function such as `add` to define its own mixed-type policy.

The result annotation remains the programmer's responsibility. Returning `long` does not guarantee that a `float` or `ulong` result is losslessly representable; conversion, overflow, and precision behavior remain governed by the implementation's explicit logic and existing conversion rules.

### Narrowing

Type tests narrow union and bounded-generic values to the tested member within the true branch:

```text
func convert[X: Number](value: X) -> long:
    if value is int:
        return value
    if value is uint:
        return long(value)
    if value is float:
        return long(value)
    ...
```

Narrowing must compose with existing `is`/`is not` flow analysis and must preserve nullable handling. A type test does not alter the runtime representation; it only refines the static type in the control-flow region.

### General union values

Unions are valid in variable, parameter, return, container-element, callable-signature, and generic-bound positions wherever a type is currently accepted:

```text
var value: int | uint

if value is int:
    print(value + 1)
```

A union value is represented by its existing concrete runtime value. No wrapper, discriminator, or allocation is introduced.

## Diagnostics and invalid forms

- Empty unions are rejected.
- Invalid or unresolved union members are rejected using the existing type-resolution diagnostics.
- A union containing incompatible forms is still a valid type, but operations or conversions that are not valid for all relevant members produce an analyzer error at the use site.
- User-defined classes in a union do not acquire numeric operators or numeric promotion behavior automatically.
- Union aliases cannot be used as runtime values or constructors.

## Implementation boundaries

The implementation will add a union/type-set representation to `FSParser::DataType` and propagate it through:

1. Parser support for `type` declarations and `|` in type contexts.
2. Alias resolution, flattening, deduplication, canonical printing, and serialization.
3. Compatibility and generic-bound checking.
4. Control-flow narrowing for `is` tests.
5. Operator validation and numeric promotion over bounded/union operand sets.
6. Completion, LSP presentation, refactoring, and grammar documentation.

Runtime generic erasure remains unchanged. The union metadata is used for static checking and tooling; values retain their existing runtime carriers.

## Testing strategy

Add executable Foundry Script fixtures and analyzer/runtime coverage for:

- Basic aliases and nested/duplicate union flattening.
- Alias use in variables, parameters, returns, containers, and callable signatures.
- Generic arguments satisfying and violating union bounds.
- `Number` acceptance of all five scalar types and rejection of non-scalars.
- Generic arithmetic with valid promotion combinations and invalid combinations.
- Type-test narrowing in branches, including nullable values.
- Diagnostics for empty, unresolved, and otherwise invalid aliases.
- LSP/completion and formatter output for alias declarations and union types.

The grammar specification must be updated in the same change as parser changes.
