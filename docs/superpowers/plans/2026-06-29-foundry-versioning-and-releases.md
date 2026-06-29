# Foundry Versioning Reset + Release Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reset Foundry's version to `0.1.0.dev`, rename the build binary `godot.*` -> `foundry.*`, scrub "godot" from CI, and add a tag/dispatch-driven release pipeline.

**Architecture:** Four independent, separately-committable changes: (0) version + docs URL constants, (1) build-binary rename in platform SCsub + macOS bundle, (2) de-"godot" the existing CI workflows + composite actions, (3) a new `release.yml`. Each is verified by build + grep audits; the C++ test suite runs once after the rename to catch version-gate regressions.

**Tech Stack:** Python (`version.py`, SCons builders), SCons (`SCsub`), GitHub Actions YAML, C++ (engine, only as build/test target).

**Base branch:** `foundry-versioning-and-releases` off `develop`. Spec: `docs/superpowers/specs/2026-06-29-foundry-versioning-and-releases-design.md`.

**Coordination note:** A separate `rename/foundry` branch does Foundry *branding* (README, About dialog, brand assets, macOS plist) and also edits `core/core_builders.py`. It does NOT rename the build binary. Expect a possible merge touch-point in `core/core_builders.py`; resolve at merge time.

**Build command used throughout (macOS, local):**
```bash
scons platform=macos target=editor dev_build=yes tests=yes cache_path=/Users/christian/.scons_cache_godot -j$(sysctl -n hw.ncpu)
```
Resulting binary: `bin/foundry.macos.editor.dev.<arch>` (after Task 1; `bin/godot.*` before).

---

### Task 0: Reset version and point docs URL at Foundry

**Goal:** Engine reports `0.1.0.dev`; in-editor doc links resolve to the Foundry placeholder docs URL with the Foundry version branch.

**Files:**
- Modify: `version.py`
- Modify: `core/core_builders.py:32`
- Modify: `modules/mono/build_scripts/build_assemblies.py:326`

**Acceptance Criteria:**
- [ ] `version.py` has `major=0, minor=1, patch=0, status="dev", docs="0.1"`
- [ ] Generated `FOUNDRY_VERSION_DOCS_URL` base is `https://docs.cafecito.games/foundry/en/`
- [ ] C#/mono `VersionDocsUrl` constant uses the same base
- [ ] `./bin/*.editor.dev.* --version` prints a `0.1.0.dev.*` string

**Verify:** build, then `./bin/godot.macos.editor.dev.* --version` -> contains `0.1.0.dev` (binary still `godot.*` until Task 1).

**Steps:**

- [ ] **Step 1: Edit `version.py`**

Set the numeric fields and docs branch (leave `short_name`, `name`, `website` as-is):

```python
short_name = "foundry"
name = "Foundry"
major = 0
minor = 1
patch = 0
status = "dev"
module_config = ""
website = "https://www.cafecito.games/"
docs = "0.1"
```

- [ ] **Step 2: Edit the docs URL generator `core/core_builders.py`**

Change line 32 from the Godot docs host to the Foundry placeholder:

```python
#define FOUNDRY_VERSION_DOCS_BRANCH "{docs_branch}"
#define FOUNDRY_VERSION_DOCS_URL "https://docs.cafecito.games/foundry/en/" FOUNDRY_VERSION_DOCS_BRANCH
```

- [ ] **Step 3: Edit the C#/mono constant `modules/mono/build_scripts/build_assemblies.py:326`**

```python
        public const string VersionDocsUrl = "https://docs.cafecito.games/foundry/en/{docs_branch}";
```

- [ ] **Step 4: Build and verify version string**

Run:
```bash
scons platform=macos target=editor dev_build=yes tests=yes cache_path=/Users/christian/.scons_cache_godot -j$(sysctl -n hw.ncpu)
./bin/godot.macos.editor.dev.* --version
```
Expected: output contains `0.1.0.dev`.

- [ ] **Step 5: Verify generated docs URL**

Run:
```bash
grep FOUNDRY_VERSION_DOCS core/version_generated.gen.h
```
Expected: `..._DOCS_BRANCH "0.1"` and `..._DOCS_URL "https://docs.cafecito.games/foundry/en/" ...`.

