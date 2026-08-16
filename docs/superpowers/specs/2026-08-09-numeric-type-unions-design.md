# Numeric Constraints and User-Defined Type Unions

## Status

Design approved. Revised 2026-08-09 after an adversarial review against the implementation; every open question below is now a locked decision. Implementation is out of scope for this document, but the decisions here are binding on it.

## Goal

Allow Foundry Script code to name static unions of types and use those unions as generic bounds, so scalar numeric constraints can be expressed:

```text
func add[X: Number, Y: Number](left: X, right: Y) -> long:
    # `left + right` is NOT valid here; see "Operators on bounded values".
    # The function must narrow or convert first.
    ...
```

The feature must preserve the existing generic-bound syntax and support both generic arithmetic and explicit type-based specialization.

## Grounding

These decisions were checked against the implementation. The load-bearing facts:

- `FSParser::DataType` (`modules/foundry_script/fs_parser.h:117`) has kinds `BUILTIN, NATIVE, SCRIPT, CLASS, ENUM, TUPLE, TYPE_PARAMETER, VARIANT, RESOLVING, UNRESOLVED`. There is no set-of-types representation today; every `Vector<DataType>` on it is positional or capped at one element.
- Type parameters carry exactly one bound (`type_parameter_bound`, "0 or 1 element", `fs_parser.h:224`), parsed by a single `parse_type()` at `fs_parser.cpp:1750`.
- Tagged unions are `ENUM`-kind with `enum_case_payloads` (`fs_parser.h:202-213`). They do not use `|`.
- `Token::PIPE` is produced at `fs_tokenizer.cpp:1839` and consumed only as bitwise OR at `fs_parser.cpp:5076`. `|` never appears in a type expression.
- `type` is **not** a keyword today (`fs_tokenizer.cpp:559-626`); it is a plain identifier and is used as one in the test corpus.
- The five source-spellable numeric types are exactly `int`, `uint`, `long`, `ulong`, `float` (`fs_parser_data_type.cpp:68-78`). `int8/uint8/int16/uint16` exist as `NumericType` constraints with empty `public_name` and are deliberately not spellable.
- `int`/`long` share carrier `Variant::INT`; `uint`/`ulong` share `Variant::UINT`. Width lives out of band in `NumericType` (`core/variant/numeric_type.h:45-83`).
- **`is` on a numeric type is a carrier-plus-value-range predicate, not a declared-width test.** `OPCODE_TYPE_TEST_BUILTIN` (`fs_vm.cpp:2064-2084`) checks `value->get_type() == builtin_type` and then `numeric_type_contains(numeric_type, *value)`. A `Variant` carries no declared width.
- Flow narrowing lives in `FSAnalyzer::FlowFinalityContext` (`fs_analyzer.h:192-250`, `fs_analyzer_flow_finality.cpp`) and keys only on `FUNCTION_PARAMETER`, `LOCAL_VARIABLE`, `LOCAL_ITERATOR`, `LOCAL_BIND` (`fs_analyzer_flow_finality.cpp:1592-1616`). Members and statics are not narrowable.
- `DataType::to_property_info` (`fs_parser_data_type.cpp:660-679`) documents an approved width-erasure boundary: a `PropertyInfo` transports the carrier only.

## Syntax

Add a dedicated type-alias declaration:

```text
type MyType = int | uint
type Meters = float
```

`const` remains a value declaration and is not reused for type aliases.

### Locked: `type` is a contextual keyword

`type` remains a valid identifier everywhere except at the start of a declaration in a file body or class body, where the three-token sequence `type IDENTIFIER =` introduces an alias. Alias declarations are permitted **only** at file scope and class-body scope, never inside a function body, which makes the disambiguation a fixed two-token lookahead with no expression ambiguity (no declaration position admits an expression statement).

Rejected: making `type` a hard keyword. The fork's clean-break policy permits it, but `type` is a common local-variable name and a hard keyword buys nothing the lookahead does not already give.

