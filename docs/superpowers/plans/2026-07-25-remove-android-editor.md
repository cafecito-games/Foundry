# Remove the On-Device Android Editor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the Android editor product surface while keeping Android
runtime, export templates, plugins, OpenXR, and desktop exporting supported.

**Architecture:** Delete the editor application at its Gradle boundary, collapse
the Android library to its template runtime variant, then remove the
now-unreachable editor bridge from Java/Kotlin and C++. Protect the result with
a source-contract test that asserts both removed and preserved surfaces.

**Tech Stack:** SCons/C++, Gradle/Groovy, Java/Kotlin, Python contract tests, GitHub Actions.

---

### Task 1: Lock the removal boundary

**Files:**
- Create: `tests/python_build/test_android_runtime_surface.py`
- Modify: `.github/workflows/android_builds.yml`

- [ ] Add a source-contract test that accumulates violations for every forbidden
  editor module, flavor, task, publication, JNI bridge, CI artifact, and
  documentation surface.
- [ ] In the same test, require the `:app`, template `:lib`, runtime API,
  plugin, service, remote-fragment, XR/OpenXR, and desktop exporter surfaces.
- [ ] Run `python3 tests/python_build/test_android_runtime_surface.py` and
  confirm it fails because the current editor surface is present while the
  preservation checks pass.
- [ ] Add the contract test as a prerequisite job in the reusable Android workflow.

### Task 2: Remove Gradle and publication surfaces

**Files:**
- Delete: `platform/android/java/editor/`
- Modify: `platform/android/java/settings.gradle`
- Modify: `platform/android/java/build.gradle`
- Modify: `platform/android/java/lib/build.gradle`
- Modify: `platform/android/java/scripts/publish-module.gradle`
- Modify: `platform/android/java/PUBLISHING.md`

- [ ] Remove `:editor`, editor distributions/tasks/copy/clean paths, and editor native-library routing.
- [ ] Collapse `:lib` to the `template` product flavor and retain template debug/release publications.
- [ ] Remove the `foundry-tools` publication and document only runtime artifacts.
- [ ] Run Gradle task/model checks and the contract test.

### Task 3: Remove editor-only runtime and JNI bridges

**Files:**
- Delete: `platform/android/java/lib/src/main/java/games/cafecito/foundry/BuildProvider.java`
- Delete: `platform/android/java/lib/src/main/java/games/cafecito/foundry/editor/`
- Delete: `platform/android/java/lib/src/main/java/games/cafecito/foundry/utils/BenchmarkUtils.kt`
- Delete: `platform/android/editor/`
- Delete: `platform/android/export/android_editor_gradle_runner.cpp`
- Delete: `platform/android/export/android_editor_gradle_runner.h`
- Modify: Android runtime Java/Kotlin host and JNI declaration files
- Modify: Android native wrapper, OS, SCons, and exporter files

- [ ] Remove editor-only host APIs and their Java/Kotlin delegates/callbacks.
- [ ] Remove corresponding JNI declarations, method lookups, wrapper methods, OS hooks, and editor Gradle runner.
- [ ] Keep plugin feature support, service/remote runtime, XR, app restart, and desktop Gradle/apksigner behavior.
- [ ] Make `platform=android target=editor` fail with a focused error.
- [ ] Run the contract test, JVM/Gradle checks, and focused native checks.

### Task 4: Remove CI, release, and documentation support

**Files:**
- Modify: `.github/workflows/android_builds.yml`
- Modify: `.github/workflows/release.yml`
- Modify: `doc/classes/EditorSettings.xml`

- [ ] Remove editor matrices, tasks, publications, and editor artifacts while retaining template debug/release coverage.
- [ ] Remove Android-editor-only settings documentation.
- [ ] Search the intended source/workflow/docs surface for stale product references and run the contract test.

### Task 5: Verify, review, and deliver

**Files:**
- Modify only files required by verified findings.

- [ ] Build Android template native libraries and run `generateFoundryTemplates`.
- [ ] Assemble/test Gradle runtime/template outputs and inspect generated artifacts.
- [ ] Run relevant strict macOS build/tests and repository hygiene checks.
- [ ] Commit all intended changes and run read-only Cursor review against `origin/develop`.
- [ ] Triage, fix, verify, commit, and re-review until Cursor returns exactly `RESULT: clean`.
- [ ] Push, open a PR targeting `develop` whose body ends `Closes #1220`,
  enable squash auto-merge, monitor the merge, set Experiment status to Done,
  and clean the worktree/branches.
