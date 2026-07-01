# Android Library: Package Rename + Maven Central Release

Date: 2026-07-01
Status: Approved (design)

## Goal

Make the Foundry Android library buildable, correctly namespaced, and publishable
as a consumable Maven artifact:

1. Rename the Android JVM package `org.godotengine.godot` → `games.cafecito.foundry`
   (and editor module `org.godotengine.editor` → `games.cafecito.foundry.editor`),
   including the C++ JNI layer so the native library actually links against the
   renamed classes.
2. Publish the library to **Maven Central** under group `games.cafecito.foundry`,
   artifacts `foundry` / `foundry-debug` / `foundry-tools`, PGP-signed, on tagged
   releases.

## Background / Current State

- The fork already renamed the *classes* to `Foundry*` (`FoundryLib`, `FoundryActivity`,
  `FoundryFragment`, `FoundryIO`, `FoundryHost`, …) but left the *packages* as
  `org.godotengine.godot` (lib, ~82 source files) and `org.godotengine.editor` (editor).
- **The JNI layer is inconsistent and currently broken.** The C++ statically exports
  84 `Java_org_godotengine_godot_GodotLib_*` symbols (plus 4 `..._utils_*`), but there is
  **no `GodotLib` JVM class** — the actual class is `FoundryLib` with 47 `native` methods.
  There are no `RegisterNatives` calls. A native method on `FoundryLib` therefore resolves
  to `Java_org_godotengine_godot_FoundryLib_*`, which does not exist → `UnsatisfiedLinkError`.
  The Android platform has evidently not been exercised since the class rename. The rename
  work must reconcile this: C++ symbols must become `Java_games_cafecito_foundry_FoundryLib_*`
  and the `..._utils_*` symbols `Java_games_cafecito_foundry_utils_*`, matching the real classes.
- `FindClass` / dotted class-name string literals in C++ (`jni_utils.cpp` and others) reference
  `org/godotengine/godot/{Godot,Dictionary,variant/Callable}` and the dotted `org.godotengine.godot.*`
  forms used for Variant<->JNI type mapping.
- Maven publishing scaffolding exists (`scripts/publish-root.gradle`, `scripts/publish-module.gradle`,
  the `io.github.gradle-nexus.publish-plugin`) targeting Sonatype/OSSRH (group `org.godotengine`,
  artifacts `godot`/`godot-debug`/`godot-tools`, PGP-signed) but **is not wired into CI**.
- `release.yml`'s `build-android` job builds native libs and runs `generateGodotTemplates` /
  `generateGodotEditor`, then uploads the raw AAR/APK artifacts to the GitHub Release. There is
  **no Maven publish step**.
- Version comes from `version.py` (`0.1.0`, status `dev`) via `getGodotPublishVersion()` /
  `getGodotLibraryVersion()` in `app/config.gradle`.

## Decisions (locked)

| Topic | Decision |
| --- | --- |
| JVM/Android package (lib) | `org.godotengine.godot` → `games.cafecito.foundry` |
| JVM/Android package (editor) | `org.godotengine.editor` → `games.cafecito.foundry.editor` |
| JNI export symbols | `Java_org_godotengine_godot_*` → `Java_games_cafecito_foundry_*` (reconcile GodotLib→FoundryLib) |
| Distribution | Maven Central (Central Portal / OSSRH compatibility API) |
| Maven group | `games.cafecito.foundry` |
| Artifact ids | `foundry`, `foundry-debug`, `foundry-tools` |
| PGP signing | Keep |
| Publish trigger | On tagged release (`vX.Y.Z`), matching existing `release.yml` gating |

**No backwards compatibility required.** Foundry is a standalone engine with no other consumers,
so nothing must preserve old names for compatibility — AAR output file names, Gradle helper names,
and task names may be renamed freely toward the Foundry brand.

Out of scope: the app/demo module `applicationId` `com.godot.game` (not the published library),
third-party `com.google.android.vending.*` licensing/expansion code (leave untouched).

