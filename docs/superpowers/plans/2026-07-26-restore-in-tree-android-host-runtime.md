# Restore In-Tree Android Host Runtime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore Foundry's current Java/Kotlin/AIDL Android application host as the internal `platform/android/java/lib` module and reconnect the engine's Android template build to it.

**Architecture:** Foundry owns and compiles the host runtime from `:lib`; `:app` uses a Gradle project dependency whenever it is built in this repository. The generated custom-build source template remains self-contained by copying the three internally built AAR variants into `app/libs`, but standalone repository preparation, Maven publication, signing, compatibility metadata, and native-bundle extraction are not part of `:lib`. Runtime sources, resources, identity metadata, API 36 fixes, tests, and JNI names come from the read-only `Foundry-Android/runtime` donor.

**Tech Stack:** Python `unittest` ownership contracts, Gradle 8.11.1/AGP 8.9.1, Java 17, Kotlin 2.1.21, Android API 36, AIDL, SCons Android native builds

---

### Task 1: Define the restored ownership contract

**Files:**
- Modify: `tests/python_build/test_android_runtime_surface.py`
- Modify: `tests/python_build/test_android_gradle_runtime_contract.py`
- Modify: `misc/scripts/test_android_runtime_identifiers.py`

- [ ] **Step 1: Require the in-tree runtime surface**

  Replace the standalone-owner assertions with exact checks for `platform/android/java/lib/build.gradle`, the main
  Java/Kotlin tree, the two licensing AIDL files, layouts/strings/provider resources, notices, JVM tests, Android
  tests, `include ':lib'`, and the internal app dependency:

  ```python
  for path in (
      "platform/android/java/lib/build.gradle",
      "platform/android/java/lib/src/main/java/games/cafecito/foundry/Foundry.kt",
      "platform/android/java/lib/src/main/aidl/com/android/vending/licensing/ILicensingService.aidl",
      "platform/android/java/lib/src/main/res/layout/foundry_app_layout.xml",
      "platform/android/java/lib/src/test/java/games/cafecito/foundry/RuntimeIdentityTest.java",
      "platform/android/java/lib/src/androidTest/java/games/cafecito/foundry/RuntimeIdentityInstrumentedTest.kt",
  ):
      require_path(path)
  require_text("platform/android/java/settings.gradle", "include ':lib'")
  require_text("platform/android/java/app/build.gradle", 'implementation project(":lib")')
  ```

- [ ] **Step 2: Reject standalone preparation and publication**

  Assert that active Gradle files contain none of `prepareFoundryAndroidRuntime`, `foundryAndroidSource`,
  `foundryAndroidFetch`, `foundryRuntimeAarRoot`, `maven-publish`, `signing`, `MavenPublication`,
  `nexusPublishing`, `foundryNativeBundle`, or donor repository URLs. Require the root copy tasks to depend on
  `:lib:assembleTemplateDebug`, `:lib:assembleTemplateDev`, and `:lib:assembleTemplateRelease`, and require the
  stable `foundry-debug.aar`, `foundry-dev.aar`, and `foundry-release.aar` source-template payloads.

- [ ] **Step 3: Preserve identity, JNI, and application-ID guards**

  Change `misc/scripts/test_android_runtime_identifiers.py` to require the in-tree runtime rather than reject it.
  Extend the Python contract to require `games.cafecito.foundry` namespace/manifest metadata, the API 36
  `BAKLAVA` host-token guard, immutable downloader alarm, canonical JNI declarations, canonical C++ exports,
  and the default/custom application-ID acceptance inputs.

- [ ] **Step 4: Run the focused contracts and capture RED**

  Run:

  ```sh
  python3 -m unittest \
    tests.python_build.test_android_runtime_surface \
    tests.python_build.test_android_gradle_runtime_contract
  python3 misc/scripts/test_android_runtime_identifiers.py
  ```

  Expected: failures report missing `platform/android/java/lib`, missing `include ':lib'` and project dependency,
  and forbidden standalone runtime preparation. Record this output before restoring production files.

- [ ] **Step 5: Commit the RED contracts and plan**

  ```sh
  git add docs/superpowers/plans/2026-07-26-restore-in-tree-android-host-runtime.md \
    tests/python_build/test_android_runtime_surface.py \
    tests/python_build/test_android_gradle_runtime_contract.py \
    misc/scripts/test_android_runtime_identifiers.py
  git commit -m "Test in-tree Android runtime ownership"
  ```

### Task 2: Restore the current host runtime as internal `:lib`

