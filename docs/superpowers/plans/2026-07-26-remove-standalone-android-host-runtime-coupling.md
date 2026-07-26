# Remove Standalone Android Host-Runtime Coupling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Foundry build, validate, package, and release its Android host runtime entirely from the in-tree `:lib` module and Foundry-owned native cells.

**Architecture:** Keep the existing debug/dev/release by four-ABI native provenance matrix, deterministic ELF/JNI inspection, in-tree AAR assembly, source-template inspection, and API 36 device acceptance. Delete the standalone source pin, Git-object resolver, bundle handoff, and release diagnostics; the only prebuilt Gradle input is a Foundry-owned native-cell root produced by the current workflow.

**Tech Stack:** Python 3 `unittest`, Groovy Gradle/AGP, SCons, GitHub Actions, actionlint, Ruff, mypy, Android SDK 36, and the command-first Foundry test runner.

---

### Task 1: Specify the standalone-coupling absence contract

**Files:**
- Modify: `tests/python_build/test_android_runtime_build.py`
- Modify: `.github/scripts/test_android_runtime_workflows.py`
- Modify: `tests/python_build/test_android_runtime_surface.py`
- Modify: `tests/python_build/test_android_gradle_runtime_contract.py`

- [x] **Step 1: Replace standalone resolver tests with an architecture absence test**

  Make `test_android_runtime_build.py` require the in-tree `:lib` module and reject these paths and production fragments:

  ```python
  REMOVED_PATHS = (
      "platform/android/foundry_android_runtime.json",
      "platform/android/android_runtime_build.py",
      "platform/android/android_native_bundle.py",
  )
  FORBIDDEN_FRAGMENTS = (
      "Foundry-Android",
      "foundryAndroidSource",
      "foundryAndroidFetch",
      "foundryNativeBundle",
      "foundryRuntimeScratch",
      "foundry-native.zip",
      "WS2_REMOVE_ANDROID_RUNTIME_COMPAT_BRIDGE",
  )
  ```

- [x] **Step 2: Require workflows and release jobs to use only in-tree inputs**

  Assert that Android assembly still downloads all 12 Foundry native cells and runs:

  ```text
  ./gradlew --no-daemon generateFoundryTemplates
  -PfoundryNativeRoot="${GITHUB_WORKSPACE}/.test_scratch/android-native"
  ```

  Reject a Foundry-Android checkout, pinned ref, source/fetch/bundle/scratch properties, standalone runtime naming, runtime AAR publication, and release uploads of `foundry-native.zip`.

- [x] **Step 3: Run the RED contracts**

  Run:

  ```sh
  python3 -m unittest \
    tests.python_build.test_android_runtime_build \
    tests.python_build.test_android_gradle_runtime_contract
  python3 tests/python_build/test_android_runtime_surface.py
  python3 .github/scripts/test_android_runtime_workflows.py
  ```

  Expected: failures name only the pin/resolver/bundle paths, temporary Gradle/SCons properties, workflow checkout and scratch handoff, and stale release diagnostics.

### Task 2: Replace the runtime pin contract with an internal native contract

**Files:**
- Delete: `platform/android/android_runtime_contract.py`
- Create: `platform/android/android_native_contract.py`
- Delete: `platform/android/android_native_bundle.py`
- Modify: `platform/android/android_native_staging.py`
- Modify: `platform/android/platform_android_builders.py`
- Modify: `tests/python_build/test_android_runtime_contract.py`
- Modify: `tests/python_build/test_android_native_staging.py`
- Delete: `tests/python_build/test_android_native_bundle.py`
- Rename and modify: `tests/python_build/android_native_bundle_test_support.py` → `tests/python_build/android_native_test_support.py`

- [x] **Step 1: Write RED native artifact and staging tests**

  Require the replacement contract to validate canonical provenance, exact debug/dev/release × four-ABI cells, library hashes, ELF class/machine, stale `org.godotengine` JNI names, the required Swappy allowlist, and identical Foundry JNI exports across every `libfoundry_android.so`.

  Require staging to accept exactly one of `--local-root` and `--native-root`, reject a subset prebuilt matrix, and replace a full stage with a changed-input ABI subset without stale files.

- [x] **Step 2: Run the RED native tests**

  Run:

  ```sh
  python3 -m unittest \
    tests.python_build.test_android_runtime_contract \
    tests.python_build.test_android_native_staging
  ```

  Expected: failures show the missing `android_native_contract.py`, bundle-only staging arguments, and absent direct ELF/JNI validation.

- [x] **Step 3: Implement the internal contract and direct staging**

  Move build specifications, provenance writing, source identity, canonical JSON, and matrix validation into `android_native_contract.py`. Integrate the current bundle inspector's ELF/JNI validation directly into `validate_native_matrix()` so no archive is required.

  Simplify `android_native_staging.py` to:

  ```text
  --revision <sha>
  --build-type debug|dev|release
  --selected-abis <android-abi-list>
  --output <revision/input/selection stage>
  (--local-root <Foundry bin matrix> | --native-root <downloaded Foundry matrix>)
  ```

  Full `--native-root` mode validates all 12 cells before staging. Local mode validates the selected current-revision cells. Both modes replace the owned stage as one complete directory.

- [x] **Step 4: Run the GREEN native tests**

  Run the two modules from Step 2 plus:

  ```sh
  python3 -m unittest tests.python_build.test_android_native_staging
  ```

  Expected: all provenance, ELF/JNI, matrix, and stale-stage tests pass.

