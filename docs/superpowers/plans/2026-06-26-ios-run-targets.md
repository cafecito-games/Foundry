# iOS Run Targets Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make running a Godot game on a physical iPhone as discoverable and one-click as the in-editor Play button, by adding a platform-agnostic "run target" layer (data model, readiness doctor, run-bar dropdown, targets dock, project wizard) on top of the existing iOS export/`run()` machinery.

**Architecture:** A new `RunTarget` record (in `run_targets.cfg`) references an existing export preset and adds device/team/signing metadata. A `RunTargetManager` resolves targets and owns the active selection; a `RunTargetReadiness` "Doctor" probes prerequisites and emits an ordered, renderable step ladder; a `RunTargetPlatform` adapter wraps the existing iOS enumeration + `run()`. UI surfaces (run-bar selector, targets dock, project-manager wizard) render from these. Execution reuses the existing export-to-`.xcarchive` + `xcodebuild`/`devicectl` path, adding `-allowProvisioningUpdates` and routing failures through the Doctor's shared error vocabulary.

**Tech Stack:** C++ (Godot engine, editor module), SCons build, doctest unit tests, macOS shell tools (`xcrun`, `devicectl`, `xcodebuild`, `xcode-select`).

**Reference spec:** `docs/superpowers/specs/2026-06-26-ios-run-targets-design.md`

---

## File Structure

| File | Responsibility | New/Modify |
|---|---|---|
| `editor/run/run_target.h/.cpp` | `RunTarget` value type + `run_targets.cfg` load/save | New |
| `editor/run/run_target_manager.h/.cpp` | Owns targets, active selection, resolves target → (preset, device, flags) | New |
| `editor/run/run_target_platform.h` | `RunTargetPlatform` adapter interface (`probe_readiness`, `list_devices`, `run`) + `ReadinessStep` | New |
| `editor/run/run_target_readiness.h/.cpp` | Platform-agnostic Doctor: ordered ladder evaluation, shared error-vocabulary mapping | New |
| `editor/run/ios_run_target_platform.h/.cpp` | iOS adapter: wraps existing enumeration + `run()`; readiness probes via shell tools behind an injectable command seam | New |
| `editor/export/editor_export_platform_apple_embedded.cpp` | Extract enumeration + run logic into adapter-callable methods; add `-allowProvisioningUpdates` | Modify |
| `editor/run/editor_run_native.cpp/.h` | Upgrade deploy menu into the run-bar target selector | Modify |
| `editor/run/run_targets_panel.h/.cpp` | Dockable targets/signing/readiness panel | New |
| `editor/project_manager/.../*` | "Mobile (iOS)" project template seeding preset + target | Modify |
| `tests/editor/run/test_run_target.h` | RunTarget model + manager unit tests | New |
| `tests/editor/run/test_run_target_readiness.h` | Doctor parsing/ladder tests with captured fixtures | New |
| `tests/data/ios_run_targets/*.json` | Captured `devicectl`/`xcodebuild` fixtures | New |
| `docs/superpowers/plans/2026-06-26-ios-run-targets-manual-qa.md` | Manual device QA checklist | New |

---

## Task 0: RunTarget data model and `run_targets.cfg` persistence

**Goal:** A `RunTarget` value type that round-trips to/from a `run_targets.cfg` `ConfigFile` in the project.

**Files:**
- Create: `editor/run/run_target.h`, `editor/run/run_target.cpp`
- Test: `tests/editor/run/test_run_target.h`

**Acceptance Criteria:**
- [ ] `RunTarget` holds `name`, `platform`, `export_preset`, `device_id`, `signing_mode`, `team_id`.
- [ ] `RunTarget::load_all(path)` / `save_all(path, targets)` round-trip a list with no data loss, using `[target.N]` sections (mirrors `export_presets.cfg` style).
- [ ] Missing/empty `run_targets.cfg` loads as an empty list without error.
- [ ] Unknown keys in a section are preserved on save (forward-compat).

**Verify:** `./bin/godot.<plat>.editor.dev.<arch> --headless --test --test-suite="*RunTarget*"` → all pass.

