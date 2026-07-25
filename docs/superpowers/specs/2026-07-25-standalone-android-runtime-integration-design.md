# Standalone Android Runtime Integration Design

## Context

Foundry currently owns two Android products in one Gradle build:

- `platform/android/java/app` is the exported-game application template and remains in this repository.
- `platform/android/java/lib` is the Java/Kotlin/AIDL runtime library. It now has an authoritative standalone home in
  [`cafecito-games/Foundry-Android`](https://github.com/cafecito-games/Foundry-Android) and must be removed here.

The native Android engine and JNI implementation remain in Foundry. SCons currently moves each native build into
`platform/android/java/lib/libs/<build-type>/<abi>`, and the in-tree `:lib` project packages those files into AARs.
That path creates an implicit coupling between SCons, a relative engine checkout, `version.py`, and the duplicate
runtime source.

The standalone project instead accepts one explicit, validated native bundle. Its checked-in compatibility document
currently identifies Foundry revision `3f1054e65f942375cb7f42f299505220fca990fd`. Reusing that document while
packaging native libraries built from a later Foundry revision would be false provenance. This integration must derive
compatibility metadata from the exact clean Foundry revision that produced the native matrix.

## Approaches Considered

### 1. Consume only published Maven artifacts

Foundry could depend on fixed Maven coordinates and stop building the runtime. This is attractive after stable
releases exist, but the current standalone version is a development snapshot, and a Maven-only workflow cannot package
native engine binaries built from the exact Foundry source revision under review without first publishing another
immutable artifact. It also makes local engine development depend on external publication timing.

### 2. Add the standalone repository as a Git submodule

A submodule would make the source revision visible in the tree. It would also introduce submodule initialization into
every source archive, contributor checkout, CI job, and release path. Foundry does not otherwise use a submodule for
this build surface, and a submodule alone does not solve truthful per-build compatibility metadata or explicit native
artifact provenance.

### 3. Export an exact pinned Git object into ignored build scratch

This is the selected design. Foundry tracks a small canonical pin manifest, exports the exact standalone Git object
into scratch, injects a freshly derived compatibility document, invokes the standalone project's own validators and
Gradle build, and passes the resulting AARs explicitly to `:app`. No standalone runtime source is tracked in Foundry,
and no verifier implementation is copied.

## Ownership Boundary

Foundry continues to own:

- `platform/android` C++ code and `Java_games_cafecito_foundry_*` JNI implementations;
- SCons native builds and `libfoundry_android.so` production;
- the exported-game `:app` Gradle module and source template;
- Android exporter code and prebuilt export-template packaging;
- `nativeSrcsConfigs`, which remains an IDE-only view of Foundry-owned native code;
- orchestration that joins a native matrix to an exact standalone source revision.

Foundry-Android owns:

- runtime Java, Kotlin, AIDL, resources, and manifest content;
- runtime Gradle build configuration and AAR publication;
- native-bundle validation, JNI declaration/export comparison, and AAR/publication inspection;
- compatibility metadata schema and embedding.

After migration, Foundry contains no tracked `platform/android/java/lib`, no `:lib` Gradle project, and no Foundry-side
Maven publication scripts. `:app` consumes generated standalone AAR files only.

## Canonical Pin

`platform/android/foundry_android_runtime.json` is canonical JSON with:

- schema version;
- repository URL `https://github.com/cafecito-games/Foundry-Android.git`;
- source commit `b8c46c807d467fcd1667b7d4cb04d07a09a08860`;
- source tree `f22bec563cbb992e770c89688a2c761855102f82`;
- bindings version `0.1.0-dev-SNAPSHOT`;
- JNI contract version `1`;
- engine compatibility policy `exact-native-source-revision`;
- paths to the compatibility generator, native-bundle tool, JNI verifier, Gradle runtime module, and expected direct
  AAR outputs in the pinned tree.

The driver validates the document's canonical encoding and schema. It resolves the commit in the explicitly supplied
Git repository or an explicitly enabled exact-commit fetch, verifies the commit's tree ID, exports that object with
`git archive`, and then validates the pinned source's checked-in bindings version, JNI contract, and required tool
paths before modifying scratch metadata. A branch name, moving tag, working-tree contents, or sibling checkout is never
used implicitly.

Updating the standalone source is a deliberate pin-manifest change. The update procedure records the new commit and
tree, reviews the standalone compatibility/JNI contract change, rebuilds all native cells, and runs the complete gate.

## Native Output and Provenance

SCons no longer writes under `platform/android/java/lib/libs`. Each Android build writes one cell under:

```text
bin/android-native/<engine-revision>/<build-type>/<abi>/
```

Each cell contains exactly:

```text
libfoundry_android.so
libc++_shared.so
provenance.json
```

`provenance.json` is canonical JSON generated after both libraries exist. It records:

- schema version;
- exact Foundry commit and Git tree;
- whether tracked source was dirty;
- build type and Android/SCons ABI;
- the target, production, development, debug-symbol, test, and Swappy inputs that define the build type;
- relative path, size, and SHA-256 for both libraries.

The three build configurations are exactly:

| Build type | SCons target | Required defining inputs |
| --- | --- | --- |
| `debug` | `template_debug` | `production=no dev_mode=no dev_build=no debug_symbols=no tests=no` |
| `dev` | `template_debug` | `production=no dev_mode=yes dev_build=yes debug_symbols=yes tests=no` |
| `release` | `template_release` | `production=yes dev_mode=no dev_build=no debug_symbols=no tests=no` |

All builds set `platform=android`, one of `arm32`, `arm64`, `x86_32`, or `x86_64`, and `swappy=yes`. The ABI mapping is
`arm32 → armeabi-v7a`, `arm64 → arm64-v8a`, `x86_32 → x86`, and `x86_64 → x86_64`.

Bundle assembly requires exactly twelve cells: three build types by four ABIs. It rejects a dirty or unknown source,
mixed revisions or trees, wrong build flags, wrong ABI mappings, missing or extra cells/files, noncanonical provenance,
and size/hash drift. Only after that validation does it stage the two libraries per cell in scratch and invoke the
pinned standalone `tools/native_bundle.py create`. The standalone tool remains authoritative for ELF, JNI, matrix,
archive-safety, manifest, and deterministic-bundle validation.

## Compatibility and Build Flow

The authoritative build flow is:

1. Validate the Foundry pin manifest.
2. Resolve and export the exact standalone commit into an ignored or shared scratch directory.
3. Validate the exported standalone tree and its checked-in bindings/JNI contract.
4. Validate the exact twelve-cell native matrix and its Foundry provenance.
5. Invoke the exported standalone `tools/sync_engine_pin.py derive` against the exact clean Foundry checkout, requiring
   the native provenance revision and Git tree.
6. Write the derived compatibility document only into the exported scratch tree. Its engine revision therefore names
   the native-producing Foundry commit, while its bindings version and JNI contract come from the tracked standalone
   pin.
7. Create or validate the explicit native bundle with the exported standalone tool.
8. Run the exported standalone Gradle wrapper with that bundle to assemble debug, dev, and release AARs and execute its
   JNI contract verification.
9. Copy the three direct AAR outputs into an ignored, explicit output directory for the Foundry `:app` build.
10. Assemble debug, dev, and release application templates and generate `android_source.zip`.

Every generated compatibility document, source export, native staging tree, bundle, and AAR is written under the
caller-supplied shared scratch directory or a Git-ignored Gradle/SCons build directory. No generated file is written to
a tracked source directory or the repository root.

The driver accepts:

- an explicit pre-fetched standalone Git repository, for offline source resolution;
- an explicit prebuilt native bundle, for fast/offline reuse;
- or an explicit native-cell root, to create the bundle.

Network access is disabled by default. An opt-in fetch flag may fetch only the repository and exact commit from the pin
manifest into the supplied scratch directory. Missing source, bundle, native cells, or opt-in fetch produces an
actionable diagnostic showing the exact property or command to provide. The driver never searches a parent or sibling
directory.

A prebuilt bundle is still validated against freshly derived compatibility metadata, so a bundle for revision
`3f1054e...` cannot be consumed by a later Foundry revision. A pre-fetched source is exported by pinned commit rather
than copied from its working tree, so local edits or a different checked-out branch cannot alter the runtime build.

## Gradle and Exporter Integration

The Foundry Gradle root keeps `:app` and `:nativeSrcsConfigs` and removes `:lib`. It also removes Nexus/Maven
publication configuration and all `version.py` readers that existed only for the in-tree library.

`generateFoundryTemplates` requires explicit runtime preparation properties and depends on one preparation task before
assembling application variants. That task invokes the Python driver and produces variant-specific AAR directories
under the Gradle build directory. `:app` declares debug, dev, and release file dependencies from those directories.
The source-template ZIP continues to include `app`, the Gradle wrapper, and the prepared AARs, so an installed custom
Android build remains self-contained.

The established release artifact names remain unchanged:

- `android_debug.apk`
- `android_dev.apk`
- `android_release.apk`
- `android_source.zip`

The standalone direct AAR names are preserved inside `app/libs` and may also be exposed in `bin` for diagnostics, but
Foundry no longer publishes them to Maven. The Android exporter continues to consume the same APK and source-template
names, while contract tests inspect the source ZIP to prove it contains the standalone AARs and no runtime source copy.

The legacy SCons `generate_android_binaries=yes` hook forwards explicit source/bundle/native-root properties to the
same Gradle preparation flow. It cannot silently package the single cell just built as a release-ready template.
Without a complete bundle or matrix it fails with the same actionable diagnostic.

## CI and Release

The authoritative Android workflow builds the full twelve-cell matrix in parallel. Each job uploads one cell including
provenance. A dependent job downloads all cells, validates and assembles the bundle, checks out the exact pinned
standalone commit explicitly, and runs `generateFoundryTemplates` for all three application build types. It uploads the
unchanged APK/source artifact set plus the validated bundle and direct AARs as diagnostics.

Pull-request CI runs:

- pin/source/workflow/static contract tests;
- all negative provenance and compatibility tests;
- the full twelve-cell matrix and standalone JNI/AAR verification for Android-affecting changes;
- template generation and source-ZIP inspection.

The Foundry release workflow uses the same full matrix and runtime preparation path. It removes the old
`:lib:publish*` Maven step because Foundry-Android is the sole Maven publisher. It packages the established Android
template names into Foundry release assets only after full compatibility, JNI, AAR, and template verification.

A narrower local build may assemble a non-authoritative application variant from an explicit prebuilt full bundle, but
it is labeled as a developer workflow and cannot emit release-ready template artifacts unless the complete gate ran.

## Failure Handling

All boundaries fail closed:

- Pin JSON is missing, noncanonical, malformed, or names a different source tree: stop before export.
- The exact standalone commit is unavailable and fetch was not explicitly enabled: explain how to supply a repository
  or enable the exact fetch.
- Foundry source is dirty, unknown, or differs from native provenance: stop before compatibility generation.
- A provenance record is missing, noncanonical, dirty, mixed, mislabeled, has wrong flags, or does not match library
  bytes: stop before invoking the standalone bundle tool.
- A supplied bundle embeds any different revision, version, contract, matrix, hash, ELF identity, or JNI surface: the
  standalone validator rejects it.
- Any standalone Gradle/JNI/AAR verification fails: do not copy AARs into the application inputs.
- Any required app variant or output is absent: do not create a source ZIP or release artifact.

Temporary outputs are created in a staging directory and promoted only after their stage succeeds, preventing a failed
run from being mistaken for current output.

## Testing

Tests are written first and cover:

- canonical pin parsing, exact commit/tree resolution, source export, and missing/offline/fetch diagnostics;
- rejection of wrong standalone commit/tree, bindings version, JNI contract, and missing required tools;
- twelve-cell matrix enumeration and exact SCons build-type/ABI mapping;
- canonical native provenance creation and rejection of dirty, unknown, mixed, missing, extra, mislabeled, or
  hash-drifted cells;
- derived compatibility naming the exact clean native-producing Foundry revision rather than the standalone baseline
  revision;
- rejection of the existing `3f1054e...` standalone bundle when the current Foundry revision differs;
- explicit pre-fetched-source and prebuilt-bundle flows;
- Gradle configuration requiring prepared AARs and the removal of `:lib`, relative `version.py`, and
  `java/lib/libs` dependencies;
- template APK/source ZIP contents, stable output names, all build types, and all ABIs;
- CI/release workflow contracts for the full matrix, compatibility gate, no Foundry Maven publication, and artifact
  handoff;
- absence of the tracked in-tree runtime after all consumers migrate.

Verification includes focused Python tests, Gradle configuration/tasks, standalone JNI and artifact inspection,
Android debug/dev/release template assembly across all four ABIs, the strict Foundry build, relevant focused engine
tests, and the full Foundry suite. Device or emulator behavior is not claimed here; epic child #1224 owns final
exported-game and plugin smoke validation.

## Migration Order

1. Add pin, driver, provenance, and their tests while the old `:lib` still exists.
2. Redirect SCons outputs to versioned native cells and prove the complete bundle path.
3. Build standalone AARs from scratch and wire `:app` to their explicit output directories.
4. Update template generation, exporter artifact checks, CI, release, and documentation.
5. Only after all consumers use generated AARs, delete `platform/android/java/lib`, Foundry Maven publishing, and old
   runtime-specific Gradle/version helpers.
6. Run the complete clean-checkout gate and Cursor review.

This order keeps each transition testable and ensures the duplicate runtime is removed only after its last consumer has
been replaced.
