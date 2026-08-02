# Fixed-Width Integers Phase 1: Variant UINT Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a complete, versioned `Variant::UINT` carrier without exposing the new FoundryScript integer syntax.

**Architecture:** Append one `uint64_t` Variant category, preserve all existing Variant enum values, and give it exact
comparison/hash semantics plus defined modulo-2^64 core arithmetic. FoundryScript checked arithmetic is deliberately
deferred to phase 3.

**Tech Stack:** C++17, Godot Variant internals, doctest, Variant text/binary codecs, GDExtension ABI generation,
SCons/Ninja through `scripts/agent_build.py`.

**Prerequisite:** Approved umbrella spec at
`docs/superpowers/specs/2026-08-02-foundry-script-fixed-width-integers-design.md`.

---

## File map

- `core/variant/variant.h`: append the UINT tag, storage, constructors, accessors, and conversion entry points.
- `core/variant/variant.cpp`: names, conversion matrices, construction, copying, hashing, and string conversion.
- `core/variant/variant_internal.h`: UINT initialization and typed access.
- `core/variant/variant_construct.cpp`: registered INT/UINT checked constructors.
- `core/variant/variant_op.cpp`: UINT operators and exact INT/UINT comparisons.
- `core/variant/variant_parser.cpp`: text parsing/writing for the wide unsigned spelling.
- `core/io/marshalls.cpp`: binary Variant UINT wire payload.
- `core/extension/foundry_extension_interface.json`: source for the generated public enum and ABI declarations.
- `core/extension/extension_api_dump.cpp`: UINT API description and metadata output.
- `tests/core/variant/test_variant.h`: construction, text, comparison, ordering, and hashing behavior.
- `tests/core/variant/test_variant_utility.h`: conversion behavior.
- `tests/core/io/test_marshalls.h`: binary codec boundaries and malformed payloads.

### Task 1: Lock the public UINT identity with failing tests

**Files:**
- Modify: `tests/core/variant/test_variant.h:40`
- Modify: `tests/core/variant/test_variant_utility.h:44`

- [ ] **Step 1: Add constructor, accessor, and type-name tests**

```cpp
TEST_CASE("[Variant][UInt] Construction preserves the full unsigned range") {
	const uint64_t values[] = { 0, uint64_t(INT64_MAX), uint64_t(INT64_MAX) + 1, UINT64_MAX };
	for (uint64_t value : values) {
		Variant variant = value;
		CHECK(variant.get_type() == Variant::UINT);
		CHECK(uint64_t(variant) == value);
	}
	CHECK(Variant::get_type_name(Variant::UINT) == "uint");
}

TEST_CASE("[Variant][UInt] Checked carrier conversions reject out-of-range values") {
	Variant converted;
	Callable::CallError error;
	const Variant negative = int64_t(-1);
	const Variant too_large = uint64_t(INT64_MAX) + 1;
	const Variant *negative_arg = &negative;
	const Variant *large_arg = &too_large;
	Variant::construct(Variant::UINT, converted, &negative_arg, 1, error);
	CHECK(error.error != Callable::CallError::CALL_OK);
	Variant::construct(Variant::INT, converted, &large_arg, 1, error);
	CHECK(error.error != Callable::CallError::CALL_OK);
}
```

- [ ] **Step 2: Build to verify the tests fail before implementation**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*[Variant][UInt]*"`

Expected: compilation fails because `Variant::UINT` is not defined.

- [ ] **Step 3: Commit the red tests**

```bash
git add tests/core/variant/test_variant.h tests/core/variant/test_variant_utility.h
git commit -m "test(variant): specify unsigned carrier"
```

### Task 2: Add UINT storage, lifetime, and construction

**Files:**
- Modify: `core/variant/variant.h:99-148,275-360,364-446`
- Modify: `core/variant/variant.cpp:44-190,1481-1518,2314-2358`
- Modify: `core/variant/variant_internal.h:130-170,360-550,680-850`

- [ ] **Step 1: Append the type and add storage/access helpers**

```cpp
enum Type {
	// Existing values remain unchanged.
	PACKED_VECTOR4_ARRAY,
	UINT,
	VARIANT_MAX,
};

union {
	bool _bool;
	int64_t _int;
	uint64_t _uint;
	double _float;
	// Existing members unchanged.
} _data alignas(8);
```

