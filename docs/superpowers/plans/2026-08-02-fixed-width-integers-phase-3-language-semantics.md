# Fixed-Width Integers Phase 3: FoundryScript Language Semantics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose `int`, `uint`, `long`, and `ulong` with uppercase literal suffixes, value-preserving promotion, checked
arithmetic, checked casts, and range-refining type tests.

**Architecture:** Resolve source numeric names through an explicit carrier-plus-descriptor registry. Centralize constant
and runtime behavior in `FSNumericOps`; the analyzer selects promotions and the compiler emits scalar-aware VM
operations that can report failure atomically.

**Tech Stack:** FoundryScript tokenizer/Pratt parser/analyzer/compiler/VM, C++ overflow intrinsics, script fixtures,
doctest, bytecode verifier.

**Prerequisite:** Phases 1 and 2 complete; UINT and numeric descriptors pass strict validation.

---

## File map

- `modules/foundry_script/GRAMMAR.md`: normative suffix and type grammar.
- `modules/foundry_script/fs_tokenizer.h/.cpp`: suffix scanning, magnitude parsing, diagnostics, token descriptor.
- `modules/foundry_script/fs_tokenizer_buffer.h/.cpp`: persist the token descriptor and advance tokenizer version.
- `modules/foundry_script/fs_parser_data_type.cpp`: explicit numeric built-in registry and source names.
- `modules/foundry_script/fs_analyzer.cpp`: literal typing, constructors/casts, promotions, operations, and type tests.
- `modules/foundry_script/fs_type.cpp`: assignment/call compatibility and implicit conversions.
- `modules/foundry_script/fs_numeric_ops.h/.cpp`: new checked operation and conversion implementation.
- `modules/foundry_script/SCsub`: compile the new numeric implementation unit.
- `modules/foundry_script/fs_function.h`: scalar-aware opcode and runtime failure contract.
- `modules/foundry_script/fs_codegen.h`: checked numeric code-generation interface.
- `modules/foundry_script/fs_byte_codegen.h/.cpp`: emit checked scalar opcodes.
- `modules/foundry_script/fs_compiler.cpp`: route analyzed scalar operations/casts to those opcodes.
- `modules/foundry_script/fs_vm.cpp`: execute checked operations and preserve failure atomicity.
- `modules/foundry_script/fs_bytecode_verifier.cpp`: validate descriptor/opcode operands.
- `modules/foundry_script/tests/test_numeric_ops.h`: new direct constant/runtime helper tests.
- `modules/foundry_script/tests/scripts/parser/`: accepted and rejected suffix fixtures.
- `modules/foundry_script/tests/scripts/analyzer/`: promotion/cast/migration fixtures.
- `modules/foundry_script/tests/scripts/runtime/features/`: checked runtime and dynamic-boundary fixtures.

### Task 1: Specify and tokenize integer suffixes

**Files:**
- Modify: `modules/foundry_script/GRAMMAR.md:182-225`
- Modify: `modules/foundry_script/fs_tokenizer.h:40-180,300-310`
- Modify: `modules/foundry_script/fs_tokenizer.cpp:757-945`
- Modify: `modules/foundry_script/fs_tokenizer_buffer.h`
- Modify: `modules/foundry_script/fs_tokenizer_buffer.cpp`
- Create: `modules/foundry_script/tests/scripts/parser/features/fixed_width_integer_literals.fs`
- Create: `modules/foundry_script/tests/scripts/parser/features/fixed_width_integer_literals.out`
- Create: `modules/foundry_script/tests/scripts/parser/errors/fixed_width_integer_suffix_case.fs`
- Create: `modules/foundry_script/tests/scripts/parser/errors/fixed_width_integer_suffix_case.out`

- [ ] **Step 1: Add accepted and rejected parser fixtures**

