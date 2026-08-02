# Issue #1538 Inner-Class Conformance Identity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prevent a retroactive conformance on one Foundry Script inner class from transferring to sibling or unrelated
classes that share the same resource path.

**Architecture:** Centralize the runtime target-identity query in `FSConformanceRegistry`. Query a Foundry Script by
fully qualified class name and distinct global name, and permit the resource-path alias only for a root script. Reuse
that rule from both `FoundryScript::_has_script_trait()` and `FSDataType::_script_conforms_to_trait()` while leaving
stored registry aliases and native-base inheritance unchanged.

**Tech Stack:** C++17, Foundry Script runtime fixtures, doctest, compiled `.fsb` round-trip fixtures, SCons/Ninja agent
build wrapper.

---

## Preconditions and shared-PR contract

- Start from a fresh branch named `issue-1482-1538` at `origin/develop`.
- This is the first implementation plan on that branch. Commit its focused fix, but do not open a PR yet.
- Continue on the same branch with
  `docs/superpowers/plans/2026-08-02-issue-1482-static-self-call-context.md`.
- The eventual implementation PR must close both #1538 and #1482. The planning-doc PR that introduced this file must
  not close either issue.
- Use `python3 scripts/agent_build.py`; do not invoke raw `scons` for agent-driven builds.

### Task 1: Add source and bytecode regressions for sibling membership isolation

**Files:**

- Create:
  `modules/foundry_script/tests/scripts/runtime/features/retroactive_conformance_inner_class_membership_isolation.fs`
- Create:
  `modules/foundry_script/tests/scripts/runtime/features/retroactive_conformance_inner_class_membership_isolation.out`
- Create: `modules/foundry_script/tests/scripts/runtime/errors/retroactive_conformance_inner_class_typed_assignment.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/errors/retroactive_conformance_inner_class_typed_assignment.out`

- [ ] **Step 1: Add the positive membership fixture**

Create the feature fixture with two sibling classes in one file, deliberately giving the first a leaf conformance and
the second only the root conformance:

```foundry
# Inner classes share one resource path but remain distinct conformance targets. Exact class identity
# must win over the file-path alias in source execution and after a compiled-bytecode round trip.
trait Root:
	pass


trait Leaf uses Root:
	pass


trait Unrelated:
	pass


class A:
	pass


class B:
	pass


extend A uses Leaf:
	pass


extend B uses Root:
	pass


func accepts_root(_value: Root) -> bool:
	return true


func test() -> void:
	var a := A.new()
	var b := B.new()
	var a_dynamic: Variant = a
	var b_dynamic: Variant = b

	print(a is Leaf)
	print(a is Root)
	print(a is Unrelated)
	print(b is Root)
	print(b is Leaf)
	print(b is Unrelated)
	print(is_instance_of(a_dynamic, Leaf))
	print(is_instance_of(b_dynamic, Leaf))
	print((a_dynamic as Leaf) != null)
	print((b_dynamic as Leaf) == null)

	var a_root: Root = a_dynamic
	var b_root: Root = b_dynamic
	print(accepts_root(a_root))
	print(accepts_root(b_root))
```

Create the expected output:

```text
FS_TEST_OK
true
true
false
true
false
false
true
false
true
true
true
true
```

- [ ] **Step 2: Add the negative dynamic typed-assignment fixture**

Create a runtime-error fixture proving that `FSDataType::_script_conforms_to_trait()` rejects `B` when a dynamic value
is assigned to a `Leaf` slot:

```foundry
trait Root:
	pass


trait Leaf uses Root:
	pass


class A:
	pass


class B:
	pass


extend A uses Leaf:
	pass


extend B uses Root:
	pass


func test() -> void:
	var dynamic_b: Variant = B.new()
	var leaked: Leaf = dynamic_b
	print(leaked)
```

Seed the `.out` as a runtime-error expectation. The supported fixture generator will fill the repository's canonical
diagnostic spelling after the fix; the generated message must identify `B` as incompatible with `Leaf`:

```text
FS_TEST_RUNTIME_ERROR
```

- [ ] **Step 3: Run both fixtures against the existing binary and verify RED**

Run without rebuilding:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*Script compilation and runtime*" --force-colors
```

Expected: FAIL. The feature fixture prints `true` for at least one `B is Leaf`/`is_instance_of`/`as` assertion, and the
negative fixture reaches `print(leaked)` instead of producing the expected runtime error. On Linux use the checkout's
`bin/foundry.linuxbsd.editor.dev.x86_64` binary with the same command-first arguments.

- [ ] **Step 4: Commit the failing regressions**

```sh
git add \
  modules/foundry_script/tests/scripts/runtime/features/retroactive_conformance_inner_class_membership_isolation.fs \
  modules/foundry_script/tests/scripts/runtime/features/retroactive_conformance_inner_class_membership_isolation.out \
  modules/foundry_script/tests/scripts/runtime/errors/retroactive_conformance_inner_class_typed_assignment.fs \
  modules/foundry_script/tests/scripts/runtime/errors/retroactive_conformance_inner_class_typed_assignment.out
git commit -m "test(foundry_script): Reproduce inner-class conformance leak"
```

### Task 2: Centralize authoritative script-target identity

**Files:**

- Modify: `modules/foundry_script/fs_conformance_registry.h`
- Modify: `modules/foundry_script/fs_conformance_registry.cpp`
- Modify: `modules/foundry_script/foundry_script.cpp:1951`
- Modify: `modules/foundry_script/fs_function.cpp:38`

- [ ] **Step 1: Declare the registry query**

Forward-declare `Script` in `fs_conformance_registry.h` and add this public query beside `has_conformance`:

```cpp
class Script;

// Tests the authoritative aliases of one script class. A resource path identifies a Foundry
// Script target only when the target is that file's root class; inner classes share the path.
bool script_has_conformance(const Script *p_script, const StringName &p_trait_name,
		bool p_include_runtime = false) const;
```

- [ ] **Step 2: Implement the exact alias order**

Implement the method in `fs_conformance_registry.cpp` using the already-included `foundry_script.h`:

```cpp
bool FSConformanceRegistry::script_has_conformance(const Script *p_script,
		const StringName &p_trait_name, bool p_include_runtime) const {
	if (p_script == nullptr || p_trait_name == StringName()) {
		return false;
	}

	const FoundryScript *foundry_script = Object::cast_to<FoundryScript>(p_script);
	if (foundry_script != nullptr &&
			has_conformance(foundry_script->get_fully_qualified_name(), p_trait_name, p_include_runtime)) {
		return true;
	}

	const StringName global_name = p_script->get_global_name();
	if (global_name != StringName() &&
			(foundry_script == nullptr || String(global_name) != foundry_script->get_fully_qualified_name()) &&
			has_conformance(String(global_name), p_trait_name, p_include_runtime)) {
		return true;
	}

	if (foundry_script != nullptr && !foundry_script->is_root_script()) {
		return false;
	}
	const String path = foundry_script != nullptr ? foundry_script->get_script_path() : p_script->get_path();
	return !path.is_empty() && has_conformance(path, p_trait_name, p_include_runtime);
}
```

Do not remove path aliases from `Conformance::target_keys`, `RuntimeConformance::target_keys`, the parse index, or the
runtime trait index. Root scripts still need the path key as their canonical identity, and old serialized registrations
must continue to load.

- [ ] **Step 3: Replace `FoundryScript`'s open-coded alias checks**

In `FoundryScript::_has_script_trait()`, keep direct/self trait checks and base recursion unchanged. Replace the FQCN,
global-name, and unconditional path block with:

```cpp
const FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
if (registry->script_has_conformance(this, p_trait, p_include_runtime)) {
	return true;
}
```

- [ ] **Step 4: Replace `FSDataType`'s open-coded alias checks**

In `FSDataType::_script_conforms_to_trait()`, preserve the base-script walk and native-base fallback but make each loop
iteration use the same registry rule:

```cpp
const FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
Ref<Script> script = p_base;
while (script.is_valid()) {
	if (registry->script_has_conformance(script.ptr(), p_trait, true)) {
		return true;
	}
	script = script->get_base_script();
}
return registry->native_class_conforms(p_base->get_instance_base_type(), p_trait, true);
```

- [ ] **Step 5: Rebuild and verify GREEN in source and bytecode modes**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*Script compilation and runtime*"
```

