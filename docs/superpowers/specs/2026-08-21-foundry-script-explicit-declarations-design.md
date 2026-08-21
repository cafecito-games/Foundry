# Foundry Script Explicit Declarations Design

**Date:** 2026-08-21
**Status:** Approved

## Summary

Foundry Script will no longer accept user-written declarations that silently create gradual types.
Every storage declaration and callable signature must either name its type or use syntax that
explicitly requests inference. The rule is unconditional: there is no project setting, compatibility
mode, staged warning, or migration tool.

The language keeps contextual inference where the binding source is intrinsic to the construct.
Loop variables, pattern bindings, and inline property accessors derive their types from their
iterable, matched position, or property declaration. A deterministically inferred `Variant` is a
valid hard static result in every inference form; contextual bindings never preserve a soft source
type as a silent gradual declaration.

## Motivation

Today these declarations have materially different meanings:

```foundry
var enabled = true       # Gradual storage.
var enabled: bool = true # Explicit static type.
var enabled := true      # Inferred static type.
```

The first spelling makes a declaration gradual without visibly stating that choice. This weakens
assignment and boundary checking and makes a missing annotation semantically significant. Foundry
Script should instead require every declaration to communicate its typing decision. Code that needs
dynamic storage can use `Variant` explicitly or infer it from a statically `Variant` value.

## Goals

- Remove plain gradual variable, constant, parameter, signal, and return declarations from valid
  Foundry Script.
- Make written annotations and written inference markers the only ordinary declaration forms.
- Require complete signatures on named functions, abstract functions, trait and enum methods, and
  lambdas.
- Preserve concise contextual inference for loops, patterns, and property accessors.
- Produce targeted parser diagnostics for omitted typing syntax and analyzer diagnostics for failed
  inference.
- Keep the grammar, formatter, editor tooling, documentation, reflection, and tests consistent with
  the new rule.

## Non-goals

- A compatibility flag or strict-declaration project setting.
- A deprecation period in which legacy declarations remain valid.
- A migration wizard, bulk source rewriter, or automatic project conversion.
- Requiring a concrete non-`Variant` type.
- Adding per-element annotations to destructuring declarations.
- Changing pattern-binding syntax.

## Language Surface

The EBNF excerpts below are illustrative deltas focused on the changed forms. The implementation
must preserve surrounding modifiers, annotations, and property placement from the complete
production in `modules/foundry_script/GRAMMAR.md`, which remains normative.

### Variables

A variable must have an explicit type, optionally followed by an initializer, or use `:=` with a
required initializer:

```foundry
var enabled: bool = true
var enabled := true
var result: Variant
final var limit := 10
static var registry: Dictionary[String, Item] = {}
```

The rule applies equally to members, locals, `static` and `final` declarations, properties, and
annotated declarations such as `@export` and `@onready`.

These forms are invalid:

```foundry
var enabled = true
var result
final var limit = 10
```

An uninitialized variable has no value from which to infer and therefore must name a type.

`null` does not provide a usable variable type, so `var node := null` remains an analyzer error. A
late-bound reference must state the intended nullable or dynamic storage type:

```foundry
var node: Node? = null
var value: Variant = null
```

For a plain literal `var node = null`, the parser should use a focused diagnostic that requires a
written type instead of advertising `:=` as a valid replacement. This parser check applies only to
the already-invalid plain-`=` form. It does not run for `var node := null`, which reaches the
analyzer's existing `null` inference error, so one declaration never receives both diagnostics. For
non-literal expressions whose type later resolves to `null`, the analyzer remains authoritative.

Conceptually, the variable grammar becomes:

```ebnf
variable_decl = "var", identifier,
                ( ":", type, [ "=", expression ]
                | ":=", expression ),
                [ property_clause ],
                NEWLINE ;
```

Declaration modifiers remain outside this production as they are today.

### Constants

Constants continue to require an initializer and must either name their type or request inference:

```foundry
const MAX_COUNT: int = 100
const MAX_COUNT := 100
const ImportedType := preload("res://imported_type.fs")
```

`const MAX_COUNT = 100` is invalid. Conceptually:

```ebnf
constant_decl = "const", identifier,
                ( ":", type, "=", expression
                | ":=", expression ),
                NEWLINE ;
```

### Parameters and callable returns

Every fixed parameter must have an explicit type or infer its type from its default with `:=`:

```foundry
func parse(text: String, fallback := 0) -> int:
    return fallback
```

An inferred default establishes a hard parameter type and callers are checked against it. Plain
defaults such as `fallback = 0` and parameters such as `text` are invalid.

A rest parameter cannot have a default, so it must explicitly declare the collected array type:

```foundry
func collect(...values: Array[Item]) -> Array[Item]:
    return values
```

Bare `...values` is invalid. Explicit `Array` and `Array[Variant]` remain valid gradual rest types;
the declaration is still statically and visibly typed. Because those annotations are explicit,
neither form emits `UNTYPED_DECLARATION` today, and this change must not replace that removed warning
with `INFERRED_DECLARATION`.

Every named function, constructor, static initializer, abstract function, trait method, enum method,
and lambda must declare `-> Type` or `-> void`. Constructors and static initializers use `-> void`;
the existing analyzer rule remains in force, so `-> void` is the only legal constructor or static
initializer return annotation. Omitting the arrow and return type is invalid; return-type inference
is not introduced.

The same complete-signature rule applies to lambdas:

```foundry
var predicate := func(item: Item) -> bool:
    return item.enabled
```

Conceptually:

```ebnf
parameter = identifier,
            ( ":", type, [ "=", expression ]
            | ":=", expression ) ;

function_decl = "func", identifier, [ type_parameters ],
                "(", [ parameter_list ], ")",
                "->", return_type,
                ( ":", block
                | (* abstract only *) NEWLINE ) ;

lambda = "func", [ identifier ], "(", [ parameter_list ], ")",
         "->", return_type, ":", block ;
```

### Signals

Signal parameters are callable interface declarations and must name their types:

```foundry
signal damaged(amount: int, source: Variant)
```

Signals have no parameter defaults, so they have no inference form. `signal damaged(amount)` is
invalid. The signal grammar should use a signal-specific typed parameter production rather than the
more permissive function-parameter production.

For parser recovery, a signal parameter followed by `=` or `:=` still consumes the default
expression and emits the existing `Signal parameters cannot have a default value.` diagnostic.
That recovery path does not also emit a redundant missing-type diagnostic. The valid grammar has no
signal default form, but the focused default-value diagnostic and its behavioral test remain.

### Destructuring

Destructuring always derives its binding types from a statically known tuple source, but it must
visibly request that inference with `:=`:

```foundry
var (name, score) := player_record
const (width, height) := dimensions
```

Plain `var (name, score) = player_record` and `const (width, height) = dimensions` are invalid.
Individual destructuring bindings do not gain annotations in this change.

```ebnf
destructure_stmt = ( "var" | "const" ), "(", destructure_binding,
                   ",", destructure_binding, { ",", destructure_binding }, [ "," ],
                   ")", ":=", expression, NEWLINE ;
```

### Contextual bindings

Loop bindings keep their current concise form:

```foundry
for item in items:
    pass

for item: Item in items:
    pass
```

The `in` source is intrinsic to the binding and acts as its inference context. The analyzer assigns
the iterable's statically determined element type to an unannotated loop variable. That result may
be `Variant`, including iteration over `Array[Variant]`, a raw `Array`, or another source whose
static element model is `Variant`. The binding always receives a hard `ANNOTATED_INFERRED` type. If
the iterable exposes only a soft or unresolved element type, the analyzer normalizes the binding to
hard `Variant`; it does not copy the soft `DataType::INFERRED` source. An explicit loop annotation
remains available as a constraint.

Pattern-binding syntax is unchanged:

```foundry
match message:
    Message.Move(x, y):
        pass
    var other:
        pass
```

Payload bindings take their declared payload field types. Other bindings take the static type of
the subject or structural position, including `Variant` when that is the known type. As with loops,
a soft source is normalized to hard `Variant` at the binding. The pattern is the inference context,
so no `:=` or new annotation syntax is added.

Inline property accessors are also contextual. The property declaration supplies the getter return
type and setter parameter type, while the setter itself returns `void`:

```foundry
var health: int:
    get:
        return health
    set(value):
        health = value
```

The synthesized accessor functions are not user-written incomplete signatures.
The accessor grammar continues to use `setter_inline = "set", "(", identifier, ")"`; it does not
reuse the newly strict ordinary `parameter` production. The implementation must not add redundant
setter parameter or getter return syntax.

