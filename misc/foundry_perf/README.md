# Foundry editor performance: macOS verification kit

This directory contains everything needed to **build and verify the editor performance
optimizations on macOS**. The implementation lives in the branch these files ship on; a macOS
agent (or you) only needs to check out the branch, build, and run the checks below.

> Status: the Foundry Script load optimizations (#760) and the **Linux** `inotify` watcher (#762)
> are already merged to `develop`. This branch adds the **macOS FSEvents backend** (plus the
> cross-platform refactor of the watcher) and needs macOS build + runtime verification.

## What was optimized

1. **Lazy Foundry Script documentation generation** — `FoundryScript::reload()` no longer runs
   `FSDocGen::generate_docs`; docs are generated lazily on first `get_documentation()`.
2. **Reuse reload's analyzed tree in `update_exports`** — removes a redundant re-parse + re-analyze.
   - (1) and (2) are platform-agnostic. On Linux they roughly halved cold script/scene open.
3. **Editor filesystem directory watcher** — `EditorFileSystem::scan_changes()` runs on every
   editor focus-in and stats every tracked file (O(number of files)). A directory watcher lets it
   **skip** that rescan when nothing changed. Linux uses `inotify`; **macOS uses FSEvents**.

> The macOS FSEvents backend (`editor/file_system/editor_file_system.cpp`, `#elif defined(__APPLE__)`)
> was written in a Linux-only environment and has **not** been compiled or run on macOS yet. That is
> what this kit is for.

## Build (macOS)

```sh
scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)
# CI-like (warnings-as-errors): add dev_mode=yes
```
Binary: `bin/foundry.macos.editor.dev.<arch>` (`arm64` or `x86_64`). The build links the
`CoreServices` framework (added in `platform/macos/detect.py`). If the FSEvents code fails to
compile, fix the API usage in the `#elif defined(__APPLE__)` block of `editor/file_system/editor_file_system.cpp`
(check whether the macOS editor TU is built with ARC — if so, drop the `dispatch_release` calls).

## Verify

### 1. Automated tests
```sh
./bin/foundry.macos.editor.dev.* --headless --test --force-colors
```
Expect `[doctest] Status: SUCCESS!` (leaked-instance warnings at exit are expected/non-blocking).
Includes the regression test `Docs are generated lazily on request after reload()`.

### 2. Headless watcher measurement (hard numbers)
Focus-in doesn't happen headless, so apply the temporary instrumentation, rebuild, and run:
```sh
git apply misc/foundry_perf/headless_scan_harness.patch
scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)

python3 misc/foundry_perf/gen_watch_project.py /tmp/proj_large 300 25   # ~7,500 files

# Idle: expect one mode=SCAN then several mode=SKIP
FOUNDRY_PERF=1 FOUNDRY_PERF_TRIGGER_SCAN=5 \
  ./bin/foundry.macos.editor.dev.* --headless --editor --path /tmp/proj_large 2>&1 | grep '\[PERF\]\[scan_changes\]'

# Activity: touch a temp file before each scan -> expect mode=SCAN every time
FOUNDRY_PERF=1 FOUNDRY_PERF_TRIGGER_SCAN=5 FOUNDRY_PERF_TOUCH=1 \
  ./bin/foundry.macos.editor.dev.* --headless --editor --path /tmp/proj_large 2>&1 | grep '\[PERF\]\[scan_changes\]'

git checkout -- editor/file_system/editor_file_system.cpp   # revert instrumentation; DO NOT commit it
```
Reference (Linux, ~8.2k files): idle → first `mode=SCAN` (~54 ms) then `mode=SKIP`; activity → every
scan is `mode=SCAN`. macOS should show the same pattern (absolute ms will differ).

### 3. Manual GUI test (the key macOS-only check)
```sh
./bin/foundry.macos.editor.dev.* --path /tmp/proj_large --editor
```
- **Detection:** with the editor open, edit + save a `.fs` script from another app (or
  `echo >> /tmp/proj_large/scripts/d0000/s000.fs` in Terminal), then click back into Foundry.
  Confirm the change is picked up (FileSystem dock refreshes / script reloads). This proves the
  watcher does **not** wrongly skip real changes.
- **Responsiveness:** with no changes, switch away and back to Foundry repeatedly on the large
  project; confirm no per-focus hitch (the scan is skipped).
- **Opt-out:** toggle `Editor Settings → docks/filesystem/use_directory_watcher` off and confirm it
  falls back to the normal scan.

### (Optional) load-path benchmark for optimizations (1) and (2)
```sh
python3 misc/foundry_perf/gen_project.py /tmp/proj_med 1000 100 20 50
cp misc/foundry_perf/bench.fs /tmp/proj_med/
PERF_LABEL=med ./bin/foundry.macos.editor.dev.* --headless --path /tmp/proj_med -s res://bench.fs 2>&1 | grep '\[PERF\]'
```
Compare `script_cold` / `flat_cold` to a build of `develop` to confirm the cold-open win holds on macOS.

## Correctness invariants (do not break when fixing the macOS backend)
- Skip the scan **only** when the watcher is healthy **and** reports zero events since the last scan.
- Call `FSEventStreamFlushSync` before reading the dirty flag (closes the latency race).
- Any dropped-event flag, stream-start failure, or disabled setting ⇒ unhealthy ⇒ full scan. A
  broken watcher must only ever cause an *unnecessary* scan, never a *missed* change.
- The dirty flag is set from the FSEvents dispatch-queue thread ⇒ it must stay a `SafeFlag`.

## Files
- `README.md` — this guide
- `gen_watch_project.py` — large project generator for the watcher test
- `headless_scan_harness.patch` — temporary env-gated instrumentation (apply → test → revert; never commit)
- `gen_project.py`, `bench.fs` — load-path (optimizations 1 & 2) benchmark
