# Android Java Foundry Class Rename Design

## Goal

Rename Android Java/Kotlin binding classes whose API names still contain `Godot` to `Foundry` equivalents, so the Android binding surface no longer exposes stale Godot-branded class names.

## Scope

The rename covers Android Java/Kotlin classes and annotations under `platform/android/java`, plus native Android JNI references that look up or document those classes. It includes:

- `games.cafecito.foundry.Godot` -> `games.cafecito.foundry.Foundry`
- `FullScreenGodotApp` -> `FullScreenFoundryApp`
- `RemoteGodotFragment` -> `RemoteFoundryFragment`
- `BaseGodotEditor` -> `BaseFoundryEditor`
- `BaseGodotGame` -> `BaseFoundryGame`
- `EmbeddedGodotGame` -> `EmbeddedFoundryGame`
- `UsedByGodot` -> `UsedByFoundry`

References to these renamed classes must be updated in Java, Kotlin, manifests, tests, and C++ JNI code. Method and callback names that are part of the Android binding API and contain `Godot` should also be renamed to `Foundry` equivalents, including host callbacks such as `getGodot`, `onGodotSetupCompleted`, `onGodotMainLoopStarted`, `onGodotForceQuit`, `onGodotRestartRequested`, and `onNewGodotInstanceRequested`.

Historical copyright text, package names outside the requested binding API such as `com.godot.game`, and general docs/comments that describe upstream Godot behavior are not part of this change unless they directly reference a renamed Android class.

## Architecture

The Android runtime singleton remains the central object that owns native setup, render views, plugin registration, input, file access, and lifecycle dispatch. Only the Java/Kotlin class identity changes from `Godot` to `Foundry`.

Native code that reflects into this singleton must be updated to resolve `games/cafecito/foundry/Foundry`. The existing C++ wrapper names such as `FoundryJavaWrapper` already match the fork branding and should stay in place.

Plugin construction and annotation discovery must use the renamed API. Plugin constructors should accept `Foundry`, `FoundryPlugin` should expose `getFoundry`, and the registry should look for methods annotated with `UsedByFoundry`.

## Compatibility

This is a breaking Java/Kotlin API rename. The change should not keep deprecated compatibility aliases for the old `Godot` class names or annotation names, because retaining those aliases would preserve the stale binding references the cleanup is intended to remove.

## Testing

Validation should include:

- A targeted Android Java/Kotlin compile task if available from Gradle.
- Targeted searches proving the renamed class declarations and old imports are gone.
- Native Android build validation when practical, because the JNI lookup path changes from `Godot` to `Foundry`.

The implementation should prefer narrow mechanical edits and avoid unrelated documentation or branding sweeps.
