# Android Runtime Identity Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Android runtime and export-template Godot package/protocol identifiers with one stable Foundry-owned identity contract.

**Architecture:** Keep the existing `games.cafecito.foundry` runtime/JNI namespace intact and use
`games.cafecito.foundry.game` for fixed export-template implementation classes. Treat the exported application's
configurable application ID as separate from that implementation package, and collapse manifest plugin discovery to
the single `games.cafecito.foundry.plugin.v1.` prefix.

**Tech Stack:** Python source-contract tests, Gradle/AGP, Java 17, Kotlin, Android manifests, C++ Android exporter

---

### Task 1: Add the Android identity contract guard

**Files:**
- Create: `misc/scripts/test_android_runtime_identifiers.py`
- Modify: `.pre-commit-config.yaml`

- [ ] **Step 1: Write a failing source-contract test**

  Add checks that the app namespace/default package, fixed launcher lookup, manifest component names, plugin metadata,
  library metadata, wake-lock source and vendored patch all use their exact Foundry identifiers. Scan app production
  and test sources for `com.godot`/`org.godotengine` package declarations, and scan native Android JNI exports to
  preserve `Java_games_cafecito_foundry_*` while rejecting `Java_com_godot_*` and `Java_org_godotengine_*`.

- [ ] **Step 2: Wire the guard into pre-commit**

  Add a local hook whose file filter covers the guard and the Android runtime/export source surface, with
  `pass_filenames: false`.

- [ ] **Step 3: Run the guard and verify red**

  Run: `python3 misc/scripts/test_android_runtime_identifiers.py`

  Expected: failure listing the current `com.godot.game`, dual upstream plugin prefixes, upstream library key,
  exporter launcher, and wake-lock label.

### Task 2: Add a focused plugin protocol unit test

**Files:**
- Create: `platform/android/java/lib/src/test/java/games/cafecito/foundry/plugin/FoundryPluginRegistryTest.java`
- Modify: `platform/android/java/lib/src/main/java/games/cafecito/foundry/plugin/FoundryPluginRegistry.java`

- [ ] **Step 1: Write the failing Java test**

  Assert that metadata parsing returns `Example` for `games.cafecito.foundry.plugin.v1.Example`, and returns null for
  empty names plus both `org.godotengine.plugin.v1.Example` and `org.godotengine.plugin.v2.Example`.

- [ ] **Step 2: Run the focused test and verify red**

  Run: `platform/android/java/gradlew -p platform/android/java :lib:testTemplateDebugUnitTest`

  Expected: Java compilation fails because the single-protocol metadata parser does not exist yet.

- [ ] **Step 3: Implement the one-prefix parser**

  Replace the two upstream constants and branches with one `FOUNDRY_PLUGIN_NAME_PREFIX` and a package-visible pure
  parser used by manifest discovery.

- [ ] **Step 4: Rerun the focused test**

  Run: `platform/android/java/gradlew -p platform/android/java :lib:testTemplateDebugUnitTest`

  Expected: all unit tests pass.

### Task 3: Move the export-template implementation package

**Files:**
- Move: `platform/android/java/app/src/main/java/com/godot/game/FoundryApp.java`
  to `platform/android/java/app/src/main/java/games/cafecito/foundry/game/FoundryApp.java`
- Move: `platform/android/java/app/src/androidTestInstrumented/java/com/godot/game/FoundryAppTest.kt`
  to `platform/android/java/app/src/androidTestInstrumented/java/games/cafecito/foundry/game/FoundryAppTest.kt`
- Move instrumented plugin and JavaClassWrapper fixtures from
  `platform/android/java/app/src/instrumented/java/com/godot/game/test/` to
  `platform/android/java/app/src/instrumented/java/games/cafecito/foundry/game/test/`
- Modify: `platform/android/java/app/build.gradle`
- Modify: `platform/android/java/app/config.gradle`
- Modify: `platform/android/java/app/src/main/AndroidManifest.xml`
- Modify: `platform/android/java/app/src/instrumented/AndroidManifest.xml`
- Modify: `platform/android/java/app/src/instrumented/assets/test/javaclasswrapper/java_class_wrapper_tests.fs`
- Modify: `platform/android/export/export_plugin.cpp`