### Inferred `Variant`

`Variant` is a valid static type, not an inference failure:

```foundry
func read_value() -> Variant:
    return null

var value := read_value() # Statically inferred as Variant.
```

`:=` may infer `Variant`. The existing optional `INFERENCE_ON_VARIANT` warning remains available to
teams that prefer the annotation `var value: Variant = read_value()`. The language itself accepts
both forms. Its default warning level changes from `ERROR` to `IGNORE`, matching its role as an
opt-in spelling preference rather than a validity rule. The warning enum and project setting remain.

### `:=` tokenization and formatting

`:=` remains the existing two-token sequence `COLON`, `EQUAL`; this change does not add a
`COLON_EQUAL` tokenizer token. Whitespace accepted by the tokenizer between those tokens remains
legal, so `var value : = 1` parses as inferred syntax. The canonical formatter always emits the
compact spelling `var value := 1`, including constants, default parameters, and destructuring
declarations.

## Parser Enforcement and Recovery

The parser owns errors for missing declaration syntax. It should report the omission at the
declaration rather than allowing the analyzer to reinterpret the declaration as gradual. Diagnostic
wording should identify both legal choices where both exist:

```text
Variable "enabled" must declare a type with ": Type" or infer it with ":=".
Variable "node" initialized with "null" must declare a type with ": Type".
Constant "MAX_COUNT" must declare a type with ": Type" or infer it with ":=".
Parameter "value" must declare a type or infer it from a default value with ":=".
Signal parameter "amount" must declare a type.
Function "update" must declare a return type with "-> Type" or "-> void".
```

The parser must continue consuming the initializer, remaining signature, or function body after an
omission so one bad declaration does not prevent useful editor state for the rest of the file. A
recovery node may carry a fallback type internally, but recovery must not make the script valid and
should not cause redundant analyzer errors for the same omission.

Partially typed editor input remains recoverable. Completion contexts should continue to appear
after a declaration name, colon, parameter, closing parenthesis, and arrow. Enforcement happens when
the parser has enough tokens to know that the required syntax is absent.

The existing `datatype_specifier` and `infer_datatype` representation can remain. For every valid
ordinary assignable, exactly one of those states is present. An absent annotation with
`infer_datatype == false` exists only during parser recovery, never as a valid gradual declaration.
The analyzer recognizes that exact state as already diagnosed by the parser and skips its own
inference and untyped-declaration diagnostics for the node.

That suppression is specific to ordinary assignables diagnosed by the parser. A loop or pattern
source that is `UNRESOLVED` because of an upstream analysis failure must retain the original error
while its binding receives the hard `Variant` recovery type; normalization must not erase or
suppress the source diagnostic.

## Analyzer Semantics

The analyzer continues to own semantic typing:

- Resolve written types.
- Reduce initializers and defaults before inference.
- Assign hard `ANNOTATED_INFERRED` types to declarations using `:=`.
- Apply the existing inference-failure rules when an initializer has no usable static type.
- Permit a hard inferred `Variant` and retain `INFERENCE_ON_VARIANT` as an optional warning.
- Validate initializers, defaults, assignments, calls, returns, overrides, signal connections, and
  rest arguments against the resulting types.
- Propagate hard contextual types into loop variables, match bindings, case payloads, and inline
  property accessors, normalizing a soft contextual source to hard `Variant`.

Valid source can no longer reach the declaration path that creates a plain `DataType::INFERRED`
slot from `var x = value` or an omitted parameter annotation. Gradual values and native boundaries
still exist; this change removes only silent gradual declaration syntax.

## Warnings and Settings

`UNTYPED_DECLARATION` becomes obsolete because no valid source site emits it: ordinary omissions are
parser errors, while contextual loop and pattern bindings receive hard inferred types. Remove the
warning enum entry, its default level, its project setting, documentation, and tests.

Keep `INFERRED_DECLARATION` as an optional style warning for written inference, including contextual
loop inference where applicable. Update its documentation so it no longer discusses its relationship
to `UNTYPED_DECLARATION`.

Keep the `INFERENCE_ON_VARIANT` warning code and project setting as an optional style rule. Change
its default level from `ERROR` to `IGNORE` in `fs_warning.h` and the ProjectSettings class reference,
so it does not affect validity in a stock project. Regenerate the three affected warning fixtures
rather than hand-editing their `.out` files.

