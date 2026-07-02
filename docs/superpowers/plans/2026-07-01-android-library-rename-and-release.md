# Android Library Rename + Maven Central Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rename the Foundry Android library from package `org.godotengine.godot` (+ editor `org.godotengine.editor`) to `games.cafecito.foundry` (+ `games.cafecito.foundry.editor`) across JVM sources, the C++ JNI layer, and the export pipeline, then publish it to Maven Central as `games.cafecito.foundry:foundry` / `foundry-debug` / `foundry-tools`.

**Architecture:** The rename is mechanical but cross-language: JVM package moves (`git mv` + package/import rewrite), Gradle `namespace` changes, and — critically — matching C++ JNI static export symbols (`Java_<pkg>_<Class>_<method>`) and `FindClass` string literals, which are currently inconsistent (`GodotLib` symbols with no `GodotLib` class). Publishing reuses the existing Sonatype/Central-Portal `nexus-publish` scaffolding with new coordinates, POM metadata, and a CI job gated on release tags.

**Tech Stack:** SCons (native Android build), Gradle 8.6 + Android Gradle Plugin, Kotlin 2.1, `io.github.gradle-nexus.publish-plugin` (Central Portal), GitHub Actions.

---

## Verification model

This work has almost no unit-testable surface; verification is **build + static-grep + artifact inspection**, not doctest/JUnit. Each task's `Verify` is the real gate. The canonical builds (run from repo root unless noted):

- Native template: `scons platform=android target=template_debug arch=arm64 -j$(nproc)`
- Native editor: `scons platform=android target=editor arch=arm64 -j$(nproc)`
- Gradle assemble (from `platform/android/java`): `./gradlew :lib:assembleTemplateDebug`
- Package static gate (from repo root):
  `grep -rInE 'org[./_]godotengine' platform/android | grep -vE 'com/google/android/vending|THIRDPARTY|godotengine\.org'`
  must return **no lines** (only third-party vending code and `godotengine.org` license-header URLs survive).
- Brand static gate (after Task 8, from repo root):
  `grep -rInE 'generateGodot|getGodotLibrary|getGodotPublish|godotLibraryVersion' platform/android/java .github/workflows`
  must return **no lines**.

> If a device/emulator is unavailable, the runtime "boots without UnsatisfiedLinkError" check in Task 3 becomes a **manual pre-release gate**; the automated substitute is the symbol-table assertion.

---

### Task 0: Rename lib JVM package `org.godotengine.godot` → `games.cafecito.foundry`

**Goal:** Move the ~82 lib source files (main + test) to the new package and update all package declarations and imports, so the `lib` module's Kotlin/Java compiles under the new namespace.

**Files:**
- Move: `platform/android/java/lib/src/main/java/org/godotengine/godot/**` → `.../java/games/cafecito/foundry/**`
- Move: `platform/android/java/lib/src/test/java/org/godotengine/godot/**` → `.../java/games/cafecito/foundry/**`
- Modify: `platform/android/java/lib/build.gradle:35` (`namespace`)
- Modify: `platform/android/java/lib/src/main/AndroidManifest.xml` (any FQ refs)

**Acceptance Criteria:**
- [ ] No file remains under `lib/src/**/java/org/godotengine`
- [ ] Every moved file declares `package games.cafecito.foundry…`
- [ ] `lib/build.gradle` `namespace = "games.cafecito.foundry"`
- [ ] `./gradlew :lib:compileTemplateDebugKotlin` (or full assemble in Task 4) has no unresolved-symbol errors from the rename

**Verify:** `grep -rIl 'org.godotengine.godot' platform/android/java/lib/src` → no output

**Steps:**

- [ ] **Step 1: Move the source trees with git mv**

```bash
cd platform/android/java/lib/src
for base in main test; do
  if [ -d "$base/java/org/godotengine/godot" ]; then
    mkdir -p "$base/java/games/cafecito"
    git mv "$base/java/org/godotengine/godot" "$base/java/games/cafecito/foundry"
    # remove now-empty org/godotengine dirs
    rmdir -p "$base/java/org/godotengine" 2>/dev/null || true
  fi
done
cd -
```

