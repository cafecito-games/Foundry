# Issue #1482 Static `Self` Call-Context Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every runtime occurrence of `Self` in an inherited static Foundry Script method or static conformance
witness resolve to the exact class handle used at the call boundary, including nested types, extracted callables,
coroutines, specialized generics, and compiled bytecode.

**Architecture:** Preserve `Self` as symbolic recursive type metadata in executable `FSDataType` values. Introduce one
immutable static-call receiver descriptor, pass it from all class-handle and witness dispatch boundaries into
`FSFunction`, retain it in callables and suspended call state, and resolve symbolic types only when the VM needs a
runtime type. Keep method lookup and declaration/reflection types unchanged.

**Tech Stack:** C++17, Foundry Script compiler and VM, runtime fixture corpus, doctest, compiled `.fsb` round trips,
SCons/Ninja agent build wrapper.

---

## Preconditions and invariants

- Continue on the same `issue-1482-1538` branch after completing
  `docs/superpowers/plans/2026-08-02-issue-1538-inner-class-conformance-identity.md`.
- Preserve the #1538 commit as a distinct review boundary, but open one implementation PR that closes both issues.
- Use `python3 scripts/agent_build.py`; do not invoke raw `scons` for agent-driven builds.
- Do not alter static method or conformance witness selection. This change carries exact receiver identity after the
  existing dispatch decision.
- An explicit base-class call starts a new context at the explicit base handle. An inherited call through a derived or
  specialized handle retains that exact handle.
- `Self` remains declaration-relative in editor reflection and diagnostics where no call is active. Only executable
  type checks, construction, defaults, arguments, returns, casts, type tests, and reified metadata use call context.
- Missing context for a function whose executable signature or body contains symbolic `Self` is an engine invariant
  violation, never a silent fallback to the declaring script.
- `FSBytecodeFormat::FORMAT_VERSION` is currently 6. Reuse reserved flag bits and existing recursively encoded data
  types where compatible. Task 4 adds opcodes, so it must bump the format version and update verifier/disassembler
  coverage as required by the opcode contract in `fs_function.h`.

### Task 1: Add exact-receiver behavioral regressions

**Files:**

- Create: `modules/foundry_script/tests/scripts/runtime/features/type_self_static_exact_receiver.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/type_self_static_exact_receiver.out`
- Create: `modules/foundry_script/tests/scripts/runtime/features/type_self_static_callable_async.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/type_self_static_callable_async.out`
- Create: `modules/foundry_script/tests/scripts/runtime/errors/type_self_static_exact_receiver_argument.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/errors/type_self_static_exact_receiver_argument.out`

- [ ] **Step 1: Cover direct, dynamic, nested, and explicit-base calls**

Create a feature fixture with `Base`, `Child extends Base`, and `Grandchild extends Child`. The inherited static
implementation must exercise all of these in one observable path:

```foundry
class Base:
	static func make() -> Self:
		return Self.new()

	static func make_nested() -> Self:
		return make()

	static func accept(value: Self) -> bool:
		return value is Self


class Child extends Base:
	pass


class Grandchild extends Child:
	pass


func print_receiver(label: String, type: Type[Base]) -> void:
	var value := type.make_nested()
	print(label, ":", is_instance_of(value, type))
	print(type.accept(value))


func test() -> void:
	print(Base.make() is Base)
	print(Base.make() is Child)
	print(Child.make() is Child)
	print(Grandchild.make() is Grandchild)
	print_receiver("dynamic", Grandchild)
	print(Base.make() is Base)
```

Use the repository's accepted explicit-base static-call syntax; do not add syntax for this fix. Expected output must
distinguish concrete receiver class names, not merely print non-null values. Add a `Self`-typed default or constructor
path if the language already accepts one; otherwise leave defaults to the focused compiler/VM unit coverage below.

- [ ] **Step 2: Cover extracted callables, static lambdas, and suspension**

Create a second feature fixture that obtains an inherited static method as a callable from `Child`, stores it before
calling, and verifies the result is `Child`. In the inherited static implementation, return or invoke a lambda that
constructs `Self`. Add an async static method that awaits a test signal before constructing `Self`; resume it and
verify the result is still `Child`.

The fixture must include these observable paths:

```foundry
var factory: Callable = Child.make
var made = factory.call()
print(made is Child)

var lambda_factory: Callable = Child.make_factory()
print(lambda_factory.call() is Child)
```

Follow the existing coroutine fixtures for signal construction and resumption. Do not use timing or sleeps.

