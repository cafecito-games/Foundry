# FoundryScript Typed Rest Parameters Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Support `...args: Array[T]` as a statically checked and concretely runtime-enforced FoundryScript rest
parameter, including generic inference, typed variadic Callables, compatibility rules, and authoring tools.

**Architecture:** Preserve the complete rest Array type in FoundryScript-owned signature metadata and compiled
`FSFunction` state while leaving engine-wide `MethodInfo` unchanged. Direct/callable analysis validates each surplus
argument against the element type; the VM constructs a concrete typed Array when the element is reifiable and retains
the existing runtime erasure boundary when it depends on a generic method parameter.

**Tech Stack:** C++17 engine/module code, FoundryScript `.fs` fixtures, doctest C++ tests, LSP/editor tests, bytecode
export/load tests, and SCons/Ninja through `scripts/agent_build.py`.

**Design:** `docs/superpowers/specs/2026-08-02-foundry-script-typed-rest-parameters-design.md`

---

## Execution rules

- Implement each numbered task as one native GitHub subissue and one focused PR.
- Start every task from current `develop` in its own worktree.
- T1 must merge first. T2, T3, and T4 may then run in parallel. T5 starts after all three merge.
- Do not change `MethodInfo`, ClassDB, or GDExtension structures.
- Do not reify generic method type arguments. A method-dependent rest element stays runtime-erased.
- Use existing fixed-parameter conversion, nullability, strict-dynamic, and error vocabulary; do not invent a second
  compatibility system for rest elements.
- Add observable tests before implementation. Never test by reading implementation source text.
- Do not regenerate unrelated script `.out` or formatter fixtures.
- Generated scratch files use the wrapper-provided `.test_scratch` path.
- Use only the command-first Foundry CLI shown in the repository instructions.
- Iterate with the Ninja wrapper; run the native strict wrapper before every implementation PR handoff.
- Stage only the files owned by the current task.

## Dependency graph

```text
T1 concrete core/runtime/bytecode
 ├── T2 generic inference and erasure ──┐
 ├── T3 Callable signatures/transforms ─┼── T5 tooling/docs/full acceptance
 └── T4 override/trait variance ────────┘
```

## File responsibility map

- `modules/foundry_script/fs_parser.h`: AST and rich `DataType` storage.
- `modules/foundry_script/fs_parser.cpp`: explicit Callable rest syntax in T3; function rest syntax already exists.
- `modules/foundry_script/fs_parser_data_type.cpp`: callable display, substitution, property-hint encoding, and
  traversal.
- `modules/foundry_script/fs_analyzer.cpp`: declaration resolution, direct signature lookup, generic application,
  override/trait compatibility, and external signature decoding.
- `modules/foundry_script/fs_analyzer_call_validation.{h,cpp}`: per-argument validation, generic binding, callable
  signature extraction, `callv`, and transformed callable types.
- `modules/foundry_script/fs_analyzer_surface.cpp`: alpha-equivalence and completed external surfaces.
- `modules/foundry_script/fs_compiler.cpp`: lower the resolved rest Array type into `FSFunction`.
- `modules/foundry_script/fs_function.h` and `fs_vm.cpp`: compiled rest metadata and call-prologue packing.
- `modules/foundry_script/fs_bytecode_{format,export,loader,verifier}.*`: wire format, bounds, and malformed-input
  rejection.
- `modules/foundry_script/fs_editor.cpp`, `editor/fs_docgen.cpp`, `editor/fs_refactoring.cpp`, and
  `language_server/*`: authoring surfaces.
- `modules/foundry_script/GRAMMAR.md`: normative function and Callable rules.
- `modules/foundry_script/tests/test_foundry_script_type.h`: focused rich-type and analyzer acceptance.
- Existing script fixture trees: observable parser, analyzer, runtime, format, completion, and LSP behavior.

## Task 1: Deliver concrete typed-rest analysis, VM packing, and bytecode

**Dependency:** none.

**Issue boundary:** A complete vertical slice for concrete `Array[T]` function/lambda rest parameters. Do not add
generic inference, explicit variadic Callable type syntax, broadening override variance, or editor/LSP polish here.
Exact typed-rest override signatures may remain the only accepted typed pairing until T4.

**Files:**

- Modify: `modules/foundry_script/GRAMMAR.md`
- Modify: `modules/foundry_script/fs_parser.h`
- Modify: `modules/foundry_script/fs_parser_data_type.cpp`
- Modify: `modules/foundry_script/fs_analyzer.h`
- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Modify: `modules/foundry_script/fs_analyzer_call_validation.h`
- Modify: `modules/foundry_script/fs_analyzer_call_validation.cpp`
- Modify: `modules/foundry_script/fs_compiler.cpp`
- Modify: `modules/foundry_script/fs_function.h`
- Modify: `modules/foundry_script/fs_vm.cpp`
- Modify: `modules/foundry_script/fs_bytecode_format.h`
- Modify: `modules/foundry_script/fs_bytecode_export.cpp`
- Modify: `modules/foundry_script/fs_bytecode_loader.cpp`
- Modify: `modules/foundry_script/fs_bytecode_verifier.cpp`
- Modify: `modules/foundry_script/tests/test_foundry_script_type.h`
- Modify: `modules/foundry_script/tests/test_bytecode_serialization.h`
- Modify: `modules/foundry_script/tests/scripts/analyzer/errors/variadic_functions.fs`
- Modify: `modules/foundry_script/tests/scripts/analyzer/errors/variadic_functions.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/typed_rest_parameter_concrete.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_argument.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_argument.out`
- Create: `modules/foundry_script/tests/scripts/runtime/features/typed_rest_parameter_concrete.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/errors/typed_rest_parameter_dynamic_mismatch.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/errors/typed_rest_parameter_dynamic_mismatch.out`

- [ ] **Step 1: Add the failing declaration tests to the existing type suite**

Add these cases beside the existing `analyze_source()` tests in `test_foundry_script_type.h`:

```cpp
TEST_CASE("[Modules][FoundryScript][TypedRestParameter] Analyzer accepts concrete typed rest arrays") {
	CHECK_EQ(analyze_source(
				"func collect(prefix: String, ...values: Array[int]) -> int:\n"
				"\treturn values.size()\n"
				"func test() -> int:\n"
				"\treturn collect(\"n\", 1, 2, 3)\n"),
			OK);
}

TEST_CASE("[Modules][FoundryScript][TypedRestParameter] Analyzer rejects a wrong concrete rest element") {
	CHECK_NE(analyze_source(
				"func collect(...values: Array[int]) -> void:\n"
				"\tpass\n"
				"func test() -> void:\n"
				"\tcollect(1, \"bad\", 3)\n"),
			OK);
}
```

