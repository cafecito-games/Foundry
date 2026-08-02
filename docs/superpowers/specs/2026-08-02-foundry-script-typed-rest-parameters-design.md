# FoundryScript Typed Rest Parameters — Design

Status: approved design, pending implementation.

## 1. Summary

FoundryScript functions may collect trailing arguments with a rest parameter, but the analyzer currently accepts only
an untyped `Array`:

```foundry
func log_all(prefix: String, ...values: Array) -> void:
	pass
```

The parser already accepts `Array[T]` in that position. The analyzer then rejects it with `Typed arrays are currently
not supported for the rest parameter.` This design removes that implementation restriction and makes the element type
part of the function contract:

```foundry
func log_all(prefix: String, ...values: Array[String]) -> void:
	# values is Array[String], including when no trailing values were passed.
	pass

log_all("server", "started", "ready") # valid
log_all("server", 404)                  # static error
```

Concrete typed rest arrays are enforced at direct call sites and through typed `Callable` values. The VM validates each
concrete incoming element and reifies every array element type representable by the existing `ContainerType` model;
nullable and method-dependent element forms retain the same packed-array erasure behavior as ordinary FoundryScript
typed containers. Generic method rest types participate in the existing method type-argument inference.

## 2. Motivation

An untyped rest parameter weakens exactly where a variadic API most needs a contract. The declaration cannot express
that every event is a `String`, every target is a `Node`, or every reducer input is the same generic `T`. Callers lose
diagnostics, completion sees only `Variant`, and dynamic calls can deliver values the implementation never intended to
handle.

Typed variadics are established in TypeScript (`...args: string[]`), Go (`args ...string`), Kotlin
(`vararg args: String`), Java (`String... args`), C# (`params string[] args`), and Swift (`args: String...`). The chosen
FoundryScript spelling follows its existing rule that a function rest parameter is the collected array, rather than
the element-oriented spelling used by custom annotation declarations.

The restriction is not syntactic. It comes from three implementation gaps:

1. function signatures retain fixed parameter types plus `METHOD_FLAG_VARARG`, but no rich rest type;
2. call validation stops after the fixed parameter list; and
3. `FSFunction::call()` always packs trailing values into a plain `Array`.

The feature closes those gaps without extending the engine-wide `MethodInfo` ABI.

## 3. Locked decisions

1. **Function syntax is `...name: Array[T]`.** `...name: T` is not accepted for functions.
2. **The rest annotation describes the collected value.** Inside the body, `name` has exactly the declared `Array[T]`
   type.
3. **Untyped behavior remains available.** `...name`, `...name: Array`, and `...name: Array[Variant]` retain gradual
   behavior. The last spelling is explicit but has no narrower element contract.
4. **Every surplus argument is checked as an element.** Fixed arguments continue to use their fixed parameter types;
   argument index `fixed_count` and later use the rest array's element type.
5. **Empty calls preserve existing container reification.** A reifiable concrete `...values: Array[int]` receives an
   empty `Array[int]`, not an untyped `Array`, when no values are supplied. Element forms the existing
   `FSDataType::to_container_type()` cannot represent remain statically typed and runtime-erased, exactly like ordinary
   typed arrays of those forms.
6. **Runtime call-boundary validation is required for concrete element types.** Reflection, `Callable.call()`,
   `callv()`, and other paths that bypass static proof cannot inject a mismatched value while its compiled `FSDataType`
   remains concrete. Array reification itself follows the existing `ContainerType` representability boundary.
7. **Generic method inference includes every rest argument.** `func collect[T](...values: Array[T])` infers `T` from
   the surplus arguments using the existing generic-method unification and bound rules.
8. **Generic method rest arrays preserve the current erasure boundary.** The analyzer and function body see
   `Array[T]`, and each statically known call is checked after substituting `T`. The VM-created array is untyped when
   its element depends on a method type parameter because method type arguments are not reified in the call frame.
9. **Explicit generic application handles empty calls.** `collect[int]()` is valid; `collect()` reports that `T`
   cannot be inferred when the rest element is its only constraint.