Add `false, // UINT` at the end of `needs_deinit`, return `_data._uint` from UINT-aware unsigned access, and initialize
UINT to zero in `VariantInternal`.

- [ ] **Step 2: Route unsigned constructors to UINT**

```cpp
Variant::Variant(uint64_t p_value) :
		type(UINT) {
	_data._uint = p_value;
}

Variant::Variant(uint32_t p_value) : Variant(uint64_t(p_value)) {}
Variant::Variant(uint16_t p_value) : Variant(uint64_t(p_value)) {}
Variant::Variant(uint8_t p_value) : Variant(uint64_t(p_value)) {}
```

Keep explicitly specialized nominal wrappers on their existing carrier. Make copy/move/reference and `clear()` treat
UINT as an inline trivial value.

- [ ] **Step 3: Add type-name and default-value behavior**

Add `"uint"` to the end of `Variant::get_type_name()`, type-name lookup, zero initialization, and every
`VARIANT_MAX`-sized table.

- [ ] **Step 4: Build and run the construction tests**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*[Variant][UInt]*"`

Expected: construction/type-name assertions pass; checked constructor assertions may still fail until Task 3.

- [ ] **Step 5: Commit storage support**

```bash
git add core/variant/variant.h core/variant/variant.cpp core/variant/variant_internal.h
git commit -m "feat(variant): add unsigned storage carrier"
```

### Task 3: Implement checked carrier conversion and exact comparison/hash

**Files:**
- Modify: `core/variant/variant.h:364-410`
- Modify: `core/variant/variant.cpp:193-750,1481-1518`
- Modify: `core/variant/variant_construct.cpp:35-260`
- Modify: `core/variant/variant_op.cpp:480-790`
- Modify: `tests/core/variant/test_variant.h:1953`

- [ ] **Step 1: Add cross-carrier equality, ordering, and Dictionary-key tests**

```cpp
TEST_CASE("[Variant][UInt] Signed and unsigned comparison is mathematical") {
	const Variant signed_42 = int64_t(42);
	const Variant unsigned_42 = uint64_t(42);
	const Variant negative = int64_t(-1);
	const Variant huge = uint64_t(UINT64_MAX);
	CHECK(signed_42 == unsigned_42);
	CHECK(negative < unsigned_42);
	CHECK(huge > Variant(int64_t(INT64_MAX)));
	CHECK(signed_42.hash() == unsigned_42.hash());

	Dictionary dictionary;
	dictionary[signed_42] = "answer";
	CHECK(dictionary[unsigned_42] == "answer");
}
```

- [ ] **Step 2: Run the comparison test and observe failure**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*Signed and unsigned comparison*"`

Expected: FAIL because cross-carrier equality/order/hash are not registered consistently.

- [ ] **Step 3: Implement value-aware conversion helpers**

```cpp
static bool int_to_uint_checked(int64_t p_value, uint64_t &r_value) {
	if (p_value < 0) {
		return false;
	}
	r_value = uint64_t(p_value);
	return true;
}

static bool uint_to_int_checked(uint64_t p_value, int64_t &r_value) {
	if (p_value > uint64_t(INT64_MAX)) {
		return false;
	}
	r_value = int64_t(p_value);
	return true;
}
```

Use these in registered constructors and conversion entry points. `can_convert()` may advertise that a checked
conversion exists; construction reports `CALL_ERROR_INVALID_ARGUMENT` for a value outside the destination.

- [ ] **Step 4: Implement exact compare and compatible hashing**

Register INT/UINT and UINT/INT equality/order evaluators that branch on the signed operand's negativity rather than
casting. Canonicalize every UINT value `<= INT64_MAX` through the existing integer hash path; hash larger UINT values
from their full 64-bit bits.

- [ ] **Step 5: Run focused tests**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*[Variant][UInt]*"`

Expected: all UINT construction, conversion, comparison, and hash tests pass.

- [ ] **Step 6: Commit checked conversion and comparison**

```bash
git add core/variant/variant.h core/variant/variant.cpp core/variant/variant_construct.cpp core/variant/variant_op.cpp tests/core/variant/test_variant.h tests/core/variant/test_variant_utility.h
git commit -m "feat(variant): define unsigned conversion semantics"
```

