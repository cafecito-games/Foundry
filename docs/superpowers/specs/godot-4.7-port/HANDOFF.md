# Godot 4.7 → Foundry Port — Session Handoff

**Read this first, then `2026-07-07-godot-4.7-port-catalog.md` (the spec) for background.**
This document lets a fresh session continue porting Godot 4.7 into Foundry with no ramp-up.

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

---

## 4. The pipeline (repeat this per wave)

### Step A — pick the next wave's subsystems
Remaining **untriaged candidate** PRs = 926 (see §6). Suggested next waves, value-ordered:
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

**Untriaged `candidate` PRs = 926** (the main opportunity). Top subsystems:
scene/gui 122 · editor/scene 119 · editor/docks 67 · editor/inspector 48 · platform/android 43 ·
platform/linuxbsd 39 · editor/animation 37 · editor/export 30 · platform/macos 27 ·
editor/settings 26 · platform/windows 26 · editor/gui 24 · editor/debugger 20 ·
editor/editor_node.cpp 17 · editor/themes 13 · editor/import 11 · modules/text_server_adv 11 ·
modules/mono 11 (likely skip) · editor/run 10 · editor/project_manager 9 · editor/shader 9 · …

**Also pending:**
- **188 port-later** from wave 1 (features/refactors deferred as too large/risky — e.g.
  AreaLight3D, HDR output, GDExtension refcount-init, RD raytracing, glTF multi-UV). Re-evaluate
  which are worth the larger effort. Some are worth adopting wholesale if the fork wants the
  feature.
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