10. **No new least-upper-bound inference is introduced.** Conflicting rest arguments use the same merge/conflict rules
    as repeated fixed occurrences of a method type parameter.
11. **Rest element types are contravariant across callable/override parameter positions.** An implementation must
    accept every trailing argument accepted by the required/base signature.
12. **Typed variadic callable types are explicit.** `Callable[[int, ...Array[String]], void]` represents one fixed
    `int` parameter followed by zero or more `String` values.
13. **Signals remain fixed-arity.** No variadic `Signal` syntax or behavior is added.
14. **`MethodInfo` remains unchanged.** Native and GDExtension varargs remain described by the existing flag and are
    treated as untyped unless a FoundryScript-owned rich signature supplies a rest type.
15. **Bytecode records the compiled rest array type.** The bytecode format version is bumped for the layout change;
    stale bytecode continues to fail through the existing version/build guards.

## 4. Goals and non-goals

### 4.1 Goals

- Accept concrete, nullable, nested, callable, enum, class, and type-handle element types wherever `Array[T]` already
  accepts them.
- Check direct calls, inferred callable calls, explicit callable signatures, `callv`, override signatures, abstract
  requirements, and trait witnesses consistently.
- Infer bounded generic method parameters from all rest arguments and substitute the result into the return type and
  function-body rest type.
- Construct and validate runtime-reifiable concrete typed arrays in the VM with the same conversion rules as fixed
  parameters and typed array insertion.
- Preserve the rest type through FoundryScript analyzer metadata, external signature hints when lossless, compiled
  functions, and bytecode export/load.
- Update completion, signature help, hover, documentation, and generated method descriptions.
- Keep all existing untyped variadic functions source- and behavior-compatible.

### 4.2 Non-goals

- Reifying generic method type arguments in call frames.
- Changing custom annotation declaration syntax (`annotation tags(...names: String)`). Annotation varargs describe
  repeated elements and do not materialize a runtime array.
- Adding variadic signals.
- Adding tuple-shaped heterogeneous rest parameters beyond ordinary fixed parameters plus one homogeneous rest tail.
- Changing native vararg reflection or GDExtension ABI.
- Inferring a common superclass or union solely for mixed generic rest arguments.
- Adding spread-call syntax; this feature concerns declarations and ordinary argument lists.
- Changing named-argument rules. A rest parameter still cannot be supplied by name.

## 5. Source syntax and grammar

### 5.1 Function declarations and lambdas

The existing grammar already admits the final form:

```ebnf
parameter_list  = param_item, { ",", param_item }, [ "," ] ;
param_item      = [ "..." ], parameter_annotation*, parameter ;
parameter       = identifier, [ ":", ( type | (* inferred *) ) ], [ "=", expression ] ;
```

The semantic rules in `GRAMMAR.md` become explicit:

- a function/lambda rest parameter must resolve to `Array` or `Array[T]`;
- the rest parameter is last and has no default;
- `T` is the expected type of each surplus call argument;
- the value bound inside the body is the declared array;
- nullable or method-dependent element forms that existing compiled containers erase remain statically specialized but
  runtime-erased under that same model.

Examples:

```foundry
func ints(...values: Array[int]) -> void:
	pass

var nodes := func(prefix: String, ...values: Array[Node?]) -> int:
	return values.size()
```

These remain invalid:

```foundry
func wrong_element_spelling(...values: int) -> void:       # rest type is not Array
	pass

func not_last(...values: Array[int], suffix: String):      # parser rule unchanged
	pass

func has_default(...values: Array[int] = []):              # parser rule unchanged
	pass
```

### 5.2 Callable signature types

The callable parameter-list grammar gains one optional final rest type:

```ebnf
callable_signature = "[", "[", [ callable_parameter_list ], "]", ",", type, "]" ;
callable_parameter_list = callable_parameter, { ",", callable_parameter }, [ "," ] ;
callable_parameter = type | "...", type ;
```

The rest type must resolve to `Array` or `Array[T]` for symmetry with function declarations:

```foundry
var sink: Callable[[...Array[String]], void]
var indexed_sink: Callable[[int, ...Array[String]], void]
```

