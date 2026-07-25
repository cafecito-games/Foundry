# Remove the On-Device Android Editor

## Goal

Remove Foundry's editor application for Android, HorizonOS, and PicoOS, including
its editor-only Gradle, Java/Kotlin, JNI, publishing, documentation, and CI
surface, without changing exported Android games or the reusable Android runtime.

## Boundary

Remove the `platform/android/java/editor` application module and the `editor`
product flavor from `:lib`. Remove the associated editor APK/AAB generation,
`foundry-tools` publication, editor native-library routing, editor JNI helpers,
and CI/release artifacts. An Android SCons invocation with `target=editor` must
fail clearly because Android is now a template/runtime-only target.

The shared runtime library must no longer expose functionality that exists only
to host the removed editor. This includes the build-provider bridge, APK
signing/verification host callbacks, editor settings and project-metadata JNI
access, editor/project-manager hint JNI access, workspace callbacks, and
benchmark code that explicitly runs only for the `editorDev` variant. Desktop
Android exporting keeps using the Android SDK's Gradle and `apksigner` tools.

Preserve `:app`, the `template` variants of `:lib`, runtime services and remote
fragments, the `games.cafecito.foundry` host/plugin API that exported games use,
Android plugin discovery/loading, XR mode and OpenXR runtime support, all
template ABIs/build types, and the normal Android exporter.

## Consumer Evidence

- `BuildProvider`, editor settings/metadata, APK signing callbacks, workspace
  callbacks, and the editor utility classes are consumed only by
  `platform/android/java/editor` or native code guarded for an Android tools
  build.
- `BenchmarkUtils` checks for `BuildConfig.FLAVOR == "editor"` and
  `BuildConfig.BUILD_TYPE == "dev"` in every operation, so it has no template
  behavior.
- `FoundryService` and `RemoteFoundryFragment` implement a general hosted
  runtime surface and have no editor-module imports, so they remain.
- `FoundryHost.supportsFeature`, plugin feature callbacks, `XRMode`, and
  `Foundry.hasFeature` are used by runtime/plugin/OpenXR paths, so they remain.
- `org.godotengine.*` runtime metadata and export-template identifiers belong
  to #1222 and are not changed here.

## Verification

A source-contract test will assert both sides of the boundary: editor
module/flavor/tasks/publications/JNI/CI/docs are absent, while `:app`, template
AARs, runtime host/plugin/service/XR classes, and desktop exporter behavior are
present. The test is added before implementation and must fail against the
existing tree.

After implementation, verification covers the contract test, Gradle project and
task models, JVM unit tests, template AAR/app assembly, template generation,
native Android template compilation, and the repository's strict macOS
editor/test requirements. Artifact inspection will confirm the expected AAR/APK
contents. Device startup cannot be claimed without an attached Android device;
that environmental limit must be reported explicitly.

