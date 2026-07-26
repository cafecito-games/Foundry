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

## Verification boundary

This integration proves the source, provenance, JNI, AAR, APK, and source-ZIP
contracts in local and CI builds. It does not claim installation or launch
validation on a physical device or emulator. That device-level acceptance,
including runtime smoke coverage, is tracked separately in
[issue #1224](https://github.com/cafecito-games/Foundry/issues/1224).
