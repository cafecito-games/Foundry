# Android Java Foundry Class Rename Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rename Android Java/Kotlin binding API names from `Godot` to `Foundry` equivalents.

**Architecture:** Keep the existing Android runtime architecture intact while changing class identities, callback names, and annotation names. Update the JNI bridge to look up the renamed `games/cafecito/foundry/Foundry` class, and keep existing C++ wrapper class names that are already Foundry-branded.

**Tech Stack:** Kotlin, Java, Android Gradle, C++ JNI, ripgrep verification, SCons/Gradle Android build checks.

---

## File Structure

- Rename `platform/android/java/lib/src/main/java/games/cafecito/foundry/Godot.kt` to `Foundry.kt`; change the class, companion singleton type, self references, benchmark labels, and public callback references from `Godot` to `Foundry`.
- Rename `platform/android/java/lib/src/main/java/games/cafecito/foundry/FullScreenGodotApp.java` to `FullScreenFoundryApp.java`.
- Rename `platform/android/java/lib/src/main/java/games/cafecito/foundry/service/RemoteGodotFragment.kt` to `RemoteFoundryFragment.kt`.
- Rename `platform/android/java/lib/src/main/java/games/cafecito/foundry/plugin/UsedByGodot.java` to `UsedByFoundry.java`.
- Rename editor classes `BaseGodotEditor.kt`, `BaseGodotGame.kt`, and `EmbeddedGodotGame.kt` to `BaseFoundryEditor.kt`, `BaseFoundryGame.kt`, and `EmbeddedFoundryGame.kt`.
- Update Java/Kotlin consumers in Android app, editor, tests, plugin registry, plugin base class, host interface, render views, input, file/directory handling, and service code.
- Update Android manifest references and process names for renamed editor activities.
- Update C++ JNI references in `platform/android/java_foundry_wrapper.*`, `platform/android/jni_utils.cpp`, and any comments that directly name the renamed class files.
- Leave unrelated `com.godot.game` package names, copyright text, and broad upstream docs/comments alone.

### Task 1: Establish Red Verification

**Files:**
- Read: `platform/android/java`
- Read: `platform/android`

- [x] **Step 1: Run a failing stale-name verification**

Run:

```bash
rg -n "class Godot|FullScreenGodotApp|RemoteGodotFragment|BaseGodotEditor|BaseGodotGame|EmbeddedGodotGame|UsedByGodot|games/cafecito/foundry/Godot|games\\.cafecito\\.foundry\\.Godot" platform/android
```

Expected: the command exits `0` and prints current stale class/API references. This is the expected red state before implementation.

### Task 2: Rename Files And Declarations

**Files:**
- Rename: `platform/android/java/lib/src/main/java/games/cafecito/foundry/Godot.kt` -> `platform/android/java/lib/src/main/java/games/cafecito/foundry/Foundry.kt`
- Rename: `platform/android/java/lib/src/main/java/games/cafecito/foundry/FullScreenGodotApp.java` -> `platform/android/java/lib/src/main/java/games/cafecito/foundry/FullScreenFoundryApp.java`
- Rename: `platform/android/java/lib/src/main/java/games/cafecito/foundry/service/RemoteGodotFragment.kt` -> `platform/android/java/lib/src/main/java/games/cafecito/foundry/service/RemoteFoundryFragment.kt`
- Rename: `platform/android/java/lib/src/main/java/games/cafecito/foundry/plugin/UsedByGodot.java` -> `platform/android/java/lib/src/main/java/games/cafecito/foundry/plugin/UsedByFoundry.java`
- Rename: `platform/android/java/editor/src/main/java/games/cafecito/foundry/editor/BaseGodotEditor.kt` -> `platform/android/java/editor/src/main/java/games/cafecito/foundry/editor/BaseFoundryEditor.kt`
- Rename: `platform/android/java/editor/src/main/java/games/cafecito/foundry/editor/BaseGodotGame.kt` -> `platform/android/java/editor/src/main/java/games/cafecito/foundry/editor/BaseFoundryGame.kt`
- Rename: `platform/android/java/editor/src/main/java/games/cafecito/foundry/editor/embed/EmbeddedGodotGame.kt` -> `platform/android/java/editor/src/main/java/games/cafecito/foundry/editor/embed/EmbeddedFoundryGame.kt`

- [x] **Step 1: Rename files with `git mv`**

Run:

```bash
git mv platform/android/java/lib/src/main/java/games/cafecito/foundry/Godot.kt platform/android/java/lib/src/main/java/games/cafecito/foundry/Foundry.kt
git mv platform/android/java/lib/src/main/java/games/cafecito/foundry/FullScreenGodotApp.java platform/android/java/lib/src/main/java/games/cafecito/foundry/FullScreenFoundryApp.java
git mv platform/android/java/lib/src/main/java/games/cafecito/foundry/service/RemoteGodotFragment.kt platform/android/java/lib/src/main/java/games/cafecito/foundry/service/RemoteFoundryFragment.kt
git mv platform/android/java/lib/src/main/java/games/cafecito/foundry/plugin/UsedByGodot.java platform/android/java/lib/src/main/java/games/cafecito/foundry/plugin/UsedByFoundry.java
git mv platform/android/java/editor/src/main/java/games/cafecito/foundry/editor/BaseGodotEditor.kt platform/android/java/editor/src/main/java/games/cafecito/foundry/editor/BaseFoundryEditor.kt
git mv platform/android/java/editor/src/main/java/games/cafecito/foundry/editor/BaseGodotGame.kt platform/android/java/editor/src/main/java/games/cafecito/foundry/editor/BaseFoundryGame.kt
git mv platform/android/java/editor/src/main/java/games/cafecito/foundry/editor/embed/EmbeddedGodotGame.kt platform/android/java/editor/src/main/java/games/cafecito/foundry/editor/embed/EmbeddedFoundryGame.kt
```

- [x] **Step 2: Update declarations and references**

Use structured search plus scoped mechanical replacement for these exact symbol mappings:

```text
Godot -> Foundry
FullScreenGodotApp -> FullScreenFoundryApp
RemoteGodotFragment -> RemoteFoundryFragment
BaseGodotEditor -> BaseFoundryEditor
BaseGodotGame -> BaseFoundryGame
EmbeddedGodotGame -> EmbeddedFoundryGame
UsedByGodot -> UsedByFoundry
getGodot -> getFoundry
onGodotSetupCompleted -> onFoundrySetupCompleted
onGodotMainLoopStarted -> onFoundryMainLoopStarted
onGodotTerminating -> onFoundryTerminating
onGodotForceQuit -> onFoundryForceQuit
onGodotRestartRequested -> onFoundryRestartRequested
onNewGodotInstanceRequested -> onNewFoundryInstanceRequested
```

Apply replacements only under `platform/android/java`, plus matching JNI method lookup strings in `platform/android/java_foundry_wrapper.cpp`, method IDs in `platform/android/java_foundry_wrapper.h`, and the known class lookup in `platform/android/jni_utils.cpp`.

### Task 3: Fix Compile Issues From API Boundaries

**Files:**
- Modify as reported by compiler/search: Android Java/Kotlin files under `platform/android/java`
- Modify as reported by compiler/search: C++ files under `platform/android`

- [x] **Step 1: Run targeted stale-name verification**

Run:

```bash
rg -n "class Godot|FullScreenGodotApp|RemoteGodotFragment|BaseGodotEditor|BaseGodotGame|EmbeddedGodotGame|UsedByGodot|games/cafecito/foundry/Godot|games\\.cafecito\\.foundry\\.Godot" platform/android
```

Expected: no output and exit code `1`.

- [x] **Step 2: Run broader source search and inspect remaining hits**

Run:

```bash
rg -n "\\bGodot\\b|\\w+Godot\\w+|Godot\\w+" platform/android/java platform/android -g '*.java' -g '*.kt' -g '*.cpp' -g '*.h' -g '*.xml'
```

Expected: remaining hits are copyright text, unchanged external package paths, upstream descriptive comments, or intentionally out-of-scope docs. Any remaining direct references to renamed Android binding classes, callbacks, or imports must be fixed.

### Task 4: Verify Build Surface

**Files:**
- Read: `platform/android/java/settings.gradle`
- Read: `platform/android/java/build.gradle`

- [x] **Step 1: Run Android Java/Kotlin compile-oriented checks**

Run from `platform/android/java`:

```bash
./gradlew :lib:compileDebugKotlin :lib:compileDebugJavaWithJavac :editor:compileDebugKotlin :app:compileDebugJavaWithJavac
```

Expected: tasks complete successfully. If a task name differs in this Gradle setup, run `./gradlew tasks --all` and choose the nearest debug compile tasks for `lib`, `editor`, and `app`.

- [x] **Step 2: Run native Android build check when feasible**

Run from the repository root:

```bash
python3 -m SCons platform=android target=template_debug dev_build=yes cache_path="$HOME/.scons_cache" -j$(sysctl -n hw.logicalcpu 2>/dev/null || nproc)
```

Expected: the build reaches the Android native compile/link stage without JNI class-name errors. If Android SDK/NDK prerequisites are unavailable locally, record the failure and rely on the Gradle compile/search verification.

Result in this worktree: native Android SCons verification was attempted, but local SCons only exposes `ios`, `macos`, and `visionos` platforms, so `platform=android` is unavailable here.

### Task 5: Final Review

**Files:**
- Read: git diff

- [x] **Step 1: Review renamed files**

Run:

```bash
git diff --name-status
git diff -- platform/android/java platform/android/java_foundry_wrapper.cpp platform/android/java_foundry_wrapper.h platform/android/jni_utils.cpp
```

Expected: changes are limited to the Android rename surface and the committed spec/plan artifacts.

- [x] **Step 2: Commit implementation**

Run:

```bash
git add platform/android docs/superpowers/plans/2026-07-02-android-java-foundry-class-rename.md
git commit -m "Rename Android Java Godot classes to Foundry"
```

Expected: commit succeeds after hooks.