- [ ] **Step 2: Rewrite package + import statements in moved files**

```bash
# Only touch files now under games/cafecito/foundry (lib). The dotted form is
# unambiguous: org.godotengine.godot -> games.cafecito.foundry.
grep -rIl 'org\.godotengine\.godot' platform/android/java/lib/src \
  | xargs sed -i '' -e 's/org\.godotengine\.godot/games.cafecito.foundry/g'
```

(On Linux CI `sed -i '' ` differs — use `sed -i` there. The engineer runs this locally on macOS; the equivalent in the CI/Cloud Linux VM is `sed -i 's/.../.../g'`.)

- [ ] **Step 3: Update the Gradle namespace**

In `platform/android/java/lib/build.gradle`, change:

```groovy
    namespace = "org.godotengine.godot"
```
to
```groovy
    namespace = "games.cafecito.foundry"
```

- [ ] **Step 4: Update AndroidManifest fully-qualified references**

```bash
grep -rIl 'org\.godotengine\.godot' platform/android/java/lib/src/main/AndroidManifest.xml \
  | xargs -r sed -i 's/org\.godotengine\.godot/games.cafecito.foundry/g'
```

- [ ] **Step 5: Verify the static gate for lib**

Run: `grep -rIl 'org.godotengine.godot' platform/android/java/lib/src`
Expected: no output.

- [ ] **Step 6: Commit**

```bash
git add -A platform/android/java/lib
git commit -m "refactor(android): rename lib package to games.cafecito.foundry"
```

---

### Task 1: Rename editor JVM package `org.godotengine.editor` → `games.cafecito.foundry.editor`

**Goal:** Move the editor module sources to the new package and update its `namespace`, consistent with the lib rename.

**Files:**
- Move: `platform/android/java/editor/src/**/java/org/godotengine/editor/**` → `.../java/games/cafecito/foundry/editor/**`
- Modify: `platform/android/java/editor/build.gradle:77` (`namespace`)
- Modify: `platform/android/java/editor/src/main/AndroidManifest.xml`

**Acceptance Criteria:**
- [ ] No file remains under `editor/src/**/java/org/godotengine`
- [ ] Every moved file declares `package games.cafecito.foundry.editor…`
- [ ] `editor/build.gradle` `namespace = "games.cafecito.foundry.editor"`
- [ ] Editor sources that import lib classes now import `games.cafecito.foundry.*` (handled by the dotted rewrite)

**Verify:** `grep -rIl 'org.godotengine' platform/android/java/editor/src` → no output

**Steps:**

- [ ] **Step 1: Move editor source trees (all source sets)**

```bash
cd platform/android/java/editor/src
for base in $(ls -d */ 2>/dev/null | sed 's#/##'); do
  if [ -d "$base/java/org/godotengine/editor" ]; then
    mkdir -p "$base/java/games/cafecito/foundry"
    git mv "$base/java/org/godotengine/editor" "$base/java/games/cafecito/foundry/editor"
    rmdir -p "$base/java/org/godotengine" 2>/dev/null || true
  fi
done
cd -
```

- [ ] **Step 2: Rewrite editor package + imports (both editor and referenced lib packages)**

```bash
grep -rIl 'org\.godotengine\.editor\|org\.godotengine\.godot' platform/android/java/editor/src \
  | xargs sed -i 's/org\.godotengine\.editor/games.cafecito.foundry.editor/g; s/org\.godotengine\.godot/games.cafecito.foundry/g'
```

- [ ] **Step 3: Update editor namespace**

In `platform/android/java/editor/build.gradle`, change `namespace = "org.godotengine.editor"` to `namespace = "games.cafecito.foundry.editor"`.

- [ ] **Step 4: Update editor AndroidManifest FQ references**

```bash
grep -rIl 'org\.godotengine' platform/android/java/editor/src/main/AndroidManifest.xml \
  | xargs -r sed -i 's/org\.godotengine\.editor/games.cafecito.foundry.editor/g; s/org\.godotengine\.godot/games.cafecito.foundry/g'
```

- [ ] **Step 5: Verify**