**Files:**
- Create: `platform/android/java/lib/build.gradle`
- Restore/update: `platform/android/java/lib/patches/**`
- Restore/update: `platform/android/java/lib/src/main/**`
- Restore/update: `platform/android/java/lib/src/test/**`
- Create: `platform/android/java/lib/src/androidTest/**`

- [ ] **Step 1: Restore the donor runtime content**

  Restore the former in-tree runtime tree, then apply every semantic difference found in the read-only donor:
  runtime/manifest compatibility metadata and alarm receiver, immutable downloader `PendingIntent`, API 36
  `SurfaceControlViewHost` token guard, lint annotations, provider-path notice, JVM identity test, Android identity
  and plugin-protocol tests, and packaged license/notice resources. Do not copy `runtime/build/`, standalone lint
  patch application, repository compatibility files, native bundle tools, or release/publication files.

- [ ] **Step 2: Implement the internal library Gradle module**

  Configure `com.android.library` and Kotlin only, with namespace `games.cafecito.foundry`, API 36/min API 24,
  Java/Kotlin 17, AIDL and BuildConfig, `template` flavor, debug/dev/release types, strict lint, JVM default values,
  unit/instrumented dependencies, and `libs/{debug,dev,release}` JNI inputs. Populate:

  ```groovy
  buildConfigField 'String', 'FOUNDRY_BINDINGS_VERSION', "\"${getFoundryLibraryVersionName()}\""
  buildConfigField 'String', 'FOUNDRY_ENGINE_VERSION', "\"${getFoundryLibraryVersionName()}\""
  buildConfigField 'String', 'FOUNDRY_ENGINE_REVISION', "\"${getFoundryEngineRevision()}\""
  buildConfigField 'int', 'FOUNDRY_JNI_CONTRACT_VERSION', '1'
  ```

  Name AAR outputs `foundry-debug.aar`, `foundry-dev.aar`, and `foundry-release.aar`. Keep SCons native compile tasks
  scoped to the selected ABIs and merge tasks; do not add publication, signing, external repository, compatibility
  metadata, native-bundle extraction, or standalone artifact inspection.

- [ ] **Step 3: Restore engine-owned version helpers**

  Add `getFoundryLibraryVersionName()` and `getFoundryEngineRevision()` to
  `platform/android/java/app/config.gradle`, deriving the library version from the repository `version.py` and the
  revision from the current Git checkout with a deterministic non-release fallback.

- [ ] **Step 4: Run GREEN library compilation/tests**

  Run:

  ```sh
  platform/android/java/gradlew -p platform/android/java \
    :lib:testTemplateDebugUnitTest \
    :lib:lintTemplateDebug \
    :lib:compileTemplateDebugJavaWithJavac \
    :lib:compileTemplateDebugKotlin \
    :lib:compileTemplateDebugAidl \
    :lib:assembleTemplateDebugAndroidTest \
    -PselectedAbis=
  ```

  Expected: all tasks succeed without fetching or reading `Foundry-Android`.

- [ ] **Step 5: Commit the restored host module**

  ```sh
  git add platform/android/java/lib platform/android/java/app/config.gradle
  git commit -m "Restore Foundry Android host module"
  ```

### Task 3: Reconnect the engine Gradle graph

**Files:**
- Modify: `platform/android/java/settings.gradle`
- Modify: `platform/android/java/build.gradle`
- Modify: `platform/android/java/app/build.gradle`

- [ ] **Step 1: Include and consume `:lib`**

  Add `include ':lib'`. In `:app`, select `implementation project(":lib")` for the engine Gradle graph while
  preserving the exported app-only fallback to `libs/debug`, `libs/dev`, and `libs/release`.

- [ ] **Step 2: Replace standalone preparation with internal copy dependencies**

  Remove standalone source/fetch/native bundle properties and `prepareFoundryAndroidRuntime`. Make each root AAR
  copy task depend on `:lib:assembleTemplate<BuildType>`, copy the stable internally generated AAR to
  `app/libs/<build-type>` and `bin/`, and retain the staged/inspected `android_source.zip` promotion path.

- [ ] **Step 3: Run focused GREEN contracts**

  Run:

  ```sh
  python3 -m unittest \
    tests.python_build.test_android_runtime_surface \
    tests.python_build.test_android_gradle_runtime_contract
  python3 misc/scripts/test_android_runtime_identifiers.py
  ```

  Expected: all ownership, Gradle, identity, application-ID, and JNI source contracts pass.

