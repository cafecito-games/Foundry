# Godot 4.7 → Foundry Port — Session Handoff

**Read this first, then `2026-07-07-godot-4.7-port-catalog.md` (the spec) for background.**
This document lets a fresh session continue porting Godot 4.7 into Foundry with no ramp-up.

---

## UPDATE 2026-07-09 — PR #1144 MERGED; waves 9/10/11 done (read this first)

The original batched PR **#1144 merged to develop** (merge `3228291573`). Follow-up waves also landed:
- **Wave 9** (non-editor closeout) — merged as **#1164** (`ported-log-wave9.json`).
- **Wave 10** (editor bugfixes: inspector/settings/export/gui/themes) — merged as **#1166**
  (`triage/wave10/`, `ported-log-wave10.json`).
- **Wave 11** (editor bugfixes: scene/docks/animation/debugger/run/editor_node/…) — THIS branch
  `feature/godot-4.7-port-w11`, off merged develop. Triaged 401 untriaged editor candidates → 145
  port-now (6 parallel agents, `triage/wave11/`), **127 net bugfix ports** (110 clean + 17
  conflict-resolved), 13 skipped (12 already-present, 1 absent-feature), **5 reverted** during
  build/test validation (`ported-log-wave11.json`): #117923/#120063/#116159 referenced absent
  Node3DEditor members (Follow-Selection / trackball / gizmo-highlight); #119721 (`_update_tab_titles`
  on theme change) SIGSEGV'd the fork's diverged tile scene-tabs; #101769 (skip-inspector-update-when-
  hidden) broke the editor-automation acceptance workflow. Two adaptations kept: #114448 `replace_node`
  bind arg-count, #119508 run-guard `project.godot`→`project.foundry`. Strict `dev_mode` build clean,
  full suite **3069 passed / 0 failed / 3 skipped**.

**Lesson reinforced:** several picks *applied textually but failed at build/runtime* on fork-diverged
surfaces (Node3DEditor, tile scene-tabs, inspector visibility, `project.foundry`). Triage grep-verify
is necessary but not sufficient — always run the strict build AND the full suite (incl. the
`DISPLAY=:1` editor-automation subprocess tests) before declaring a wave done.

The sections below (§0–§8) predate the #1144 merge and describe that PR's finish; they remain accurate
for pipeline/tooling/gotchas. Remaining candidate mass is still the `editor/*` set minus what waves
9–11 consumed — regenerate the untriaged list by excluding all `ported-log*.json` + `triage/**/out-*.json`
PRs from `catalog.json` (see the wave-11 generator approach).

---

## 0. ACTIVE TASK — PR #1144 status + how to continue (read this first)