Run: `grep -rIl 'org.godotengine' platform/android/java/editor/src`
Expected: no output.

- [ ] **Step 6: Commit**

```bash
git add -A platform/android/java/editor
git commit -m "refactor(android): rename editor package to games.cafecito.foundry.editor"
```

---

### Task 2: Rename app-module + nativeSrcsConfigs references

**Goal:** Update the app/demo module and `nativeSrcsConfigs` (which reference lib/editor packages) so the whole Gradle project resolves. The app `applicationId` `com.godot.game` is intentionally NOT changed.

**Files:**
- Modify: files under `platform/android/java/app/src/**` that import `org.godotengine.*`
- Modify: `platform/android/java/nativeSrcsConfigs/build.gradle`
- Modify: `platform/android/java/app/src/instrumented/AndroidManifest.xml`

**Acceptance Criteria:**
- [ ] `grep -rIl 'org.godotengine' platform/android/java/app platform/android/java/nativeSrcsConfigs` → no output
- [ ] `com.godot.game` references are untouched (still present)

**Verify:** `grep -rIn 'org.godotengine' platform/android/java/app platform/android/java/nativeSrcsConfigs` → no output; `grep -rIl 'com.godot.game' platform/android/java/app` → still returns files

**Steps:**

- [ ] **Step 1: Rewrite imports (preserve com.godot.game and com.google.android.vending)**

```bash
grep -rIl 'org\.godotengine' platform/android/java/app platform/android/java/nativeSrcsConfigs \
  | xargs sed -i 's/org\.godotengine\.editor/games.cafecito.foundry.editor/g; s/org\.godotengine\.godot/games.cafecito.foundry/g'
```

- [ ] **Step 2: Verify app id preserved and no godotengine left**

Run: `grep -rIn 'org.godotengine' platform/android/java/app platform/android/java/nativeSrcsConfigs`
Expected: no output.
Run: `grep -rIl 'com.godot.game' platform/android/java/app | head`
Expected: at least one file (unchanged).

- [ ] **Step 3: Commit**

```bash
git add -A platform/android/java/app platform/android/java/nativeSrcsConfigs
git commit -m "refactor(android): update app/nativeSrcsConfigs to games.cafecito.foundry"
```

---

### Task 3: Rename C++ JNI export symbols + FindClass literals (reconcile GodotLib → FoundryLib)

**Goal:** Make the native library export `Java_games_cafecito_foundry_*` symbols that match the real JVM classes (`FoundryLib`, editor `EditorUtils`/`GameMenuUtils`, etc.) and update every `FindClass`/dotted class-name string literal, so native calls link and resolve at runtime.

**Files (JNI-bearing, non-exhaustive — the grep drives the full set):**
- Modify: `platform/android/java_foundry_lib_jni.{cpp,h}`
- Modify: `platform/android/java_foundry_wrapper.{cpp,h}`, `java_foundry_io_wrapper.{cpp,h}`, `java_foundry_view_wrapper.{cpp,h}`
- Modify: `platform/android/jni_utils.{cpp,h}`, `java_class_wrapper.cpp`, `dialog_utils_jni.{cpp,h}`, `os_android.cpp`
- Modify: editor-utils JNI TUs exporting `Java_org_godotengine_godot_editor_utils_*`

**Acceptance Criteria:**
- [ ] No `Java_org_godotengine` symbol remains in `platform/android`
- [ ] The `GodotLib` symbol stem is renamed to `FoundryLib` (the class that actually declares the 47 natives); `..._utils_*` stems keep their real class suffix (`EditorUtils`, `GameMenuUtils`, etc.)
- [ ] No `"org/godotengine/godot"` or `"org.godotengine.godot"` string literal remains in `platform/android`
- [ ] `scons platform=android target=template_debug arch=arm64` links successfully
- [ ] `nm -D` on the built `.so` shows `Java_games_cafecito_foundry_FoundryLib_initialize`

**Verify:**
```bash
scons platform=android target=template_debug arch=arm64 -j$(nproc) \
&& nm -D bin/libfoundry.android.template_debug.arm64.so 2>/dev/null \
   | grep -c 'Java_games_cafecito_foundry_FoundryLib_initialize'
```
Expected: build success and count ≥ 1. (Adjust the `.so` filename to the actual output; discover with `ls bin/*.so`.)

