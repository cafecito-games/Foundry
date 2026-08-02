# Fixed-Width Integers Phase 2: Numeric Descriptor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Carry exact integer width and signedness through FoundryScript types, typed containers, reflection, generics,
and bytecode without changing public source syntax yet.

**Architecture:** Add one core `NumericType` descriptor shared by `ContainerType` and FoundryScript's parser/runtime
type records. Plain `PropertyInfo` remains carrier-only; rich Foundry metadata is authoritative, and lossy boundaries
decode to the wide carrier type.

**Tech Stack:** C++17, FoundryScript analyzer/compiler/VM type records, typed Variant containers, doctest, `.fsb`
bytecode codec.

**Prerequisite:** Phase 1 complete; `Variant::UINT` exists and the strict suite passes.

---

## File map

- `core/variant/numeric_type.h`: new descriptor enum and pure range/carrier helpers.
- `core/variant/container_type_validate.h/.cpp`: preserve and validate descriptors in typed containers.
- `modules/foundry_script/fs_parser.h`: add the descriptor to `FSParser::DataType`.
- `modules/foundry_script/fs_parser_data_type.cpp`: equality, rendering, PropertyInfo erasure, and substitution.
- `modules/foundry_script/fs_function.h/.cpp`: add the descriptor to `FSDataType` and ContainerType conversion.
- `modules/foundry_script/fs_compiler.cpp`: lower rich parser types to runtime types.
- `modules/foundry_script/fs_analyzer.cpp`: construct rich types and apply wide defaults at erased boundaries.
- `modules/foundry_script/fs_type.cpp`: include the descriptor in compatibility.
- `modules/foundry_script/fs_bytecode_format.h`: advance the `.fsb` format.
- `modules/foundry_script/fs_bytecode_export.cpp`: serialize descriptors recursively.
- `modules/foundry_script/fs_bytecode_loader.cpp`: validate and decode descriptors recursively.
- `modules/foundry_script/tests/test_foundry_script_type.h`: DataType/property-boundary tests.
- `modules/foundry_script/tests/test_generic_runtime.h`: ContainerType/generic round trips.
- `modules/foundry_script/tests/test_bytecode_serialization.h`: `.fsb` descriptor round trips and invalid bytes.

### Task 1: Define a reusable numeric descriptor

**Files:**
- Create: `core/variant/numeric_type.h`
- Modify: `tests/core/variant/test_variant.h`

- [ ] **Step 1: Write range/carrier tests**

```cpp
TEST_CASE("[Variant][NumericType] Integer descriptors expose exact contracts") {
	CHECK(numeric_type_carrier(NumericType::INT32) == Variant::INT);
	CHECK(numeric_type_carrier(NumericType::UINT64) == Variant::UINT);
	CHECK(numeric_type_contains(NumericType::INT32, Variant(int64_t(INT32_MAX))));
	CHECK_FALSE(numeric_type_contains(NumericType::INT32, Variant(int64_t(INT32_MAX) + 1)));
	CHECK(numeric_type_contains(NumericType::UINT32, Variant(uint64_t(UINT32_MAX))));
	CHECK_FALSE(numeric_type_contains(NumericType::UINT32, Variant(uint64_t(UINT32_MAX) + 1)));
}
```

- [ ] **Step 2: Run the test to verify the API is absent**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*[NumericType]*"`

Expected: compilation fails because `NumericType` and its helpers do not exist.

- [ ] **Step 3: Implement the descriptor and pure helpers**

```cpp
enum class NumericType : uint8_t {
	NONE,
	INT8,
	UINT8,
	INT16,
	UINT16,
	INT32,
	UINT32,
	INT64,
	UINT64,
	MAX,
};

Variant::Type numeric_type_carrier(NumericType p_type);
bool numeric_type_contains(NumericType p_type, const Variant &p_value);
String numeric_type_name(NumericType p_type);
```

Keep the implementation header-only and side-effect-free so core typed containers and the module can share it without a
new registration lifecycle. Return false when a descriptor/carrier pair is inconsistent.

- [ ] **Step 4: Run the focused test**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*[NumericType]*"`

Expected: all descriptor range/carrier assertions pass.

- [ ] **Step 5: Commit the descriptor**

```bash
git add core/variant/numeric_type.h tests/core/variant/test_variant.h
git commit -m "feat(core): define numeric type descriptors"
```

