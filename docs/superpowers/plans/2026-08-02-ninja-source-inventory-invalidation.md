# Ninja Source Inventory Invalidation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Regenerate cached Ninja graphs whenever repository files are added, removed, or renamed while preserving incremental builds for ordinary content edits.

**Architecture:** Collect every tracked path plus non-ignored untracked graph candidates, with an exclusion-aware filesystem fallback for source archives. Add each repository-relative path and its presence state to the Ninja configuration fingerprint, while retaining content hashing only for build descriptions and selected configuration files.

**Tech Stack:** Python 3 standard library, `unittest`, SCons-generated Ninja graphs.

---

### Task 1: Specify source-inventory invalidation behavior

**Files:**
- Modify: `scripts/tests/test_agent_build.py`
- Test: `scripts/tests/test_agent_build.py`

- [ ] **Step 1: Write the failing addition/removal test**

Add a test that creates a temporary repository with `SConstruct`, records its Ninja state, adds
`core/object/class_handle.cpp`, records a different state, deletes the source, and asserts that the original state is
restored.

- [ ] **Step 2: Write the source-content stability test**

Add a test that creates `core/object/class_handle.cpp`, records its Ninja state, changes only the file contents, and
asserts that the state remains unchanged.

- [ ] **Step 3: Write the ignored-output stability test**

Add a test that initializes a temporary Git repository with `*.gen.*` ignored, records its Ninja state, creates an
ignored generated C++ file, and asserts that the state remains unchanged.

- [ ] **Step 4: Run the three tests and verify RED**

Run:

```sh
python3 -m unittest \
  scripts.tests.test_agent_build.AgentBuildCharacterizationTests.test_ninja_state_changes_when_repository_file_inventory_changes \
  scripts.tests.test_agent_build.AgentBuildCharacterizationTests.test_ninja_state_ignores_repository_source_content_changes \
  scripts.tests.test_agent_build.AgentBuildCharacterizationTests.test_ninja_state_ignores_gitignored_generated_files
```

Expected: the inventory test fails before path hashing is implemented; after raw path hashing is added, the ignored
generated-file test fails and exposes self-invalidating graph state. The content-stability test passes throughout.

### Task 2: Add repository paths to the fingerprint

**Files:**
- Modify: `scripts/agent_build.py`
- Test: `scripts/tests/test_agent_build.py`

- [ ] **Step 1: Collect source-controlled repository paths**

Replace the build-description-only walk with a helper that returns tracked paths plus non-ignored untracked graph
candidates below non-excluded directories in deterministic order. Retain an exclusion-aware filesystem fallback for
source archives, omitting known generated-output names.

- [ ] **Step 2: Hash repository-relative paths**

In `build_description_fingerprint()`, add every returned file's repository-relative POSIX path and presence state to
the SHA-256 input with an explicit inventory namespace and NUL delimiters. Do not read ordinary source contents.

- [ ] **Step 3: Run the focused tests and verify GREEN**

Run the three-test command from Task 1. Expected: all three tests pass.

- [ ] **Step 4: Run the complete wrapper test suite**

Run:

```sh
python3 -m unittest scripts.tests.test_agent_build
```

Expected: all tests pass with no errors or failures.

### Task 3: Verify the real graph and review the patch

**Files:**
- Verify: `scripts/agent_build.py`
- Verify: `scripts/tests/test_agent_build.py`

- [ ] **Step 1: Confirm the affected source is represented**

Resolve the current strict macOS Ninja state and confirm its generated graph contains both
`core/object/class_handle.cpp` and `bin/obj/core/object/class_handle.macos.editor.dev.arm64.o`. Generate the graph with
`python3 scripts/agent_build.py --backend ninja --jobs 12` if that state does not exist.

- [ ] **Step 2: Review repository state and diff**

Run:

```sh
git status --short
git diff --check
git diff -- scripts/agent_build.py scripts/tests/test_agent_build.py
```

Expected: only the intended documentation, wrapper, and test files are changed; `git diff --check` prints nothing.

- [ ] **Step 3: Commit the focused change**

Run:

```sh
git add docs/superpowers/specs/2026-08-02-ninja-source-inventory-invalidation-design.md \
  docs/superpowers/plans/2026-08-02-ninja-source-inventory-invalidation.md \
  scripts/agent_build.py scripts/tests/test_agent_build.py
git commit -m "fix(build): Invalidate Ninja graph for file inventory changes"
```