**Steps:**

- [ ] **Step 1: Confirm the class↔symbol mapping before rewriting**

The JVM class `FoundryLib` (package now `games.cafecito.foundry`) declares the natives, but C++ currently exports the stale `GodotLib` stem. Verify the mapping so the rename targets `FoundryLib`, not a literal transliteration:

```bash
grep -rhoE 'Java_org_godotengine_godot_[A-Za-z_]+' platform/android/*.cpp platform/android/*.h | sort -u
```
Expected stems: `..._GodotLib_*` (→ must become `FoundryLib`) and `..._utils_<Class>_*` (keep `<Class>`).

- [ ] **Step 2: Rewrite the `GodotLib` symbol stem to `FoundryLib` first**

```bash
grep -rIl 'Java_org_godotengine_godot_GodotLib' platform/android \
  | xargs sed -i 's/Java_org_godotengine_godot_GodotLib/Java_games_cafecito_foundry_FoundryLib/g'
```

- [ ] **Step 3: Rewrite all remaining `Java_org_godotengine_godot_` symbol prefixes**

```bash
grep -rIl 'Java_org_godotengine_godot_' platform/android \
  | xargs sed -i 's/Java_org_godotengine_godot_/Java_games_cafecito_foundry_/g'
```

- [ ] **Step 4: Rewrite FindClass / string-literal class names (slash and dotted forms)**

```bash
grep -rIl 'org/godotengine/godot\|org\.godotengine\.godot' platform/android/*.cpp platform/android/*.h \
  | xargs sed -i 's#org/godotengine/godot#games/cafecito/foundry#g; s/org\.godotengine\.godot/games.cafecito.foundry/g'
```

- [ ] **Step 5: Static gate for the native layer**

Run: `grep -rInE 'org[/._]godotengine' platform/android/*.cpp platform/android/*.h`
Expected: no output (license-header URLs like `https://godotengine.org` are `godotengine.org`, not matched by this pattern — confirm none remain that reference the package).

- [ ] **Step 6: Build and assert the symbol table**

Run the Verify command above. Expected: link success + symbol present.

- [ ] **Step 7: Commit**

```bash
git add -A platform/android
git commit -m "refactor(android): rename JNI symbols/classes to games.cafecito.foundry"
```

---

### Task 4: Update export pipeline package references

**Goal:** Ensure games exported by the editor reference the renamed library package in their generated `AndroidManifest.xml`/Gradle files, so exports still build and launch.

**Files:**
- Modify: `platform/android/export/export_plugin.cpp`
- Modify: `platform/android/export/gradle_export_util.{cpp,h}`

**Acceptance Criteria:**
- [ ] No `org.godotengine.godot` / `org/godotengine/godot` string remains in `platform/android/export`
- [ ] `scons platform=android target=editor arch=arm64` builds (editor embeds the export plugin)
- [ ] A generated export manifest references `games.cafecito.foundry` (spot-checked from a template export, or by reading the emitted string constants)

**Verify:** `grep -rInE 'org[/.]godotengine[/.]godot' platform/android/export` → no output; `scons platform=android target=editor arch=arm64 -j$(nproc)` → success

**Steps:**

- [ ] **Step 1: Inspect the references first**

```bash
grep -rInE 'org[/.]godotengine[/.]godot' platform/android/export
```
Read each hit to confirm it is a library-package reference (manifest string, `<meta-data>`, activity/provider name) and not an unrelated URL.

- [ ] **Step 2: Rewrite them**

```bash
grep -rIl 'org/godotengine/godot\|org\.godotengine\.godot' platform/android/export \
  | xargs sed -i 's#org/godotengine/godot#games/cafecito/foundry#g; s/org\.godotengine\.godot/games.cafecito.foundry/g'
```

- [ ] **Step 3: Build the editor**

Run: `scons platform=android target=editor arch=arm64 -j$(nproc)`
Expected: success.

- [ ] **Step 4: Commit**