Expected: both doctest cases pass. The ordinary case and the compiled-bytecode round-trip case each run the new feature
and runtime-error fixtures. If the negative `.out` needs the canonical runtime diagnostic, regenerate all script
fixtures with the supported command, inspect this fixture's diff, and rerun:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test generate-fixtures \
  modules/foundry_script/tests/scripts
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*Script compilation and runtime*" --force-colors
```

- [ ] **Step 6: Commit the identity fix**

```sh
git add modules/foundry_script/fs_conformance_registry.h \
  modules/foundry_script/fs_conformance_registry.cpp \
  modules/foundry_script/foundry_script.cpp \
  modules/foundry_script/fs_function.cpp \
  modules/foundry_script/tests/scripts/runtime/errors/retroactive_conformance_inner_class_typed_assignment.out
git commit -m "fix(foundry_script): Isolate inner-class conformances"
```

### Task 3: Audit witness dispatch for the same non-unique alias

**Files:**

- Modify if the regression proves necessary: `modules/foundry_script/foundry_script.cpp:2776`
- Test: `modules/foundry_script/tests/scripts/runtime/features/retroactive_conformance_inner_class_target.fs`
- Test: `modules/foundry_script/tests/scripts/runtime/features/retroactive_conformance_inner_class_static_witness.fs`

- [ ] **Step 1: Run the existing exact-inner-target fixtures**

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*Script compilation and runtime*" --force-colors
```

Expected: the existing `retroactive_conformance_inner_class_target` and
`retroactive_conformance_inner_class_static_witness` outputs remain unchanged in both corpus passes.

- [ ] **Step 2: Remove the instance witness path fallback if it can cross class boundaries**

`FSInstance::callp()` currently walks FQCN/global/path aliases after a method miss, while static dispatch already uses
the exact `find_witness_function_for_target()` query. If the audit confirms that an unrelated sibling can reach a
witness by path, replace the open-coded alias lookup inside the base loop with:

```cpp
FSFunction *witness = registry->find_witness_function_for_target(cursor, p_method);
if (witness != nullptr && !witness->is_static()) {
	return witness->call(this, p_args, p_argcount, r_error);
}
```

Add a same-file sibling fixture only if needed to make this behavior fail before the change. Do not alter
method-before-witness precedence, base-chain order, or visibility rules.

- [ ] **Step 3: Commit only if the audit required a dispatch change**

```sh
git add modules/foundry_script/foundry_script.cpp \
  modules/foundry_script/tests/scripts/runtime/features
git commit -m "fix(foundry_script): Keep inner-class witnesses target-exact"
```

If no dispatch change is needed, record the inspected exact-target helper and passing fixture names in the eventual PR
description; do not create an empty commit.

### Task 4: Validate #1538 before starting #1482

**Files:**

- Verify only; no new files expected.

- [ ] **Step 1: Run focused source and bytecode fixtures**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*Script compilation and runtime*"
```

Expected: 2 doctest cases pass, including the positive and negative #1538 fixtures in both source and restored-bytecode
modes.

- [ ] **Step 2: Run conformance-focused C++ tests**

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*[Conformance]*" --force-colors
```

Expected: all selected tests pass. On Linux use the platform-specific binary.

- [ ] **Step 3: Check scope and hand off the shared branch**

```sh
git status --short
git log --oneline origin/develop..HEAD
git diff --stat origin/develop...HEAD
```

Expected: only #1538 tests and the focused registry/runtime callers are changed after the planning documents. Leave the
branch and worktree intact, then execute the #1482 plan in a later session before opening the single implementation PR.
