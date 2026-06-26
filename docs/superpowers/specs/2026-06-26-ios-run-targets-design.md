# iOS Run Targets — Design Spec

**Status:** Approved design, pending implementation
**Date:** 2026-06-26
**Scope:** iOS vertical now; architecture generalizes to Android/desktop later

## Problem

Godot already has a working iOS one-click deploy path on macOS
(`editor/export/editor_export_platform_apple_embedded.cpp` `run()` — enumerates
devices via `xcrun devicectl` / `ios-deploy`, exports a `.xcarchive`, installs,
launches, and wires the remote debugger). The capability exists; the **product
around it does not**. It is undiscoverable (buried in a separate "remote deploy"
menu most users never open — an experienced Godot user did not know it existed),
and the surrounding setup (export presets, signing/provisioning, Xcode toolchain)
is unguided. The goal: make running a game on a physical iPhone **as easy and
discoverable as the in-editor Play button**.

## Goals

- Make iOS a first-class, discoverable **run target** at the Play button.
- Reduce first-run friction to the irreducible Apple steps, each clearly
  explained; every run after the first is one click.
- Reuse the existing export/`run()` machinery — orchestrate, don't rebuild.
- Bake in a platform-agnostic "run target" abstraction so Android (and desktop)
  drop in later via a single adapter, without rework.

## Non-Goals

- Reimplementing code signing (no direct App Store Connect API integration).
  Signing is delegated to **Xcode automatic signing**.
- Cross-platform iOS deploy. iOS work remains macOS-only (Xcode required).
- Fully headless/zero-prompt Apple auth. The irreducible Apple steps (Trust,
  Developer Mode, Apple ID sign-in) stay manual but guided.
- Android implementation (this spec only ensures the abstraction admits it).

## Signing Strategy

Delegate to Xcode automatic signing. Godot orchestrates `xcodebuild`/`devicectl`
and passes `-allowProvisioningUpdates`; Apple's own tooling creates/refreshes
certificates and provisioning profiles and registers the device (the same
machinery that "just works" in Xcode after signing in with an Apple ID). This
also covers the free personal-team / 7-day path automatically. Godot never
automates Apple ID auth; it detects missing prerequisites and guides the user.

## First-Run Bar

Guided-but-manual one-time setup. Acceptable first run: plug in iPhone → Godot
detects it → the Targets panel walks through the unavoidable Apple steps (Trust
This Computer, enable Developer Mode, sign in with Apple ID, pick team) with live
status/checkmarks → click Run. Every subsequent run is one click. Godot's job:
detect state, explain exactly what is wrong, hand off cleanly — not hide Apple.

## Architecture

A lightweight, platform-agnostic **Run Target** layer sits *on top of* the
existing export-preset + `run()` machinery. Nothing in the existing deploy path
is replaced.

### Data model — `RunTarget`

A small named record stored in a new `run_targets.cfg` (parallel to
`export_presets.cfg`). It **references** an export preset rather than duplicating
its fields; the export preset stays the source of truth for build config.

```
[target.0]
name = "My iPhone"
platform = "ios"
export_preset = "iOS"        # links to an existing export preset
device_id = "00008..."        # last-selected device, or "auto"
signing_mode = "automatic"    # delegated to Xcode
team_id = "ABCDE12345"        # selected once, remembered
```

### Components (platform-agnostic core + per-platform adapters)

| Component | Responsibility | Location |
|---|---|---|
| `RunTargetManager` | Load/save targets, resolve target → (preset, device, flags), own the active target | `editor/run/` (new) |
| `RunTargetReadiness` (Doctor) | Detect prerequisite state for the active target; emit ordered, actionable steps | `editor/run/` (new) + per-platform probe |
| Run-bar dropdown | Surface targets + live devices at the Play button; selection sets active target | extends `editor/run/editor_run_native.cpp` |
| Targets panel (dock) | Configure targets, signing, devices; embed the Doctor's guidance | `editor/` (new dock) |