- [ ] **Step 2: Run the focused test and verify the old analyzer guard fails it**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypedRestParameter*"
```

Expected: the positive test fails with `Typed arrays are currently not supported for the rest parameter.`

- [ ] **Step 3: Add the optional rich rest slot to analyzer `DataType`**

Add this beside `method_parameter_types` and copy it in the existing copy/assignment path:

```cpp
Vector<DataType> method_rest_parameter_type; // Empty or exactly one resolved Array DataType.
```

Extend `DataType::substitute()`, signature traversal, type-parameter detection, alpha equality, strict identity, and
completion traversal with the same empty-or-one invariant. Use a small accessor rather than repeated indexing:

```cpp
bool has_method_rest_parameter_type() const {
	return method_rest_parameter_type.size() == 1;
}

const DataType &get_method_rest_parameter_type() const {
	DEV_ASSERT(has_method_rest_parameter_type());
	return method_rest_parameter_type[0];
}
```

Do not alter `MethodInfo::arguments`. Add a doctest that copies and substitutes a callable `DataType` containing
`Array[int]` in the rest slot and checks that the slot survives.

- [ ] **Step 4: Thread the resolved rest Array through direct function lookup**

Extend `FSAnalyzer::get_function_signature()` with an optional out parameter placed after the fixed parameter list:

```cpp
FSParser::DataType *r_rest_parameter_type = nullptr
```

At every FoundryScript `FunctionNode` return path:

```cpp
if (r_rest_parameter_type != nullptr && found_function->rest_parameter != nullptr) {
	*r_rest_parameter_type = substitute_member_type(
			found_function->rest_parameter->get_datatype(), specialized_base, found_function, &parameter_self_type);
}
```

Initialize the out value to an unset `DataType` at function entry. MethodInfo/native paths leave it unset while still
setting `METHOD_FLAG_VARARG`.

- [ ] **Step 5: Remove the obsolete typed-array rejection and retain the outer Array check**

Change the declaration validation to:

```cpp
if (specified_type.kind != FSParser::DataType::BUILTIN ||
		specified_type.builtin_type != Variant::ARRAY) {
	push_error(vformat(R"(The rest parameter type must be "Array", but "%s" is specified.)",
					specified_type.to_string()),
			p_function->rest_parameter->datatype_specifier);
}
```

Delete only the `Typed arrays are currently not supported` branch. Update `variadic_functions.fs/.out` so the
`Array[int]` declaration no longer contributes an error; retain the non-Array declaration failure and override errors.

- [ ] **Step 6: Validate surplus direct arguments against the concrete element type**

Add an optional rest Array parameter to `validate_call_arg()` and `validate_callable_array_literal_args()`:

```cpp
const FSParser::DataType *p_rest_parameter_type = nullptr
```

Extract the expected element once:

```cpp
const FSParser::DataType *rest_element_type = nullptr;
if (p_rest_parameter_type != nullptr &&
		p_rest_parameter_type->kind == FSParser::DataType::BUILTIN &&
		p_rest_parameter_type->builtin_type == Variant::ARRAY &&
		p_rest_parameter_type->has_container_element_type(0) &&
		!p_rest_parameter_type->get_container_element_type(0).is_variant()) {
	rest_element_type = &p_rest_parameter_type->container_element_types[0];
}
```

Refactor the existing fixed-argument body into a helper taking the expected type, expression, one-based argument
number, and function name. In the loop, select a fixed parameter while `i < fixed_count`; otherwise select
`rest_element_type`. Preserve synthesized-default skipping only for fixed positions. The helper must retain constant
coercion, `Self`, nullable, strict dynamic, unsafe warnings, and implicit conversion behavior.

Add the script error fixture:

```foundry
func collect(prefix: String, ...values: Array[int]) -> void:
	pass

func test() -> void:
	collect("ok", 1, "bad", 3)
```

Its expected diagnostic identifies argument 3 and expected `int`.

- [ ] **Step 7: Add compiled rest metadata without changing MethodInfo**

Add to `FSFunction` beside `argument_types`:

```cpp
FSDataType rest_parameter_type;
```

Expose a const accessor for tests. In `_parse_function()`, retain the converted rest type when adding the local:

```cpp
FSDataType compiled_rest_type;
compiled_rest_type = _gdtype_from_datatype(
		p_func->rest_parameter->get_datatype(), codegen.script);
vararg_addr = codegen.add_local(p_func->rest_parameter->identifier->name, compiled_rest_type);
method_info.flags |= METHOD_FLAG_VARARG;
```

Declare `compiled_rest_type` next to `vararg_addr`, then assign it to `gd_function->rest_parameter_type` immediately
after `write_end()` returns. Keep `method_info.arguments` unchanged and keep `_vararg_index` as the arity/stack-slot
marker.

- [ ] **Step 8: Construct and validate a concrete typed Array in the VM call prologue**

Extract the fixed-argument conversion code into a private helper that can validate one `Variant` against an
`FSDataType` and set `Callable::CallError`. Reuse it for surplus elements. For a reifiable element type:

```cpp
Array vararg;
const FSDataType &array_type = rest_parameter_type;
FSDataType element_data;
if (array_type.has_container_element_type(0)) {
	element_data = array_type.get_container_element_type(0);
	const ContainerType element_type = element_data.to_container_type();
	if (element_type.builtin_type != Variant::NIL) {
		vararg.set_typed(element_type);
	}
}
vararg.resize(rest_count);
for (int i = 0; i < rest_count; i++) {
	Variant value;
	if (!_convert_call_argument(*p_args[_argument_count + i], element_data, value,
				r_err, _argument_count + i)) {
		call_depth--;
		return _get_default_variant_for_data_type(return_type);
	}
	vararg.set(i, value);
}
stack[_vararg_index] = vararg;
```

Use `FSDataType::to_container_type()` exactly; when it returns `Variant::NIL` (including existing nullable-element
erasure), retain an untyped packed Array but still validate every incoming value against `element_data`. A compiler-
erased method type parameter arrives as the default Variant element and remains gradual. Build the Array in a local and
assign the stack only after all concrete elements validate.

- [ ] **Step 9: Add observable runtime acceptance**

Create `typed_rest_parameter_concrete.fs`:

```foundry
func collect(...values: Array[int]) -> int:
	Utils.check(values.is_typed())
	Utils.check(values.get_typed_builtin() == TYPE_INT)
	return values.size()