```bash
git add -A platform/android/export
git commit -m "refactor(android): point exported-project references at games.cafecito.foundry"
```

---

### Task 5: Rename AAR output artifacts `godot-lib.*.aar` → `foundry-lib.*.aar`

**Goal:** Rename the generated AAR file names and every Gradle copy/delete reference to them, so the template/editor generation pipeline stays consistent under the Foundry brand.

**Files:**
- Modify: `platform/android/java/lib/build.gradle:130` (`output.outputFileName = "godot-lib.…"`)
- Modify: `platform/android/java/build.gradle:148,162` (`include("godot-lib.template_…")`)
- Modify: `platform/android/java/build.gradle:324-331` (delete paths)
- Modify: any remaining `godot-lib` reference (grep-driven), incl. `platform/android/export` and `release.yml`

**Acceptance Criteria:**
- [ ] `grep -rIn 'godot-lib' platform/android .github/workflows/release.yml` → no output
- [ ] `lib/build.gradle` emits `foundry-lib.${outputSuffix}.aar`
- [ ] `./gradlew :lib:assembleTemplateDebug` produces `lib/build/outputs/aar/foundry-lib.template_debug.aar`

**Verify:** `cd platform/android/java && ./gradlew :lib:assembleTemplateDebug && ls lib/build/outputs/aar/foundry-lib.template_debug.aar`

**Steps:**

- [ ] **Step 1: Rewrite the output file name**

In `platform/android/java/lib/build.gradle`, change:
```groovy
            output.outputFileName = "godot-lib.${outputSuffix}.aar"
```
to
```groovy
            output.outputFileName = "foundry-lib.${outputSuffix}.aar"
```

- [ ] **Step 2: Rewrite every remaining `godot-lib` reference**

```bash
grep -rIl 'godot-lib' platform/android .github/workflows/release.yml \
  | xargs sed -i 's/godot-lib/foundry-lib/g'
```

- [ ] **Step 3: Verify no stale references and the AAR builds**

Run: `grep -rIn 'godot-lib' platform/android .github/workflows/release.yml`
Expected: no output.
Run: `cd platform/android/java && ./gradlew :lib:assembleTemplateDebug && ls lib/build/outputs/aar/`
Expected: `foundry-lib.template_debug.aar` present. (Requires the native `.so` from Task 3 in `lib/libs/…`; run the scons template build first if the AAR assembly complains about missing jniLibs.)

- [ ] **Step 4: Commit**

```bash
git add -A platform/android .github/workflows/release.yml
git commit -m "build(android): rename AAR output to foundry-lib"
```

---

### Task 6: Update Maven coordinates and POM metadata for Maven Central

**Goal:** Publish under group `games.cafecito.foundry`, artifacts `foundry` / `foundry-debug` / `foundry-tools`, with Foundry/Cafecito POM metadata, PGP signing retained. Validate locally via `publishToMavenLocal`.

**Files:**
- Modify: `platform/android/java/lib/build.gradle:6-10` (`ext` artifact ids)
- Modify: `platform/android/java/scripts/publish-module.gradle` (POM `name`/`description`/`url`/`licenses`/`developers`/`scm`)
- Reference (unchanged logic, env-driven): `platform/android/java/scripts/publish-root.gradle` (`ossrhGroupId` ← `OSSRH_GROUP_ID`)

**Acceptance Criteria:**
- [ ] `ext.PUBLISH_ARTIFACT_ID = 'foundry'`, `DEBUG_PUBLISH_ARTIFACT_ID = 'foundry-debug'`, `TOOLS_PUBLISH_ARTIFACT_ID = 'foundry-tools'`
- [ ] POM `url` = `https://www.cafecito.games/`, `scm` points at `github.com/cafecito-games/Foundry`, license URL points at the fork's LICENSE
- [ ] `publishToMavenLocal` writes artifacts under `~/.m2/repository/games/cafecito/foundry/foundry/…` with a signed `.aar`, `.pom`, sources + javadoc jars