### Type-completeness gate

Removing the `UNTYPED_DECLARATION` warning does not remove gradual values from the type system.
Native boundaries, erased values, and projections from raw containers can still produce soft source
types, so the type-completeness `source_proof=gradual` leaf remains distinct from hard
`source_proof=variant`.

The union-pilot generator must stop using an untyped script parameter and missing function return
annotation as its gradual witness. Replace its gradual source expression concretely with a raw-array
element passed directly across the existing destination boundary:

```foundry
func test() -> void:
    var raw_source: Array = [5U]
    var stored: Variant = accept(raw_source[0])
```

For the `reflective_write` cells, the same `raw_source[0]` expression is the value argument to
`holder.set()`. The raw element projection produces the soft source proof, while the enclosing
`accept(...)` or `holder.set(...)` operation remains the manifest's `argument_binding` or
`reflective_write` boundary. Therefore the existing coordinates, stable case IDs—including
`text_gradual_argument_binding`—and witness assignments do not change.

Update `source_expression_for()` to render `raw_source[0]` for the `gradual` leaf and add the valid
typed `raw_source` declaration to every generated program. Rewrite both syntactic and resolved
source-proof classifiers to recognize that exact subscript shape and prove that its analyzed type is
a non-hard `Variant` (`DataType::UNDETECTED` for a raw-array element under the current reducer); they
must no longer inspect an untyped `supply` signature. The `gradual_to_erased_parity` and
`gradual_to_variant_parity` relations remain unchanged: each compares the same destination boundary
and observable obligations while varying only the source proof. If the new witness does not satisfy
the existing observations, update the manifest expectation explicitly rather than changing
coordinates or weakening the gate.

Gate-safety tests that use `UNTYPED_DECLARATION` merely to exercise diagnostic capture, suppression,
column mapping, or reconciliation should be retargeted to `INFERRED_DECLARATION` alongside
`UNUSED_VARIABLE`. Tests whose only asserted behavior is the removed warning should be deleted.
After retargeting the generator and migrating embedded source strings, run the full census and
baseline comparator with the newly built binary, regenerate its expected-finding ledger or baseline
artifact if the observed set changes, and verify that no stale `UNTYPED_DECLARATION` code remains in
gate inputs.

## Tooling and Documentation

Every source-producing or source-interpreting surface must follow the new grammar:

- Update `modules/foundry_script/GRAMMAR.md` in the implementation change; it is the normative
  grammar and must land with parser changes.
- Update the formatter and tree printer to preserve `:=`, including destructuring.
- Update completion and signature generation so generated variables, overrides, trait and abstract
  implementations, callbacks, signals, and lambdas always contain valid typing syntax.
- When a native or external signature exposes no narrower type, generated Foundry Script spells
  `Variant` explicitly.
- Remove `text_editor/completion/add_type_hints`, its EditorSettings registration, its class-reference
  member, and every conditional source-generation branch. Foundry Script templates, node drops,
  function creation, override completion, signal connection, and LSP edits always emit type and
  return syntax.
- Keep LSP diagnostics and completion useful on parser-recovery nodes.
- Update Foundry Script documentation, class-reference snippets, tutorials, and examples that use
  invalid declarations.
- Revise gradual-typing documentation to explain explicit `Variant` and dynamic boundaries without
  presenting omitted annotations as valid syntax.
- Regenerate `doc/translations/*.po` through the repository's documentation/translation workflow;
  do not hand-edit translated `untyped_declaration` references.

This work does not add a migration wizard, bulk rewrite command, compatibility setting, or automatic
project conversion.

### In-repository source conversion

The product does not ship migration tooling, but implementing this clean break requires a large,
one-time conversion of the repository itself. A review-time inventory found approximately:

| Newly invalid form | Lines | Files |
| --- | ---: | ---: |
| Named `func` declarations without `->` | 1,598 | 1,307 |
| Plain or bare `var` declarations | 938 | 431 |
| Plain `const` declarations | 317 | 213 |
| Untyped fixed parameters | 211 | 139 |
| Lambdas without `->` | 108 | n/a |
| Plain destructuring declarations | 32 | 17 |
| Untyped signal parameters | 11 | 9 |
| Bare rest parameters | 3 | 3 |
| Foundry Script embedded in C++ strings | 552 | 38 |
| Plain declarations in documentation examples | 1,079 | 163 |

