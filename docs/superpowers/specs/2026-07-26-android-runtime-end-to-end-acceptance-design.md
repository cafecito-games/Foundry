# Android Runtime End-to-End Acceptance Design

## Context

Epic #1223 split the Android runtime across two repositories:

- Foundry owns the exporter, application source template, C++ JNI implementation,
  and native `libfoundry_android.so` production.
- Foundry-Android owns the Java/Kotlin/AIDL runtime, runtime resources, JNI
  declaration verification, AAR assembly, and publication.

The integration merged for #1225 already proves the exact three-build-type by
four-ABI native matrix, standalone revision pin, compatibility metadata, JNI
declaration/symbol parity, AAR/APK/source-template contents, and compiled runtime
identities. It intentionally does not claim that an APK installs or starts on
Android. Issue #1224 closes that device-level gap.

## Goals

The acceptance surface must provide reproducible evidence that:

1. The generated Android source template consumes the pinned standalone AAR and
   boots the native engine on Android without missing-class or JNI linkage
   failures.
2. The canonical application ID, `games.cafecito.foundry.game`, and a
   user-configured application ID both build, install, and start.
3. Runtime plugin discovery accepts exactly
   `games.cafecito.foundry.plugin.v1.*`, loads the declared smoke plugin, and
   rejects the removed upstream metadata aliases.
4. The device proof is downstream of the authoritative 3×4 native matrix and
   the existing artifact/JNI/identity inspectors rather than becoming a second
   artifact authority.
5. The release-alignment procedure and every epic completion criterion map to a
   concrete command, CI job, or captured device result.

## Non-goals

- Running every ABI on physical hardware. The authoritative artifact inspectors
  prove all four ABI payloads; the emulator executes the ABI native to its host.
- Running every build type on a device. Debug is the smoke-test vehicle; the
  existing assembly gate proves debug, dev, and release artifact composition.
- Reimplementing standalone artifact, JNI, ELF, provenance, or branding checks.
- Publishing artifacts or changing the standalone pin.
- Treating required license attribution, third-party notices, or provenance as
  stale public/runtime branding.

## Chosen approach

Add a checked-in, standard-library Python acceptance driver and run it in a new
emulator-backed job after `assemble-android` in the reusable Android workflow.
The driver consumes the already-inspected `android_source.zip`, so it tests the
same self-contained source template installed by the Foundry exporter. It runs
one canonical-ID scenario and one custom-ID scenario in isolated extraction
directories.

The recurring CI job does not build a second editor binary. A separate local
release-evidence run uses an exact Foundry editor, the command-first
`foundry project export` CLI, the same source template, and the same emulator to
prove the exporter path itself. This keeps routine Android CI focused while
retaining an end-to-end exporter proof for epic completion.

Rejected alternatives:

- A local-only emulator checklist would prove the current revision but could
  regress immediately.
- Building an editor inside every Android acceptance job would exercise the
  exporter on every run, but duplicates the repository's strict editor build
  and substantially lengthens an already complete 12-cell Android workflow.

## Evidence architecture

Acceptance is layered. No narrow layer stands in for a broader requirement.

| Layer | Authoritative evidence |
| --- | --- |
| Native matrix | Existing SCons 3×4 jobs and canonical provenance validator |
| JNI compatibility | Pinned Foundry-Android `verify_jni_contract.py` against compiled `classes.jar` and every native payload |
| AAR/publication identities | Pinned standalone artifact inspector, including compiled classes, manifests, sources, docs, POM/module metadata, and attribution-safe stale-identity rules |
| APK/source-template composition | Existing Gradle assembly and `android_source_template.py` inspection |
| Runtime boot and plugin protocol | New instrumented smoke test on a booted emulator |
| Canonical/custom app IDs | New acceptance driver builds, installs, starts, and records both scenarios |
| Exporter path | Local exact command-first CLI export followed by the same install/start checks |
| Release procedure | `platform/android/ANDROID_RUNTIME.md` completion-evidence map and version-alignment checklist |

The emulator job depends on `assemble-android`. Therefore it cannot pass unless
all twelve native cells and every preparation/inspection gate have already
passed for the same Foundry revision.

## Instrumented smoke

`FoundryAppTest` gains one focused test that:

1. launches `FoundryApp`;
2. obtains `FoundryAppInstrumentedTestPlugin` from the runtime registry;
3. waits for `onFoundryMainLoopStarted`, proving Java-to-native startup;
4. verifies the canonical v1 metadata key resolves to the plugin name;
5. verifies the removed upstream v1 and v2 aliases return no plugin name.

The acceptance driver selects this method explicitly. Existing broader Android
instrumented tests remain unchanged and available for their normal coverage.