**Steps:**
- [ ] Define the `RunTarget` struct and a `RunTargetStore` with static `load_all`/`save_all` using Godot's `ConfigFile`. Follow the section-iteration pattern in `editor/export/editor_export.cpp` (how `export_presets.cfg` is parsed).
- [ ] Write doctest cases: empty file → empty list; two-target round trip; unknown-key preservation. Assert field equality after save→load.
- [ ] Register the test header in `tests/test_main.cpp` (alphabetical include block).
- [ ] Run the suite; red → implement → green.
- [ ] Commit: `feat(editor): add RunTarget model and run_targets.cfg persistence`.

---

## Task 1: ReadinessStep contract and RunTargetPlatform adapter interface

**Goal:** Define the platform-agnostic adapter interface and the `ReadinessStep` output contract both UI surfaces render from.

**Files:**
- Create: `editor/run/run_target_platform.h`
- Test: covered indirectly via Task 3/fake adapter; add a minimal compile/contract test in `tests/editor/run/test_run_target.h`.

**Acceptance Criteria:**
- [ ] `ReadinessStep { StringName id; String title; String detail; Status status; String fix_hint; }` with `Status { OK, ACTION_NEEDED, BLOCKED }`.
- [ ] `RunTargetPlatform` abstract interface declares `Vector<ReadinessStep> probe_readiness(const RunTarget&)`, `Vector<RunTargetDevice> list_devices()`, `Error run(const RunTarget&, int debug_flags)`.
- [ ] A `FakeRunTargetPlatform` test double can be constructed and returns canned data (proves the abstraction is usable without iOS).

**Verify:** `--test-suite="*RunTarget*"` compiles and passes with the fake adapter.

**Steps:**
- [ ] Declare `ReadinessStep`, `RunTargetDevice` (id, name, readiness badge), and the pure-virtual `RunTargetPlatform`.
- [ ] Add `FakeRunTargetPlatform` in the test file returning two devices and a fixed ladder; assert the manager (Task 2) can consume it.
- [ ] Commit: `feat(editor): add RunTargetPlatform adapter interface and ReadinessStep contract`.

---

## Task 2: RunTargetManager — resolution and active-target selection

**Goal:** A manager that loads targets, owns the active target, resolves a target to its export preset + device + debug flags, and dispatches to the right platform adapter.

**Files:**
- Create: `editor/run/run_target_manager.h`, `editor/run/run_target_manager.cpp`
- Test: `tests/editor/run/test_run_target.h` (extend)

**Acceptance Criteria:**
- [ ] `resolve(target)` returns the linked `EditorExportPreset` (looked up by name) + device id + a debug-flags set including `DEBUG_FLAG_REMOTE_DEBUG`.
- [ ] Resolving a target whose `export_preset` no longer exists returns a clear error (no crash).
- [ ] `set_active_target` / `get_active_target` persist the active selection across reload.
- [ ] Manager selects the platform adapter by `target.platform` from a registry; unknown platform → graceful error.

**Verify:** `--test-suite="*RunTarget*"` → all pass, including the missing-preset case.

**Steps:**
- [ ] Implement the manager with an adapter registry (`HashMap<String, RunTargetPlatform*>`), seeded with the fake in tests and the iOS adapter at runtime (Task 4).
- [ ] Implement `resolve` against `EditorExport::get_singleton()` preset lookup; cover the deleted-preset path.
- [ ] Tests: resolve happy path with fake; deleted preset; active-target round trip.
- [ ] Commit: `feat(editor): add RunTargetManager with resolution and active-target selection`.

---

## Task 3: RunTargetReadiness Doctor — ladder evaluation and error vocabulary

**Goal:** A pure-detection Doctor that, given an adapter's raw probe results, produces the ordered readiness ladder and maps tool errors into the shared `ReadinessStep` vocabulary.

**Files:**
- Create: `editor/run/run_target_readiness.h`, `editor/run/run_target_readiness.cpp`
- Test: `tests/editor/run/test_run_target_readiness.h`
- Test data: `tests/data/ios_run_targets/{paired_device,devmode_off,no_team,bundle_id_taken}.json`

**Acceptance Criteria:**
- [ ] Ladder is evaluated in fixed order (Xcode → device → trust → Developer Mode → team → provisioning); the first non-OK step is `ACTION_NEEDED`/`BLOCKED`, earlier steps `OK`, later steps reported as upcoming.
- [ ] Parsing fixtures produces the expected step statuses: a Developer-Mode-off device fixture yields the Developer-Mode step as the blocking one; the no-team fixture yields the team step; etc.
- [ ] `map_provisioning_error(stderr_text)` converts a captured `xcodebuild` provisioning failure into the same step id/fix_hint the cold probe would emit (asserted both directions).
- [ ] No UI, no process side-effects: the Doctor consumes injected raw strings/structs only.

