# FoundryScript Generic Tagged Unions Design

Status: approved design; tracked by GitHub epic
[#1595](https://github.com/cafecito-games/Foundry/issues/1595).

## 1. Summary

FoundryScript tagged unions cannot currently declare type parameters. This forces APIs whose success
payload varies by caller, including JSON decoding, to use generic classes even when their natural model
is a closed sum type. This design adds bounded, invariant type parameters to named tagged unions while
preserving their existing read-only `[tag, payload...]` runtime representation.

```foundry
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)

func parse_count(text: String) -> Result[int, String]:
	if text.is_valid_int():
		return Result[int, String].Ok(text.to_int())
	return Result[int, String].Err("not an integer")
```

V1 is deliberately runtime-erased. `Result[int, String]` and `Result[float, String]` are distinct,
invariant static types, but their values remain the same compact read-only Arrays used by every tagged
union today. Runtime reification is a linked follow-up with its own design and implementation epic.

The feature also ships a builtin `Result[T, E]` tagged union as the integrated acceptance consumer.
The existing `JsonResult[T]` remains unchanged; a linked follow-up evaluates migrating it to
`Result[T, JsonDecodeError]` without losing its JSON-specific helpers.

## 2. Motivation

Tagged unions and generics already solve complementary problems:

- a tagged union expresses one value selected from a closed set of payload shapes;
- a generic declaration expresses a shape whose contained types vary by use site.

Without their intersection, users must replace value-like sum types with mutable reference classes:

```foundry
class_name JsonResult[T] extends RefCounted

var value: T?
var error: JsonDecodeError?
var _succeeded: bool
```

That workaround has extra allocation and state-invariant costs. It can represent impossible states
such as both fields being populated, and it requires a separate success discriminant when `T` is
nullable. A generic tagged union makes those states unrepresentable:

```foundry
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)
```

This work is not a syntax-only extension. Current generic declaration storage, type-parameter lookup,
type application, bound checking, receiver specialization, compatibility, and runtime handles are
class-centric. Tagged-union payload metadata is also intentionally excluded from the general recursive
substitution walk to avoid infinitely expanding recursive unions. The implementation must close each
of those seams explicitly.

## 3. Locked decisions

1. **Only named tagged unions may be generic.** `enum Name[T]` and `enum_name Name[T]` are legal
   when the completed declaration contains at least one payload-bearing case. Unnamed generic enums
   and generic integer-backed enums are errors.
2. **Type parameters are invariant.** Different arguments produce statically incompatible
   specializations, including in assignments, calls, returns, comparisons, patterns, containers, and
   callable signatures.
3. **Every external use supplies the complete argument list.** Bare `Result` is an error outside
   `Result`'s own declaration. There are no defaults, wildcards, partial application, or raw generic
   union types in v1.
4. **A bare self-reference means the open self-specialization.** Inside `Tree[T]`, bare `Tree`
   means `Tree[T]`. Explicit `Tree[T]` is also legal.
5. **Bounds use the existing generic syntax and rules.** `[T: Resource]`, dependent bounds, nullable
   arguments, nested generic arguments, and sibling-bound substitution behave like generic classes.
6. **Construction is explicit in v1.** Users write `Result[int, String].Ok(1)`. Constructor-driven
   inference such as `Result.Ok(1)` is not added.
7. **Contextual case shorthand is a follow-up.** Expected-type-driven `.Ok(1)` is designed as a
   separate feature for generic and non-generic tagged unions rather than a special parser shortcut in
   this epic.
8. **The runtime representation stays erased.** A value remains a read-only `[tag, payload...]`
   Array. No union descriptor or type-argument vector is embedded in the value, and no new construction
   opcode is added.
9. **Static metadata preserves specialization.** Analyzer `DataType`, compiler `FSDataType`, and
   nested `ContainerType` descriptors retain type arguments wherever those models already carry
   generic arguments. This metadata guides static checking and code generation but does not change the
   runtime value.
10. **Specialized runtime tests are intentionally weaker than static identity.** A runtime test can
    validate the tagged-union shape and declared tag domain. It cannot prove erased payload type
    arguments. This limitation is documented and covered by tests.
11. **Enum methods participate fully.** Enum parameters specialize instance and static enum-function
    signatures. A function's own type parameters shadow enum parameters with the same names.
12. **The builtin `Result[T, E]` lands in the v1 epic.** `Ok` has tag 0 and `Err` has tag 1; this case
    order is a stable runtime contract.
13. **`JsonResult[T]` migration is not part of v1.** Its `fail`, `nested`, path, and explicit success
    behavior remain unchanged until the linked migration issue resolves their destination.
14. **Grammar and tooling land with the syntax they describe.** `GRAMMAR.md`, formatter fixtures,
    semantic classification, completion, hover/signature surfaces, symbols, rename/reference support,
    and doc generation are acceptance requirements rather than optional cleanup.

## 4. Source language

### 4.1 Grammar

The normative grammar changes are:

```ebnf
enum_name_decl = "enum_name", identifier, [ type_parameters ], ":", enum_body ;

enum_decl = "enum", [ identifier, [ type_parameters ] ], ":", enum_body ;
```

`type_parameters` and `type_parameter` retain their existing productions:

```ebnf
type_parameters = "[", type_parameter, { ",", type_parameter }, [ "," ], "]" ;
type_parameter  = identifier, [ ":", type ] ;
```

The parser accepts the parameter list after a named enum identifier and preserves enough syntax to
format malformed declarations deterministically. Semantic validation after the complete enum body
decides whether the declaration is a tagged union. A parameterized declaration with no payload-bearing
case reports the generic-integer-enum diagnostic rather than being silently treated as a generic enum.

Examples:

```foundry
enum Option[T]:
	None
	Some(value: T)

enum Result[T, E: Error]:
	Ok(value: T)
	Err(error: E)

enum_name Tree[T]:
	Leaf(value: T)
	Branch(children: Array[Tree])
```

Rejected declarations:

```foundry
enum[T]:                         # unnamed generic enum
	Value(payload: T)

enum Direction[T]:              # generic integer-backed enum
	North = 0
	South = 1
```

### 4.2 Use-site specialization

Every external type and value use supplies exactly the declared number of arguments:

```foundry
var value: Result[int, String]
var maybe_name: Option[String]
var nested: Array[Result[Dictionary[String, int], String]]

var ok := Result[int, String].Ok(1)
var none := Option[String].None
```

The following are errors:

```foundry
var raw: Result
var short: Result[int]
var long: Result[int, String, float]
var constructed := Result.Ok(1)
```

Generic application must be recognized before the enum metatype's runtime `Dictionary` skin. In a
type annotation, brackets bind type arguments rather than Dictionary element types. In an expression,
`Result[int, String]` is a specialized enum handle rather than an ordinary Dictionary subscript.

### 4.3 Open self-specialization and recursion

Inside a generic union declaration, its bare name denotes the open specialization formed from its own
parameters:

```foundry
enum Tree[T]:
	Leaf(value: T)
	Branch(children: Array[Tree])       # Array[Tree[T]]

	static func singleton(value: T) -> Tree:
		return Tree.Leaf(value)            # open Tree[T] inside the declaration
```

The shorthand applies only to the declaration currently being resolved. A bare reference to some
other generic union remains an error. Explicit self-specialization is legal and equivalent:

```foundry
	Branch(children: Array[Tree[T]])
```

The analyzer publishes an identity shell before resolving payload fields, as tagged unions already do
for recursive declarations. For a generic union, that shell also carries the open parameter handles.
Recursive edges retain the shell rather than copying the completed payload map, so the type graph stays
finite. When a recursive value is used for construction, matching, or binding, the existing declaration
completion path reattaches the completed case table while preserving the edge's concrete type arguments.

Direct and indirect recursion are both supported:

```foundry
enum List[T]:
	Nil
	Cons(head: T, tail: List)

enum Expression[T]:
	Literal(value: T)
	Group(children: Array[Expression[T]])
```

### 4.4 Scope and shadowing

`DataType::TypeParameterScope` gains `TYPE_PARAMETER_ENUM`. Type lookup inside an enum follows this
order:

1. the current enum function's method parameters, including an enclosing generic function across a
   lambda boundary;
2. the current enum declaration's parameters;
3. the containing class and its lexical outer classes.

An enum parameter therefore shadows an enclosing class parameter, and an enum function parameter
shadows both:

```foundry
class Outer[T]:
	enum Choice[T]:
		Value(value: T)                    # Choice.T

		static func convert[T](value: T) -> Choice:
			# Function T shadows Choice.T in the parameter only.
			# Bare Choice still means Choice[Choice.T].
			pass
```

The parameter's scope plus ordinal remains part of its identity, so equal spelling does not collapse
unrelated handles. Bound resolution runs in the declaring enum's scope, not the use site's scope.

A generic union nested in a generic class may mention an enclosing class parameter. When reached
through a specialized outer receiver, outer-class substitution happens before enum-argument
substitution. A use for which the enclosing parameter remains open retains that open static type and
cannot be treated as a fully concrete specialization. This follows the existing nested tuple/member
specialization rule rather than adding an independent capture syntax.

### 4.5 Bounds, nullability, and argument identity

Bound checking reuses the generic-class semantics:

- arguments resolve positionally and a failed argument retains its position;
- arity is checked against the number written, even if one argument failed to resolve;
- all sibling bindings exist before dependent bounds are substituted;
- bounds resolve in the declaration scope;
- an already-failed argument does not produce a second bound diagnostic;
- nested specialized bounds are invariant;
- nullable arguments are distinct from non-nullable arguments;
- `Type[T]` follows the existing nested class-handle rules.

```foundry
enum ResourceResult[T: Resource, E]:
	Ok(value: T)
	Err(error: E)

var valid: ResourceResult[Texture2D, String]
var invalid: ResourceResult[int, String]       # bound error
```

### 4.6 Invariance

Two generic tagged-union values are compatible only when they name the same declaration and every
type argument is invariantly equal. The rule applies recursively and is checked before the historical
enum identity shortcut:

```foundry
var ints: Result[int, String] = Result[int, String].Ok(1)
var floats: Result[float, String] = ints        # error
```

The same distinction applies in:

- function parameters and returns;
- variable and constant initialization;
- Array and Dictionary element types;
- callable and signal signatures;
- enum function receivers and results;
- match subjects and case patterns;
- `is` and flow-narrowed case bindings;
- equality/comparison analysis when both operands have hard types.

An untyped or `Variant` source follows the existing dynamic-check path. Because v1 erases arguments,
the runtime backstop can validate only the union/tag domain, not generic payload identity.

### 4.7 Case construction and payload-less cases

A specialized case constructor substitutes enum bindings through its payload schema before checking
arguments:

```foundry
var ok := Result[int, String].Ok(1)
var wrong := Result[int, String].Ok("one")      # expected int, got String
```

Literal propagation uses the specialized field type. `Result[Array[int], String].Ok([1, 2])` turns
the Array literal into the same typed Array expected by an ordinary `Array[int]` parameter. Dictionary,
Callable, Signal, tuple, nested generic-class, and nested generic-union payloads receive the same
recursive specialization.

A payload-less case is a value of the specialized union:

```foundry
var none: Option[String] = Option[String].None
```

The runtime constant remains the shared read-only `[tag]` singleton. Static specialization lives on
the expression's datatype, not in the singleton itself.

### 4.8 Pattern matching, `is` binds, and narrowing

Case patterns use the subject specialization to substitute payload bind types:

```foundry
func unwrap(result: Result[int, String]) -> int:
	match result:
		Result[int, String].Ok(value):
			# value: int
			return value
		Result[int, String].Err(_):
			return 0
```

The same applies to conditional binds:

```foundry
if result is Result[int, String].Ok(value):
	# value: int
	print(value)
```

A pattern from a different specialization is statically incompatible even though it may have the
same erased tag and arity. Exhaustiveness remains a case-domain check: every specialization of one
declaration has the same finite case set.

Flow narrowing carries the specialization. Narrowing `Result[int, String]` to its `Ok` case never
loses `[int, String]` or widens the payload bind to `Variant`.

### 4.9 Enum functions

Enum functions resolve in the generic enum scope and specialize against their receiver:

```foundry
enum Option[T]:
	None
	Some(value: T)

	static func some(value: T) -> Option:
		return Option.Some(value)

	func is_some() -> bool:
		return self is Option.Some(_)
```

On `Option[int]`, `some` has parameter `int` and returns `Option[int]`. Callable references, hover,
signature help, named-argument canonicalization, async enum functions, and generic enum methods all
consume the substituted signature. A method's own type parameters shadow enum parameters in the
method signature and body without changing what bare `Option` means.

## 5. Type model and analyzer architecture

### 5.1 AST and datatype storage

`FSParser::EnumNode` gains:

```cpp
Vector<TypeParameterNode *> type_parameters;
```

`FSParser::DataType` already has declaration-neutral `type_arguments`, which generic enum handles
reuse. The enum's nominal identity remains `native_type` plus declaration linkage. Open and concrete
handles use:

```text
Result[T, E]             # open declaration handle
Result[int, String]      # concrete use-site handle
```

The hand-written datatype copy, equality, invariant identity, display, property/flat-signature
encoding, callable/signature recursion, editor mirrors, and bytecode conversion must preserve the
arguments. Every switch over `DataType::Kind::ENUM` must be audited for assumptions that all enums
have no arguments.

### 5.2 Shared argument binding

The class-only application helpers are split into declaration-neutral operations whose inputs are:

- declaration kind and display name;
- ordered `TypeParameterNode` vector;
- ordered source argument nodes or already-resolved argument datatypes;
- use-site source node;
- whether bounds are checked immediately or deferred.

The shared layer performs positional resolution, arity diagnostics, binding-map construction, bound
resolution, dependent-bound substitution, and failure suppression. Thin class and enum entry points
retain declaration-specific diagnostics and post-binding behavior.

This is a targeted extraction, not a generic-declaration hierarchy. Classes keep inheritance,
instance reification, and specialized-class-handle behavior. Enums keep payload maps, case domains,
and Dictionary metatype behavior.

### 5.3 Enum specialization

An enum-specific specialization operation takes a resolved enum handle plus enum argument bindings
and returns a new handle with:

- unchanged nominal identity, tag table, case names, and declaration link;
- concrete `type_arguments`;
- each top-level payload field type substituted with the enum bindings;
- metatype/value flags preserved;
- recursive self edges left as finite identity shells;
- any enclosing-class bindings applied before enum bindings.

General `DataType::substitute()` continues to recurse through container elements, ordinary
`type_arguments`, callable parameters/returns, and bounds. It must not blindly recurse through every
`enum_case_payloads` map: doing so would infinitely copy `Tree[T]`'s complete declaration into its own
`Branch` field. The enum-specific operation substitutes each declared payload slot exactly once.
Nested union identities inside a slot are recursively substituted through their ordinary type
arguments, not through their payload maps.

### 5.4 Type-position application

When a resolved type carries bracket arguments, resolution proceeds in this order:

1. language pseudo-types such as `Coroutine[T]` and `Type[T]`;
2. Array and Dictionary element types;
3. generic class specialization;
4. generic enum specialization;
5. declaration-specific rejection for non-generic classes/enums;
6. the ordinary "only arrays and dictionaries" fallback.

The enum branch checks the declaration, validates arguments, specializes payload metadata, and
returns the correct metatype/value form. Because a tagged-union metatype uses `DICTIONARY` as its
runtime builtin, resolution must dispatch by `DataType::kind` before treating it as a typed
Dictionary.

### 5.5 Expression-position application

Subscript reduction recognizes a constant generic enum metatype before ordinary indexing:

```foundry
Result[int, String]
```

It resolves the expression arguments as types, preserves nullable markers, validates arity/bounds,
and returns a specialized enum metatype. The reduced constant remains the declaration's existing
read-only Dictionary; the specialized datatype carries the arguments. Subsequent `.Ok`/`.Err`
resolution therefore sees a specialized payload schema without needing a new runtime handle object.

A comma-separated or nullable-marked list on a non-generic enum reports the generic-enum diagnostic
rather than an ordinary multiple-index diagnostic.

### 5.6 Compatibility

The enum branch in `FSTypeCompatibility::check` changes from nominal identity alone to:

1. preserve the existing int-to-int-enum exception only for non-tagged enums;
2. require both enum kinds and equal nominal identity;
3. require equal argument counts;
4. compare each argument invariantly;
5. preserve existing nullable and dynamic-check handling.

Unspecialized open handles are legal only within their declaration and analyzer internals. They do not
provide a raw external compatibility escape hatch.

## 6. Compiler and erased runtime

### 6.1 Construction

The analyzer annotates a case-construction call with the specialized result type and payload field
types. The compiler uses those types for conversion, then emits the existing tuple construction:

```text
Result[int, String].Ok(1)   -> read-only [0, 1]
Result[int, String].Err(x)  -> read-only [1, x]
```

There is no new opcode and no generic-union runtime allocation. Payload-less cases remain read-only
singleton constants.

### 6.2 Static and bytecode metadata

`FSDataType` and `ContainerType` already carry `type_arguments`. Generic enum conversion preserves
them recursively in function signatures, local/member metadata, typed-container children, and
compiled bytecode. The underlying enum value still lowers to the builtin Array representation.

Compiled-bytecode export/load tests must prove that a script reloaded without parser/analyzer state
still retains enough specialization metadata to:

- reject statically incompatible calls at compile time before export;
- run specialized case construction with the correct field conversions;
- expose specialized completion/reflection metadata where compiled metadata supports it;
- preserve nested `Result[...]` descriptors inside containers and callables.

V1 does not promise that a free-standing runtime Array can reveal its erased arguments after all
static metadata is gone.

### 6.3 Runtime tests and dynamic boundaries

Existing union tests operate on Array shape and tag membership. For a specialized union:

```foundry
value is Result[int, String]
```

the emitted runtime predicate proves only that `value` is a read-only tagged-union-shaped Array with
a tag in `Result`'s declared domain. It cannot distinguish another specialization of `Result`.

Known-specialization mismatches are rejected before code generation. When a `Variant` or untyped
boundary makes the source specialization unknowable, analysis follows the existing unsafe/dynamic
path and runtime validation checks only the erased union domain. Tests must demonstrate both facts so
users are not given a false soundness claim.

### 6.4 Equality, hashing, serialization, and JSON

Runtime equality and hashing remain deep Array value semantics. Two dynamically obtained values with
the same tag and equal payloads compare equal even if they originated from different erased
specializations. Hard-typed source comparisons between incompatible specializations are rejected.

Variant text/binary encoding, resource persistence, and native JSON continue to encode the Array
value. Type descriptors attached to typed slots preserve arguments through the existing recursive
generic metadata transports. No new serialized value marker is introduced.

The generic tagged-union documentation names these erasure consequences explicitly.

## 7. Builtin `Result[T, E]`

The builtin source registry gains a global type with the semantic declaration:

```foundry
# A successful value or an error value.
# Case order is a stable runtime and serialization contract.
enum_name Result[T, E]:
	Ok(value: T)
	Err(error: E)
```

Acceptance requirements:

- `Result` is globally resolvable without import or preload;
- both type and expression specialization work;
- `Ok` and `Err` construction enforce substituted payload types;
- match patterns and `is` binds infer `T` and `E` respectively;
- nullable arguments work, including `Result[String?, E].Ok(null)`;
- nested use such as `Array[Result[int, String]]` retains specialization;
- builtin source registration, parser caching, docs, and compiled-bytecode export are covered;
- tag order is fixed at `Ok = 0`, `Err = 1` and documented as a wire contract.

No convenience methods are added in v1. They would either duplicate case construction or require a
policy for method names and transformations that is independent of the generic-union capability.

## 8. Diagnostics

Diagnostic wording may be adjusted for repository style during implementation, but every category
below must have a dedicated observable fixture and must include the named mechanical information.

| Category | Required information |
|---|---|
| Unnamed generic enum | Type parameters require a named tagged union |
| Generic integer enum | Declaration name; generic parameters require a payload-bearing tagged union |
| Bare external use | Generic union name; exact expected argument count |
| Non-generic enum application | Enum name; declaration is not generic |
| Arity mismatch | Declaration name; expected count; supplied count |
| Bound violation | Actual argument; effective bound; parameter name |
| Failed argument resolution | Argument position/source; no duplicate bound error |
| Constructor payload mismatch | Case name; argument position; specialized expected and actual types |
| Assignment/return/call mismatch | Both complete specializations |
| Pattern specialization mismatch | Pattern specialization and subject specialization |
| Recursive wrong arity | Recursive declaration name; expected and supplied counts |
| Premature contextual shorthand | `.Case` is not available; spell the complete specialized union |
| Export/inspector restriction | Existing tagged-union export diagnostic remains applicable |

Additional rules:

- a parser recovery error must not cascade into misleading Dictionary-index diagnostics;
- a failed type argument keeps its ordinal so later arguments never shift left;
- open internal handles must never print as a valid external raw type;
- diagnostics render nested and nullable specializations completely;
- erased runtime limitations belong in documentation, not as warnings on every valid use.

## 9. Formatter, editor, LSP, and documentation

### 9.1 Formatter

The formatter prints:

```foundry
enum Result[T, E: Error]:
	Ok(value: T)
	Err(error: E)
```

It preserves declaration comments, multiline parameter bounds, trailing commas accepted by the
generic parameter grammar, enum annotations, case payload comments, and enum function formatting.
Input and expected fixtures cover inner `enum` and whole-file `enum_name` forms.

### 9.2 Semantic and navigation surfaces

- enum type parameters are definition tokens of the existing type-parameter classification;
- references in bounds, payloads, methods, and recursive self-specializations are reference tokens;
- go-to-definition, references, and rename use scope+ordinal identity and do not touch shadowing
  class or method parameters;
- document symbols display the enum's parameter list without changing the enum/case hierarchy;
- namespace/import/preload chains retain navigation to the generic `enum_name` declaration.

### 9.3 Completion, hover, and signature help

- completion after `Result[` suggests types under the existing type-argument rules;
- completion after `Result[int, String].` lists `Ok`, `Err`, and enum static functions;
- case completion shows substituted signatures (`Ok(value: int)`, `Err(error: String)`);
- hover on a specialization prints the complete type;
- hover on `T` or `E` shows its declaring enum and effective bound;
- enum function hover and signature help substitute receiver arguments;
- pattern and `is` bind hover shows concrete payload types;
- inheritance-solved cross-file completion works for `enum_name` dependencies.

Tooling consumes analyzer specialization helpers. It must not implement a second payload-substitution
algorithm.

### 9.4 Documentation

`modules/foundry_script/GRAMMAR.md` changes in the parser/formatter child. User-facing language docs
cover declaration, construction, matching, bounds, invariance, recursion, enum methods, and erasure.
Doc generation preserves type-parameter lists and specialized signatures. Builtin `Result` docs include
examples and the stable case-order contract.

## 10. Cross-file and integration behavior

Generic `enum_name` files retain the single-artifact rules of ordinary `enum_name` files. Their
global registration still maps a qualified name to the declaration path; arity and parameter metadata
come from the depended parser/analyzer rather than the global registry bit.

The implementation covers:

- direct global use;
- namespace-qualified use;
- imported namespace use;
- `preload` access through the supported enum-name/type path;
- recursive references within the same `enum_name` file;
- dependencies raised only to inheritance-solved state for completion;
- analyzer/parser cache reuse across multiple specializations;
- compiled-bytecode export/load;
- name mangling and keep rules, with enum parameter names analyzer-only and case names following
  existing tagged-union rules.

Different specializations share one parsed declaration and one runtime constant Dictionary. No
per-specialization parser, script resource, or constant table is created.

## 11. Testing strategy

Tests assert parser/analyzer/runtime/tooling behavior. No test may assert that implementation source or
Markdown contains a substring.

### 11.1 Parser and formatter fixtures

Positive coverage:

- one and multiple parameters;
- bounded and dependent parameters;
- trailing comma;
- inner and `enum_name` declarations;
- comments and multiline bounds/payloads;
- recursive bare and explicit self-specialization.

Negative coverage:

- unnamed generic enum;
- generic integer-backed enum;
- duplicate parameter names;
- malformed/empty parameter lists;
- recovery at the `]`/`:` boundary.

### 11.2 Analyzer fixtures

Positive coverage:

- type- and expression-position specialization;
- exact arity and bounds;
- nullable and nested arguments;
- open self-specialization;
- direct and collection-mediated recursion;
- payload-less case typing;
- specialized enum method parameters and returns;
- generic enum method shadowing;
- outer-class/enum/method parameter shadowing;
- callable and signal payloads/signatures;
- invariant identity through Array/Dictionary and function boundaries;
- cross-file, namespace, import, preload, and dependency-cache paths.

Negative coverage contains one fixture per diagnostics-table row and distinct fixtures for wrong
specializations in assignment, return, call, comparison, case pattern, and `is` bind positions.

### 11.3 Runtime fixtures

- construct every case and verify the read-only `[tag, payload...]` shape;
- verify payload conversion for concrete numeric and typed-container fields;
- match and `is` bind values with concrete inferred bind types;
- run recursive `Tree[int]` and `List[String]` values;
- run static, instance, async, and generic enum functions;
- store specializations in typed containers and retrieve them;
- prove statically known incompatible specializations do not compile;
- pass a union through `Variant` and prove runtime `is Result[int, E]` checks only erased tag/domain;
- preserve Array equality/hash behavior;
- run builtin `Result`, including nullable success and error payloads.

### 11.4 C++ doctests

- `DataType` copy/equality/string rendering with enum arguments;
- declaration-neutral argument binding and dependent bounds;
- one-level payload specialization without recursive map expansion;
- enum compatibility invariance;
- `FSDataType`/`ContainerType` and bytecode round-trips;
- builtin source registration and tag order;
- doc XML/model round-trip for generic enum parameter metadata where applicable.

### 11.5 LSP/editor tests

- semantic tokens for declaration/reference/shadowing;
- specialized case and enum-function completion;
- hover and signature help;
- document symbols;
- definition/references/rename;
- inheritance-solved cross-file completion;
- formatter fixtures for every declaration form.

### 11.6 Verification commands

Focused iteration uses the agent wrapper and a case matching the implementing child's doctest suite:

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*GenericTaggedUnion*"
```

Before every implementation PR is handed off, run the native strict backend:

```sh
python3 scripts/agent_build.py --test --case "*GenericTaggedUnion*"
```

Before epic closure, run the full strict build and full suite:

```sh
python3 scripts/agent_build.py --compiler-cache ccache --jobs 4 --test
```

The wrapper-provided progress paths are authoritative. On Linux, the full run uses `DISPLAY=:1` so
GUI-dependent subprocess tests do not silently skip. Success requires the final doctest
`Status: SUCCESS!` summary even if cleanup emits the repository's documented leak warnings.

## 12. Epic decomposition

The v1 epic contains nine ordered native subissues. Every child owns a reviewable behavior slice and
must merge with its focused tests green. The epic body is the live checklist; this spec is the
normative design.

### G1. Grammar, AST, parser, formatter, and declaration diagnostics

GitHub: [#1596](https://github.com/cafecito-games/Foundry/issues/1596).

Deliver type-parameter syntax on `enum` and `enum_name`, `EnumNode` storage, formatter support, the
normative `GRAMMAR.md` update, and declaration-shape diagnostics.

Mechanical acceptance:

- positive parser fixtures for single/multiple/bounded/trailing-comma parameters in both forms;
- negative fixtures for unnamed and integer-backed generic enums;
- formatter fixtures round-trip comments and multiline declarations;
- tree printing exposes parameter nodes for parser doctests;
- no analyzer/runtime behavior is claimed beyond preserving the AST;
- focused `*GenericTaggedUnionParser*` doctests pass.

Dependencies: none.

### G2. Enum parameter scope, bounds, and recursive open identity

GitHub: [#1597](https://github.com/cafecito-games/Foundry/issues/1597).

Deliver `TYPE_PARAMETER_ENUM`, lookup/shadowing, enum-bound resolution, open self-specialization, and
finite recursive identity shells.

Mechanical acceptance:

- fixtures prove method > enum > class shadowing;
- bare self means the complete open specialization only inside its declaration;
- a bare external generic union reports exact required arity;
- direct and `Array[...]` recursion resolve without a cyclic-reference error;
- wrong recursive arity reports one declaration-specific error;
- dependent bounds resolve in enum scope;
- C++ tests prove recursive payload metadata does not expand without bound;
- focused `*GenericTaggedUnionScope*` doctests pass.

Dependencies: G1.

### G3. Type/value application, shared bound validation, and invariance

GitHub: [#1598](https://github.com/cafecito-games/Foundry/issues/1598).

Extract declaration-neutral argument binding, add generic enum application in type and expression
positions, and enforce invariant specialization identity.

Mechanical acceptance:

- `Result[int, String]` resolves in annotations and as a constant metatype expression;
- application wins over Dictionary typing/indexing;
- exact too-few/too-many/non-generic/bound diagnostics have fixtures;
- failed arguments retain positions and suppress duplicate bound errors;
- assignments, returns, calls, containers, callables, comparisons, and nullable specializations are
  invariant;
- existing generic class, trait, and method suites remain unchanged and green;
- focused `*GenericTaggedUnionApplication*` doctests pass.

Dependencies: G2.

### G4. Specialized construction, patterns, binds, and flow narrowing

GitHub: [#1599](https://github.com/cafecito-games/Foundry/issues/1599).

Substitute payload maps at specialization, type case values/constructors, and preserve specialization
through match and `is` flow paths.

Mechanical acceptance:

- constructor arity and field diagnostics print concrete types;
- Array/Dictionary literals receive specialized element/key/value types;
- payload-less cases retain specialization;
- match and `is` binds infer concrete field types;
- foreign specialization patterns are rejected before code generation;
- exhaustiveness remains based on the shared case domain;
- recursive payload construction and destructuring run;
- focused `*GenericTaggedUnionCases*` doctests pass.

Dependencies: G3.

### G5. Specialized enum functions and signature surfaces

GitHub: [#1600](https://github.com/cafecito-games/Foundry/issues/1600).

Specialize static/instance/async/generic enum functions and callable references against the receiver.

Mechanical acceptance:

- parameters and returns substitute enum arguments;
- bare self in an enum method retains the receiver specialization;
- generic method parameters shadow enum parameters without rewriting the receiver;
- named arguments, callable references, async return wrapping, and method calls use concrete types;
- wrong-specialization calls fail statically;
- runtime fixtures execute static, instance, async, and generic methods;
- focused `*GenericTaggedUnionMethods*` doctests pass.

Dependencies: G3 and G4.

### G6. Compiler, bytecode, and erased Variant boundaries

GitHub: [#1601](https://github.com/cafecito-games/Foundry/issues/1601).

Preserve static type arguments through lowering/metadata while retaining the existing Array value and
runtime predicates.

Mechanical acceptance:

- case construction emits existing tuple construction and no new opcode;
- specialized payload conversion uses concrete `FSDataType` fields;
- `FSDataType`/`ContainerType`/bytecode round-trips retain nested union arguments;
- scripts loaded from bytecode run construction, methods, match, and binds without parser state;
- a Variant-boundary fixture proves runtime specialization erasure explicitly;
- equality/hash/read-only behavior matches ordinary tagged unions;
- focused `*GenericTaggedUnionBytecode*` doctests pass.

Dependencies: G4 and G5.

### G7. Global `enum_name`, namespaces, dependencies, and serialization integration

GitHub: [#1602](https://github.com/cafecito-games/Foundry/issues/1602).

Complete cross-file declaration resolution and every supported persistence/dependency path.

Mechanical acceptance:

- direct, qualified, imported, and preload fixtures resolve the same declaration identity;
- same-file recursive `enum_name` works with bare self;
- inheritance-solved completion dependencies retain parameter/case metadata;
- two specializations reuse one parser/declaration and do not poison each other's payload maps;
- text/binary/resource/native-JSON value behavior stays the erased Array behavior;
- specialized typed-slot descriptors retain arguments across existing transports;
- name mangling and keep-rule tests remain green;
- focused `*GenericTaggedUnionGlobal*` doctests pass.

Dependencies: G3 through G6.

### G8. LSP, editor, formatter completion, and doc generation

GitHub: [#1603](https://github.com/cafecito-games/Foundry/issues/1603).

Deliver all authoring surfaces against analyzer-owned specialization.

Mechanical acceptance:

- semantic-token tests cover parameters, references, and three-level shadowing;
- completion lists specialized cases and enum functions locally and cross-file;
- hover/signature help display concrete payload and method types;
- pattern binds hover as concrete types;
- definition/references/rename touch only the correct scoped parameter;
- document symbols and generated docs retain the parameter list;
- formatter fixtures from G1 remain the sole canonical spelling;
- focused `*GenericTaggedUnionLSP*` and docgen doctests pass.

Dependencies: G3 through G7.

### G9. Builtin `Result[T, E]`, user docs, and integrated acceptance

GitHub: [#1604](https://github.com/cafecito-games/Foundry/issues/1604).

Ship the builtin, document the complete feature and erasure boundary, and close the epic only after
integrated validation.

Mechanical acceptance:

- builtin `Result` resolves without import/preload;
- `Ok = 0` and `Err = 1` are asserted through runtime behavior/API output, not source text;
- construction, nullable payload, matching, `is` binds, methods from surrounding code, typed
  containers, and bytecode export/load have positive fixtures;
- user docs cover syntax, bounds, invariance, recursion, explicit construction, and erasure;
- no `JsonResult` behavior changes;
- native strict focused validation passes;
- `python3 scripts/agent_build.py --compiler-cache ccache --jobs 4 --test` reaches doctest success;
- every G1-G9 issue is closed and every completion checkbox in the epic is satisfied.

Dependencies: G1 through G8.

## 13. Linked follow-ups outside the v1 epic

These issues are related from the epic but are not native children and do not block v1 closure.

### F1. Runtime-reified generic tagged unions

GitHub: [#1605](https://github.com/cafecito-games/Foundry/issues/1605).

Produce a dedicated design and implementation epic for values that preserve union declaration
identity and type arguments at runtime. The design must decide representation, compatibility with
existing `[tag, payload...]` values, `is`/`as`, dynamic assignment validation, equality/hash,
serialization, JSON, reflection, GDExtension, bytecode versioning, and migration. No representation
change is made under the v1 epic.

### F2. Expected-type-driven contextual case syntax

GitHub: [#1606](https://github.com/cafecito-games/Foundry/issues/1606).

Add `.Case(...)` and payload-less `.Case` when an unambiguous expected tagged-union type is available:

```foundry
var result: Result[int, String] = .Ok(1)
return .Err("bad")
```

The feature applies to generic and non-generic tagged unions, specifies every expected-type context,
and reports ambiguity when no unique union/case can be inferred. It does not add unconstrained
`Result.Ok(1)` constructor inference.

### F3. Evaluate `JsonResult[T]` migration

GitHub: [#1607](https://github.com/cafecito-games/Foundry/issues/1607).

Determine whether JSON decode APIs should return `Result[T, JsonDecodeError]`. Preserve or deliberately
relocate `fail(message, path)`, `nested(error, key)`, nullable-success behavior, source compatibility,
builtin bytecode, and trait/conformance contracts. The issue may conclude that the JSON-specific
wrapper remains preferable; it must record evidence and a migration/no-migration decision.

## 14. Epic completion criteria

The epic closes only when all of the following are mechanically true:

- all nine native children are closed;
- local and `enum_name` generic tagged unions support bounded invariant arguments;
- external raw uses and generic integer enums are rejected;
- recursive bare self-specialization works without infinite metadata expansion;
- construction, payload-less cases, match patterns, `is` binds, narrowing, and enum methods preserve
  concrete arguments;
- compiler and bytecode retain static metadata while runtime values remain read-only Arrays;
- Variant-boundary tests document and prove erased runtime behavior;
- builtin `Result[T, E]` is globally available with stable tag order;
- formatter, LSP/editor, docgen, namespaces/imports/preloads, caching, and persistence paths have
  observable coverage;
- `GRAMMAR.md` and user documentation describe the shipped behavior;
- the native strict full build and full test suite pass;
- F1-F3 exist as linked non-child issues and therefore do not block epic closure.

## 15. Rejected alternatives

### 15.1 Reify arguments in v1

Embedding a descriptor in every union value would make dynamic specialization tests sound, but it
would change the compact representation, equality/hash, serialization, JSON, bytecode, and existing
tagged-union contracts. It is valuable but independently designable, so it is F1 rather than a hidden
expansion of this epic.

### 15.2 Lower generic unions to hidden classes

Hidden generic classes could reuse instance reification, but would add allocation and reference
identity, discard existing Array value semantics, and make generic unions behave differently from
ordinary tagged unions. This contradicts the feature's purpose.

### 15.3 Build a universal generic-declaration hierarchy first

A shared abstraction across classes, traits, methods, and enums could reduce some duplicated control
flow, but it would refactor mature generics before delivering behavior. V1 extracts only argument
resolution/bound validation and retains declaration-specific specialization.

### 15.4 Permit raw or partially applied generic unions

Treating bare `Result` as `Result[Variant, Variant]`, accepting partial argument lists, or adding
wildcard arguments would weaken invariance and complicate inference, reflection, and diagnostics.
Every external use remains complete and explicit.

### 15.5 Infer arguments from `Result.Ok(1)`

There is no expected error type in that expression, so inference would require placeholders,
bidirectional context, or arbitrary defaults. F2 addresses the useful form—`.Ok(1)` under a complete
expected type—without introducing unconstrained inference.