- [ ] **Step 4: Compile the app and retained instrumented tests**

  Run:

  ```sh
  platform/android/java/gradlew -p platform/android/java \
    :app:compileStandardDebugJavaWithJavac \
    :app:compileStandardDebugKotlin \
    :app:compileInstrumentedDebugAndroidTestKotlin \
    :app:assembleInstrumentedDebugAndroidTest \
    -PselectedAbis=
  ```

  Expected: Java, Kotlin, AIDL-backed host references, and the instrumented-test APK compile successfully.

- [ ] **Step 5: Commit the internal Gradle graph**

  ```sh
  git add platform/android/java/settings.gradle \
    platform/android/java/build.gradle \
    platform/android/java/app/build.gradle
  git commit -m "Reconnect Android templates to in-tree host"
  ```

### Task 4: Document and inspect the restored runtime

**Files:**
- Modify: `platform/android/ANDROID_RUNTIME.md`
- Generated verification outputs only: `platform/android/java/{app,lib}/build/**`, `bin/android_*.apk`

- [ ] **Step 1: Rewrite runtime ownership/build documentation**

  Document `:lib` as the internal host owner, the debug/dev/release and four-ABI matrix, internal AAR source-template
  packaging, unit/lint/instrumented commands, JNI declaration/export checks, default/custom application-ID
  acceptance, and the API 36 emulator gate. Remove instructions that fetch, pin, publish, or prepare
  `Foundry-Android`.

- [ ] **Step 2: Build all template variants**

  Build or reuse the exact four native ABI payloads, then run:

  ```sh
  platform/android/java/gradlew -p platform/android/java \
    :app:assembleStandardDebug \
    :app:assembleStandardDev \
    :app:assembleStandardRelease
  ```

  Expected: all three template APKs build from internal `:lib`.

- [ ] **Step 3: Inspect artifacts**

  Use `unzip -l`, `jar tf`, `javap`, `nm`, and `apkanalyzer` to verify the three AAR/APK variants contain canonical
  host classes/resources, compiled native declarations, matching `Java_games_cafecito_foundry_*` exports,
  `libfoundry_android.so` plus `libc++_shared.so`, and all four ABI directories. Process manifests once with the
  canonical application ID and once with `-Pexport_package_name=dev.example.foundryacceptance`.

- [ ] **Step 4: Run API 36 device acceptance when available**

  If exactly one API 36 emulator/device is ready, run
  `platform/android/android_device_acceptance.py source-template` for canonical/custom application IDs. If the SDK,
  system image, hardware acceleration, or device is unavailable, capture the exact command and environmental
  diagnostic as an explicit skip; do not weaken compilation or artifact gates.

- [ ] **Step 5: Commit the documentation**

  ```sh
  git add platform/android/ANDROID_RUNTIME.md
  git commit -m "Document the in-tree Android runtime"
  ```

### Task 5: Fresh verification and handoff

**Files:**
- Verify all issue changes; do not touch `.epic-1241-status.md`.

- [ ] **Step 1: Run the complete focused verification**

  Rerun both Python contract modules, the identifier guard, `:lib:testTemplateDebugUnitTest`,
  `:lib:lintTemplateDebug`, Java/Kotlin/AIDL compilation, retained instrumented-test assembly, all three standard
  app variants, source-template inspection, JNI parity checks, and four-ABI artifact inspection from the final HEAD.

- [ ] **Step 2: Check repository hygiene**

  Run `git diff --check`, inspect `git status --short`, confirm `custom.py` still has its exact required contents,
  confirm `Foundry-Android` is unchanged, and confirm the epic ledger is untouched.

- [ ] **Step 3: Self-review and commit any verified corrections**

  Review `git diff origin/develop...HEAD` for scope, donor semantic parity, stale standalone ownership, Maven
  publication, missing resources/tests, and accidental generated files. Fix any in-scope defect through a new
  red/green cycle and commit it separately.

- [ ] **Step 4: Report `READY_FOR_SPEC_REVIEW`**

  Report base/HEAD SHAs, commits and changed files, captured RED/GREEN evidence, every fresh verification result,
  exact environmental skips, and required follow-up work. Do not push, open a PR, update the epic ledger, or run
  final Cursor review in this workstream handoff.

### Quality remediation: preserve existing Android callers

The initial implementation restored the in-tree host, but quality review found that it removed the existing
workflow/SCons Gradle property contract before Workstream 2 had migrated those callers. The following steps amend
the implementation without moving ownership back out of this repository. The stable
`WS2_REMOVE_ANDROID_RUNTIME_COMPAT_BRIDGE` marker identifies every temporary compatibility surface that Workstream
2 must remove after callers have migrated.

#### Task 6: Specify the temporary caller bridge and revision-safe staging