The `...` token is not itself a parameter type, may occur at most once, and must be final. `Signal[[...]]` continues to
reject the token.

## 6. Static semantics

### 6.1 Declaration resolution

`resolve_function_signature()` continues to resolve the rest `ParameterNode` as an ordinary local. Its post-resolution
validation changes from rejecting every non-`Variant` element to enforcing only the outer-array rule:

- `Array` and `Array[Variant]`: valid gradual rest arrays;
- `Array[T]`: valid typed rest array when `T` is a legal array element type;
- any non-Array type: the existing `The rest parameter type must be "Array"` error;
- invalid nested/type-handle/nullability details: the existing type resolver's errors, with no duplicate rest error.

The resolved array `DataType` remains on `FunctionNode::rest_parameter`. Consumers must not reconstruct it from
`METHOD_FLAG_VARARG`.

### 6.2 Direct call validation

Function signature lookup returns four independent facts:

1. ordered fixed parameter types;
2. default argument count;
3. optional rest array type; and
4. method flags/return type.

Call validation canonicalizes named fixed arguments first, as it does today. The canonical argument sequence is then
validated as follows:

```text
argument index < fixed_count  -> matching fixed parameter type
argument index >= fixed_count -> rest_array.element_type, when one exists and is narrower than Variant
```

Rest arguments follow the fixed-parameter policy for:

- constant coercion;
- implicit numeric and builtin conversions;
- nullable-to-non-nullable diagnostics;
- class/type-handle compatibility;
- unsafe-call warnings in gradual mode; and
- `strict_dynamic_checks` rejection of an unproven `Variant`.

Diagnostics retain the original one-based call argument number. They identify the function and expected element type,
not `Array[T]`, because the offending value occupies one repeated element slot:

```text
Invalid argument for "collect()" function: argument 3 should be "String" but is "int".
```

### 6.3 Generic method inference

`apply_generic_method_call()` receives the optional rest array type alongside the fixed list. When explicit method type
arguments are absent, it gathers bindings from:

1. each present non-synthesized fixed argument against its fixed parameter; then
2. every remaining argument against the rest element type.

Bindings use the existing `collect_type_parameter_bindings()` merge and conflict set. Bounds are collected from the
fixed parameters, rest array type, and return type before they are checked. After solving, substitution updates the
fixed parameter list, the rest array type, and the return type together.

Examples:

```foundry
func collect[T](...values: Array[T]) -> Array[T]:
	return values

var ints: Array[int] = collect(1, 2, 3)       # T := int
var empty: Array[int] = collect[int]()        # explicit application
collect()                                     # cannot infer T
collect(1, "two")                            # conflicting T
```

A type parameter may also be constrained by both fixed and rest positions:

```foundry
func prepend[T](first: T, ...rest: Array[T]) -> Array[T]:
	return [first] + rest
```

All occurrences participate in one binding. A conflict is reported once through the existing generic-method error
vocabulary.

An unconstrained `Variant` rest argument may legitimately infer `T := Variant`. Strict dynamic rejection applies when
the expected element has already become narrower through an explicit type argument or another fixed/rest occurrence;
it does not reject a genuinely inferred `Variant` specialization.

### 6.4 Named arguments

The named-call canonicalizer already keeps surplus positional values after all fixed slots and rejects the rest
parameter's name. Typed validation runs after that canonicalization. No call may write:

```foundry
collect(rest = [1, 2])
```

Nor does passing an `Array[T]` as one ordinary argument spread it. `collect([1, 2])` supplies one array element and is
valid only when the rest element itself accepts that array.

## 7. Signature representation

### 7.1 Analyzer-owned rich metadata

`FSParser::DataType` already stores rich callable parameter and return types that `MethodInfo` cannot preserve. Add:

```cpp
Vector<DataType> method_rest_parameter_type; // Empty or exactly one Array DataType.
```

A vector is used for the same recursive-type reason as `method_return_type`: `DataType` cannot contain itself directly.
Every copy, substitution, traversal, completion, equality, alpha-equivalence, strict-identity, encodability, and display
path that handles `method_parameter_types` must handle the optional rest slot as well.

