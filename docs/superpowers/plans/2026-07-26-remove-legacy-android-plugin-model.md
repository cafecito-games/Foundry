# Remove Legacy Android Plugin Model Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove Foundry's obsolete manifest/reflection Android plugin protocol while preserving the ordinary Android host lifecycle, JavaClassWrapper, file/permission handling, and FoundryExtension loading.

**Architecture:** Delete the Java plugin registry/API and its JNI singleton adapter as one closed subsystem. Keep host-owned callbacks directly on `FoundryHost`, keep platform FoundryExtension loading available through the existing Java/native wrapper boundary, and replace the plugin-based instrumented test fixture with an explicit JavaClassWrapper test bridge that is compiled only into the instrumented app flavor.

**Tech Stack:** C++17, JNI, Java 17, Kotlin, Gradle/Groovy, Foundry Script, Python `unittest`, SCons, Android API 36 instrumentation.

---

## Inventory and behavior classification

Plugin-only code to remove:

- `FoundryPlugin`, `FoundryPluginRegistry`, `UsedByFoundry`, `SignalInfo`, and `AndroidRuntimePlugin`.
- `foundry_plugin_jni.{h,cpp}`, `JNISingleton`, their SCons/ClassDB registration, and the termination cleanup call.
- Manifest-v1 metadata discovery and the instrumented manifest entries that exercise it.
- Host/plugin injection (`FoundryHost.getHostPlugins`, the third `Foundry.initEngine` argument) and every plugin lifecycle/render/view/feature callback loop.
- `plugins_maven_repos`, `plugins_remote_binaries`, and `plugins_local_binaries` parsing and dependency wiring.
- Plugin protocol JVM/instrumented tests and the `FoundryAppInstrumentedTestPlugin` fixture.

Host-required code to preserve:

- `FoundryHost` setup/main-loop/force-quit/restart/new-instance callbacks, feature support, activity access, and host-thread dispatch.
- `Foundry.onActivityResult` file-picker delivery and `onRequestPermissionsResult` native permission delivery.
- Render start/surface/resize/frame delivery to the native renderer.
- `JavaClassWrapper` and `Callable` JNI used by ordinary Foundry Script Android interop.
- `FoundryJavaWrapper.get_foundry_extension_list_config_file()` and `OS_Android::load_platform_foundry_extensions()`. The Java host returns an empty platform-extension list after plugin path discovery is removed; normal project FoundryExtension loading remains native, and Workstream 11 can add the fixed Foundry-Java marker without reviving manifest scanning.
- App-local `res://addons` JAR/AAR discovery, which is explicit project content rather than legacy plugin Maven metadata.

### Task 1: Freeze the removal contract

**Files:**

- Modify: `tests/python_build/test_android_runtime_surface.py`
- Modify: `tests/python_build/test_android_gradle_runtime_contract.py`

- [x] **Step 1: Add exact removed-path assertions**

List every legacy Java/JNI/test/fixture path, including `platform/android/api/jni_singleton.h`, and fail if any remains.

- [x] **Step 2: Add forbidden production tokens**

Reject `FoundryPlugin`, `FoundryPluginRegistry`, `UsedByFoundry`, `SignalInfo`,
`AndroidRuntimePlugin`, plugin-v1 metadata, plugin JNI exports, reflective
registration, the three plugin dependency properties/helpers, and broad
plugin keep rules from active Android production/build sources.

- [x] **Step 3: Run the focused contracts and record RED**

Run:

```sh
python3 tests/python_build/test_android_runtime_surface.py
python3 -m unittest tests.python_build.test_android_gradle_runtime_contract
```

Expected: failures identify the still-present legacy paths and tokens, not a
syntax/import error.

### Task 2: Remove native plugin registration

**Files:**

- Delete: `platform/android/plugin/foundry_plugin_jni.h`
- Delete: `platform/android/plugin/foundry_plugin_jni.cpp`
- Delete: `platform/android/api/jni_singleton.h`
- Modify: `platform/android/SCsub`
- Modify: `platform/android/api/api.cpp`
- Modify: `platform/android/java_foundry_lib_jni.cpp`
- Modify: `tests/python_build/android_native_test_support.py`

- [x] **Step 1: Delete JNI singleton implementation and registration**

Remove the plugin native source, include, SCons source entry, ClassDB
registration, and `_terminate()` cleanup call. Keep `JavaClassWrapper` itself.

- [x] **Step 2: Remove plugin JNI symbols from native matrix fixtures**

Delete only the four `FoundryPlugin_native*` expected symbols.

- [x] **Step 3: Run the absence contracts**

Expected: native-path/JNI failures are gone while Java/Gradle/fixture failures
remain.

### Task 3: Remove Java plugin lifecycle and metadata discovery

**Files:**

- Delete: `platform/android/java/lib/src/main/java/games/cafecito/foundry/plugin/FoundryPlugin.java`
- Delete: `platform/android/java/lib/src/main/java/games/cafecito/foundry/plugin/FoundryPluginRegistry.java`
- Delete: `platform/android/java/lib/src/main/java/games/cafecito/foundry/plugin/UsedByFoundry.java`
- Delete: `platform/android/java/lib/src/main/java/games/cafecito/foundry/plugin/SignalInfo.java`
- Delete: `platform/android/java/lib/src/main/java/games/cafecito/foundry/plugin/AndroidRuntimePlugin.kt`
- Modify: `platform/android/java/lib/src/main/java/games/cafecito/foundry/Foundry.kt`
- Modify: `platform/android/java/lib/src/main/java/games/cafecito/foundry/FoundryHost.java`
- Modify: `platform/android/java/lib/src/main/java/games/cafecito/foundry/FoundryFragment.java`
- Modify: `platform/android/java/lib/src/main/java/games/cafecito/foundry/service/FoundryService.kt`
- Modify: `platform/android/java/lib/src/main/java/games/cafecito/foundry/gl/FoundryRenderer.java`
- Modify: `platform/android/java/lib/src/main/java/games/cafecito/foundry/vulkan/VkRenderer.kt`