```foundry
# fixed_width_integer_literals.fs
const I = 42
const U = 42U
const L = 42L
const UL = 18_446_744_073_709_551_615UL
const HEX = 0xFFFFU
const BIN = 0b1010UL
```

```foundry
# fixed_width_integer_suffix_case.fs
const A = 1u
const B = 1l
const C = 1uL
const D = 1LU
```

Expected diagnostics name `1U`, `1L`, `1UL`, and `1UL` respectively.

- [ ] **Step 2: Run fixtures and verify current rejection**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*FoundryScript*Parser*"`

Expected: the feature fixture reports invalid numeric notation because letters after numbers are forbidden.

- [ ] **Step 3: Extend token data and scan canonical suffixes**

Add `NumericType numeric_type = NumericType::NONE;` to `FSTokenizer::Token`. In `number()`, consume one of `U`, `L`, or
`UL` only after the numeric body; detect any identifier-continuation spelling as one invalid suffix so diagnostics can
suggest a single replacement.

Parse through checked unsigned magnitude accumulation rather than `String::to_int()`, then construct:

```cpp
switch (numeric_type) {
	case NumericType::INT32:
	case NumericType::INT64:
		return make_numeric_literal(Variant(int64_t(signed_value)), numeric_type);
	case NumericType::UINT32:
	case NumericType::UINT64:
		return make_numeric_literal(Variant(uint64_t(magnitude)), numeric_type);
	default:
		return make_error("Invalid integer literal type.");
}
```

- [ ] **Step 4: Advance tokenizer-buffer layout and round-trip the descriptor**

Increment `TOKENIZER_VERSION`; write/read the descriptor for literal tokens; reject descriptor values outside
`NumericType::MAX` and carrier/descriptor mismatches.

- [ ] **Step 5: Update the normative grammar in the same change**

Add `integer_suffix = "U" | "L" | "UL"`, uppercase-only and tuple-index restrictions, plus examples for inferred
`int`/`long` and explicit unsigned types.

- [ ] **Step 6: Regenerate fixtures and run parser/tokenizer tests**

Run: `./bin/foundry.* --headless test generate-fixtures modules/foundry_script/tests/scripts`

Then: `python3 scripts/agent_build.py --backend ninja --test --case "*FoundryScript*Parser*" --case "*Tokenizer*"`

Expected: canonical forms parse; invalid forms produce the targeted replacement.

- [ ] **Step 7: Commit syntax support**

```bash
git add modules/foundry_script/GRAMMAR.md modules/foundry_script/fs_tokenizer.h modules/foundry_script/fs_tokenizer.cpp modules/foundry_script/fs_tokenizer_buffer.h modules/foundry_script/fs_tokenizer_buffer.cpp modules/foundry_script/tests/scripts/parser
git commit -m "feat(foundry_script): parse fixed-width integer literals"
```

### Task 2: Resolve the four source built-ins and infer literals

**Files:**
- Modify: `modules/foundry_script/fs_parser.h:2325`
- Modify: `modules/foundry_script/fs_parser_data_type.cpp:35-85`
- Modify: `modules/foundry_script/fs_analyzer.cpp:9600-9805,11336-11420`
- Modify: `modules/foundry_script/foundry_script.cpp:3611-3650`
- Modify: `modules/foundry_script/tests/test_foundry_script_type.h`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/fixed_width_integer_types.fs`

- [ ] **Step 1: Add built-in resolution and inference tests**

```cpp
TEST_CASE("[FoundryScript][NumericTypes] Source names resolve independently of carriers") {
	CHECK(FSParser::get_builtin_data_type("int").numeric_type == NumericType::INT32);
	CHECK(FSParser::get_builtin_data_type("uint").numeric_type == NumericType::UINT32);
	CHECK(FSParser::get_builtin_data_type("long").numeric_type == NumericType::INT64);
	CHECK(FSParser::get_builtin_data_type("ulong").numeric_type == NumericType::UINT64);
	CHECK(FSParser::get_builtin_data_type("int").builtin_type == Variant::INT);
	CHECK(FSParser::get_builtin_data_type("long").builtin_type == Variant::INT);
}
```

```foundry
func test():
	var inferred_int := 2_147_483_647
	var inferred_long := 2_147_483_648
	var contextual_uint: uint = 42
	assert(inferred_int is int)
	assert(inferred_long is long)
	assert(contextual_uint is uint)