**Files:**
- Modify: `tests/python_build/test_android_gradle_runtime_contract.py`
- Modify: `tests/python_build/test_android_runtime_contract.py`
- Modify: `tests/python_build/test_android_runtime_surface.py`
- Modify: `misc/scripts/test_android_runtime_identifiers.py`
- Add: `tests/python_build/test_android_native_staging.py`

- [x] **Step 1: Add RED caller-compatibility contracts**

  Require `generateFoundryTemplates` to default to all four ABIs and to accept the current
  `foundryAndroidSource`, `foundryAndroidFetch`, `foundryNativeRoot`, `foundryNativeBundle`, and
  `foundryRuntimeScratch` properties. Keep source/fetch as temporary compatibility signals only: neither property
  may build an external runtime AAR. Require native-root and native-bundle inputs to validate the exact
  debug/dev/release × four-ABI matrix, and require native-root mode to preserve the existing downstream
  `foundry-native.zip` bundle format in runtime scratch.

- [x] **Step 2: Add RED staging regressions**

  Require the library source sets to consume only a fresh input/revision/ABI-scoped staging directory. Test a
  full-matrix stage followed by a changed-input subset stage and assert that no stale ABI or mixed-provenance
  payload survives. Direct `:lib` development builds may select an ABI subset; production template generation must
  always select all four.

- [x] **Step 3: Add RED bundle compatibility tests**

  Exercise the existing public `foundry-android-native-bundle` schema used by scripts and workflows. Test
  validation/extraction of its exact 12-cell, four-ABI matrix and rejection of stale revisions, malformed cells,
  and unsafe archive paths. Do not create a second public bundle format.

- [x] **Step 4: Capture RED**

  Run the focused Python contract modules and identifier guard. Record failures for the missing bridge/stager,
  stale persistent `lib/libs` staging, one-ABI default, and missing Workstream 2 removal marker before production
  edits.

#### Task 7: Implement the bridge and safe staging

**Files:**
- Add: `platform/android/android_native_bundle.py`
- Add: `platform/android/android_native_staging.py`
- Modify: `platform/android/java/lib/build.gradle`
- Modify: `platform/android/java/build.gradle`
- Modify: `platform/android/java/app/config.gradle`

- [x] **Step 1: Restore the existing bundle implementation in-tree**

  Preserve the established deterministic bundle schema, manifest, compatibility document, ELF/JNI validation, and
  extraction semantics. Validate the exact current engine revision, JNI contract, build types, ABIs, and library
  names.

- [x] **Step 2: Add a single native-input staging command**

  Support mutually exclusive native-root, native-bundle, and local-SCons modes. Native-root mode validates all 12
  provenance cells and writes `foundry-native.zip` in the requested scratch directory. Native-bundle mode validates
  and extracts the existing format. Local mode stages selected current-revision cells. Always create a fresh
  input/revision/ABI-scoped output containing exactly the selected ABIs. Prepare the complete temporary tree first,
  then replace the previously owned output directory as a whole; do not describe delete-then-`os.replace` as atomic.

- [x] **Step 3: Wire Gradle to staged inputs**

  Make all four ABIs the default. Enforce all four for `generateFoundryTemplates`, retain ABI subsets for direct
  library development tasks, and use a revision/input-scoped staging path as the only `jniLibs` source. Keep the
  legacy properties and their stable removal marker until Workstream 2 migrates every caller.

- [x] **Step 4: Make revision fallback process-start safe**

  Catch both non-zero Git results and process-start failures in `getFoundryEngineRevision()`, returning the
  deterministic zero SHA. Verify Gradle configuration with a `PATH` that contains Java but no Git.

- [x] **Step 5: Run focused GREEN**

  Run all modified Python tests, the identifier guard, native-root and native-bundle integration staging against
  the current 12 cells, the no-Git configuration test, and the library/app Gradle contract tasks.

#### Task 8: Correct provenance documentation and re-verify

**Files:**
- Modify: `platform/android/java/THIRDPARTY.md`
- Modify: `platform/android/ANDROID_RUNTIME.md`

- [x] **Step 1: Correct third-party provenance**

  Preserve unrelated entries while documenting downloader and licensing sources under `lib/src/main/java` and
  `lib/src/main/aidl`. Replace vague modification text with the donor's precise Handler/resource/locale/lint,
  immutable `PendingIntent`, asynchronous preference, and debug-check descriptions.

- [x] **Step 2: Document the compatibility bridge**

  Explain local/native-root/native-bundle modes, the exact public bundle format, four-ABI production requirement,
  fresh staging semantics, and `foundry-native.zip` scratch output. Mark the bridge for Workstream 2 removal with
  `WS2_REMOVE_ANDROID_RUNTIME_COMPAT_BRIDGE`.