- [x] **Step 1: Remove plugin injection and initialization**

Make `Foundry.initEngine` accept only the host and command line. Update fragment
and service callers; remove `FoundryHost.getHostPlugins`.

- [x] **Step 2: Remove plugin-only lifecycle forwarding**

Delete registry loops for views, render callbacks, pause/resume/destroy,
activity/permission callbacks, setup/start/terminate, back, and feature tags.
Retain the adjacent host/native behavior.

- [x] **Step 3: Preserve the FoundryExtension wrapper contract**

Keep `getFoundryExtensionConfigFiles()` and return an empty array without
plugin discovery so native platform-extension loading remains a stable host
boundary.

- [x] **Step 4: Compile the runtime source**

Run:

```sh
cd platform/android/java
./gradlew --no-daemon :lib:compileTemplateDebugKotlin :lib:compileTemplateDebugJavaWithJavac
```

### Task 4: Remove plugin dependency plumbing

**Files:**

- Modify: `platform/android/java/app/config.gradle`
- Modify: `platform/android/java/app/build.gradle`

- [x] **Step 1: Delete legacy project-property parsers**

Remove the three `getFoundryPlugins*` helpers.

- [x] **Step 2: Delete custom Maven and plugin binary wiring**

Remove plugin repositories and remote/local plugin dependency blocks. Preserve
standard repositories, in-tree `:lib`, source-template AAR fallbacks, explicit
`res://addons` dependencies, and .NET dependencies.

- [x] **Step 3: Run focused Python contracts**

Expected: Gradle plugin-property failures are gone.

### Task 5: Replace the plugin instrumented fixture

**Files:**

- Delete: `platform/android/java/lib/src/test/java/games/cafecito/foundry/plugin/FoundryPluginRegistryTest.java`
- Delete: `platform/android/java/lib/src/androidTest/java/games/cafecito/foundry/plugin/FoundryPluginProtocolInstrumentedTest.java`
- Delete: `platform/android/java/app/src/instrumented/java/games/cafecito/foundry/game/test/FoundryAppInstrumentedTestPlugin.kt`
- Create: `platform/android/java/app/src/instrumented/java/games/cafecito/foundry/game/test/FoundryAppInstrumentedTestBridge.java`
- Modify: `platform/android/java/app/src/instrumented/AndroidManifest.xml`
- Modify: `platform/android/java/app/src/instrumented/assets/main.fs`
- Modify: `platform/android/java/app/src/androidTestInstrumented/java/games/cafecito/foundry/game/FoundryAppTest.kt`

- [x] **Step 1: Add an explicit instrumented-only bridge**

Use public static methods, atomics, result storage, and countdown latches.
Foundry Script reaches the explicitly named class through `JavaClassWrapper`;
there is no manifest discovery, plugin registry, annotation, or native
singleton registration.

- [x] **Step 2: Retarget the Foundry Script runner**

Poll bridge requests, run the existing JavaClassWrapper/file-access suites,
return structured pass/fail results, update `quit_on_go_back`, and report
startup/termination.

- [x] **Step 3: Retarget instrumentation**

Wait on the bridge, request tests through it, assert the manifest contains no
legacy plugin metadata, and preserve launcher/back-press coverage.

- [x] **Step 4: Compile unit and instrumented variants**

Run:

```sh
cd platform/android/java
./gradlew --no-daemon :lib:testTemplateDebugUnitTest :lib:lintTemplateDebug :lib:assembleTemplateDebugAndroidTest :app:assembleInstrumentedDebug :app:assembleInstrumentedDebugAndroidTest
```

### Task 6: Update documentation and prove absence

**Files:**

- Modify: `platform/android/ANDROID_RUNTIME.md`
- Modify: `tests/python_build/test_android_runtime_surface.py`
- Modify: `tests/python_build/test_android_gradle_runtime_contract.py`

- [x] **Step 1: Replace the temporary legacy-protocol statement**

Document that host exports no longer scan manifest plugin metadata or accept
plugin Maven properties, and that extensions use FoundryExtension.

- [x] **Step 2: Run both absence contracts GREEN**

Run the two Task 1 commands and require zero failures.

- [x] **Step 3: Run production absence search**

Search active Android source/build/test/docs for the removed classes, JNI
symbols, manifest prefix, reflection registration, dependency properties, and
broad keep rules. Historical `docs/superpowers` records are not production
surfaces and remain unchanged.

### Task 7: Run the complete WS3 gate

**Files:**

- Verify all modified files.

- [x] **Step 1: Run Android Gradle/template gates**

Run the restored runtime unit/lint/instrumented compilation, 12 native cells,
template AAR/APK/source generation, and default/custom API 36 acceptance using
the repository's agent-friendly scripts and current SDK.

- [x] **Step 2: Run the strict Foundry build**

Run `python3 scripts/agent_build.py` without `--dev-build`.

- [x] **Step 3: Run the full command-first suite**

Run the built editor with `--headless test run`, a JSONL progress file, and
the platform-required display when available. Trust the final doctest status,
while reporting any environment-gated Android emulator skip exactly.

- [x] **Step 4: Review and commit**

Inspect `git diff --check`, the full branch diff against `origin/develop`, and
fresh verification output. Commit focused changes and stop at the clean
pre-PR checkpoint for root-run spec, quality, and Cursor reviews.
