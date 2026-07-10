# Wave 12 progress

## Cluster 1 — node_3d_editor_plugin trackball/follow/gizmo-highlight chain

Branch `feature/godot-4.7-port-continue` off develop 96f87df168.

Cherry-picks (chronological), all into `editor/scene/3d/node_3d_editor_plugin.{cpp,h}`:

| upstream sha | local | result | notes |
|---|---|---|---|
| 12782eac8e Add trackball-style rotation | a9a5ec3406 | ported | conflict: fork's guarded `_finish_gizmo_instances` + multi-scene `_rebind_gizmo_scenarios`; added trackball_sphere_instance free + scenario rebind in fork style |
| 6006596fd8 Don't highlight gizmos in freelook | — | empty | already present |
| afb5839696 Make trackball optional (toggle) | de08a0d376 | ported | clean |
| 415ddc83e1 Fix trackball highlight (defines update_transform_gizmo_highlight) | e05b63736a | ported | conflict: new fn inserted where fork has apply_preview_camera_state; kept both |
| 996353e457 Fix viewport text not clearing | — | empty | already present |
| c899f017e2 Consecutive presses → trackball | 966727df87 | ported | clean |
| 3201f3bb5d Immediately update gizmo highlight on tool change | 9993056584 | ported | **was wave-11 revert #116159** — now unblocked |
| 040e19e75d Add "Follow Selection" | 8051ed9029 | ported | conflict: fork multi-scene `EditorNode::get_focused_scene_tree_dock()` vs upstream `SceneTreeDock::get_singleton()`; kept fork accessor + follow-mode block |
| 005a661bd0 Reset follow count on selection change | e22681826c | ported | clean |
| 28ebd60ce2 Reset follow count after committing transform | db5db33d5a | ported | clean |
| 59e6ff8cc5 Reset follow count on subgizmo point switch | 60f9314722 | ported | **was wave-11 revert #117923** — now unblocked |
| d92a1acc36 Fix trackball when use local space enabled | 310ed18435 | ported | **was wave-11 revert #120063** — now unblocked |

Net: 10 ported, 2 already-present. All 3 wave-11 absent-infra reverts re-applied.

## Validation
- [x] dev_build build: clean, 0 errors
- [x] full suite (DISPLAY=:1): **3069 passed / 0 failed / 3 skipped**
- [ ] strict dev_mode build (warnings-as-errors) — final gate before wave done

## Next
- [ ] widen to retriage blocked buckets (retriage/CONSOLIDATED.json 132 blocked + 74 feature_decision)