**Verify:** `--test-suite="*Readiness*"` → all pass.

**Steps:**
- [ ] Implement ladder evaluation taking an injected `ReadinessProbeResult` (booleans + raw tool output), not live shell calls.
- [ ] Capture real fixtures from a Mac (or hand-author representative JSON matching `devicectl list devices -j` shape) and commit under `tests/data/`.
- [ ] Implement `map_provisioning_error`; add the bidirectional equivalence test (cold-probe step == error-mapped step for bundle-id-taken).
- [ ] Register the test header in `tests/test_main.cpp`.
- [ ] Commit: `feat(editor): add RunTargetReadiness doctor with ladder and error mapping`.

---

## Task 4: iOS adapter — wrap existing enumeration and run()

**Goal:** An `IOSRunTargetPlatform` that implements the adapter by reusing the existing device enumeration and deploy code, behind an injectable command-runner seam so probes are testable.

**Files:**
- Create: `editor/run/ios_run_target_platform.h`, `editor/run/ios_run_target_platform.cpp`
- Modify: `editor/export/editor_export_platform_apple_embedded.cpp` (extract enumeration + run into callable methods; expose a `run_on_device(preset, device_id, flags)` entry not gated behind the legacy menu)

**Acceptance Criteria:**
- [ ] `list_devices()` returns the same devices the existing `#ifdef MACOS_ENABLED` poll produces (refactor, not reimplement).
- [ ] `probe_readiness()` runs the shell checks through an injectable `CommandRunner` interface; in tests a fake runner feeds the Task 3 fixtures.
- [ ] `run()` delegates to the extracted `run_on_device` path and passes `-allowProvisioningUpdates`.
- [ ] Existing iOS export behavior is unchanged when invoked through the legacy menu (no regression).

**Verify:** `--test-suite="*RunTarget*"` passes with the fake `CommandRunner`; manual: legacy remote-deploy menu still deploys.

**Steps:**
- [ ] In `editor_export_platform_apple_embedded.cpp`, extract the device-list parsing and the `run()` body into reusable methods that take explicit args; keep the existing `run()` as a thin wrapper calling them (no behavior change).
- [ ] Add the `-allowProvisioningUpdates` flag to the `xcodebuild`/`devicectl` invocation.
- [ ] Implement the adapter; define `CommandRunner` (real impl shells out; fake returns fixtures).
- [ ] Register the iOS adapter with `RunTargetManager` at editor startup (macOS only).
- [ ] Commit: `feat(editor): add iOS run-target adapter reusing export deploy path`.

---

## Task 5: Run-bar target selector

**Goal:** Replace the buried remote-deploy menu with a target selector beside the Play button that merges configured targets with live devices and shows readiness badges.

**Files:**
- Modify: `editor/run/editor_run_native.cpp`, `editor/run/editor_run_native.h`

**Acceptance Criteria:**
- [ ] Dropdown lists configured run targets + live-detected devices; an unconfigured connected device shows "Set up this device…".
- [ ] Each entry shows a readiness badge (ready / needs-step / busy) sourced from `RunTargetReadiness`.
- [ ] Selecting an entry sets the active target on `RunTargetManager`; the main Play button then deploys to it.
- [ ] Clicking Run on an unready target surfaces the Doctor's next step inline instead of failing silently.
- [ ] Badges refresh on the existing ~3s poll without UI jank.

**Verify:** Manual in editor: connect a device (or use a stub adapter), confirm it appears, select it, badge updates. Add a headless smoke test that the menu model is built from the manager's target/device list.

**Steps:**
- [ ] Route `editor_run_native` device enumeration through `RunTargetManager`/adapters instead of calling export platforms directly.
- [ ] Build menu items from targets + devices; attach badge icons mapped from `ReadinessStep::Status`.
- [ ] On select → `set_active_target`; on Run-unready → emit signal that opens the inline step (and offers "Open Targets panel").
- [ ] Commit: `feat(editor): surface run targets and devices at the Play button`.

---

## Task 6: Targets dock panel

**Goal:** A dockable panel to manage targets, edit signing/devices, and view the live readiness ladder.

