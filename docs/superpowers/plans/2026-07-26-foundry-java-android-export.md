# Foundry-Java Android Export Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a fail-closed, explicit, opt-in Foundry-Java handoff to Foundry's Android exporter while leaving ordinary exports and source templates binding-free.

**Architecture:** The Android exporter validates export-preset inputs and passes one versioned marker plus explicit Maven/local values to the existing Gradle application. The application conditionally resolves and applies the merged Foundry-Java plugin, supplies its normal dependency graph, and delegates descriptor/payload/ABI validation and generated assets to that plugin. Foundry independently guards source-template ownership, explicit-local path safety, and final export observations.

**Tech Stack:** C++17 exporter code, Groovy Gradle scripts, Android Gradle Plugin 8.9.1, Python `unittest` contracts/real Gradle fixtures, ZIP/AAR/APK inspection, doctest command-first verification.

---

### Task 1: Freeze ordinary-export isolation and the handoff surface

**Files:**
- Create: `tests/python_build/test_android_foundry_java_export.py`
- Modify: `tests/python_build/test_android_gradle_runtime_contract.py`

- [ ] **Step 1: Write the failing ordinary-export and source-surface tests**

Add a new test module whose first class reads production files and requires the
approved names while forbidding unconditional resolution:

```python
class FoundryJavaExportSurfaceTests(unittest.TestCase):
    def test_exporter_exposes_only_the_versioned_explicit_handoff(self) -> None:
        exporter = read("platform/android/export/export_plugin.cpp")
        for option in (
            "gradle_build/foundry_java/enabled",
            "gradle_build/foundry_java/gradle_plugin_maven",
            "gradle_build/foundry_java/gradle_plugin_local",
            "gradle_build/foundry_java/maven_repositories",
            "gradle_build/foundry_java/maven_artifacts",
            "gradle_build/foundry_java/local_artifacts",
        ):
            self.assertIn(option, exporter)
        self.assertIn("foundry_java_registry_marker=registry-index-v2", exporter)

    def test_ordinary_gradle_build_has_no_foundry_java_resolution(self) -> None:
        build = read("platform/android/java/app/build.gradle")
        config = read("platform/android/java/app/config.gradle")
        self.assertIn("getFoundryJavaRegistryMarker", config)
        self.assertIn("if (getFoundryJavaEnabled())", build)
        self.assertNotIn('implementation "games.cafecito.foundry:', build)

    def test_source_template_embeds_no_foundry_java_artifact(self) -> None:
        inspector = load_source_template_module()
        self.assertIn("foundry-java", inspector.FORBIDDEN_BINDING_FRAGMENTS)
        self.assertEqual(
            {
                "libs/debug/foundry-debug.aar",
                "libs/dev/foundry-dev.aar",
                "libs/release/foundry-release.aar",
            },
            set(inspector.EXPECTED_AARS),
        )
```

Extend `test_android_gradle_runtime_contract.py` to assert that the existing
ordinary dependency blocks and `res://addons` behavior remain present.

- [ ] **Step 2: Run the tests and verify RED**

Run:

```sh
python3 -m unittest \
  tests.python_build.test_android_foundry_java_export \
  tests.python_build.test_android_gradle_runtime_contract -v
```

Expected: the new tests fail only because the six options, marker parser,
conditional Gradle block, and explicit binding-fragment source-template guard
do not exist.

- [ ] **Step 3: Commit the frozen RED contracts**

```sh
git add tests/python_build/test_android_foundry_java_export.py \
  tests/python_build/test_android_gradle_runtime_contract.py
git commit -m "Test opt-in Foundry-Java export contracts"
```

### Task 2: Implement fail-closed Gradle property parsing and conditional wiring

**Files:**
- Modify: `platform/android/java/app/config.gradle`
- Modify: `platform/android/java/app/build.gradle`
- Modify: `platform/android/java/app/settings.gradle`

- [ ] **Step 1: Add focused RED tests for every handoff parser case**

In `test_android_foundry_java_export.py`, create temporary app-only copies of
`platform/android/java/app` and invoke Gradle `help` with project properties.
Add table-driven failures for:

```python
INVALID_PROPERTIES = (
    ({"foundry_java_registry_marker": "v1"}, "registry-index-v2"),
    ({"foundry_java_registry_marker": "registry-index-v2"}, "foundry_java_gradle_plugin"),
    ({"foundry_java_gradle_plugin_kind": "maven"}, "foundry_java_registry_marker"),
    ({"foundry_java_maven_artifacts": "g:a:1|g:a:1"}, "duplicate"),
    ({"foundry_java_maven_artifacts": "g:a:1.+"}, "exact Maven coordinate"),
    ({"foundry_java_maven_repositories": "ftp://example.test/repo"}, "HTTP(S) or file"),
    ({"foundry_java_local_artifacts": "missing.jar"}, "regular file"),
)
```

Add success parsing for sorted Maven coordinates, sorted repository URLs, and
mixed Maven/local application dependencies. Assert that no marker means no
plugin repository, classpath, application, or dependency line in Gradle's
configuration output.

- [ ] **Step 2: Run the parser cases and verify RED**

Run:

```sh
python3 -m unittest \
  tests.python_build.test_android_foundry_java_export.FoundryJavaGradlePropertyTests -v
```

Expected: all cases fail because the getters and conditional plugin wiring are
absent.

- [ ] **Step 3: Add exact parsing helpers to `config.gradle`**

Implement:

```groovy
final String FOUNDRY_JAVA_REGISTRY_MARKER = "registry-index-v2"

ext.getFoundryJavaRegistryMarker = { ->
    String marker = project.findProperty("foundry_java_registry_marker") ?: ""
    if (!marker.isEmpty() && marker != FOUNDRY_JAVA_REGISTRY_MARKER) {
        throw new GradleException(
            "Invalid foundry_java_registry_marker '$marker'; expected $FOUNDRY_JAVA_REGISTRY_MARKER"
        )
    }
    return marker
}

ext.parseFoundryJavaList = { String propertyName ->
    String raw = project.findProperty(propertyName) ?: ""
    if (raw.isEmpty()) {
        return []
    }
    List<String> values = raw.split("\\|", -1).collect { it.trim() }
    if (values.any { it.isEmpty() } || values.toSet().size() != values.size()) {
        throw new GradleException("Invalid or duplicate $propertyName entry")
    }
    return values.sort()
}
```

Add kind, exact-coordinate, repository URL, regular-file, plugin-source, and
application-artifact validation. These helpers must not read the filesystem or
resolve a configuration when the marker is absent.

- [ ] **Step 4: Add conditional buildscript and application wiring**

In `app/build.gradle`, keep the ordinary `plugins` and dependencies intact.
Before applying Foundry-Java, add its selected plugin artifact to the
buildscript classpath only when the marker exists. Then add:

```groovy
if (getFoundryJavaEnabled()) {
    apply plugin: "games.cafecito.foundry.java"

    dependencies {
        getFoundryJavaMavenArtifacts().each { implementation it }
        getFoundryJavaLocalArtifacts().each { implementation files(it) }
    }

    foundryJava {
        requestedAbis.set(getExportEnabledABIs() as Set)
    }
}
```

The Maven repository list must be added only inside the enabled buildscript and
project repository blocks. A local plugin uses `classpath files(...)`; Maven
and local plugin sources are mutually exclusive.

- [ ] **Step 5: Run focused Gradle/parser tests and reach GREEN**

Run:

```sh
python3 -m unittest \
  tests.python_build.test_android_foundry_java_export.FoundryJavaGradlePropertyTests \
  tests.python_build.test_android_gradle_runtime_contract -v
```

Expected: parser/wiring tests pass, ordinary contracts remain green, and no
network access is attempted without the marker.

- [ ] **Step 6: Commit**

```sh
git add platform/android/java/app/config.gradle \
  platform/android/java/app/build.gradle \
  platform/android/java/app/settings.gradle \
  tests/python_build/test_android_foundry_java_export.py
git commit -m "Wire explicit Foundry-Java Gradle inputs"
```

### Task 3: Add exporter options, validation, and property emission

**Files:**
- Modify: `platform/android/export/export_plugin.h`
- Modify: `platform/android/export/export_plugin.cpp`
- Modify: `tests/python_build/test_android_foundry_java_export.py`

- [ ] **Step 1: Add RED source and command-contract tests**

Require all six preset options, enabled-only visibility/warnings, a fixed
marker, sorted property construction, and no Foundry-Java property outside the
enabled branch. Add exact invalid cases:

```python
for fragment in (
    "dynamic Maven versions are not supported",
    "must use HTTP(S) or file",
    "must be a regular file",
    "must not traverse a symbolic link",
    "select exactly one Maven or local Gradle plugin",
    "requires at least one Maven or local application artifact",
):
    self.assertIn(fragment, exporter)
```

