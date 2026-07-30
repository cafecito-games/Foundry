# Split iOS Template Builds Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Parallelize the release workflow's four logical iOS template builds while preserving the single `ios.zip` export-template artifact.

**Architecture:** A four-entry matrix builds release/debug device and simulator libraries independently. Simulator matrix entries compile both required architectures. A dependent assembly job downloads the four raw-library artifacts, packages the two simulator fat libraries, and uploads the unchanged `release-ios` zip consumed by the existing release package job.

**Tech Stack:** GitHub Actions YAML, Python 3 standard library, Apple's `lipo`, `zipfile`/`shutil`, unittest-style executable regression scripts.

---

## File map

- Create: `misc/scripts/package_ios_templates.py` — deterministic cross-job assembler for the iOS Xcode template zip.
- Create: `misc/checks/check_ios_template_package.py` — executable regression test using temporary fixtures and a fake `lipo` executable.
- Modify: `.github/workflows/release.yml:790-860` — turn `build-ios` into a four-entry matrix, use unique cache/artifact names, and add the `assemble-ios` job.
- Modify: `.github/scripts/test_release_ios_workflow.py` — assert the matrix, simulator architecture steps, assembly dependency, and final package wiring.
- Create: `docs/superpowers/specs/2026-07-24-split-ios-template-builds-design.md` — approved design, already committed as `57265ee2d5`.

### Task 1: Add a failing packaging-helper regression test

**Files:**
- Create: `misc/checks/check_ios_template_package.py`

- [x] **Step 1: Write the failing test**

Create a temporary Apple embedded template containing the four framework destinations and two non-iOS framework directories. Create the six raw input files expected from the four build jobs. Put a fake executable named `lipo` first on `PATH`; it parses `-output` and copies the first input to the output while recording the invocation. Run the not-yet-created helper with `--bin-dir`, `--template-dir`, and `--output`, then assert:

```python
with zipfile.ZipFile(output) as archive:
    names = set(archive.namelist())
    assert "ios_xcode/libfoundry.ios.release.xcframework/ios-arm64/libfoundry.a" in names
    assert "ios_xcode/libfoundry.ios.release.xcframework/ios-arm64_x86_64-simulator/libfoundry.a" in names
    assert "ios_xcode/libfoundry.ios.debug.xcframework/ios-arm64/libfoundry.a" in names
    assert "ios_xcode/libfoundry.ios.debug.xcframework/ios-arm64_x86_64-simulator/libfoundry.a" in names
    assert not any("visionos" in name for name in names)
```

Also run the helper with one required raw library removed and assert it exits non-zero with the missing filename in stderr. Keep the test executable with `python3 misc/checks/check_ios_template_package.py` and use only the Python standard library.

- [x] **Step 2: Run the test to verify it fails**

Run:

```bash
python3 misc/checks/check_ios_template_package.py
```

Expected: FAIL because `misc/scripts/package_ios_templates.py` does not exist yet.

### Task 2: Implement the deterministic iOS package assembler

**Files:**
- Create: `misc/scripts/package_ios_templates.py`

- [x] **Step 1: Implement the input model and validation**

Add an `argparse` CLI with required `--bin-dir`, `--template-dir`, and `--output` paths. Define the four raw library names from the existing SCons output convention:

```python
LIBRARY_NAMES = {
    "release_device": "libfoundry.ios.template_release.arm64.a",
    "release_simulator_arm64": "libfoundry.ios.template_release.arm64.simulator.a",
    "release_simulator_x86_64": "libfoundry.ios.template_release.x86_64.simulator.a",
    "debug_device": "libfoundry.ios.template_debug.arm64.a",
    "debug_simulator_arm64": "libfoundry.ios.template_debug.arm64.simulator.a",
    "debug_simulator_x86_64": "libfoundry.ios.template_debug.x86_64.simulator.a",
}
```

Require every listed input to be a regular file before creating the archive. Raise `FileNotFoundError` naming the exact missing path. Require the template directory to exist and the output parent to be created before packaging.

- [x] **Step 2: Implement simulator lipo assembly**

For each release/debug simulator pair, run:

```python
subprocess.run(
    ["lipo", "-create", str(arm64), str(x86_64), "-output", str(fat_output)],
    check=True,
)
```

Write fat outputs as `libfoundry.ios.template_{release,debug}.fat.simulator.a` in `--bin-dir`. Return the existing arm64 device path directly. Do not invoke `lipo` for device libraries.

- [x] **Step 3: Implement framework assembly and archive creation**

Copy `--template-dir` to a temporary `ios_xcode` staging directory beside the requested output. Copy the four device/simulator libraries to:

```text
ios_xcode/libfoundry.ios.release.xcframework/ios-arm64/libfoundry.a
ios_xcode/libfoundry.ios.release.xcframework/ios-arm64_x86_64-simulator/libfoundry.a
ios_xcode/libfoundry.ios.debug.xcframework/ios-arm64/libfoundry.a
ios_xcode/libfoundry.ios.debug.xcframework/ios-arm64_x86_64-simulator/libfoundry.a
```

Remove every top-level `libfoundry.*.xcframework` whose name is not an iOS release/debug framework. Create the requested zip with archive paths rooted at `ios_xcode/`, then remove the staging directory and generated fat libraries in a `finally` block. Return the output path on stdout and let subprocess/archive errors fail the command.

- [x] **Step 4: Run the test to verify it passes**