### Locked: `|` is contextual and binds looser than `?`

`|` is a type-union operator only while `parse_type()` is active; expression-level `|` remains bitwise OR with unchanged precedence. Within a type expression `|` has the lowest precedence, so `int? | uint` is the union of a nullable `int` and `uint`. There is no parenthesized type form — `(A, B)` is already an unnamed tuple of arity two or more (`fs_parser.cpp:6312-6345`) — so `(int | uint)?` is not spellable and must not be introduced.

Nullability does not need a parenthesized form because it is hoisted (see below).

### Locked: what may be a union member

Members may be builtins, native/script classes, traits, enums and tagged unions, tuples, generic specializations, `Type[T]` handles, nullable forms of any of these, and other aliases.

Rejected as members, each with its own diagnostic: `void`, `Variant` (absorbing — write `Variant` directly), and a bare type parameter (a union of type parameters has no static meaning under erasure).

### Locked: aliases are not generic in v1

`type Pair[T] = ...` is rejected. A member may be a generic *specialization* (`Array[int]`), but the alias itself takes no parameters. Parameterized aliases are a follow-up.

### Locked: aliases are file-local in v1

An alias is visible in the file that declares it and is not registered as a global name, not exported across files, and not reachable through `import`/`namespace`. Cross-file aliases are a follow-up. This keeps v1 free of the global-name-registration and conformance-index invalidation surface that `class_name`/`trait_name` carry.

`Number` is the one exception: it is compiler-provided and globally visible.

## Static semantics

### Alias transparency and normalization

Aliases are transparent. The analyzer expands them for compatibility, assignment, generic inference, reflection metadata, and bound checking. A named alias does not create a nominal subtype or distinct runtime type.

Normalization, in order:

1. Expand alias references recursively, with cycle detection.
2. Flatten nested unions.
3. **Hoist nullability**: a union is nullable if any member is nullable; members are then stored non-nullable. `int? | uint` and `int | uint?` normalize to the same nullable union of `{int, uint}`. `null` is not a member spelling.
4. Remove duplicate members.
5. Canonicalize member order, so equality and diagnostics are deterministic.

A single-member union collapses to that member and is indistinguishable from it thereafter. `type Meters = float` therefore behaves exactly like `float`, including runtime typing.

### Locked: aliases are type-position-only

An alias name is not an expression and not a nominal type. `MyAlias()`, `MyAlias.new()`, `extends MyAlias`, and `uses MyAlias` are errors, including for single-member aliases, each with a diagnostic naming the alias declaration.

`value is MyAlias` where the alias normalizes to more than one member is rejected; the diagnostic directs the author to test an individual member. Where the alias collapses to one member, `is` behaves as that member.

### Generic bounds

The existing upper-bound syntax applies to aliases and unions:

```text
func add[X: Number, Y: Number](left: X, right: Y) -> long:
    ...
```

Satisfaction rules, all routed through `type_argument_satisfies_bound()` (`fs_analyzer.cpp:8870`):

- A **concrete** type argument satisfies a union bound when it satisfies at least one member.
- A **type-parameter** argument is accepted only when its own bound proves it satisfies the target union, preserving the existing strict behavior. An unbounded parameter is rejected against a concrete union bound.
- A **union** type argument satisfies a union bound only when *every* one of its members satisfies the bound. This is the case the original design omitted; it arises from inference, since inferring `T` from a union-typed argument yields the normalized union.

`Number` is a compiler-provided alias containing exactly `int`, `uint`, `long`, `ulong`, and `float` — the five source-spellable numeric types. It is a reserved global name: declaring `class_name Number`, `trait_name Number`, or `type Number` is an error. It is closed: user-defined types do not become numeric by belonging to a union or by defining operators, and this is not a staged restriction — opening it is a separate design.

