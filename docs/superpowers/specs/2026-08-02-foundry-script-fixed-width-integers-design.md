# FoundryScript Fixed-Width Integers — Design

Status: approved umbrella design; implementation epics pending.

Scope: redefine `int` as signed 32-bit and add `uint`, `long`, and `ulong`, backed by a new unsigned
Variant carrier and a width-aware FoundryScript numeric type descriptor. Floating-point widths and
source-visible integer widths smaller than 32 bits are follow-ups.

## 1. Motivation

FoundryScript currently exposes Godot's two numeric Variant categories directly: `int` is a signed
64-bit `Variant::INT`, and `float` is a 64-bit `Variant::FLOAT`. Native binding metadata already
distinguishes `int8`/`int16`/`int32`/`int64`, `uint8`/`uint16`/`uint32`/`uint64`, `float`, and
`double`, but the analyzer collapses those distinctions to `INT` or `FLOAT`.

That erasure prevents the language from expressing common native integer contracts and from safely
representing the upper half of `uint64_t`. Encoding a large unsigned value in the signed carrier
would make it appear negative to serialization, hashing, ordering, reflection, and untyped engine
APIs. The language therefore needs both a real unsigned runtime carrier and a static width layer.

The intended source model is:

| Type | Range | Runtime carrier |
|---|---:|---|
| `int` | -2^31 through 2^31 - 1 | `Variant::INT` |
| `uint` | 0 through 2^32 - 1 | `Variant::UINT` |
| `long` | -2^63 through 2^63 - 1 | `Variant::INT` |
| `ulong` | 0 through 2^64 - 1 | `Variant::UINT` |

Breaking the current 64-bit meaning of `int` is accepted. The feature favors checked, explicit
numeric behavior over preserving that source compatibility.

## 2. Locked decisions

1. **Add one runtime type, not four.** `Variant::UINT` stores a `uint64_t`. Width is a
   FoundryScript type property. `int`/`long` share `INT`; `uint`/`ulong` share `UINT`.
2. **Width erases at an untyped Variant boundary; signedness does not.** An untyped `INT` has the
   full `long` range and an untyped `UINT` has the full `ulong` range. Entering a narrower typed
   slot checks the value's range.
3. **Arithmetic is checked by default.** Constant overflow is an analyzer error. Runtime overflow,
   division by zero, invalid shifts, and invalid casts are script runtime errors. The destination of
   a failed compound operation is not modified.
4. **Implicit promotion is value-preserving.** The language never silently reinterprets a sign bit
   or narrows a range. Mixed types with no safe common type require an explicit cast.
5. **Integer suffixes are uppercase-only.** The accepted suffixes are `U`, `L`, and `UL`, in that
   order. Lowercase, mixed-case, and `LU` forms are rejected with a canonical-spelling diagnostic.
6. **Typed containers preserve width.** `Array[int]` and `Array[long]` are distinct runtime typed
   containers even though their elements use the same Variant carrier. The same applies to
   `uint`/`ulong` and Dictionary key/value types.
7. **Native metadata is authoritative.** Exact C++ integer metadata controls Foundry signatures and
   checked native-call validation. Missing signed metadata defaults to `long`; missing unsigned
   metadata defaults to `ulong`.
8. **The public v1 surface is exactly four integer types.** `float`/`double` and source-visible
   `byte`/small integer types are separate designs built on the same numeric descriptor.
9. **No implicit bit-pattern literals.** Hexadecimal and binary literals denote non-negative
   mathematical values unless they carry an explicit leading minus. Suffixes select a type; they do
   not reinterpret bits.
10. **The feature is released as a complete matrix.** Public syntax does not ship before runtime,
    persistence, native boundaries, typed containers, and core tooling all support it.

## 3. Runtime representation

### 3.1 `Variant::UINT`

Append `UINT` immediately before `VARIANT_MAX` rather than inserting it beside `INT`. Appending
preserves every existing `Variant::Type` numeric value. The ordering in the enum is less important
than maintaining compatibility with generated extension interfaces and stored type IDs.

The Variant data union gains a `uint64_t _uint` member. This does not increase the union's storage
requirement or Variant's alignment. All unsigned C++ integer constructors and accessors produce or
consume `UINT`; special nominal wrappers such as object IDs retain their explicitly defined binding
behavior rather than changing merely because their C++ storage is unsigned.

