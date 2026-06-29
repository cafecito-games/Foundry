# Foundry versioning reset + release pipeline

**Date:** 2026-06-29
**Status:** Approved for planning

## Goal

Divorce Foundry's version numbering from upstream Godot. Reset the engine
version from `4.6.3.stable` to `0.1.0.dev`, rename the compiled binary from
`godot.*` to `foundry.*`, scrub "godot" from the CI workflow surface, and add a
release pipeline that builds and publishes Foundry releases from a git tag or a
manual trigger.

## Non-goals

- Hosting a Foundry documentation site.
- Migrating existing user projects or `.pck` files created by the old 4.6.x
  builds.
- Renaming the web runtime's internal asset identity (`godot.js`, `godot.wasm`,
  the JS `Godot` engine class) — see Section B.
- Removing dead engine-version migration branches beyond what the renumber
  requires.

## A. Version renumbering

Edit `version.py`:

```python
major = 0
minor = 1
patch = 0
status = "dev"
```

`short_name`, `name`, `website` already reflect Foundry and are unchanged.

### Side effects and decisions

- **`docs` field stays `"4.6"`.** It feeds `VERSION_DOCS_BRANCH` /
  `VERSION_DOCS_URL`, which build in-editor documentation links against
  `docs.godotengine.org/en/4.6`. There is no Foundry docs site and the engine
  API is still 4.6-based, so changing it to `0.1` would 404 every link. Left
  unchanged deliberately.
- **`.pck` compatibility (`core/io/file_access_pack.cpp`).** Pack
  compatibility is gated on `FOUNDRY_VERSION_MAJOR/MINOR`. After the reset, a
  `.pck` built by an old 4.6.x binary reads as "newer" (4 > 0) and is refused.
  This is acceptable: a 0.1.0 build pairs with 0.1.0 packs. Known consequence,
  no code change.
- **`editor_settings.cpp:1246`.** The `FOUNDRY_VERSION_MAJOR == 4 && minor < 3`
  one-time editor-settings migration branch becomes unreachable at major 0. It
  only migrated old Godot 4.x editor settings; left as-is rather than removing
  migration logic.
- **`CONFIG_VERSION`** (project.godot format version) is independent of the
  engine version and is not touched.

### Verification

`./bin/foundry.<platform>.editor.dev.<arch> --version` prints a `0.1.0.dev`
version string; the editor launches without version-gate regressions.

## B. Binary rename `godot.*` -> `foundry.*`

### In scope

- `env.add_program("#bin/godot", ...)` -> `"#bin/foundry"` (and the matching
  `add_library` / `add_shared_library` calls) in:
  - `platform/linuxbsd/SCsub`
  - `platform/macos/SCsub`
  - `platform/windows/SCsub`
  - `platform/web/SCsub`
- The macOS editor `.app` bundle name (`Godot.app` -> `Foundry.app`) and any
  bundling/codesign references that depend on it.
- All `./bin/godot.*` invocations in CI workflows, composite actions, and the
  test commands documented in `CLAUDE.md`.

### Not derived from the binary name (no change needed)

Desktop export-template filenames are canonical (`linux_release.x86_64`,
`windows_release_x86_64.exe`, `macos.zip`, etc.), produced by each platform's
`get_template_file_name()`. They do not depend on the build binary's basename,
so the rename does not affect export.

### Out of scope — web runtime internal naming

`platform/web/emscripten_helpers.py` and the web export plugin emit
`godot.js`, `godot.wasm`, `godot.editor.*`, `godot.service.worker.js`,
`godot.offline.html`, and a JS `Godot` engine class. These are the web
runtime's identity wired into the HTML shell and service worker. Renaming them
risks breaking web export/runtime with no user-facing benefit and is excluded.
The web *build artifact* (`#bin/godot` in `platform/web/SCsub`) is still renamed
to `#bin/foundry` so the SCons program target is consistent; only the runtime
asset basenames stay.

### Verification

A clean build emits `bin/foundry.*` (no `bin/godot.*`); `grep -rn '#bin/godot'
platform/` returns nothing; the web template still exports and runs.

## C. Workflow de-"godot"-ification