- [ ] **Step 3: Add a negative argument-boundary fixture**

Create a runtime-error fixture that calls an inherited `static func accept(value: Self)` through `Child` with a `Base`
instance. Seed its output with `FS_TEST_RUNTIME_ERROR`; after the fix, regenerate the canonical diagnostic and verify
it names the exact expected receiver type `Child`.

- [ ] **Step 4: Verify RED in source and bytecode modes**

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*Script compilation and runtime*" --force-colors
```

Expected: FAIL. At least the inherited `Self.new()` result, extracted callable, post-await result, and argument
boundary resolve as the declaring `Base`. The second corpus pass must demonstrate the same failure after compiled
bytecode restoration. On Linux use the checkout's `bin/foundry.linuxbsd.editor.dev.x86_64` binary.

- [ ] **Step 5: Commit the failing core regressions**

```sh
git add modules/foundry_script/tests/scripts/runtime/features/type_self_static_exact_receiver.* \
  modules/foundry_script/tests/scripts/runtime/features/type_self_static_callable_async.* \
  modules/foundry_script/tests/scripts/runtime/errors/type_self_static_exact_receiver_argument.*
git commit -m "test(foundry_script): Reproduce inherited static Self erasure"
```

### Task 2: Preserve symbolic `Self` and define one runtime resolver

**Files:**

- Modify: `modules/foundry_script/fs_function.h`
- Modify: `modules/foundry_script/fs_function.cpp`
- Modify: `modules/foundry_script/fs_compiler.cpp:3830`
- Modify: `modules/foundry_script/fs_byte_codegen.cpp:197`
- Modify: `modules/foundry_script/fs_bytecode_export.cpp:438`
- Modify: `modules/foundry_script/fs_bytecode_loader.cpp:737`
- Modify if the encoded shape changes: `modules/foundry_script/fs_bytecode_format.h:61`
- Test: `modules/foundry_script/tests/test_fs_bytecode.h`

- [ ] **Step 1: Add recursive symbolic-type detection**

Add `FSDataType::contains_self_type()` beside the existing recursive type helpers. It returns true when the root,
container elements, or specialized type arguments contain `is_self_type`:

```cpp
bool FSDataType::contains_self_type() const {
	if (is_self_type) {
		return true;
	}
	for (const FSDataType &element : container_element_types) {
		if (element.contains_self_type()) {
			return true;
		}
	}
	for (const FSDataType &argument : type_arguments) {
		if (argument.contains_self_type()) {
			return true;
		}
	}
	return false;
}
```

- [ ] **Step 2: Introduce the immutable call-context value**

Add a small value type in `fs_function.h`, near `FSDataType`, with no dependency on an active stack frame:

```cpp
struct FSStaticSelfContext {
	enum Kind { NONE, BUILTIN, NATIVE, SCRIPT, SPECIALIZED_SCRIPT };

	Kind kind = NONE;
	Variant receiver_handle;
	Variant::Type builtin_type = Variant::NIL;
	StringName native_type;

	bool is_valid() const;
	FSDataType resolve(const FSDataType &p_symbolic) const;
	Variant class_value() const;
	String get_type_name() const;
};
```

Provide named constructors for `FoundryScript`, `FSSpecializedClassHandle`, `FSNativeClass`, and builtin receivers.
`resolve()` must copy its input and recursively replace every `is_self_type` node while preserving nullability,
`is_type_handle`, container structure, and unrelated generic arguments. For a specialized receiver, use the exact
specialized handle and its reified `ContainerType` arguments rather than erasing to the underlying script.

Keep the descriptor immutable after construction. Do not retain raw pointers to temporary class handles; the
`Variant` owns the receiver for callable and coroutine lifetimes.

- [ ] **Step 3: Split executable types from declaration/reflection types**

In `FSCompiler::_parse_function()`, stop permanently substituting `Self` before generating executable argument and
return types. Convert the original parser data type with `_gdtype_from_datatype()` for `FSFunction::argument_types`,
`FSFunction::return_type`, and bytecode generation. Continue using
`_substitute_self_type_parameter_for_class()` for `MethodInfo`, editor reflection, and declaration-site validation.

Ensure `_gdtype_from_datatype()` marks every receiver-relative `@Self` leaf with `is_self_type`, including native
conformance targets and leaves nested in `Array`, `Dictionary`, tuple, `Type`, and specialized generic arguments.

- [ ] **Step 4: Mark functions that require a static receiver**

Add `_requires_static_self` to `FSFunction`. Set it during `write_start()` or after parsing when a static function's
argument type, return type, default expressions, or emitted symbolic type descriptors contain `Self`. Treat a static
lambda inside such a context as requiring the captured context when its body contains `Self`.

Store the flag in function record flag bit 1 in `FSBytecodeExporter::encode_function()` and restore it in
`FSBytecodeLoader::decode_function()`. The bytecode record size does not change, so do not bump format version for this
flag alone.

- [ ] **Step 5: Preserve `is_self_type` in runtime descriptors**

Add `"is_self_type"` to `FSByteCodeGenerator::make_container_type_descriptor()` and restore it in every descriptor
reader in `fs_vm.cpp`. Include recursive container elements and specialized arguments. Bare symbolic `Self` must use a
descriptor rather than a baked script constant, so update `get_container_type_pos()` to return the full descriptor
whenever `contains_self_type()` is true.

- [ ] **Step 6: Add bytecode metadata round-trip tests**

In `test_fs_bytecode.h`, construct or compile a function whose return and argument types include
`Dictionary[String, Array[Type[Self]]]`. Export and restore it, then assert the decoded tree retains `is_self_type` at
the leaf, the container structure, and `_requires_static_self`. Assert on decoded data structures, not source text.

- [ ] **Step 7: Build and run the focused bytecode tests**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*[FoundryScript][Bytecode]*"
```