func test() -> void:
	Utils.check(collect() == 0)
	Utils.check(collect(1, 2, 3) == 3)
	var callback := collect
	Utils.check(callback.call(4, 5) == 2)
```

Add the dynamic mismatch under `runtime/errors` so the fixture runner captures the expected VM error independently:

```foundry
var body_calls := 0

func collect(...values: Array[int]) -> void:
	body_calls += 1

func test() -> void:
	var callback := collect
	callback.call("bad")
	print(body_calls)
```

The `.out` begins with `FS_TEST_RUNTIME_ERROR`, contains the invalid argument error for argument 1 expecting `int`, and
prints `0`, proving the body did not execute. Add `Array[Node?]` coverage that compares its runtime `is_typed()` result
to an ordinary `Array[Node?]`; both follow existing nullable-element erasure while the analyzer still rejects an int.

- [ ] **Step 10: Serialize and validate the compiled rest type**

Increment:

```cpp
static constexpr uint32_t FORMAT_VERSION = 8;
```

Write `rest_parameter_type` after fixed `argument_types` and before `return_type`; read it in the same position. Add
loader validation:

```cpp
if (p_function->is_vararg()) {
	ERR_FAIL_COND_V_MSG(
			p_function->rest_parameter_type.kind != FSDataType::BUILTIN ||
					p_function->rest_parameter_type.builtin_type != Variant::ARRAY,
			ERR_INVALID_DATA,
			vformat("Malformed compiled function '%s': variadic rest type is not Array.", function_name));
} else {
	ERR_FAIL_COND_V_MSG(p_function->rest_parameter_type.has_type(), ERR_INVALID_DATA,
			"Malformed non-variadic function contains a rest type.");
}
```

Extend bytecode equality helpers and malformed-stream tests. Export a concrete typed-rest script, clear source/parser
caches, load the bytecode, invoke zero/many arguments, and assert the body observes the same typed Array.

- [ ] **Step 11: Update the normative function-rest rule**

In `GRAMMAR.md` state that a function/lambda rest annotation resolves to `Array` or `Array[T]`, that each surplus call
argument is checked against `T`, and that concrete empty calls receive a typed empty Array. Do not add Callable syntax;
T3 owns that grammar.

- [ ] **Step 12: Run focused and regression verification**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypedRestParameter*"
python3 scripts/agent_build.py --backend ninja --test --case "*Bytecode*"
python3 scripts/agent_build.py --backend ninja --test --case "*FoundryScriptType*"
```

Expected: all focused doctests and script fixtures pass; existing untyped variadic runtime output is unchanged.

- [ ] **Step 13: Run the native strict handoff gate**

```sh
python3 scripts/agent_build.py --test --case "*TypedRestParameter*"
```

Expected: the native backend completes with no warnings or test failures.

- [ ] **Step 14: Commit Task 1**

```sh
git add modules/foundry_script/GRAMMAR.md \
  modules/foundry_script/fs_parser.h modules/foundry_script/fs_parser_data_type.cpp \
  modules/foundry_script/fs_analyzer.h modules/foundry_script/fs_analyzer.cpp \
  modules/foundry_script/fs_analyzer_call_validation.h modules/foundry_script/fs_analyzer_call_validation.cpp \
  modules/foundry_script/fs_compiler.cpp modules/foundry_script/fs_function.h modules/foundry_script/fs_vm.cpp \
  modules/foundry_script/fs_bytecode_format.h modules/foundry_script/fs_bytecode_export.cpp \
  modules/foundry_script/fs_bytecode_loader.cpp modules/foundry_script/fs_bytecode_verifier.cpp \
  modules/foundry_script/tests/test_foundry_script_type.h \
  modules/foundry_script/tests/test_bytecode_serialization.h \
  modules/foundry_script/tests/scripts/analyzer/errors/variadic_functions.fs \
  modules/foundry_script/tests/scripts/analyzer/errors/variadic_functions.out \
  modules/foundry_script/tests/scripts/analyzer/features/typed_rest_parameter_concrete.fs \
  modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_argument.fs \
  modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_argument.out \
  modules/foundry_script/tests/scripts/runtime/features/typed_rest_parameter_concrete.fs \
  modules/foundry_script/tests/scripts/runtime/errors/typed_rest_parameter_dynamic_mismatch.fs \
  modules/foundry_script/tests/scripts/runtime/errors/typed_rest_parameter_dynamic_mismatch.out
git commit -m "feat(foundry_script): Support concrete typed rest parameters"
```

## Task 2: Add generic typed-rest inference and document erasure

**Dependency:** Task 1.

**Issue boundary:** Generic method binding, bounds, substitution, strict diagnostics, and runtime-erasure acceptance.
Do not add Callable type grammar or change the generic method runtime ABI.

**Files:**

- Modify: `modules/foundry_script/fs_analyzer_call_validation.h`
- Modify: `modules/foundry_script/fs_analyzer_call_validation.cpp`
- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Modify: `modules/foundry_script/fs_parser_data_type.cpp`
- Modify: `modules/foundry_script/fs_compiler.cpp`
- Modify: `modules/foundry_script/tests/test_foundry_script_type.h`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/typed_rest_parameter_generic.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_generic_conflict.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_generic_conflict.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_generic_empty.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_generic_empty.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_generic_bound.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_generic_bound.out`
- Create: `modules/foundry_script/tests/scripts/runtime/features/typed_rest_parameter_generic_erasure.fs`

- [ ] **Step 1: Add failing inference tests for rest-only and fixed-plus-rest constraints**

Add C++ analyzer cases:

```cpp
CHECK_EQ(analyze_source(
		"func collect[T](...values: Array[T]) -> Array[T]:\n"
		"\treturn values\n"
		"var ints: Array[int] = collect(1, 2, 3)\n"), OK);

CHECK_EQ(analyze_source(
		"func prepend[T](first: T, ...values: Array[T]) -> Array[T]:\n"
		"\treturn [first] + values\n"
		"var ints: Array[int] = prepend(0, 1, 2)\n"), OK);