### Composite actions

Rename directories under `.github/actions/`:

| Current                  | New                         |
| ------------------------ | --------------------------- |
| `godot-build`            | `foundry-build`             |
| `godot-cache-restore`    | `foundry-cache-restore`     |
| `godot-cache-save`       | `foundry-cache-save`        |
| `godot-deps`             | `foundry-deps`              |
| `godot-cpp-build`        | `foundry-cpp-build`         |
| `godot-converter-test`   | `foundry-converter-test`    |
| `godot-project-test`     | `foundry-project-test`      |

Update every `uses: ./.github/actions/godot-*` reference across all workflows to
the new paths, and the human-readable `name:` strings inside each action.

### Workflows

- Rename user-visible workflow `name:` and step `name:` fields from
  "Godot ..." to "Foundry ...".
- Replace `bin/godot.*` paths with `bin/foundry.*`.

### Keep genuine upstream references

Do **not** rewrite references to real external projects whose URLs/identifiers
must resolve:

- `godotengine/godot-cpp` (GDExtension bindings repo) used by
  `foundry-cpp-build`.
- emsdk, scoop/apt package names, and any other upstream tool identifiers.

The distinction: rename our own names (binary, our workflow/step/action names);
preserve names that point at external projects.

### Verification

`grep -rin 'godot' .github/workflows .github/actions` returns only legitimate
external-project references (godot-cpp, etc.); workflows parse (CI lint passes).

## D. Release workflow (`.github/workflows/release.yml`)

### Triggers

- `push` of a tag matching `v*` (e.g. `v0.1.0`).
- `workflow_dispatch` with a `version` string input for manual runs.

### Build matrix

Reuse the existing composite build actions to produce, per platform:

| Platform | Outputs                                                  |
| -------- | ------------------------------------------------------- |
| Linux    | editor + `template_release`/`template_debug` (x86_64)   |
| Windows  | editor + `template_release`/`template_debug`            |
| macOS    | universal editor + universal templates                  |
| Web      | `template_release`/`template_debug`                     |
| Android  | export templates (debug + release)                      |
| iOS      | export templates                                        |

(Architectures mirror what the current per-platform CI workflows already build;
the release workflow targets `template_release`/`editor` rather than dev builds.)

### Packaging

- Per-platform editor archives (zip), named with the Foundry version, e.g.
  `Foundry_v0.1.0_linux.x86_64.zip`, `Foundry_v0.1.0_macos.universal.zip`.
- An export-templates package (`.tpz`) following the standard Godot template
  layout, with each binary renamed to its canonical template filename
  (`linux_release.x86_64`, `windows_release_x86_64.exe`, `web_*.zip`,
  `android_*.apk`, `ios.zip`, ...) plus a `version.txt`.

### Publish

- **Tag push:** create and **auto-publish** a GitHub Release for the tag with
  all artifacts attached and auto-generated release notes.
- **Manual dispatch:** create the release as a **draft** for manual review and
  publishing.

### Verification

A dry-run via `workflow_dispatch` produces a draft release with all expected
artifacts attached and correctly named; pushing a `v*` tag produces a published
release.

## Risks

- **Version-gate fallout:** the major 4 -> 0 change can surface in any code that
  numerically reasons about the engine version. The audited cases (`.pck`
  compat, editor-settings migration) are accounted for above; a build + test
  run is the safety net for anything missed.
- **macOS bundle/codesign:** renaming `Godot.app` -> `Foundry.app` must be kept
  consistent with any plist/codesign/notarization references in the macOS build
  and release paths.
- **Composite-action rename churn:** every `uses:` reference must be updated in
  lockstep with the directory renames or CI breaks immediately.

## Testing

- Local: `scons platform=<os> target=editor dev_build=yes tests=yes` builds and
  emits `bin/foundry.*`; `./bin/foundry.* --version` shows `0.1.0.dev`;
  `./bin/foundry.* --headless --test` passes.
- CI: workflows parse and run green on a PR; `grep` audits for residual
  `bin/godot` and stray `godot` references pass.
- Release: `workflow_dispatch` dry-run yields a complete draft release.