The core Variant audit covers every type-indexed table and switch, including:

- Lifecycle tables and `VariantInternal` initialization/access.
- Constructors, conversions, built-in methods, members, and utility functions.
- Operators and validated evaluator registration.
- Exact comparison, ordering, hashing, truthiness, duplication, and string conversion.
- Variant text parsing/writing and binary marshaling.
- RPC/network encodings, debugger transport, and extension ABI mappings.
- Editor property handling and generated class/extension API descriptions.

`Variant::get_type_name(Variant::UINT)` returns `uint`. This is the carrier name, not a promise that
an untyped value retains 32-bit width.

### 3.2 Core signed/unsigned behavior

An `INT` and `UINT` with the same non-negative mathematical value compare equal. Ordering compares
mathematical values without first converting either side, so negative signed values sort before all
unsigned values and large unsigned values remain greater than `LONG_MAX`. Equal cross-carrier values
must hash identically so Dictionary lookup obeys the equality/hash contract.

Core explicit construction between `INT` and `UINT` is checked:

- `INT -> UINT` fails for a negative value.
- `UINT -> INT` fails above `LONG_MAX`.

Type-level convertibility may report that a checked conversion exists, but the value conversion must
still return failure rather than wrap. FoundryScript does not use ordinary validated Variant integer
operators for statically typed arithmetic because their function signature cannot report checked
overflow.

The general engine's dynamic UINT operator surface must be complete enough for Variant containers and
engine APIs. Core `UINT`/`UINT` arithmetic and shifts use defined modulo-2^64 unsigned behavior, while
mixed `INT`/`UINT` arithmetic is not registered and reports an invalid operator. Exact mixed-carrier
equality and ordering remain available. Foundry's VM applies the stricter checked language rules in
section 6 before any operation could reach the wrapping core UINT evaluator.

### 3.3 Width erasure

An untyped `Variant` records only `INT` or `UINT`. Consequently:

- `typeof(value)` reports `TYPE_INT` or new `TYPE_UINT`, never `TYPE_LONG`/`TYPE_ULONG`.
- Generic Variant reflection displays the carrier name.
- An untyped `INT` is analyzed as `long`; an untyped `UINT` is analyzed as `ulong`.
- Scalar-specific static extensions distinguish all four types when the expression has a rich
  Foundry type. Dynamic extension lookup sees the carrier-wide `long` or `ulong` surface.

Runtime `is` tests are range refinements:

- `value is int`: carrier is `INT` and value fits signed 32-bit.
- `value is long`: carrier is `INT`.
- `value is uint`: carrier is `UINT` and value fits unsigned 32-bit.
- `value is ulong`: carrier is `UINT`.

This overlap is intentional: the narrow types are refinements of their wide carrier types after
static width information has been erased.

## 4. Numeric type descriptor

FoundryScript can no longer derive source built-ins one-to-one from `Variant::Type`. Introduce a
serializable numeric descriptor with `NONE`, `INT8`, `UINT8`, `INT16`, `UINT16`, `INT32`, `UINT32`,
`INT64`, and `UINT64`. The 8-bit and 16-bit values are initially internal native-signature
constraints, not source-visible types. The descriptor is independent of `DataType::Kind`: all four
public integer types remain `BUILTIN` and carry `INT` or `UINT` plus the descriptor.

The descriptor is carried through:

- Parser/analyzer `FSParser::DataType`.
- Runtime/compiler `FSDataType`.
- Function parameters, returns, members, signals, and rich Callable signatures.
- Generic arguments and substitutions.
- `ContainerType` and `ContainerTypeValidate`, including nested element and type-argument types.
- Compiled script metadata, reflection descriptors, and bytecode.
- LSP/documentation type renderers and editor refactoring models.

It participates in type equality, invariance, substitution, signature comparison, and string
rendering. Every hand-written copy/assignment path for these structures must copy it.

The descriptor is deliberately a numeric seam rather than an integer-only special case. Follow-up
designs append floating and smaller-width descriptors and reuse the same propagation, serialization,
and checked-boundary infrastructure.

### 4.1 Built-in resolution

Replace `FSParser::get_builtin_type()`'s generated one-name-per-Variant map with an explicit source
registry returning both carrier and numeric descriptor. The registry includes:

- `int` -> (`INT`, `INT32`)
- `uint` -> (`UINT`, `UINT32`)
- `long` -> (`INT`, `INT64`)
- `ulong` -> (`UINT`, `UINT64`)

Non-numeric Variant built-ins retain their existing mapping. Constructors and meta-type lookup must
use this richer registry; looking only at `Variant::Type` cannot distinguish `int()` from `long()`.

### 4.2 Property and signature boundaries

Foundry's rich compiled metadata is the authoritative channel for Foundry-to-Foundry properties and
signatures. A plain `PropertyInfo` remains carrier-only: `int` and `long` both encode as `INT`, while
`uint` and `ulong` encode as `UINT`. Decoding a genuinely generic PropertyInfo therefore yields the
wide carrier type, consistent with untyped width erasure.

Cross-script analysis must prefer the compiled rich member/signature metadata rather than rebuilding
a Foundry declaration from a lossy PropertyInfo. MethodInfo's existing integer metadata remains the
native signature channel. Native properties without a direct metadata slot recover width from a
consistent getter return/setter argument when available and otherwise use the wide default.

This avoids adding a new member to `PropertyInfo` or overloading its single editor hint, while still
preserving width across every typed Foundry boundary.

## 5. Literal syntax and inference

The grammar becomes:

```ebnf
integer         = integer_body, [ integer_suffix ] ;
integer_body    = int_dec | int_hex | int_bin ;
integer_suffix  = "U" | "L" | "UL" ;
```

Suffixes apply equally to decimal, hexadecimal, and binary literals:

```foundry
42       # int
42U      # uint
42L      # long
42UL     # ulong
0xFFFFU
0b1010UL
```

Rules:

- An unsuffixed integer is `int` when representable, otherwise `long`.
- An unsuffixed positive value above `LONG_MAX` is an error suggesting `UL`.
- `U`, `L`, and `UL` select exactly `uint`, `long`, and `ulong`; they do not choose a wider type on
  overflow.
- A negative unsigned literal is an error. Unary negation of an unsigned expression is also an error.
- `-2147483648` is a valid `int`; `-2147483649` infers `long`.
- A contextual integer constant may convert to any integer type that represents its value, so
  `var count: uint = 42` is valid without a suffix.
- Lowercase/mixed suffixes and `LU` are consumed as one invalid suffix and diagnosed with the exact
  uppercase canonical replacement.
- A suffix remains forbidden on tuple indices after `.`, preserving the grammar's existing tuple
  index disambiguation.

The tokenizer must parse magnitude without first forcing it through `int64_t`; `UINT64_MAX` must be
accepted for `UL`. The token carries a Variant carrier value plus its numeric descriptor. Numeric
overflow is diagnosed before constructing the literal Variant.

The existing rules for separators, decimal points, exponents, base prefixes, and letters following
numbers remain. `modules/foundry_script/GRAMMAR.md` is normative and changes in the same syntax PR.

Variant text serialization has no static width to preserve, so a `UINT` writes the wide carrier form,
for example `123UL`. Source formatting uses the AST's numeric descriptor and preserves the selected
source type.

## 6. Conversions, promotion, and operations

### 6.1 Implicit conversions

The value-preserving implicit conversions are:

- `int -> long`
- `uint -> long`
- `uint -> ulong`

All other integer assignments require an explicit cast, including `int -> uint`, `int -> ulong`,
`long -> uint`, `long -> ulong`, `ulong -> long`, and every width-narrowing conversion. A constant
may cross signedness or narrow when its value is representable in the destination.

Binary promotion is:

| Operands | Common result |
|---|---|
| `int`, `int` | `int` |
| `uint`, `uint` | `uint` |
| `long`, `long` | `long` |
| `ulong`, `ulong` | `ulong` |
| `int`, `long` | `long` |
| `int`, `uint` | `long` |
| `uint`, `long` | `long` |
| `uint`, `ulong` | `ulong` |

All other signed/unsigned pairs have no safe common v1 integer type and are analyzer errors. The
table applies symmetrically and to arithmetic, bitwise operations where meaningful, ordering, and
numeric helpers that select a common type.