Expected: all selected bytecode tests pass. The runtime fixtures may still fail until the call boundaries and VM
consumers use the new context.

- [ ] **Step 8: Commit symbolic metadata and serialization**

```sh
git add modules/foundry_script/fs_function.h modules/foundry_script/fs_function.cpp \
  modules/foundry_script/fs_compiler.cpp modules/foundry_script/fs_byte_codegen.cpp \
  modules/foundry_script/fs_bytecode_export.cpp modules/foundry_script/fs_bytecode_loader.cpp \
  modules/foundry_script/fs_bytecode_format.h modules/foundry_script/tests/test_fs_bytecode.h
git commit -m "refactor(foundry_script): Preserve symbolic static Self types"
```

Exclude `fs_bytecode_format.h` from the commit if no version change was necessary.

### Task 3: Carry exact identity through every static dispatch boundary

**Files:**

- Modify: `modules/foundry_script/fs_function.h:807`
- Modify: `modules/foundry_script/fs_vm.cpp:966`
- Modify: `modules/foundry_script/foundry_script.cpp:160`
- Modify: `modules/foundry_script/foundry_script.cpp:1458`
- Modify: `modules/foundry_script/foundry_script.cpp:1503`
- Modify: `modules/foundry_script/foundry_script.h`
- Modify: static witness callers found by `rg "witness->call\\(nullptr" modules/foundry_script`

- [ ] **Step 1: Extend the function-call contract**

Add the static context to `FSFunction::call()` without changing instance witness `self_override`:

```cpp
Variant call(FSInstance *p_instance, const Variant **p_args, int p_argcount,
		Callable::CallError &r_err, CallState *p_state = nullptr,
		const Variant *p_self_override = nullptr,
		const FSStaticSelfContext *p_static_self = nullptr);
```

At function entry, restore it from `CallState` on resume. If `_requires_static_self` is true and no valid context is
present, report an internal/runtime invariant failure that identifies the function; do not use `_script` as the exact
receiver. Set `ADDR_STACK_CLASS` from `p_static_self->class_value()` for static calls so nested unqualified calls and
`Self` in value position retain the receiver. Instance methods keep their existing class-stack behavior.

- [ ] **Step 2: Bind script and specialized script handles**

Refactor `FoundryScript::callp()` into a private helper that accepts both the lookup script and exact receiver context.
The public entry creates the context from `this`; inherited method lookup may advance `top`, but the call receives the
original `this`. Static witness lookup keeps selecting by the existing exact target helper and receives the same
context.

`FSSpecializedClassHandle::callp()` must call that helper with a context created from the specialized handle, not
delegate to the unspecialized public entry. `new` and equality/hash behavior stay unchanged.

Audit `OPCODE_CALL_SELF_BASE` separately. For a static `super.method()` call, select the same immediate-base
implementation as today but start a context from that explicit base handle. Do not dereference a null `p_instance` in
the static path. Instance `super` calls keep their existing receiver behavior.

- [ ] **Step 3: Bind native and builtin static witnesses**

Update `FSNativeClass::callp()` and all VM static-witness fallbacks to build a native or builtin context and pass it to
the selected witness. Continue using the existing target-exact registry lookup and real-method-before-witness
precedence. Instance witnesses still use `call_witness()` and are outside this static context.

