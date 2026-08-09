# Class-Parameterized Container Return Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Retype runtime-erased containers returned by class- and trait-parameterized methods at concrete consumers.

**Architecture:** Classify erasure from the unspecialized declared return before receiver substitution, then reuse the
existing call/callable marker and compiler conversion opcodes. Keep untyped consumers and strict content validation.

**Tech Stack:** C++17, Foundry Script analyzer/compiler metadata, runtime fixtures, compiled-bytecode round trips.

---

### Task 1: Add failing generic class and trait runtime coverage

**Files:**
- Create: `modules/foundry_script/tests/scripts/runtime/features/class_parameter_typed_container_return.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/errors/class_parameter_typed_container_return_wrong_type.fs`

- [ ] **Step 1: Add the minimal generic-class reproduction**

```foundryscript
class Bag[T]:
	func singleton(value: T) -> Array[T]:
		var result: Array[T] = []
		result.append(value)
		return result

func test() -> void:
	var values: Array[int] = Bag[int].new().singleton(41)
	assert(values.get_typed_builtin() == TYPE_INT)
	assert(values == [41])
```

Extend the same fixture with inferred/later assignments, `Dictionary[String, T]`, `Array[Array[T]]`, direct trait
application, `ForwardingBag[U] uses Bag[U]`, and untyped targets whose metadata remains untyped.

- [ ] **Step 2: Add consumer and callable coverage**

Add typed argument, return, setter/property, instance/static field initializer, rich callable `call()`, and literal
`callv()` consumers following the existing generic-method and inherited-`Self` fixture patterns.

- [ ] **Step 3: Add strict incompatible-content coverage**

Return an erased array whose runtime contents conflict with the concrete destination and assert the existing conversion
error remains. Do not accept widening to `Variant`.

- [ ] **Step 4: Build and prove the feature fixture fails**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*Script compilation and runtime*"
```

Expected: the new feature case reports `Trying to assign an array of type "Array" to a variable of type "Array[int]"`.

### Task 2: Generalize erased-return classification

**Files:**
- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Modify: `modules/foundry_script/fs_analyzer.h`
- Modify: `modules/foundry_script/fs_analyzer_call_validation.cpp`

- [ ] **Step 1: Add one recursive declared-return predicate**

Implement a helper that accepts a declared return and parameter scope:

```cpp
static bool _container_return_involves_erased_parameter(
		const FSParser::DataType &p_return_type,
		FSParser::DataType::TypeParameterScope p_scope) {
	for (const FSParser::DataType &element : p_return_type.container_element_types) {
		if (_datatype_contains_type_parameter_from_scope(element, p_scope)) {
			return true;
		}
	}
	return false;
}
```

The recursive inner predicate traverses container elements and nested type arguments and matches only the requested
scope. Top-level non-container returns return false.

- [ ] **Step 2: Mark direct calls before substitution**

In `get_function_signature()`, inspect `found_function->get_datatype()` before `substitute_member_type()`. When it
contains a class-scoped parameter and the specialized return is concrete, set `CallNode::returns_erased_container`.
Retain the existing inherited-`Self` case.

- [ ] **Step 3: Mark captured method callables**

During `MEMBER_FUNCTION` reduction, set `method_return_is_erased_container` from the same declared-return predicate so
`call()` and `callv()` preserve classification.

- [ ] **Step 4: Reuse the predicate for generic methods**

Replace the open-coded loop in `CallSiteValidationContext::apply_generic_method_bindings()` with the shared predicate
for method-scoped parameters before binding substitution. Keep the final static return substitution unchanged.

- [ ] **Step 5: Rebuild and rerun the runtime corpus**

Run the Task 1 command. Expected: feature fixtures pass, and the wrong-content fixture fails with the expected strict
conversion diagnostic in both source and bytecode modes.

### Task 3: Verify all existing consumers and commit

**Files:**
- Verify: `modules/foundry_script/tests/scripts/runtime/features/generic_method_typed_container_return.fs`
- Verify: `modules/foundry_script/tests/scripts/runtime/features/type_self_callable_inherited_container_return.fs`
- Verify: `modules/foundry_script/tests/scripts/runtime/errors/generic_method_typed_container_return_wrong_type.fs`

- [ ] **Step 1: Run focused erased-container controls**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --suite "*[Modules][FoundryScript][Runtime]*"
```

Expected: existing generic-method and inherited-`Self` container cases remain unchanged and all new cases pass.

- [ ] **Step 2: Root review and commit**

```sh
git add modules/foundry_script/fs_analyzer.cpp \
  modules/foundry_script/fs_analyzer.h \
  modules/foundry_script/fs_analyzer_call_validation.cpp \
  modules/foundry_script/tests/scripts/runtime
git commit -m "Retype class-parameterized container returns"
```