This includes roughly 1,400 tracked `.fs` files plus C++ tests and source generators, documentation,
Android instrumented assets, `modules/foundry_script/grammar/fixtures/highlighting_sample.fs`, and
`misc/foundry_perf/bench.fs`.

Implementation may use a disposable, non-shipping rewrite script to perform the mechanical portion
of this repository conversion. Run it before the parser rejects legacy source, while the old parser
and analyzer can still classify declarations. Its rewrite policy is semantic:

- Use `:=` when the initializer provides the intended hard type and later stores remain compatible.
- Use an explicit written type when the declaration intentionally models a wider or dynamic slot.
- Use `: Variant` rather than `:=` when the initializer is statically `Variant`; the corpus runner
  forces `INFERENCE_ON_VARIANT` to `WARN`, so this avoids unrelated `.out` warning churn.
- Rewrite `null` initializers to an intended nullable type or `Variant`; never to `:= null`.
- Add explicit parameter and return types from analyzed signatures where available, then review
  unresolved cases manually.

Sequence the conversion as follows:

1. Inventory and mechanically rewrite tracked `.fs`, embedded C++ source strings, templates,
   generated-source expectations, and documentation examples under the legacy parser. Re-derive
   line and display-column expectations from each rewritten embedded source rather than retaining
   hand-pinned positions from the old spelling.
2. Review behavior-sensitive fixtures and replace intentional gradual declarations with explicit
   `Variant` rather than hard inference.
3. Land parser, analyzer, warning, formatter, editor/LSP, and type-completeness changes together with
   the converted sources.
4. Build a new binary in the same worktree through `scripts/agent_build.py`.
5. Regenerate script and formatter `.out` fixtures with that exact new binary; never regenerate with
   a stale binary from another checkout or invocation.
6. Run the type-completeness census/baseline comparison and the full strict suite.

## Testing

Tests must assert observable parser, analyzer, formatter, tooling, reflection, and runtime behavior.
They must not assert on implementation source text.

### Parser coverage

- Accept explicitly typed and `:=`-inferred members and locals, including `static`, `final`,
  `@export`, `@onready`, and properties.
- Accept typed and inferred constants, including type-handle imports.
- Accept fully typed named functions, lambdas, constructors, static initializers, abstract methods,
  trait methods, enum methods, rest parameters, and signals.
- Accept `:=` destructuring and contextual loop and pattern bindings.
- Reject each omitted variable, constant, parameter, signal parameter, function return, lambda
  return, rest parameter, and destructuring marker with a targeted diagnostic.
- Demonstrate that a bad declaration recovers and later declarations still parse without duplicate
  diagnostics.

### Analyzer coverage

- Infer concrete types and `Variant` through `:=`.
- Preserve existing errors for values from which a usable type cannot be inferred.
- Enforce inferred parameter types at call sites and explicit return types at returns and overrides.
- Validate typed rest parameters and signal signatures.
- Infer concrete and `Variant` loop variables from their iterable element types.
- Prove that loops and pattern bindings over soft sources normalize to hard `Variant`, while their
  existing strict-dynamic assignment behavior remains unchanged and upstream resolution errors are
  preserved.
- Propagate tuple, structural-pattern, and tagged-union payload types into bindings.
- Derive inline property accessor types from the property.

### Tooling and runtime coverage

- Round-trip every new valid form through the canonical formatter.
- Exercise completion and generated signatures near required type and return positions.
- Prove every former `add_type_hints=false` source-generation path now emits valid typed source and
  that the obsolete setting is absent.
- Verify reflected property, method, default-argument, signal, and callable metadata.
- Verify runtime assignment behavior for statically inferred declarations and explicit `Variant`
  declarations.
- Update existing fixtures according to their behavioral purpose: use `:=` when static inference is
  intended and explicit `Variant` when dynamic behavior is the subject of the test.

Final validation uses the repository wrapper for the native strict build and full suite, including
the normative grammar and generated-document checks:

```sh
python3 scripts/agent_build.py --test
```

## Compatibility

This is an intentional source-breaking language change. Existing omitted annotations and plain
`=` declarations fail immediately. There is no legacy mode and no built-in migration path. The
clear parser diagnostics identify the replacement form or forms, while developers choose the
correct semantics for each declaration.