### Task 2: Preserve descriptors in typed containers

**Files:**
- Modify: `core/variant/container_type_validate.h:37-82`
- Modify: `core/variant/container_type_validate.cpp:50-180`
- Modify: `tests/core/variant/test_array.h`
- Modify: `tests/core/variant/test_dictionary.h`

- [ ] **Step 1: Add typed Array and Dictionary validation tests**

```cpp
TEST_CASE("[Variant][NumericType] Typed Array preserves numeric width") {
	ContainerType int_type;
	int_type.builtin_type = Variant::INT;
	int_type.numeric_type = NumericType::INT32;
	ContainerTypeValidate validator(int_type);
	CHECK(validator.test_validate(Variant(int64_t(INT32_MAX))));
	CHECK_FALSE(validator.test_validate(Variant(int64_t(INT32_MAX) + 1)));
	Array values;
	values.set_typed(int_type);
	CHECK(values.get_element_type().numeric_type == NumericType::INT32);
}

TEST_CASE("[Variant][NumericType] Typed Dictionary rejects signedness crossing") {
	ContainerType key_type;
	key_type.builtin_type = Variant::UINT;
	key_type.numeric_type = NumericType::UINT32;
	ContainerTypeValidate validator(key_type);
	CHECK_FALSE(validator.test_validate(Variant(int64_t(1))));
	CHECK(validator.test_validate(Variant(uint64_t(1))));
}
```

- [ ] **Step 2: Run tests and observe width erasure**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*[Variant][NumericType]*"`

Expected: compilation fails until `ContainerType` has `numeric_type`; then behavioral tests fail until validation uses
it.

- [ ] **Step 3: Add the descriptor to both container records**

```cpp
struct ContainerType {
	Variant::Type builtin_type = Variant::NIL;
	NumericType numeric_type = NumericType::NONE;
	// Existing fields unchanged.
};

struct ContainerTypeValidate {
	Variant::Type type = Variant::NIL;
	NumericType numeric_type = NumericType::NONE;
	// Existing fields unchanged.
};
```

Copy it in constructors/getters; include it in equality and `get_type_name()`; reject a non-NONE descriptor whose
carrier disagrees with `builtin_type`.

- [ ] **Step 4: Enforce carrier and range in `_internal_validate()`**

After ordinary Variant conversion establishes the correct carrier, call `numeric_type_contains()`. Do not convert INT to
UINT or UINT to INT during typed-container insertion. Emit the declared numeric type name in the existing container
validation error.

- [ ] **Step 5: Run typed-container tests**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*Typed Array*" --case "*Typed Dictionary*" --case "*[NumericType]*"`

Expected: width is preserved recursively and boundary values are accepted/rejected as specified.

- [ ] **Step 6: Commit typed-container support**

```bash
git add core/variant/container_type_validate.h core/variant/container_type_validate.cpp tests/core/variant/test_array.h tests/core/variant/test_dictionary.h
git commit -m "feat(variant): retain typed container numeric widths"
```

### Task 3: Add descriptors to parser and runtime data types

**Files:**
- Modify: `modules/foundry_script/fs_parser.h:110-390`
- Modify: `modules/foundry_script/fs_parser_data_type.cpp:45-280,528-804`
- Modify: `modules/foundry_script/fs_function.h:57-380`
- Modify: `modules/foundry_script/fs_function.cpp:80-115`
- Modify: `modules/foundry_script/fs_compiler.cpp` at `_gdtype_from_datatype()`
- Modify: `modules/foundry_script/tests/test_foundry_script_type.h`
- Modify: `modules/foundry_script/tests/test_generic_runtime.h:363`

- [ ] **Step 1: Add equality/copy/render/ContainerType tests**

```cpp
TEST_CASE("[FoundryScript][NumericType] Rich data types preserve width") {
	FSParser::DataType narrow;
	narrow.kind = FSParser::DataType::BUILTIN;
	narrow.builtin_type = Variant::INT;
	narrow.numeric_type = NumericType::INT32;
	narrow.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	FSParser::DataType wide = narrow;
	wide.numeric_type = NumericType::INT64;
	CHECK(narrow != wide);
	CHECK(narrow.to_string() == "int");
	CHECK(wide.to_string() == "long");

	FSDataType runtime;
	runtime.kind = FSDataType::BUILTIN;
	runtime.builtin_type = Variant::UINT;
	runtime.numeric_type = NumericType::UINT32;
	CHECK(runtime.to_container_type().numeric_type == NumericType::UINT32);
}
```