**Files:**
- Create: `editor/run/run_targets_panel.h`, `editor/run/run_targets_panel.cpp`
- Modify: editor dock registration (follow an existing bottom/right dock registration pattern)

**Acceptance Criteria:**
- [ ] Targets list supports add/remove/rename; each target binds to an export preset (auto-create if the project has none for the platform).
- [ ] Signing & Devices region: team picker populated from detected teams, bundle-id field with live validity indicator, signing mode (automatic default).
- [ ] Readiness region renders the Doctor ladder: OK steps as green checks, the blocking step expanded with `fix_hint` and a **Recheck** button.
- [ ] Edits write through to `run_targets.cfg` (target/device/team) and the linked export preset (bundle id, signing).
- [ ] Opening the panel triggers an on-demand reprobe; **Recheck** reprobes a single target.

**Verify:** Manual: open panel, edit bundle id, confirm it persists to the export preset; toggle a fixture/stubbed readiness state and confirm the ladder re-renders.

**Steps:**
- [ ] Build the three-region `VBoxContainer` panel; bind list selection to the editor area.
- [ ] Wire writes to `RunTargetStore` and `EditorExportPreset` setters; debounce bundle-id validation.
- [ ] Render the ladder from `RunTargetReadiness`; wire Recheck → reprobe → refresh.
- [ ] Register as a dock and as the "Manage targets…" / "Set up this device…" target from Task 5.
- [ ] Commit: `feat(editor): add Run Targets dock with signing and readiness`.

---

## Task 7: New-project "Mobile (iOS)" template

**Goal:** A Project Manager option that seeds a new project with an iOS export preset and a default run target.

**Files:**
- Modify: project manager new-project dialog (renderer/option section) and project-creation code

**Acceptance Criteria:**
- [ ] A "Mobile (iOS)" choice appears alongside renderer options in new-project creation.
- [ ] Choosing it writes an `iOS` export preset with mobile defaults (portrait, correct renderer, placeholder bundle id derived from project name) and a `run_targets.cfg` with one `iOS Device` target (`signing_mode=automatic`, `device_id=auto`, empty `team_id`).
- [ ] No signing or device detection is attempted at create time.
- [ ] On first editor open of such a project, the Targets dock opens to the readiness ladder.

**Verify:** Manual: create a project with the option; confirm `export_presets.cfg` and `run_targets.cfg` contents; open editor and confirm the dock opens.

**Steps:**
- [ ] Add the option to the new-project UI; follow the existing renderer-selection wiring.
- [ ] On create, generate the preset + `run_targets.cfg` (derive bundle id: lowercase, strip non-alphanumerics → `com.example.<slug>`).
- [ ] Add a first-open flag the editor reads to auto-open the Targets dock.
- [ ] Commit: `feat(project-manager): add Mobile (iOS) project template seeding run target`.

---

## Task 8: Manual QA checklist

**Goal:** Document the irreducible real-device verification that CI cannot perform.

**Files:**
- Create: `docs/superpowers/plans/2026-06-26-ios-run-targets-manual-qa.md`

**Acceptance Criteria:**
- [ ] Step-by-step first-run script on a clean Mac + new iPhone: Trust, Developer Mode, Apple ID sign-in, team pick, first Run.
- [ ] Repeat-run script asserting one-click deploy with no prompts.
- [ ] Failure-path checks: bundle-id-taken, no team, Developer Mode off — confirm each maps to the correct readiness step in the panel.

**Verify:** A human follows the doc end-to-end and records pass/fail.

**Steps:**
- [ ] Write the checklist mirroring the readiness ladder and the spec's first-run bar.
- [ ] Commit: `docs: add iOS run-targets manual QA checklist`.

---

## Self-Review

- **Spec coverage:** data model (T0), adapter+contract (T1), manager (T2), Doctor (T3), iOS execution + automatic provisioning (T4), run-bar dropdown (T5), targets dock (T6), wizard (T7), testing/manual QA (T3/T8). All spec sections mapped.
- **Type consistency:** `RunTarget`, `ReadinessStep{id,title,detail,status,fix_hint}`, `RunTargetPlatform{probe_readiness,list_devices,run}`, `RunTargetManager{resolve,set_active_target}` used consistently across tasks.
- **Dependencies:** T1→T0; T2→T1; T3→T1; T4→T2,T3; T5→T2,T3,T4; T6→T2,T3,T4; T7→T0; T8→T3.