The existing `float` remains a 64-bit double in this design. `int` and `uint` may promote to `float`
because every 32-bit integer is exactly representable. `long` and `ulong` require an explicit float
cast unless a constant is exactly representable, preventing silent precision loss above 2^53. A
future `float`/`double` design may rename or refine the floating side without changing the integer
descriptor model.

### 6.2 Explicit casts

`int(value)`, `uint(value)`, `long(value)`, and `ulong(value)` are checked conversions. For integer
sources, the mathematical value must fit the destination. For floating sources, an explicit cast
keeps the existing truncation-toward-zero behavior but rejects non-finite and out-of-range values.

`as` follows the same checked rules when used with an integer destination. A failed runtime cast is a
script runtime error, not a wrapped result or sentinel value.

### 6.3 Checked operations

Checked behavior applies to:

- Addition, subtraction, multiplication, and exponentiation.
- Signed negation and absolute value, including each signed minimum.
- Division and remainder, including zero divisors and signed minimum divided by `-1`.
- Compound assignments and increment/decrement facilities if present.
- Left shifts whose result is not representable in the left operand's result type.
- Shift counts that are negative or greater than or equal to the result width.

Same-type operations retain their type and check that type's range. They do not silently widen after
overflow. Signed right shift is arithmetic; unsigned right shift is logical. Unary `~` operates at
the type's declared width. Unary `-` is invalid for unsigned operands.

Bitwise wrapping APIs are not part of this initial feature. A follow-up may add explicitly named
wrapping operations after the checked model is complete; there is no unchecked language mode in v1.

### 6.4 Dynamic operations

The VM's dynamic numeric path discovers only the wide carriers. `INT`/`INT` is checked as `long` and
`UINT`/`UINT` as `ulong`. Mixed `INT`/`UINT` arithmetic has no universally safe common 64-bit type and
raises a runtime type error requiring an explicit cast. Equality and ordering use the exact
cross-carrier comparison from section 3.2 because they do not need a result carrier.

## 7. Compiler and VM design

Implement one shared checked-numeric layer used by constant folding, VM execution, explicit casts,
and typed boundary validation. It accepts operand descriptors and carrier values, computes through
overflow-safe primitives (compiler builtins or equivalent unsigned-domain checks), and reports a
structured failure reason. Constant and runtime evaluation must therefore agree by construction.

The analyzer applies promotion before code generation and records the exact result descriptor. The
compiler emits a scalar-aware checked operation for statically numeric operands rather than storing a
plain `Variant::ValidatedOperatorEvaluator`. The operation either writes a fully validated result or
routes through the existing VM runtime-error path. A compound assignment computes into a temporary
and commits only after success.

Checked scalar conversion is also applied at:

- Local/member assignment.
- Function arguments and returns.
- Signal emission and Callable invocation.
- Typed Array/Dictionary insertion and replacement.
- Generic/reified type validation.
- Native method calls and return adaptation.

The bytecode verifier validates scalar descriptor values and scalar-aware opcode operands. Compiled
functions serialize descriptors for parameters, returns, members, container elements, generic
arguments, and any scalar operation metadata. This is a wire-layout change and increments
`FSBytecodeFormat::FORMAT_VERSION`.

## 8. Typed containers and generic types

Add the numeric descriptor to both `ContainerType` and `ContainerTypeValidate`. It participates in
equality, names, descriptors, recursive element metadata, reified generic arguments, and bytecode
serialization.

A typed container validates both carrier and range:

- `Array[int]` accepts `INT` values within signed 32-bit.
- `Array[long]` accepts any `INT`.
- `Array[uint]` accepts `UINT` values within unsigned 32-bit.
- `Array[ulong]` accepts any `UINT`.

Typed insertion does not implicitly cross signedness at runtime. A positive `INT` held in Variant is
not silently inserted into `Array[uint]`; the caller must use `uint(value)`. Compile-time constants
are converted to their destination carrier before insertion.

Reads recover the container's declared descriptor, so width does not erase merely because each
element is physically a Variant. Nested containers and generic arguments recurse through the same
rules. `Array[int]` is invariant with `Array[long]`, matching existing typed-container invariance.

## 9. Native API and reflection

Native method metadata maps as follows:

| Native metadata | Foundry type |
|---|---|
| `int32` | `int` |
| `uint32` | `uint` |
| `int64` | `long` |
| `uint64` | `ulong` |