### Task 4: Register the complete core UINT operator surface

**Files:**
- Modify: `core/variant/variant_op.cpp:200-1050`
- Modify: `tests/core/variant/test_variant.h:2228-2300`

- [ ] **Step 1: Add boundary tests for defined modulo behavior**

```cpp
TEST_CASE("[Variant][UInt] Core arithmetic wraps modulo 2^64") {
	bool valid = false;
	Variant result;
	Variant::evaluate(Variant::OP_ADD, Variant(uint64_t(UINT64_MAX)), Variant(uint64_t(1)), result, valid);
	REQUIRE(valid);
	CHECK(uint64_t(result) == 0);
	Variant::evaluate(Variant::OP_SHIFT_RIGHT, Variant(uint64_t(1) << 63), Variant(int64_t(63)), result, valid);
	REQUIRE(valid);
	CHECK(uint64_t(result) == 1);
	Variant::evaluate(Variant::OP_ADD, Variant(int64_t(1)), Variant(uint64_t(1)), result, valid);
	CHECK_FALSE(valid);
}
```

- [ ] **Step 2: Run the operator test and observe missing registrations**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*Core arithmetic wraps*"`

Expected: FAIL because UINT arithmetic is not registered.

- [ ] **Step 3: Register UINT unary/binary operators**

Register UINT/UINT add, subtract, multiply, divide, modulo, power, shifts, bitwise operations, comparisons, boolean
truthiness, string modulo, and containment paths where INT currently participates. Use `uint64_t` templates for defined
modulo arithmetic. Do not register mixed INT/UINT arithmetic; retain only the exact comparison registrations from Task
3.

- [ ] **Step 4: Run Variant operator coverage**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*Variant*Operator*" --case "*[Variant][UInt]*"`

Expected: all selected tests pass with no unregistered UINT operation reported.

- [ ] **Step 5: Commit operators**

```bash
git add core/variant/variant_op.cpp tests/core/variant/test_variant.h
git commit -m "feat(variant): register unsigned operators"
```

### Task 5: Add exact text persistence

**Files:**
- Modify: `core/variant/variant_parser.cpp:1000-1100,2050-2110`
- Modify: `tests/core/variant/test_variant.h:40-108`

- [ ] **Step 1: Add text round-trip tests**

```cpp
TEST_CASE("[Variant][UInt] Writer and parser preserve unsigned magnitude") {
	for (uint64_t value : { uint64_t(0), uint64_t(INT64_MAX) + 1, UINT64_MAX }) {
		Variant source = value;
		String text;
		VariantWriter::write_to_string(source, text);
		CHECK(text.ends_with("UL"));
		Variant decoded;
		String error;
		int line = 0;
		REQUIRE(VariantParser::parse(text, decoded, error, line) == OK);
		CHECK(decoded.get_type() == Variant::UINT);
		CHECK(uint64_t(decoded) == value);
	}
}
```

- [ ] **Step 2: Run the test and observe parser failure**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*preserve unsigned magnitude*"`

Expected: FAIL because the Variant parser does not accept the `UL` form.

- [ ] **Step 3: Parse and write the wide carrier spelling**

Extend numeric tokenization to accept uppercase `UL` for Variant text, parse through `uint64_t` overflow checks, and
write every UINT as decimal digits followed by `UL`. Keep ordinary `str(UINT)` as digits without a type suffix; only
persistence syntax carries `UL`.

- [ ] **Step 4: Run text and resource parser tests**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*[Variant]*Writer*parser*" --case "*[Variant][UInt]*"`

Expected: selected text round trips pass, including `UINT64_MAX`.

- [ ] **Step 5: Commit text persistence**

```bash
git add core/variant/variant_parser.cpp tests/core/variant/test_variant.h
git commit -m "feat(variant): serialize unsigned text values"
```

### Task 6: Add binary marshaling and extension ABI support

**Files:**
- Modify: `core/io/marshalls.cpp:236-320,1477-1610`
- Modify: `tests/core/io/test_marshalls.h`
- Modify: `core/extension/foundry_extension_interface.json:45-210`
- Modify: `core/extension/extension_api_dump.cpp:80-110`
- Modify: `core/extension/foundry_extension.cpp:50-220`

