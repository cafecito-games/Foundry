# Adding a Run-Target Platform Adapter

This guide explains how to add a new platform to the **run-target layer** — the
abstraction that makes "run my game on a real device" as discoverable and
one-click as the in-editor Play button. The iOS adapter shipped first (epic
#490); this document shows how to drop in another platform (Android is used as
the running example) by implementing a single interface.

The whole point of the layer is that you write **one adapter class** and the rest
of the editor — the run-bar deploy dropdown, Run Targets configuration, the
readiness "doctor", target persistence — works for your platform with no further
changes.

## Architecture at a glance

```
                       ┌──────────────────────────────┐
   deploy dropdown ───▶│                              │
   (EditorRunNative)   │       RunTargetManager       │──▶ RunTargetPlatform
                       │  owns targets + active sel.   │      (your adapter)
   configuration UI ──▶│  dispatches by platform str.  │
   (RunTargetsPanel)   └──────────────────────────────┘            │
                                     ▲                              ▼
                              RunTarget records              RunTargetReadiness
                            (run_targets.cfg)                 (the "Doctor")
```

- **`RunTargetPlatform`** (`editor/run/run_target_platform.h`) — the interface you
  implement. Pure detection + deploy; no UI.
- **`RunTargetManager`** (`editor/run/run_target_manager.h`) — owns the project's
  targets and the active selection, resolves a target into deploy inputs, and
  dispatches to the adapter registered for the target's `platform` string. It
  never references a concrete platform.
- **`RunTargetReadiness`** (`editor/run/run_target_readiness.h`) — the "Doctor":
  pure logic that turns a `ProbeResult` of raw signals into an ordered, renderable
  ladder of `ReadinessStep`s. Reuse it, or model your own ladder on it.
- **`EditorRunNative`** (`editor/run/editor_run_native.cpp`) — the editor surface
  that constructs the manager, registers the built-in adapters, and renders the
  run-bar dropdown. This is where you register your adapter.
- **`RunTargetsPanel`** (`editor/run/run_targets_panel.cpp`) — the Run Targets
  configuration panel shown in the run-options modal. It consumes the manager
  generically; you do not normally touch it.

Every UI surface renders from the same data the adapter returns, so detection
logic is written **once** and never duplicated.

## The interface you implement

```cpp
class RunTargetPlatform {
public:
    // Detect prerequisite state for a target and return the ordered readiness
    // ladder. Pure detection: no UI, no run side-effects.
    virtual Vector<ReadinessStep> probe_readiness(const RunTarget &p_target) = 0;

    // Enumerate the devices this platform can currently deploy to.
    virtual Vector<RunTargetDevice> list_devices() = 0;

    // Optional: signing teams to offer in Run Targets configuration. Default
    // returns empty -> the panel falls back to a free-form team field.
    virtual Vector<SigningTeam> list_signing_teams() { return {}; }

    // Deploy and launch. p_debug_flags is a bitmask of
    // EditorExportPlatform::DebugFlags (e.g. DEBUG_FLAG_REMOTE_DEBUG).
    virtual Error run(const RunTarget &p_target, int p_debug_flags) = 0;

    virtual ~RunTargetPlatform() {}
};
```

Three methods are required (`probe_readiness`, `list_devices`, `run`); one is
optional (`list_signing_teams`). That is the entire contract.

### The data types you exchange

All defined in `editor/run/run_target_platform.h` and `editor/run/run_target.h`:

- **`RunTarget`** — a named record of *where* to run. It references an export
  preset **by name** (`export_preset`) rather than duplicating build config, plus
  `platform`, `device_id` (or `"auto"`), `signing_mode`, and `team_id`. Persisted
  to `run_targets.cfg`. Unknown keys are preserved on round-trip, so a newer
  editor's fields survive an older one.
- **`RunTargetDevice`** — `{ id, name, badge }`. `badge` is a one-glance
  `ReadinessStep::Status` for the dropdown; the full per-target ladder still comes
  from `probe_readiness`.
- **`ReadinessStep`** — `{ id, title, detail, status, fix_hint }`. `status` is
  `OK` / `ACTION_NEEDED` / `BLOCKED`. This is the unit the ladder renders.
