# Global Trait Signal Materialization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep signals supplied by external/global traits available after state-preserving script reloads.

**Architecture:** Expose the compiler's effective direct-plus-flattened member collection for reuse by the export-cache
rebuild. Rebuild the current signal table from that effective surface, preserving shadowing, diamond safety, and source
removals.

**Tech Stack:** C++17, Foundry Script compiler/runtime, file-backed script cache, doctest.

---

### Task 1: Add a failing state-preserving reload regression

**Files:**
- Modify: `modules/foundry_script/tests/test_foundry_script.cpp`
- Modify: `modules/foundry_script/tests/scripts/runtime/features/trait_flatten_global_companion.notest.fs`
- Modify: `modules/foundry_script/tests/scripts/runtime/features/trait_flatten_global_external.fs`

- [ ] **Step 1: Add typed signal behavior to the global trait fixture**

Declare `signal requested(value: int)` and a concrete `request(value: int)` method that emits it on the external trait.
Connect from the implementer and assert one delivery with the expected payload.

- [ ] **Step 2: Add a file-backed C++ reload case**

Use `TempScriptFile`, `GlobalScriptClassCacheBackup`, and `register_global_script_class()` from neighboring tests. Write
an external trait, direct implementer, and child. Load the implementer through `FSCache`, call `reload(true)`, and
assert:

```cpp
CHECK(script->has_script_signal(SNAME("requested")));
List<MethodInfo> signals;
script->get_script_signal_list(&signals);
CHECK(signals.size() == 1);
```

Instantiate direct and child objects, connect dynamically, invoke the trait method, and check exactly one delivery.
Include a composed/diamond trait control and a same-file control.

- [ ] **Step 3: Build and prove the reload case fails**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*global trait signals survive state-preserving reload*"
```

Expected: FAIL because `_update_exports()` clears `_signals` and restores only direct AST signals.

### Task 2: Reuse compiler-effective member collection during reload

**Files:**
- Modify: `modules/foundry_script/fs_compiler.h`
- Modify: `modules/foundry_script/fs_compiler.cpp`
- Modify: `modules/foundry_script/foundry_script.cpp`

- [ ] **Step 1: Expose a read-only effective-member collector**

Refactor the existing private collector into a static helper callable by `FoundryScript`:

```cpp
static void collect_effective_members(const FSParser::ClassNode *p_class,
		Vector<const FSParser::ClassNode::Member *> &r_members);
```

The helper first appends direct members, then invokes the existing flattening policy. Keep
`_is_flattenable_trait_member`,
ordinary class/base shadowing, resolved-trait order, and first-writer diamond behavior unchanged.

- [ ] **Step 2: Rebuild signals from effective members**

In `_update_exports()`, call `collect_effective_members(c, effective_members)` after analysis succeeds. Iterate that
vector for signals while keeping export-variable/group handling limited to the direct AST members. Clear `_signals`
before the rebuild so removed trait declarations do not remain stale.

- [ ] **Step 3: Rerun the file-backed regression**

Run the Task 1 command. Expected: PASS with one signal on direct and child instances and one observed emission.

### Task 3: Run controls and commit

- [ ] **Step 1: Run trait flattening and script reload suites**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --suite "*[Modules][FoundryScript]*Trait*"
```

Expected: all direct, global, same-file, composed, and existing trait signal cases pass.

- [ ] **Step 2: Root review and commit**

```sh
git add modules/foundry_script/fs_compiler.h \
  modules/foundry_script/fs_compiler.cpp \
  modules/foundry_script/foundry_script.cpp \
  modules/foundry_script/tests/test_foundry_script.cpp \
  modules/foundry_script/tests/scripts/runtime/features/trait_flatten_global*
git commit -m "Preserve global trait signals on reload"
```