### Task 3: Retarget Gradle and SCons callers to the in-tree host

**Files:**
- Modify: `platform/android/java/build.gradle`
- Modify: `platform/android/java/lib/build.gradle`
- Modify: `platform/android/platform_android_builders.py`
- Modify: `platform/android/detect.py`
- Modify: `tests/python_build/test_android_gradle_behavioral.py`
- Modify: `tests/python_build/test_android_gradle_runtime_contract.py`

- [x] **Step 1: Write RED real-Gradle caller tests**

  Keep real checked-in-wrapper tests for native-root staging, incomplete production ABI rejection, missing-Git zero revision, and changed-input subset isolation. Reject `foundryAndroidSource`, `foundryAndroidFetch`, `foundryNativeBundle`, and `foundryRuntimeScratch` in root and library Gradle files.

- [x] **Step 2: Run the RED Gradle tests**

  Run:

  ```sh
  python3 -m unittest \
    tests.python_build.test_android_gradle_runtime_contract \
    tests.python_build.test_android_gradle_behavioral
  ```

  Expected: static absence assertions fail against the temporary bridge while the retained real-Gradle behaviors identify the old scratch/bundle command line.

- [x] **Step 3: Remove the compatibility properties**

  Keep `foundryNativeRoot` as the optional Foundry-owned CI matrix input. Use local `bin/android-native/<revision>` otherwise. Remove source/fetch/bundle/runtime-scratch properties and SCons options. Preserve revision/input/ABI-scoped staging and all-four-ABI production validation.

- [x] **Step 4: Run the GREEN Gradle tests**

  Re-run the modules from Step 2. Expected: static and real-Gradle tests pass, with exact native-root staging and no external source or bundle path.

### Task 4: Retarget CI, release, artifacts, and documentation

**Files:**
- Modify: `.github/workflows/android_builds.yml`
- Modify: `.github/workflows/android_java_check.yml`
- Modify: `.github/workflows/release.yml`
- Modify: `.github/scripts/test_android_runtime_workflows.py`
- Modify: `platform/android/android_source_template.py`
- Modify: `platform/android/ANDROID_RUNTIME.md`
- Modify: `platform/android/README.md`

- [x] **Step 1: Assemble directly from `:lib`**

  Remove the Foundry-Android checkout. Download the 12 Foundry-owned native-cell artifacts, pass only `-PfoundryNativeRoot`, and keep AAR/APK/source-template assertions and artifact uploads under in-tree host/template names.

- [x] **Step 2: Remove standalone release diagnostics**

  Keep release debug/release APK and source-template artifacts. Remove the standalone runtime AAR/native-bundle diagnostic upload and all source pin, resolver, preparation, or Maven publication language.

- [x] **Step 3: Rewrite ownership and acceptance documentation**

  Document local SCons and prebuilt Foundry native-root modes, internal `:lib` AARs, deterministic native/JNI checks, source-template/APK inspection, and API 36 default/custom-ID acceptance. Do not document any standalone repository input or archive handoff.

- [x] **Step 4: Run workflow and documentation contracts**

  Run:

  ```sh
  python3 .github/scripts/test_android_runtime_workflows.py
  python3 tests/python_build/test_android_runtime_surface.py
  python3 -m unittest tests.python_build.test_android_gradle_runtime_contract
  ```

  Expected: all tests pass.

### Task 5: Run complete verification and commit

**Files:**
- Verify: all changed files

- [x] **Step 1: Run all Android Python contracts**

  Run:

  ```sh
  python3 -m unittest discover -s tests/python_build -p 'test_android*.py'
  python3 .github/scripts/test_android_runtime_workflows.py
  python3 misc/scripts/test_android_runtime_identifiers.py
  ```

- [x] **Step 2: Run formatting, typing, workflow, and Gradle gates**

  Run Ruff format/check and mypy over changed Python files, actionlint over the three Android/release workflows, and the checked-in Gradle wrapper's unit, lint, AAR, app, source-template, and instrumented-test tasks with a valid 12-cell native root.

- [x] **Step 3: Inspect final artifacts**

  Verify all three in-tree AARs, all three APKs, `android_source.zip`, exact AAR/JNI/native matrices, and API 36 default/custom application-ID evidence. Record environment-only emulator skips instead of substituting source-only evidence.

- [x] **Step 4: Run the strict Foundry and full command-first gates**

  Run:

  ```sh
  python3 scripts/agent_build.py
  DISPLAY=:1 ./bin/foundry.* --headless test run \
    --progress-format=jsonl \
    --progress-file /tmp/foundry-1243-test-progress.jsonl \
    --force-colors
  ```

- [x] **Step 5: Prove no production path reads Foundry-Android**

  Run:

  ```sh
  rg -n --hidden --glob '!docs/superpowers/**' --glob '!tests/**' \
    --glob '!.git/**' 'Foundry-Android|foundry_android_runtime|foundryAndroidSource|foundryAndroidFetch|foundryNativeBundle|foundryRuntimeScratch|foundry-native.zip' \
    platform/android .github
  ```

  Expected: no production build, CI, or release match.

- [x] **Step 6: Commit the verified change**

  Stage only Workstream 2 files, review `git diff --cached`, commit with an imperative subject under 72 characters, and confirm `git status --short` is empty.
