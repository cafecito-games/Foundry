# Android Runtime End-to-End Acceptance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a reproducible emulator-backed acceptance gate for the standalone Android runtime and prove the exact Foundry exporter path locally for canonical and custom application IDs.

**Architecture:** A standard-library Python driver consumes the already-validated `android_source.zip`, builds and instruments isolated canonical/custom-ID scenarios, then performs explicit APK install/start/process/logcat checks. A new CI job runs the driver only after the authoritative 3×4 native matrix and artifact assembly pass; existing JNI, ELF, provenance, and identity inspectors remain authoritative for their layers.

**Tech Stack:** Python 3 standard library and `unittest`, Gradle 8.11.1/AGP 8.9.1, Android SDK/API 36, adb/emulator, Kotlin Android instrumentation, GitHub Actions, SCons, Foundry command-first CLI.

---

## File structure

- Create `platform/android/android_device_acceptance.py`
  - safe source-template extraction;
  - adb device/boot/process handling;
  - isolated Gradle scenario builds;
  - JUnit, APK application-ID, install/start, and logcat proof;
  - canonical JSON evidence;
  - APK-only verification for local CLI exports.
- Create `tests/python_build/test_android_device_acceptance.py`
  - unit and orchestration tests using deterministic fake commands/artifacts.
- Modify `platform/android/java/app/src/androidTestInstrumented/java/games/cafecito/foundry/game/FoundryAppTest.kt`
  - one focused runtime-boot/plugin-protocol smoke.
- Modify `.github/workflows/android_builds.yml`
  - emulator-backed job downstream of `assemble-android`.
- Modify `.github/scripts/test_android_runtime_workflows.py`
  - static workflow acceptance contract.
- Modify `.pre-commit-config.yaml`
  - run driver/workflow contracts when the acceptance surface changes.
- Modify `platform/android/ANDROID_RUNTIME.md`
  - device commands, evidence map, version-alignment/release checklist.

### Task 1: Define the acceptance-driver contract

**Files:**
- Create: `tests/python_build/test_android_device_acceptance.py`
- Create: `platform/android/android_device_acceptance.py`

- [ ] **Step 1: Write failing tests for application IDs, devices, and runtime errors**

Add tests that load the driver module and require these contracts:

```python
class AndroidDeviceAcceptanceTests(unittest.TestCase):
    def test_application_ids_require_lowercase_reverse_dns(self) -> None:
        self.assertEqual(
            "dev.example.foundryacceptance",
            tool.validate_application_id("dev.example.foundryacceptance"),
        )
        for invalid in ("", "Foundry", "dev..foundry", "9dev.example", "dev.Example"):
            with self.subTest(invalid=invalid):
                with self.assertRaises(tool.AcceptanceError):
                    tool.validate_application_id(invalid)

    def test_select_device_requires_one_ready_device_or_matching_serial(self) -> None:
        output = (
            "List of devices attached\n"
            "emulator-5554 device product:sdk model:Virtual_Device\n"
            "offline-5556 offline\n"
        )
        self.assertEqual("emulator-5554", tool.select_device(output, None))
        with self.assertRaises(tool.AcceptanceError):
            tool.select_device("List of devices attached\n", None)
        with self.assertRaises(tool.AcceptanceError):
            tool.select_device(
                output + "emulator-5558 device product:sdk model:Other\n",
                None,
            )

    def test_runtime_log_rejects_linkage_class_and_fatal_failures(self) -> None:
        self.assertEqual([], tool.runtime_log_failures("Foundry main loop started"))
        for signature in (
            "java.lang.UnsatisfiedLinkError",
            "java.lang.NoClassDefFoundError",
            "java.lang.ClassNotFoundException",
            "FATAL EXCEPTION: main",
            'couldn\'t find "libfoundry_android.so"',
        ):
            with self.subTest(signature=signature):
                self.assertTrue(tool.runtime_log_failures(signature))
```

- [ ] **Step 2: Run the tests and verify RED**

Run:

```sh
python3 -m unittest tests.python_build.test_android_device_acceptance -v
```

Expected: import/missing-file failure for
`platform/android/android_device_acceptance.py`.

- [ ] **Step 3: Implement the minimal pure contracts**

Create the driver with:

```python
class AcceptanceError(Exception):
    """Android device acceptance failed."""


APPLICATION_ID_PATTERN = re.compile(
    r"^[a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*)+$"
)
RUNTIME_FAILURE_PATTERNS = (
    "UnsatisfiedLinkError",
    "NoClassDefFoundError",
    "ClassNotFoundException",
    "FATAL EXCEPTION",
    'couldn\'t find "libfoundry_android.so"',
)


def validate_application_id(value: str) -> str:
    if APPLICATION_ID_PATTERN.fullmatch(value) is None:
        raise AcceptanceError(f"invalid Android application ID: {value!r}")
    return value


def select_device(output: str, requested_serial: str | None) -> str:
    ready = [
        line.split()[0]
        for line in output.splitlines()[1:]
        if len(line.split()) >= 2 and line.split()[1] == "device"
    ]
    if requested_serial is not None:
        if requested_serial not in ready:
            raise AcceptanceError(
                f"requested Android device is not ready: {requested_serial}"
            )
        return requested_serial
    if len(ready) != 1:
        raise AcceptanceError(
            f"expected exactly one ready Android device, found {len(ready)}"
        )
    return ready[0]


def runtime_log_failures(contents: str) -> list[str]:
    return [
        signature
        for signature in RUNTIME_FAILURE_PATTERNS
        if signature in contents
    ]
```

- [ ] **Step 4: Run the tests and verify GREEN**

Run the Task 1 command. Expected: all Task 1 tests pass.

- [ ] **Step 5: Commit**

```sh
git add platform/android/android_device_acceptance.py \
  tests/python_build/test_android_device_acceptance.py
git commit -m "Define Android device acceptance contracts"
```

### Task 2: Build isolated source-template scenarios

**Files:**
- Modify: `tests/python_build/test_android_device_acceptance.py`
- Modify: `platform/android/android_device_acceptance.py`

- [ ] **Step 1: Write failing tests for safe extraction and Gradle argv**

Create a minimal source-template ZIP containing the required root Gradle files,
three standalone AAR paths, instrumented assets, and a Gradle wrapper. Require:

```python
def test_stage_scenario_preserves_template_and_adds_smoke_assets(self) -> None:
    scenario = tool.stage_scenario(
        self.source_template,
        self.workspace / "canonical",
    )
    self.assertTrue((scenario / "libs/debug/foundry-debug.aar").is_file())
    self.assertEqual(
        b"foundry project",
        (scenario / "src/main/assets/project.foundry").read_bytes(),
    )


def test_gradle_command_omits_canonical_override_and_sets_custom_override(self) -> None:
    canonical = tool.gradle_acceptance_command(
        Path("/scenario/gradlew"),
        "arm64-v8a",
        tool.DEFAULT_APPLICATION_ID,
    )
    self.assertNotIn("-Pexport_package_name=", " ".join(canonical))
    custom = tool.gradle_acceptance_command(
        Path("/scenario/gradlew"),
        "arm64-v8a",
        "dev.example.foundryacceptance",
    )
    self.assertIn(
        "-Pexport_package_name=dev.example.foundryacceptance",
        custom,
    )
    self.assertIn("assembleStandardDebug", custom)
    self.assertIn("connectedInstrumentedDebugAndroidTest", custom)
    self.assertIn("-Pexport_enabled_abis=arm64-v8a|", custom)
```

Also require rejection of unsafe ZIP paths and symbolic links.

- [ ] **Step 2: Run the focused tests and verify RED**

Run:

```sh
python3 -m unittest \
  tests.python_build.test_android_device_acceptance.AndroidDeviceAcceptanceTests.test_stage_scenario_preserves_template_and_adds_smoke_assets \
  tests.python_build.test_android_device_acceptance.AndroidDeviceAcceptanceTests.test_gradle_command_omits_canonical_override_and_sets_custom_override -v
```

Expected: failure because staging and Gradle command construction do not exist.

- [ ] **Step 3: Implement staging and command construction**

Reuse `android_source_template.inspect_source_template()` before extracting.
Extract only its validated regular members, copy
`src/instrumented/assets/**` to `src/main/assets/**`, make `gradlew`
executable, and construct:

```python
command = [
    str(gradlew),
    "--no-daemon",
    "assembleStandardDebug",
    "connectedInstrumentedDebugAndroidTest",
    f"-Pexport_enabled_abis={abi}|",
    "-Pperform_signing=true",
    "-Pperform_zipalign=true",
    (
        "-Pandroid.testInstrumentationRunnerArguments.class="
        "games.cafecito.foundry.game.FoundryAppTest"
        "#runtimeBootsWithCanonicalPluginProtocol"
    ),
]
if application_id != DEFAULT_APPLICATION_ID:
    command.append(f"-Pexport_package_name={application_id}")
```

- [ ] **Step 4: Run the entire driver unit suite and verify GREEN**

Run:

```sh
python3 -m unittest tests.python_build.test_android_device_acceptance -v
```

Expected: all current tests pass.

- [ ] **Step 5: Commit**

```sh
git add platform/android/android_device_acceptance.py \
  tests/python_build/test_android_device_acceptance.py
git commit -m "Stage Android source-template smoke scenarios"
```

### Task 3: Prove instrumentation, APK identity, install, and start

**Files:**
- Modify: `tests/python_build/test_android_device_acceptance.py`
- Modify: `platform/android/android_device_acceptance.py`

- [ ] **Step 1: Write failing orchestration tests**

Use a deterministic `FakeRunner` returning `CommandResult` values for
`adb devices`, boot properties, ABI, Gradle, `apkanalyzer`, install,
`am start -W`, `pidof`, logcat, and uninstall. Require:

```python
def test_acceptance_runs_canonical_and_custom_scenarios(self) -> None:
    report = tool.run_source_template_acceptance(
        source_template=self.source_template,
        work_dir=self.workspace / "work",
        evidence_dir=self.workspace / "evidence",
        adb=Path("/sdk/platform-tools/adb"),
        apkanalyzer=Path("/sdk/cmdline-tools/latest/bin/apkanalyzer"),
        requested_serial="emulator-5554",
        runner=self.runner,
    )
    self.assertEqual(
        [
            tool.DEFAULT_APPLICATION_ID,
            tool.CUSTOM_APPLICATION_ID,
        ],
        [scenario["application_id"] for scenario in report["scenarios"]],
    )
    self.assertTrue(all(scenario["instrumentation_passed"] for scenario in report["scenarios"]))
    self.assertTrue(all(scenario["start_status"] == "ok" for scenario in report["scenarios"]))
```

Add separate failures for:

- boot timeout;
- missing named JUnit test case;
- failed JUnit test case;
- wrong `apkanalyzer manifest application-id`;
- failed install;
- `am start -W` without `Status: ok`;
- process timeout;
- each runtime log signature;
- cleanup attempted after primary failure.

- [ ] **Step 2: Run the orchestration tests and verify RED**

Run:

```sh
python3 -m unittest tests.python_build.test_android_device_acceptance -v
```

Expected: failures for missing command runner, report parsers, polling, scenario
orchestration, and cleanup.

- [ ] **Step 3: Implement command execution and proof parsers**

Add:

```python
@dataclass(frozen=True)
class CommandResult:
    argv: tuple[str, ...]
    returncode: int
    stdout: str
    stderr: str


class SubprocessRunner:
    def run(
        self,
        argv: Sequence[str],
        *,
        cwd: Path | None,
        timeout: float,
        description: str,
    ) -> CommandResult:
        try:
            completed = subprocess.run(
                list(argv),
                cwd=cwd,
                check=False,
                capture_output=True,
                text=True,
                timeout=timeout,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise AcceptanceError(f"{description} failed: {error}") from error
        result = CommandResult(
            tuple(argv),
            completed.returncode,
            completed.stdout,
            completed.stderr,
        )
        if result.returncode != 0:
            raise AcceptanceError(
                f"{description} failed with exit {result.returncode}:\n"
                f"{result.stdout}{result.stderr}"
            )
        return result
```

Run every command without `shell=True`, write one command log per invocation,
and include stdout/stderr in `AcceptanceError` diagnostics on nonzero exit.

Parse the exact named JUnit XML case with `xml.etree.ElementTree`; compute APK
SHA-256 with `hashlib`; obtain application IDs with:

```sh
apkanalyzer manifest application-id APK_PATH
```

Poll boot via `adb shell getprop sys.boot_completed` and the process via
`adb shell pidof APPLICATION_ID`. Start the fixed component:

```text
APPLICATION_ID/games.cafecito.foundry.game.FoundryAppLauncher
```