### Per-platform adapter interface

`RunTargetPlatform` abstraction exposing `probe_readiness()`, `list_devices()`,
and `run(target)`. The iOS adapter wraps the existing
`EditorExportPlatformAppleEmbedded` device-enumeration + `run()` code; that code
is refactored so the adapter can invoke it without going through the legacy
"remote deploy" menu path. Android later supplies its own adapter.

**Key principle:** the Doctor (detection/guidance) is separate from execution
(the existing `run()`). The Doctor only reports state; `run()` does the work.
This isolates the hard "is it set up?" logic and makes it independently testable.

## The Readiness Doctor

Given the active iOS target, determine exactly what stands between the user and a
running app, and say so in plain language with a clear next action. It detects,
explains, and re-checks — it never performs Apple's steps.

### Readiness ladder (checked in order; first failure is surfaced, rest shown as upcoming)

| # | Check | Detection | Guidance on failure |
|---|---|---|---|
| 1 | Xcode toolchain present | `xcode-select -p`, `xcrun --version` | Install Xcode from the App Store, then `xcodebuild -runFirstLaunch`. |
| 2 | A device is connected | existing `devicectl list devices` enumeration | Plug in your iPhone over USB. (clears live on plug-in) |
| 3 | Device paired/trusted | `devicectl` pairing state (already parsed) | Unlock your iPhone and tap **Trust This Computer**. |
| 4 | Developer Mode enabled | device `developerModeStatus` field | Settings → Privacy & Security → Developer Mode → On, then reboot. |
| 5 | Apple ID / team available | `xcodebuild -showsdks` + usable signing team on the generated project | Sign in with your Apple ID in Xcode → Settings → Accounts, then pick your team here. |
| 6 | Bundle id + automatic signing resolve | dry `xcodebuild -allowProvisioningUpdates` resolve | Xcode couldn't create a profile — your bundle id may be taken; try changing it here. |

### Output contract

The Doctor returns an ordered list of
`ReadinessStep { id, title, detail, status: ok|action_needed|blocked, fix_hint }`.
The run-bar and the panel both render from this single list — no duplicated
logic.

### Re-check cadence

Re-probes on the existing ~3s device-poll timer and on demand when the panel is
open, so checkmarks light up live. Each step exposes a **Recheck** affordance.

### Isolation / testability

The Doctor is pure detection: it shells out, parses, returns the step list — no
UI, no run side-effects. Unit-testable by feeding canned `devicectl`/`xcodebuild`
output and asserting the emitted list. iOS-specific probing lives behind the
adapter so Android can supply its own ladder. Mechanism: shell out to
`xcrun`/`devicectl`/`xcodebuild` and parse output (no linking Apple frameworks).

## Editor UX Surfaces

### A. Run-bar target selector (the discoverability fix)

A target selector immediately beside Play, upgrading the existing deploy menu
into the primary surface:

```
[ ▶ Run ]  [ 🖥 My Mac ▾ ]
                 ├─ 🖥  My Mac (desktop)
                 ├─ 📱  My iPhone                    ✓ ready
                 ├─ 📱  iPad (Developer Mode off)    ⚠
                 ├─ ──────────────
                 └─ ⚙  Manage targets…
```

- Merges configured targets with live-detected devices. A connected-but-
  unconfigured device appears and offers "Set up this device…" → opens the panel
  pre-filled.
- Each entry shows a one-glance readiness badge (✓ ready / ⚠ needs a step /
  ⤓ busy) from the Doctor.
- Selecting a target makes it active; the normal Play button deploys there.
- Clicking Run on an unready target expands the Doctor's next step inline rather
  than failing silently.
- Implemented by extending `editor/run/editor_run_native.cpp` (already owns
  device enumeration and the deploy menu).

### B. Targets panel (the config + troubleshoot hub)

A dockable panel (live, glanceable readiness — not a modal Project Settings
page). Opened from "Manage targets…". Three regions:

1. **Targets list** — add/remove/rename targets, each bound to an export preset
   (auto-created if none).
2. **Signing & Devices** — team picker (from detected Apple IDs/teams), bundle id
   field with live validity, signing mode (automatic, default). A friendly face
   over the export preset's otherwise-buried signing fields.
3. **Readiness** — renders the Doctor's step ladder for the selected target:
   green checks for satisfied steps, the current blocking step expanded with its
   fix hint and a **Recheck** button.

The panel writes through to both `run_targets.cfg` (target/device/team) and the
linked export preset (bundle id, signing). No new build config is invented.

## New-Project Wizard

Lives in the **Project Manager**. Add a **"Mobile (iOS)"** option alongside the
renderer choices. Picking it seeds the project so the target is half-configured
before the editor opens:

- Creates an `iOS` export preset with mobile-sane defaults (portrait, correct
  renderer, placeholder bundle id derived from project name, e.g.
  `com.example.myproject`).
- Creates a default `RunTarget` "iOS Device" bound to that preset,
  `signing_mode = automatic`, `device_id = auto`, empty `team_id` (filled on
  first run via the Doctor).
- On first editor open, the Targets dock opens to the Readiness ladder, landing
  the user on "here's what's left to run on your phone."

Deliberately minimal: no signing attempts, no device detection at create time
(no device is plugged in then). It only removes the "build an export preset from
scratch" barrier.

## Run Execution (reuse, don't rebuild)

When Play fires on an iOS target:

1. `RunTargetManager` resolves target → (export preset, device id, debug flags
   incl. `REMOTE_DEBUG`).
2. Hand off to the refactored iOS adapter, which calls the **existing**
   export-to-`.xcarchive` + `xcodebuild`/`devicectl` install + launch path in
   `editor_export_platform_apple_embedded.cpp` — now invoked with
   `-allowProvisioningUpdates` so automatic signing resolves certs/profiles/
   device registration.
3. The existing remote-debug arg wiring (`--remote-debug tcp://host:port`,
   breakpoints) connects the running app back to the editor debugger — unchanged.

The only execution change versus today: enabling automatic provisioning and
surfacing failures **through the Doctor** (parsed into readiness steps) instead
of a raw error log. Run path and Doctor share one error vocabulary.

## Testing Strategy

Isolate pure logic from device/Xcode I/O so the bulk is testable without a
device.

- **Doctor / readiness parsing (priority):** make the shell-out an injectable
  seam; unit-test the parser with captured real fixtures (paired device,
  Developer-Mode-off device, no-team, bundle-id-taken). doctest C++ tests under
  `tests/`, no device needed.
- **RunTarget model:** round-trip load/save of `run_targets.cfg`, target→preset
  resolution, active-target selection, graceful handling of a target whose linked
  preset was deleted.
- **Adapter interface:** a fake `RunTargetPlatform` proves manager/dropdown/panel
  work against the abstraction with no real iOS (also validates Android drop-in).
- **Error-vocabulary sharing:** a simulated `xcodebuild` provisioning failure
  maps to the same readiness step the Doctor emits from a cold probe (asserted
  both directions).
- **Manual/QA checklist (irreducible):** real-device first-run on a clean
  machine — Trust / Developer Mode / Apple ID guided steps, then one-click repeat
  runs. Documented as a manual QA script; CI cannot plug in an iPhone.

No attempt to mock Xcode end-to-end or test real signing in CI.

## Key Existing Code References

- Interface contract: `editor/export/editor_export_platform.h:326-352`
- Editor deploy UI: `editor/run/editor_run_native.cpp:46-172`
- iOS enumeration + `run()`: `editor/export/editor_export_platform_apple_embedded.cpp:2303-2797`
- Android one-click deploy (reference pattern): `platform/android/export/export_plugin.cpp:2339-2602`
- Remote debug settings: `network/debug/remote_host`, `network/debug/remote_port`