**Verify:**
```bash
cd platform/android/java
OSSRH_GROUP_ID=games.cafecito.foundry ./gradlew :lib:publishToMavenLocal
ls ~/.m2/repository/games/cafecito/foundry/foundry/
```
Expected: version directory containing `foundry-<version>.aar`, `.pom`, `-sources.jar`, `-javadoc.jar` (and `.asc` if signing keys are configured locally).

**Steps:**

- [ ] **Step 1: Rename the artifact ids in `lib/build.gradle`**

Change:
```groovy
ext {
    DEBUG_PUBLISH_ARTIFACT_ID = 'godot-debug'
    PUBLISH_ARTIFACT_ID = 'godot'
    TOOLS_PUBLISH_ARTIFACT_ID = 'godot-tools'
}
```
to:
```groovy
ext {
    DEBUG_PUBLISH_ARTIFACT_ID = 'foundry-debug'
    PUBLISH_ARTIFACT_ID = 'foundry'
    TOOLS_PUBLISH_ARTIFACT_ID = 'foundry-tools'
}
```

- [ ] **Step 2: Update POM metadata in `publish-module.gradle`**

For each of the three publications (`templateDebug`, `templateRelease`, `toolsRelease`), replace the `description`/`url`/`licenses`/`developers`/`scm` blocks. Example for the `templateRelease` publication (apply the analogous change to all three, keeping their distinct `name`/`description`):

```groovy
                pom {
                    name = PUBLISH_ARTIFACT_ID
                    description = 'Foundry Engine Android Library - Template Build'
                    url = 'https://www.cafecito.games/'
                    licenses {
                        license {
                            name = 'MIT License'
                            url = 'https://github.com/cafecito-games/Foundry/blob/develop/LICENSE.txt'
                        }
                    }
                    developers {
                        developer {
                            id = 'cafecito-games'
                            name = 'CafecitoGames'
                            email = 'csueiras@gmail.com'
                        }
                    }
                    scm {
                        connection = 'scm:git:github.com/cafecito-games/Foundry.git'
                        developerConnection = 'scm:git:ssh://github.com/cafecito-games/Foundry.git'
                        url = 'https://github.com/cafecito-games/Foundry/tree/develop'
                    }
                }
```

Keep the per-publication `description` distinct: `foundry-debug` → "…(Debug) Template Build", `foundry-tools` → "Foundry Engine Tools Android Library - Editor Build".

- [ ] **Step 3: Validate locally**

Run the Verify command. If no signing keys are present locally, run with `-x signTemplateReleasePublication` (and the sibling sign tasks) to validate coordinates without signing; note that CI keeps signing enabled.

- [ ] **Step 4: Commit**

```bash
git add -A platform/android/java/lib/build.gradle platform/android/java/scripts/publish-module.gradle
git commit -m "build(android): set Maven coordinates games.cafecito.foundry:foundry"
```

---

### Task 7: Wire the Maven Central publish job into `release.yml`

**Goal:** On a release, build the native libs + assemble the library AARs and publish the three publications to the Central Portal — releasing (close+release) for stable versions, and pushing `-SNAPSHOT` to the snapshot repo for dev/prerelease versions (`getGodotPublishVersion()` appends `-SNAPSHOT` when status ≠ `stable`).

**Files:**
- Modify: `.github/workflows/release.yml` (`build-android` job — add a publish step; or a new `publish-android-library` job gated on the resolve outputs)

**Acceptance Criteria:**
- [ ] A publish step runs only for release events (not PRs/drafts), reusing the resolve job's outputs
- [ ] Secrets are passed as env: `OSSRH_USERNAME`, `OSSRH_PASSWORD`, `OSSRH_GROUP_ID=games.cafecito.foundry`, `SONATYPE_STAGING_PROFILE_ID`, `SIGNING_KEY_ID`, `SIGNING_KEY`, `SIGNING_PASSWORD`
- [ ] Stable versions run `publishToSonatype closeAndReleaseSonatypeStagingRepository`; SNAPSHOT versions run only `publishToSonatype`
- [ ] `actionlint`/YAML parse passes

**Verify:** `actionlint .github/workflows/release.yml` (or `python -c "import yaml,sys; yaml.safe_load(open('.github/workflows/release.yml'))"`) → no errors; visual review that the publish step is gated on `needs.resolve.outputs.draft == 'false'` (or equivalent) and the tagged-release condition.