CHECK_NE(analyze_source(
		"func collect[T](...values: Array[T]) -> Array[T]:\n"
		"\treturn values\n"
		"var bad := collect(1, \"two\")\n"), OK);
```

- [ ] **Step 2: Run the focused test and verify rest-only `T` is unresolved**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypedRestParameter*Generic*"
```

Expected: `collect(1, 2, 3)` reports that `T` cannot be inferred because inference currently walks only fixed
parameters.

- [ ] **Step 3: Pass the optional rest Array through generic application**

Change the call context API to receive the rest type by mutable reference:

```cpp
void apply_generic_method_call(
		FSParser::CallNode *p_call,
		FSParser::FunctionNode *p_function,
		List<FSParser::DataType> &r_par_types,
		FSParser::DataType &r_rest_parameter_type,
		FSParser::DataType &r_return_type);
```

The unset rest value is harmless for fixed-only generic methods.

- [ ] **Step 4: Collect bindings from every surplus argument**

After the fixed-parameter loop and before conflict reporting:

```cpp
if (r_rest_parameter_type.kind == FSParser::DataType::BUILTIN &&
		r_rest_parameter_type.builtin_type == Variant::ARRAY &&
		r_rest_parameter_type.has_container_element_type(0)) {
	const FSParser::DataType &element_type =
			r_rest_parameter_type.get_container_element_type(0);
	for (int i = r_par_types.size(); i < p_call->arguments.size(); i++) {
		if (p_call->arguments[i] != nullptr) {
			collect_type_parameter_bindings(
					element_type, p_call->arguments[i]->get_datatype(), bindings, conflicts);
		}
	}
}
```

Use the canonicalized argument list, not source order before named-argument normalization.

- [ ] **Step 5: Include the rest Array in bound discovery and substitution**

Add:

```cpp
collect_method_type_parameter_bounds(r_rest_parameter_type, parameter_bounds);
```

Then substitute after bound validation:

```cpp
r_rest_parameter_type = FSParser::DataType::substitute(r_rest_parameter_type, bindings);
```

Call surplus validation only after this substitution so `Array[T]` has become `Array[int]` at the call site.

- [ ] **Step 6: Cover explicit empty calls, conflicts, bounds, and strict dynamic behavior**

Positive fixture:

```foundry
func collect[T](...values: Array[T]) -> Array[T]:
	return values

func prepend[T](first: T, ...values: Array[T]) -> Array[T]:
	return [first] + values

var inferred: Array[int] = collect(1, 2, 3)
var explicit_empty: Array[int] = collect[int]()
var combined: Array[String] = prepend("a", "b", "c")
```

Negative fixtures separately prove:

```foundry
var conflict := collect(1, "two")
var unconstrained := collect()
func collect_resource[T: Resource](...values: Array[T]) -> Array[T]:
	return values
var bound_failure := collect_resource[Node](Node.new())
```

For strict dynamic behavior, use an explicit `collect[int](dynamic_value)` call and expect the existing strict
Variant-to-int diagnostic. Also prove that unconstrained `collect(dynamic_value)` may infer `T := Variant`.

- [ ] **Step 7: Preserve the generic runtime erasure boundary**

Ensure `_gdtype_from_datatype()` erases the rest Array element when it contains a method-scoped type parameter, just as
it erases a generic method `Array[T]` return. Do not transport inferred type arguments into `FSFunction::call()`.

Runtime fixture:

```foundry
func collect[T](...values: Array[T]) -> Array[T]:
	Utils.check(not values.is_typed())
	return values

func test() -> void:
	var ints: Array[int] = collect(1, 2, 3)
	Utils.check(ints == [1, 2, 3])
	var empty: Array[int] = collect[int]()
	Utils.check(empty.is_empty())
```

The caller-side assignment may retype an erased generic return through the existing mechanism; the check inside the
generic function is the mechanical proof that the packed rest Array is erased.

- [ ] **Step 8: Run focused generic and existing generic-method regressions**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypedRestParameter*"
python3 scripts/agent_build.py --backend ninja --test --case "*GenericMethod*"
```

Expected: typed-rest generic fixtures pass and existing fixed-parameter inference behavior is unchanged.

- [ ] **Step 9: Run the native strict handoff gate**

```sh
python3 scripts/agent_build.py --test --case "*TypedRestParameter*"
```

- [ ] **Step 10: Commit Task 2**

```sh
git add modules/foundry_script/fs_analyzer_call_validation.h \
  modules/foundry_script/fs_analyzer_call_validation.cpp modules/foundry_script/fs_analyzer.cpp \
  modules/foundry_script/fs_parser_data_type.cpp modules/foundry_script/fs_compiler.cpp \
  modules/foundry_script/tests/test_foundry_script_type.h \
  modules/foundry_script/tests/scripts/analyzer/features/typed_rest_parameter_generic.fs \
  modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_generic_conflict.fs \
  modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_generic_conflict.out \
  modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_generic_empty.fs \
  modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_generic_empty.out \
  modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_generic_bound.fs \
  modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_generic_bound.out \
  modules/foundry_script/tests/scripts/runtime/features/typed_rest_parameter_generic_erasure.fs
