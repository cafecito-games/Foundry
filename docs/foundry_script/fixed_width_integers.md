# Fixed-width integers in Foundry Script

Foundry Script has four integer types. Each one names an exact range, and every operation that could
leave that range is refused rather than wrapped.

| Type | Range | Runtime carrier | `typeof()` |
|---|---|---|---|
| `int` | -2147483648 through 2147483647 | `Variant::INT` | `TYPE_INT` (2) |
| `uint` | 0 through 4294967295 | `Variant::UINT` | `TYPE_UINT` (39) |
| `long` | -9223372036854775808 through 9223372036854775807 | `Variant::INT` | `TYPE_INT` (2) |
| `ulong` | 0 through 18446744073709551615 | `Variant::UINT` | `TYPE_UINT` (39) |

These four names are the entire integer surface. There is no `byte`, `short`, `int32`, or any other
spelling, and `float` remains a 64-bit double.

## Carriers and width erasure

There are two runtime carriers, not four. `int` and `long` both travel in `Variant::INT`; `uint` and
`ulong` both travel in `Variant::UINT`. Width is a static property that the analyzer, compiled
metadata, typed containers, and reflection descriptors carry; signedness is a runtime property that
the value itself carries.

The consequence is that **width erases at an untyped boundary and signedness does not**. A value
stored in an untyped `Variant` and read back is analyzed as `long` or `ulong` — the widest type its
carrier can hold — and `Variant.get_type_name()` reports `int` or `uint` for the carrier, never
`long` or `ulong`.

Runtime `is` tests are therefore range refinements over the carrier:

```foundry
value is int    # carrier is INT   (see below: not yet range-checked)
value is long   # carrier is INT
value is uint   # carrier is UINT  and the value fits unsigned 32-bit
value is ulong  # carrier is UINT
```