If narrower integer widths ever become source-spellable, `Number` gains them by construction; it is defined as "the source-spellable numeric types", not as a hand-written list of five.

### Operators on bounded values

For a bounded generic expression such as `left + right`, the analyzer enumerates the member sets of both operands, applies the existing validated operator and promotion logic to each permitted pair (`FSAnalyzer::get_operation_type`, `fs_analyzer.cpp:15736`, which delegates integer promotion to `FSNumericConversion::promote_integer_pair`, `fs_type.cpp:311`), and:

- rejects the operation if **any** permitted pair has no valid result;
- otherwise **locked: the result type is the normalized union of the per-pair results**, which collapses to a single type when all pairs agree.

Union-of-results is chosen over "a single common statically representable type" because unions are now expressible, so there is no reason to force a lossy join. `int | long` plus `int | long` yields `long` (all pairs promote to `long`); `int | float` plus `int` yields `int | float`.

**Locked consequence, and it is the important one:** under `[X: Number, Y: Number]`, `left + right` is **always rejected**. The existing promotion matrix (`fs_type.cpp:334-353`) has no common type for `int` with `ulong` or for `long` with `ulong`, because `Variant::INT` and `Variant::UINT` are disjoint carriers and Variant registers neither mixed-carrier arithmetic nor an INT↔UINT conversion (`fs_analyzer.cpp:15887-15895`). Direct arithmetic under a full `Number` bound is therefore not a feature this design delivers, and the diagnostic must say so concretely, naming the offending pair. Narrower unions such as `int | long` do permit direct arithmetic. Implementers must not "fix" this by weakening carrier rules.

The result annotation remains the programmer's responsibility. Returning `long` does not guarantee that a `float` or `ulong` result is losslessly representable; conversion, overflow, and precision behavior remain governed by explicit logic and existing conversion rules.

Known pre-existing asymmetry, in scope to *not* regress and out of scope to fix: `var f: float = some_long` is rejected as needing an explicit conversion (`fs_type.cpp:417-438`), but `some_long + 1.5` is accepted and yields `float` with no gate (`fs_analyzer.cpp:15829-15836`). Set-wise checking must reproduce this behavior rather than diverge from it.

### Narrowing

**Locked: `is` on a numeric member is a value-range test, not a declared-type discriminator.** This follows from the runtime (`fs_vm.cpp:2064-2084`) and cannot be designed around without adding a runtime width carrier, which is out of scope. The consequences are normative:

- `int` is a *subset* predicate of `long`, and `uint` of `ulong`. For a value of `5`, both `is int` and `is long` are true.
- True-branch narrowing to the tested member is **sound**: if the value's carrier matches and its magnitude fits, treating it as that width in the branch is correct. This is the existing behavior and existing fixtures depend on it (`tests/scripts/runtime/errors/fixed_width_integer_flow_narrowed_type_test.fs`).
- False-branch narrowing must be **downward-closed under the subset relation**, not plain member removal. `is not long` removes `long` *and* `int`; `is not ulong` removes `ulong` *and* `uint`. Removing only the named member is unsound. `is not int` removes only `int`, which is correct because a value that does not fit `int` may still be a `long`.
- Same-carrier members cannot be discriminated from each other in the general case, so a union such as `Number` cannot be split into five disjoint arms. A chain that tests the wider member first makes the narrower arm unreachable. **Locked (revised by #2160): this is an error, not a warning**, and it is reported through one of two complementary rules, because a warning on the same node the error path already rejects is never observable:
  - When the failed test leaves a non-empty surviving set, the later test is checked against that set and rejected because nothing in it can match — including when the set has collapsed to a single alternative, where the diagnostic names that alternative instead of a set.
  - When the failed test leaves *nothing*, subtraction cannot express the result: there is no bottom type, `apply_failed_type_test_flow_narrowing()` gives up, and the later test would be checked against the full set and accepted. The exhausting test is reported instead — it is statically always true, so its false branch, and therefore every arm after it, is dead.
  - Subsumption is decided only between numeric types on a shared carrier and between identical types, so the rule fires exactly where the analyzer can prove it. A union of two subclasses tested against their shared base is not reported; a nullable set is not reported either, because null does reach the false branch of a test on a non-nullable type.
  - A type test written as a `match` pattern is exempt from the always-true half of the rule. A branch is reached only after every earlier pattern failed, and that ordering is not modeled as narrowing, so a pattern that subsumes the whole set can still be the arm that tells the remaining alternatives apart.

Type tests narrow union and bounded-generic values to the tested member in the true branch:

```text
func convert[X: Number](value: X) -> long:
    # Narrowest-first. Testing `long` before `int` would make the `int` arm unreachable.
    if value is int:
        return value
    if value is long:
        return value
    if value is uint:
        return long(value)
    if value is ulong:
        return long(value)
    return long(value)  # float
```

Narrowing composes with existing `is`/`is not` flow analysis, preserves nullable handling, and leaves the runtime representation untouched — it only refines the static type in the control-flow region.

**Locked: narrowing a bounded type parameter refines the value, not the parameter.** Inside `if value is int:`, the static type of `value` becomes `int`; the type parameter `X` is unchanged and every generic substitution, return check, and further application still sees `X`. Consequently, in a function returning `X`, `return value` after narrowing is an error, because `int` does not satisfy an arbitrary `X`. This is deliberate and must be covered by a negative fixture.

**Locked: an assignment is only allowed to lean on a runtime check when the runtime can perform that check.** The compatibility rules accept several unsafe-looking assignments on the grounds that a check backs them at run time. That reasoning is legitimate exactly when two things hold: the destination type still exists at run time, and the value carries the evidence the check needs.

- A class downcast (`var n: Node2D = some_node`) satisfies both. `Node2D` is a real runtime class and every object carries its own. This stays allowed.
- A declared integer width out of a union does not. The value carries a carrier and a magnitude, never a declared width, so an alternative the destination cannot represent would be laundered into the slot untested. Each numeric alternative is therefore judged by the rule governing the concrete assignment it stands for.
- A method-scope type-parameter *destination* satisfies neither half. The caller picks `X` per call and it is erased before the callee runs, so there is nothing to test a value against and no check is emitted at all (`FSDataType::is_type()` accepts any value for a type parameter). A concrete value never satisfies such a slot; only a value already known to be that same parameter does. This includes `null`, which is a concrete value and does not satisfy a bare `X` — a stub that returns nothing useful declares `X?`.

The destination test is applied to the whole shape, not to its outermost type. Wrapping an erased parameter in a container creates no evidence: `Array[X]` compiles to a plain untyped Array, so a concrete `Array[int]` stored there would never be element-checked. Array elements, both dictionary slots, and deeper mixed nesting such as `Array[Dictionary[String, Array[X]]]` are therefore rejected exactly as a bare `X` destination is.

A parameter bounded by a `final` class is the one exception, and it is not an exception to the criterion: no subtype of a final bound can exist, so the parameter denotes exactly that bound and a value the bound accepts is a value every possible type argument accepts. Such an assignment is decided against the bound like any other concrete destination, matching how a final bound closes a receiver elsewhere, and it stays decidable at every nesting depth a bare parameter is rejected at.

**Locked: a class-scope parameter is checked against the receiver, wherever the slot lives.** A class parameter is reified onto the instance from its type arguments, so a slot declared with one is checkable even though it erases. Three kinds of slot rely on that, and all three enforce it:

- An instance member, bare (`value: T`) or composite (`items: Array[T]`, `by_name: Dictionary[String, T]`, and deeper nesting), carries a structured `TypeArgumentBinding` whose surviving parameter nodes are projected against the receiver's reification before every write (`_project_binding_data_type()` in `foundry_script.cpp`).
- A function-body slot — a local's initializer, a later store into that local, and the return — is validated against the same reification by `OPCODE_ASSIGN_TYPED_CLASS_PARAMETER`, resolved through the leaf's per-ancestor table so an inherited body checks against the leaf's specialization. Method bodies are still compiled once per declaration; no per-specialization body is generated. Because the check happens at the slot's own boundary, a wrong value fails there even when the caller consumes the result as `Variant`. The slot itself stays erased: the check rejects values the receiver cannot hold and does not retype the value, so a generic method returning `Array[T]` is still retyped by its concrete consumer rather than by the callee.
- A static function has no receiver, so nothing can reify the parameter and no check exists. A class-dependent destination there is rejected outright, exactly like a method-scope one.

One position is deliberately outside this rule: a parameter used as the type argument of a specialized class handle (`holder: Holder[T]`). Constructing `Holder[T]` inside the declaring class does not reify `T` onto the constructed instance, so there is no argument at run time to check against; enforcing the slot would reject values the program legitimately produces. Those slots stay unchecked until construction-site reification exists.

The mirror direction is not symmetric and stays allowed: a type parameter as the *source* of an assignment to a concrete type is the downcast shape, because the destination is a type the runtime can still name and the erased value carries what a type test needs. That is why `return value` under `-> long` in the example above is accepted while `return value` under `-> X` is not.

At branch joins, the surviving alternatives are unioned; a narrower type must not survive a merge of divergent paths.

**Locked: member and static variables are not narrowable.** Flow narrowing keys only on parameters, locals, iterators, and binds (`fs_analyzer_flow_finality.cpp:1592-1616`). A union-typed member variable is legal but can never be narrowed; the supported pattern is to copy it into a local first. This limitation is documented, fixtured, and not worked around in v1.

`match` narrows through the existing `when value is T` type-test path identically to `if`. Bare-type match patterns for builtins are unsupported today (`fs_analyzer_flow_finality.cpp:1567-1581` accepts only `CLASS`/`NATIVE`/`SCRIPT` and `BUILTIN` with `builtin_type == OBJECT`) and stay unsupported.

### General union values

Unions are valid in variable, parameter, return, callable-signature, and generic-bound positions:

```text
var value: int | uint

if value is int:
    print(value + 1)
```

## Runtime representation

**Locked: a multi-member union erases to untyped at runtime.** No wrapper, discriminator, tag, or allocation is introduced, and equally no runtime type check is emitted. A union-typed declaration produces no typed local, no typed parameter check, and a `PropertyInfo` of `Variant::NIL`. The normalized union is recorded in the rich compiled `FSDataType` metadata channel, which is already the authoritative channel for information a `PropertyInfo` cannot carry (`fs_parser_data_type.cpp:670-679`).

A single-member alias is fully transparent and keeps the member's runtime typing, including its `NumericType` width.

Two consequences follow and are locked:

- **`@export` on a multi-member union type is rejected**, with a diagnostic distinct from the existing "Export type can only be built-in, a resource, a node, or an enum" (`fs_parser.cpp:7478`).
- **Typed containers reject multi-member union element types.** `Array[int | uint]` and `Dictionary[String, int | uint]` are errors. A typed container enforces exactly one element `Variant::Type` plus one `NumericType` at runtime (`core/variant/container_type_validate.cpp`), which a union cannot supply. Authors wanting a heterogeneous container use `Array[Variant]`. This reverses the original design's claim that unions are valid in container-element position.

Union metadata must survive the compiled-bytecode round trip. The script corpus is executed twice — once from source and once through compiled bytecode (`modules/foundry_script/tests/fs_test_runner_suite.h:71,84`) — so serialization is a correctness requirement, not a nicety.

Runtime generic erasure is otherwise unchanged.

## Diagnostics and invalid forms

Each of these has its own actionable, source-located diagnostic:

- Empty unions.
- Invalid or unresolved union members, identifying both the alias and the member.
- Alias cycles, reported deterministically at one designated declaration.
- `void`, `Variant`, or a bare type parameter as a member.
- A parameterized alias declaration.
- An alias used as an expression, constructor, `extends`, or `uses` target.
- `is` against a multi-member alias.
- A union operand pair with no valid operator result, naming the offending pair.
- A type argument failing a union bound, naming the alias and its normalized members.
- `@export` on a union type, and a union as a typed-container element type.
- A subsumed, statically unreachable type test in a chain, or the always-true test that exhausted the alternatives ahead of it.

A union containing types that share no operators is still a valid type; only the offending use site errors. User-defined classes in a union do not acquire numeric operators or promotion behavior.

## Implementation boundaries

Add a union/type-set representation to `FSParser::DataType` — a new `Kind` value plus canonical member storage — and propagate it through:

1. Parser support for `type` declarations and `|` in type contexts (`fs_parser.cpp:6294-6576`).
2. `DataType::operator==` (`fs_parser.h:361-434`), substitution, `to_string`, `to_property_info` (`fs_parser_data_type.cpp:660`), and bytecode serialization.
3. Alias resolution, normalization, and compatibility.
4. Generic bounds (`fs_analyzer.cpp:8870`) and inference.
5. Set-wise operator validation over `FSAnalyzer::get_operation_type` (`fs_analyzer.cpp:15736`).
6. Flow narrowing in `fs_analyzer_flow_finality.cpp`, including downward-closed removal.
7. Formatter, completion, LSP presentation, refactoring, and `GRAMMAR.md`.

## Documentation

Two documents, two charters, and they must not be conflated.

`modules/foundry_script/GRAMMAR.md` carries the **syntax**: the alias production, contextual `type` and `|`, precedence relative to `?`, normalization including nullability hoisting, and runtime erasure. It is a normative specification written to be a blueprint for re-implementing the front-end in another language, and §10 scopes its sync obligation to tokens, keywords, precedence, and syntax. Usage advice does not belong in it.

`docs/fs_language_primer.md` carries the **guidance**: when a type union is the right tool and when it is not. This is not optional polish. Every locked limitation above sits exactly where an author's first instinct lands — reaching for a union to distinguish cases, which needs a tagged union's runtime tag; or expecting `+` to work under a `Number` bound, which is rejected by design. Shipping the syntax without the guidance invites precisely the misuse the design forbids.

The guidance must state, with reasons: prefer a tagged union when the cases must be reliably distinguished, a trait when a shared method surface is needed, `T?` for "value or nothing", and `Variant` for genuinely dynamic values; a single-member alias is a zero-cost readability device; `[T: int | long]` is roughly the widest bound permitting direct arithmetic; numeric type tests go narrowest-first; and unions are unavailable on member variables, in typed containers, and in exports.

## Testing strategy

The corpus conventions constrain how this is tested; see the plan for the mechanics. Coverage must include:

- Basic aliases, single-member collapse, nested and duplicate flattening, nullability hoisting, cycles.
- Alias use in variables, parameters, returns, and callable signatures; rejection in typed containers and `@export`.
- Generic arguments satisfying and violating union bounds, including a union argument and a forwarded type parameter.
- `Number` accepting all five scalars and rejecting non-scalars, `Variant`, and user types; rejection of a user redeclaration of `Number`.
- Set-wise arithmetic: a union pair that promotes cleanly, a pair with no common result, and an explicit negative fixture proving `[X: Number, Y: Number]` rejects `left + right`.
- Narrowing in true and false branches, downward-closed removal, branch joins, the unreachable-test warning, the non-narrowable member variable, and the negative case where a narrowed value is returned as `X`.
- Runtime fixtures proving single-member aliases keep their width and multi-member unions erase, including through the bytecode round trip.
- Formatter idempotency and LSP presentation.

`GRAMMAR.md` must be updated in the same change as the parser change, per the repository rule.