```

- [ ] **Step 2: Run the tests and observe one-to-one registry failure**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*[NumericTypes]*"`

Expected: `uint`/`long`/`ulong` do not resolve and legacy `int` remains wide.

- [ ] **Step 3: Replace `get_builtin_type()` for source resolution**

Introduce `get_builtin_data_type(StringName)` returning carrier plus descriptor, with an explicit four-entry numeric
table and the existing generated mapping for non-numeric built-ins. Keep a carrier-only helper only for code that truly
operates on Variant types.

- [ ] **Step 4: Type literals and contextual constants**

Use the token descriptor in `reduce_literal()`. Unsuffixed integers choose INT32 when representable and INT64 otherwise.
During constant assignment/call validation, allow any destination descriptor whose exact range contains the mathematical
value and materialize the destination carrier before codegen.

- [ ] **Step 5: Reserve and render all source names**

Add the four built-in names to language reserved-word/type completion sources and render numeric names through
`numeric_type_name()` rather than `Variant::get_type_name()`.

- [ ] **Step 6: Run focused analyzer tests**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*[NumericTypes]*" --case "*fixed_width_integer_types*"`

Expected: names and literal inference match the spec.

- [ ] **Step 7: Commit source type resolution**

```bash
git add modules/foundry_script/fs_parser.h modules/foundry_script/fs_parser_data_type.cpp modules/foundry_script/fs_analyzer.cpp modules/foundry_script/foundry_script.cpp modules/foundry_script/tests
git commit -m "feat(foundry_script): expose four integer types"
```

### Task 3: Implement the promotion and assignment matrix

**Files:**
- Modify: `modules/foundry_script/fs_type.h`
- Modify: `modules/foundry_script/fs_type.cpp`
- Modify: `modules/foundry_script/fs_analyzer.h`
- Modify: `modules/foundry_script/fs_analyzer.cpp:13207-13320`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/fixed_width_integer_promotions.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/fixed_width_integer_unsafe_mix.fs`

- [ ] **Step 1: Add a complete promotion fixture**

```foundry
func test():
	var i: int = 1
	var u: uint = 2U
	var l: long = 3L
	var ul: ulong = 4UL
	var int_uint: long = i + u
	var uint_long: long = u + l
	var uint_ulong: ulong = u + ul
	assert(int_uint == 3L)
	assert(uint_long == 5L)
	assert(uint_ulong == 6UL)
```

The error fixture covers `int + ulong`, `long + ulong`, `int -> uint`, `long -> uint`, `ulong -> long`, and every
narrowing assignment.

- [ ] **Step 2: Run analyzer fixtures and observe carrier-only acceptance**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*fixed_width_integer_promotions*" --case "*fixed_width_integer_unsafe_mix*"`

Expected: unsafe pairs are accepted or mis-typed because compatibility still keys on carrier alone.

- [ ] **Step 3: Add one promotion function**

```cpp
bool promote_integer_pair(NumericType p_left, NumericType p_right, NumericType &r_result);
```

Implement exactly the eight symmetric rows in the design. Use it from analyzer binary operations, comparison helpers,
and common-type inference. Return false for pairs without a safe type.

- [ ] **Step 4: Add assignment conversion classification**

Return one of `IDENTITY`, `IMPLICIT_WIDEN`, `CONSTANT_CHECKED`, `EXPLICIT_REQUIRED`, or `INVALID`. Use the same
classifier for assignments, arguments, returns, signals, and typed collection literals so diagnostics cannot drift.