- [ ] **Step 2: Run the test and observe missing fields**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*[FoundryScript][NumericType]*"`

Expected: compilation fails because parser/runtime data types lack `numeric_type`.

- [ ] **Step 3: Add and propagate the field**

Add `NumericType numeric_type = NumericType::NONE;` to both records. Include it in `operator==`, hand-written
assignment/copy, `can_reference`, substitution output, `to_string`, `to_container_type`, `from_container_type`, and
compiler lowering.

- [ ] **Step 4: Define carrier-only wide defaults**

Create helpers used by both analyzer and runtime conversion:

```cpp
static NumericType wide_numeric_type_for_carrier(Variant::Type p_type) {
	if (p_type == Variant::INT) {
		return NumericType::INT64;
	}
	if (p_type == Variant::UINT) {
		return NumericType::UINT64;
	}
	return NumericType::NONE;
}
```

Only apply this helper when reconstructing a genuinely erased type. Rich types must retain their explicit descriptor.

- [ ] **Step 5: Run type and generic tests**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*[FoundryScript][NumericType]*" --case "*[Generics]*ContainerType*"`

Expected: all type copies, names, equality, and generic/container conversions preserve the descriptor.

- [ ] **Step 6: Commit rich type plumbing**

```bash
git add modules/foundry_script/fs_parser.h modules/foundry_script/fs_parser_data_type.cpp modules/foundry_script/fs_function.h modules/foundry_script/fs_function.cpp modules/foundry_script/fs_compiler.cpp modules/foundry_script/tests/test_foundry_script_type.h modules/foundry_script/tests/test_generic_runtime.h
git commit -m "feat(foundry_script): carry numeric type metadata"
```

### Task 4: Make compatibility and PropertyInfo erasure explicit

**Files:**
- Modify: `modules/foundry_script/fs_type.cpp:35-190`
- Modify: `modules/foundry_script/fs_analyzer.cpp:11336-11620`
- Modify: `modules/foundry_script/fs_parser_data_type.cpp:528-720`
- Modify: `modules/foundry_script/tests/test_foundry_script_type.h:100-180`

- [ ] **Step 1: Add erased-boundary and strict-identity tests**

```cpp
TEST_CASE("[FoundryScript][NumericType] PropertyInfo erases width to the wide carrier") {
	FSParser::DataType source;
	source.kind = FSParser::DataType::BUILTIN;
	source.builtin_type = Variant::UINT;
	source.numeric_type = NumericType::UINT32;
	source.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	const PropertyInfo property = source.to_property_info("value");
	CHECK(property.type == Variant::UINT);

	const FSParser::DataType decoded = decode_type_from_property(property);
	CHECK(decoded.builtin_type == Variant::UINT);
	CHECK(decoded.numeric_type == NumericType::UINT64);
}
```

- [ ] **Step 2: Run the test and observe ambiguous INT/UINT decoding**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*PropertyInfo erases width*"`

Expected: FAIL until PropertyInfo decoding applies the wide default.

- [ ] **Step 3: Update compatibility**

Require equal descriptors for invariant identity checks and typed-container element equality. Keep implicit numeric
promotion out of this phase: ordinary source `int` still resolves as the current legacy INT behavior until phase 3.

- [ ] **Step 4: Encode carrier-only PropertyInfo and decode wide defaults**

`DataType::to_property_info()` writes only `Variant::INT`/`UINT`. `FSAnalyzer::type_from_property()` assigns
`INT64`/`UINT64` when no richer method/member metadata is available. Add comments at both ends identifying this as the
approved width-erasure boundary.

- [ ] **Step 5: Run type tests**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*[FoundryScript][NumericType]*" --case "*[FoundryScript]*Type*"`

Expected: erased boundaries widen; rich identity and nested containers remain distinct.

- [ ] **Step 6: Commit compatibility and erasure rules**

```bash
git add modules/foundry_script/fs_type.cpp modules/foundry_script/fs_analyzer.cpp modules/foundry_script/fs_parser_data_type.cpp modules/foundry_script/tests/test_foundry_script_type.h
git commit -m "feat(foundry_script): define numeric width erasure"
```