`METHOD_FLAG_VARARG` remains the arity bit. Invariants are:

- no vararg flag -> empty rest type;
- vararg flag + empty rich rest type -> external/native/legacy untyped vararg;
- vararg flag + one rest type -> FoundryScript rich variadic signature;
- more than one rest type -> internal/bytecode validation failure.

`make_callable_type(MethodInfo, FunctionNode)` copies `FunctionNode::rest_parameter->get_datatype()` into the rich slot.
Direct function lookup returns the same type without routing it through `PropertyInfo`.

### 7.2 Explicit callable syntax and metadata hints

`TypeNode` gains one optional `signature_rest_parameter_type`. Resolution produces the rich slot and sets
`METHOD_FLAG_VARARG`. String rendering, property-hint encoding, and external hint decoding use:

```text
Callable[[int, ...Array[String]], void]
AsyncCallable[[...Array[Node]], int]
```

Lossless cross-script hints may encode the rich rest type. If any nested rest element is not losslessly encodable, the
entire callable hint follows the current gradual fallback rather than publishing a falsely strict signature.

### 7.3 `MethodInfo` boundary

The compiler continues to place only fixed parameters in `MethodInfo::arguments` and continues setting
`METHOD_FLAG_VARARG`. The rest name and type remain available through the parser, FoundryScript `DataType`, compiled
`FSFunction`, and generated `DocData`, but are not added to `MethodInfo`.

Native varargs and callables reconstructed from MethodInfo-only boundaries therefore remain untyped. This is deliberate:
changing `MethodInfo` would affect core reflection, ClassDB, GDExtension ABI, compatibility hashing, and other script
languages.

## 8. Compatibility and variance

### 8.1 Arity remains the first rule

The current accepted-arity interval remains unchanged:

- a variadic signature has maximum arity `INT_MAX`;
- fixed/default parameters determine minimum arity;
- an override must accept the entire arity interval promised by its base;
- a callable source must be invocable wherever its target type is invocable.

The rest type is compared only after arity compatibility succeeds.

### 8.2 Rest elements are parameter-position contravariant

Although the body sees an `Array[T]`, callers supply individual `T` values. Compatibility therefore compares element
acceptance, not invariant Array assignability:

```foundry
class Base:
	func visit(...nodes: Array[Node]) -> void:
		pass

class Good extends Base:
	func visit(...nodes: Array[Object]) -> void: # accepts every Node
		pass

class Bad extends Base:
	func visit(...nodes: Array[Sprite2D]) -> void: # rejects some Nodes
		pass
```

Rules:

- exact element type is valid;
- a broader implementation element type is valid;
- an untyped implementation rest tail satisfies a typed requirement;
- a typed implementation rest tail cannot satisfy an untyped requirement;
- two untyped rest tails are compatible;
- an absent rest tail never satisfies a required variadic interval;
- `Array[Variant]` is treated as untyped for acceptance.

The same helper is used for class overrides, abstract methods, trait witnesses, inferred/explicit callable assignment,
and signal-to-callable compatibility where the callable has a rest tail.

### 8.3 Generic signatures

Generic method compatibility compares rest types under the existing alpha-renaming rules. For example,
`...values: Array[T]` and `...values: Array[U]` match when `T` and `U` are aligned method parameters with equivalent
bounds. A concrete `Array[int]` does not satisfy an open required `Array[T]` merely because one instantiation would.

## 9. Runtime, compiler, and bytecode

### 9.1 Compiled function metadata

`FSFunction` gains:

```cpp
FSDataType rest_parameter_type;
```

`_vararg_index >= 0` remains the indicator that a rest slot exists. The compiler converts the resolved rest array
`DataType` through `_gdtype_from_datatype()` and stores it on the function while adding the local stack slot. A rest
type whose element depends on a method type parameter follows the existing compiler erasure rule and stores a plain
Array runtime type.

### 9.2 Concrete VM packing

For a runtime-reifiable concrete typed rest array, `FSFunction::call()`:

1. resolves the element `ContainerType` from `rest_parameter_type`;
2. creates an empty `Array` and applies `set_typed(element_type)` before resizing;
3. validates/converts every surplus argument with the same helper used by fixed parameters;
4. writes through `Array::set()` so typed-container validation remains authoritative; and
5. stores the completed array at `_vararg_index` only after all values validate.

On the first mismatch, the call returns `CALL_ERROR_INVALID_ARGUMENT` with the absolute incoming argument index and
expected builtin type. The function body does not execute and no partially filled array escapes.

If `FSDataType::to_container_type()` returns an untyped descriptor, as it currently does for nullable element metadata,
the VM still validates each incoming value against the concrete `FSDataType` but packs it into an untyped Array. The
ordinary typed-container implementation has the same representational boundary. No new VM opcode is needed because
packing happens in the call prologue, not bytecode emitted for the function body.

### 9.3 Generic runtime erasure

For `Array[T]` where `T` is a method type parameter, the analyzer checks each statically known call after inference, but
the VM cannot construct a typed array because `T` is absent from the runtime call frame. It constructs the existing
plain Array. This matches existing generic-method container return erasure.

Consequences are explicit:

- body analysis treats the local as `Array[T]`;
- statically compiled calls reject incompatible rest values;
- explicit `collect[int]()` is statically typed but receives an empty untyped runtime Array inside the generic body;
- a Variant/reflection boundary cannot dynamically distinguish or enforce the erased `T`;
- reifying method type arguments is a separate design and epic.

### 9.4 Bytecode

The function-body wire layout writes `rest_parameter_type` after the fixed `argument_types` and before the return type.
The loader validates:

- a non-vararg function has an unset rest type;
- a vararg function has an Array rest type;
- the rest slot index lies within the stack;
- decoded nested type depth/count limits are honored; and
- generic method-dependent element metadata is erased exactly as source compilation erases it.

`FSBytecodeFormat::FORMAT_VERSION` increments from 7 to 8 in the implementing change. Bytecode round-trip tests compare
the rest type and execute concrete typed-rest calls after parser state and source caches are cleared.

## 10. Tooling and documentation

### 10.1 Editor and LSP

All authoring surfaces use the resolved rest array type rather than synthesizing `...args: Array`:

- argument hints and signature help show the declared name and `Array[T]`;
- the active-parameter range covers all surplus arguments and highlights the rest entry;
- hover and document symbols render typed rest signatures;
- completion inside the function body sees the typed Array and its element-returning methods;
- override/implement-abstract refactors preserve the typed rest declaration;
- external MethodInfo-only varargs continue to display `...args: Array`;
- semantic tokens continue to classify `...`, the parameter name, `Array`, and nested element types through existing
  token rules.

### 10.2 Documentation

`fs_docgen.cpp` already has a dedicated `rest_argument` field. It records the declared Array type and name for
FoundryScript functions. Editor help renders the result without changing core `MethodInfo`.

`GRAMMAR.md` is updated in the same syntax/semantic change. User-facing FoundryScript documentation explains concrete
runtime enforcement, generic inference, callable syntax, override variance, and generic method erasure.

## 11. Diagnostics

The implementation reuses fixed-parameter diagnostics wherever possible. Required dedicated cases are:

| Condition | Diagnostic contract |
| --- | --- |
| Rest declaration is not Array | Existing `The rest parameter type must be "Array", but "X" is specified.` |
| Concrete rest argument mismatch | Existing invalid-argument form with argument number and expected element type |
| Strict Variant rest argument | Existing strict-dynamic invalid-argument form with expected element type |
| Generic rest-only empty call | Existing cannot-infer-method-parameter error with explicit-application suggestion |
| Conflicting generic rest arguments | Existing conflicting-types inference error, once per parameter |
| Callable `...` not last | `The rest parameter type must be the final Callable parameter type.` |
| Callable rest type is not Array | `The Callable rest parameter type must be "Array", but "X" is specified.` |
| Variadic Signal syntax | `Signal signatures cannot declare a rest parameter.` |
| Narrowing override/witness | Existing signature-mismatch error; the required signature renders its typed rest tail |

Removing `Typed arrays are currently not supported for the rest parameter.` is itself covered by the regenerated
analyzer fixture.