**Steps:**

- [ ] **Step 1: Add the publish step to the editor matrix leg of `build-android`**

After "Generate Foundry editor" in the `build-android` job (the editor leg produces the `tools`/editor AAR; the template legs produce the template AARs — publish from whichever leg(s) have the assembled AARs, or add a dedicated job that depends on all three legs and re-runs the gradle publish with the libs restored). Add, gated on release:

```yaml
      - name: Publish Android library to Maven Central
        if: needs.resolve.outputs.draft == 'false'
        working-directory: platform/android/java
        env:
          OSSRH_GROUP_ID: games.cafecito.foundry
          OSSRH_USERNAME: ${{ secrets.OSSRH_USERNAME }}
          OSSRH_PASSWORD: ${{ secrets.OSSRH_PASSWORD }}
          SONATYPE_STAGING_PROFILE_ID: ${{ secrets.SONATYPE_STAGING_PROFILE_ID }}
          SIGNING_KEY_ID: ${{ secrets.SIGNING_KEY_ID }}
          SIGNING_KEY: ${{ secrets.SIGNING_KEY }}
          SIGNING_PASSWORD: ${{ secrets.SIGNING_PASSWORD }}
        run: |
          VERSION=$(python -c "import sys; sys.path.insert(0,'../../..'); import version; \
            s=version.status; base=f'{version.major}.{version.minor}.{version.patch}'; \
            print(base if s=='stable' else base+'-'+s+'-SNAPSHOT')")
          echo "Publishing $VERSION"
          if echo "$VERSION" | grep -q 'SNAPSHOT'; then
            ./gradlew publishToSonatype --no-daemon
          else
            ./gradlew publishToSonatype closeAndReleaseSonatypeStagingRepository --no-daemon
          fi
```

> The exact leg/job placement depends on where all three AAR variants are simultaneously available. If no single leg has all of them, add a `publish-android-library` job that `needs` the three `build-android` legs, restores their uploaded AAR artifacts (or re-runs the scons+gradle assemble with the build cache), then runs the gradle publish. Choose one approach during implementation and keep the gating identical.

- [ ] **Step 2: Lint the workflow**