Read both PID-filtered and unfiltered captured logcat before cleanup so an
early native crash cannot disappear with its process.

- [ ] **Step 4: Implement source-template and APK-only CLIs**

Expose:

```sh
python3 platform/android/android_device_acceptance.py source-template \
  --source-template bin/android_source.zip \
  --work-dir .test_scratch/android-device-acceptance \
  --evidence-dir .test_scratch/android-device-evidence \
  --adb "$ANDROID_SDK_ROOT/platform-tools/adb" \
  --apkanalyzer "$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/apkanalyzer" \
  --serial emulator-5554

python3 platform/android/android_device_acceptance.py verify-apks \
  --apk games.cafecito.foundry.game=.test_scratch/android-cli-export/canonical.apk \
  --apk dev.example.foundryacceptance=.test_scratch/android-cli-export/custom.apk \
  --evidence-dir .test_scratch/android-cli-export-evidence \
  --adb "$ANDROID_SDK_ROOT/platform-tools/adb" \
  --apkanalyzer "$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/apkanalyzer" \
  --serial emulator-5554
```

Both commands atomically write canonical `report.json`.

- [ ] **Step 5: Run all driver tests and verify GREEN**

Run:

```sh
python3 -m unittest tests.python_build.test_android_device_acceptance -v
python3 -m py_compile platform/android/android_device_acceptance.py
```

Expected: all tests pass and the driver compiles.

- [ ] **Step 6: Commit**

```sh
git add platform/android/android_device_acceptance.py \
  tests/python_build/test_android_device_acceptance.py
git commit -m "Verify Android runtime on connected devices"
```

### Task 4: Add the focused runtime/plugin instrumentation smoke

**Files:**
- Modify: `platform/android/java/app/src/androidTestInstrumented/java/games/cafecito/foundry/game/FoundryAppTest.kt`
- Modify: `misc/scripts/test_android_runtime_identifiers.py`

- [ ] **Step 1: Extend the source contract and verify RED**

Require the Android identifier guard to find:

```python
require_contains(
    app_test,
    "runtimeBootsWithCanonicalPluginProtocol",
    failures,
)
require_contains(
    app_test,
    '"games.cafecito.foundry.plugin.v1.FoundryAppInstrumentedTestPlugin"',
    failures,
)
require_contains(app_test, '"org.godotengine.plugin.v1.Legacy"', failures)
require_contains(app_test, '"org.godotengine.plugin.v2.Legacy"', failures)
```

Run:

```sh
python3 misc/scripts/test_android_runtime_identifiers.py
```

Expected: failure because the focused smoke is absent.

- [ ] **Step 2: Add the focused instrumented test**

Add imports for `assertFalse` and this method:

```kotlin
@Test
fun runtimeBootsWithCanonicalPluginProtocol() {
    ActivityScenario.launch(FoundryApp::class.java).use {
        val plugin = getTestPlugin()
        assertNotNull(plugin)
        plugin.waitForFoundryMainLoopStarted()

        assertEquals(
            "FoundryAppInstrumentedTestPlugin",
            FoundryPluginRegistry.getPluginNameFromMetadata(
                "games.cafecito.foundry.plugin.v1.FoundryAppInstrumentedTestPlugin"
            )
        )
        assertNull(
            FoundryPluginRegistry.getPluginNameFromMetadata(
                "org.godotengine.plugin.v1.Legacy"
            )
        )
        assertNull(
            FoundryPluginRegistry.getPluginNameFromMetadata(
                "org.godotengine.plugin.v2.Legacy"
            )
        )
        assertFalse(FoundryPluginRegistry.getPluginRegistry().isEmpty())
    }
}
```

- [ ] **Step 3: Verify GREEN**

Run:

```sh
python3 misc/scripts/test_android_runtime_identifiers.py
```

Expected: identifier guard passes. Task 7's real source-template acceptance
compiles and executes this exact Kotlin test after the assembled artifact is
available.

- [ ] **Step 4: Commit**

```sh
git add misc/scripts/test_android_runtime_identifiers.py \
  platform/android/java/app/src/androidTestInstrumented/java/games/cafecito/foundry/game/FoundryAppTest.kt
git commit -m "Add Android runtime plugin smoke coverage"
```

### Task 5: Add the emulator-backed CI gate

**Files:**
- Modify: `.github/scripts/test_android_runtime_workflows.py`
- Modify: `.github/workflows/android_builds.yml`