- [ ] **Step 1: Add binary boundary and malformed-input tests**

```cpp
TEST_CASE("[Marshalls][Variant][UInt] Binary round trip") {
	const Variant source = uint64_t(UINT64_MAX);
	int size = 0;
	REQUIRE(encode_variant(source, nullptr, size) == OK);
	Vector<uint8_t> bytes;
	bytes.resize(size);
	REQUIRE(encode_variant(source, bytes.ptrw(), size) == OK);
	Variant decoded;
	int used = 0;
	REQUIRE(decode_variant(decoded, bytes.ptr(), bytes.size(), &used) == OK);
	CHECK(decoded.get_type() == Variant::UINT);
	CHECK(uint64_t(decoded) == UINT64_MAX);
}
```

- [ ] **Step 2: Run the marshaling test and observe failure**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*[Marshalls][Variant][UInt]*"`

Expected: FAIL because the UINT wire case is absent.

- [ ] **Step 3: Encode UINT as an explicit 64-bit payload**

Add a `Variant::UINT` switch case that always writes and reads eight unsigned bytes using
`encode_uint64`/`decode_uint64`. Reject truncated input before reading. Do not reuse INT's compact signed encoding
because the wire tag must preserve carrier identity.

- [ ] **Step 4: Append the public extension enum and regenerate API output**

Append `FOUNDRY_EXTENSION_VARIANT_TYPE_UINT = 39` and advance `VARIANT_MAX` to 40 in the JSON source. Let SCons run
`core/extension/make_interface_header.py` and regenerate `foundry_extension_interface.gen.h`; do not hand-edit the
generated build output.

- [ ] **Step 5: Run codec and extension tests**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*[Marshalls]*" --case "*Extension*API*" --case "*[Variant][UInt]*"`

Expected: UINT binary and extension tests pass; old Variant enum numeric-value assertions remain unchanged.

- [ ] **Step 6: Commit binary and ABI support**

```bash
git add core/io/marshalls.cpp tests/core/io/test_marshalls.h core/extension
git commit -m "feat(variant): expose unsigned wire type"
```

### Task 7: Audit every Variant type table and validate the phase

**Files:**
- Modify: every source returned by the audit that indexes or switches on `Variant::VARIANT_MAX`
- Modify: the closest behavioral test for each corrected subsystem

- [ ] **Step 1: Produce the exhaustive audit list**

Run:

```bash
git grep -nE 'Variant::VARIANT_MAX|case Variant::INT|case INT:|\[Variant::VARIANT_MAX\]' -- '*.cpp' '*.h' > /tmp/foundry-uint-audit.txt
```

Expected: a finite list covering core, editor, servers, modules, and tests. Review every line and either add UINT
behavior or record why the switch intentionally rejects UINT in the implementing commit message.

- [ ] **Step 2: Add behavioral coverage for each corrected switch**

For every changed subsystem, extend its existing doctest with a UINT input and observable assertion. Do not add tests
that inspect source text or merely assert that a switch case exists.

- [ ] **Step 3: Run the strict native build and focused suite**

Run: `python3 scripts/agent_build.py --test --case "*[Variant]*" --case "*[Marshalls]*" --case "*Extension*API*"`

Expected: strict native build succeeds and all selected tests pass.

- [ ] **Step 4: Run the full suite with structured progress**

Run: `python3 scripts/agent_build.py --compiler-cache ccache --jobs 4 --test`

Expected: `[doctest] Status: SUCCESS!`; use the progress-file path printed by the wrapper to monitor the run.

- [ ] **Step 5: Commit the audit fixes**

```bash
git add core editor servers modules tests
git commit -m "fix(variant): complete unsigned type audit"
```

## Phase 1 completion gate

- UINT construction, conversion, comparison, hashing, operators, text/binary persistence, and extension ABI are tested.
- Existing Variant numeric enum values are unchanged.
- Mixed INT/UINT arithmetic is invalid in core; comparison remains exact.
- The strict build and full suite pass.
- No FoundryScript `uint`/`long`/`ulong` source syntax is exposed yet.