git commit -m "feat(foundry_script): Infer generic typed rest elements"
```

## Task 3: Add typed variadic Callable signatures and transformations

**Dependency:** Task 1.

**Issue boundary:** Explicit `Callable[[fixed, ...Array[T]], R]` syntax, rich callable propagation, external metadata
round-trips, and `call`/`callv`/`bind`/`bindv`. Do not implement class/trait override variance or editor UI polish.

**Files:**

- Modify: `modules/foundry_script/GRAMMAR.md`
- Modify: `modules/foundry_script/fs_parser.h`
- Modify: `modules/foundry_script/fs_parser.cpp`
- Modify: `modules/foundry_script/fs_parser_data_type.cpp`
- Modify: `modules/foundry_script/fs_format.cpp`
- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Modify: `modules/foundry_script/fs_analyzer_surface.cpp`
- Modify: `modules/foundry_script/fs_analyzer_call_validation.h`
- Modify: `modules/foundry_script/fs_analyzer_call_validation.cpp`
- Modify: `modules/foundry_script/tests/test_foundry_script_type.h`
- Create: `modules/foundry_script/tests/scripts/parser/features/typed_variadic_callable_type.norun.fs`
- Create: `modules/foundry_script/tests/scripts/parser/errors/typed_variadic_callable_not_last.fs`
- Create: `modules/foundry_script/tests/scripts/parser/errors/typed_variadic_callable_not_last.out`
- Create: `modules/foundry_script/tests/scripts/parser/errors/typed_variadic_signal.fs`
- Create: `modules/foundry_script/tests/scripts/parser/errors/typed_variadic_signal.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/typed_variadic_callable.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/typed_variadic_callable_argument.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/typed_variadic_callable_argument.out`
- Create: `modules/foundry_script/tests/scripts/format/typed_variadic_callable/input.fs`
- Create: `modules/foundry_script/tests/scripts/format/typed_variadic_callable/expected.fs`

- [ ] **Step 1: Add failing parser and type-string tests**

Extend `test_foundry_script_type.h`:

```cpp
TEST_CASE("[Modules][FoundryScript][TypedRestParameter] Callable type stores a typed rest tail") {
	FSParser parser;
	REQUIRE_EQ(parser.parse(
			"var callback: Callable[[int, ...Array[String]], bool]\n",
			"user://typed_variadic_callable.fs", false), OK);
	FSAnalyzer analyzer(&parser);
	REQUIRE_EQ(analyzer.analyze(), OK);
	const FSParser::DataType type = parser.head->get_member(SNAME("callback")).variable->get_datatype();
	CHECK(type.method_info.flags & METHOD_FLAG_VARARG);
	REQUIRE(type.has_method_rest_parameter_type());
	CHECK_EQ(type.get_method_rest_parameter_type().to_string(), "Array[String]");
	CHECK_EQ(type.to_string(), "Callable[[int, ...Array[String]], bool]");
}
```

- [ ] **Step 2: Run the focused test and verify `...` is not accepted in a type list**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypedRestParameter*Callable*"
```

Expected: parser reports an expected parameter type at `...`.

- [ ] **Step 3: Add one final rest type to Callable `TypeNode` parsing**

Add:

```cpp
TypeNode *signature_rest_parameter_type = nullptr;
```

In the inner Callable/Signal type loop, recognize `PERIOD_PERIOD_PERIOD` before `parse_type(false)`. Enforce:

```cpp
if (is_signal_type) {
	push_error("Signal signatures cannot declare a rest parameter.");
}
if (type->signature_rest_parameter_type != nullptr) {
	push_error("A Callable signature can contain only one rest parameter type.");
}
type->signature_rest_parameter_type = parse_type(false);
if (!check(FSTokenizer::Token::BRACKET_CLOSE) && !check(FSTokenizer::Token::COMMA)) {
	push_error("The rest parameter type must be the final Callable parameter type.");
}
```

Allow one trailing comma after the rest type. Consume later types for parser recovery while retaining the first error.

- [ ] **Step 4: Resolve and validate the callable rest Array**

When resolving explicit Callable signatures, resolve the optional `TypeNode`, require outer `Array`, append it to
`method_rest_parameter_type`, and set `METHOD_FLAG_VARARG`. Use this exact error for a resolved non-Array:

```text
The Callable rest parameter type must be "Array", but "int" is specified.
```

Do not add the rest type to `method_parameter_types` or `MethodInfo::arguments`.

- [ ] **Step 5: Extend display, substitution, identity, and formatter paths**

Change `_method_signature_to_string()` and `_encode_method_signature_suffix()` to append:

```cpp
if (p_type.has_method_rest_parameter_type()) {
	params.push_back("..." + _encode_signature_type(p_type.get_method_rest_parameter_type()));
}
```

Update the decoder to split one top-level entry beginning with `...`, require it to be final, decode its suffix as a
type, and set both the rich slot and vararg flag. Extend copy/substitution/traversal/alpha/strict equality in both
`fs_parser_data_type.cpp` and `fs_analyzer_surface.cpp`.

The formatter prints exactly:

```foundry
Callable[[int, ...Array[String]], bool]
```

- [ ] **Step 6: Preserve inferred function-reference rest signatures**

Update `make_callable_type(MethodInfo, FunctionNode)`:

```cpp
if (p_function->rest_parameter != nullptr) {
	type.method_rest_parameter_type.push_back(p_function->rest_parameter->get_datatype());
	type.method_info.flags |= METHOD_FLAG_VARARG;
}
```

Do the same in receiver specialization and external parser-surface completion. A MethodInfo-only vararg retains an
empty rich rest slot.

- [ ] **Step 7: Validate `Callable.call()` and literal `callv()` surplus elements**

Extend `callable_signature_from_type()` to return the optional rest type separately. Forward it to
`validate_call_arg()` for `.call(...)` and to `validate_callable_array_literal_args()` for `.callv([...])`.

Positive fixture:

```foundry
func accept(index: int, ...names: Array[String]) -> bool:
	return index == names.size()

func test() -> void:
	var callback: Callable[[int, ...Array[String]], bool] = accept
	var ok: bool = callback.call(2, "a", "b")
	var okv: bool = callback.callv([2, "a", "b"])
```

Negative fixture calls both forms with an `int` in a String rest position and expects the existing invalid-argument
diagnostic. A nonliteral `callv(values)` remains statically gradual and relies on T1 runtime enforcement.

- [ ] **Step 8: Preserve the rest boundary through `bind` and `bindv`**

Callable binding consumes arguments from the same end/order used today. Update transformed callable creation so:

- binding fewer than all fixed parameters reduces only the fixed list;
- binding all fixed parameters leaves the same rest type;
- binding additional values to a variadic callable validates them against the rest element but leaves the callable
  variadic with the same rest type;
- fixed-only over-binding behavior is unchanged;
- `unbind(n)` restores only fixed slots represented by existing metadata and does not fabricate a typed rest slot.

Add C++ truth-table tests for `bind`, `bindv`, `unbind`, async callables, default arguments, and typed/untyped varargs.

- [ ] **Step 9: Encode only lossless external rich rest signatures**

Allow `_signature_type_is_encodable()` to encode a vararg callable only when it has one rich rest Array and that Array
is recursively encodable. Continue returning false for MethodInfo-only varargs. Add cross-file provider/consumer
fixtures for builtin/native/nested Array rest elements and one script-local lossy type that degrades to bare Callable.

- [ ] **Step 10: Update the normative Callable grammar**

Add the `callable_parameter` rule from the design. State that `...Array[T]` is final, optional, and Callable-only; a
Signal never has a rest tail.

