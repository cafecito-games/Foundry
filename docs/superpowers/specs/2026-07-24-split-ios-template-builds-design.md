# Split iOS Template Builds Design

**Date:** 2026-07-24
**Status:** Approved

## Problem

The release workflow's `build-ios` job runs six SCons invocations serially on one macOS runner. The invocations produce release and debug iOS templates for a device and simulator, with the simulator requiring both `arm64` and `x86_64` slices. The serial layout makes the wall-clock time approximately the sum of all compilation times even though the four logical template variants are independent.

## Goals

- Run the four logical iOS template variants in parallel:
  - release device
  - release simulator
  - debug device
  - debug simulator
- Preserve the current `ios.zip` artifact and its device/simulator framework contents.
- Keep simulator output as a universal `arm64` + `x86_64` slice.
- Keep release and debug outputs separate until the final assembly step.
- Use distinct cache and artifact names so parallel jobs do not contend for the same GitHub Actions cache key or artifact.

## Non-goals

- Do not change SCons, iOS platform code, template naming, or generated framework contents.
- Do not publish four separate release assets; the public release continues to contain one `ios.zip` template package.
- Do not add a second build of the device arm64 slice or alter the existing production/release flags.

## Design

Replace the single `build-ios` job with four matrix jobs. Each matrix entry checks out the same revision, selects Xcode 26, restores and saves a variant-specific cache, and uploads its raw build outputs under a variant-specific artifact name.

The device entries compile one `arm64` target. The simulator entries compile `arm64` and `x86_64` targets sequentially within their own job, because the final simulator framework must contain both architectures. No matrix entry runs `generate_bundle`; that operation requires all release and debug inputs and therefore belongs in a separate assembly job.

Add `assemble-ios` after the four build jobs. It downloads all four artifacts, places their static libraries in one `bin/` directory, and runs a checked-in standard-library Python packaging helper. The helper performs the same operations required by the existing iOS bundle path: `lipo` the simulator arm64/x86_64 pairs, copy the Apple embedded Xcode template, place release/debug device and simulator libraries into their corresponding iOS xcframework directories, remove non-iOS frameworks, and zip the project. It uploads the resulting zip as `release-ios`.

The helper is used only for cross-job assembly, where invoking SCons would otherwise rebuild the source tree because the assembly runner has no prior SCons dependency graph. Its filenames and framework destinations are derived from the checked-in `libfoundry.ios.*` template and platform outputs, and it fails on missing input libraries or bundle destinations rather than silently creating an incomplete archive. The release `package` job continues to consume `artifacts/release-ios/*.zip` without changes to the final `.tpz` layout.

## Data flow

```text
release device ───────┐
release simulator ────┼─> assemble-ios ──> release-ios ──> package ──> ios.zip in .tpz
debug device ─────────┤
debug simulator ──────┘
```

Each compile job uploads the raw `bin/libfoundry.ios.*.a` outputs needed by the assembler. The assembler downloads them into a clean workspace, combines simulator architectures, then creates the same single zip currently uploaded by `build-ios`.

## Error handling and compatibility

- Matrix jobs use `fail-fast: false`, so failures identify the exact device/debug/release variant while allowing other variants to finish.
- The assembly job requires all four variants and therefore cannot run when any required compile job fails.
- Cache restore/save remains `continue-on-error: true`, matching existing release behavior; a cache miss must not fail a build.
- The existing `package` dependency changes from `build-ios` to `assemble-ios`, ensuring it never packages a partially assembled iOS artifact.

## Verification

- Unit-test the packaging helper's input validation and archive layout with a fake `lipo` executable and temporary directories.
- Parse the modified workflow with a YAML parser available in the repository environment.
- Check that every matrix variant has a unique cache and artifact name, that simulator variants build both architectures, and that `package` depends on `assemble-ios`.
- Inspect the final diff and run the repository's applicable workflow-shape tests if they cover release workflows.
