# Nested Conformance Visibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Resolve every foreign analyzer node under the owning file's conformance visibility.

**Architecture:** Add one private RAII wrapper around `ScopedVisibility` and instantiate it at all five delegation
boundaries. Owner-scoped resolution makes cached results deterministic regardless of caller and analysis order.

**Tech Stack:** C++17, Foundry Script analyzer/cache, doctest, temporary multi-file projects.

---

### Task 1: Add order-independent cross-file failures

**Files:**
- Create: `modules/foundry_script/tests/test_conformance_nested_visibility.h`

- [ ] **Step 1: Add the foreign-member case in both orders**

Use `TemporaryProjectTree` to write `widget.fs`, `gadgetlike.fs`, `conformance.fs`, `b.fs`, and `a.fs` exactly as in
#1388. Analyze A then B, reset parser/global-class/conformance registry state, then analyze B then A. Assert A reports
`Could not resolve external class member "thing"` and B reports the concrete invalid assignment diagnostic.

- [ ] **Step 2: Add foreign-interface and valid controls**

Add a dependency interface whose method signature names a trait visible only to its caller; the dependent must not
borrow that conformance visibility. Add a negative control where the dependency itself preloads `conformance.fs`; both
analyses must then be clean.

- [ ] **Step 3: Build and prove the member case fails**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*dependency is resolved under its own conformance visibility*"
```

Expected: FAIL because the dependent analyzes clean under the caller's wider visibility.

### Task 2: Route visibility through every delegation

**Files:**
- Modify: `modules/foundry_script/fs_analyzer.h`
- Modify: `modules/foundry_script/fs_analyzer_surface.cpp`
- Modify: `modules/foundry_script/fs_analyzer.cpp`

- [ ] **Step 1: Add the private owner scope**

Place this helper next to `ConformanceVisibility`:

```cpp
class ForeignAnalyzerVisibilityScope {
	FSConformanceRegistry::ScopedVisibility visibility_scope;

public:
	explicit ForeignAnalyzerVisibilityScope(FSAnalyzer *p_owner) :
			visibility_scope(&p_owner->conformance_visibility) {}
};
```

Document that a foreign node's memoized result must be defined by its owning file and independent of the caller that
first forces it.

- [ ] **Step 2: Scope all five delegated calls**

Immediately before calling the owning analyzer for inheritance, member, interface, trait uses, and class body, create:

```cpp
ForeignAnalyzerVisibilityScope visibility_scope(other_analyzer);
```

Do not add raw `ScopedVisibility` instances at call sites and do not intersect caller/owner visibility.

- [ ] **Step 3: Run all new conformance cases**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*conformance visibility*"
```

Expected: member and interface invalid cases fail consistently in both orders; the dependency-owned preload control
passes.

### Task 3: Verify neighboring conformance behavior and commit

- [ ] **Step 1: Run focused suites**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*Conformance*"
```

Expected: all conformance cases pass without fixture regeneration.

- [ ] **Step 2: Root review and commit**

```sh
git add modules/foundry_script/fs_analyzer.h \
  modules/foundry_script/fs_analyzer.cpp \
  modules/foundry_script/fs_analyzer_surface.cpp \
  modules/foundry_script/tests/test_conformance_nested_visibility.h
git commit -m "Scope nested conformance resolution to owners"
```