## 12. Testing strategy

Tests assert observable parsing, analysis, runtime values, call errors, emitted metadata, or editor protocol output.
They do not inspect implementation source text.

### 12.1 Parser and analyzer fixtures

- concrete builtin, native class, nullable, nested container, callable, enum, and `Type[T]` rest arrays;
- invalid non-Array declarations;
- good/bad direct arguments, zero rest arguments, defaulted fixed prefixes, and named fixed arguments;
- legacy vs strict handling of Variant rest arguments;
- generic inference from one/many rest values, fixed+rest occurrences, bounds, explicit empty application, conflicts,
  and unconstrained empty calls;
- callable syntax placement/type diagnostics;
- exact, broadening, narrowing, typed/untyped overrides and trait witnesses.

### 12.2 Runtime fixtures

- runtime-reifiable concrete empty/non-empty arrays report `is_typed()` and the expected builtin/class element identity;
- nullable rest arrays match ordinary nullable typed-container runtime erasure while remaining statically checked;
- permitted implicit conversions match fixed parameters;
- dynamic `Callable.call()` and `callv()` reject wrong values before body execution;
- untyped rest functions retain current behavior;
- generic rest functions execute successfully and explicitly demonstrate the documented erased runtime Array;
- source and bytecode-loaded functions behave identically for concrete types.

### 12.3 C++ doctests

- rich callable DataType copy/substitute/display/alpha/strict equality;
- callable property-hint encode/decode with nested typed rest arrays;
- compiled function and bytecode rest-type round-trip plus malformed metadata rejection;
- runtime `Callable::CallError` index/expected type for an incompatible surplus argument;
- override and trait compatibility helper truth table.

### 12.4 Tooling tests

- signature help labels and active rest parameter for direct and callable calls;
- hover/document symbol/API output;
- completion inside a typed rest body;
- override and implement-abstract generated signatures;
- docgen `rest_argument` name/type;
- external MethodInfo-only fallback remains untyped.

### 12.5 Verification