Run this audit command and account for every result:

```sh
rg -n "witness->call\(nullptr|->call\(nullptr,.*p_args" modules/foundry_script
```

- [ ] **Step 4: Preserve exact receiver in extracted callables**

In `FoundryScript::_get()`, when an inherited static function is found on `top`, create `Callable(this, method)`, not
`Callable(top, method)`. Keep `top` only for method lookup and RPC metadata. Apply the same exact target to
`FSRPCCallable` and add a regression if static RPC callables are supported. For specialized handles, implement `_get`
so static methods resolve to a callable whose object is the `FSSpecializedClassHandle`; do not delegate callable
creation to the unspecialized script.

If static conformance witness lookup participates in method extraction, add the same last-resort lookup to `_get()` so
the callable retains the target handle and later enters the ordinary target-exact `callp()` path. Preserve
real-method-before-witness precedence.

- [ ] **Step 5: Retain context across static lambdas**

When `OPCODE_CREATE_LAMBDA` runs under a valid static context, capture that value in the lambda callable. On lambda
invocation, pass it to `FSFunction::call()`. Instance/self lambdas keep their existing receiver capture and may also
carry the static context when their compiled body contains symbolic `Self`.

- [ ] **Step 6: Retain context across await/resume**

Add `FSStaticSelfContext static_self_context` and a validity flag to `FSFunction::CallState`. Copy them when an await
saves the stack, restore them before argument/default/body execution on resume, and clear/move ownership consistently
with `self_override`. The context must survive repeated await/resume cycles.

- [ ] **Step 7: Build and run the core regressions**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*Script compilation and runtime*"
```

Expected: both corpus passes now preserve the exact receiver for direct, dynamic, nested, extracted-callable, lambda,
and post-await cases. The negative argument fixture may remain red until Task 4 resolves executable type consumers.

- [ ] **Step 8: Commit call-context propagation**

```sh
git add modules/foundry_script/fs_function.h modules/foundry_script/fs_vm.cpp \
  modules/foundry_script/foundry_script.h modules/foundry_script/foundry_script.cpp
git commit -m "fix(foundry_script): Carry exact static Self receiver"
```

Add any other audited static witness caller files to this commit.

### Task 4: Resolve every executable `Self` type at point of use

**Files:**

- Modify: `modules/foundry_script/fs_byte_codegen.h`
- Modify: `modules/foundry_script/fs_byte_codegen.cpp`
- Modify: `modules/foundry_script/fs_function.h`
- Modify: `modules/foundry_script/fs_function.cpp`
- Modify: `modules/foundry_script/fs_vm.cpp`
- Modify: `modules/foundry_script/fs_compiler.cpp`
- Modify: `modules/foundry_script/fs_disassembler.cpp`
- Modify: `modules/foundry_script/fs_bytecode_verifier.cpp`
- Modify: `modules/foundry_script/fs_bytecode_format.h`
- Test: `modules/foundry_script/tests/test_fs_bytecode.h`
- Test: new fixtures from Tasks 1 and 5

- [ ] **Step 1: Centralize runtime validation and default construction**

Add helpers that accept an already resolved `FSDataType` for argument validation, assignment, return validation,
casts, type tests, typed-container construction, and default values. Reuse the existing conversions and diagnostic
paths rather than duplicating them in new opcodes.

At `FSFunction::call()` entry, resolve symbolic argument and return types once against the active context. Validate
arguments with the resolved types and use the resolved return type for empty-code/default and error returns.

- [ ] **Step 2: Emit a symbolic descriptor whenever the static type contains `Self`**

For assignment, cast, type-test, and return generation, branch on `contains_self_type()` before selecting a baked
`BUILTIN`, `NATIVE`, or `SCRIPT` opcode. Emit one descriptor-driven opcode per operation:

```cpp
OPCODE_ASSIGN_TYPED_SELF
OPCODE_TYPE_TEST_SELF
OPCODE_CAST_TO_SELF
OPCODE_RETURN_TYPED_SELF
```

Each opcode receives the recursive descriptor, resolves it against the current static context, then calls the shared
operation helper. This is required for bare `Self`: the exact receiver may be native, script, native-backed script, or
specialized even when the declaring target was another kind.

Append the new opcodes together immediately before `OPCODE_END`, update the computed-goto table in `fs_vm.cpp`, and
add their exact operand layouts to `FSFunction::disassemble()` and `FSBytecodeVerifier::verify_function()`. Bump
`FSBytecodeFormat::FORMAT_VERSION` from 6 to 7 because the opcode set changed. Add malformed/truncated operand cases
to the existing verifier tests and update the bytecode version round-trip expectation.

- [ ] **Step 3: Resolve typed containers and specialized constructors**

Update `_container_type_from_descriptor()`, `_container_type_from_type_info()`,
`_script_type_from_type_info()`, `OPCODE_CONSTRUCT_SPECIALIZED`, and all typed `Array`/`Dictionary` creation or
assignment paths to accept the active context. Resolve nested `Self` before producing `ContainerType`, and preserve
the exact specialized class handle in `Type[Self]` and generic arguments.

- [ ] **Step 4: Resolve `Self` in value position and `Self.new()`**

Compile receiver-relative `Self` in an active static function to `Address::CLASS` rather than a declaring-script
constant. `Self.new()`, nested unqualified static calls, passing `Self` as a `Type[...]` value, equality, and dynamic
method access must therefore operate on the exact context handle. Explicit named/base class constants remain baked.

- [ ] **Step 5: Account for all typed opcode families**

Audit every opcode family returned by these searches:

```sh
rg -n "OPCODE_(ASSIGN_TYPED|RETURN_TYPED|TYPE_TEST|CAST_TO|CONSTRUCT_SPECIALIZED)" \
  modules/foundry_script/{fs_function.h,fs_byte_codegen.cpp,fs_vm.cpp}
