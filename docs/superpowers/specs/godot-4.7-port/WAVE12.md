# Wave 12 — challenge "absent 4.7 infra" deferrals

**Goal (from HANDOFF §NEXT SESSION):** the wave-11 "absent infra" reverts were often a small
foundation commit that was simply never ported. Port the small foundations first, then re-apply the
dependents that were reverted. Deliverable: `ported-log-wave12.json` recording per previously-blocked
item either `ported` or `blocked-large` (with file/line evidence).

Branch: `feature/godot-4.7-port-continue` off `origin/develop` (96f87df168, waves 1–11 merged).
Target file (fork path): `editor/scene/3d/node_3d_editor_plugin.{cpp,h}` (upstream moved from
`editor/plugins/` to `editor/scene/3d/` mid-4.6.3..4.7 range).

## Cluster 1 — trackball + follow-selection + gizmo-highlight chain (node_3d_editor_plugin)

The three wave-11 reverts (#116159, #117923, #120063) are DEPENDENTS. Their foundations were never
ported. All commits touch one file; port the full connected chain in chronological order.

Chronological single-parent change commits to attempt (upstream `godot` remote SHAs):

| # | sha | title | role |
|---|-----|-------|------|
| 1 | 12782eac8e | Add trackball-style rotation for 3D transform gizmo | trackball foundation (#109976) |
| 2 | 6006596fd8 | Don't highlight gizmos while in freelook | related (#115543) |
| 3 | afb5839696 | Make trackball rotation optional as toggle | trackball (#115794) |
| 4 | 415ddc83e1 | Fix trackball not highlighting immediately (Use Trackball) | DEFINES update_transform_gizmo_highlight (#115992) |
| 5 | 996353e457 | Fix viewport text not clearing after commit/cancel of gizmo handles | related |
| 6 | c899f017e2 | Consecutive presses of Begin Rotate enables trackball | trackball |
| 7 | 3201f3bb5d | Immediately update transform gizmo handle highlight on tool change | DEPENDENT — reverted #116159 |
| 8 | 040e19e75d | Add "Follow Selection" (Center Selection twice) | follow foundation |
| 9 | 005a661bd0 | Reset follow mode focus count on selection change | follow (#117214) |
| 10 | 28ebd60ce2 | Reset follow mode focus count after committing a transform | follow |
| 11 | 59e6ff8cc5 | Reset follow mode count on subgizmo point switch | DEPENDENT — reverted #117923 |
| 12 | d92a1acc36 | Fix trackball when use local space is enabled | DEPENDENT — reverted #120063 |

Note: fork's file is at `editor/scene/3d/`; commits 1–? that predate the upstream move used
`editor/plugins/` — cherry-pick may need path handling. Empty picks = already present, drop them.

## Gotchas (from HANDOFF §5)
- `GDCLASS`→`FOUNDRY_CLASS`; `RS::` (fork) vs `RSE::` (upstream); Godot→Foundry rebrand.
- Grep base to confirm a symbol exists before integrating.
- Build with `--keep-going`; validate strict `dev_mode` + full suite incl. `DISPLAY=:1`.

## Later buckets (after cluster 1 lands)
- `retriage/CONSOLIDATED.json` 132 `blocked` (C) + 74 `feature_decision` (B).
- `triage/wave11/out-*.json` port-later with absent-symbol rationale.
- §5 verified-absent list re-audit (AreaLight3D/LTC, HDR core, RD raytracing — likely stay deferred).
