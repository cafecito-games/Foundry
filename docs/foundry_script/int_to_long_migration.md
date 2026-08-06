# Migrating 64-bit `int` code to `long`

`int` used to mean a signed 64-bit integer. It now names a signed 32-bit integer, and `long` is the
signed 64-bit type. This is a breaking change with no compatibility alias, no legacy mode, and no
implicit widening back: code that needs the old range must say `long`.

Read [Fixed-width integers](fixed_width_integers.md) first for the full type model. This document is
the mechanical change list.

## What breaks and what does not

**Unannotated code mostly survives.** An unsuffixed literal that does not fit `int` infers `long`, so
a large constant keeps its range without an annotation, and `var count := 0` is unaffected in
practice.

**Annotated 64-bit slots are what change.** Every `int` you wrote to mean "a 64-bit integer" now
means 32 bits: variable, constant, parameter, and return annotations; class members; typed containers
(`Array[int]`, `Dictionary[String, int]`); generic arguments; tuple and enum-payload fields; signal
parameters; and `as int` / `is int`.

**Unsigned native APIs move carrier.** A native method whose C++ signature is `uint32_t` or
`uint64_t` now produces `uint` or `ulong`, which no longer assigns to an `int`/`long` slot. This is
the change most likely to appear in code that was previously silent.

## Mechanical changes

### Widen a 64-bit slot

```foundry
# Before
var total_bytes_written: int = 0
func total(values: Array[int]) -> int:
    var sum: int = 0
    for value in values:
        sum += value
    return sum

# After
var total_bytes_written: long = 0
func total(values: Array[long]) -> long:
    var sum: long = 0
    for value in values:
        sum += value
    return sum
```

`0` stays unsuffixed: an `int` constant reaches a `long` slot by value-preserving promotion. Add `L`
only where inference would otherwise pick `int` and you need the wider type to be part of the
inferred type:

```foundry
var accumulator := 0      # int
var accumulator := 0L     # long
```

### Take the type a native API actually declares

```foundry
# Before
var seed: int = rng.get_seed()
var roll: int = rng.randi()
var started_at: int = Time.get_ticks_usec()

# After
var seed: ulong = rng.get_seed()          # uint64_t
var roll: uint = rng.randi()              # uint32_t
var started_at: ulong = Time.get_ticks_usec()
```

If a signed slot is genuinely what you want, convert explicitly and accept that the conversion is
checked:

```foundry
var seed: long = rng.get_seed() as long   # fails above 9223372036854775807
```

### Suffix an unsigned constant

An unsuffixed constant stays on the signed carrier, so it does not enter an unsigned slot:

```foundry
# Before / naive
var flags: uint = 12        # error: Cannot assign a value of type "int" as "uint"

# After
var flags: uint = 12U
var mask: ulong = 0xFFFF_FFFF_FFFF_FFFFUL
```

### Do not mix carriers in one expression

```foundry
# Before
var total := unsigned_count + signed_offset   # error

# After
var total := (unsigned_count as long) + signed_offset
```

### Replace conversion calls with `as`

`uint()`, `long()`, and `ulong()` do not exist, and `int()` is the legacy unchecked Variant
constructor rather than a checked width conversion. Use `as` for every width or signedness change.

```foundry
# Before
var narrow := int(wide_value)

# After
var narrow := wide_value as long
```

## Diagnostics you will see, and what to do

| Diagnostic | Cause | Fix |
|---|---|---|
| `Cannot assign a value of type long to variable "x" with specified type uint.` | A signed value entering an unsigned slot. | Convert with `as`, or make the slot signed. |
| `Cannot assign a value of type "int" as "uint".` | An unsuffixed constant in an unsigned slot. | Add `U` or `UL`. |
| `Cannot assign a value of type "ulong" as "uint".` | A suffixed constant too large for the slot. | Widen the slot, or use a value that fits. |
| `Invalid argument for "f()" function: argument 1 should be "uint" but is "ulong".` | A wide value passed to a narrow parameter. | Convert with `as`, or widen the parameter. |
| `Cannot return value of type "ulong" because the function return type is "uint".` | A wide value returned from a narrow function. | Convert with `as`, or widen the return type. |
| `Cannot have an element of type "ulong" in an array of type "Array[uint]".` | Typed containers keep width and are invariant. | Match the element type. |
| `The "+" operator cannot mix "uint" and "long" operands. Convert both to "long" explicitly.` | Carrier crossing in one expression. | Convert one operand with `as`. |
| `No integer type holds every value of both "long" and "ulong", so the "+" operator has no common type here.` | No safe common type exists. | Convert one operand with `as`. |
| `The "+" operator overflows "uint": the result is outside its range 0 to 4294967295.` | A checked operation left its result type's range. | Widen the operands' type, or restructure the arithmetic. |
| `Invalid operand of type "uint" for unary operator "unary-".` | Negation of an unsigned value. | Convert to a signed type first. |
| `Invalid shift count for the "<<" operator on "uint": it must be from 0 to 31.` | A shift count at or beyond the result width. | Use a count inside the width, or widen the type. |
| `Cannot convert nan to "long": it is not a finite number.` | A non-finite float cast to an integer. | Guard the value before converting. |
| `Invalid integer suffix "ul". Integer suffixes are uppercase "U", "L", or "UL"; write "1UL".` | Lowercase or misordered suffix. | Use the canonical uppercase spelling. |

## Search targets

Run these against your project's `.fs` sources to find the slots that changed meaning. They over-match
by design: review each hit rather than rewriting blindly.

```sh
# Annotated int slots, typed containers, casts, and type tests.
rg -n ':\s*int\b|->\s*int\b|\bArray\[int\]|\bDictionary\[[^]]*\bint\b|\bas int\b|\bis int\b' --glob '*.fs'

# Lowercase or misordered literal suffixes left over from other languages.
rg -n '\b[0-9][0-9_]*(u|l|uL|Ul|lu|LU|LL|ull)\b' --glob '*.fs'

# Calls into native APIs that are now unsigned.
rg -n '\.get_seed\(|\.set_seed\(|\.randi\(|Time\.get_ticks_' --glob '*.fs'
```

## Known limitations while migrating

`int` is documented and rendered as a signed 32-bit type, but it does not yet apply that constraint
internally, so `var value: int = 4000000000L` is currently accepted and `int` arithmetic is still
checked at 64 bits ([#1684]). The constraint will be enforced. Migrate on the documented meaning of
`int`, not on what the analyzer tolerates today, or the same code will start failing when the
carve-out is removed.

The remaining known gaps are listed under
[Known limitations](fixed_width_integers.md#known-limitations).

[#1684]: https://github.com/cafecito-games/Foundry/issues/1684