- [ ] **Step 11: Run focused parser, callable, and external-signature tests**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypedRestParameter*"
python3 scripts/agent_build.py --backend ninja --test --case "*FoundryScriptType*"
python3 scripts/agent_build.py --backend ninja --test --case "*Callable*"
```

- [ ] **Step 12: Run the native strict handoff gate**

```sh
python3 scripts/agent_build.py --test --case "*TypedRestParameter*"
```

- [ ] **Step 13: Commit Task 3**

```sh
git add modules/foundry_script/GRAMMAR.md modules/foundry_script/fs_parser.h \
  modules/foundry_script/fs_parser.cpp modules/foundry_script/fs_parser_data_type.cpp \
  modules/foundry_script/fs_format.cpp modules/foundry_script/fs_analyzer.cpp \
  modules/foundry_script/fs_analyzer_surface.cpp modules/foundry_script/fs_analyzer_call_validation.h \
  modules/foundry_script/fs_analyzer_call_validation.cpp \
  modules/foundry_script/tests/test_foundry_script_type.h \
  modules/foundry_script/tests/scripts/parser/features/typed_variadic_callable_type.norun.fs \
  modules/foundry_script/tests/scripts/parser/errors/typed_variadic_callable_not_last.fs \
  modules/foundry_script/tests/scripts/parser/errors/typed_variadic_callable_not_last.out \
  modules/foundry_script/tests/scripts/parser/errors/typed_variadic_signal.fs \
  modules/foundry_script/tests/scripts/parser/errors/typed_variadic_signal.out \
  modules/foundry_script/tests/scripts/analyzer/features/typed_variadic_callable.fs \
  modules/foundry_script/tests/scripts/analyzer/errors/typed_variadic_callable_argument.fs \
  modules/foundry_script/tests/scripts/analyzer/errors/typed_variadic_callable_argument.out \
  modules/foundry_script/tests/scripts/format/typed_variadic_callable
git commit -m "feat(foundry_script): Type variadic Callable signatures"
```

## Task 4: Enforce typed-rest compatibility across overrides and traits

**Dependency:** Task 1.

**Issue boundary:** One compatibility rule shared by class overrides, abstract requirements, traits, inferred/explicit
callable assignment, and signal connections. Do not change arity intervals, fixed-parameter variance, or tooling.

**Files:**

- Modify: `modules/foundry_script/fs_analyzer.h`
- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Modify: `modules/foundry_script/fs_analyzer_surface.cpp`
- Modify: `modules/foundry_script/tests/test_foundry_script_type.h`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/typed_rest_parameter_override_variance.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_override_narrowing.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_override_narrowing.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/typed_rest_parameter_trait_variance.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_trait_narrowing.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_trait_narrowing.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/typed_rest_parameter_callable_variance.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_callable_variance.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_callable_variance.out`

- [ ] **Step 1: Add the compatibility truth table as failing analyzer tests**

Add one `TEST_CASE("[Modules][FoundryScript][TypedRestParameter] Variance truth table")` to
`test_foundry_script_type.h`. Build each row below as a complete Base/Child, abstract/implementation, or typed Callable
source passed to `analyze_source()`. Assert `OK` for pass rows and non-`OK` for fail rows:

| Required tail | Implementation tail | Result |
| --- | --- | --- |
| `Array[Node]` | `Array[Node]` | pass |
| `Array[Node]` | `Array[Object]` | pass |
| `Array[Node]` | `Array` | pass |
| `Array` | `Array[Node]` | fail |
| `Array[Node]` | `Array[Sprite2D]` | fail |
| `Array` | `Array` | pass |
| none | typed/untyped rest | governed by existing arity interval |
| typed/untyped rest | none | fail existing arity interval |

Use actual resolved `DataType` instances; do not mock compatibility with strings.

- [ ] **Step 2: Run focused tests and verify broadening is currently rejected or ignored**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypedRestParameter*Variance*"
```

Expected: at least broadening/narrowing cases disagree with the table because current signature checks compare only
arity or exact Array identity.

- [ ] **Step 3: Implement one rest-element acceptance helper**

Add a helper with required/source semantics made explicit by names:

```cpp
bool FSAnalyzer::rest_parameter_accepts_required_arguments(
		const FSParser::DataType *p_implementation_array,
		const FSParser::DataType *p_required_array) const;
```

Rules in code:

```cpp
if (p_required_array == nullptr || p_implementation_array == nullptr) {
	return p_required_array == p_implementation_array;
}
const FSParser::DataType required_element =
		p_required_array->get_container_element_type_or_variant(0);
const FSParser::DataType implementation_element =
		p_implementation_array->get_container_element_type_or_variant(0);
if (implementation_element.is_variant()) {
	return true;
}
if (required_element.is_variant()) {
	return false;
}
return is_type_compatible(implementation_element, required_element);
```

Use the existing hard-Variant and nullable exceptions from fixed-parameter contravariance where applicable. Add an
alpha-equivalent variant for generic trait/method comparison rather than weakening concrete compatibility.

- [ ] **Step 4: Apply the helper after override arity checks**

In `resolve_function_signature()`, retrieve the parent rest Array through the T1 signature out parameter and compare it
to `p_function->rest_parameter`. Render the required signature as:

```text
visit(...nodes: Array[Node]) -> void
```

Keep fixed/default interval and fixed-parameter contravariance logic unchanged.

- [ ] **Step 5: Apply the same rule to abstract and trait requirements**

Extend required-function surface records to carry the optional rest Array or retrieve it from the declaration when
both sides are FoundryScript. Compare generic rest types under method type-parameter alpha-renaming and equivalent
bounds. A native MethodInfo-only vararg has no rich required element and remains gradual.

Fixtures use real abstract classes and traits:

```foundry
trait AcceptsNodes:
	abstract func accept(...nodes: Array[Node]) -> void

class Good uses AcceptsNodes:
	func accept(...nodes: Array[Object]) -> void:
		pass
```

The negative sibling implements `Array[Sprite2D]` and receives one signature mismatch.

- [ ] **Step 6: Apply the rule to callable assignment and signal connection**

When both callable `DataType` values carry a rich rest slot, compare it after fixed parameters and return type. The
target/required callable determines which calls must remain valid; the source callable's rest element must be equal or
broader. For signal connections, fixed signal arguments beyond the callable's fixed prefix must each fit the callable
rest element. A signal does not become variadic.

- [ ] **Step 7: Add generic alpha-renaming coverage**

Positive requirement/implementation pair:

```foundry
abstract func accept[T: Node](...nodes: Array[T]) -> void
func accept[U: Node](...nodes: Array[U]) -> void:
	pass