The existing `int8`/`int16` and `uint8`/`uint16` metadata retain their exact range internally for
call validation. Until public small-width types land, tooling renders them as `int` and `uint`
respectively while diagnostics can state the narrower accepted range. Passing a statically broader
value requires either a proven constant or a runtime checked native-boundary conversion.

Method binding metadata needed for source compilation must be available in every build configuration
that can compile Foundry source, not only editor/debug API dumping. Unsigned native returns construct
`Variant::UINT`; signed returns construct `Variant::INT`.

Native properties infer a numeric descriptor from matching getter/setter MethodBind metadata. A
conflict is an engine registration error; missing information uses the wide carrier default. Foundry
properties and methods expose their exact type through `FSReflection` descriptors even when generic
Object/PropertyInfo reflection sees only the carrier.

Integer-backed Foundry enums retain their existing signed 64-bit runtime representation in this
feature. Enum constants convert to the four scalar types only through the existing enum conversion
rules plus the new range checks. Changing enum backing types or adding explicit enum underlying-type
syntax is a separate language design.

## 10. Persistence and external boundaries

### 10.1 Variant formats

Add a UINT case to every binary Variant encoding and version or capability marker that distinguishes
wire layouts. Existing signed payloads remain readable. A consumer that does not support UINT must
fail with an unsupported-type/version error; it must not reinterpret the payload as signed.

Text Variant/resource syntax writes unsigned carrier values with `UL` and parses all three source
suffixes. Scenes, resources, project settings, clipboard/property serialization, and command-line
Variant parsing must round-trip `UINT64_MAX` exactly.

RPC and multiplayer Variant encoding gains the same UINT tag. Peers with incompatible protocol
support fail negotiation or decoding clearly. Network compatibility is not inferred merely because
the enum value was appended.

### 10.2 JSON

JSON has no signedness or fixed-width integer type, and common consumers cannot exactly represent all
64-bit integers. Untyped JSON is therefore an explicitly lossy boundary: it does not promise to
round-trip a numeric descriptor or all `long`/`ulong` values through external systems.

Schema-driven Foundry JSON decoding uses the destination descriptor and performs the same checked
range conversion as other typed boundaries, but it cannot recover digits that an underlying JSON
number parser has already rounded. A parsed value that cannot prove an exact in-range integer fails
rather than approximating the destination. Documentation must warn when a value exceeds the exact
integer range of common JSON number implementations. This feature does not introduce a string-based
JSON bigint convention.

### 10.3 Extension ABI

Append `FOUNDRY_EXTENSION_VARIANT_TYPE_UINT` after every existing public Variant type and advance the
corresponding maximum. Regenerate the extension interface/API dump and add constructor, accessor,
pointer-call, and operator coverage. Existing enum numeric values stay fixed, but extensions must
declare compatibility with an interface version that knows UINT before exchanging it.

## 11. Tooling and editor behavior

Update all source and semantic surfaces:

- `GRAMMAR.md`, tokenizer versioning, tokenizer-buffer round trips, and parser diagnostics.
- Formatter canonicalization and format fixtures.
- TextMate/highlighter numeric patterns and built-in type lists.
- LSP completion, hover, signature help, semantic tokens, diagnostics, and refactoring type models.
- Documentation generation, XML type names, reflection descriptors, and API search.
- Disassembler/debugger value rendering, watches, and remote inspection.
- Export annotations and validation, including range hints.

The inspector needs an exact integer editing control for `long` and `ulong`. A double-backed spin
control loses integer precision above 2^53. The control may provide stepping within an exactly
representable local range, but text entry, display, copy/paste, undo, and serialization must retain
all decimal digits through `UINT64_MAX`. `uint` uses the same control model for consistency even
though its entire range fits exactly in a double.

Substantial inspector work requires end-to-end editor automation and a review gallery demonstrating
minimum, maximum, invalid, and undo/redo states.

## 12. Diagnostics and migration

Diagnostics identify the source and destination types, the offending value when known, and the
valid destination range. Important cases include:

- Existing 64-bit expressions assigned to newly 32-bit `int`, suggesting `long`.
- Unsuffixed literals above `LONG_MAX`, suggesting `UL`.
- Invalid suffix case/order with the canonical `U`, `L`, or `UL` spelling.
- Negative unsigned literals and unary negation on unsigned values.
- Mixed signed/unsigned operations with no safe common type.
- Constant and runtime overflow, invalid shifts, and division failures.
- Native parameters with smaller hidden widths.
- Lossy `long`/`ulong` to `float` operations requiring explicit conversion.