rg -n "to_container_type\(|from_container_type\(|_get_default_variant_for_data_type" \
  modules/foundry_script
```

For every result, either route symbolic `Self` through the context-aware path or document why the operand cannot
contain symbolic `Self`. Include tuple elements, nullable types, trait-qualified types, and nested specialized generic
arguments.

- [ ] **Step 6: Rebuild and regenerate the canonical negative output**

```sh
python3 scripts/agent_build.py --backend ninja
./bin/foundry.macos.editor.dev.arm64 --headless test generate-fixtures \
  modules/foundry_script/tests/scripts
git diff -- modules/foundry_script/tests/scripts/runtime/errors/type_self_static_exact_receiver_argument.out
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*Script compilation and runtime*" --force-colors
```

Expected: only intentionally changed `.out` fixtures differ, the negative error names `Child`, and both source and
bytecode corpus cases pass.

- [ ] **Step 7: Commit runtime type resolution**

```sh
git add modules/foundry_script/fs_byte_codegen.h modules/foundry_script/fs_byte_codegen.cpp \
  modules/foundry_script/fs_function.h modules/foundry_script/fs_function.cpp \
  modules/foundry_script/fs_vm.cpp modules/foundry_script/fs_compiler.cpp \
  modules/foundry_script/fs_disassembler.cpp modules/foundry_script/fs_bytecode_verifier.cpp \
  modules/foundry_script/fs_bytecode_format.h modules/foundry_script/tests/test_fs_bytecode.h \
  modules/foundry_script/tests/scripts/runtime/errors/type_self_static_exact_receiver_argument.out
git commit -m "fix(foundry_script): Resolve runtime Self from call context"
```

### Task 5: Cover nested generics, conformances, and receiver kinds

**Files:**

- Create: `modules/foundry_script/tests/scripts/runtime/features/type_self_static_reified_types.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/type_self_static_reified_types.out`
- Create: `modules/foundry_script/tests/scripts/runtime/features/type_self_static_conformance_receivers.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/type_self_static_conformance_receivers.out`
- Create: `modules/foundry_script/tests/scripts/runtime/errors/type_self_static_nested_container_assignment.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/errors/type_self_static_nested_container_assignment.out`
- Modify if useful:
  `modules/foundry_script/tests/scripts/runtime/features/retroactive_conformance_native_self_generic.fs`

- [ ] **Step 1: Add the recursive reification matrix**

Create a fixture with a generic `Crate[T]` and an inherited static method returning or accepting each of:

- `Self`
- `Type[Self]`
- `Array[Self]`
- `Dictionary[String, Self]`
- `Crate[Self]`
- `Array[Crate[Dictionary[String, Type[Self]]]]`
- a typed callable with `Self` in both its parameter and return signature
- nullable `Self` where accepted by the grammar

Call through root, derived, and specialized derived handles. Assert element `ContainerType` names/type arguments and
observable typed-write behavior, not just returned values. Add a negative fixture that attempts to store the root type
inside a nested container reified for the derived receiver and verify the runtime rejects it.

- [ ] **Step 2: Add static conformance receiver coverage**

Define a trait with `static func make() -> Self` and cover:

- a script-class conformance invoked through a derived script handle;
- a native-class conformance invoked through a derived native class handle;
- a native-backed script subclass invoked through its script handle;
- a builtin target if static builtin witness invocation is currently supported;
- inherited supertrait witnesses, preserving the #1468/#1540 root/leaf matrix.

Assert each result is the exact receiver kind. Extend `retroactive_conformance_native_self_generic.fs` only when doing
so keeps the fixture focused; otherwise use the new matrix fixture.

- [ ] **Step 3: Verify specialized generic handles and extracted callables**

Add a generic base and at least two specializations whose inherited static method uses `Self` and its type arguments.
Extract the same inherited method from both specialized handles before either call. Verify each callable retains its
own specialization and no context leaks between sequential or nested calls.

- [ ] **Step 4: Run the full runtime fixture corpus**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*Script compilation and runtime*"
```