```

Negative cases change the bound or replace the open element with `Sprite2D`. Confirm only the intended signature error
appears.

- [ ] **Step 8: Run focused and conformance regressions**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypedRestParameter*"
python3 scripts/agent_build.py --backend ninja --test --case "*Trait*"
python3 scripts/agent_build.py --backend ninja --test --case "*Abstract*"
```

- [ ] **Step 9: Run the native strict handoff gate**

```sh
python3 scripts/agent_build.py --test --case "*TypedRestParameter*"
```

- [ ] **Step 10: Commit Task 4**

```sh
git add modules/foundry_script/fs_analyzer.h modules/foundry_script/fs_analyzer.cpp \
  modules/foundry_script/fs_analyzer_surface.cpp \
  modules/foundry_script/tests/test_foundry_script_type.h \
  modules/foundry_script/tests/scripts/analyzer/features/typed_rest_parameter_override_variance.fs \
  modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_override_narrowing.fs \
  modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_override_narrowing.out \
  modules/foundry_script/tests/scripts/analyzer/features/typed_rest_parameter_trait_variance.fs \
  modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_trait_narrowing.fs \
  modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_trait_narrowing.out \
  modules/foundry_script/tests/scripts/analyzer/features/typed_rest_parameter_callable_variance.fs \
  modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_callable_variance.fs \
  modules/foundry_script/tests/scripts/analyzer/errors/typed_rest_parameter_callable_variance.out
git commit -m "feat(foundry_script): Enforce typed rest variance"
```

## Task 5: Complete editor, LSP, documentation, and integrated acceptance

**Dependencies:** Tasks 2, 3, and 4.

**Issue boundary:** Every authoring surface, generated documentation, user-facing reference text, cross-file acceptance,
and the epic full-suite gate. Do not change the locked runtime erasure boundary or MethodInfo ABI.

**Files:**

- Modify: `modules/foundry_script/fs_editor.cpp`
- Modify: `modules/foundry_script/editor/fs_docgen.cpp`
- Modify: `modules/foundry_script/editor/fs_refactoring.cpp`
- Modify: `modules/foundry_script/editor/fs_refactoring_types.cpp`
- Modify: `modules/foundry_script/language_server/fs_extend_parser.cpp`
- Modify: `modules/foundry_script/language_server/fs_semantic_tokens.cpp`
- Modify: `modules/foundry_script/tests/test_lsp.h`
- Modify: `modules/foundry_script/tests/test_refactor.h`
- Modify: `modules/foundry_script/tests/test_foundry_script.cpp`
- Modify: `modules/foundry_script/tests/test_foundry_script_type.h`
- Modify: `modules/foundry_script/README.md`
- Modify: `modules/foundry_script/GRAMMAR.md`
- Create: `modules/foundry_script/tests/scripts/completion/typed_rest_parameter.fs`
- Create: `modules/foundry_script/tests/scripts/lsp/typed_rest_parameter.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/typed_rest_parameter_external_provider.notest.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/typed_rest_parameter_external.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/typed_rest_parameter_integrated.fs`

- [ ] **Step 1: Add failing signature-help and hover tests**

Create an LSP source containing:

```foundry
func collect(prefix: String, ...values: Array[int]) -> int:
	return values.size()

func use() -> void:
	collect("n", 1, 2, 3)
```

Assert:

```cpp
CHECK_EQ(signature.label,
		"func collect(prefix: String, ...values: Array[int]) -> int");
CHECK_EQ(signature.parameters.size(), 2);
CHECK_EQ(signature.parameters[1].label, "...values: Array[int]");
CHECK_EQ(signature.active_parameter, 1);
```

Request signature help at the first, second, and fourth surplus arguments; all select the rest entry. Hover and
document-symbol tests require the same typed signature.

- [ ] **Step 2: Run the focused LSP test and verify the fallback still says `...args: Array`**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypedRestParameter*LSP*"
```

- [ ] **Step 3: Render declaration-owned rest names and types in editor hints**

Where `FSParser::FunctionNode` is available, append:

```cpp
const FSParser::ParameterNode *rest = p_function->rest_parameter;
arghint += "..." + String(rest->identifier->name) + ": " +
		(rest->get_datatype().is_hard_type() ? rest->get_datatype().to_string() : String("Array"));
```

Retain `...args: Array` only in MethodInfo-only paths. Make active-parameter selection clamp every argument index at or
beyond the fixed count to the rest parameter's signature-help slot.

- [ ] **Step 4: Preserve typed rest declarations in refactors**

The override and implement-abstract renderers already place `...name` last. Ensure the resolved/substituted rest Array
is passed to `render_function_signature()` and emitted through `render_annotatable_type()`. Add tests for:

- concrete inherited `Array[Node]`;
- generic/receiver-substituted `Array[T]` becoming a concrete type where existing substitution requires it;
- untyped rest remaining `Array`;
- no default value emitted;
- generated code parsing and analyzing successfully.

- [ ] **Step 5: Populate DocData's existing rest argument**

`fs_docgen.cpp` already writes a rest argument for FoundryScript functions. Assert its name is the declaration name and
its type string is `Array[int]`, including nested element types. Confirm MethodInfo-only native docs still use the
generic vararg fallback and no `MethodInfo` field was added.

- [ ] **Step 6: Complete LSP symbol, semantic-token, and completion surfaces**

Use the existing `ParameterNode` traversal so semantic tokens cover the ellipsis-adjacent name, outer Array, and nested
element. Completion inside the body must prove element-returning operations are typed:

```foundry
func collect(...values: Array[Node]) -> void:
	var node: Node = values.front()
	values.append(Node.new())
```

Also add a negative analyzer companion showing `values.append(1)` fails; completion alone is not behavioral proof.

- [ ] **Step 7: Add cross-file rich-signature acceptance**

Provider:

```foundry
func collect(prefix: String, ...values: Array[int]) -> int:
	return values.size()

func callback() -> Callable[[String, ...Array[int]], int]:
	return collect