Add a command-first fixture preset with `enabled=true` and Gradle disabled;
assert export fails before starting `gradlew` and names
`gradle_build/foundry_java/enabled`.

- [ ] **Step 2: Run and verify RED**

Run:

```sh
python3 -m unittest \
  tests.python_build.test_android_foundry_java_export.FoundryJavaExporterContractTests -v
```

Expected: failures identify missing options, validation messages, and emitted
properties.

- [ ] **Step 3: Implement one exporter preflight**

Add a private handoff value type and helpers in `export_plugin.h`:

```cpp
struct FoundryJavaExportConfig {
    bool enabled = false;
    String plugin_kind;
    String plugin;
    PackedStringArray repositories;
    PackedStringArray maven_artifacts;
    PackedStringArray local_artifacts;
};

Error _get_foundry_java_export_config(
        const Ref<EditorExportPreset> &p_preset,
        FoundryJavaExportConfig &r_config,
        String &r_error) const;
```

The C++ helper must:

- return immediately and ignore stored subordinate values when disabled;
- require Gradle, exact-one plugin source, and one or more application inputs;
- reject CR, LF, `|`, blanks, duplicates, malformed/dynamic coordinates, and
  invalid repository schemes;
- reject a symlink at every existing user-supplied path component before
  resolving the path;
- require a regular `.jar` plugin and regular `.jar`/`.aar` inputs;
- inspect explicit local archives and reject any root or nested
  `libfoundry_android.so` entry with the input path and entry name;
- canonicalize only accepted local paths; and
- sort every emitted list.

Call this same helper from option warnings/configuration validation and at the
start of `export_project_helper`.

- [ ] **Step 4: Add enabled-only preset fields and pass exact properties**

Add the six approved export options. Use packed string arrays for Maven
repositories/artifacts, a global-file string for the one local plugin, and an
array whose element hint is global `.jar,*.aar` files for local application
artifacts. Normalize that array into the handoff's packed string list. In the
enabled branch append:

```cpp
cmdline.push_back("-Pfoundry_java_registry_marker=registry-index-v2");
cmdline.push_back("-Pfoundry_java_gradle_plugin_kind=" + config.plugin_kind);
cmdline.push_back("-Pfoundry_java_gradle_plugin=" + config.plugin);
cmdline.push_back("-Pfoundry_java_maven_repositories=" +
        String("|").join(config.repositories));
cmdline.push_back("-Pfoundry_java_maven_artifacts=" +
        String("|").join(config.maven_artifacts));
cmdline.push_back("-Pfoundry_java_local_artifacts=" +
        String("|").join(config.local_artifacts));
```

Do not emit empty optional list properties. The existing
`-Pexport_enabled_abis` remains the only ABI source.

- [ ] **Step 5: Run focused tests and format**

Run:

```sh
python3 -m unittest tests.python_build.test_android_foundry_java_export -v
pre-commit run clang-format --files \
  platform/android/export/export_plugin.cpp \
  platform/android/export/export_plugin.h
```

Expected: exporter/static/Gradle contracts pass and `git diff --check` is
clean.

- [ ] **Step 6: Commit**

```sh
git add platform/android/export/export_plugin.cpp \
  platform/android/export/export_plugin.h \
  tests/python_build/test_android_foundry_java_export.py
git commit -m "Add opt-in Foundry-Java export settings"
```

### Task 4: Guard source templates and explicit local artifacts

**Files:**
- Modify: `platform/android/android_source_template.py`
- Modify: `tests/python_build/test_android_foundry_java_export.py`
- Modify: `tests/python_build/test_android_gradle_runtime_contract.py`

- [ ] **Step 1: Add RED archive cases**

Build temporary source ZIPs and local JAR/AARs. Require rejection of:

- any `foundry-java-*.jar` or `foundry-java-*.aar` in the source ZIP;
- generated `FoundryJava.foundryextension` or
  `foundry_java/registry-index-v2.txt` in the source ZIP;
- extra binding AARs outside the exact three host AAR paths;
- local artifacts containing `libfoundry_android.so`;
- a symlink final component; and
- a regular file reached through a symlinked directory.

Require the existing ordinary template fixture to remain accepted.

- [ ] **Step 2: Run and verify RED**