Run:

```bash
python3 misc/checks/check_ios_template_package.py
```

Expected: PASS, including the archive layout and missing-input failure case.

### Task 3: Replace serial iOS compilation with four matrix jobs

**Files:**
- Modify: `.github/workflows/release.yml:790-860`

- [x] **Step 1: Write the workflow-shape regression test**

Create `.github/scripts/test_release_ios_workflow.py` that reads `.github/workflows/release.yml` and asserts:

```python
assert "build-ios:" in workflow
assert "fail-fast: false" in workflow
for cache_name in ("release-ios-device", "release-ios-simulator", "debug-ios-device", "debug-ios-simulator"):
    assert f"cache-name: {cache_name}" in workflow
    assert f"name: {cache_name}" in workflow
assert "arch=x86_64 ios_simulator=yes" in workflow
assert "assemble-ios:" in workflow
assert "needs: build-ios" in workflow
assert "package_ios_templates.py" in workflow
assert "needs:\n      - resolve\n      - build-linux" in workflow
assert "- assemble-ios" in workflow
```

The test must also assert that the old serial-only marker `Compilation (debug simulator x86_64) + bundle` is absent and that the assembly job uploads `release-ios`.

- [x] **Step 2: Run the workflow-shape test to verify it fails**

Run:

```bash
python3 .github/scripts/test_release_ios_workflow.py
```

Expected: FAIL because the current workflow has one serial job, no assembly job, and no matrix cache names.

- [x] **Step 3: Convert `build-ios` to a four-entry matrix**

Keep the existing release environment and checkout/Xcode setup. Add:

```yaml
strategy:
  fail-fast: false
  matrix:
    include:
      - name: release-ios-device
        cache-name: release-ios-device
        target: template_release
        scons-flags: arch=arm64
        extra-scons-flags: ""
      - name: release-ios-simulator
        cache-name: release-ios-simulator
        target: template_release
        scons-flags: arch=arm64 ios_simulator=yes
        extra-scons-flags: arch=x86_64 ios_simulator=yes
      - name: debug-ios-device
        cache-name: debug-ios-device
        target: template_debug
        scons-flags: arch=arm64
        extra-scons-flags: ""
      - name: debug-ios-simulator
        cache-name: debug-ios-simulator
        target: template_debug
        scons-flags: arch=arm64 ios_simulator=yes
        extra-scons-flags: arch=x86_64 ios_simulator=yes
```

Use `name: iOS ${{ matrix.name }}` at the job level and `cache-name: ${{ matrix.cache-name }}` for both cache actions. Run the normal `foundry-build` action once with `matrix.scons-flags`, then run it a second time only when `matrix.extra-scons-flags != ''`. Remove every `generate_bundle` flag from matrix compilation. Upload each raw `bin/*` artifact with `name: ${{ matrix.cache-name }}`.

- [x] **Step 4: Add the `assemble-ios` job**

Create a macOS job after `build-ios` with `needs: build-ios`, checkout, Xcode selection, `actions/download-artifact@v8` using `pattern: '*-ios-*'` and `path: artifacts`, and a shell step that copies the downloaded artifact files into `bin/`:

```bash
mkdir -p bin
for artifact in artifacts/*; do
  cp "$artifact"/* bin/
done
```

Invoke:

```bash
python3 misc/scripts/package_ios_templates.py \
  --bin-dir bin \
  --template-dir misc/dist/apple_embedded_xcode \
  --output bin/ios_xcode.zip
```

Upload `bin/ios_xcode.zip` through the local upload action as `release-ios`. Do not restore a shared cache in the assembly job; it only packages downloaded static libraries.

- [x] **Step 5: Update release package dependencies**

In `package.needs`, replace `build-ios` with `assemble-ios`, and assert that `build-ios` no longer appears in that dependency list. Leave the existing `cp artifacts/release-ios/*.zip templates/ios.zip` command unchanged.

- [x] **Step 6: Run both regression tests**

Run:

```bash
python3 misc/checks/check_ios_template_package.py
python3 .github/scripts/test_release_ios_workflow.py
```

Expected: both commands pass.

### Task 4: Validate YAML and final diff

**Files:**
- Modify: none

- [x] **Step 1: Validate the workflow syntax**

Run:

```bash
actionlint .github/workflows/release.yml
```

Expected: no diagnostics and exit status 0.

- [x] **Step 2: Run applicable Python checks**

Run:

```bash
python3 -m compileall -q misc/scripts/package_ios_templates.py misc/checks/check_ios_template_package.py .github/scripts/test_release_ios_workflow.py
python3 misc/checks/check_ios_template_package.py
python3 .github/scripts/test_release_ios_workflow.py
```

Expected: all commands exit 0.

- [x] **Step 3: Inspect repository state and diff**

Run:

```bash
git status --short
git diff --check HEAD
git diff HEAD -- .github/workflows/release.yml misc/scripts/package_ios_templates.py misc/checks/check_ios_template_package.py .github/scripts/test_release_ios_workflow.py
```

Confirm only the intended implementation files are changed after the already-committed design document, with no generated archives or temporary files tracked.

- [x] **Step 4: Commit the implementation**

```bash
git add .github/workflows/release.yml .github/scripts/test_release_ios_workflow.py misc/scripts/package_ios_templates.py misc/checks/check_ios_template_package.py
git commit -m "ci: parallelize iOS template builds"
```