- [ ] **Step 1: Change packages and reflected test strings**

  Use `games.cafecito.foundry.game` for the app and `games.cafecito.foundry.game.test` for fixtures. Update the
  JavaClassWrapper class strings and expected object renderings in lockstep.

- [ ] **Step 2: Separate implementation package from application ID**

  Set the AGP namespace and default application ID to `games.cafecito.foundry.game`, fully qualify the manifest
  activity and alias names, and make the native exporter target
  `games.cafecito.foundry.game.FoundryAppLauncher` after the configurable package/component slash.

- [ ] **Step 3: Update the instrumented plugin fixture**

  Use `games.cafecito.foundry.plugin.v1.FoundryAppInstrumentedTestPlugin` and its moved initializer class.

### Task 4: Rename the library metadata and vendored wake-lock label

**Files:**
- Modify: `platform/android/java/lib/src/main/AndroidManifest.xml`
- Modify: `platform/android/java/lib/src/main/java/games/cafecito/foundry/plugin/FoundryPlugin.java`
- Modify: `platform/android/java/lib/src/main/java/com/google/android/vending/expansion/downloader/impl/DownloadThread.java`
- Modify: `platform/android/java/lib/patches/com.google.android.vending.expansion.downloader.patch`

- [ ] **Step 1: Rename the compiled identifiers**

  Use `games.cafecito.foundry.library.version` and `games.cafecito.foundry:wakelock`, preserving all inherited license
  headers and the patch's `FOUNDRY` provenance markers.

- [ ] **Step 2: Update plugin documentation**

  Document only `games.cafecito.foundry.plugin.v1.[PluginName]`; do not describe or retain compatibility aliases.

- [ ] **Step 3: Run the source-contract guard**

  Run: `python3 misc/scripts/test_android_runtime_identifiers.py`

  Expected: `Foundry Android runtime identifier tests passed`.

### Task 5: Verify Android compilation and generated manifests

**Files:**
- Verify generated outputs only; do not commit `build/` products.

- [ ] **Step 1: Run JVM tests and compile app test sources**

  Run the template library unit test task and the app Java/Kotlin compilation tasks available in the Gradle task
  graph.

- [ ] **Step 2: Generate default and custom-application-ID manifests**

  Run the app manifest processing task once with the default package and once with
  `-Pexport_package_name=dev.example.custom`, then inspect merged manifests for the fixed Foundry implementation
  classes and requested application ID.

- [ ] **Step 3: Assemble runtime/template artifacts**

  Assemble the template AAR and export-template APK variants supported by the local Android SDK/toolchain. Record any
  unavailable device-only instrumented run separately; do not represent compilation as a device smoke test.

- [ ] **Step 4: Audit bytecode/artifacts and JNI source**

  Search generated JAR/AAR/APK entries and strings for stale `com.godot`/`org.godotengine` runtime identities, and
  rerun the source JNI export audit.

### Task 6: Verify the engine integration and publish

**Files:**
- All intended files above

- [ ] **Step 1: Build with strict macOS settings**

  Run: `scons platform=macos target=editor dev_mode=yes tests=yes`

  Expected: successful editor/test build with warnings treated as errors.

- [ ] **Step 2: Run relevant command-first tests**

  Run the focused Android/export doctest filters present in the built test registry, then the full command-first
  headless test suite when practical.

- [ ] **Step 3: Check formatting and repository state**

  Run `git diff --check`, the relevant pre-commit hooks, the source/artifact audits, and confirm only intended files
  are modified.

- [ ] **Step 4: Commit and converge Cursor review**

  Commit the verified diff, run read-only Cursor against `origin/develop`, triage every finding technically, fix and
  reverify in-scope defects, and repeat with a fresh committed HEAD until the exact verdict is `RESULT: clean`.

- [ ] **Step 5: Open and merge the PR**

  Push `issue-1222`, open a PR targeting `develop` whose body ends with `Closes #1222`, enable squash auto-merge,
  monitor checks to merge, set the Experiment item to Done, and remove the worktree and local/remote issue branch.
