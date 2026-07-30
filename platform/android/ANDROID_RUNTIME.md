# In-tree Android host runtime

Foundry owns its Android application host in
`platform/android/java/lib`. This internal Gradle module contains the Java,
Kotlin, AIDL, manifest, resources, lifecycle, rendering, input, storage,
services, JVM tests, and instrumented tests used by the Android platform and
export templates. The native implementation and JNI exports remain under
`platform/android/`.

The host module is not published to Maven. The engine Gradle graph includes
`:lib`, and `:app` uses `implementation project(":lib")`. A custom-build source
template is an app-only Gradle project, so `android_source.zip` carries the
three AARs built from this same in-tree source under:

```text
libs/debug/foundry-debug.aar
libs/dev/foundry-dev.aar
libs/release/foundry-release.aar
```

Those AARs are an export format, not a separate source owner.

## Runtime identities

The implementation namespace and JNI namespace are
`games.cafecito.foundry`. The export-template implementation package and
default application ID are `games.cafecito.foundry.game`; a project may replace
only the application ID. Runtime manifest metadata records the Foundry library
version, engine version, 40-character engine revision, and JNI contract
version. `BuildConfig` exposes the same values to JVM and instrumented tests.

The host does not scan application manifests for Android plugin initializer
classes and does not register Java methods or signals through reflection.
Native extensions use the FoundryExtension loading path. Source-template
projects may still include explicit project-local JAR or AAR dependencies from
`res://addons`; those artifacts are normal build inputs, not dynamically
discovered host plugins.

## Optional Foundry-Java extensions

Foundry-Java is an explicit, opt-in Gradle export path. Ordinary exports remain
unchanged: with `gradle_build/foundry_java/enabled` disabled, the exporter
passes no Foundry-Java properties, resolves no plugin or artifact, and produces
no Foundry-Java task or generated output. Enabling it requires
`gradle_build/use_gradle_build` and exactly one plugin source:

- `gradle_build/foundry_java/gradle_plugin_maven`: one exact
  `group:artifact:version` coordinate;
- `gradle_build/foundry_java/gradle_plugin_local`: one regular local plugin
  JAR with no user-controlled symbolic-link path component.

At least one application input is also required. Exact Maven inputs use
`gradle_build/foundry_java/maven_repositories` and
`gradle_build/foundry_java/maven_artifacts`; offline archives use
`gradle_build/foundry_java/local_artifacts`. Local plugin and application paths
are globalized and lexically simplified once, and the same normalized path is
used for symlink validation and opening. They must have no user-controlled
symbolic-link component in that normalized path. On macOS, the standard
OS-owned `/etc`, `/tmp`, and `/var` aliases are accepted only when they are
symlinks to the exact `private/etc`, `private/tmp`, and `private/var` targets;
every component below the alias and the final file remains subject to the
symlink check. Maven and local application inputs may be combined, but Maven
and local plugin sources may not. For example:

```text
gradle_build/foundry_java/gradle_plugin_maven =
  games.cafecito.foundry.java:games.cafecito.foundry.java.gradle.plugin:0.1.0
gradle_build/foundry_java/maven_repositories =
  ["https://repo.maven.apache.org/maven2"]
gradle_build/foundry_java/maven_artifacts =
  ["games.cafecito.foundry:foundry-java-android:0.1.0",
   "com.example:my-foundry-extension:1.0.0"]
```

Repository URLs must use HTTPS or an absolute local `file:///` URL and ASCII URI
syntax. Non-ASCII characters must be percent-encoded. Malformed percent escapes
and unsupported raw URI characters are rejected before Gradle runs. Plain HTTP,
remote file authorities, embedded credentials, queries, and fragments are also
rejected. Repository values are redacted from verbose export command logging
and from captured Gradle output before it is displayed or retained by the
exporter.

An offline build instead sets `gradle_plugin_local` to the exact
Foundry-Java plugin JAR and lists the binding AAR, runtime JAR, and extension
module JARs in `local_artifacts`. Local archives are external export inputs;
they are never embedded in `android_source.zip`. The exporter inspects nested
JAR, AAR, and ZIP entries recursively and rejects `libfoundry_android.so` at
any depth. Inspection fails closed at 8 nested levels, 64 root-inclusive
archives, 65,534 entries per archive, 128 MiB per decompressed entry, or
512 MiB of aggregate declared decompressed content.