Focused iteration:

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypedRestParameter*"
```

Native strict pre-PR validation for each implementation slice:

```sh
python3 scripts/agent_build.py --test --case "*TypedRestParameter*"
```

Epic closeout:

```sh
python3 scripts/agent_build.py --compiler-cache ccache --jobs 4 --test
```

The final doctest `Status: SUCCESS!` is authoritative. On Linux, `DISPLAY=:1` remains required so GUI-dependent
subprocess tests do not silently skip.

## 13. Epic decomposition

The implementation epic contains five native subissues. T1 is a concrete vertical slice. T2, T3, and T4 begin after T1
and may run in parallel. T5 integrates authoring surfaces and owns the full-suite closeout after the three parallel
semantic slices merge.

### T1. Concrete typed-rest core, VM, and bytecode

Deliver rich rest metadata for functions, accept concrete `Array[T]`, validate direct calls, construct concrete typed
arrays in the VM, serialize the compiled rest type, and retain safe exact-type compatibility until T4 broadens it.

Acceptance:

- concrete direct calls accept/reject the correct values;
- empty and populated runtime-reifiable concrete rest locals are genuinely typed at runtime;
- dynamic calls reject mismatches with the correct argument index;
- source and bytecode-loaded functions behave identically;
- existing untyped variadics are unchanged;
- bytecode format is version 8 and malformed rest metadata is rejected;
- focused native strict tests pass.

Dependencies: none.

### T2. Generic typed-rest inference and erasure

Extend method type inference, bounds, substitution, and diagnostics across rest arguments. Document and test the
generic-method runtime erasure boundary.

Acceptance:

- one/many/fixed+rest occurrences infer one binding;
- conflicts, bounds, strict dynamic values, and empty calls diagnose through existing generic rules;
- explicit type arguments permit empty calls;
- the rest local remains `Array[T]` while substituted call return types are concrete;
- runtime fixtures prove correct execution and the deliberate untyped packed Array;
- focused native strict tests pass.

Dependencies: T1.

### T3. Typed variadic Callable signatures and invocation transforms

Add `Callable[[fixed, ...Array[T]], R]`, rich DataType propagation, inferred method references, property-hint
round-trips, and correct `call`/`callv`/`bind`/`bindv` transformations.

Acceptance:

- parser/formatter/type-string round-trips preserve the final rest type;
- misplaced/non-Array rest types and variadic Signals fail exactly;
- typed callable calls and `callv` validate every surplus value;
- bind transformations retain or consume the correct fixed/rest boundary;
- lossy external signatures degrade gradually rather than becoming falsely strict;
- sync/async callable identity and return typing remain intact;
- focused native strict tests pass.

Dependencies: T1.

### T4. Override, abstract, trait, and callable variance

Apply one contravariant rest-element compatibility helper to class overrides, abstract requirements, trait witnesses,
callable assignment, and signal connection checks.

Acceptance:

- exact and broader implementation elements pass;
- narrower implementation elements fail;
- untyped implementations satisfy typed requirements but not vice versa;
- generic signatures compare under alpha-renaming and bounds;
- required signature diagnostics render `...name: Array[T]`;
- native MethodInfo-only varargs stay gradual;
- focused native strict tests pass.

Dependencies: T1.

### T5. Editor/LSP/docgen integration and epic acceptance

Finish signature help, hover, symbols, completion, refactors, editor help, and user documentation after T2-T4 establish
the complete semantic surface. Run the native full-suite gate.

Acceptance:

- all authoring surfaces display and preserve the declared rest name/type;
- active signature-help parameter remains the rest slot for every surplus argument;
- body completion exposes the typed element behavior;
- override/abstract generation keeps `Array[T]`;
- docgen stores the FoundryScript rest argument while native fallback stays `...args: Array`;
- `GRAMMAR.md` and user docs match concrete and generic behavior;
- all T1-T5 issues are closed and the native strict full suite succeeds.

Dependencies: T2, T3, and T4.

## 14. Epic completion criteria

- all five native subissues are closed;
- `...args: Array[T]` works for functions and lambdas with every legal Array element type;
- direct, named-prefix, callable, `callv`, abstract, trait, and override paths agree;
- runtime-reifiable concrete arrays are typed and runtime-enforced, including empty calls and bytecode-loaded scripts;
- nullable typed rest arrays match ordinary typed-container erasure and stay statically enforced;
- generic inference consumes every rest argument and the runtime erasure boundary is mechanically demonstrated;
- explicit typed variadic Callable syntax round-trips through supported metadata boundaries;
- native/GDExtension MethodInfo ABI is unchanged and native varargs remain gradual;
- editor, LSP, refactoring, and doc generation surfaces are covered by observable tests;
- the obsolete analyzer rejection is removed;
- `GRAMMAR.md` and user documentation describe the shipped contract;
- `python3 scripts/agent_build.py --compiler-cache ccache --jobs 4 --test` succeeds.

## 15. Rejected alternatives

### 15.1 Annotate the repeated element as `...args: T`

Custom annotation declarations use this form, but function rest parameters are runtime locals containing an Array. The
chosen `Array[T]` spelling preserves the existing outer-Array rule and makes the local type explicit.

### 15.2 Support both `T` and `Array[T]`

Two spellings for one function contract add ambiguity to diagnostics, formatter output, callable signatures, and
reflection without adding capability.

### 15.3 Extend `MethodInfo`

This would provide universal reflection but changes a core ABI shared by ClassDB, GDExtension, native methods, and
other script languages. FoundryScript already owns a rich callable signature layer specifically for metadata MethodInfo
cannot represent.

### 15.4 Static checking with an untyped concrete runtime array

This permits dynamic invocation to violate the declaration and makes `values.is_typed()` contradict `Array[T]`. It is
not acceptable for concrete types.

### 15.5 Reify generic method type arguments in this epic

Doing so requires a new call-frame ABI, hidden descriptor transport through direct and dynamic callables, binding and
RPC decisions, bytecode changes beyond the rest slot, and a general answer for every generic-method typed container.
That is independently valuable but not necessary to deliver sound concrete typed rest arrays and static generic
inference.