Prerequisite (account setup, outside this codebase): verify ownership of the `cafecito.games`
domain (namespace `games.cafecito`) on the Sonatype Central Portal, and provision the CI secrets
(`OSSRH_USERNAME`/token, `SIGNING_KEY*`, `OSSRH_GROUP_ID`, `SONATYPE_STAGING_PROFILE_ID`).

## Workstream A — Package Rename

### A1. JVM sources (lib + editor + app + tests)
- Move source trees:
  `lib/src/{main,test}/java/org/godotengine/godot/**` → `.../java/games/cafecito/foundry/**`;
  `editor/src/**/java/org/godotengine/editor/**` → `.../java/games/cafecito/foundry/editor/**`.
- Update every `package org.godotengine.godot…` / `package org.godotengine.editor…` declaration
  and every `import org.godotengine.…` across lib, editor, app, `nativeSrcsConfigs`, and instrumented tests.
- Update `namespace = "org.godotengine.godot"` → `"games.cafecito.foundry"` in `lib/build.gradle`
  and `"org.godotengine.editor"` → `"games.cafecito.foundry.editor"` in `editor/build.gradle`.
- Update `AndroidManifest.xml` files (lib, editor, app instrumented) for any fully-qualified references.
- Rename the AAR output file names `godot-lib.*.aar` → `foundry-lib.*.aar` in `lib/build.gradle`,
  and update the `generateGodotTemplates` template-copy logic and `release.yml` steps that reference
  those file names.
- Optional consistency cleanup (permitted, no compat constraint): rename the `getGodotLibraryVersion*`
  Gradle helpers and the `generateGodotTemplates`/`generateGodotEditor` task names to Foundry
  equivalents, updating their `release.yml` call sites. Keep this a separate, clearly-scoped step so
  it doesn't obscure the functional rename.

### A2. C++ JNI layer
- Rename all static export symbols: `Java_org_godotengine_godot_GodotLib_*` →
  `Java_games_cafecito_foundry_FoundryLib_*`; `Java_org_godotengine_godot_utils_*` →
  `Java_games_cafecito_foundry_utils_*`; and every other `Java_org_godotengine_godot_*` prefix
  (e.g. editor utils `EditorUtils`, `GameMenuUtils`) → `Java_games_cafecito_foundry_*`, preserving
  the class/method suffix so it matches the real JVM class names.
- Update `FindClass` / slash-form string literals `"org/godotengine/godot/…"` → `"games/cafecito/foundry/…"`
  and dotted `"org.godotengine.godot.…"` → `"games.cafecito.foundry.…"` in `jni_utils.cpp`
  (Dictionary, Callable, Godot, type-map table) and any other `.cpp/.h`.
- Files in scope (non-exhaustive): `java_foundry_lib_jni.{cpp,h}`, `java_foundry_wrapper.{cpp,h}`,
  `java_foundry_io_wrapper.{cpp,h}`, `java_foundry_view_wrapper.{cpp,h}`, `jni_utils.{cpp,h}`,
  `java_class_wrapper.cpp`, `dialog_utils_jni.{cpp,h}`, `os_android.cpp`, and editor-utils JNI files.

### A3. Export pipeline
- `export/export_plugin.cpp`, `export/gradle_export_util.{cpp,h}` reference `org.godotengine`
  when emitting the AndroidManifest / gradle files for *exported games*. Update these so exported
  projects reference the renamed library package, and verify a template export still produces a
  launchable APK.

### A4. Verification
- Build succeeds: `scons platform=android target=template_debug arch=arm64` (+ arm32) and
  `target=editor`, then `./gradlew generateGodotTemplates` / `generateGodotEditor`.
- Static check: `grep -r "org.godotengine\|org/godotengine\|org_godotengine" platform/android`
  returns only intentional leftovers (third-party vending code, unrelated `getGodotLibraryVersion`
  helper names) — no package/JNI references remain.