- [ ] **Step 5: Implement float interaction**

Allow INT32/UINT32 to promote to existing double-backed `float`. Require an explicit cast for INT64/UINT64 unless an
exactly representable constant is proven. Preserve existing explicit float-to-integer truncation rules, adding
non-finite/range rejection in Task 4.

- [ ] **Step 6: Run promotion fixtures**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*fixed_width_integer*" --case "*[NumericTypes]*"`

Expected: safe pairs infer the table's result and unsafe pairs emit type-specific diagnostics.

- [ ] **Step 7: Commit promotion rules**

```bash
git add modules/foundry_script/fs_type.h modules/foundry_script/fs_type.cpp modules/foundry_script/fs_analyzer.h modules/foundry_script/fs_analyzer.cpp modules/foundry_script/tests/scripts/analyzer
git commit -m "feat(foundry_script): enforce integer promotion rules"
```

### Task 4: Build one checked numeric operation layer

**Files:**
- Create: `modules/foundry_script/fs_numeric_ops.h`
- Create: `modules/foundry_script/fs_numeric_ops.cpp`
- Modify: `modules/foundry_script/SCsub`
- Create: `modules/foundry_script/tests/test_numeric_ops.h`
- Modify: `modules/foundry_script/tests/test_foundry_script.h`

- [ ] **Step 1: Write table-driven boundary tests**

```cpp
TEST_CASE("[FoundryScript][NumericOps] Checked add is identical across widths") {
	struct Case { NumericType type; Variant max; Variant one; };
	const Case cases[] = {
		{ NumericType::INT32, int64_t(INT32_MAX), int64_t(1) },
		{ NumericType::UINT32, uint64_t(UINT32_MAX), uint64_t(1) },
		{ NumericType::INT64, int64_t(INT64_MAX), int64_t(1) },
		{ NumericType::UINT64, uint64_t(UINT64_MAX), uint64_t(1) },
	};
	for (const Case &test : cases) {
		Variant result;
		FSNumericError error;
		CHECK_FALSE(FSNumericOps::binary(Variant::OP_ADD, test.type, test.max, test.one, result, error));
		CHECK(error == FSNumericError::OVERFLOW);
	}
}
```

Add analogous tables for subtraction, multiplication, division/remainder, power, negation/abs, bitwise width, and
shifts.

- [ ] **Step 2: Build and observe missing helper failure**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*[NumericOps]*"`

Expected: compilation fails because `FSNumericOps` is absent.

- [ ] **Step 3: Define structured operations and failures**

```cpp
enum class FSNumericError : uint8_t {
	NONE,
	OVERFLOW,
	DIVISION_BY_ZERO,
	INVALID_SHIFT,
	INVALID_SIGN,
	NON_FINITE,
};

class FSNumericOps {
public:
	static bool binary(Variant::Operator p_op, NumericType p_type, const Variant &p_left,
			const Variant &p_right, Variant &r_result, FSNumericError &r_error);
	static bool unary(Variant::Operator p_op, NumericType p_type, const Variant &p_value,
			Variant &r_result, FSNumericError &r_error);
	static bool convert(NumericType p_target, const Variant &p_value, Variant &r_result,
			FSNumericError &r_error);
};
```

- [ ] **Step 4: Implement every operation with overflow-safe primitives**

Use compiler overflow intrinsics for signed/unsigned add/sub/mul, explicit checks for division and shifts,
exponentiation by checked repeated squaring, and descriptor-specific masks for `~`. Never compute signed overflow and
inspect it afterward.

- [ ] **Step 5: Run direct numeric tests**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*[NumericOps]*"`

Expected: every boundary returns the exact value or structured error.

- [ ] **Step 6: Commit the shared layer**

```bash
git add modules/foundry_script/fs_numeric_ops.h modules/foundry_script/fs_numeric_ops.cpp modules/foundry_script/SCsub modules/foundry_script/tests/test_numeric_ops.h modules/foundry_script/tests/test_foundry_script.h
git commit -m "feat(foundry_script): add checked numeric operations"
```