The exporter passes the fixed `registry-index-v2` marker and Gradle applies
only plugin ID `games.cafecito.foundry.java`. The plugin owns descriptor,
binding-payload, provenance, and requested-ABI validation. A zero descriptor
opt-in is rejected and leaves no generated outputs. A valid build produces
exactly:

```text
assets/FoundryJava.foundryextension
assets/foundry_java/registry-index-v2.txt
lib/<requested-abi>/libfoundry_java.so
```

After Gradle copies the final APK or AAB, the exporter validates its archive
structure and streams the configuration, registry index, and each requested ABI
bridge through miniz so their declared size and CRC are verified before export
success. Required payload inspection is limited to 128 MiB per entry and
512 MiB in aggregate. Unrequested bridge names are still collected for the ABI
set diagnostic, but their payloads are not read.

Byte integrity is not loadability, so the packaged descriptor is also parsed as
part of the same inspection. `assets/FoundryJava.foundryextension` is rejected,
naming the entry and the exact violated requirement, unless it parses as a
configuration file and declares:

- a non-empty `configuration/entry_symbol`,
- a `configuration/compatibility_minimum` that is at least `0.1.0` and not newer
  than the exporting engine,
- a `configuration/compatibility_maximum`, when present, that is not older than
  the exporting engine, and
- a `[libraries]` entry that resolves under the `android.<arch>` feature tag of
  every requested ABI, and no `[libraries]` key that names no library at all. A
  device reports more feature tags than an export can enumerate, so any key can
  turn out to be the loader's most specific match and shadow a populated one.

That is the runtime extension loader's mandatory contract, enforced by the export
that produces the binding instead of by the device that runs it. Because the
descriptor is buffered to be parsed, it carries its own 64 KiB decompressed limit
and a larger payload fails inspection without being read.

The existing architecture selection remains authoritative:

| Foundry SCons architecture | Android ABI |
| --- | --- |
| `arm32` | `armeabi-v7a` |
| `arm64` | `arm64-v8a` |
| `x86_32` | `x86` |
| `x86_64` | `x86_64` |

Foundry continues to own and package its ordinary host
`libfoundry_android.so`; the binding AAR may not contain that host library.

### Loading the binding at runtime

Packaging the binding is not the same as running it. `assets/FoundryJava.foundryextension`
is injected into the APK by the Gradle plugin at build time, so it is never a
project file and never appears in `res://.foundry/extension_list.cfg`, which is the
only list the ordinary extension loader reads. The Android platform-extension
seam is what closes that gap.

The exported application loads the binding extension at startup when, and only
when, the fixed asset `FoundryJava.foundryextension` is present. Discovery is a
single fixed-path check: `getFoundryExtensionConfigFiles()` reports exactly
`res://FoundryJava.foundryextension` if that one asset opens, and nothing
otherwise. Only a missing asset means "no binding"; a broken `AssetManager`
propagates rather than being reported as an application that ships no binding.
There is no manifest scanning, no class scanning, no asset enumeration, and no
reflection.

Startup order:

```text
FoundryJavaStartupProvider (pre-Activity, primes the bridge)
  -> FoundryLib.initialize()
  -> engine CORE init
  -> load_platform_foundry_extensions()
  -> foundry_java_library_init
  -> level-ordered registration
```

A failed load is reported with the stable token
`FOUNDRY_JAVA_PLATFORM_EXTENSION_LOAD_FAILED` and the offending path, and startup
continues rather than aborting, so the device gate observes a diagnosable log
instead of a dead process. That token is a fatal runtime signature for
`android_device_acceptance.py`, so the gate fails immediately with the real cause.

## Android and toolchain levels

The root app configuration is authoritative for both `:app` and `:lib`:

- Android compile and target API: 36;
- minimum API: 24;
- Android build tools: 36.1.0;
- NDK: 29.0.14206865;
- Java and Kotlin bytecode: 17.

API 36 compatibility includes the predictive-back behavior already used by
the app, `InputTransferToken` selection guarded by
`Build.VERSION_CODES.BAKLAVA`, an immutable downloader alarm
`PendingIntent`, and explicit lint annotations on version- or
permission-sensitive compatibility helpers.