Run:

```sh
python3 -m unittest \
  tests.python_build.test_android_foundry_java_export.FoundryJavaArchiveContractTests \
  tests.python_build.test_android_gradle_runtime_contract.AndroidSourceTemplateTests -v
```

Expected: new binding-fragment and symlink cases fail for the intended missing
guards.

- [ ] **Step 3: Extend the source-template inspector minimally**

Add fixed forbidden fragments and paths:

```python
FORBIDDEN_BINDING_FRAGMENTS = (
    "foundry-java",
    "FoundryJava.foundryextension",
    "foundry_java/registry-index-v2.txt",
)
```

Reject these case-sensitively in regular source-template entries while
preserving the exact existing three-host-AAR contract. Keep local artifact ZIP
entry validation in exporter/Gradle tests; do not create a second descriptor or
binding graph parser in Foundry.

- [ ] **Step 4: Run focused tests and commit**

Run:

```sh
python3 -m unittest \
  tests.python_build.test_android_foundry_java_export \
  tests.python_build.test_android_gradle_runtime_contract -v
```

Then:

```sh
git add platform/android/android_source_template.py \
  tests/python_build/test_android_foundry_java_export.py \
  tests/python_build/test_android_gradle_runtime_contract.py
git commit -m "Keep Android source templates binding-free"
```

### Task 5: Prove real local and staged-Maven Android builds

**Files:**
- Create: `tests/fixtures/android_foundry_java/README.md`
- Create: `tests/fixtures/android_foundry_java/module/src/main/java/example/DemoExtension.java`
- Create: `tests/fixtures/android_foundry_java/module/src/main/resources/META-INF/foundry-java/modules/demo.descriptor`
- Create: `tests/fixtures/android_foundry_java/module/src/main/resources/META-INF/proguard/foundry-java-demo.pro`
- Modify: `tests/python_build/test_android_foundry_java_export.py`
- Modify: `tests/python_build/test_android_gradle_behavioral.py`

- [ ] **Step 1: Build the exact merged dependency artifacts**

The test helper resolves `FOUNDRY_JAVA_REPO`, defaulting to the sibling
`/Users/christian/CafecitoGames/Foundry-Java`, and requires
`git rev-parse HEAD` to equal
`7eb98b37845b42ff67f3da1427bd78ebef19668f`. With Java 17, run:

```sh
./gradlew --no-daemon \
  :foundry-java-gradle-plugin:jar \
  :foundry-java-runtime:jar \
  :foundry-java-android:assembleRelease
```

The helper then compiles the checked-in demo registry against the exact runtime
JAR and creates a deterministic descriptor-bearing module JAR.

- [ ] **Step 2: Add a real fully-local RED build**

Pass the exact plugin JAR, Android AAR, runtime JAR, and generated module JAR.
Copy the app source fixture, provide one ABI, run `assembleStandardDebug`, and
assert:

```python
self.assertEqual(
    ["assets/FoundryJava.foundryextension"],
    apk_entries_ending(apk, "FoundryJava.foundryextension"),
)
self.assertIn("assets/foundry_java/registry-index-v2.txt", apk_entries(apk))
self.assertEqual([f"lib/{abi}/libfoundry_java.so"], bridge_entries(apk))
```

Also assert the ordinary Foundry host `libfoundry_android.so` remains present
independently.

- [ ] **Step 3: Run and verify RED**

Run the local integration case with:

```sh
FOUNDRY_JAVA_REPO=/Users/christian/CafecitoGames/Foundry-Java \
python3 -m unittest \
  tests.python_build.test_android_foundry_java_export.FoundryJavaAndroidIntegrationTests.test_exact_local_inputs -v
```

Expected: Gradle fails because conditional plugin/dependency wiring is not yet
complete, or the final assertions expose the first real integration defect.

- [ ] **Step 4: Fix only the real integration defects**

Adjust the conditional buildscript, dependency declarations, or variant task
ordering without adding reflection, manifest/class scanning, inferred
coordinates, or embedded template artifacts.

- [ ] **Step 5: Add the staged Maven case**

Create a temporary Maven2 layout from the exact built artifacts, deterministic
POMs, and the Gradle plugin marker coordinate
`games.cafecito.foundry.java:games.cafecito.foundry.java.gradle.plugin`.
Run debug with the default application ID using only exact coordinates and the
explicit repository's `file:` URI. Require the same index, configuration,
bootstrap, and selected-ABI outputs.