### Task 5: Use checked operations for constants, casts, and type tests

**Files:**
- Modify: `modules/foundry_script/fs_analyzer.cpp:5750-6070,6710-6780,9780-9810,10920-11050`
- Modify: `modules/foundry_script/fs_type.cpp`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/fixed_width_integer_constant_overflow.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/fixed_width_integer_casts.fs`

- [ ] **Step 1: Add constant-folding, cast, and refinement fixtures**

```foundry
func test(value: Variant):
	assert(int(2_147_483_647L) == 2_147_483_647)
	assert(uint(4_294_967_295UL) == 4_294_967_295U)
	assert(value is int or value is long or value is uint or value is ulong)
```

The error fixture covers `2_147_483_647 + 1`, `0U - 1U`, `ulong(-1)`, `int(2_147_483_648L)`, non-finite float casts, and
invalid shifts.

- [ ] **Step 2: Run fixtures and observe unchecked constant results**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*fixed_width_integer_constant_overflow*" --case "*fixed_width_integer_casts*"`

Expected: constant folding wraps or uses the wrong carrier before integration.

- [ ] **Step 3: Route constant reduction through `FSNumericOps`**

When both operands are constant and promoted numeric descriptors are known, call the shared helper. Convert a structured
failure into an analyzer diagnostic naming the operation, type, and valid range.

- [ ] **Step 4: Route constructors and `as` through checked conversion**

Use `FSNumericOps::convert()` for scalar constructors and casts. Constant failures are analyzer errors; non-constant
casts retain a runtime checked-cast marker for Task 6.

- [ ] **Step 5: Implement range-refining `is` semantics**

For Variant operands, `is int`/`is uint` check carrier plus range and `is long`/`is ulong` check carrier. Statically
known narrow/wide relationships may fold only when doing so preserves existing diagnostic and reachability behavior.

- [ ] **Step 6: Run analyzer fixtures**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*fixed_width_integer*" --case "*[NumericOps]*"`

Expected: constants and casts agree with direct helper tests; type tests follow range refinement.

- [ ] **Step 7: Commit analyzer integration**

```bash
git add modules/foundry_script/fs_analyzer.cpp modules/foundry_script/fs_type.cpp modules/foundry_script/tests/scripts/analyzer
git commit -m "feat(foundry_script): check numeric constants and casts"
```

### Task 6: Execute checked operations in bytecode and the VM

**Files:**
- Modify: `modules/foundry_script/fs_function.h:380-620`
- Modify: `modules/foundry_script/fs_codegen.h:130-150`
- Modify: `modules/foundry_script/fs_byte_codegen.h`
- Modify: `modules/foundry_script/fs_byte_codegen.cpp`
- Modify: `modules/foundry_script/fs_compiler.cpp`
- Modify: `modules/foundry_script/fs_vm.cpp:1200-1300,3900-4020`
- Modify: `modules/foundry_script/fs_bytecode_verifier.cpp`
- Modify: `modules/foundry_script/fs_bytecode_format.h`
- Modify: `modules/foundry_script/fs_bytecode_export.cpp`
- Modify: `modules/foundry_script/fs_bytecode_loader.cpp`

- [ ] **Step 1: Add runtime and failure-atomicity fixtures**

```foundry
func test():
	var x: uint = 4_294_967_295U
	var before := x
	# Test harness invokes overflow paths separately and verifies x remains before.
	assert((1U << 31) == 2_147_483_648U)
	assert((9UL / 2UL) == 4UL)
```

Add C++ VM tests that invoke a function containing `x += 1U`, capture the runtime error, and inspect `x` to prove the
store did not occur.

- [ ] **Step 2: Run runtime tests and observe unchecked Variant evaluation**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*NumericOps*Runtime*"`

