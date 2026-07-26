# Standalone Android runtime integration

Foundry owns the Android C++ platform, JNI implementation, exporter, application
template, and native-library production. The separate
[Foundry-Android repository](https://github.com/cafecito-games/Foundry-Android)
owns the Java, Kotlin, AIDL, resources, manifest, runtime tests, AAR assembly,
and Maven publication. Foundry consumes its AARs as immutable build outputs; it
does not keep a second copy of runtime source or publication logic.

`platform/android/foundry_android_runtime.json` is the canonical bridge between
the repositories. It pins:

- `"repository": "https://github.com/cafecito-games/Foundry-Android.git"`;
- an exact standalone `"revision":` and Git `"tree":`;
- the bindings version and JNI contract version;
- the compatibility policy and required standalone tools;
- the exact debug, dev, and release AAR output paths.

Foundry-Android is the sole Maven publisher. Foundry release jobs build and
package the pinned standalone runtime but never publish its Maven coordinates.

## Authoritative native matrix

An authoritative runtime build begins from a clean Foundry checkout. Every
native cell records the exact Foundry commit, Git tree, build settings, ABI,
library sizes, and SHA-256 hashes in `provenance.json`. A dirty checkout, a
revision or tree mismatch, a mislabeled cell, an unexpected file, or a changed
library hash fails validation.

The complete matrix is three build types by four Android ABIs:

| Runtime | Target | Required SCons flags |
| --- | --- | --- |
| debug | `template_debug` | `production=no dev_mode=no dev_build=no debug_symbols=no` |
| dev | `template_debug` | `production=no dev_mode=yes dev_build=yes debug_symbols=yes` |
| release | `template_release` | `production=yes dev_mode=no dev_build=no debug_symbols=no` |

| Android ABI | SCons `arch` |
| --- | --- |
| `armeabi-v7a` | `arm32` |
| `arm64-v8a` | `arm64` |
| `x86` | `x86_32` |
| `x86_64` | `x86_64` |

Each cell also requires `platform=android tests=no swappy=yes` and the target
shown above. For example, the debug ARM64 cell is:

```sh
scons platform=android target=template_debug arch=arm64 \
  production=no dev_mode=no dev_build=no debug_symbols=no \
  tests=no swappy=yes
```

Repeat with every row in both tables. SCons writes each result under:

```text
bin/android-native/<foundry-revision>/<build-type>/<abi>/
├── libfoundry_android.so
├── libc++_shared.so
└── provenance.json
```

The consumer accepts only the full 12-cell root. A partial matrix can be useful
for narrow local compilation, but it is non-authoritative and cannot produce a
release-ready AAR set or source template.

## Prepare the pinned runtime

The driver exports the exact pinned standalone Git object, derives compatibility
metadata from the clean Foundry checkout, validates or creates the native
bundle with the pinned standalone tool, runs the standalone Gradle verification
suite, and promotes all three AARs only after success.

For an offline build with a prefetched standalone Git repository and the full
native root:

```sh
python3 platform/android/android_runtime_build.py prepare \
  --pin platform/android/foundry_android_runtime.json \
  --engine-source . \
  --scratch .test_scratch/android-runtime \
  --output-aars platform/android/java/build/foundryAndroidRuntime/aars \
  --source-repository /absolute/path/to/Foundry-Android \
  --native-root "bin/android-native/$(git rev-parse HEAD)"
```

The standalone repository must contain the pinned commit as a Git object. Its
working tree is not copied: the driver exports the exact revision and checks
its tree, bindings version, JNI contract, and required tools.

There are two explicit alternatives:

- Replace `--source-repository` with `--allow-fetch` to opt in to fetching only
  the repository and exact commit from the pin. Network access is never
  implicit.
- Replace `--native-root` with `--native-bundle /absolute/path/to/foundry-native.zip`
  to consume a prebuilt complete bundle. The pinned standalone verifier still
  checks its compatibility metadata, matrix, ABI identity, and hashes.

Exactly one source option and exactly one native option are required. `--scratch`
and `--output-aars` must be outside the source tree or Git-ignored; generated
runtime source is never written into tracked paths. Successful AAR output is:

```text
platform/android/java/build/foundryAndroidRuntime/aars/
├── debug/foundry-debug.aar
├── dev/foundry-dev.aar
└── release/foundry-release.aar
```

## Build export templates

Gradle exposes the same explicit inputs as project properties:

```sh
cd platform/android/java
./gradlew --no-daemon generateFoundryTemplates \
  -PfoundryAndroidSource=/absolute/path/to/Foundry-Android \
  -PfoundryNativeRoot=/absolute/path/to/bin/android-native/<foundry-revision> \
  -PfoundryRuntimeScratch=/absolute/path/to/ignored/runtime-scratch
```

For an opted-in network build, use `-PfoundryAndroidFetch=true` instead of
`-PfoundryAndroidSource`. For a prebuilt bundle, use
`-PfoundryNativeBundle=/absolute/path/to/foundry-native.zip` instead of
`-PfoundryNativeRoot`. These alternatives preserve the same offline and
provenance checks as the Python driver.

The stable artifacts remain:

```text
bin/android_debug.apk
bin/android_dev.apk
bin/android_release.apk
bin/android_source.zip
bin/foundry-debug.aar
bin/foundry-dev.aar
bin/foundry-release.aar
```

`android_source.zip` is staged outside `bin/`, inspected, and atomically
promoted only after all three requested APK variants succeed. It must contain
exactly these standalone AAR paths at its root:

```text
libs/debug/foundry-debug.aar
libs/dev/foundry-dev.aar
libs/release/foundry-release.aar
```

Inspect the final archive independently before promotion or release:

```sh
python3 platform/android/android_source_template.py inspect \
  --archive bin/android_source.zip
```

The inspector rejects missing, empty, extra, or legacy AARs; unsafe paths;
symbolic links; and runtime Java/Kotlin/AIDL source outside the application
package. This prevents stale in-tree runtime code from re-entering the custom
build template.

## Update the standalone pin

Pin changes are explicit compatibility changes, not floating dependency
updates:

1. Fetch the intended Foundry-Android commit into a local repository.
2. Resolve and record the exact commit and tree:

   ```sh
   git -C /absolute/path/to/Foundry-Android rev-parse --verify <revision>^{commit}
   git -C /absolute/path/to/Foundry-Android rev-parse --verify <revision>^{tree}
   ```

3. Review that revision's `compatibility/foundry-engine.json`,
   `tools/native_bundle.py`, `tools/sync_engine_pin.py`, and
   `tools/verify_jni_contract.py`. Update the pin's bindings or JNI contract
   only with the corresponding reviewed compatibility change.
4. Replace `"revision":` and `"tree":` in
   `platform/android/foundry_android_runtime.json`, preserving canonical
   one-line sorted JSON.
5. Rebuild the full 12-cell matrix from the exact Foundry commit, run the
   preparation driver, generate the templates, and inspect
   `bin/android_source.zip`.

The driver rejects dirty Foundry source, mixed native revisions, pin drift, and
incompatible standalone inputs before Gradle assembles an AAR.

## Compiled JNI and artifact verification

Runtime preparation invokes the pinned standalone tools rather than inferring
compatibility from source names. `tools/verify_jni_contract.py` reads the
compiled Java/Kotlin native declarations from the runtime classes JAR with
`javap`, applies JNI escaping and overload rules, and compares that exact set
with every native payload in the validated 12-cell bundle. A missing or stale
Foundry JNI symbol fails the build.

The standalone `tools/inspect_artifacts.py` gate independently opens every
debug, dev, and release AAR and publication. It checks compiled
`BuildConfig.class` values, classes, manifests, resources, every packaged ELF,
sources, documentation, POM and module metadata, and rejects stale runtime
identities. License, notice, and native provenance records are required
evidence; they are not treated as stale product identifiers.

## Device or emulator acceptance

Issue #1224 closes the previous device-validation boundary with a reproducible
exported-game gate.

Use an API 36 device or emulator whose ABI is one of `armeabi-v7a`,
`arm64-v8a`, `x86`, or `x86_64`. Java 17, `adb`, `apkanalyzer`, the matching
system image, and a freshly assembled `android_source.zip` are required. The
work and evidence directories are owned by one run and must not already exist.

The source-template mode builds and instruments isolated canonical and custom
application-ID scenarios, then installs and starts each standard APK:

```sh
python3 platform/android/android_device_acceptance.py source-template \
  --source-template bin/android_source.zip \
  --work-dir .test_scratch/android-device-acceptance \
  --evidence-dir .test_scratch/android-device-evidence \
  --adb "${ANDROID_SDK_ROOT}/platform-tools/adb" \
  --apkanalyzer "${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin/apkanalyzer" \
  --serial emulator-5554
```

The canonical scenario deliberately omits the Gradle package override and must
produce `games.cafecito.foundry.game`. The second scenario explicitly overrides
the package with `dev.example.foundryacceptance`. For both, the driver requires
the focused JUnit case, verifies the manifest application ID with
`apkanalyzer`, installs the APK, requires `am start -W` to report `Status: ok`,
waits for one live process, and scans both complete and PID-filtered logcat for
linkage, class-loading, native-library, and fatal-exception failures.

Plugin discovery recognizes exactly
`games.cafecito.foundry.plugin.v1.*`. The focused instrumented fixture presents
that canonical key plus `org.godotengine.plugin.v1.Legacy` and
`org.godotengine.plugin.v2.Legacy`; after the Foundry main loop starts, only the
canonical plugin may be registered. The legacy entries are negative test data
and must never become supported aliases.

To verify APKs produced through the real command-first editor exporter, use a
fresh evidence directory:

```sh
python3 platform/android/android_device_acceptance.py verify-apks \
  --apk games.cafecito.foundry.game=.test_scratch/android-cli-export/canonical.apk \
  --apk dev.example.foundryacceptance=.test_scratch/android-cli-export/custom.apk \
  --evidence-dir .test_scratch/android-cli-export-evidence \
  --adb "${ANDROID_SDK_ROOT}/platform-tools/adb" \
  --apkanalyzer "${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin/apkanalyzer" \
  --serial emulator-5554
```

Each successful mode atomically writes `report.json`. Failures write
`failure.json` when the evidence directory is available. Real command
stdout/stderr is retained under `commands/`, per-application full and
PID-filtered output is written as `<application-id>-logcat.txt`, and the source
mode records the exact JUnit XML under
`<work-dir>/<scenario>/build/outputs/androidTest-results/connected/instrumentedDebug/`.
CI uploads this evidence together with the emulator log even when a step fails.

## Acceptance evidence map

| Completion criterion | Authoritative proof |
| --- | --- |
| Native build types and ABIs | The clean 3×4 `android_builds.yml` matrix, each cell's `provenance.json`, and standalone native-bundle validation prove all twelve payloads and ELF identities. |
| JNI declarations/exports | Pinned standalone `tools/verify_jni_contract.py` compares compiled Java/Kotlin native declarations with every native payload. |
| AAR identity/content | Standalone Gradle tests and `tools/inspect_artifacts.py` inspect all three direct AARs and release publication artifacts. |
| APK identity/content | Template assembly plus `apkanalyzer manifest application-id` prove the canonical and custom IDs in the installed bytes. |
| Source ZIP | `android_source_template.py inspect` proves the promoted archive has only the three standalone AARs and safe application-template content. |
| Plugin protocol | `runtimeBootsWithCanonicalPluginProtocol` boots the activity and accepts `games.cafecito.foundry.plugin.v1.*` while rejecting both legacy variants. |
| Device runtime | `android_device_acceptance.py source-template` provides named JUnit, install, start, PID, and clean-log evidence for both application IDs. |
| Editor exporter | Command-first `project export` for both presets followed by `android_device_acceptance.py verify-apks` proves the real exporter path. |
| Compiled/runtime identifiers | Engine identifier guards and standalone artifact inspection reject stale names in classes, resources, metadata, JNI, and ELF payloads while retaining license and provenance evidence. |

## Release alignment checklist

Bindings, engine source, JNI, and publication versions move as one reviewed
compatibility set. Use this order:

1. Choose the clean Foundry engine revision and intended bindings version.
   Update native/JNI sources first and keep the JNI contract version unchanged
   unless the compiled declarations and exports intentionally change.
2. In Foundry-Android, run `tools/sync_engine_pin.py` against that exact public
   engine revision. Review `compatibility/foundry-engine.json`, including the
   engine version components, bindings version, JNI contract, ABI map, and
   external JNI allowlist.
3. Build all twelve native cells from the same clean Foundry revision. Run
   standalone preparation so `tools/verify_jni_contract.py`, native-bundle
   validation, all JVM/instrumented compilation, and artifact inspection pass.
4. Generate all Foundry templates, inspect the final source ZIP, and run the
   emulator source-template gate for both application IDs.
5. Build the strict Foundry editor, export the minimal project through the
   command-first CLI with both IDs, and run the APK-only device gate.
6. For a non-snapshot standalone release, tag Foundry-Android exactly
   `v<bindings.version>`. Its release workflow must publish immutable Central
   coordinates successfully before creating the standalone GitHub release; do
   not retry after Central has accepted an upload without checking its recorded
   deployment ID.
7. Pin the exact released standalone commit and tree in
   `platform/android/foundry_android_runtime.json`, rerun the complete matrix,
   artifact, device, exporter, and release-package gates, then release Foundry.
   Foundry-Android remains the sole Maven publisher.

No individual layer substitutes for another: a green matrix is not a device
boot, a compiled instrumented APK is not an executed JUnit result, and a direct
Gradle APK is not proof of the editor exporter path.