### Task 5: Version and serialize descriptors in `.fsb`

**Files:**
- Modify: `modules/foundry_script/fs_bytecode_format.h:61`
- Modify: `modules/foundry_script/fs_bytecode_export.cpp:325-375`
- Modify: `modules/foundry_script/fs_bytecode_loader.cpp:550-620`
- Modify: `modules/foundry_script/tests/test_bytecode_serialization.h:563-680`

- [ ] **Step 1: Add recursive round-trip and malformed-enum tests**

```cpp
TEST_CASE("[FoundryScript][BytecodeCodec][NumericType] Descriptor round-trip") {
	FSDataType element;
	element.kind = FSDataType::BUILTIN;
	element.builtin_type = Variant::UINT;
	element.numeric_type = NumericType::UINT32;
	FSDataType array;
	array.kind = FSDataType::BUILTIN;
	array.builtin_type = Variant::ARRAY;
	array.set_container_element_type(0, element);
	const FSDataType decoded = bytecode_round_trip_data_type(array);
	CHECK(decoded.get_container_element_type(0).numeric_type == NumericType::UINT32);
}
```

Add a loader test that mutates the descriptor byte to `NumericType::MAX` and expects `ERR_INVALID_DATA`.

- [ ] **Step 2: Run the codec test and observe width loss**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*[BytecodeCodec][NumericType]*"`

Expected: round trip loses the descriptor before the codec changes.

- [ ] **Step 3: Add one descriptor byte to every encoded data type**

Immediately after `builtin_type`, write `put_u8(uint8_t(p_data_type.numeric_type))`. The loader reads it, rejects values
`>= NumericType::MAX`, and rejects inconsistent descriptor/carrier pairs. Recursive element/type-argument encoding uses
the same function automatically.

- [ ] **Step 4: Bump the bytecode format**

Advance `FSBytecodeFormat::FORMAT_VERSION` by one. Keep the existing exact-version rejection message; compiled scripts
from the prior layout must not be misread.

- [ ] **Step 5: Run bytecode codec and verifier tests**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*[BytecodeCodec]*" --case "*[BytecodeHardening]*"`

Expected: valid descriptors round-trip and malformed values fail cleanly.

- [ ] **Step 6: Commit bytecode support**

```bash
git add modules/foundry_script/fs_bytecode_format.h modules/foundry_script/fs_bytecode_export.cpp modules/foundry_script/fs_bytecode_loader.cpp modules/foundry_script/tests/test_bytecode_serialization.h
git commit -m "feat(foundry_script): serialize numeric descriptors"
```

### Task 6: Audit every rich-type copy and comparison path

**Files:**
- Modify: files returned by the audit under `modules/foundry_script/`
- Modify: nearest behavioral doctests for corrected paths

- [ ] **Step 1: Enumerate construction/copy/equality sites**

Run:

```bash
git grep -nE 'DataType result|FSDataType result|builtin_type =|to_container_type|from_container_type|operator==.*DataType|operator=.*DataType' modules/foundry_script -- '*.cpp' '*.h' > /tmp/foundry-numeric-type-audit.txt
```

Expected: a review list covering analyzer helpers, compiler lowering, name mangling snapshots, reflection, generics,
callables, tuples, and bytecode.

- [ ] **Step 2: Fix each behaviorally relevant omission**

Copy or compare `numeric_type` wherever `builtin_type` is copied or compared as type identity. Do not add source-text
assertions; add a nested width-sensitive type to the affected subsystem's existing behavioral test.

- [ ] **Step 3: Run the strict phase suite**

Run: `python3 scripts/agent_build.py --test --case "*[NumericType]*" --case "*[Generics]*" --case "*[BytecodeCodec]*" --case "*Typed*Container*"`

Expected: strict build succeeds and all descriptor/container/bytecode tests pass.

- [ ] **Step 4: Commit audit fixes**

```bash
git add core/variant modules/foundry_script
git commit -m "fix(foundry_script): complete numeric metadata audit"
```

## Phase 2 completion gate

- Numeric descriptors survive every rich type, nested container, generic, reflection, and `.fsb` path.
- Generic PropertyInfo decoding deliberately yields `long`/`ulong` carrier widths.
- Invalid descriptor/carrier combinations fail validation.
- Public FoundryScript numeric spelling and legacy `int` semantics remain unchanged until phase 3.