- [ ] **Step 1: Write the workflow contract first**

Add a test that requires `device-acceptance` to:

```python
job = _job(self.android, "device-acceptance")
self.assertIn("- assemble-android", job)
self.assertIn("name: android-runtime-assembled", job)
self.assertIn("system-images;android-36;default;x86_64", job)
self.assertIn("sudo chmod 666 /dev/kvm", job)
self.assertIn("sys.boot_completed", job)
self.assertIn("android_device_acceptance.py source-template", job)
self.assertIn("--serial emulator-5554", job)
self.assertIn("if: always()", job)
self.assertIn("android-device-acceptance-evidence", job)
```

Also require `validate-runtime-contracts` to run
`tests.python_build.test_android_device_acceptance`.

- [ ] **Step 2: Run the workflow test and verify RED**

Run:

```sh
python3 .github/scripts/test_android_runtime_workflows.py
```

Expected: failure because `device-acceptance` is missing.

- [ ] **Step 3: Implement the downstream emulator job**

Add an Ubuntu 24.04 job with `timeout-minutes: 60`, checkout at
`${{ inputs.checkout-ref || github.sha }}`, Java 17, assembled-artifact
download, SDK package installation, KVM permission, AVD creation, background
emulator launch, observable boot polling, driver invocation, and
`actions/upload-artifact` guarded by `if: always()`.

Use:

```sh
sdkmanager \
  "platform-tools" \
  "emulator" \
  "platforms;android-36" \
  "build-tools;36.1.0" \
  "system-images;android-36;default;x86_64"
echo no | avdmanager create avd --force \
  --name foundry-acceptance \
  --package "system-images;android-36;default;x86_64"
nohup emulator -avd foundry-acceptance \
  -no-window -no-audio -no-boot-anim -no-snapshot -wipe-data \
  -gpu swiftshader_indirect \
  > "$RUNNER_TEMP/foundry-emulator.log" 2>&1 &
```

The driver performs the bounded boot wait and both application-ID scenarios.

- [ ] **Step 4: Run the workflow contract and YAML checks**

Run:

```sh
python3 .github/scripts/test_android_runtime_workflows.py
pre-commit run check-yaml --files .github/workflows/android_builds.yml
```

Expected: both pass.

- [ ] **Step 5: Commit**

```sh
git add .github/scripts/test_android_runtime_workflows.py \
  .github/workflows/android_builds.yml
git commit -m "Run Android runtime acceptance on an emulator"
```

### Task 6: Document the completion evidence and wire pre-commit

**Files:**
- Modify: `tests/python_build/test_android_gradle_runtime_contract.py`
- Modify: `.pre-commit-config.yaml`
- Modify: `platform/android/ANDROID_RUNTIME.md`

- [ ] **Step 1: Write failing documentation/pre-commit assertions**

Require the runtime document to contain:

- `android_device_acceptance.py source-template`;
- `android_device_acceptance.py verify-apks`;
- both exact application IDs;
- `games.cafecito.foundry.plugin.v1.`;
- all twelve matrix cells as the artifact authority;
- pinned standalone `verify_jni_contract.py` results against compiled
  declarations and every native payload;
- JNI, AAR, APK, source ZIP, device, and exporter evidence mappings;
- the ordered bindings/engine compatibility and release checklist.

Require pre-commit file filters to cover the driver, driver tests, Android app
test, runtime documentation, workflow, and workflow contract.

- [ ] **Step 2: Run the documentation contract and verify RED**

Run:

```sh
python3 -m unittest \
  tests.python_build.test_android_gradle_runtime_contract.AndroidGradleRuntimeContractTests.test_android_runtime_documentation_covers_authoritative_and_offline_flows -v
```

Expected: failure for the missing device acceptance and evidence-map text.

- [ ] **Step 3: Document the device and release procedure**

Extend `ANDROID_RUNTIME.md` with:

1. emulator/device prerequisites and both driver commands;
2. canonical/default versus custom application-ID semantics;
3. exact plugin v1 and removed-alias expectations;
4. JSON/log/JUnit evidence locations;
5. a table mapping every #1223/#1224 criterion to its authoritative proof;
6. pin update → clean 12-cell build → standalone preparation/JNI/artifact
   inspection → source-template inspection → emulator gate → local CLI export
   → release ordering.