- [ ] **Step 6: Cover plugin-owned fail-closed matrix**

Drive the merged plugin with generated fixtures and assert stable failures for
zero descriptors, format 1/malformed paths/headers/names, duplicate
module/registry, each mixed provenance field, missing/duplicate/split
bridge/configuration, forbidden `libfoundry_android.so`, and empty/unsupported/
missing requested ABI. Assert artifact and descriptor paths occur in each
diagnostic; do not reproduce the plugin parser in Foundry.

- [ ] **Step 7: Run local and Maven cases twice**

Run:

```sh
python3 -m unittest \
  tests.python_build.test_android_foundry_java_export.FoundryJavaAndroidIntegrationTests -v
```

Expected: local and Maven builds pass; the second identical build says
`Reusing configuration cache`; generated index/config bytes are unchanged.

- [ ] **Step 8: Commit**

```sh
git add tests/fixtures/android_foundry_java \
  tests/python_build/test_android_foundry_java_export.py \
  tests/python_build/test_android_gradle_behavioral.py \
  platform/android/java/app
git commit -m "Verify Foundry-Java Android export integration"
```

### Task 6: Verify ABI selection, release minification, and final artifacts

**Files:**
- Modify: `tests/python_build/test_android_foundry_java_export.py`
- Modify: `tests/python_build/test_android_gradle_behavioral.py`
- Modify: `platform/android/android_device_acceptance.py`

- [ ] **Step 1: Add parameterized RED artifact assertions**

For each Android ABI, build debug with that one requested ABI and require
exactly one matching `libfoundry_java.so`, one index, and one fixed
configuration while preserving unrelated application/host native libraries.
Reject empty, unsupported, or bridge-missing selections before outputs.

- [ ] **Step 2: Add the custom-ID minified release RED**

Run release with a custom application ID and minification enabled. Inspect the
APK for the direct bootstrap/provider/trampoline retention, exactly one
configuration/index, the selected bridge ABI, no unrequested bridge ABI, and no
broad Foundry-Java keep rule. Repeat and compare generated bytes.

- [ ] **Step 3: Run and verify RED**

Run:

```sh
python3 -m unittest \
  tests.python_build.test_android_foundry_java_export.FoundryJavaAbiAndReleaseTests -v
```

Expected: failures identify only missing final-output assertions or variant
wiring defects.

- [ ] **Step 4: Implement the minimum final-output inspection**

Reuse existing ZIP/APK helpers and extend the existing device/export acceptance
inspector with enabled-only checks for the two assets and requested bridge
entries. Do not reject Foundry's legitimate host `libfoundry_android.so` in the
final app; input ownership is validated before assembly.

- [ ] **Step 5: Rerun and commit**

Run the same class twice and require GREEN plus configuration-cache reuse.
Commit:

```sh
git add tests/python_build/test_android_foundry_java_export.py \
  tests/python_build/test_android_gradle_behavioral.py \
  platform/android/android_device_acceptance.py
git commit -m "Prove Foundry-Java ABI and release exports"
```

### Task 7: Document and register the contract gates

**Files:**
- Modify: `platform/android/ANDROID_RUNTIME.md`
- Modify: `platform/android/doc_classes/EditorExportPlatformAndroid.xml`
- Modify: `.pre-commit-config.yaml`
- Modify: `tests/python_build/test_android_foundry_java_export.py`

- [ ] **Step 1: Add RED documentation/gate assertions**

Require the runtime guide and class reference to document every option, the
fixed marker, exact plugin ID, Maven/local examples, zero-descriptor behavior,
ABI mapping, generated asset paths, ordinary-export isolation, and
Foundry-Android's read-only/non-dependency status. Require pre-commit to run the
new module when any exporter, app Gradle, source-template, fixture, doc, or test
surface changes.

- [ ] **Step 2: Run and verify RED**

Run:

```sh
python3 -m unittest \
  tests.python_build.test_android_foundry_java_export.FoundryJavaDocumentationTests -v
```

Expected: missing documentation and pre-commit registration failures.

- [ ] **Step 3: Write docs and hook registration**

Add a concise "Optional Foundry-Java extensions" section to
`ANDROID_RUNTIME.md`, six export-option members to
`EditorExportPlatformAndroid.xml`, and a local pre-commit hook:

```yaml
- id: foundry-java-android-export
  name: Foundry-Java Android export contracts
  language: python
  entry: python -m unittest
  args:
    - tests.python_build.test_android_foundry_java_export
  pass_filenames: false
```

Scope the hook to the exact files listed in this plan.

- [ ] **Step 4: Validate and commit**

Run:

```sh
python3 -m unittest tests.python_build.test_android_foundry_java_export -v
python3 misc/scripts/validate_xml.py platform/android/doc_classes/EditorExportPlatformAndroid.xml
pre-commit run foundry-java-android-export --all-files
```

Then:

```sh
git add platform/android/ANDROID_RUNTIME.md \
  platform/android/doc_classes/EditorExportPlatformAndroid.xml \
  .pre-commit-config.yaml tests/python_build/test_android_foundry_java_export.py
git commit -m "Document optional Foundry-Java exports"
```

### Task 8: Broad verification, exact-head reviews, PR, merge, and cleanup

**Files:**
- Verify all changed files
- Update externally: `.epic-1241-status.md`

- [ ] **Step 1: Run Python and Gradle focused gates**

Run:

```sh
python3 -m unittest \
  tests.python_build.test_android_foundry_java_export \
  tests.python_build.test_android_gradle_runtime_contract \
  tests.python_build.test_android_device_acceptance -v
python3 -m unittest tests.python_build.test_android_gradle_behavioral -v
```

Expected: all tests pass, including real ordinary/local/Maven/debug/release/
custom-ID/four-ABI/cache cases.

- [ ] **Step 2: Run format, XML, source-template, and Gradle checks**

Run:

```sh
pre-commit run --files \
  platform/android/export/export_plugin.cpp \
  platform/android/export/export_plugin.h \
  platform/android/java/app/build.gradle \
  platform/android/java/app/config.gradle \
  platform/android/java/app/settings.gradle \
  platform/android/android_source_template.py \
  platform/android/ANDROID_RUNTIME.md \
  platform/android/doc_classes/EditorExportPlatformAndroid.xml \
  tests/python_build/test_android_foundry_java_export.py
platform/android/java/gradlew -p platform/android/java --no-daemon \
  :app:assembleStandardDebug -PselectedAbis=
```

Expected: hooks and Gradle pass without binding inputs in the ordinary build.

- [ ] **Step 3: Run strict Foundry build and command-first suites**

On this macOS workspace run the repository's CI-style macOS SCons build using
the shared cache, then:

```sh
./bin/foundry.* --headless test run \
  --case "*Android*Foundry*Java*" --force-colors
./bin/foundry.* --headless test run \
  --progress-format=jsonl \
  --progress-file /tmp/foundry-1245-test-progress.jsonl \
  --force-colors
```

Expected: strict build succeeds and the final doctest summary is
`[doctest] Status: SUCCESS!`.

- [ ] **Step 4: Run final device/source-template acceptance when available**

Use the checked-in command-first exporter and API 36 device tooling for default
debug and custom-ID minified release. Capture exact index/config/native/
bootstrap diagnostics. If no API 36 device is available, do not claim this
gate; report the environment constraint to the orchestrator before PR.

- [ ] **Step 5: Freeze and independently review exact HEAD**

Ensure a clean worktree, fetch `origin/develop`, record both SHAs, and dispatch
an independent read-only review covering the approved design and 17-case
matrix. Triage every finding with receiving-review/systematic-debugging and add
a failing regression test before any fix.

- [ ] **Step 6: Run Cursor review to exact clean**

From the worktree run:

```sh
python3 ~/.claude/scripts/codex_review/await_review.py start-wait \
  --cwd /Users/christian/CafecitoGames/Foundry/.worktrees/issue-1245 \
  --scope branch --base origin/develop --deadline 540
```

Repeat only after a changed, fully verified HEAD until the latest valid verdict
is exactly `RESULT: clean`.

- [ ] **Step 7: Publish only the reviewed head**

Push `issue-1245`, open a ready PR targeting `develop`, include complete
verification/review evidence, and end the body with `Closes #1245`. Keep
auto-merge off until all required checks and review states converge.

- [ ] **Step 8: Merge and clean up**

After checks are green and reviews remain clean, enable squash auto-merge.
Wait for the merged SHA, confirm #1245 is Closed/Completed and Experiment Done,
remove the worktree and local/remote `issue-1245` branch, and record every
review/PR/check/merge/cleanup transition in `.epic-1241-status.md`.