- Runtime: no `UnsatisfiedLinkError`; an exported/template app boots. Where the VM can't run a device,
  at minimum confirm the AAR contains the `.so` and the symbol table matches
  (`nm -D libfoundry*.so | grep Java_games_cafecito_foundry_FoundryLib_initialize`).

## Workstream B — Maven Central Publishing

- Keep the OSSRH/Central-Portal `nexusPublishing` config in `publish-root.gradle` (already points at
  `ossrh-staging-api.central.sonatype.com`). Group `games.cafecito.foundry` sourced from
  `ossrhGroupId` (env `OSSRH_GROUP_ID`) — set that to `games.cafecito.foundry`.
- In `lib/build.gradle`, change the artifact ids:
  `PUBLISH_ARTIFACT_ID = 'foundry'`, `DEBUG_PUBLISH_ARTIFACT_ID = 'foundry-debug'`,
  `TOOLS_PUBLISH_ARTIFACT_ID = 'foundry-tools'`.
- In `publish-module.gradle`, keep the three publications (templateDebug, templateRelease,
  toolsRelease). Update POM metadata to Foundry/Cafecito: `name`, `description`, `url`
  (`https://www.cafecito.games/`), `licenses` (MIT, point at the fork's LICENSE),
  `developers`, and `scm` (`github.com/cafecito-games/Foundry`).
- Keep PGP `signing` block as-is (keys from CI secrets / `local.properties`).
- Version = `PUBLISH_VERSION` (`getGodotPublishVersion()`), already release-aware.

## Workstream C — CI Release Wiring

- Extend `release.yml`'s `build-android` job (or a dedicated `publish-android-library` job gated on
  the release/tag) to, after building native libs + generating templates/editor, run the Gradle
  publish tasks that push the three publications to the Central Portal staging and close/release them
  (`publishToSonatype` + `closeAndReleaseSonatypeStagingRepository`, per the nexus-publish plugin).
- The AAR variants pull native `.so` from the `libs/{release,dev,debug,tools/*}` dirs; ensure the
  publish step runs after the templates/editor generation that populates those dirs (or wire the
  publish task's dependency accordingly), so published AARs contain the prebuilt libraries.
- Gate on published (non-draft) tagged releases only; skip on PRs/drafts. Provide secrets:
  `OSSRH_USERNAME`, `OSSRH_PASSWORD`, `OSSRH_GROUP_ID=games.cafecito.foundry`,
  `SONATYPE_STAGING_PROFILE_ID`, `SIGNING_KEY_ID`, `SIGNING_KEY`, `SIGNING_PASSWORD`.
- Document consumer usage (README/docs): `mavenCentral()` + `implementation "games.cafecito.foundry:foundry:<version>"`.

## Risks / Open Items

- **Latent JNI breakage (highest risk).** Because the C++/JVM class names are currently mismatched,
  the rename is also the fix; expect to touch every JNI TU and to need a real link/run check, not just
  a compile. Budget for reconciling any method-name drift between `FoundryLib`'s 47 natives and the 84
  C++ `GodotLib` symbols (some may be stale/removed or renamed methods).
- Domain verification + secret provisioning on the Central Portal must be done before CI can publish;
  the code can be merged and dry-run (`publishToMavenLocal`) before the account side is ready.
- Reflection / string-based class lookups (plugin loading, `Class.forName`, manifest `<meta-data>`
  plugin class names) may hardcode the old package — grep for string forms, not just symbols.

## Testing Strategy

- C++/gradle build of both `template_*` and `editor` targets for arm64 (+ arm32 for templates).
- Static grep gate (A4) in CI or pre-merge.
- Symbol-table check on the built `.so`.
- `./gradlew publishToMavenLocal` produces correctly-named, signed artifacts with valid POMs under
  `~/.m2/repository/games/cafecito/foundry/`.
- Where a device/emulator is available: template export boots without `UnsatisfiedLinkError`;
  otherwise gate that as a manual pre-release check.