## Acceptance driver

`platform/android/android_device_acceptance.py` owns deterministic device
orchestration. It uses only Python's standard library and explicit executable
paths.

For each application ID, the driver:

1. validates the application ID and connected-device ABI;
2. safely extracts `android_source.zip` into a scenario-specific directory;
3. invokes the template's Gradle wrapper with:
   - the requested application ID, or no override for the canonical default;
   - only the connected ABI;
   - debug signing and zip alignment enabled;
   - the exact focused instrumentation test selected;
4. requires the Gradle connected-test task to succeed and the XML report to
   contain the named passing smoke test;
5. locates the generated target APK and confirms its manifest application ID;
6. installs the APK explicitly with `adb install -r`;
7. clears logcat, force-stops the package, and starts the fixed
   `games.cafecito.foundry.game.FoundryAppLauncher` component with
   `adb shell am start -W`;
8. waits for the package process and records its PID;
9. reads process logcat and fails on `UnsatisfiedLinkError`,
   `NoClassDefFoundError`, `ClassNotFoundException`, native-library load
   failures, or a fatal exception;
10. uninstalls target and test packages in cleanup.

The canonical scenario omits `-Pexport_package_name`, proving the source
template default. The custom scenario uses
`dev.example.foundryacceptance`. The fixed activity implementation package
remains `games.cafecito.foundry.game` in both APKs.

The driver writes one canonical JSON report with the device serial/ABI,
application IDs, APK paths and hashes, instrumentation result, start result,
process identity, and log scan. On failure it preserves the scenario workspace,
Gradle reports, command output, and logcat for diagnosis.

## CI emulator job

The reusable `.github/workflows/android_builds.yml` gains a
`device-acceptance` job that:

1. requires `assemble-android`;
2. checks out the same Foundry revision;
3. downloads `android-runtime-assembled`;
4. configures Java 17 and Android SDK API 36;
5. enables KVM on the Ubuntu runner;
6. creates and boots an API 36 x86_64 AVD without a window or audio;
7. waits on `sys.boot_completed` instead of relying on a fixed delay;
8. runs the acceptance driver for the canonical and custom IDs;
9. uploads the JSON report, Gradle XML/HTML results, command logs, and logcat
   even when the smoke fails.

The workflow-contract test statically requires this dependency, exact artifact,
two application-ID scenarios, API/ABI setup, boot wait, driver invocation, and
always-uploaded evidence. Android path filters and pre-commit coverage include
the driver and its tests.

## Local exporter evidence

Epic completion additionally records a local run using:

1. a strict editor binary built from the issue branch;
2. the exact assembled runtime/source-template artifact for the accepted native
   engine revision;
3. generated minimal `project.foundry`, scene, and Android export presets;
4. command-first exports:

   ```sh
   foundry --headless project export --project <project> \
     --preset <preset> --output <apk> --mode debug \
     --install-android-build-template
   ```

5. installation and `am start -W` of both exported IDs on the local API 36
   arm64 emulator;
6. process/logcat validation using the same forbidden-runtime-error rules.

This evidence is recorded in the issue/PR report with the exact Foundry commit,
runtime artifact revision, emulator API/ABI, package IDs, APK hashes, and
command results. Generated projects, SDK images, AVD state, APKs, and logs stay
under ignored test scratch or host SDK directories.

## Error handling and diagnostics

- Every subprocess is run without a shell, with a timeout and captured output.
- Timeouts identify the command and preserve its partial output.
- Device selection fails when zero or multiple ready devices are present unless
  a serial is supplied.
- Boot/process waits poll observable properties with explicit deadlines.
- Gradle success without the expected JUnit XML test case is a failure.
- `am start` success without a live target process is a failure.
- Cleanup errors are reported but never replace the primary failure.
- A runtime crash signature is a failure even if the build and instrumentation
  command returned zero.

## Testing strategy

The Python driver is implemented test-first. Unit tests use a fake executable
that records argv and emits controlled adb/Gradle/aapt output. Tests cover:

- canonical and custom ID command construction;
- application-ID and device-selection rejection;
- boot/process condition polling;
- JUnit XML proof requirements;
- manifest package validation;
- fatal/missing-class/JNI log signature detection;
- JSON evidence shape;
- cleanup after success and failure.

The workflow-contract test is updated test-first to fail until the
device-acceptance job is complete. The new Android instrumentation assertion is
compiled locally before the emulator run and then executed on the emulator.

Final verification includes the focused Python tests, all Android contract
tests, the standalone preparation/inspection suite, exact local emulator
acceptance, strict editor build, the command-first full Foundry test suite,
scoped pre-commit hooks, and an independent Cursor review against
`origin/develop`.