- [ ] **Step 4: Wire and verify the scoped pre-commit contracts**

Run:

```sh
pre-commit run foundry-android-runtime-identifiers --all-files
pre-commit run foundry-android-runtime-workflows --all-files
python3 -m unittest \
  tests.python_build.test_android_gradle_runtime_contract \
  tests.python_build.test_android_device_acceptance -v
```

Expected: all pass.

- [ ] **Step 5: Commit**

```sh
git add .pre-commit-config.yaml platform/android/ANDROID_RUNTIME.md \
  tests/python_build/test_android_gradle_runtime_contract.py
git commit -m "Document Android runtime acceptance evidence"
```

### Task 7: Run real local Android acceptance

**Files:**
- Generated only under `.test_scratch/` and the host Android SDK/AVD directory.

- [ ] **Step 1: Install and create the local API 36 arm64 AVD**

Run:

```sh
export ANDROID_SDK_ROOT=/Users/christian/Library/Android/sdk
"$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/sdkmanager" \
  "system-images;android-36;default;arm64-v8a"
echo no | "$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/avdmanager" \
  create avd --force --name foundry-acceptance-api36 \
  --package "system-images;android-36;default;arm64-v8a"
"$ANDROID_SDK_ROOT/emulator/emulator" \
  -avd foundry-acceptance-api36 -no-window -no-audio -no-boot-anim \
  -no-snapshot -wipe-data -gpu swiftshader_indirect
```

Keep the emulator in a managed foreground session and let the driver perform
the bounded boot wait.

- [ ] **Step 2: Obtain an exact assembled artifact**

Use the successful develop workflow artifact at Foundry commit
`9560acf7a99758013737a5b275bc08e65b4a6680`, or rebuild the complete 12-cell
matrix and `generateFoundryTemplates` from the exact issue revision. Verify the
native provenance revision before use.

- [ ] **Step 3: Run the source-template device acceptance**

Run the `source-template` command from Task 3. Expected:

- focused JUnit smoke passes twice;
- canonical and custom standard APKs report their exact IDs;
- both install and `am start -W` with `Status: ok`;
- both processes become live;
- no forbidden runtime error appears;
- canonical JSON report records both scenarios.

- [ ] **Step 4: Build the strict editor**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j8
```

Expected: exit 0 with strict warnings-as-errors enabled.

- [ ] **Step 5: Export a minimal project with both IDs**

Create the project under `.test_scratch/android-cli-export/`. Its
`project.foundry` is:

```ini
[application]

config/name="Foundry Android Acceptance"
run/main_scene="res://main.tscn"

[display]

window/size/viewport_width=640
window/size/viewport_height=360

[rendering]

renderer/rendering_method="gl_compatibility"
renderer/rendering_method.mobile="gl_compatibility"
textures/vram_compression/import_etc2_astc=true
```

Its `main.tscn` is:

```ini
[gd_scene load_steps=2 format=3]

[ext_resource path="res://main.fs" type="Script" id="1"]

[node name="AndroidAcceptance" type="Node"]
script = ExtResource("1")
```

Its `main.fs` is:

```gdscript
extends Node

func _ready() -> void:
	print("FOUNDRY_ANDROID_ACCEPTANCE_READY")
```

Its `export_presets.cfg` has two Android presets that differ only by name and
application ID:

```ini
[preset.0]

name="Android Canonical"
platform="Android"
runnable=true
advanced_options=true
dedicated_server=false
custom_features=""
export_filter="all_resources"
include_filter=""
exclude_filter=""
export_path=""
script_export_mode=2

[preset.0.options]

gradle_build/use_gradle_build=true
gradle_build/android_source_template="/Users/christian/CafecitoGames/Foundry/.worktrees/issue-1224/.test_scratch/android-runtime-assembled/android_source.zip"
architectures/armeabi-v7a=false
architectures/arm64-v8a=true
architectures/x86=false
architectures/x86_64=false
package/unique_name="games.cafecito.foundry.game"
package/name="Foundry Android Acceptance"
package/signed=true

[preset.1]

name="Android Custom"
platform="Android"
runnable=false
advanced_options=true
dedicated_server=false
custom_features=""
export_filter="all_resources"
include_filter=""
exclude_filter=""
export_path=""
script_export_mode=2

[preset.1.options]