- [ ] **Step 6: Commit**

```bash
git add version.py core/core_builders.py modules/mono/build_scripts/build_assemblies.py
git commit -m "Reset version to 0.1.0.dev and point docs URL at Foundry"
```

---

### Task 1: Rename build binary `godot.*` -> `foundry.*`

**Goal:** SCons emits `bin/foundry.*` on every desktop/web platform, and the macOS editor `.app` bundle is Foundry-named; no `#bin/godot` program target remains.

**Files:**
- Modify: `platform/linuxbsd/SCsub:49,51,53`
- Modify: `platform/macos/SCsub:53,55,57`
- Modify: `platform/windows/SCsub:88,90,92,117`
- Modify: `platform/web/SCsub` (the `add_program("#bin/godot" ...)` line)
- Modify: `platform/macos/platform_macos_builders.py` (bundle prefix + inner executable name + Info.plist `CFBundleExecutable`)
- Modify: `misc/dist/macos_tools.app/Contents/Info.plist` (`CFBundleExecutable` if it names `Godot`)

**Out of scope (left as Godot on purpose):** web runtime asset basenames (`godot.js`, `godot.wasm`, `godot.service.worker.js`, `godot.offline.html`, JS `Godot` class) in `platform/web/emscripten_helpers.py` and the web export plugin. Only the SCons program target (`#bin/godot` -> `#bin/foundry`) changes for web.

**Acceptance Criteria:**
- [ ] `grep -rn '#bin/godot' platform/` returns nothing
- [ ] Build emits `bin/foundry.macos.editor.dev.<arch>` (no `bin/godot.*`)
- [ ] macOS bundle dir is `foundry_macos_editor*.app` with executable `Contents/MacOS/Foundry` and matching `CFBundleExecutable`
- [ ] `./bin/foundry.* --headless --test` passes (no version-gate regressions)

**Verify:** `ls bin/foundry.*` succeeds and `ls bin/godot.* 2>/dev/null` is empty after a clean-ish rebuild of the affected targets.

**Steps:**

- [ ] **Step 1: Rename the program target in each platform SCsub**

Replace `#bin/godot` with `#bin/foundry` in the `add_program` / `add_library` / `add_shared_library` calls. Apply to all four platforms:

```bash
sed -i '' 's|"#bin/godot"|"#bin/foundry"|g' \
  platform/linuxbsd/SCsub platform/macos/SCsub platform/windows/SCsub platform/web/SCsub
```
Then confirm no stray `#bin/godot` remains in `platform/`:
```bash
grep -rn '#bin/godot' platform/
```
Expected: no output.

- [ ] **Step 2: Rename the macOS `.app` bundle in `platform/macos/platform_macos_builders.py`**

In `generate_bundle`, change the editor bundle prefix and the copied executable name:

```python
# Editor bundle.
prefix = "foundry." + env["platform"] + "." + env["target"]
```
and
```python
shutil.copy(target_bin, app_dir + "/Contents/MacOS/Foundry")
```

- [ ] **Step 3: Update the macOS app template plist**

