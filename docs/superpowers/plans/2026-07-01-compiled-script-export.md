# Compiled Script Export (.fsb) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Exported games ship Foundry Script as serialized compiled bytecode (`.fsb`) that skips tokenize/parse/analyze/compile at runtime and cannot be reversed to original source structure.

**Architecture:** A writer (`fs_bytecode_export`, TOOLS_ENABLED) serializes the compiled `FoundryScript` object graph after the editor toolchain compiles it at export time; a reader/linker (`fs_bytecode_loader`, all builds) reconstructs the graph and re-resolves all process-bound pointers via symbolic fixup tables recorded by `FSByteCodeGenerator`. A new `MODE_SCRIPT_COMPILED_BYTECODE` export preset mode drives it; `FSCache` gains `.fsb` branches mirroring the existing `.fsc` ones.

**Tech Stack:** C++ (Godot engine fork), SCons, doctest (`tests/test_macros.h`), Foundry Script fixture corpus (`modules/foundry_script/tests/scripts/`).

**Authoritative references:**
- Spec: `docs/superpowers/specs/2026-07-01-compiled-script-export-design.md`
- Detailed design with verified file:line ground truth: `/Users/christian/.claude/plans/i-want-exported-versions-lexical-snail-agent-af5965cde49649c3a.md` (referred to below as "the design doc"; read its section 0 before starting any task)

**Build & test commands used throughout** (macOS local; use `platform=linuxbsd` + `python3 -m SCons` on the Linux VM, with `cache_path="$HOME/.scons_cache"` there):

```bash
scons platform=macos target=editor dev_build=yes tests=yes cache_path=/Users/christian/.scons_cache_godot -j$(sysctl -n hw.ncpu)
./bin/foundry.macos.editor.dev.arm64 --headless --test --test-case="*Bytecode*" --force-colors
```

Trust the `[doctest] Status: SUCCESS!` line; `ObjectDB instances leaked` noise at exit is a known non-failure. New `test_*.h` files in `modules/foundry_script/tests/` are picked up automatically by the generated module test registry — no `tests/test_main.cpp` edit needed. Every new header gets the standard Godot copyright banner and `#ifndef`-style include guards (copy from a neighboring file; `pre-commit run --files <paths>` validates both).

**Commit discipline:** one commit per task minimum, imperative subject under 72 chars. Never commit if the suite fails.

---

## File Structure

| File | Responsibility |
|---|---|
| `modules/foundry_script/fs_bytecode_format.h` (new) | Format constants: magic, version, engine guard, section IDs, Variant tags, fixup-table IDs. Header comment documents the residual-information list (design doc §1.7). |
| `modules/foundry_script/fs_bytecode_export.h/.cpp` (new, `#ifdef TOOLS_ENABLED` around everything) | `FSBytecodeExporter`: string table builder, tagged Variant encoder, `FSDataType` encoder, per-function writer, per-class writer, `serialize(const Ref<FoundryScript> &)` entry point. |
| `modules/foundry_script/fs_bytecode_loader.h/.cpp` (new) | `FSBytecodeLoader`: header/guard check, string table reader, tagged Variant decoder, `FSDataType` decoder, function reader + fixup linker + bytecode verifier, `load_skeleton()` and `load_full()` entry points. |
| `modules/foundry_script/fs_function.h` (modify) | `#ifdef TOOLS_ENABLED` export-fixup metadata struct populated by codegen; friend access for exporter/loader. |
| `modules/foundry_script/fs_codegen.h`, `fs_byte_codegen.h/.cpp` (modify) | `write_store_global` gains the global name parameter; codegen records symbolic descriptors for every pointer-table entry under TOOLS_ENABLED. |
| `modules/foundry_script/fs_compiler.cpp` (modify) | Pass global name at the `write_store_global` call site. |
| `modules/foundry_script/fs_cache.h/.cpp` (modify) | `.fsb` branches in `get_shallow_script` / `get_full_script`; `FSParserRef::raise_status` guard. |
| `modules/foundry_script/foundry_script.h/.cpp` (modify) | `compiled_binary` flag + `reload()` guard; loader extension lists; `get_dependencies` `.fsb` branch. |
| `modules/foundry_script/register_types.cpp` (modify) | Export plugin: compiled-bytecode branch, built-in-script check, failure reporting. |
| `editor/export/editor_export_preset.h/.cpp`, `editor/export/project_export.cpp` (modify) | `MODE_SCRIPT_COMPILED_BYTECODE` enum value + UI item. |
| `modules/foundry_script/tests/test_bytecode_serialization.h` (new) | Codec + round-trip + hardening doctest suites. |
| `modules/foundry_script/tests/fs_test_runner.h/.cpp`, `fs_test_runner_suite.h` (modify) | Third corpus mode: compile → serialize → load → run. |

---

### Task 1: Format constants, string table, and Variant/FSDataType codecs