gradle_build/use_gradle_build=true
gradle_build/android_source_template="/Users/christian/CafecitoGames/Foundry/.worktrees/issue-1224/.test_scratch/android-runtime-assembled/android_source.zip"
architectures/armeabi-v7a=false
architectures/arm64-v8a=true
architectures/x86=false
architectures/x86_64=false
package/unique_name="dev.example.foundryacceptance"
package/name="Foundry Android Acceptance"
package/signed=true
```

Install `openjdk@17` with Homebrew when absent, then set
`ANDROID_HOME=/Users/christian/Library/Android/sdk` and
`JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home` so
the exporter initializes exact SDK settings. Run:

```sh
./bin/foundry.macos.editor.arm64 --headless project export \
  --project .test_scratch/android-cli-export \
  --preset "Android Canonical" \
  --output .test_scratch/android-cli-export/canonical.apk \
  --mode debug --install-android-build-template

./bin/foundry.macos.editor.arm64 --headless project export \
  --project .test_scratch/android-cli-export \
  --preset "Android Custom" \
  --output .test_scratch/android-cli-export/custom.apk \
  --mode debug --install-android-build-template
```

Expected: both command-first exports succeed.

- [ ] **Step 6: Verify both CLI-exported APKs on the emulator**

Run the `verify-apks` command from Task 3. Expected: both manifest IDs,
installs, starts, processes, and log scans pass, with a separate canonical JSON
report.

- [ ] **Step 7: Record exact evidence**

Record in the PR/issue:

- Foundry and standalone commits/trees;
- native provenance revision;
- emulator API, ABI, and serial;
- application IDs and APK SHA-256 hashes;
- JUnit test name/status;
- install/start/process/logcat status;
- evidence report paths.

### Task 8: Final verification and review convergence

**Files:**
- All branch changes.

- [ ] **Step 1: Run the complete Android contract suite**

Run:

```sh
python3 -m unittest \
  tests.python_build.test_android_runtime_contract \
  tests.python_build.test_android_runtime_build \
  tests.python_build.test_android_gradle_runtime_contract \
  tests.python_build.test_android_device_acceptance -v
python3 tests/python_build/test_android_runtime_surface.py
python3 misc/scripts/test_android_runtime_identifiers.py
python3 .github/scripts/test_android_runtime_workflows.py
python3 misc/scripts/test_release_resolver.py
```

Expected: all pass.

- [ ] **Step 2: Run scoped formatting/static gates**

Run the applicable Ruff format/check, mypy, codespell, YAML, executable
shebang/mode, copyright, and Android local pre-commit hooks against the changed
files. Run `git diff --check origin/develop...HEAD`.

Expected: all branch-scoped gates pass without modifying unrelated files.

- [ ] **Step 3: Run the full command-first Foundry suite**

Run:

```sh
./bin/foundry.macos.editor.arm64 --headless test run \
  --progress-format=jsonl \
  --progress-file=/tmp/foundry-1224-test-progress.jsonl \
  --force-colors
```

Expected: structured `run_end` success and doctest `Status: SUCCESS!`.

- [ ] **Step 4: Commit any verification-only corrections**

Run:

```sh
git status --short --untracked-files=all
git diff --check origin/develop...HEAD
```

Expected: no uncommitted tracked changes and no unexpected untracked files.

- [ ] **Step 5: Run Cursor review until exact clean**

Run the `cursor-review` skill in read-only plan mode against
`origin/develop`. Validate every finding with
`superpowers:receiving-code-review`; use systematic debugging and a new failing
test for every real defect. Commit fixes, rerun relevant verification, and
start a fresh Cursor round until the latest valid output is exactly:

```text
RESULT: clean
FINDINGS:
- none
```

- [ ] **Step 6: Push, open the ready PR, and enable squash auto-merge**

Open against `develop`; summarize layered evidence and local emulator results;
end the body with `Closes #1224`; enable:

```sh
gh pr merge --squash --auto
```

Monitor every required check, including the emulator acceptance job, through
actual merge.

- [ ] **Step 7: Complete tracking and cleanup**

Verify issue #1224 closes as completed, set its Experiment project item to
Done, remove the worktree, delete local/remote issue branches as appropriate,
and report the merge commit, Cursor rounds, CI, device evidence, follow-up
issues (if any), and cleanup status to the epic orchestrator.