Unannotated large literals infer `long`, which preserves many existing scripts. Annotated `int`
members, parameters, returns, containers, and casts intentionally change to 32-bit and must migrate
to `long` when they need the old range. Migration documentation includes mechanical search targets
and before/after examples, but there is no silent compatibility alias for old `int`.

## 13. Delivery phases

This work is too broad for one implementation issue. It is delivered as an epic with these ordered
phases:

1. **Unsigned engine carrier.** Add `Variant::UINT`, exact core semantics, persistence, extension ABI,
   and core tests. Do not expose Foundry source syntax yet.
2. **Numeric type infrastructure.** Add the numeric descriptor throughout parser/runtime types,
   signatures, containers, generics, reflection, and bytecode while preserving current source
   behavior.
3. **Language semantics.** Redefine `int`; add `uint`, `long`, `ulong`, literal suffixes, promotions,
   checked operations/casts, diagnostics, and the normative grammar changes.
4. **Tooling and integration.** Complete native metadata mapping, editor/LSP/docgen/formatter,
   debugger, inspector, export, RPC, and persistence audits.
5. **Migration and follow-ups.** Publish migration guidance, then separately design floating widths,
   smaller integer widths, and explicit wrapping APIs.

Each foundation phase may land internally if it does not advertise incomplete syntax. The public
language change becomes available only once phases 1 through 4 pass the integrated matrix.

## 14. Verification

Tests assert observable behavior rather than source text. Required coverage includes:

### 14.1 Core Variant

- Construct/access zero, boundaries, `LONG_MAX + 1`, and `UINT64_MAX`.
- INT/UINT exact equality and ordering across negative, equal, and upper-range values.
- Equal cross-carrier values have equal hashes and work interchangeably as Dictionary keys.
- Checked conversions reject negative-to-unsigned and too-large-to-signed values.
- Text and binary Variant round trips preserve UINT identity and full magnitude.
- Extension ABI construction, pointer calls, API dumps, and incompatible-version rejection.

### 14.2 Tokenizer, parser, and analyzer

- Decimal/hex/binary literals for all suffixes and boundaries.
- Invalid case/order, separators adjacent to suffixes, negative unsigned values, and tuple-index
  disambiguation.
- The complete pairwise promotion and rejection matrix for all four types.
- Constant overflow, casts, shifts, division/remainder, unary operations, and float interaction.
- Migration diagnostics for old 64-bit `int` usage.

### 14.3 Runtime and compiled bytecode

- Every minimum/maximum value plus one-step overflow for each checked operation.
- Constant-folded and runtime-evaluated expressions produce identical values or failure categories.
- Compound assignments do not modify their destination after failure.
- Parameters, returns, members, signals, callables, typed containers, generics, traits, and dynamic
  Variant crossings retain or erase width exactly where specified.
- Source and exported-bytecode execution agree; malformed scalar descriptors fail verification.
- Bytecode export/load round trips nested numeric descriptors.

### 14.4 Integration and tools

- Native metadata for all eight existing integer widths, including checked small-width calls.
- Scene/resource/project-setting, RPC, debugger, and reflection round trips.
- Formatter, completion, hover, signature help, semantic tokens, docs, and refactoring output.
- Automated inspector entry and persistence of `LONG_MIN`, `LONG_MAX`, zero, and `UINT64_MAX`, plus
  invalid input and undo/redo behavior.

Boundary/property-style C++ tests calculate expected results with overflow-safe wider arithmetic or
compiler overflow intrinsics. Final validation uses the repository's agent build wrapper and the full
headless test suite, including GUI-dependent editor automation under the configured display.

## 15. Follow-up designs

The following are intentionally outside this implementation epic but reuse its numeric descriptor:

1. `float`/`double` source widths and their literal suffixes, promotion rules, and precision checks.
2. Source-visible 8-bit and 16-bit signed/unsigned types and naming.
3. Explicit wrapping arithmetic and bit-pattern conversion APIs.
4. Explicit enum backing types if Foundry enums need width/signedness control.

None of these follow-ups is required to define or safely ship the four-type integer model.