- **`SigningTeam`** — `{ id, name }`. Only relevant if your platform has a
  signing-team concept (iOS does; Android does not — return empty).

## Step-by-step: an Android adapter

### 1. Create the adapter files

Add `editor/run/android_run_target_platform.h` and `.cpp`. No build wiring is
needed: `editor/run/SCsub` globs `*.cpp`, so new files are compiled
automatically.

```cpp
// editor/run/android_run_target_platform.h
#pragma once

#include "editor/run/run_target_platform.h"

// A seam to shell out through, mirroring the iOS adapter's CommandRunner. The
// production implementation runs the real tools (adb); tests inject a fake that
// returns captured fixtures, so probing and enumeration are unit-testable with
// no SDK and no connected device.
class AndroidCommandRunner;

class AndroidRunTargetPlatform : public RunTargetPlatform {
public:
    AndroidRunTargetPlatform();
    explicit AndroidRunTargetPlatform(AndroidCommandRunner *p_runner); // injected, not owned
    ~AndroidRunTargetPlatform();

    virtual Vector<ReadinessStep> probe_readiness(const RunTarget &p_target) override;
    virtual Vector<RunTargetDevice> list_devices() override;
    virtual Error run(const RunTarget &p_target, int p_debug_flags) override;
    // list_signing_teams() intentionally not overridden: Android has no team concept.

private:
    AndroidCommandRunner *command_runner = nullptr;
    bool owns_command_runner = false;
};
```

### 2. Wrap existing machinery — do not reimplement deploy

The iOS adapter is deliberately thin: `list_devices()` parses the same output the
export platform's poll thread already produces, and `run()` hands off to the
export platform's extracted deploy path. **Do the same for Android.** Your adapter
should be glue over the existing `platform/android` export + run code, not a
second deploy implementation. Concretely:

- `list_devices()` → enumerate via `adb devices` (or reuse whatever the Android
  export platform already polls), mapping each to a `RunTargetDevice`.
- `run()` → resolve `device_id` (`"auto"` picks a sensible default), then call the
  existing Android one-click deploy/run with the resolved export preset and the
  passed `p_debug_flags`.

### 3. Implement `probe_readiness` using the Doctor pattern

Keep detection pure and let the ladder logic live in one place. The cleanest
approach is to reuse `RunTargetReadiness`: gather raw booleans into a
`ProbeResult`-style struct, then map them to an ordered `Vector<ReadinessStep>`.
The first unsatisfied rung is `ACTION_NEEDED` (with a `fix_hint`); rungs after it
are `BLOCKED`; satisfied leading rungs are `OK`.

For Android the rungs differ from iOS (no provisioning), e.g.:

```
SDK/platform-tools present → device connected → USB debugging authorized → (deployable)
```

You can either (a) extend `RunTargetReadiness` with Android-aware steps and a
parser, following the iOS `parse_ios_probe` + `evaluate` split, or (b) build the
`Vector<ReadinessStep>` directly in your adapter if your ladder is simple. Either
way, **the evaluation must be pure** (no shelling out, no UI) so it is unit-
testable from captured fixtures — gather signals separately, decide the ladder
from data.

### 4. Make it testable: inject the command seam

Follow the iOS adapter's `CommandRunner` pattern exactly:

- A default runner shells out via `OS::execute`.
- A constructor overload takes an injected runner the adapter does **not** own.
- The pure parsers (`adb devices` output → device list, probe snapshot →
  readiness) are `static` so tests exercise them directly from fixtures.

This is what lets the iOS tests run headlessly with no Xcode; your Android tests
should run with no SDK.

### 5. Register the adapter in `EditorRunNative`

Register your adapter where the iOS one is, platform-gated. In
`editor/run/editor_run_native.cpp`:

```cpp
#ifdef ANDROID_DEPLOY_SUPPORTED // or whatever guard fits; iOS uses MACOS_ENABLED
    if (android_platform == nullptr) {
        android_platform = memnew(AndroidRunTargetPlatform);
        run_target_manager.register_platform("android", android_platform);
    }
#endif
```