Set `ANDROID_HOME` or `ANDROID_SDK_ROOT` to an SDK containing those components
before invoking Gradle.

## Native matrix

Debug, dev, and release AARs and APKs require all four Android ABI payloads:

| Android ABI | SCons `arch` |
| --- | --- |
| `armeabi-v7a` | `arm32` |
| `arm64-v8a` | `arm64` |
| `x86` | `x86_32` |
| `x86_64` | `x86_64` |

| Runtime | Target | Required SCons flags |
| --- | --- | --- |
| debug | `template_debug` | `production=no dev_mode=no dev_build=no debug_symbols=no` |
| dev | `template_debug` | `production=no dev_mode=yes dev_build=yes debug_symbols=yes` |
| release | `template_release` | `production=yes dev_mode=no dev_build=no debug_symbols=no` |

Each cell also uses `platform=android tests=no swappy=yes`. The internal Gradle
module can schedule the matrix directly:

```sh
cd platform/android/java
./gradlew --no-daemon \
  :lib:assembleTemplateDebug \
  :lib:assembleTemplateDev \
  :lib:assembleTemplateRelease \
  -PselectedAbis=arm32,arm64,x86_32,x86_64
```

Gradle stages JNI inputs into a fresh path scoped by engine revision, native
input identity, and ABI selection. The stage is replaced as a whole, so a
full-matrix build followed by a development subset cannot retain stale ABIs or
mix payload provenance. Direct `:lib` development tasks may pass an ABI subset;
`generateFoundryTemplates` and `generateFoundryMonoTemplates` require all four
ABIs and use all four by default.

The staging tool accepts only lexical outputs contained by its dedicated
`android-native-stage` root. Root and output ownership markers, symlink checks,
and transactional backup/restore prevent an unsafe or failed replacement from
deleting an arbitrary directory.

### Foundry-owned native inputs

`platform/android/android_native_contract.py` validates the exact 12-cell
debug/dev/release by four-ABI matrix before Gradle consumes a prebuilt native
root. Validation covers canonical provenance, the current Foundry revision and
tree, library hashes and sizes, ELF class and machine, required external JNI
exports, rejection of stale JNI identities, and exact agreement between every
`libfoundry_android.so` and the native declarations derived from the compiled
Java/Kotlin classes. The expected tree is resolved independently as
`<revision>^{tree}` from the checked-out Foundry repository; input provenance
never supplies its own expected identity.

CI passes `-PfoundryNativeRoot=<root>` after downloading the cells built by the
same Foundry workflow. With no property, the in-tree library schedules local
SCons cells and validates their current-revision provenance and artifacts before
staging. Both modes stage into a fresh revision/input/ABI-scoped path; there is
no source-repository resolver or native archive handoff.

For example, the current workflow contract is:

```sh
cd platform/android/java
./gradlew --no-daemon generateFoundryTemplates \
  -PfoundryNativeRoot=/scratch/android-native
```

## JVM, lint, AIDL, and instrumented tests

Run the focused host verification from the repository root:

```sh
platform/android/java/gradlew -p platform/android/java --no-daemon \
  :lib:testTemplateDebugUnitTest \
  :lib:lintTemplateDebug \
  :lib:compileTemplateDebugJavaWithJavac \
  :lib:compileTemplateDebugKotlin \
  :lib:compileTemplateDebugAidl \
  :lib:assembleTemplateDebugAndroidTest \
  -PselectedAbis=
```

Lint is abort-on-error. The JVM unit suite covers runtime identity, canonical
types, and command-line parsing. The retained library Android test APK covers
runtime identity and canonical types. The application instrumented suite uses
an explicit JavaClassWrapper test bridge for Foundry Script interop and file
access, and separately covers runtime boot, launcher variants, command-line
arguments, back-press behavior, and engine termination.