Expected: overflow wraps or no scalar-aware opcode exists.

- [ ] **Step 3: Add scalar-aware opcode contracts**

Add `OPCODE_NUMERIC_BINARY`, `OPCODE_NUMERIC_UNARY`, and `OPCODE_NUMERIC_CAST`. Each encodes the Variant
operator/destination descriptor and addresses. Add corresponding `FSCodeGenerator` methods and bytecode-generator
emission.

- [ ] **Step 4: Compile typed expressions and casts to the new opcodes**

Use scalar-aware opcodes whenever the analyzed result carries a numeric descriptor. Preserve existing generic Variant
operators for non-numeric values. Compound assignment writes to a temporary, then stores only after the helper succeeds.

- [ ] **Step 5: Execute and diagnose structured failures**

Call `FSNumericOps` from the VM; translate its error enum into stable runtime messages. Dynamic INT/INT uses INT64,
UINT/UINT uses UINT64, mixed carrier arithmetic reports a runtime type error, and mixed equality/order uses core exact
comparison.

- [ ] **Step 6: Serialize and verify the opcodes**

Advance `.fsb` format again for the opcode layout. The verifier checks operator enum, numeric descriptor, operand
addresses, and destination address before execution.

- [ ] **Step 7: Run source/bytecode runtime tests**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*NumericOps*" --case "*[BytecodeCodec]*" --case "*[BytecodeHardening]*"`

Expected: source and exported bytecode return identical values/errors; failed compound assignments are atomic.

- [ ] **Step 8: Commit VM integration**

```bash
git add modules/foundry_script/fs_function.h modules/foundry_script/fs_codegen.h modules/foundry_script/fs_byte_codegen.h modules/foundry_script/fs_byte_codegen.cpp modules/foundry_script/fs_compiler.cpp modules/foundry_script/fs_vm.cpp modules/foundry_script/fs_bytecode_verifier.cpp modules/foundry_script/fs_bytecode_format.h modules/foundry_script/fs_bytecode_export.cpp modules/foundry_script/fs_bytecode_loader.cpp modules/foundry_script/tests
git commit -m "feat(foundry_script): execute checked integer operations"
```

### Task 7: Complete language fixtures and strict validation

**Files:**
- Modify: `modules/foundry_script/tests/scripts/runtime/features/`
- Modify: `modules/foundry_script/tests/scripts/analyzer/`
- Modify: `modules/foundry_script/tests/scripts/parser/`

- [ ] **Step 1: Add the full behavioral matrix**

Cover every min/max and one-past boundary, all promotion pairs, all operators, explicit casts, float interaction, typed
parameters/returns/members/signals/callables, dynamic Variant crossings, generic/container use, enums retaining signed
64-bit backing, and suffix diagnostics.

- [ ] **Step 2: Regenerate intentional fixtures**

Run: `./bin/foundry.* --headless test generate-fixtures modules/foundry_script/tests/scripts`

Expected: only new or intentionally changed numeric `.out` files change.

- [ ] **Step 3: Run the strict numeric suite**

Run: `python3 scripts/agent_build.py --test --case "*NumericOps*" --case "*fixed_width_integer*" --case "*[FoundryScript]*Type*" --case "*[Bytecode*]*"`

Expected: strict build succeeds and the numeric/parser/analyzer/runtime/bytecode matrix passes.

- [ ] **Step 4: Commit fixture completion**

```bash
git add modules/foundry_script/tests
git commit -m "test(foundry_script): cover fixed-width integers"
```

## Phase 3 completion gate

- The four public types, uppercase suffixes, promotion matrix, checked constants/runtime/casts, dynamic rules, and
  type refinements match the spec.
- `GRAMMAR.md` is authoritative for the new syntax.
- Constant folding and VM execution share `FSNumericOps` and agree at every boundary.
- Source and `.fsb` execution pass focused strict validation.
- Tooling/native/editor completeness remains the phase 4 release gate.