`value is long` and `value is int` can both be true for the same value. That overlap is intentional:
after width has erased, the narrow type is a refinement of its carrier's wide type. `is uint`
performs that refinement today; `is int` does not yet, because `int` applies no width constraint
internally ([#1684]), so `is int` currently answers for the whole signed carrier.

## Literals and suffixes

```ebnf
integer        = integer_body, [ integer_suffix ] ;
integer_body   = int_dec | int_hex | int_bin ;
integer_suffix = "U" | "L" | "UL" ;
```

The suffixes are **uppercase only**, ordered `U` before `L`, and apply equally to decimal,
hexadecimal, binary, and underscore-separated literals.

```foundry
42        # int
42U       # uint
42L       # long
42UL      # ulong
0xFFFFU   # uint
0b1010UL  # ulong
1_000L    # long
```

- An unsuffixed integer is `int` when its value fits `int`, and `long` otherwise.
- A suffix selects its type outright. It never widens on overflow and never reinterprets bits, so a
  literal outside the suffixed type's range is an error.
- An unsuffixed literal above the `long` range is an error naming the `UL` suffix.
- A negative literal with `U` or `UL` is an error, and so is unary `-` on any unsigned expression.
- `1u`, `1l`, `1uL`, `1Ul`, and `1LU` are errors that name the exact canonical replacement.
- A suffix is not allowed on a float literal or on a tuple index (`pair.0` is an index, never `0U`).

`modules/foundry_script/GRAMMAR.md` section 2.6.1 is the normative statement of this syntax.

## Inference, promotion, and assignment

The value-preserving implicit conversions are:

- `int` -> `long`
- `uint` -> `ulong`
- `int` -> `float`

Every other integer assignment of a non-constant value needs an explicit conversion, including
`int` -> `uint`, `uint` -> `long`, `long` -> `uint`, `ulong` -> `long`, and all narrowing. A
**constant** may additionally narrow within its own carrier when its exact value fits the
destination, and a signed constant of either width reaches `float` when it is exactly representable:

```foundry
var narrowed: uint = 4000000000UL   # accepted: the value fits uint
var too_large: uint = 5000000000UL  # error: the value does not fit uint
var wide: ulong = 4UL
var narrowed_value: uint = wide     # error: a non-constant never narrows implicitly

var exact: float = 4000000000L      # accepted: the constant is exactly representable
var lossy: float = wide             # error: a non-constant long/ulong needs an explicit cast
```

Binary operands promote to a common type:

| Operands | Common result |
|---|---|
| `int`, `int` | `int` |
| `uint`, `uint` | `uint` |
| `long`, `long` | `long` |
| `ulong`, `ulong` | `ulong` |
| `int`, `long` | `long` |
| `uint`, `ulong` | `ulong` |

Every other pair is an error, because no integer type holds every value of both operands:

```
The "+" operator cannot mix "uint" and "long" operands. Convert both to "long" explicitly.
No integer type holds every value of both "long" and "ulong", so the "+" operator has no
common type here. Convert one operand explicitly.
```

The same rule governs every typed boundary — arguments, returns, members, signal parameters, tuple
and enum-payload fields, typed containers, and generic arguments — so a width declared in another
file is enforced exactly like one declared inline.

## Checked operations

Arithmetic is checked at the operation's result type, not at its carrier. A result that does not fit
that type is refused; it is never wrapped and never silently widened.

Checked operations are addition, subtraction, multiplication, exponentiation, division, remainder,
negation, absolute value, and shifts. A constant expression is refused by the analyzer with the same
message the runtime would produce, because both go through one shared implementation.

```
The "+" operator overflows "uint": the result is outside its range 0 to 4294967295.
The "/" operator on "uint" has a divisor of zero.
Invalid shift count for the "<<" operator on "uint": it must be from 0 to 31.
The "unary-" operator overflows "long": the result is outside its range ... .
Invalid operand of type "uint" for unary operator "unary-".
```

Signed right shift is arithmetic and unsigned right shift is logical. Unary `~` complements at the
type's declared width, so `~0U` is `4294967295`. A compound assignment computes into a temporary and
commits only on success, so a failed operation never partially mutates its destination.

There is no unchecked or wrapping mode. Explicitly named wrapping operations are a separate,
unscheduled design.

## Explicit conversions

`as` is the checked conversion:

```foundry
var wide: ulong = 5UL
var narrow: long = wide as long        # checked: fails above long's range
print(3.9 as int)                      # -3.9 as int is -3; truncation is toward zero
```

An integer source must be mathematically representable in the destination. A floating source
truncates toward zero and is refused when it is not finite or when the truncated value is out of
range:

```
Cannot convert 1000000000000000019884624838656.0 to "long": the value is outside its range
-9223372036854775808 to 9223372036854775807.
Cannot convert nan to "long": it is not a finite number.
```

A cast whose operands are constants is decided at analysis time; otherwise it is a script runtime
error that unwinds normally rather than producing a sentinel value.

### Bit reinterpretation with `as!`

`as!` is the unchecked counterpart of `as`, for the one case a checked conversion cannot express: a
lossless signed/unsigned reinterpretation of the *same* bit pattern. It is restricted to a crossing
between the source-nameable integer types of equal width — `int` ↔ `uint` and `long` ↔ `ulong` — and
copies the raw N-bit pattern onto the target carrier instead of checking the value's magnitude.

```foundry
var wire: uint = 0xFFFFFFFFU
var signed := wire as! int        # -1, the same 32 bits read as signed
var back := signed as! uint       # 0xFFFFFFFF again, round-trips losslessly
```

Every other use is an error, so `as!` cannot become a general escape hatch. The target must be `int`,
`uint`, `long`, or `ulong`; the operand must be an integer; and the operand width must equal the
target width, so a genuine narrowing such as `some_ulong as! uint` still fails. An unpinned integer
(an unsuffixed literal, or a value whose width was never declared) counts as 64-bit, so it
reinterprets only into a 64-bit target; narrow it explicitly first (`(-1 as int) as! uint`) to reach
a 32-bit target. Use `as` for any width change. Constant folding, the runtime, and the analyzer all
produce the identical pattern.

Because a signed left shift is a checked multiply-by-2ⁿ, it overflows before it can set the sign bit:
`(255 as long) << 56` is refused as out of range. Assemble the pattern on the unsigned carrier, where
the shift is well defined, and then reinterpret it:

```foundry
var high := (255 as ulong) << 56UL as! long   # 0xFF00000000000000 read back as a negative long
```

> **Known limitation.** `int(value)` still resolves to the legacy Variant constructor rather than to a
> checked width conversion, and `uint()`, `long()`, and `ulong()` do not exist as conversion calls at
> all. Use `as` for every width or signedness change. See "Known limitations" below.

## Native boundaries

Native C++ integer metadata is authoritative for the Foundry type of a native parameter, return, or
property:

| Native metadata | Foundry type |
|---|---|
| `int32_t` | `int` |
| `uint32_t` | `uint` |
| `int64_t` | `long` |
| `uint64_t` | `ulong` |
| none declared, signed carrier | `long` |
| none declared, unsigned carrier | `ulong` |

`int8_t`, `int16_t`, `uint8_t`, and `uint16_t` keep their exact range internally for call validation
even though no source spelling names them yet.

A native property recovers its width from its getter/setter metadata, so
`RandomNumberGenerator.seed` is a `ulong` because its accessors take and return `uint64_t`. A nominal
wrapper that deliberately binds a signed carrier keeps it: `Object.get_instance_id()` stays a `long`
despite `ObjectID`'s unsigned C++ storage.

```foundry
var rng := RandomNumberGenerator.new()
var seed: ulong = rng.get_seed()       # uint64_t
var roll: uint = rng.randi()           # uint32_t
var span: long = rng.get_seed()        # error: ulong does not fit long
var span2: long = rng.get_seed() as long
```

## Reflection, containers, and persistence

**Typed containers keep both carrier and range.** `Array[int]`, `Array[uint]`, `Array[long]`, and
`Array[ulong]` are distinct typed containers, and reading an element recovers the container's
declared width rather than the carrier's. They are invariant with each other, like every other typed
container.

**Reflection reports what the channel can carry.** Foundry Script's own rich metadata
(`FSReflection` descriptors and compiled member/signature records) keeps the exact type, so
cross-script analysis sees `uint` where `uint` was declared. A plain `PropertyInfo` has one type slot
and therefore encodes only the carrier: `int` and `long` both appear as `INT`, and `uint` and `ulong`
both appear as `UINT`. Generic Object reflection is consequently carrier-accurate and width-erased,
by design.

**Persistence is exact.** Text Variant and resource syntax tags the unsigned carrier with `UL` and
parses all three suffixes, so `UINT64_MAX` round-trips through scenes, resources, project settings,
clipboard serialization, and command-line Variant parsing:

```foundry
var v: Variant = 18446744073709551615UL
print(var_to_str(v))                  # 18446744073709551615UL
print(str_to_var("18446744073709551615UL"))
```

Binary Variant encoding, the debugger transport, RPC encoding, and the extension ABI all carry a
distinct UINT tag. A peer or extension that does not know the tag fails decoding or version
negotiation rather than reinterpreting the payload as signed.

**JSON is explicitly lossy.** JSON has no signedness and no fixed-width integer type, and common
consumers cannot represent every 64-bit integer exactly. Untyped JSON does not promise to round-trip
a width, a carrier, or every `long`/`ulong` value. Schema-driven decoding applies the same checked
range conversion as any other typed boundary, but it cannot recover digits a JSON number parser has
already rounded, and it fails rather than approximating.

## Known limitations

These are tracked defects in the shipped surface, not intended behavior. Each one has an open issue.

- **`int` is not yet range-checked at 32 bits** ([#1684]). `int` is documented and rendered as a
  signed 32-bit type, but it still applies no width constraint internally, so
  `var value: int = 4000000000L` is accepted today and `int` arithmetic is checked at 64 bits. Do not
  rely on that; write `long` when you mean 64 bits. Related, `int` mixed with `uint` reports the
  generic `Invalid operands "int" and "uint"` message instead of the carrier-crossing message with
  conversion advice.
- **An unsuffixed constant does not cross carriers** ([#1759]). `var count: uint = 42` is rejected;
  write `42U`. A constant narrows within its own carrier (`var count: uint = 42UL` is accepted), but
  it will not move between the signed and unsigned carrier.
- **`uint` does not reach `long` at all**, in an assignment or in a mixed expression, even though
  every `uint` value is a `long` value. The design lists `uint` -> `long` as a value-preserving
  implicit conversion; it is not implemented. Convert explicitly.
- **`uint` does not reach `float` implicitly**, even though every `uint` value is exactly
  representable as a double. `int` does.
- **Conversion-call syntax is missing** ([#1763]). `uint()`, `long()`, and `ulong()` do not exist, and
  `int(value)` is the legacy unchecked Variant constructor: applied to a `ulong` above the signed
  range it wraps in a release build and trips a development assertion in a development build. Use
  `as`.
- **Compound assignment on a flow-narrowed value is not checked at the narrowed width** ([#1763]).
- **Builtin Variant method binds carry no integer metadata** ([#1757]), so a builtin method whose C++
  signature is unsigned — `Color.hex()` for instance — is described with the carrier-wide `ulong`.
- **`ADD_PROPERTY(Variant::INT)` with unsigned accessors** disagrees with generic reflection
  ([#1758]): the accessors say unsigned and the declared property type says signed.
- **`TypedArray` hint strings erase element width** in signature hints, so a signature hint may show
  a wider element type than the container validates ([#1763]).
- **Global numeric utility functions do not accept the unsigned carrier.** `abs()`, `sign()`,
  `floor()`, `ceil()`, `round()`, `snapped()`, `lerp()`, and `wrap()` reject a `uint`/`ulong` argument
  with an explicit invalid-argument error. Convert to `long` first when the value fits.
- **`JsonNode.of()` refuses unsigned values** with `JsonNode.of() cannot represent a value of type
  39`. Convert a `uint`/`ulong` to `long` or to a `String` first. `JSON.stringify()` does accept
  them.

[#1684]: https://github.com/cafecito-games/Foundry/issues/1684
[#1757]: https://github.com/cafecito-games/Foundry/issues/1757
[#1758]: https://github.com/cafecito-games/Foundry/issues/1758
[#1759]: https://github.com/cafecito-games/Foundry/issues/1759
[#1763]: https://github.com/cafecito-games/Foundry/issues/1763

## See also

- [Migrating 64-bit `int` code to `long`](int_to_long_migration.md)
- `modules/foundry_script/GRAMMAR.md` — the normative grammar