and tear it down in the destructor (`unregister_platform("android")` +
`memdelete`). Add the `android_platform` member to `editor_run_native.h` next to
`ios_platform`. The manager **does not take ownership** — `EditorRunNative` keeps
the adapter alive for the manager's lifetime.

The `"android"` string here is the key: it must match the `platform` field of any
`RunTarget` that should dispatch to your adapter.

### 6. (Optional) Seed a project template

iOS ships a "Mobile (iOS)" new-project template
(`editor/project_manager/ios_project_template.cpp`) that pre-creates an export
preset and a half-configured `RunTarget` so the workflow is discoverable on first
open. If you want the same for Android, mirror that file: derive sensible
defaults (e.g. an application id), write an `export_presets.cfg` Android preset
and a `run_targets.cfg` target with `platform = "android"`, and wire a radio
button in `editor/project_manager/project_dialog.cpp`. This is optional — adapters
work without a template; users just add a target manually in Run Targets
configuration.

### 7. Add tests

Create `tests/editor/run/test_android_run_target_platform.h` with doctest cases
over your static parsers and `probe_readiness` (driven by fixtures, not a live
device), then register it by adding an `#include` to `tests/test_main.cpp`
alongside the existing `tests/editor/run/test_*.h` includes. Store any captured
tool output under `tests/data/` like the iOS `tests/data/ios_run_targets/`
fixtures.

## Build and test

```sh
# Build (macOS shown; use platform=linuxbsd on Linux).
scons platform=macos target=editor dev_mode=yes tests=yes

# Run the run-target tests. These cases use no TEST_SUITE, so filter by case:
./bin/godot.* --headless --test --test-case="*RunTarget*,*AndroidRunTarget*"
```

## Checklist and gotchas

- **Dispatch key.** The string you pass to `register_platform("...")` must equal
  the `RunTarget::platform` value. Mismatch → `resolve()` returns
  `ERR_UNAVAILABLE` and nothing runs.
- **Use the shared manager.** `EditorRunNative` owns the live `RunTargetManager`
  used by the deploy dropdown. Configuration surfaces should prefer
  `EditorRunNative::get_singleton()->get_run_target_manager()` when available and
  handle null while the editor is still starting or in headless tooling.
- **Keep detection pure.** `probe_readiness` and any ladder logic must not touch
  the UI or run anything. Gather raw signals, decide from data — that is what
  makes it testable and keeps the dropdown and configuration panel in sync.
- **Wrap, don't reimplement.** Reuse the platform's existing export/run path.
  `run()` is glue, not a second deploy engine.
- **`device_id == "auto"`.** Resolve `"auto"`/empty to a concrete device the same
  way `list_devices()` orders them, so the dropdown badge, readiness, and the
  actual run all pick the same device.
- **Ownership.** The manager does not own adapters. Construct in
  `EditorRunNative`'s constructor, `memdelete` in its destructor, and
  `unregister_platform` first.
- **`-Werror=shadow`.** Linux CI compiles with `-Wshadow -Werror`; a local macOS
  build does **not** catch it. Avoid local names that shadow inherited members in
  UI code; use specific names such as `title_label`. This has failed CI before.
- **Signing teams are optional.** Only override `list_signing_teams()` if your
  platform actually has a team concept; otherwise the default empty list gives
  Run Targets configuration a free-form field, which is correct for Android.

## Reference files

| Concern | File |
|---|---|
| The interface | `editor/run/run_target_platform.h` |
| Target record + persistence | `editor/run/run_target.{h,cpp}` |
| Manager / dispatch / resolution | `editor/run/run_target_manager.{h,cpp}` |
| Readiness Doctor | `editor/run/run_target_readiness.{h,cpp}` |
| Reference adapter (iOS) | `editor/run/ios_run_target_platform.{h,cpp}` |
| Registration + run-bar dropdown | `editor/run/editor_run_native.{h,cpp}` |
| Run Targets configuration panel | `editor/run/run_targets_panel.{h,cpp}` |
| Project template (optional) | `editor/project_manager/ios_project_template.{h,cpp}` |
| Design spec | `docs/superpowers/specs/2026-06-26-ios-run-targets-design.md` |