```

Consumer imports/preloads the provider, invokes the method and returned callable correctly, and includes one dedicated
error fixture for a wrong rest element. This proves parser-surface completion and property-hint fallback do not lose
the rich type across files.

- [ ] **Step 8: Add one integrated runtime fixture**

The fixture combines fixed defaults, named fixed arguments, concrete rest arrays, a typed Callable, a broadening
override, a trait witness, and a generic rest helper. It checks observable results plus concrete/generic `is_typed()`
behavior exactly as specified. Keep each assertion independent so failures identify the broken surface.

- [ ] **Step 9: Finish normative and user documentation**

Review `GRAMMAR.md` as one coherent contract covering:

- function/lambda `...name: Array[T]`;
- explicit Callable `...Array[T]` final type;
- surplus argument checking;
- named-rest prohibition;
- override/callable contravariance; and
- generic method inference with runtime erasure.

Add a concise `README.md` section with concrete, Callable, and generic examples. State that native MethodInfo-only
varargs remain untyped and that generic method runtime reification is outside this feature.

- [ ] **Step 10: Run focused authoring and language verification**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypedRestParameter*"
python3 scripts/agent_build.py --backend ninja --test --case "*LSP*"
python3 scripts/agent_build.py --backend ninja --test --case "*Refactor*"
```

Expected: all new and existing relevant tests pass.

- [ ] **Step 11: Run the native strict focused gate**

```sh
python3 scripts/agent_build.py --test --case "*TypedRestParameter*"
```

- [ ] **Step 12: Run the epic full-suite gate**

```sh
python3 scripts/agent_build.py --compiler-cache ccache --jobs 4 --test
```

Expected: wrapper `build_summary` reports success and the test log ends with doctest `Status: SUCCESS!`. On Linux,
ensure `DISPLAY=:1` is available so GUI-dependent subprocess tests execute.

- [ ] **Step 13: Run repository policy checks for touched areas**

```sh
git diff --check origin/develop...HEAD
git grep -nE '\.split\("(void|bool|String|private fun|const val|def )' -- '*.py'
git grep -n "docs/superpowers" -- '*/test_*.py' '*/check_*.py'
git grep -n "hasattr(" -- '*/test_*.py'
git grep -l "assert" -- 'misc/checks/check_*.py'
```

Expected: no whitespace errors and no new policy violations attributable to the epic.

- [ ] **Step 14: Commit Task 5**

```sh
git add modules/foundry_script/fs_editor.cpp modules/foundry_script/editor/fs_docgen.cpp \
  modules/foundry_script/editor/fs_refactoring.cpp modules/foundry_script/editor/fs_refactoring_types.cpp \
  modules/foundry_script/language_server/fs_extend_parser.cpp \
  modules/foundry_script/language_server/fs_semantic_tokens.cpp \
  modules/foundry_script/tests/test_lsp.h modules/foundry_script/tests/test_refactor.h \
  modules/foundry_script/tests/test_foundry_script.cpp \
  modules/foundry_script/tests/test_foundry_script_type.h modules/foundry_script/README.md \
  modules/foundry_script/GRAMMAR.md \
  modules/foundry_script/tests/scripts/completion/typed_rest_parameter.fs \
  modules/foundry_script/tests/scripts/lsp/typed_rest_parameter.fs \
  modules/foundry_script/tests/scripts/analyzer/features/typed_rest_parameter_external_provider.notest.fs \
  modules/foundry_script/tests/scripts/analyzer/features/typed_rest_parameter_external.fs \
  modules/foundry_script/tests/scripts/runtime/features/typed_rest_parameter_integrated.fs
git commit -m "docs(foundry_script): Complete typed rest tooling"
```

## Epic acceptance matrix

| Requirement | Owning task | Mechanical proof |
| --- | --- | --- |
| Concrete declaration acceptance | T1 | analyzer C++ test + positive fixture |
| Wrong direct element rejection | T1 | argument error fixture with absolute index |
| Empty/non-empty typed runtime Array | T1 | runtime `is_typed()` and element identity checks |
| Dynamic VM enforcement | T1 | callback mismatch leaves body-call counter unchanged |
| Bytecode metadata/version | T1 | version-8 export/load and malformed-input doctests |
| Generic rest inference | T2 | rest-only and fixed+rest fixtures |
| Generic conflicts/bounds/empty calls | T2 | three dedicated error fixtures |
| Generic runtime erasure | T2 | in-body `not values.is_typed()` runtime assertion |
| Explicit variadic Callable syntax | T3 | parser/type-string/format round-trips |
| Callable call/callv validation | T3 | positive and negative analyzer fixtures |
| bind/bindv transformations | T3 | C++ truth table |
| External callable metadata | T3/T5 | lossless and gradual cross-file fixtures |
| Override/trait contravariance | T4 | complete required/implementation truth table |
| Generic alpha-equivalent rest signatures | T4 | bounded requirement/witness fixtures |
| Signature help and hover | T5 | active rest parameter and exact label tests |
| Completion/refactoring/docgen | T5 | observable LSP/refactor/DocData tests |
| Normative and user docs | T1/T3/T5 | reviewed grammar plus README examples |
| Full integration | T5 | native strict full-suite doctest success |

## GitHub epic publication contract

After this markdown plan merges to `develop`:

1. Create one issue titled `Epic: Typed rest parameters for FoundryScript` with `epic` and `enhancement` labels.
2. Create T1-T5 as native GitHub subissues, one issue per numbered task.
3. Copy each task's boundary, exact files, steps, acceptance criteria, and verification commands into its issue body.
4. Configure native `blockedBy` relationships, not body text alone:
   - T1 has no blocker;
   - T2 is blocked by T1;
   - T3 is blocked by T1;
   - T4 is blocked by T1;
   - T5 is blocked by T2, T3, and T4.
5. Add the epic and all children to the repository's `Experiment` project with status `Todo`.
6. The epic body links the merged design and plan, lists the dependency graph, and contains the full epic acceptance
   checklist.
7. Verify the hierarchy and dependencies with GraphQL `subIssues`, `blockedBy`, and `blocking` reads before handoff.

## Implementation handoff check

Before an epic runner starts:

- every locked design decision maps to one task and acceptance row;
- every task is independently mergeable and begins with a failing observable test;
- T2, T3, and T4 have no dependency on one another and may execute concurrently after T1;
- only T5 depends on all parallel semantic work;
- no issue changes MethodInfo or reifies generic method calls;
- no issue treats custom annotation varargs or signals as function rest arrays;
- every issue has focused Ninja iteration, native strict handoff verification, and a focused commit boundary;
- epic closure requires the native strict full suite, not only the five focused suites.