The API 36 source-template smoke first uses an exact-checkout Linux editor to
export `app/src/instrumented/assets` as a compiled resource ZIP. The export
contains `.fsb` bytecode and remaps, and the device job replaces the source
template's raw assets with that ZIP before building either APK. Production
templates intentionally omit the Foundry Script front-end, and `.fsb` files
are guarded by the exact engine version and revision, so checked-in bytecode
or raw `.fs` sources are not valid substitutes. The focused host assertion
observes `RunStatus.STARTED` directly, while the compiled script bridge remains
available to the retained JavaClassWrapper, file-access, and quit tests. If
Gradle or instrumentation fails, `android_device_acceptance.py` captures
logcat before uninstalling the application and test packages.

Compile the application and its own instrumented suite with:

```sh
platform/android/java/gradlew -p platform/android/java --no-daemon \
  :app:compileStandardDebugJavaWithJavac \
  :app:compileStandardDebugKotlin \
  :app:compileInstrumentedDebugAndroidTestKotlin \
  :app:assembleInstrumentedDebugAndroidTest \
  -PselectedAbis=
```

## Build export templates

With all native cells present, build the stable template artifacts:

```sh
cd platform/android/java
./gradlew --no-daemon generateFoundryTemplates \
  -PselectedAbis=arm32,arm64,x86_32,x86_64
```

The result is:

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
promoted only after its AAR and application-template contract passes. Inspect
it independently with:

```sh
python3 platform/android/android_source_template.py inspect \
  --archive bin/android_source.zip
```

The inspector rejects missing, empty, extra, or legacy AARs; unsafe paths;
symbolic links; and host Java/Kotlin/AIDL source outside the application
package.

## Compiled JNI and artifact verification

`android_jni_contract.py` runs `javap` over every Foundry class in the compiled
classes JAR and derives exact JNI names from the
compiled Java/Kotlin native declarations using the JNI mangling rules:

```sh
python3 platform/android/android_jni_contract.py \
  --classes-jar platform/android/java/lib/build/intermediates/compile_library_classes_jar/templateDebug/bundleLibCompileToJarTemplateDebug/classes.jar
```

Every Gradle staging task depends on the corresponding compiled classes JAR.
`android_native_contract.py` reads each ELF dynamic symbol table directly and
rejects missing declared exports and undeclared extra exports in every native
cell. CI and release therefore compare all 12 cells with compiled declarations
before packaging. Inspect the three AARs and APKs with `jar tf`, `unzip -l`,
and `apkanalyzer`; require:

- `games/cafecito/foundry/Foundry.class`, `FoundryLib.class`, and host support
  classes;
- the Foundry layouts, strings, provider paths, mipmaps, license, and notice;
- the two licensing AIDL-generated interfaces;
- both `libfoundry_android.so` and `libc++_shared.so`;
- `armeabi-v7a`, `arm64-v8a`, `x86`, and `x86_64`;
- the requested application ID and fixed Foundry implementation classes.

## Host JNI keep rules

`android_jni_contract.py` covers Java-to-native calls. The reverse direction —
Java members the native host resolves reflectively with `GetMethodID`,
`GetStaticMethodID`, `GetFieldID`, or `GetStaticFieldID` — has no compiled Java
reference keeping it alive, so R8 is free to rename, inline, staticize, or drop
those members in minified release builds. `android_host_contract.py` derives that
surface from the native call sites and generates
`platform/android/java/lib/proguard-rules.pro`:

```sh
python3 platform/android/android_host_contract.py --emit-keep-rules
python3 platform/android/android_host_contract.py --check
```

The library ships the generated file through `consumerProguardFiles`, so both
project-dependency builds and exported app-only projects consuming the packaged
AARs inherit it. Rules are emitted without `allowoptimization`, because inlining
or staticizing a member breaks `GetMethodID` exactly as renaming does.

The contract fails closed. A lookup on a `jclass` obtained from `GetObjectClass`
must declare its type in `INSTANCE_CLASS_BY_HANDLE`, a lookup that resolves a
framework member on a polymorphic handle must appear in
`FRAMEWORK_DISPATCH_MEMBERS`, and a new runtime-named lookup must be added to
`DYNAMIC_LOOKUP_SITES`. `tests/python_build/test_android_host_contract.py` proves
the checked-in rules match the derived contract and that every native source
performing lookups is scanned; `:lib:verifyFoundryHostKeepRules` repeats the
check on every Android build.

To prove built artifacts kept the surface, without a device:

```sh
python3 platform/android/android_device_acceptance.py inspect-host-contract \
  --apk .test_scratch/foundry-java-command-first/foundry-java-custom-release.apk \
  --apkanalyzer "${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin/apkanalyzer"
```

This runs in seconds against any already-built APK and needs neither the native
matrix, template assembly, nor an emulator. `verify-apks` runs the same check
before installing, so a minified-away member fails before the device work starts.

## Device or emulator acceptance

Use an API 36 device or emulator whose ABI is one of the four supported ABIs.
Java 17, `adb`, `apkanalyzer`, and a freshly generated
`bin/android_source.zip` are required.

The source-template acceptance mode builds, instruments, installs, and starts
both the canonical and custom application-ID scenarios:

```sh
python3 platform/android/android_device_acceptance.py source-template \
  --source-template bin/android_source.zip \
  --compiled-assets .test_scratch/android-instrumented-assets/android-instrumented-assets.zip \
  --work-dir .test_scratch/android-device-acceptance \
  --evidence-dir .test_scratch/android-device-evidence \
  --adb "${ANDROID_SDK_ROOT}/platform-tools/adb" \
  --apkanalyzer "${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin/apkanalyzer"
```

The canonical scenario omits the package override and must produce
`games.cafecito.foundry.game`. The custom scenario uses
`dev.example.foundryacceptance`. Both require the named JUnit result, a
successful activity start, one live process, and logs free of linkage,
class-loading, native-library, and fatal-exception failures.

To verify APKs produced through the real command-first editor exporter:

These APKs have already passed structural APK validation in the editor and the
command-first producer. This command is intentionally limited to
device-runtime behavior.

```sh
python3 platform/android/android_device_acceptance.py verify-apks \
  --apk games.cafecito.foundry.game=.test_scratch/foundry-java-command-first/foundry-java-default-debug.apk \
  --apk dev.example.foundryjava=.test_scratch/foundry-java-command-first/foundry-java-custom-release.apk \
  --required-runtime-marker FOUNDRY_JAVA_EXPORT_ACCEPTANCE_READY \
  --evidence-dir .test_scratch/foundry-java-command-first-device-evidence \
  --adb "${ANDROID_SDK_ROOT}/platform-tools/adb" \
  --apkanalyzer "${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin/apkanalyzer"
```

## Acceptance evidence map

| Completion criterion | Authoritative proof |
| --- | --- |
| Runtime ownership | Python contracts require `:lib`, its source trees, and the internal `:app` dependency while rejecting external preparation/publication. |
| Java/Kotlin/AIDL | Focused compile tasks and `:lib:testTemplateDebugUnitTest`. |
| Lint | `:lib:lintTemplateDebug` with abort-on-error. |
| Instrumented tests | Both retained Android test APKs assemble; an API 36 device run supplies execution evidence. |
| Native build types and ABIs | Three AAR/APK variants contain both native libraries for all four ABI directories. |
| JNI declarations/exports | `javap` proves compiled declarations and `nm` proves matching exports in every native payload. |
| AAR identity/content | `jar tf` and `unzip -l` prove canonical classes, resources, metadata, notices, and native libraries. |
| APK identity/content | `apkanalyzer manifest application-id` proves canonical and custom IDs in built bytes. |
| Source ZIP | `android_source_template.py inspect` proves the three internal AAR payloads and safe app-only source. |
| Device runtime | `android_device_acceptance.py source-template` records JUnit, install, start, PID, and log evidence for both IDs. |
| Editor exporter | Command-first exports followed by `android_device_acceptance.py verify-apks`. |
| Engine-loaded runtime | `android_device_acceptance.py verify-apks` gates on `FOUNDRY_JAVA_EXPORT_ACCEPTANCE_READY`, which the acceptance app prints only after `DemoExtension` resolves through `ClassDB` and `callback_probe(41)` returns `42`. |
| Minified host JNI surface | `android_host_contract.py --check` proves the shipped keep rules match the native call sites; `android_device_acceptance.py inspect-host-contract` proves built APKs kept every member. |

No individual layer substitutes for another: compilation is not a device boot,
a green matrix is not JNI parity, and a direct Gradle APK is not proof of the
editor exporter path.