- [ ] **Step 3: Final verification and handoff**

  Repeat the complete focused Python, Gradle, artifact, JNI, and API 36 acceptance gates from final HEAD. Check
  worktree/donor/ledger hygiene, commit focused corrections, and report `READY_FOR_QUALITY_REREVIEW` with exact
  evidence. Do not push, open a PR, run Cursor review, or update `.epic-1241-status.md`.

### Quality rereview remediation: execute the real Gradle caller contract

The first quality remediation added helper-level and source-string contracts, but did not durably invoke the
checked-in Gradle wrapper. These steps add scratch-isolated behavioral coverage of the public properties and
observable staging paths.

#### Task 9: Add real-Gradle behavioral coverage

**Files:**
- Modify: `tests/python_build/android_native_bundle_test_support.py`
- Add: `tests/python_build/test_android_gradle_behavioral.py`
- Modify: `platform/android/android_native_staging.py`
- Modify: `docs/superpowers/plans/2026-07-26-restore-in-tree-android-host-runtime.md`

- [x] **Step 1: Build deterministic scratch fixtures**

  Copy the checked-in Gradle project without ignored build outputs into `FOUNDRY_TEST_SCRATCH`, initialize a
  deterministic minimal Git checkout, and generate exact debug/dev/release × four-ABI ELF/provenance matrices with
  the existing native bundle fixture support. All Gradle project caches, generated sources, stages, and packages
  must remain inside that disposable scratch mirror.

- [x] **Step 2: Invoke native-root and native-bundle staging through Gradle**

  Invoke the repository's exact `platform/android/java/gradlew` with `-p` pointing at the scratch project. Run
  `:lib:stageFoundryNativeTemplateDebug` with `foundryNativeRoot`, then run it with the emitted
  `foundry-native.zip` through `foundryNativeBundle`. Assert the existing bundle schema, 12-cell/24-file manifest,
  compatibility revision, scratch outputs, computed revision/input/ABI path, and exact staged ABI/library set.

- [x] **Step 3: Exercise production rejection and no-Git fallback**

  Execute `generateFoundryTemplates` with an exact synthetic native root and incomplete `selectedAbis`, requiring
  the four-ABI production diagnostic. Execute real BuildConfig generation with a `PATH` containing the Gradle
  wrapper utilities but no Git, requiring the deterministic 40-zero revision.

- [x] **Step 4: Exercise changed-input subset staging**

  Stage a full four-ABI matrix, then stage a differently marked valid matrix with an arm64-only selection. Assert
  distinct computed input/ABI scopes and that the second stage contains only the two arm64 libraries with the new
  payload marker.

- [x] **Step 5: Capture mutation-based RED evidence**

  Each probe below temporarily changed one production contract, let the test copy that mutation into its isolated
  fixture, ran the named test, and restored the production file immediately:

  - Native-root property probe:
    `python3 -m unittest tests.python_build.test_android_gradle_behavioral.AndroidGradleBehavioralTests.test_native_root_and_existing_bundle_stage_through_real_gradle`
    failed in 7.612s after renaming the consumed property, because Gradle incorrectly entered local SCons mode:
    `No SConstruct file found` and `compileFoundryNativeLibsTemplateDebugArm32 FAILED`.
  - Production validation probe:
    `python3 -m unittest tests.python_build.test_android_gradle_behavioral.AndroidGradleBehavioralTests.test_production_generation_rejects_incomplete_selected_abis`
    failed in 35.234s after bypassing `validateProductionNativeMatrix` with
    `AssertionError: 0 == 0`, proving the incomplete-ABI build otherwise completed.
  - Missing-Git fallback probe:
    `python3 -m unittest tests.python_build.test_android_gradle_behavioral.AndroidGradleBehavioralTests.test_missing_git_uses_zero_revision_during_real_gradle_configuration`
    failed in 5.997s after changing the fallback to 40 ones: the generated BuildConfig contained that value instead
    of the required 40-zero revision.
  - Staging-scope probe:
    `python3 -m unittest tests.python_build.test_android_gradle_behavioral.AndroidGradleBehavioralTests.test_changed_input_subset_uses_new_scope_without_stale_abis`
    failed in 12.932s after removing input/ABI values from the Gradle stage path: the expected full stage contained
    zero of eight libraries.

- [x] **Step 6: Run GREEN and the complete focused verification**

  Run the four behavioral tests together, all prior Python/workflow contracts, Ruff format/check, mypy, relevant
  direct Gradle caller gates, and final worktree/donor/ledger hygiene. Commit only after every mutation is restored
  and `git diff --check` passes.