Expected: both source and compiled-bytecode cases pass all new matrices and the existing `type_self_*`, generic,
conformance, async, callable, and exact-inner-target fixtures.

- [ ] **Step 5: Commit the receiver-kind matrix**

```sh
git add modules/foundry_script/tests/scripts/runtime/features/type_self_static_reified_types.* \
  modules/foundry_script/tests/scripts/runtime/features/type_self_static_conformance_receivers.* \
  modules/foundry_script/tests/scripts/runtime/errors/type_self_static_nested_container_assignment.* \
  modules/foundry_script/tests/scripts/runtime/features/retroactive_conformance_native_self_generic.*
git commit -m "test(foundry_script): Cover exact static Self receiver matrix"
```

Exclude the existing fixture from the commit command if it was not modified.

### Task 6: Document semantics and validate the combined implementation

**Files:**

- Modify: `modules/foundry_script/GRAMMAR.md`
- Verify: all files changed for #1538 and #1482

- [ ] **Step 1: Update the normative `Self` semantics**

In the semantic prose near traits, conformances, and receiver-relative type parameters, specify:

- In a static method, executable `Self` denotes the exact class/type handle at the call boundary.
- Inherited bodies retain the derived or specialized receiver; explicit base calls use the explicit base.
- The rule applies recursively inside containers, tuples, `Type`, generic arguments, parameters, returns, defaults,
  casts, type tests, construction, callables, and async continuation state.
- Static conformance witnesses use the exact target handle selected at dispatch, across script, native,
  native-backed-script, specialized, and supported builtin targets.
- Method and witness selection remains declaration/base-chain based; receiver specialization does not redispatch.

- [ ] **Step 2: Run focused conformance and runtime suites**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*Script compilation and runtime*"
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*[Conformance]*" --force-colors
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*[FoundryScript][Bytecode]*" --force-colors
```

Expected: all selected tests pass. Use the platform-specific binary on Linux.

- [ ] **Step 3: Run the required native strict build and full suite**

```sh
python3 scripts/agent_build.py --test
```

On Linux with a GUI display available, use `DISPLAY=:1 python3 scripts/agent_build.py --test`. Monitor the exact
progress path printed by the wrapper. Expected: strict `dev_mode=yes dev_build=yes tests=yes` build succeeds and the
full C++ plus Foundry Script suite reports doctest success. Evaluate the known cleanup/leak exit behavior by the final
doctest summary as directed by `AGENTS.md`.

- [ ] **Step 4: Run repository hygiene checks**

```sh
git diff --check origin/develop...HEAD
git status --short
git log --oneline origin/develop..HEAD
git diff --stat origin/develop...HEAD
```

If Python, workflow, or policy files changed unexpectedly, also run the test-authoring self-checks from `AGENTS.md`.
Do not include generated files outside the intentional `.out` fixtures.

- [ ] **Step 5: Commit documentation**

```sh
git add modules/foundry_script/GRAMMAR.md
git commit -m "docs(foundry_script): Specify static Self receiver semantics"
```

- [ ] **Step 6: Review and open the single implementation PR**

Run the repository's supervised Codex branch review against `origin/develop`, address every actionable finding, and
repeat until clean. Push `issue-1482-1538` and open one PR targeting `develop` whose body includes:

```text
Closes #1482
Closes #1538

- makes conformance membership target-exact for same-file inner classes
- preserves exact static Self receiver identity through dispatch, callables, async state, and bytecode
- reifies nested Self types at the runtime point of use
```

Do not force-merge the implementation PR unless the user separately authorizes that future-session action.
