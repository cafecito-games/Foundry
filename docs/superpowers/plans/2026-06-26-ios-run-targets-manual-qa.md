# iOS Run Targets — Manual QA Checklist (Real-Device Deploy)

**Status:** Manual verification script
**Date:** 2026-06-26
**Epic:** iOS Run Targets (#490) — Task 8 (#499). Depends on Task 3 (#494, RunTargetReadiness doctor).
**Reference spec:** `docs/superpowers/specs/2026-06-26-ios-run-targets-design.md`
**Reference plan:** `docs/superpowers/plans/2026-06-26-ios-run-targets.md`

## Purpose

CI cannot plug in an iPhone, enable Developer Mode, or sign in with an Apple ID,
so the irreducible real-device verification is documented here as a script a
human runs by hand. Unit tests already cover the readiness ladder, the error
vocabulary, and target/preset resolution against captured fixtures
(`tests/data/ios_run_targets/*.json`); this checklist verifies the parts those
tests cannot reach: the actual first-run Apple setup, true one-click repeat
deploys, and that real failures surface as the **correct** readiness step in the
Targets panel.

The failure-path expectations below quote the exact step identifiers, titles, and
fix hints emitted by `RunTargetReadiness` (`editor/run/run_target_readiness.cpp`),
so a tester can confirm the running editor matches the shipped doctor copy
verbatim.

## The Readiness Ladder (reference)

The doctor evaluates these rungs in fixed order. The first unsatisfied rung is
shown as `ACTION_NEEDED` with its fix hint; every later rung is reported as
`BLOCKED` ("Pending — resolve the step(s) above first."); earlier rungs are `OK`.

| # | Step id | Title | Fix hint shown on failure |
|---|---|---|---|
| 1 | `xcode` | Xcode toolchain | Install Xcode from the App Store, then run: `xcodebuild -runFirstLaunch` |
| 2 | `device` | Device connected | Plug in your iPhone over USB. |
| 3 | `trust` | Device trusted | Unlock your iPhone and tap Trust This Computer. |
| 4 | `developer_mode` | Developer Mode | On the device: Settings > Privacy & Security > Developer Mode > On, then reboot. |
| 5 | `team` | Apple ID / team | Sign in with your Apple ID in Xcode > Settings > Accounts, then pick your team here. |
| 6 | `provisioning` | Automatic signing | (depends on captured error — see Section C) |

## Environment Under Test

Record these before starting so a failure can be reproduced.

- [ ] Mac model and macOS version: ______________________
- [ ] Xcode version (`xcodebuild -version`): ______________________
- [ ] iPhone model and iOS version: ______________________
- [ ] Godot editor build (commit / `--version`): ______________________
- [ ] Apple ID type: free personal team / paid Apple Developer Program (circle one)
- [ ] Tester name and date: ______________________

---

## Section A — First Run on a Clean Mac + New iPhone

**Goal:** A user who has never deployed to a device reaches a running app through
the guided Apple steps, each surfaced by the doctor, with no out-of-band
instructions. Start from a Mac that has never run a device deploy and an iPhone
that has never trusted this Mac.

> Tip: to simulate "clean" without wiping a machine, sign the Apple ID out of
> Xcode (Settings > Accounts), and on the iPhone toggle Developer Mode off and
> use Settings > General > Transfer or Reset iPhone > Reset > Reset Location &
> Privacy to clear the trust prompt. Note in results if you simulated vs. used a
> genuinely fresh machine.

### A0. Project setup

1. [ ] Create or open a project that has the **Mobile (iOS)** template (or an iOS
   export preset and an `iOS Device` run target in `run_targets.cfg`).
2. [ ] Confirm the run-bar target selector (beside Play) lists the iOS target.

   **Pass:** the iOS target appears in the dropdown with a readiness badge.

### A1. Xcode toolchain (`xcode` step)

1. [ ] Open the Targets panel for the iOS target and read the Readiness ladder.
2. [ ] If Xcode is missing or its command-line tools are not selected, confirm the
   blocking step is **Xcode toolchain** (`xcode`) with fix hint
   *"Install Xcode from the App Store, then run: `xcodebuild -runFirstLaunch`."*
3. [ ] Install Xcode (App Store), run `xcodebuild -runFirstLaunch`, accept the
   license, then press **Recheck**.

   **Pass:** the `xcode` step turns green (`OK`) and the ladder advances to the
   next unsatisfied rung.

### A2. Connect the device (`device` step)

1. [ ] With no iPhone plugged in, confirm the blocking step is **Device
   connected** (`device`), fix hint *"Plug in your iPhone over USB."*
2. [ ] Plug the iPhone into the Mac over USB. The ~3s device poll should clear
   this step live without pressing Recheck.

   **Pass:** the `device` step turns green within a few seconds of plugging in;
   the device appears in the run-bar selector.

### A3. Trust This Computer (`trust` step)

1. [ ] On first connect, confirm the blocking step is **Device trusted** (`trust`),
   fix hint *"Unlock your iPhone and tap Trust This Computer."*
2. [ ] Unlock the iPhone; tap **Trust** on the "Trust This Computer?" prompt and
   enter the passcode.
3. [ ] Press **Recheck** (or wait for the poll).

   **Pass:** the `trust` step turns green once pairing completes.

### A4. Developer Mode (`developer_mode` step)

1. [ ] Confirm the blocking step is **Developer Mode** (`developer_mode`), fix hint
   *"On the device: Settings > Privacy & Security > Developer Mode > On, then
   reboot."*
2. [ ] On the iPhone: Settings > Privacy & Security > Developer Mode > **On**;
   reboot when prompted; after reboot confirm enabling Developer Mode.
3. [ ] Press **Recheck**.

   **Pass:** the `developer_mode` step turns green.

### A5. Apple ID sign-in and team pick (`team` step)

1. [ ] Confirm the blocking step is **Apple ID / team** (`team`), fix hint
   *"Sign in with your Apple ID in Xcode > Settings > Accounts, then pick your
   team here."*
2. [ ] In Xcode > Settings > Accounts, sign in with the Apple ID.
3. [ ] Back in the Targets panel, open the **Signing & Devices** region and pick
   the team from the team picker. Confirm the selection persists (it is saved to
   `run_targets.cfg` as `team_id`).
4. [ ] Press **Recheck**.

   **Pass:** the `team` step turns green; the picked team is remembered.

### A6. First Run (`provisioning` step + deploy)

1. [ ] With steps 1–5 green, the **Automatic signing** (`provisioning`) step should
   be the last rung. Press **Run** (or **Recheck**) to trigger an
   `-allowProvisioningUpdates` resolve.
2. [ ] On first signing, expect Apple/Xcode prompts (keychain access, profile
   creation). Approve them. The app should export, install, and launch on the
   iPhone.
3. [ ] Confirm the running app connects back to the editor's remote debugger
   (set a breakpoint or print and confirm it appears in the Output/Debugger).

   **Pass:** the app launches on the physical iPhone and the debugger is wired.
   Record the wall-clock time from pressing Run to the app appearing.

**Section A overall result:** PASS / FAIL — notes: ______________________

---

## Section B — Repeat Run (One-Click, No Prompts)

**Goal:** After the first successful run, every subsequent run is a single click
with no Apple prompts and no readiness steps to clear.

1. [ ] Keep the same iPhone connected and trusted. Make a trivial code change
   (e.g. change a label's text) so there is something to observe.
2. [ ] Confirm every readiness rung (`xcode`, `device`, `trust`, `developer_mode`,
   `team`, `provisioning`) is green in both the run-bar badge and the panel.
3. [ ] Click **Run** once.

   **Pass — all must hold:**
   - [ ] No Trust prompt.
   - [ ] No Developer Mode prompt.
   - [ ] No Apple ID / keychain / signing prompt.
   - [ ] No readiness step turns `ACTION_NEEDED`.
   - [ ] The changed app launches on the device and the debugger reconnects.
4. [ ] Unplug and replug the iPhone, then Run again.

   **Pass:** the device re-appears in the selector and a single click deploys with
   no prompts.
5. [ ] Repeat the one-click run 3 times in a row.

   **Pass:** all three are single-click, prompt-free deploys.

**Section B overall result:** PASS / FAIL — notes: ______________________

---

## Section C — Failure-Path Checks (each maps to the correct readiness step)

**Goal:** Induce each failure and confirm the Targets panel surfaces the **exact**
readiness step (id + title) and fix hint the doctor is specified to emit. For each
case, record the step id shown and whether it matches.

### C1. Bundle id taken (`provisioning` step)

1. [ ] Set the target's bundle id to one already registered to a different Apple
   account/team (or one known to be unavailable).
2. [ ] Run (or Recheck) to force an `-allowProvisioningUpdates` resolve; Xcode
   fails to register the identifier.

   **Pass:** the blocking step is **Automatic signing** (`provisioning`,
   `ACTION_NEEDED`) with:
   - detail: *"Xcode could not register the app's bundle identifier — it may
     already be taken."*
   - fix hint: *"Change the bundle id (e.g. `com.yourname.game`) in the target's
     signing settings, then recheck."*
3. [ ] Change the bundle id to a unique value (e.g. `com.<yourname>.<slug>`),
   Recheck.

   **Pass:** the `provisioning` step resolves and a run succeeds.

   Step id shown: ____________  Matches: YES / NO

### C2. No team / not signed in (`team` step)

1. [ ] Sign the Apple ID out of Xcode (Settings > Accounts), or clear the target's
   `team_id`. Recheck.

   **Pass:** the blocking step is **Apple ID / team** (`team`, `ACTION_NEEDED`)
   with fix hint *"Sign in with your Apple ID in Xcode > Settings > Accounts, then
   pick your team here."* — and the later `provisioning` step shows `BLOCKED`
   ("Pending — resolve the step(s) above first."), **not** a provisioning error.

   Step id shown: ____________  Matches: YES / NO

   > Distinguish from C1: a missing team must block at `team`, not at
   > `provisioning`. If a no-team state surfaces as an automatic-signing error
   > instead, that is a FAIL (the ladder skipped a rung).

2. [ ] Sign back in, pick the team, Recheck.

   **Pass:** the `team` step turns green and the ladder advances.

### C3. Developer Mode off (`developer_mode` step)

1. [ ] On the iPhone: Settings > Privacy & Security > Developer Mode > **Off**;
   reboot. Keep the device plugged in and trusted. Recheck.

   **Pass:** the blocking step is **Developer Mode** (`developer_mode`,
   `ACTION_NEEDED`) with fix hint *"On the device: Settings > Privacy & Security >
   Developer Mode > On, then reboot."* — and the `team`/`provisioning` rungs below
   it show `BLOCKED`, not their own failures.

   Step id shown: ____________  Matches: YES / NO

2. [ ] Re-enable Developer Mode, reboot, Recheck.

   **Pass:** the `developer_mode` step turns green.

### C4. (Optional) Device disconnected mid-session (`device` step)

1. [ ] While the target is selected, unplug the iPhone. Within ~3s the run-bar
   badge and panel should regress to the **Device connected** (`device`) step.
2. [ ] Click **Run** while disconnected.

   **Pass:** the run does not fail silently; the Doctor's next step (`device`,
   "Plug in your iPhone over USB.") is surfaced inline.

   Step id shown: ____________  Matches: YES / NO

**Section C overall result:** PASS / FAIL — notes: ______________________

---

## Sign-Off

- [ ] Section A (first run) — PASS / FAIL
- [ ] Section B (one-click repeat) — PASS / FAIL
- [ ] Section C (failure paths map to correct step) — PASS / FAIL

**Overall:** PASS / FAIL

Tester: ______________________  Date: ____________

File any defect as a new issue under epic #490, referencing the failing section
and the step id observed vs. expected.