**Goal:** The `.fsb` building blocks — format header, deduped string table, tagged Variant codec (external-resource table), `FSDataType` codec — round-trip in isolation.

**Files:**
- Create: `modules/foundry_script/fs_bytecode_format.h`
- Create: `modules/foundry_script/fs_bytecode_export.h`, `modules/foundry_script/fs_bytecode_export.cpp`
- Create: `modules/foundry_script/fs_bytecode_loader.h`, `modules/foundry_script/fs_bytecode_loader.cpp`
- Test: `modules/foundry_script/tests/test_bytecode_serialization.h`

**Acceptance Criteria:**
- [ ] Header write/read round-trips; a corrupted engine-guard string makes the reader fail with `ERR_INVALID_DATA` and an error message naming both hashes — no crash.
- [ ] String table dedups (writing the same `StringName` twice yields one entry) and round-trips non-ASCII strings.
- [ ] Tagged Variant codec round-trips: all builtin scalar types, nested Array/Dictionary, typed arrays, a `Ref<Resource>` with a resource path (encodes as `TAG_EXTERNAL_RESOURCE` + path index — assert the raw buffer does NOT contain the resource's property data), and rejects a `Callable`/`RID`/plain-`Object` Variant with `ERR_INVALID_PARAMETER`.
- [ ] `FSDataType` codec round-trips every `Kind`, nested `container_element_types`, `type_arguments`, and script kinds as (path, fqcn) with no `Script *` in the payload.

**Verify:** `./bin/foundry.macos.editor.dev.arm64 --headless --test --test-case="*BytecodeCodec*" --force-colors` → all assertions pass.

**Steps:**

- [ ] **Step 1: Write `fs_bytecode_format.h`**

Copy the copyright banner + include-guard shape from `modules/foundry_script/fs_tokenizer_buffer.h`. Content:

```cpp
#include "core/typedefs.h"

class FSBytecodeFormat {
public:
	static constexpr uint8_t MAGIC[4] = { 'F', 'S', 'B', 'C' };
	static constexpr uint32_t FORMAT_VERSION = 1;

	enum SectionId : uint32_t {
		SECTION_STRING_TABLE,
		SECTION_EXTERNAL_REFS,
		SECTION_SKELETON,
		SECTION_CLASS_BODIES,
		SECTION_WITNESSES,
		SECTION_DEPENDENCIES,
		SECTION_MAX,
	};

	enum VariantTag : uint8_t {
		TAG_INLINE_VARIANT, // encode_variant payload, full_objects = false
		TAG_ARRAY,          // element-wise recursion (typed metadata included)
		TAG_DICTIONARY,     // element-wise recursion (typed metadata included)
		TAG_SCRIPT_REF,     // intra-file class index OR external (path, fqcn)
		TAG_EXTERNAL_SCRIPT,
		TAG_EXTERNAL_RESOURCE,
		TAG_NATIVE_CLASS,
		TAG_ENGINE_SINGLETON,
		TAG_SPECIALIZED_HANDLE,
	};

	enum FixupTable : uint8_t {
		FIXUP_OPERATOR,
		FIXUP_SETTER,
		FIXUP_GETTER,
		FIXUP_KEYED_SETTER,
		FIXUP_KEYED_GETTER,
		FIXUP_INDEXED_SETTER,
		FIXUP_INDEXED_GETTER,
		FIXUP_BUILTIN_METHOD,
		FIXUP_CONSTRUCTOR,
		FIXUP_UTILITY,
		FIXUP_GDS_UTILITY,
		FIXUP_METHOD_BIND,
		FIXUP_MAX,
	};
};
```

Add a block comment at the top documenting the residual-information list verbatim from the spec ("Accepted residual information" section) — this is the normative statement of what the format intentionally retains.

- [ ] **Step 2: Write the failing codec tests**

Create `modules/foundry_script/tests/test_bytecode_serialization.h` following the shape of `modules/foundry_script/tests/test_annotation_usages.h` (includes `tests/test_macros.h`, `TEST_SUITE("[Modules][FoundryScript][Bytecode]")`). Test cases:

```cpp
TEST_CASE("[FoundryScript][BytecodeCodec] Header round-trip and engine guard") {
	Vector<uint8_t> buffer = FSBytecodeExporter::write_test_header();
	CHECK(FSBytecodeLoader::check_header(buffer) == OK);
	// Flip one byte inside the engine-guard string region.
	buffer.write[16] ^= 0xFF;
	ERR_PRINT_OFF;
	CHECK(FSBytecodeLoader::check_header(buffer) == ERR_INVALID_DATA);
	ERR_PRINT_ON;
}

TEST_CASE("[FoundryScript][BytecodeCodec] String table dedup and round-trip") {
	FSBytecodeExporter::StringTable table;
	uint32_t a = table.insert("player_speed");
	uint32_t b = table.insert("player_speed");
	uint32_t c = table.insert(String::utf8("ñandú_速度"));
	CHECK(a == b);
	CHECK(a != c);
	// serialize -> deserialize -> lookups return identical strings
}

TEST_CASE("[FoundryScript][BytecodeCodec] Tagged Variant round-trip") {
	// scalars, nested arrays/dicts, typed arrays
}

TEST_CASE("[FoundryScript][BytecodeCodec] Resource constants become external references") {
	Ref<Resource> resource;
	resource.instantiate();
	resource->set_path_cache("res://icon.png");
	resource->set_meta("secret", "THIS_MUST_NOT_SERIALIZE");
	// encode Variant(resource); assert buffer lacks "THIS_MUST_NOT_SERIALIZE",
	// decode with a stub resolver returning a fresh Resource for "res://icon.png".
}

TEST_CASE("[FoundryScript][BytecodeCodec] Process-bound Variants are rejected") {
	// Callable, RID, Signal, bare Object* -> encoder returns ERR_INVALID_PARAMETER
}

TEST_CASE("[FoundryScript][BytecodeCodec] FSDataType round-trip") {
	// every Kind; nested container_element_types; script kind -> (path, fqcn)
}
```

The decoder takes a resolver callback (`std::function`-free; use a small abstract `FSBytecodeExternalResolver` interface with a test stub) so codec tests run without touching `ResourceLoader`.

- [ ] **Step 3: Build and confirm the tests fail to compile / fail**

Run the build; expect compile errors for the missing classes — that is the red state.

- [ ] **Step 4: Implement the codecs**

`fs_bytecode_export.{h,cpp}` and `fs_bytecode_loader.{h,cpp}`. Implementation notes (all verified in the design doc §1.3):
- All multi-byte integers little-endian via `StreamPeerBuffer`.
- Tagged encoder dispatch: Object-typed Variants classify by class (`FoundryScript` → `TAG_SCRIPT_REF`; other `Script` → `TAG_EXTERNAL_SCRIPT`; `Resource` with non-empty path → `TAG_EXTERNAL_RESOURCE`; `FSNativeClass` → `TAG_NATIVE_CLASS`; engine singleton identified via `Engine::get_singleton()->get_singleton_object_name(object)` → `TAG_ENGINE_SINGLETON`; `FSSpecializedClassHandle` → `TAG_SPECIALIZED_HANDLE`; anything else → error). Arrays/Dictionaries recurse element-wise and preserve typed-container metadata (element `ContainerType` script refs go through the same tagging). Leaves use `encode_variant(..., false)`.
- `FSDataType` fields to serialize: `kind`, `builtin_type`, `native_type`, nullability/handle/self flags, trait fields, type-parameter fields, recursive `container_element_types` + `type_arguments`, script reference as string-table (path, fqcn) — enumerate the actual field list from `fs_function.h:60-150` while implementing; the loader's script-resolution behavior (local class → raw pointer, external → `script_type_ref`) is deferred to Task 4 behind the resolver interface.
- Engine guard: write `VERSION_FULL_CONFIG` and `VERSION_HASH` (from `core/version.h`), `sizeof(real_t)`, and the opcode fingerprint `(uint32_t)FSFunction::OPCODE_END` + opcode count; `check_header` compares all of them.

- [ ] **Step 5: Build, run, iterate until green**

```bash
./bin/foundry.macos.editor.dev.arm64 --headless --test --test-case="*BytecodeCodec*" --force-colors
```

- [ ] **Step 6: Commit**

```bash
git add modules/foundry_script/fs_bytecode_format.h modules/foundry_script/fs_bytecode_export.* modules/foundry_script/fs_bytecode_loader.* modules/foundry_script/tests/test_bytecode_serialization.h
git commit -m "Add .fsb bytecode format codecs and string table"
```

---

### Task 2: Codegen fixup recording

**Goal:** `FSByteCodeGenerator` records, under `TOOLS_ENABLED`, symbolic descriptors for every process-bound pointer it emits, plus `{code_offset, global_name}` pairs for `OPCODE_STORE_GLOBAL`, stored on the produced `FSFunction`.

**Files:**
- Modify: `modules/foundry_script/fs_function.h` (export-fixup struct)
- Modify: `modules/foundry_script/fs_codegen.h:132`, `fs_byte_codegen.h:528`, `fs_byte_codegen.cpp` (`write_store_global` signature + recording in each `write_*` that appends to a pointer map)
- Modify: `modules/foundry_script/fs_compiler.cpp:800-805` (call site passes the global name)
- Test: `modules/foundry_script/tests/test_bytecode_serialization.h`

**Acceptance Criteria:**
- [ ] After compiling a script exercising operators, member set/get, builtin methods, constructors, utilities, native method calls, and an autoload access, every pointer-table entry on the compiled `FSFunction` has a descriptor at the same index, and every `OPCODE_STORE_GLOBAL` operand offset appears in `global_store_fixups` with the right name.
- [ ] Full existing suite still green (codegen behavior unchanged in release paths; new fields are TOOLS_ENABLED-only).

**Verify:** `./bin/foundry.macos.editor.dev.arm64 --headless --test --force-colors` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Add the fixup metadata struct to `FSFunction`**

In `fs_function.h`, next to the existing DEBUG_ENABLED name vectors (`fs_function.h:606-637`):

```cpp
#ifdef TOOLS_ENABLED
public:
	struct ExportFixups {
		struct OperatorKey {
			Variant::Operator op;
			Variant::Type left_type;
			Variant::Type right_type;
		};
		struct TypedNameKey {
			Variant::Type type;
			StringName name;
		};
		struct ConstructorKey {
			Variant::Type type;
			int constructor_index;
		};
		struct MethodBindKey {
			StringName class_name;
			StringName method_name;
		};
		struct GlobalStore {
			int code_offset; // index into `code` of the baked global-array operand
			StringName global_name;
		};
		Vector<OperatorKey> operators;
		Vector<TypedNameKey> setters;
		Vector<TypedNameKey> getters;
		Vector<Variant::Type> keyed_setters;
		Vector<Variant::Type> keyed_getters;
		Vector<Variant::Type> indexed_setters;
		Vector<Variant::Type> indexed_getters;
		Vector<TypedNameKey> builtin_methods;
		Vector<ConstructorKey> constructors;
		Vector<StringName> utilities;
		Vector<StringName> gds_utilities;
		Vector<MethodBindKey> method_binds;
		Vector<GlobalStore> global_stores;
		Vector<StringName> named_globals; // for export-time validation only
	};
	ExportFixups export_fixups;
#endif
```

(Indexed/keyed accessors key on type alone; setter/getter/builtin-method on type+name; utilities on name; `MethodBind` on class+method — resolution APIs verified in `core/variant/variant.h:630-835` and `ClassDB::get_method`.)

- [ ] **Step 2: Record at each codegen site**

In `fs_byte_codegen.cpp`, each place that inserts into one of the pointer maps (`operator_func_map`, `setters_map`, …, `method_bind_map` — declared at `fs_byte_codegen.h:112-124`) already has the symbolic information in hand (the DEBUG_ENABLED `add_debug_name` calls at those sites prove it). Mirror each insertion with a `#ifdef TOOLS_ENABLED` append into a generator-local `ExportFixups` staging member, then transfer to `function->export_fixups` in `write_end()` alongside the existing table transfers (`fs_byte_codegen.cpp:214-450`). Invariant to assert in tests: `staging.operators.size() == operator_func_map.size()` etc. — the descriptor vector index must equal the pointer-table index.

For constructors: the emit site receives the constructor index — record `{type, constructor_index}` directly.

- [ ] **Step 3: Extend `write_store_global`**

```cpp
// fs_codegen.h:132
virtual void write_store_global(const Address &p_dst, int p_global_index, const StringName &p_global_name) = 0;
```

Update `fs_byte_codegen.h:528` + its implementation: after appending the instruction, record `{ code.size() - 1 /* offset of the index operand just written */, p_global_name }` into staging `global_stores` under TOOLS_ENABLED (compute the exact operand offset from how the instruction is appended — verify against the emit code, not by assumption). Update the single call site `fs_compiler.cpp:800-805`, which has the identifier name in scope. Also transfer the existing TOOLS_ENABLED `named_globals` vector (`fs_byte_codegen.h:110`) into `export_fixups.named_globals` in `write_end()`.

- [ ] **Step 4: Add the recording test**

```cpp
TEST_CASE("[FoundryScript][BytecodeCodec] Codegen records export fixups") {
	// Compile (parser -> analyzer -> compiler, as FoundryScript::reload does) a source string:
	//   a member var with a Vector2 typed member access, `abs()` utility call,
	//   `"x".length()` builtin method, `Vector2(1, 2)` constructor, `+` operator,
	//   and a native call like `get_instance_id()`.
	// For the compiled main function F:
	//   CHECK(F->export_fixups.operators.size() == F->get_operator_funcs_count()); // etc. per table
}
```

Use the in-process compile pattern from existing tests (grep `FSCompiler::compile` under `modules/foundry_script/tests/` for the established fixture; `tests/fs_test_runner.cpp` has the canonical sequence).

- [ ] **Step 5: Build, run full suite, commit**

```bash
git add modules/foundry_script/fs_function.h modules/foundry_script/fs_codegen.h modules/foundry_script/fs_byte_codegen.* modules/foundry_script/fs_compiler.cpp modules/foundry_script/tests/test_bytecode_serialization.h
git commit -m "Record symbolic export fixups in Foundry Script codegen"
```

---

### Task 3: FSFunction serialize / deserialize / link, single-function round-trip

**Goal:** One compiled `FSFunction` round-trips through bytes and executes identically after linking, with hard errors on unresolvable fixups.

**Files:**
- Modify: `modules/foundry_script/fs_bytecode_export.cpp` (function writer)
- Modify: `modules/foundry_script/fs_bytecode_loader.cpp` (function reader + linker)
- Modify: `modules/foundry_script/fs_function.h` (friend declarations for exporter/loader)
- Test: `modules/foundry_script/tests/test_bytecode_serialization.h`

**Acceptance Criteria:**
- [ ] Serialized function excludes source text and all DEBUG_ENABLED-only fields; deserialized function's `code`, counts, and table sizes byte/size-match the original.
- [ ] `FSFunction::call` on the deserialized function returns results identical to the original for fixtures covering: arithmetic/operators, string builtin methods, constructors, utility calls, `MethodBind` native calls, lambdas (nested), typed assignments, iteration, default arguments, async/await state (a function with `await` deserializes; full resume behavior covered by the corpus in Task 5).
- [ ] Tampering with a fixup key (e.g. renaming a recorded builtin method to a nonexistent name in the buffer) fails the load with a message naming function + table + key — no crash.

**Verify:** `./bin/foundry.macos.editor.dev.arm64 --headless --test --test-case="*BytecodeFunction*" --force-colors`

**Steps:**

- [ ] **Step 1: Write the failing round-trip test**

```cpp
TEST_CASE("[FoundryScript][BytecodeFunction] Single function round-trip executes identically") {
	// compile source; for each member function:
	//   Vector<uint8_t> bytes = exporter.serialize_function(function);
	//   FSFunction *restored = loader.deserialize_function(bytes, script, r_error);
	//   compare code/counts; call both with same args on a fresh instance; CHECK results equal.
}
```

- [ ] **Step 2: Implement the writer**

Serialize, in order (field inventory verified at `fs_function.h:325-691`, design doc §1.5): `name`, `_static`, `_initial_line`, `_argument_count`, `_vararg_index`, `_stack_size`, `_instruction_args_size`, `temporary_slots`, `argument_types`/`return_type` (Task 1 codec), `method_info` (encode via `Dictionary` from `MethodInfo::to_dict`-equivalent field encoding; `MethodInfo::from_dict` exists at `core/object/object.h:265`), `rpc_config` (plain Variant), `code` (raw i32 array), `default_arguments`, `global_names` + `builtin_method_names` (string-table indices), constant pool (tagged codec), the twelve fixup tables from `export_fixups`, `global_stores`, and `lambdas` recursively (children serialized depth-first; a lambda entry carries its `{capture_count, use_self}` from `script->lambda_info`). Deduplicate lambda pointers.

Skip entirely: `source` (write the script path only), `func_cname`, `*_names` debug vectors, `stack_debug`, profiling fields.

- [ ] **Step 3: Implement the reader + linker**

Resolution per table (APIs verified in `core/variant/variant.h`): operators → `Variant::get_validated_operator_evaluator`; setters/getters → `get_member_validated_setter/getter`; keyed/indexed → corresponding validated accessors; builtin methods → `get_validated_builtin_method`; constructors → `get_validated_constructor(type, index)` with an argument-signature sanity check; utilities → `get_validated_utility_function`; gds utilities → `FSUtilityFunctions::get_function`; method binds → `ClassDB::get_method(class_name, method_name)`. Any null resolution → `ERR_FAIL_V_MSG` naming script path, function name, table, key, and the hint "was the game exported with a matching engine build?".

Rewrite `code[global_store.code_offset]` with `FSLanguage::get_singleton()`'s global map index for `global_name` (map built at `foundry_script.cpp:2727-2802`); missing name → hard error.

Re-establish every `_*_count` / `_*_ptr` mirror field exactly as `FSByteCodeGenerator::write_end` does at `fs_byte_codegen.cpp:232-436` — extract that mirror-setup into a shared static helper `FSFunction::setup_runtime_pointers()` called from both places so the invariants cannot drift.

- [ ] **Step 4: Build, run, iterate until green; commit**

```bash
git add modules/foundry_script/fs_bytecode_export.cpp modules/foundry_script/fs_bytecode_loader.cpp modules/foundry_script/fs_function.h modules/foundry_script/fs_byte_codegen.cpp modules/foundry_script/tests/test_bytecode_serialization.h
git commit -m "Serialize and link FSFunction bytecode round-trip"
```

---

### Task 4: Whole-script graph serialization

**Goal:** `FSBytecodeExporter::serialize(script)` / `FSBytecodeLoader::load_skeleton` + `load_full` round-trip a complete `FoundryScript` — inheritance, inner classes, members, signals, constants, static variables, lambdas, generics, traits/witnesses — into a runnable script.

**Files:**
- Modify: `modules/foundry_script/fs_bytecode_export.{h,cpp}`, `fs_bytecode_loader.{h,cpp}`
- Modify: `modules/foundry_script/foundry_script.h` (friend access; `compiled_binary` flag + stored bytecode path)
- Test: `modules/foundry_script/tests/test_bytecode_serialization.h`

**Acceptance Criteria:**
- [ ] Round-trip preserves and re-links: `member_indices` (full `MemberInfo`), `_signals`, `constants` (incl. enum dictionaries and subclass refs as intra-file class indices), `subclasses` tree, merged `rpc_config`, static variables + `static_initializer` execution, `implicit_initializer`/`implicit_ready`, all annotation-usage maps, `type_parameters` + `type_parameter_bindings_by_ancestor` (re-keyed from serialized ancestor references), trait lists + conformance witnesses re-registered with `FSConformanceRegistry`.
- [ ] A script `extends` another script in a second file: base resolves through the resolver interface; a preload constant resolves to a loaded resource at link.
- [ ] Load order matches `reload()`'s tail: `_static_default_init()` → `valid = true` → `_static_init()` (mirrors `foundry_script.cpp:1211-1216`, `fs_compiler.cpp:4492`).

**Verify:** `./bin/foundry.macos.editor.dev.arm64 --headless --test --test-case="*BytecodeScript*" --force-colors`

**Steps:**

- [ ] **Step 1: Write failing tests** — round-trip fixtures with: inner classes referenced as constants, two-file `extends` chain, signals/enums/annotations/rpc, static variables + static initializer, `@onready`, preload constants, a generic class, a trait conformance (`extend X uses Trait` fixture — copy shapes from `modules/foundry_script/tests/scripts/` trait fixtures). Instantiate the restored script, call methods, emit signals, check `get_script_method_list`/`has_method`.

- [ ] **Step 2: Implement per-class body writer/reader** — field inventory is design doc §1.2 (from `FSCompiler::_prepare_compilation` at `fs_compiler.cpp:3825-4328` and `_compile_class` at `:4330-4496`). Intra-file script references (subclass constants, `FSDataType` pointing at a sibling class, `type_parameter_bindings_by_ancestor` keys) encode as preorder class indices; external ones as (path, fqcn) external refs. Skeleton section carries the class tree exactly as `make_scripts` builds it (`fs_compiler.cpp:4512-4551`): fqcn, local/global name, base ref, native class name, flags, `trait_type_name`, `simplified_icon_path`.

- [ ] **Step 3: Implement `load_full` ordering** — the seven ordered steps in design doc §2.3: plain data first; base links second (recursing through the resolver); functions third; fixup/constant/`FSDataType`/global-store link pass fourth; witness re-registration fifth (`FSConformanceRegistry::register_runtime_witnesses`, entries per `fs_compiler.cpp:4679-4788`); finalize sixth (`_static_default_init`, `valid = true`, `_static_init`); errors propagate with script path + detail. `FSDataType` local-class references link as raw pointer without a strong ref (rule at `foundry_script.h:140-144`); external ones as `script_type_ref`.

- [ ] **Step 4: Build, run, commit**

```bash
git commit -am "Serialize full FoundryScript graph to .fsb"
```

---

### Task 5: Fixture-corpus round-trip mode

**Goal:** Every runtime fixture in `modules/foundry_script/tests/scripts/` passes when executed through compile → serialize → deserialize → link → run, diffing the same `.out` files as the text path.

**Files:**
- Modify: `modules/foundry_script/tests/fs_test_runner.h` (new run-mode flag alongside the `use_binary_tokens` one at `fs_test_runner.h:139`)
- Modify: `modules/foundry_script/tests/fs_test_runner.cpp`
- Modify: `modules/foundry_script/tests/fs_test_runner_suite.h` (third parameterized pass, mirroring `--use-binary-tokens` at `fs_test_runner_suite.h:62-70`)

**Acceptance Criteria:**
- [ ] All `runtime/` fixtures produce byte-identical `.out` results in the bytecode mode (parser/analyzer-error fixtures are excluded — they never reach compilation; keep the exclusion rule in the runner, not per-fixture).
- [ ] Full suite green.

**Verify:** `./bin/foundry.macos.editor.dev.arm64 --headless --test --force-colors` → SUCCESS, with the new suite visibly executing (check its case count > 0).

**Steps:**

- [ ] **Step 1:** Add `bool use_compiled_bytecode` plumbing through `FSTestRunner` construction, mirroring `use_binary_tokens` exactly. In the runner's script-preparation step, when the flag is set and the fixture compiled successfully: serialize the compiled script, clear it, deserialize into a fresh `FoundryScript` via the loader, and execute that instead.
- [ ] **Step 2:** Add the third pass in `fs_test_runner_suite.h`, mirroring the binary-tokens block at `:62-70`. Respect the known suite-filter caveat: run via full `--test` (not `--test-case` filters) once before committing, since generic analyzer tests lack a TEST_SUITE tag.
- [ ] **Step 3:** Fix every fixture failure — expect to discover 1-2 unhandled constant/object kinds here (design doc risk #2); extend the tagged encoder rather than special-casing fixtures.
- [ ] **Step 4:** Full suite run, commit: `git commit -am "Run fixture corpus through .fsb round-trip mode"`.

---

### Task 6: FSCache and resource-loader integration

**Goal:** A `.fsb` file on disk loads end-to-end through `ResourceLoader` — shallow/full cache branches, dependencies, cycles, reload guard — with the engine-version guard enforced.

**Files:**
- Modify: `modules/foundry_script/fs_cache.cpp` (`get_shallow_script:438`, `get_full_script:489-515`, `FSParserRef::raise_status:79`)
- Modify: `modules/foundry_script/foundry_script.h/.cpp` (`compiled_binary` flag; `reload()` guard; `get_recognized_extensions:3824`, `get_resource_type:3833`, `get_dependencies:3841`)
- Test: `modules/foundry_script/tests/test_bytecode_serialization.h`

**Acceptance Criteria:**
- [ ] `ResourceLoader::load("res://….fsb")` (and a `.fs` path remapped to `.fsb`) yields a valid, instantiable script; `get_dependencies` returns the header dependency list without parsing.
- [ ] Two scripts that preload each other (cyclic) both load — publish-before-link ordering preserved (`fs_cache.cpp:517-527`).
- [ ] `script->reload()` on a bytecode-backed script re-links from the `.fsb` (or no-ops returning OK) — it never attempts to parse; `FSParserRef::raise_status` returns `ERR_UNAVAILABLE` for `.fsb`-backed paths.
- [ ] A `.fsb` with a corrupted engine guard fails `ResourceLoader::load` with the version-mismatch message.

**Verify:** `./bin/foundry.macos.editor.dev.arm64 --headless --test --test-case="*BytecodeCache*" --force-colors` plus full suite (cache-sync tests at `fs_test_runner_suite.h:96-113` must stay green).

**Steps:**

- [ ] **Step 1: Write failing tests** — write a temp-dir `.fsb` from a compiled script, load via `ResourceLoader`, instantiate, call; cyclic preload pair; corrupted-guard rejection; `reload()` guard; `get_dependencies`.
- [ ] **Step 2: Cache branches** — in `get_shallow_script`, mirror the `.fsc` branch (`fs_cache.cpp:438-443`): `remapped_path.has_extension("fsb")` → `FSBytecodeLoader::load_skeleton(remapped_path, script)`, skip `get_parser` + `make_scripts`. In `get_full_script`, replace the `reload(true)` call with `FSBytecodeLoader::load_full(...)` for `.fsb`, inside the existing `WorkerThreadPool::thread_enter_unlock_allowance_zone` block (`fs_cache.cpp:511-515`), keeping publication ordering untouched. The loader's production resolver routes script references through `FSCache::get_full_script` and resources through `ResourceLoader::load`.
- [ ] **Step 3: Script + loader plumbing** — `compiled_binary` flag + stored remapped path on `FoundryScript`; `reload()` early-path for bytecode-backed scripts; add `"fsb"` to the three loader methods; `get_dependencies` branches on the extension and reads the dependency section.
- [ ] **Step 4: Full suite, commit**: `git commit -am "Load .fsb compiled scripts through FSCache and ResourceLoader"`.

---

### Task 7: Export preset mode and export plugin

**Goal:** Selecting "Compiled bytecode" in an export preset ships `.fsb` files (path-remapped like `.fsc`), fails the export on any compile error, and fails the export when a scene contains a built-in script.

**Files:**
- Modify: `editor/export/editor_export_preset.h:56-60`, `editor/export/editor_export_preset.cpp:145-147` (+ property hint string in `_get_property_list` if it enumerates modes)
- Modify: `editor/export/project_export.cpp:1920-1922`
- Modify: `modules/foundry_script/register_types.cpp:84-122`

**Acceptance Criteria:**
- [ ] `MODE_SCRIPT_COMPILED_BYTECODE` appended (never inserted) to the enum; UI dropdown shows it.
- [ ] Exporting a test project in the new mode produces a PCK containing only `.fsb` scripts (no `.fs`/`.fsc`), and the exported game runs.
- [ ] A script with a compile error fails the export with the script path + error text; a scene with an embedded FoundryScript fails the export naming the scene.
- [ ] `.fsc` and text modes behave exactly as before.

**Verify:** Scripted export of a minimal test project (create one under the scratchpad with `project.foundry`, two scripts, a scene) via `--headless --export-release`; inspect the PCK file list; run the exported build headless. Plus full test suite.

**Steps:**

- [ ] **Step 1: Enum + UI** — append the enum value, `BIND_ENUM_CONSTANT`, `script_mode->add_item(TTR("Compiled Bytecode (no source shipped)"), (int)EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);`. Grep `editor_export_preset.cpp` for a mode-enumerating property hint string and extend it.
- [ ] **Step 2: Export plugin branch** — in `EditorExportFoundryScript::_export_file` (`register_types.cpp:100`): for `.fs` in the new mode, `FSCache::get_full_script(p_path, err)`; on `err != OK || !script->is_valid()` report via `get_export_platform()->add_message(EXPORT_MESSAGE_ERROR, …)` — verify during implementation that an error message marks the export failed in this fork's `EditorExportPlatform` (design doc risk #3); if it does not, add a failure flag surfaced in `_export_end` that aborts. Otherwise `FSBytecodeExporter::serialize(script)` → `add_file(p_path.get_basename() + ".fsb", data, true)`. Compile with call-stack tracking per target profile: force `track_call_stack` off for release exports before compiling, restore after (editor forces it on at `foundry_script.cpp:3732`); validate every `export_fixups.named_globals` entry against the template-available named-globals list (design doc §3.3), erroring on editor-only names.
- [ ] **Step 3: Built-in script check** — in the same plugin, handle `.tscn`/`.scn`/`.res` files when the mode is compiled-bytecode: load the scene's resource and scan sub-resources for FoundryScript instances (or text-scan `.tscn` for `sub_resource type="FoundryScript"`-equivalent type tags — pick whichever is robust for both text and binary scenes during implementation); on hit, `add_message(EXPORT_MESSAGE_ERROR, …)` naming the scene path.
- [ ] **Step 4: End-to-end export verification** — build a scratchpad test project; export with the new mode; assert PCK contents and run the export. Commit: `git commit -am "Add compiled-bytecode script export mode"`.

---

### Task 8: Hardening — bytecode verifier, corrupt inputs, leak assertions

**Goal:** A hostile or corrupted `.fsb` cannot crash the release VM, and a produced `.fsb` provably contains no source text or local identifiers.

**Files:**
- Modify: `modules/foundry_script/fs_bytecode_loader.cpp` (verifier pass)
- Test: `modules/foundry_script/tests/test_bytecode_serialization.h`

**Acceptance Criteria:**
- [ ] Link-time verifier walks the opcode stream of every deserialized function, decodes 24-bit packed addresses (`fs_function.h:499-517`), and rejects any operand index out of range for its address space (stack/constant/member), any jump target outside `code`, and any table index ≥ its table size — release VM has no checks (`fs_vm.cpp:1147-1160`), so the loader is the only line of defense.
- [ ] Fuzz-ish tests: truncation at every section boundary, bit-flips in section offsets/string indices/code operands → load error, never a crash (run a loop of randomized single-byte corruptions over a real `.fsb`, seeded constant for determinism).
- [ ] Leak test: compile a fixture whose source contains distinctive markers (`UNIQUE_LOCAL_VARIABLE_MARKER` as a local, `UNIQUE_PARAMETER_MARKER` as a parameter used only positionally, a distinctive comment, a distinctive source substring); assert the serialized buffer contains none of them, while a member name marker IS present (documents the residual surface).
- [ ] Format-version pin test: `CHECK(FSBytecodeFormat::FORMAT_VERSION == 1);` with a comment explaining it must be bumped on any layout change (pattern: `TOKENIZER_VERSION` pin at `tests/test_foundry_script.cpp:76`).

**Verify:** `./bin/foundry.macos.editor.dev.arm64 --headless --test --test-case="*BytecodeHardening*" --force-colors` plus full suite; finish with `pre-commit run --all-files` and a `dev_mode=yes` CI-parity build (watch `-Wshadow`).

**Steps:**

- [ ] **Step 1:** Write the failing hardening tests (corruption loop, leak markers, version pin).
- [ ] **Step 2:** Implement the verifier as the last step of function deserialization, before `setup_runtime_pointers()`. Note `method_info` argument names remain by design; the leak test's parameter marker must therefore use a local, not a declared parameter name — parameter names in `method_info` are residual surface, documented in `fs_bytecode_format.h`.
- [ ] **Step 3:** Full suite + `dev_mode=yes` parity build + pre-commit. Commit: `git commit -am "Harden .fsb loader against corrupt input and identifier leaks"`.

---

## Self-Review Notes

- Spec coverage: format/container (T1), fixups + STORE_GLOBAL (T2/T3), full graph + witnesses + static variables (T4), corpus parity (T5), cache/loader/cycles/reload guard/dependencies/engine guard (T6), preset + plugin + compile-profile + built-in-script failure + named-global validation (T7), verifier + leak proof + version pin (T8). No spec section is untasked.
- The `.fsb` never ships from the editor filesystem (exports generate it), so no editor import interaction is needed; `FSParserRef::raise_status` guard covers the defensive case.
- GRAMMAR.md: untouched — no language change.
- After each substantial task lands, follow the standing practice: run a Codex adversarial review round (fresh agent pinned to current HEAD) before considering the task merged-quality.