In `misc/dist/macos_tools.app/Contents/Info.plist`, set `CFBundleExecutable` to `Foundry` (match Step 2's executable filename). Leave other branding keys untouched (they belong to the `rename/foundry` branding branch).

```bash
grep -n 'CFBundleExecutable' -A1 misc/dist/macos_tools.app/Contents/Info.plist
```
Edit the value from `Godot` to `Foundry`.

- [ ] **Step 4: Rebuild and verify binary name**

```bash
scons platform=macos target=editor dev_build=yes tests=yes cache_path=/Users/christian/.scons_cache_godot -j$(sysctl -n hw.ncpu)
ls bin/foundry.macos.editor.dev.* && (ls bin/godot.* 2>/dev/null && echo "STRAY GODOT BINARY" || echo "no godot binary - good")
```
Expected: `bin/foundry.*` listed; "no godot binary - good".

- [ ] **Step 5: Run the C++/Foundry test suite (version-gate regression check)**

```bash
./bin/foundry.macos.editor.dev.* --headless --test --force-colors
```
Expected: all suites pass. (Watches for fallout from the major 4 -> 0 change, e.g. `.pck` and editor-settings version gates.)

- [ ] **Step 6: Commit**

```bash
git add platform/ misc/dist/macos_tools.app/Contents/Info.plist
git commit -m "Rename build binary godot -> foundry"
```

---

### Task 2: De-"godot" the existing CI workflows and composite actions

**Goal:** No Foundry-owned "godot" naming remains in `.github/` (binary paths, workflow/step/action names, composite-action directory names); genuine upstream references (godot-cpp, emsdk) are preserved.

**Files:**
- Rename dirs: `.github/actions/godot-build` -> `foundry-build`, `godot-cache-restore` -> `foundry-cache-restore`, `godot-cache-save` -> `foundry-cache-save`, `godot-deps` -> `foundry-deps`, `godot-cpp-build` -> `foundry-cpp-build`, `godot-converter-test` -> `foundry-converter-test`, `godot-project-test` -> `foundry-project-test`
- Modify: all `.github/workflows/*.yml` (the `uses:` paths, `bin/godot.*` -> `bin/foundry.*`, "Godot" -> "Foundry" in `name:` fields)
- Modify: the `name:` strings inside each renamed `action.yml`

**Acceptance Criteria:**
- [ ] `git -C . ls-files .github/actions | grep -c 'godot-'` is 0
- [ ] `grep -rn 'actions/godot-' .github/workflows` returns nothing
- [ ] `grep -rn 'bin/godot' .github` returns nothing
- [ ] Remaining `.github` "godot" hits are only `godotengine/godot-cpp`, emsdk, or other external identifiers
- [ ] All workflow YAML parses (actionlint clean, or manual YAML lint if actionlint unavailable)

**Verify:** `grep -rin 'godot' .github/workflows .github/actions | grep -viE 'godot-cpp|emsdk'` returns only intentional leftovers (ideally none).

**Steps:**

- [ ] **Step 1: Rename the composite action directories with git**

```bash
cd .github/actions
for d in build cache-restore cache-save deps cpp-build converter-test project-test; do
  git mv "godot-$d" "foundry-$d"
done
cd -
```

- [ ] **Step 2: Update every `uses:` reference in workflows**

```bash
sed -i '' 's|actions/godot-|actions/foundry-|g' .github/workflows/*.yml
grep -rn 'actions/godot-' .github/workflows
```
Expected: no output.

- [ ] **Step 3: Update binary paths in workflows**

```bash
sed -i '' 's|bin/godot\.|bin/foundry.|g' .github/workflows/*.yml
grep -rn 'bin/godot' .github/workflows
```
Expected: no output.

- [ ] **Step 4: Rename "Godot" in human-readable names**

Replace "Godot" with "Foundry" in workflow/step `name:` fields and in each renamed action's internal `name:`/`description:` strings. Do NOT touch `godotengine/godot-cpp`, emsdk URLs, or package identifiers.

```bash
# Step names like "Restore Godot build cache", "Generate Godot templates/editor".
grep -rln 'name:.*Godot' .github/workflows .github/actions
# Edit each match: "Godot" -> "Foundry" in the name:/description: text only.
```
After editing, confirm only external refs remain:
```bash
grep -rin 'godot' .github/workflows .github/actions | grep -viE 'godot-cpp|emsdk'
```
Expected: empty (or only clearly-external identifiers).

- [ ] **Step 5: Lint the workflows**

```bash
command -v actionlint >/dev/null && actionlint || echo "actionlint not installed; YAML-parse instead:"
python3 -c "import sys,glob,yaml; [yaml.safe_load(open(f)) for f in glob.glob('.github/workflows/*.yml')+glob.glob('.github/actions/*/action.yml')]; print('YAML OK')"
```
Expected: actionlint clean, or `YAML OK`.

- [ ] **Step 6: Commit**

```bash
git add .github
git commit -m "Rename CI workflows and composite actions from godot to foundry"
```

---

### Task 3: Add the release pipeline (`release.yml`)

**Goal:** Pushing a `v*` tag (or manual dispatch) builds Foundry for all platforms, packages editor archives + an export-templates `.tpz`, and publishes a GitHub Release (auto for tags, draft for manual dispatch).

**Files:**
- Create: `.github/workflows/release.yml`

**Acceptance Criteria:**
- [ ] Triggers on `push` tag `v*` and on `workflow_dispatch` with a `version` input
- [ ] Build matrix covers Linux, Windows, macOS, Web, Android, iOS (editor where applicable + `template_release`/`template_debug`)
- [ ] Reuses the renamed `foundry-*` composite actions and references `bin/foundry.*`
- [ ] Produces per-platform editor archives and a `foundry_export_templates.tpz` with canonical template filenames + `version.txt`
- [ ] Tag push auto-publishes the release; `workflow_dispatch` produces a draft
- [ ] YAML parses (actionlint clean)

**Verify:** actionlint clean; a `workflow_dispatch` dry-run (after merge, in GitHub) yields a draft release with all artifacts. Locally, verification is YAML lint + structural review only.

**Steps:**

- [ ] **Step 1: Determine the version string source**

The release version comes from the tag (`github.ref_name` with leading `v` stripped) on tag push, or the `version` dispatch input on manual runs. Expose it as a job output used by packaging/naming. Sanity-check it matches `version.py` (`python -c "import version; print(f'{version.major}.{version.minor}.{version.patch}')"`).

- [ ] **Step 2: Author `.github/workflows/release.yml`**

Create the workflow with:
- `on: { push: { tags: ['v*'] }, workflow_dispatch: { inputs: { version: { description: 'Release version (e.g. 0.1.0)', required: true } } } }`
- A `resolve-version` job that outputs the version string and a `draft` boolean (`draft=true` when `github.event_name == 'workflow_dispatch'`).
- Per-platform build jobs reusing `./.github/actions/foundry-deps`, `foundry-cache-restore`, `foundry-build` with `target=template_release`/`template_debug` (and `target=editor` for desktop), uploading each via `./.github/actions/upload-artifact`. Mirror the SCons flags/arches already used in the matching `*_builds.yml`.
- A `package` job that downloads all artifacts, renames each template binary to its canonical name (`linux_release.x86_64`, `linux_debug.x86_64`, `windows_release_x86_64.exe`, `windows_debug_x86_64.exe`, `macos.zip`, `web_dlink_nothreads_release.zip` etc. per Godot's template layout, `android_release.apk`/`android_debug.apk`, `ios.zip`), writes `version.txt` (the resolved version), zips them into `foundry_export_templates.tpz`, and zips desktop editors as `Foundry_v<version>_<platform>.<arch>.zip` (macOS = zipped `.app`).
- A `publish` job using `softprops/action-gh-release` (or `gh release create`) that attaches all packaged assets, sets `draft: ${{ needs.resolve-version.outputs.draft }}`, `generate_release_notes: true`, and `tag_name`/`name` from the resolved version.

- [ ] **Step 3: Lint the new workflow**

```bash
command -v actionlint >/dev/null && actionlint .github/workflows/release.yml || \
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/release.yml')); print('YAML OK')"
```
Expected: actionlint clean or `YAML OK`.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "Add Foundry release workflow (tag + manual dispatch)"
```

---

## Post-implementation

- [ ] Full grep audit: `grep -rin 'godot' .github` shows only external refs; `grep -rn '#bin/godot' platform/` empty.
- [ ] Supervised Codex review of the branch (`codex-review-supervised`).
- [ ] Address review feedback.
- [ ] Open PR targeting `develop`.

## Self-Review notes

- Spec coverage: A (version+docs) -> Task 0; B (binary rename) -> Task 1; C (workflow de-godot) -> Task 2; D (release) -> Task 3. All covered.
- The web runtime asset-name exclusion from the spec is explicitly carried into Task 1's "Out of scope".
- The `docs="0.1"`, placeholder docs host, and `.pck` hard-reset decisions from the spec amendment are reflected in Task 0 and require no extra code.
