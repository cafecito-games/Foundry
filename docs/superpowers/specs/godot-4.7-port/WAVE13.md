# Wave 13 — absent-infra re-audit of the CONSOLIDATED "blocked" items

**Branch:** `feature/godot-4.7-port-wave13` off develop `5a284a3c95` (wave-12 merged).
**Method:** the §NEXT-SESSION absent-infra challenge from `HANDOFF.md`, applied to the 112
non-big-infra items in `retriage/CONSOLIDATED.json` `blocked[]` (the 20 HDR/AreaLight3D/RD-raytracing
items were left as recorded blocked-large without re-analysis).

## Triage (6 parallel read-only agents, `triage/wave13/out-*.json`)

| cluster | port-now | blocked-large | blocked-unverifiable | skip |
|---|---|---|---|---|
| core | 0 | 6 | – | 11 |
| scene (scene-other + scene-gui) | 7 | 4 | – | 4 |
| render + modules | 5 | 1 | – | 9 |
| platform-macOS-verifiable | 2* | 4 | – | 1 |
| view3d (View3DController deps) | 0 | 5 | – | 4 |
| platform-unverifiable | 0 | 26 | 19 | 4 |

\*The `OS::RENDERING_SOURCE` pair (#117250/#117253) has an extractable macOS os.h enum primitive,
but both dependents are **Windows-only** ANGLE fixes — not end-to-end build-verifiable here, so
deferred as blocked-unverifiable rather than ported blind (wave-11 lesson).

### Key confirmations
- **View3DController (`27c86165f7`) is genuinely blocked-large**: a 1599/1368-line 4.7 editor
  rework gutting `node_3d_editor_plugin.cpp` (−1223) and rewriting `runtime_node_select.cpp` (−378),
  colliding head-on with the fork's diverged multi-scene 3D editor and pre-split monolithic
  `scene_debugger`. 4 of its 9 dependents are **moot** (they fix bugs the rework itself introduced
  that the fork's pre-rework code never had).
- **core** yielded 0: every blocked core item roots at a genuinely large foundation (the GDExtension
  refcount-ABI series `create_instance3`/`register_extension_class6`; the `memnew_result_t` ownership
  rework) or is low-value header-churn refactor.
- **WTP deadlock pair (#120072/#120111) was reverted upstream** by #120250 → skip.
- Several "blocked — fork divergence" premises were **stale/false**: tab_container, rich_text_label,
  polygon_2d, skeleton_3d, particle_process_material each diverge from 4.6.3 by only 1–3 rename lines.

## Ported (14 commits: 3 foundations + 11 dependents/companions)

| PR | area | foundation |
|---|---|---|
| #115177 | render: unique env uniform buffer per pass | — |
| #115602 | OpenXR: hide OpenXRUserPresence singleton | #115190 (XR_EXT_user_presence) |
| #116681 | TabContainer: iterate-based `get_tab_control()` | — |
| #113509 | particle process: velocity userdata | — |
| #117334 | Polygon2D: mesh fast-path update | — |
| #113605 | Skeleton3D: `_process_modifiers` perf | — |
| #118277 | RichTextLabel: item RID counting | — |
| #96748 | glTF: per-texture texCoord / multi-UV import | — |
| #118867 | GridMap: navmesh bake-bounds via octant query | #118280 (octant querying) |
| #118975 | OpenXR: default action map → khr/generic_controller | #110778 (XR_KHR_generic_controller) |
| #119872 | Polygon2D fast-path AABB companion to #117334 | — |

### Notable conflict resolutions
- **register_types.cpp (#115190/#115602)**: `Godot`→`Foundry` register-macro rebrand; #115602's
  singleton-hiding correctly drops the `OpenXRUserPresenceExtension` registration. New
  `openxr_user_presence_extension.h` rewritten `GDCLASS`→`FOUNDRY_CLASS`.
- **tab_container.cpp (#116681)**: adapted the new `_as_tab_control()` helper to the fork's `tab_bar`
  sentinel (fork has no `internal_container`) and kept the fork's AccessibilityServer refactor; the
  `iterate_children()` + `ERR_THREAD_GUARD_V` rewrite applies to all three lookups.
- **glTF (#96748)**: clean-break — dropped the `#ifndef DISABLE_DEPRECATED` generate_scene branch,
  kept the renamed `gltf_doc`/`gltf_state` locals; added `_append_khr_texture_transform_ext_json_pointer`
  while preserving the fork's `p_foundry_node` param rename; doc XML rebranded + enum-name defaults.

## Deferred after conflict (never force a messy port)
- **#117030** (TrackCache IDs-not-hashes): `Animation::TypeHash`→`TrackCacheID` type migration threaded
  through 10 regions of the fork's diverged animation internals + unrelated `folded_groups` drift.
  Minor robustness value; not worth the messy port.
- **#119367** (FileSystem dock selection rework): 320-line rework colliding head-on with the fork's
  multi-scene-workspace-diverged `FileSystemDock` display-mode / scroll-hint / theme-variation logic.
  Editor UX nicety; deferred.

## Lesson
Unlike wave-12 (trackball/follow had genuinely small extractable foundations), the wave-13 blocked
mass is dominated by **large 4.7-only reworks** (View3DController, AnimationTree internals, GDExtension
ABI, HDR core) and **platform code that can't be build-verified on macOS** (WinRT/ANGLE/MSVC/iOS/
Android). The real wins were **stale-premise re-triage** (fixes that now apply because the fork only
rename-diverged) plus **three small self-contained module foundations** (octant query, KHR generic
controller, XR user presence). Absent-infra still pays, but the tail is thinning.