The port lives on branch **`feature/godot-4.7-port`**, draft **PR #1144 → develop**
(https://github.com/cafecito-games/Foundry/pull/1144). Waves 1–8 are done (§2). **Strategy: this PR
can be merged as-is once green, and the remaining 4.7 work continues in a NEW session/PR** — all
state needed to resume is on disk (see §3 + §6). State as of **2026-07-09**:

- **Reconcile with develop #1147 DONE.** develop's authoritative cross-platform fixes were merged
  (merge commit `63461bfe9c`); my 4 overlapping duplicate fixes were reverted first so #1147 applied
  clean. The unique **Windows-regression fix** in `platform/windows/display_server_windows.cpp`
  (`DisplayServerEnums::`→bare enums, PR 117748 leftover) is kept.
- **Wave 8 DONE** (port-later re-triage + cherry-picks — §2 wave-8). Strict `dev_mode` editor build
  clean, full suite **3038 passed / 0 failed / 3 skipped**, `template_release` build clean.
- **Every-platform matrix blocker FIXED.** The `Check every platform` run (workflow
  `.github/workflows/pr_platform_checks.yml`) was red on **all template builds** (Web/iOS/macOS/
  Windows-mingw/Android) due to a **develop-owned** template-only `-Werror` break:
  `main/main.cpp:229 projectless_editor_shell` was file-scope but only read under `TOOLS_ENABLED`, so
  templates tripped `-Wunused-variable`. **Fixed** by scoping it into the `TOOLS_ENABLED` block
  (commit `218b496ed3`); verified with a local `scons platform=macos target=template_release
  dev_mode=yes tests=no` build. Editor builds never hit it; Windows/MSVC template passed (MSVC
  doesn't flag it) — a clang/gcc-only gap, same class as the earlier shadow fixes. **A develop
  backport PR was filed** so develop is fixed at the source (this branch's copy then no-ops on the
  next develop merge).

### Finish steps for PR #1144 (do these next)
1. **Push** the branch (wave-8 commits + the `main.cpp` fix are committed locally, tree clean).
2. **Re-run the matrix:** comment **`Check every platform`** on #1144 (a push alone does NOT
   re-trigger the comment workflow). macOS-template builds clean locally now; Web/iOS/Android/
   Windows-mingw templates may reveal their OWN breaks that were masked behind the shared
   `main.cpp:229` error — the matrix is the only way to see those. Triage each: `gh api
   repos/cafecito-games/Foundry/actions/jobs/<id>/logs | sed 's/\x1b\[[0-9;]*m//g' | grep '##\[error\]'`;
   `git diff origin/develop HEAD -- <file>` tells regression (fix here) vs pre-existing develop bug
   (fix here to unblock + backport to develop).
3. **Mark ready:** once green, `gh pr ready 1144` (undraft).
4. Android builds LOCALLY on macOS for fast iteration: `export ANDROID_HOME=~/Library/Android/sdk;
   scons platform=android target=template_debug dev_mode=yes swappy=no arch=arm64 --keep-going`
   (also `arch=arm32`, `target=editor`). Windows/iOS/Web can't build locally — use the matrix.
   **template_release/template_debug builds need `tests=no`** (dev_mode implies tests=yes, which is
   incompatible with the stripped FS front-end).

### Continuing 4.7 after this PR merges (new session / new PR)
The biggest untriaged mass is **`editor/*` (~662 candidate PRs)** — see §6/§8. Also pending: the
**74 wave-8 "feature-decision" (B) items** (`retriage/CONSOLIDATED.json`, need a product yes/no),
the **6 wave-8 conflict-deferred items** (§2 wave-8, `retriage/wave8-deferred.txt`), and the
**wave-1 port-later** remainder. Start a new branch off the merged develop and follow §4 A→G.

### Deferred (not blocking the PR)
- **HDR/EDR output** — intractable without first porting `b8389cc76b`, a 65-file `renderer_rd` HDR
  core (compositor/scene-render/viewport/tonemapper + color shaders + an absent `RenderingDevice`
  ColorSpace/HDR-output API). A separate rendering-pipeline effort; see §2 wave-5 tail. Note: wave-8
  re-triage found PR #119237 ("vulkan-improve-errors") actually pulls in this HDR swapchain API, so
  it is HDR-blocked too, not a quick fix.

---

## 1. Mission & locked scope decisions

Port as much of the Godot `4.6.3-stable..4.7-stable` delta into Foundry as is compatible with
the fork, batching clear wins onto **one staging branch** `feature/godot-4.7-port`.

Locked decisions (do not re-litigate):
- **One batched staging branch**, not per-item PRs.
- **Diverged areas are flagged, not auto-ported:** anything touching `modules/gdscript`
  (→ Foundry Script), `editor/script/`, or `editor/plugins/script_*`. Cataloged as
  `manual-flag`; port only by hand later if ever.
- **Asset Store: skip entirely** (already being removed on branch `remove-assetlib`).
- **`doc/classes` sync: deferred** (heavy rebrand collision, low functional value).
- **Never force a messy port.** Conflicts that expose real divergence → skip and record.
- Foundry is a **clean-break, no-backwards-compat** fork: strip deprecated/compat shims that
  ported PRs add (keep the extension-API removal record though).

`4.6.3-stable` is a **clean ancestor** of develop, so `git cherry-pick -x -m 1 <merge-sha>`
of upstream PR merges is the porting primitive.

---

## 2. Current state (as of this handoff)

- **Branch `feature/godot-4.7-port`** pushed to origin, rebased on **latest develop**.
- **Waves 1–4 done.** Build green under strict `dev_mode=yes` (warnings-as-errors, macOS
  arm64). **Full suite: 3016 passed / 0 failed / 3 skipped** after every wave.
- Worktree: `/Users/christian/CafecitoGames/Foundry/.worktrees/godot-4.7-port`.
- The `godot` remote + tags `4.6.3-stable` / `4.7-stable` are fetched in the shared repo.

**Wave 1** = the 482 stock-adjacent candidate PRs in core, servers, drivers, scene runtime,
and self-contained modules (jolt/gltf/gridmap/openxr/visual_shader). Low-risk (275→106 net)
and med-risk (32→25 net) port-now sets landed + conflict resolution. 162 PRs + `deinit_ref`.

**Wave 2 = `scene/gui` (122 candidates).** Triaged (49 port-now / 47 port-later / 26 skip).
**45 PRs landed** (40 clean cherry-picks + 5 hand-resolved conflicts; 4 conflicts were
already-present no-ops). Blockers behind the port-later set: the Control max-size feature
(PR 116640), PopupMenu search-bar, BaseButton multitouch, the AccessibilityServer refactor
(PR 116839), and diverged Tree/RTL reworks. See `triage/wave2/`.

**Wave 3 = `platform/*` (142 candidates).** Triaged by OS (android/linuxbsd/windows/macos).
**23 PRs landed** (13 clean + 10 hand-resolved; 14 already-present/absent no-ops).
**android contributed 0** — its tree is fully rebranded (`org.godotengine`→`games.cafecito`,
`java_godot_*`→`java_foundry_*`) and the good self-contained fixes were already backported.
Caveat: only macOS + shared code is compiled here, so android/windows/linux/iOS ports are
**not build-verified** (notably a hand-adapted `crash_handler_linuxbsd.cpp` hunk). See
`triage/wave3/`.

**Wave 4 = `dep-bump` (21).** 11 leaf-lib bumps cherry-picked clean (harfbuzz, ufbx×2, glad,
tinyexr, dr_mp3, libjpeg-turbo, re-spirv×2, 2 SDL controller fixes). Of the 10 "conflicts":
4 were already-satisfied (fork develop already at 4.7 for freetype/libpng×2/re-spirv);
**Jolt bumped 5.4.0→5.5.0** via whole-dir checkout from `4.7-stable` + a 1-line wrapper
adaptation (`InternalEdgeRemovingCollector` 2-arg ctor) — physics tests pass. See
`ported-log-depbump.json` for per-PR dispositions.

**Wave 5 = metal-cpp + raytracing (RD) migration.** Ported the 4.7 Metal C++ migration
(`metal-cpp` add → the Metal-4-ready C++ refactor → Metal fixes → SDK-linking / PR 116419) as a
**10-commit chain** that also required the **RenderingDevice raytracing feature** it depends on
(the 4.7 Metal driver overrides the base RD's pure-virtual raytracing API): `27e4f24800`
raytracing initial → blas_create refactor → API adjustments → pipeline refactor → SBT
alignment. The raytracing base auto-merged with only 2 conflicts (both the fork's clean-break
removal of deprecated methods). Resolution recipe: drivers/metal + metal_fx are stock 4.6.3 +
rebrand → take-theirs + `GODOT_MTL_/GODOT_CLANG_WARNING`→`FOUNDRY_*`; `register_server_types.cpp`
keep `FOUNDRY_REGISTER_*` + add new class registrations; rendering_device.cpp/.h take-theirs per
raytracing hunk; drop fork-removed deprecated docs. Fixed a mis-resolved dispose hunk (BufferID
scratch buffer freed via the driver path, not `free_rid`) and `GODOT_VERSION_*`→`FOUNDRY_VERSION_*`.
`dev_mode` -Werror needed metal-cpp added as a **system include** (`-isystem`) in both
`drivers/metal/SCsub` and `servers/rendering/renderer_rd/effects/SCsub` (Apple's vendored headers
trip `-Wshadow-field-in-constructor`/`-Wc99-designator`). Strict build clean, full suite 3016
passed. Metal IS compiled on macOS, so this is fully build-verified.

**Wave 6 = glslang + spirv-headers bump (DONE).** Bumped **glslang → vulkan-sdk-1.4.335.0**
(73-file whole-dir from `4.7-stable`) which is chained to **spirv-headers → vulkan-sdk-1.4.335.0**
(4.7's glslang references `spirv.hpp11`, only in the newer spirv-headers). The `modules/glslang`
wrapper was brought to 4.7 (drop `SPVRemapper.cpp`, D3D12-gate `shader_compile.h`) with the
Foundry rebrand and the `dev_mode` `-Wshadow-field-in-constructor` include-wrap re-applied. Then
**PR 116225** (Metal glslang memory-decorations / Apple-M1-MSAA fix) cherry-picked cleanly on the
newer glslang. Strict `dev_mode` build clean, full suite 3016 passed.

**Wave 7 = low-risk deferrals + Control-feature unlock-chains (DONE).** From the 130 low-risk
`port-later` items, 48 cherry-picked clean; the rest were blocked on absent Control *features*, so
those were ported as **unlock-chains** (foundation feature PR + its dependent fixes), each built &
tested green: **BaseButton multitouch** (PR 110893 + 4 fixes), **Control `custom_maximum_size`**
(PR 116640 + deferred-layout + 7 fixes; reconciled to the fork's TabContainer/Label structure),
**PopupMenu search bar** (PR 114236 + fuzzy/sizing + 9 fixes), **RTL table/shaping** (PR 116277 +
follow-ups incl. RTL `custom_maximum_size`), **Tree drag&drop + custom canvas-item** (PR 112993 +
edge-cases + custom_ci). Full suite **3017 passed** (a ported feature added a test).
- **Zero-deprecated invariant restored.** Several ports (incl. wave 1) had re-introduced
  `DISABLE_DEPRECATED` blocks by porting upstream compat shims. Audited `git diff origin/develop..HEAD`
  and stripped them from ALL fork code: the AccessibilityServer refactor's DisplayServer forwarders
  (converted 8 diverged-editor callers to `AccessibilityServer::`/`AccessibilityServerEnums::` then
  deleted the blocks), plus `Image` (`_save_exr*_bind_compat_117800` + `image.compat.inc`),
  `OptimizedTranslation` (`_generate_bind_compat_119563`), and `TabContainer.all_tabs_in_front`.
  Verify: `git diff origin/develop HEAD` adds **0** non-`thirdparty/` `DISABLE_DEPRECATED`. (Vendored
  thirdparty keeps its own guards — exempt.)
- **a11y = AccessibilityServer refactor (PR 116839, 88-file)** landed + fixes 117244/117283 + a
  ported two-arg `MAKE_ENUM_TYPE_INFO`/`VARIANT_ENUM_CAST_EXT` infra commit it needed.
- **HDR = DEFERRED (intractable for a bounded effort).** The Apple/Metal EDR commits (106814 etc.)
  can't compile without first porting **`b8389cc76b`** — a 65-file `renderer_rd` HDR core
  (compositor/scene-render/viewport/tonemapper + color-managed `blit`/`tonemap` shaders + a whole
  `RenderingDevice` ColorSpace/HDR-output/`SUPPORTS_HDR_OUTPUT` API absent in the fork). Treat that
  rendering-pipeline chain as a separate prerequisite investment before the EDR commits can land.
- One honest artifact in history: PR 118846 appears as port (`0fea40cc27`, a build-breaker) →
  revert (`9e4cb2f0b9`) → fresh clean re-port in the PopupMenu cluster (`132f488d0e`). Net-zero, kept
  as traceable record (non-adjacent rebase-drop wasn't worth risking the validated branch).

**Wave 8 = port-later re-triage + actionable cherry-picks (DONE).** The 311 wave-1..7 `port-later`
items had STALE deferral rationales (they were judged before later waves landed the features they
depended on). Re-triaged all 311 against current HEAD via 6 parallel read-only agents
(`retriage/in-*.json` → `retriage/out-*.json`, consolidated in **`retriage/CONSOLIDATED.json`**),
bucketed **A=port-now / B=feature-decision / C=still-blocked**: **65 already-on-HEAD** (wave-7
dependent chains had already pulled them in — porting = no-op), **40 A**, **74 B**, **132 C**.
Cherry-picked the 40 A (bugfixes first): **26 applied** (16 bugfixes + 10 perf/cleanup), **8 empty**
(already present), **6 conflict-deferred** — recorded in **`retriage/wave8-deferred.txt`**:
  - `#118554` Wayland pointer-frames — unbuildable-here (linuxbsd), `DisplayServerEnums::`→
    `DisplayServer::` rebrand overlay on rewritten logic.
  - `#117060` `crash_handler_linuxbsd` — unbuildable-here; fork already hand-adapted this file in
    wave 3 (richer dladdr/demangle path) — competing solutions.
  - `#114076` d3d12 driver — unbuildable-here; fork's NIR shader path adds fields upstream lacks.
  - `#116768` jolt "tidy" — real divergence vs the fork's Jolt 5.5 whole-dir bump (soft-body/
    area-overlap partial overlap; risks breaking physics).
  - `#117502` `scu_builders.py` — fork-customized SCU module list (foundry_* vs godot_*/mono) +
    gles3 `RS::`/`RSE::` + free-fn divergence; build-time-only tuning, low value.
  - `#119237` "vulkan-improve-errors" — **mislabeled by re-triage**; actually the HDR-output
    swapchain feature (absent HDR core + diverged fork swapchain) → HDR-blocked (see §0-deferred).
  Notable hand-resolutions: `object.cpp` signal-lock boundary (the safe-signals PR #117511 was
  already in develop → empty); `scene_debugger.cpp` (fork consolidated the pre-4.7-split file — the
  fix relocated there); `binder_common.h` (fork moved the `Object::ConnectFlags` VARIANT cast off the
  deleted `variant_caster.h`); `window.cpp` (took upstream's `get_accessibility_transform()` helper
  **and fixed a `DisplayServerEnums::INVALID_WINDOW_ID` bug the PR's own helper introduced** — that
  namespace is invalid in the fork). Strict `dev_mode` editor build clean + full suite **3038 passed**;
  `template_release` build clean.
- **`main/main.cpp:229` template fix (commit `218b496ed3`, develop-owned).** Not a 4.7-port change —
  arrived via the develop merge (#1149 projectless shell). See §0 for the full write-up + backport.

---

## 3. Tooling & artifacts (all under `docs/superpowers/specs/godot-4.7-port/`)

| File | Purpose |
|---|---|
| `catalog.json` | All 1651 PRs classified: `{sha, pr, title, bucket, subsystem, nfiles}`. Source of truth for what exists. |
| `classify.py` | Regenerates `catalog.json` by path-bucketing the delta. |
| `port.py` | Cherry-pick harness. `python3 .../port.py --risk low\|med [--dry-run] [--limit N]`. Reads `triage/out-*.json`, picks `port-now` of that risk in **chronological order**, `cherry-pick -x -m 1`, auto-aborts+records conflicts, writes `ported-log.json`. **Guards on a clean tree** — move untracked files out first. |
| `triage/in-*.json`, `triage/out-*.json` | Wave-1 per-subsystem triage inputs/verdicts. |
| `ported-log.json`, `ported-log-med.json` | Per-PR cherry-pick outcomes (low / med). |
| `conflict-components.json`, `resolve/result-*.json` | Wave-1 conflict resolution. |
| `med-conflict-components.json`, `resolve-med/result-*.json` | Med conflict resolution. |
| `retriage/CONSOLIDATED.json` | **Wave-8 port-later re-triage buckets.** `{summary, port_now[], feature_decision[], already_on_head[], blocked[]}` — each item `{pr, sha, verdict, reason, dependency, dependency_on_head, est_conflict, kind}`. The **74 `feature_decision`** entries are the pending product yes/no list. |
| `retriage/in-*.json`, `retriage/out-*.json` | Wave-8 re-triage per-group inputs/verdicts (A/B/C). |
| `retriage/wave8-deferred.txt` | The 6 wave-8 conflict-deferred PRs + one-line reason each. |

---

## 4. The pipeline (repeat this per wave)

### Step A — pick the next wave's subsystems
Remaining **untriaged candidate** PRs ≈ 662 (see §6; waves 1–8 consumed the rest). Suggested next
waves, value-ordered:
1. **`scene/gui` (122)** — runtime Control nodes, stock-adjacent, low editor-divergence risk. Best next target.
2. **`platform/*` (~145: android/linuxbsd/macos/windows)** — self-contained per-OS fixes; test the platform you build.
3. **`editor/*` (~400: scene, docks, inspector, animation, export, settings, gui, themes…)** — HIGHER risk: the fork heavily rewrote the workspace/multi-scene/script-editor. Triage agents MUST grep to confirm the touched editor files/classes still exist and aren't part of the diverged workspace surfaces. Expect a higher incompatible rate.

Skip during triage: `modules/mono` (C#, 11), `.github/*` (CI-only), web-platform-only.

### Step B — generate triage inputs
Filter `catalog.json` for `bucket==candidate` and the chosen subsystem(s), write one
`triage/in-<group>.json` per group (see the wave-1 inline python in git history of this dir,
or the spec). Balance groups to ~50–120 PRs each.

### Step C — fan out triage agents (parallel, read-only)
One `general-purpose` agent per group. Each reads its `in-*.json`, inspects each PR
(`git show --stat <sha>`; full diff only if ambiguous), and writes `triage/out-<group>.json`
with `{pr, sha, decision, kind, risk, rationale}` where decision ∈
`port-now`|`port-later`|`skip`. Prefer bugfixes; low risk = self-contained + undiverged.
**Tell agents the gotchas in §5.**

### Step D — cherry-pick the clear wins
`python3 docs/superpowers/specs/godot-4.7-port/port.py --risk low` (then `--risk med`).
Note: `port.py` currently loads ALL `triage/out-*.json`; to port only the new wave, either
move old `out-*.json` aside or add a filter. It writes `ported-log.json` (back it up between
runs — see how med was kept as `ported-log-med.json`).

### Step E — build & triage feature-dependency breaks
Build with **keep-going** to surface every break at once:
```
scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu) --keep-going > /tmp/b.log 2>&1
grep -n "error:" /tmp/b.log
```
For each break: `git log --oneline -1 -S'<symbol>' origin/develop..HEAD -- <file>` finds the
culprit PR. If it depends on unported 4.7 infra → `git revert --no-edit <local_sha>` and record
it. **But first check §5's deinit_ref lesson** — if the only blocker is a small extractable
primitive, hand-port that instead of dropping the fix.

### Step F — resolve conflicts in parallel (disjoint components)
The conflicted PRs from `ported-log.json` partition into **file-disjoint components** (PRs
sharing a file must stay together, in chronological order). Union-find them (see
`conflict-components.json` generator inline in git history). Bin-pack components into N buckets,
create N worktrees off the current staging HEAD (`git worktree add ../rN -b rN feature/godot-4.7-port`),
dispatch one agent per worktree to `cherry-pick`+resolve its bucket (with the §5 gotchas +
feature-incompatibility guard, **do NOT build**). Then reconcile: `git cherry-pick <base>..rN`
for each — **conflict-free because components are file-disjoint**. Remove the worktrees after.

### Step G — validate & checkpoint
Full rebuild (`python3 scripts/agent_build.py --dev-build`) then full suite:
```
DISPLAY=:1 bin/foundry.macos.editor.dev.arm64 --headless test run --progress-format=jsonl --progress-file /tmp/t.jsonl --force-colors
```
Trust the `[doctest] Status: SUCCESS!` line. Then clean history (drop dead port+revert pairs
via scripted `git rebase -i origin/develop` with a `GIT_SEQUENCE_EDITOR` that marks their SHAs
`drop`), rebuild+retest, update the catalog spec, commit, push.

---

## 5. Critical gotchas (tell every agent these)

- **`GDCLASS` was renamed to `FOUNDRY_CLASS`** (core/object/object.h:482). Any NEW ported file
  using `GDCLASS(...)` fails at build with *"a type specifier is required for all declarations"*
  on the macro line. Rewrite to `FOUNDRY_CLASS`.
- **`RS::` vs `RSE::`**: the fork uses an `RS::` alias where upstream 4.7 uses `RSE::` for
  rendering-server enums (`RS::ENV_BG_CANVAS`, etc.). Resolutions that keep upstream `RSE::`
  fail to compile.
- **`Godot`→`Foundry` rebrand**: many "conflicts" are pure branding/naming and reduce to empty
  (already-present). The binary is `foundry.*`, project config is `project.foundry`, class macro
  is `FOUNDRY_CLASS`, help is `FoundryCLIHelp`.
- **Feature-incompatibility guard**: MANY 4.7 fixes depend on 4.7 features absent in the 4.6.3
  base. Verified-absent so far: AreaLight/`ltc`/LTC, HDR-output tonemap members, particle
  transform packing (`float[12]` vs `mat4`), the AnimationTree/blend-space rework (the fork ALSO
  refactored this), Jolt env-props refactor (`_update_environmental_properties`), subsampled &
  drawable textures, GDExtension refcount-init, RD raytracing, DrawableTexture2D, the
  `scene/debugger` game-view file split. Agents must **grep the base to confirm a symbol exists
  before integrating**; if absent → skip, don't fabricate.
- **deinit_ref lesson**: if a fix's ONLY blocker is a small, self-contained primitive (like
  `RefCounted::deinit_ref`, ~5 lines using existing members), **hand-port that primitive**
  rather than dropping the whole fix. Don't hand-port large ABI-sensitive dependencies.
- **Build exit-code false alarms**: a trailing `grep -c "error:"` returns exit 1 when there are
  0 matches, which makes the whole bash command "fail". Check the actual error count and the
  `scons: done building targets` line, not the shell exit code.
- **The fork also refactored some engine areas** (notably animation internals), so a file being
  "stock" in upstream doesn't guarantee it's stock here. Always grep.

---

## 6. Remaining work inventory

**Untriaged `candidate` PRs ≈ 662** (THE main opportunity; catalog `candidate` counts include
already-ported PRs — authoritative "what's ported" is `ported-log*.json` + `triage/**/out-*.json`).
Now overwhelmingly **`editor/*`** (the fork's diverged workspace/multi-scene/script-editor surfaces —
expect a high incompatible rate; triage MUST grep-verify each touched file/class still exists):
editor/scene 119 · editor/docks 67 · editor/inspector 48 · editor/animation 37 · doc/classes 36
(deferred) · editor/export 30 · editor/settings 26 · editor/gui 24 · editor/debugger 20 ·
`.github/*` 25 (CI-only, skip) · editor/editor_node.cpp 17 · editor/themes 13 · editor/import 11 ·
modules/text_server_adv 11 · modules/mono 11 (skip, C#) · editor/run 10 · … Non-editor residuals
also remain in servers/rendering, scene/*, core/*, modules/openxr|gridmap|gltf.

**Also pending:**
- **Wave-8 re-triage OUTPUT** (supersedes the old "188 wave-1 port-later"; see §2 wave-8,
  `retriage/CONSOLIDATED.json`): **74 `feature-decision` (B)** items = net-new features that apply
  cleanly but need a product yes/no (particle flags, GridMap/XR features, FileDialog niceties,
  translation_context, etc.); **132 `blocked` (C)** = need absent 4.7 infra (AreaLight3D, RD
  raytracing base, HDR core, DrawableTexture2D, GDType migration) or hit fork divergence — leave
  until the prerequisite feature is a deliberate effort; **6 conflict-deferred** (`wave8-deferred.txt`).
  The **40 port-now (A)** are DONE (wave 8).
- **`manual-flag` = 128 PRs** touching `modules/gdscript`/script-editor — hand-port to Foundry
  Script only if specifically wanted.
- **`dep-bump` = 21** vendored thirdparty updates (freetype, harfbuzz, thorvg, jolt, etc.).
  Handle as **whole-directory version bumps** matched to 4.7's vendored versions + SCsub, NOT
  cherry-picks. Self-contained, high value.
- **`docs` = 74** pure `doc/classes` — deferred.
- **`skip-assetstore` = 19** — do not port.

---

## 7. Environment

- **Build (macOS):** `python3 scripts/agent_build.py --dev-build` (fast iteration) → binary
  `bin/foundry.macos.editor.dev.arm64`. Use plain `scons platform=macos target=editor
  dev_build=yes tests=yes --keep-going` for a full error sweep. Run **without** `--dev-build`
  (i.e. `dev_mode=yes`, warnings-as-errors) before declaring a wave truly done.
- **Test:** `DISPLAY=:1 bin/foundry.macos.editor.dev.arm64 --headless test run
  --progress-format=jsonl --progress-file /tmp/t.jsonl --force-colors`. Trust `[doctest]
  Status: SUCCESS!`; ObjectDB-leak lines at exit are expected noise.
- **Regenerate fixtures** after intentional behavior changes: `test generate-fixtures
  modules/foundry_script/tests/scripts` and `test generate-format-fixtures …/format`.
- Everything is **resumable**: `catalog.json` + `ported-log*.json` + `triage/out-*.json` hold
  all state; re-running a wave picks up untouched PRs.

---

## 8. Suggested next action for a fresh session

Waves 2–4 (scene/gui, platform, dep-bump) are **done**. The remaining candidate mass is the
**higher-risk `editor/*` set** (~editor/scene 119, editor/docks 67, editor/inspector 48,
editor/animation 37, editor/export 30, editor/settings 26, editor/gui 24, editor/debugger 20,
editor/editor_node.cpp 17, editor/themes 13, editor/import 11, editor/run 10, …). These are the
counts in `catalog.json`, but that file's `bucket`/`subsystem` fields are **static gross counts
that still include already-ported PRs** — the authoritative "what is ported" is
`ported-log*.json` + `triage/**/out-*.json`. Triage agents here MUST grep-verify each touched
editor file/class still exists and is not part of the fork's diverged workspace/multi-scene/
script-editor surfaces (expect a much higher incompatible/port-later rate than scene/gui).
Follow §4 A→G. `servers/rendering`, `scene/animation`, `scene/main`, `core/*`, and
`modules/openxr|gridmap|gltf` also have residual untriaged candidates worth a lighter pass.

Deferred clusters worth a dedicated effort later: the **AccessibilityServer refactor (PR
116839)** unlocks a chain of a11y fixes; the **metal-cpp Metal migration** unlocks glslang +
Metal PRs; **Control max-size (116640)**, **PopupMenu search-bar**, and **BaseButton
multitouch** each unlock several scene/gui port-later items.
