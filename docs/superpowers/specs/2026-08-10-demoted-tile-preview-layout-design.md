# Demoted Tile Preview Layout Design

Status: approved
Issue: [#2067](https://github.com/cafecito-games/Foundry/issues/2067)
Depends on: [#2059](https://github.com/cafecito-games/Foundry/issues/2059)
Date: 2026-08-10

## Summary

A demoted scene tile can lose its entire live preview in a horizontal split because its left and
right dock chrome consumes the tile's available width first. The selected product rule is
preview-only presentation: every non-focused live 2D or 3D scene tile hides both dock columns and
both side rails. Promoting the tile restores its exact prior dock presentation.

The preview-only state is transient. It must not change or persist the user's dock modes, open
drawers, selected tabs, or split offsets.

## Confirmed Layout Mechanism

The live editor reproduction measured this layout in a two-tile horizontal split at `EDSCALE == 2`:

- `SceneTreeDock` consumes 360 physical pixels, matching its declared `180 * EDSCALE` minimum in
  `editor/editor_scene_pane_tile.cpp`.
- The right tab stack declares the same `180 * EDSCALE` minimum in
  `editor/editor_tile_dock_region.cpp` and measured 440 physical pixels with its Inspector contents.
- The two side rails measured 89 physical pixels each.
- Splitter gaps consumed another 12 physical pixels.
- Even in a 3024-pixel-wide editor window, the demoted tile's center host was only 204 physical
  pixels wide. Narrower ordinary windows reduce that remainder to zero.

The pending #2059 real-editor workflow independently records the same constraint. It deliberately
uses a vertical split because a horizontal split leaves no preview geometry at which to aim its
pointer-input acceptance probes.

This is a layout-budget failure, not a rendering failure. `content_host` correctly uses expand-fill,
but a `SplitContainer` cannot give it space after satisfying the visible dock minima and chrome.

## Product Decision

`ScenePaneTile::set_preview_mode()` defines the presentation:

- `TilePreviewMode::FOCUSED_LIVE`: show the tile's normal dock and rail presentation.
- `TilePreviewMode::LIVE_2D`: hide both dock columns and both side rails.
- `TilePreviewMode::LIVE_3D`: hide both dock columns and both side rails.

The rule is independent of width and split orientation. A demoted scene tile is a passive rendering
surface, not a second editor whose chrome happens to collapse only under pressure.

The scene tab strip and focus frame remain visible. They identify the scene and owning tile, preserve
tab drag/drop, and provide the existing click-to-promote surface. The center content host remains
visible and receives the width released by the side chrome.

The original issue criterion that vertical splits remain completely unchanged is replaced with a
more precise rule: vertical and horizontal demoted tiles both use preview-only chrome, while their
rendering, outer workspace split proportions, focus transitions, and promotion behavior remain
unchanged. A single focused tile retains its current presentation.

## Transient Presentation Override

Preview-only presentation must not be implemented by switching both sides to `RAILED`, closing
drawers, or modifying the stored side state. Those are user-visible layout transitions and would
overwrite restoration and persistence information.

`EditorTileDockRegion` will instead own a transient presentation-hidden override. While entering the
override it will:

1. Capture both live split gaps by identity before changing any child visibility.
2. Hide the left dock column and right tab stack without changing either side's pure state.
3. Leave the center host visible and reapply only offsets that still have a live gap.

Both sides must transition as one presentation operation. `SplitContainer` rewrites offset arrays
synchronously when a child becomes hidden; separately applying ordinary side transitions could
re-alias the surviving offset and overwrite the remembered value for the other side.

While leaving the override, the region will derive visibility from the stored side modes and drawer
identities, then reapply both remembered gaps by identity. The original asymmetric presentation must
return exactly.

The external `EditorSideRailStrip` controls are hidden and shown by `ScenePaneTile` alongside the
dock-region override. Their mirrored state is not destroyed or rebuilt merely because the tile is a
preview.

Repeated calls with the same preview mode are idempotent. Dock availability, title/icon state, and
right-tab selection may change while hidden and must be reflected when the tile is promoted.

## Persistence Contract

- `save_layout()` always writes the underlying user state, even while preview-only presentation is
  active.
- No new layout key records the temporary override.
- `load_layout()` restores dock modes, drawer identities, selected tabs, enabled docks, and split
  offsets normally. If the tile is still demoted, those values remain hidden until promotion.
- Entering or leaving a board overview must not convert the temporary preview presentation into a
  saved layout change.
- No dock is reparented, recreated, or globally registered as part of this feature.

## Focus and Input Flow

The #2059 passive-preview contract remains authoritative:

1. A non-focused tile receives `LIVE_2D` or `LIVE_3D` and hides its side chrome.
2. The preview surface rejects editing focus, camera manipulation, selection, scene mutation, and
   drag/drop.
3. A primary-button gesture passes through to the tile and promotes it.
4. The display-attachment update assigns `FOCUSED_LIVE`; normal chrome is restored before the user
   can interact with its docks.
5. The previously focused sibling becomes preview-only with its own independent stored layout.

Focused-tile command routing remains unchanged. Hidden docks in a demoted tile are never selected as
the target of global Scene, Inspector, Signals, Groups, or History commands.

## Unit and Workspace Tests

Extend the dock-region and scene-workspace tests with observable behavior:

- Begin with distinct left and right split offsets, asymmetric `DOCKED`/`RAILED` modes, an open
  drawer, and a non-default selected right tab.
- Enter preview-only presentation and assert that both side columns are hidden, the center remains
  visible, and the stored modes, drawers, tab, offsets, and saved `ConfigFile` values are unchanged.
- Change dock availability while hidden, leave preview-only presentation, and assert that the
  original layout returns with the availability change correctly reflected.
- Cover repeated enter/leave calls and both orders of asymmetric side state.
- Create a fixed-width horizontal two-tile workspace narrower than the combined dock minima.
- Exercise `LIVE_2D` and `LIVE_3D`; both rails and dock columns must be hidden and the content host
  must receive essentially the full tile width rather than zero geometry.
- Switch focus in both directions. The promoted tile restores its chrome and the newly demoted tile
  hides its own chrome without copying state between tiles.
- Verify that a single `FOCUSED_LIVE` tile retains its existing presentation.

Tests must assert control state, geometry, persisted values, and focus behavior. They must not inspect
source text.

## Real-Editor Acceptance

Extend #2059's `passive_preview_input_policy` workflow instead of adding a disconnected synthetic
workflow:

1. Constrain the real editor window to a deterministic size within the checked-in workflow.
2. Split the active board horizontally rather than using the current vertical workaround.
3. Exercise the existing 3D preview, then promote it so the 2D sibling becomes the demoted preview.
4. For both previews, assert that the side rails and dock columns are hidden.
5. Require each preview surface to have positive area and at least 80 percent of its owning tile's
   width.
6. Retain all #2059 assertions for passive focus, input, camera, selection, scene mutation, and
   drag/drop behavior.
7. Promote each preview through real pointer input and verify exact chrome restoration.
8. Emit actionable geometry and visibility details in every failure message.

Create a proof review gallery with a captioned before/after comparison. The before image must make
the zero-width horizontal preview failure visible; the after image must show the same constrained
split with a usable preview-only tile.

## Validation

- Run the focused dock-region, side-rail, scene-workspace, and passive-preview cases.
- Run the real-editor workflow with a GUI display so it cannot silently skip.
- Run the native strict validation build through `python3 scripts/agent_build.py`.
- Confirm that saving and reopening a layout after a tile was demoted restores only the user's dock
  state, never the temporary hidden presentation.

## Acceptance Criteria

- A demoted 2D or 3D scene tile shows a visible, usable preview in a constrained horizontal split.
- Its preview surface receives at least 80 percent of the tile width in the checked-in acceptance
  fixture.
- Both dock columns and both side rails are absent from every demoted scene tile, independent of
  available width or split direction.
- Promoting a tile restores its exact previous side modes, drawers, selected tab, enabled docks, and
  split offsets.
- Demotion and promotion do not modify persisted layout values.
- Passive-preview input and click-to-promote behavior from #2059 remains intact.
- Focused single-tile behavior and command routing remain unchanged.
- The proof gallery lets a reviewer judge the before/after result without a local build.

## Non-Goals

- A width threshold or resize-driven chrome policy.
- Shrinking the Scene or Inspector dock minimum sizes.
- Keeping rails or drawers interactive on a demoted tile.
- Changing scene-tab presentation or tab drag/drop.
- Adding layout persistence keys.
- Redesigning focused-tile command routing.

## Automation Follow-Up

The editor MCP used during investigation has no semantic window-resize action. The checked-in
acceptance workflow can size its own root window and this issue does not need to expand the MCP
surface. A general automation resize action remains a useful follow-up for future layout testing.