Run: `actionlint .github/workflows/release.yml`
Expected: no errors. (If `actionlint` is unavailable, validate YAML parses.)

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "ci(android): publish Foundry library to Maven Central on release"
```

---

### Task 8: Rebrand remaining `Godot` identifiers in the Android build layer

**Goal:** Eliminate the remaining `Godot`-branded identifiers in the Android Gradle/build layer — the `getGodotLibraryVersion*` / `getGodotPublishVersion` / `generateGodotLibraryVersion` helpers, the `generateGodot{Templates,MonoTemplates,Editor,HorizonOSEditor,PicoOSEditor}` tasks, the `godotLibraryVersion` manifest placeholder, and any other Godot-branded Gradle symbol — renaming them to Foundry equivalents and updating every call site (Gradle files, `AndroidManifest.xml`, and both CI workflows). No compat constraint. Explicitly excluded: the app/demo `applicationId` `com.godot.game` (spec-scoped out) and third-party `com.google.android.vending.*` / `godotengine.org` license-header URLs.

**Files:**
- Modify: `platform/android/java/app/config.gradle`, `platform/android/java/build.gradle`, `platform/android/java/lib/build.gradle`, `platform/android/java/editor/build.gradle`
- Modify: `platform/android/java/lib/src/main/AndroidManifest.xml` (and any manifest using `${godotLibraryVersion}`)
- Modify: `.github/workflows/release.yml`, `.github/workflows/android_builds.yml`

**Acceptance Criteria:**
- [ ] `grep -rInE 'Godot|godot' platform/android/java/*.gradle platform/android/java/app/config.gradle platform/android/java/lib/build.gradle platform/android/java/editor/build.gradle` → only `com.godot.game` and `godotengine.org` URL hits remain (no Godot-branded helper/task/placeholder identifiers)
- [ ] `cd platform/android/java && ./gradlew tasks` lists `generateFoundryTemplates`/`generateFoundryEditor` and no `generateGodot*`
- [ ] The manifest placeholder is `foundryLibraryVersion` in both `lib/build.gradle` and the consuming `AndroidManifest.xml`
- [ ] Both `release.yml` and `android_builds.yml` call the renamed tasks

**Verify:** `cd platform/android/java && ./gradlew help --task generateFoundryTemplates` → task found; `grep -rIn 'generateGodot\|godotLibraryVersion\|getGodotLibrary\|getGodotPublish' platform/android/java .github/workflows` → no output

**Steps:**

- [ ] **Step 1: Rename helpers, tasks, and the manifest placeholder consistently**

```bash
cd platform/android/java
sed -i 's/getGodotLibraryVersion/getFoundryLibraryVersion/g; \
        s/generateGodotLibraryVersion/generateFoundryLibraryVersion/g; \
        s/getGodotPublishVersion/getFoundryPublishVersion/g; \
        s/generateGodotTemplates/generateFoundryTemplates/g; \
        s/generateGodotMonoTemplates/generateFoundryMonoTemplates/g; \
        s/generateGodotEditor/generateFoundryEditor/g; \
        s/generateGodotHorizonOSEditor/generateFoundryHorizonOSEditor/g; \
        s/generateGodotPicoOSEditor/generateFoundryPicoOSEditor/g; \
        s/godotLibraryVersion/foundryLibraryVersion/g' \
   build.gradle lib/build.gradle editor/build.gradle app/config.gradle
# Manifest consumer(s) of the placeholder
grep -rIl 'godotLibraryVersion' lib/src editor/src app/src 2>/dev/null \
  | xargs -r sed -i 's/godotLibraryVersion/foundryLibraryVersion/g'
cd -
sed -i 's/generateGodotTemplates/generateFoundryTemplates/g; s/generateGodotEditor/generateFoundryEditor/g' \
   .github/workflows/release.yml .github/workflows/android_builds.yml
```

- [ ] **Step 2: Catch any residual Godot-branded Gradle identifier**

Run: `grep -rInE 'Godot' platform/android/java/*.gradle platform/android/java/app/config.gradle platform/android/java/lib/build.gradle platform/android/java/editor/build.gradle`
Inspect each remaining hit. Rename any Godot-branded identifier (function, task, variable, or comment referring to the old brand) to its Foundry equivalent; leave only genuinely external references (there should be none of `com.godot.game` in these Gradle files — that lives in `getExportPackageName`'s default string, which is the app id and stays).

- [ ] **Step 3: Verify tasks resolve, placeholder renamed, CI call sites updated**

Run: `cd platform/android/java && ./gradlew help --task generateFoundryTemplates`
Expected: task found.
Run: `grep -rIn 'generateGodot\|godotLibraryVersion\|getGodotLibrary\|getGodotPublish' platform/android/java .github/workflows`
Expected: no output.

- [ ] **Step 4: Commit**

```bash
git add -A platform/android/java .github/workflows
git commit -m "chore(android): rebrand remaining Godot identifiers to Foundry"
```

---

## Self-review notes

- **Spec coverage:** A1→Tasks 0–2; A2→Task 3; A3→Task 4; A4 static gate→embedded in each Verify + the top-level gates; B (coordinates/POM/signing)→Task 6; C (CI)→Task 7; AAR rename→Task 5; Godot-brand identifier rename→Task 8 (required). All spec sections map to a task.
- **Ordering/dependencies:** 0→1→2 (JVM), then 3 (native, depends on classes existing), 4 (export), 5 (AAR names), 6 (coordinates), 7 (CI, depends on 5+6), 8 (brand rename, last so its renames don't churn earlier diffs).
- **SNAPSHOT nuance** is handled explicitly in Task 7 because `version.py` status is `dev`; stable Central releases require status `stable` (→ plain `0.1.0`).
- **`sed -i` portability:** BSD/macOS needs `sed -i ''`; Linux/CI needs `sed -i`. Steps note this; the implementing environment is the Linux Cloud VM, so plain `sed -i` applies there.
