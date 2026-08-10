# Base-Applied Trait Lookup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve exact static types for trait members reached through a derived Foundry Script receiver.

**Architecture:** Add one ordered hierarchy-trait lookup helper to `FSAnalyzer`, then reuse it from identifier, call,
and explicit generic-method lookup. Ordinary class-member precedence remains unchanged; specialization stays anchored at
the original receiver and continues through `specialize_ancestor_type()`.

**Tech Stack:** C++17, Foundry Script analyzer, doctest, Foundry Script analyzer/runtime fixtures.

---

### Task 1: Lock the derived-receiver failure into analyzer tests

**Files:**
- Modify: `modules/foundry_script/tests/test_generic_analyzer.h`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/generic_trait_member_inherited_argument_wrong_type.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/generic_trait_member_inherited_variable_wrong_type.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/generic_trait_member_inherited_return_wrong_type.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/generic_trait_member_inherited_signal_wrong_type.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/generic_trait_member_inherited_container_wrong_type.fs`
- Create:
  `modules/foundry_script/tests/scripts/analyzer/errors/generic_trait_member_inherited_generic_method_wrong_type.fs`

- [ ] **Step 1: Add a focused C++ resolved-type regression**

Add a case named
`[Modules][FoundryScript][Generic] derived receiver specializes base-applied trait members`. Parse and analyze this
program through the existing helper used by neighboring cases:

```foundryscript
trait Holder[T]:
	var value: T
	func get_value() -> T:
		return value

class Base[T] uses Holder[T]:
	pass

class Derived extends Base[int]:
	pass

func read(holder: Derived) -> int:
	return holder.get_value()
```

Assert analysis succeeds and the resolved return/member datatype is `BUILTIN`/`Variant::INT`, not `TYPE_PARAMETER`
or `VARIANT`. Add a second case where an ordinary class member shadows an identically named derived/base trait member.

- [ ] **Step 2: Add negative fixtures for every flattened member route**

Use the direct `generic_trait_member_*_wrong_type.fs` fixtures as structural controls. Each derived fixture declares
`Base[T] uses Holder[T]`, `Derived extends Base[int]`, and performs one invalid `String` operation through `Derived`.
The expected `.out` must contain the existing concrete `int` diagnostic and no runtime-only assignment failure.

- [ ] **Step 3: Build and prove the new tests fail**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*derived receiver specializes base-applied trait members*"
```

Expected: build succeeds and the new doctest fails because lookup never reaches the base-applied trait or resolves the
member as non-concrete.

### Task 2: Implement one hierarchy-trait lookup policy

**Files:**
- Modify: `modules/foundry_script/fs_analyzer.h`
- Modify: `modules/foundry_script/fs_analyzer_surface.cpp`
- Modify: `modules/foundry_script/fs_analyzer.cpp`

- [ ] **Step 1: Declare the ordered helper and result type**

Add a private result type and helper beside `find_generic_method()`:

```cpp
struct HierarchyTraitMember {
	FSParser::ClassNode *trait = nullptr;
	const FSParser::ClassNode::Member *member = nullptr;
};

bool find_hierarchy_trait_member(FSParser::ClassNode *p_receiver_class,
		const StringName &p_name, const FSParser::Node *p_source,
		HierarchyTraitMember &r_result);
```

The helper walks only `base_type.class_type`, calls `resolve_trait_uses()` at each level, preserves
`resolved_traits` order, and uses a `HashSet<FSParser::ClassNode *>` to visit each trait once.

- [ ] **Step 2: Replace starting-class-only generic method lookup**

Keep the full ordinary class/base search in `find_generic_method()`. After it misses, call
`find_hierarchy_trait_member()`, set `r_found_member`, and return only a concrete generic `FUNCTION` member. Do not
reconstruct or copy a member into the applying class.

- [ ] **Step 3: Reuse the helper from identifier reduction**

Keep `get_class_node_current_scope_classes()` and ordinary/outer member lookup order unchanged. After ordinary lookup
misses for the receiver hierarchy, resolve the helper's returned member with its declaring trait. Feed
`specialize_ancestor_type(base, result.trait)` into existing variable/function substitution and explicit signal typing.
Keep the original `base` datatype for all specializations.

- [ ] **Step 4: Reuse the helper from function signature lookup**

After the ordinary class/base loop in `get_function_signature()`, use the helper instead of inspecting only
`original_base_class->resolved_traits`. Set `found_in_class` to the declaring trait so existing parameter/return
specialization projects `Derived -> Base[int] -> Holder[int]`.

- [ ] **Step 5: Run the focused C++ test**

Run the Task 1 build/test command again. Expected: PASS.

### Task 3: Cover positive, cross-file, and precedence behavior

**Files:**
- Create: `modules/foundry_script/tests/scripts/analyzer/features/generic_trait_member_inherited_application.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/generic_trait_member_inherited_global.fs`
- Create:
  `modules/foundry_script/tests/scripts/runtime/features/generic_trait_member_inherited_global_provider.notest.fs`
- Modify: `modules/foundry_script/tests/test_generic_analyzer.h`

- [ ] **Step 1: Add the positive fixture matrix**

Cover correct variable writes, method arguments/returns, signals, callable method values, and explicit generic methods
through bare, `self`, explicit `Derived`, and grandchild receivers. Include concrete-base use, forwarded `Base[T]`, two
independent `int`/`String` specializations, transitive traits, and a cross-file provider.

- [ ] **Step 2: Add compiler-compatible winner tests**

In C++, assert ordinary class/base members win before traits; then assert most-derived traits win before base traits,
diamond-reached traits are de-duplicated, and traits on a lexical outer class do not become inner-instance members.

- [ ] **Step 3: Rebuild and run focused corpora**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --suite "*[Modules][FoundryScript][Generic]*"
```

Expected: all new and existing generic trait/inheritance cases pass in source and compiled-bytecode modes.

- [ ] **Step 4: Root review and commit**

```sh
git add modules/foundry_script/fs_analyzer.h \
  modules/foundry_script/fs_analyzer.cpp \
  modules/foundry_script/fs_analyzer_surface.cpp \
  modules/foundry_script/tests/test_generic_analyzer.h \
  modules/foundry_script/tests/scripts/analyzer \
  modules/foundry_script/tests/scripts/runtime/features/generic_trait_member_inherited_global*
git commit -m "Fix inherited trait member specialization"
```
