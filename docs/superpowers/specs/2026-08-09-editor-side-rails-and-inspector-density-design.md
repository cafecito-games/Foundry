# Editor side rails and inspector density — design

Status: design, not yet implemented.
Date: 2026-08-09. Revised 2026-08-09 after adversarial review; see §10 for what changed
and why, since the first revision targeted the wrong surface.

## 1. Summary

Two independent editor changes, driven by one goal: let the user trade screen space for
detail on both axes, without changing the bottom panel.

1. **Tile side rails.** Inside each scene tile, the left dock (Scene tree) and the right
   dock tab stack (Inspector / Signals / Groups / History) each gain a persistent vertical
   strip — a *rail* — and a collapsed mode. Collapsed, that side of the tile costs one rail
   width instead of a full dock column, and shows **exactly one dock at a time**, the way
   the bottom drawer shows exactly one. The two sides are independent, and every tile keeps
   its own state.
2. **Inspector density.** A three-level editor setting scaling inspector row height,
   control height and section padding, composed with (not replacing) the existing global
   `interface/theme/spacing_preset`.

The rails are a deliberate transposition of the shipping bottom drawer
(`EditorBottomDrawerStrip` + `EditorBottomPanel`), which is already a strip-plus-drawer
machine with per-dock persisted geometry. Where this design departs from that machine, the
departure is called out and justified in §4.

## 2. Locked decisions

These were decided during design review and are not open for re-litigation during
implementation:

| # | Decision |
|---|---|
| D1 | Rail buttons show **icon plus label**, the label drawn rotated. Icon-only is the automatic fallback when the labelled rail does not fit (§4.5), not a user-facing preference. |
| D2 | A collapsed side shows **one dock at a time**. Clicking another rail toggle switches to it; clicking the active one closes the drawer, leaving a bare rail. |
| D3 | Left and right are **fully independent**. No setting, shortcut, or state couples them. |
| D4 | The inspector density setting ships with three levels. **Compact** is the level the change exists to enable. |
| D5 | **The bottom panel does not change.** No behavioural edits to `EditorBottomPanel`, `EditorBottomDrawerStrip`, `BottomDrawerGeometry`, or `BottomDrawerLayout`. Shared code may be extracted, but observable bottom-panel behaviour must be identical before and after. |
| D6 | Unpinned/floating side drawers are **out of scope** for this design. See §7. |
| **D7** | **The rails target the per-tile dock region (`EditorTileDockRegion`), not the global `EditorDockManager` side slots.** That is where Scene tree, Inspector, Signals, Groups and History actually live (§3.1). The global side columns are out of scope. |
| **D8** | **A rail is never a child of a `SplitContainer` whose offsets are persisted positionally.** Rails are mounted in a wrapper `HBoxContainer` outside the split (§4.3). This is a hard constraint, not a style preference — see the offset-aliasing hazard in §4.6. |
| **D9** | **Inspector density ships defaulting to `Default`**, and scales the inspector's styleboxes as well as `inspector_property_height`. Scaling the constant alone is close to a no-op (§5.2), and flipping the shipped default would change first-run appearance for every user. §9's first open decision is hereby closed. |

## 3. Current state

### 3.1 Where docks actually live

**This is the fact the first revision of this design got wrong, and it invalidated most of
it.** Verified in this checkout:

- `EditorTileDockRegion` (`editor/editor_tile_dock_region.h:42-43`) states it plainly:
  *"Manages the in-tile dock strip: [left dock | center host | right tab stack]. Docks live
  here instead of the global EditorDockManager slots."*
- Each `ScenePaneTile` builds its own `SceneTreeDock`, `InspectorDock`, `SignalsDock`,
  `GroupsDock` and `HistoryDock` (`editor/editor_scene_pane_tile.cpp:224-258`) and places
  them in that region. They are constructed with the global-registration flag **false**, so
  they register no `docks/open_*` shortcut at all
  (`editor/docks/signals_dock.cpp:52-57` and siblings).
- Only `ImportDock`, `FileSystemDock` and the bottom docks are added to the global
  `EditorDockManager` (`editor/editor_node.cpp:11391-11398`, `:11477`).
- The default layout therefore assigns docks to just two of the eight global side slots —
  `dock_3` = `DOCK_SLOT_LEFT_UR` = Import, `dock_4` = `DOCK_SLOT_LEFT_BR` = FileSystem
  (`editor/editor_node.cpp:11429-11431`, and the comment there saying as much). **The
  global right column is empty by default.**

Consequence: rails on the global side columns would collapse Import and FileSystem on the
left and an empty column on the right, and would not touch the Inspector — the dock the
feature exists to make collapsible. Hence D7.

### 3.2 The tile dock region

```
ScenePaneTile
└── focus_frame (PanelContainer)
    └── body (HSplitContainer)            editor/editor_scene_pane_tile.cpp:215-236
        ├── scene_tree_dock               left dock,  place_left()
        ├── content_host                  the viewport / center host
        └── right_tabs (TabContainer)     Inspector, Signals, Groups, History
```

- `right_tabs` is a plain `TabContainer` (`editor/editor_tile_dock_region.cpp:50-57`), which
  makes it a *structurally exact* analogue of the bottom drawer's `TabContainer`. The rail
  can mirror it the way `EditorBottomDrawerStrip` mirrors the bottom panel, and
  "one dock at a time" is already native to it.
- Signals / Groups / History can be individually disabled
  (`ScenePaneTile::set_*_dock_enabled` → `EditorTileDockRegion::set_dock_enabled`,
  `editor/editor_tile_dock_region.cpp:86-97`), which hides the dock and moves selection off
  it. The rail must mirror enabled docks only.
- Per-tile layout is persisted under a tile-scoped section key
  (`EditorTileDockRegion::layout_key_for_tile`, `editor/editor_tile_dock_region.cpp:38-43`),
  saved and loaded by `save_layout` / `load_layout`
  (`editor/editor_tile_dock_region.cpp:131-193`).
- Tiles are constructible in the headless doctest environment: existing cases build a real
  workspace with `EditorSceneWorkspace::create_single_leaf_workspace(selection, &editor_data)`
  (`tests/editor/test_scene_workspace.h:243`). This is the test seam for everything in §4.

### 3.3 The bottom drawer, as the reference implementation

- `EditorBottomDrawerStrip` (`editor/gui/editor_bottom_drawer_strip.h:45`) is a pure
  mirror of the drawer's `TabContainer`: it owns no drawer state (header comment,
  lines 40-44).
- Toggle semantics: clicking the active toggle deselects to tab `-1`, clicking another
  switches (`_toggle_pressed`, `editor/gui/editor_bottom_drawer_strip.cpp:149-152`).
- The close button is moved to sit immediately after the active toggle, and is hidden
  when nothing is open (`_update_active_states`,
  `editor/gui/editor_bottom_drawer_strip.cpp:173-193`).
- Toggles carry icon **and** text unconditionally (`_refresh_toggle`, lines 103-113), and
  their tooltip is `"<Shortcut Name> (<keys>)"` — **empty when the dock has no shortcut with
  a bound key**, since the guard is `has_valid_event()`
  (`editor/gui/editor_bottom_drawer_strip.cpp:115-121`). The identical guard exists a second
  time in `EditorDockManager::_update_tab_style`
  (`editor/docks/editor_dock_manager.cpp:625`), which builds tab tooltips.
- Toggles restyle from the dock's `_tab_style_changed` signal (lines 88-95, 140-146), and
  the whole row rebuilds on theme or translation change (lines 43-53).
- Pure geometry lives in a scene-free, headlessly unit-testable header
  (`editor/gui/bottom_drawer_geometry.h:42-104`), covered by
  `tests/editor/test_bottom_drawer_geometry.h`.

### 3.4 Inspector sizing

Every property row derives its height from one theme constant:

- `EditorProperty::get_minimum_size` seeds from `theme_cache.inspector_property_height`
  (`editor/inspector/editor_inspector.cpp:224`, also used at line 317).
- `EditorSpinSlider::get_minimum_size` reads the same constant
  (`editor/gui/editor_spin_slider.cpp:68`).
- The constant is computed once per theme, identically in both themes, as the inspector
  button stylebox min height plus the LineEdit font height
  (`editor/themes/theme_modern.cpp:2526-2527`,
  `editor/themes/theme_classic.cpp:2133-2134`).

**Both consumers use the constant as a floor, not as a size.** `EditorProperty` seeds
`Size2(0, height)` and then maxes against its children's minimum sizes; `EditorSpinSlider`
computes `MAX(stylebox + font height, constant)`. Since the constant is itself *derived
from* the stylebox and font that dominate those child minimums, shrinking it alone moves
almost nothing. See §5.2.

A global spacing preset already exists — `interface/theme/spacing_preset` with values
`Compact,Default,Spacious,Custom` (`editor/settings/editor_settings.cpp:577`, applied at
`editor/themes/editor_theme_manager.cpp:394-427`). It scales the **whole editor**, not the
inspector. This design does not replace it; see §5.

## 4. Design — tile side rails

### 4.1 Per-side, per-tile state machine

Each side of each tile is in exactly one mode, persisted in that tile's layout section:

| Mode | Rail | Dock side of the tile body | Docks visible |
|---|---|---|---|
| `DOCKED` | visible | shown as today | left: Scene tree. right: the selected tab |
| `RAILED`, drawer open on dock *D* | visible | shown, holding only *D* | exactly one: *D* |
| `RAILED`, drawer closed | visible | hidden | none |

Transitions:

- `DOCKED` + click the toggle of the already-visible dock → `RAILED`, drawer closed.
- `DOCKED` + click the toggle of a non-visible dock → stays `DOCKED`, selects that dock's
  tab (existing `EditorTileDockRegion::focus_dock`).
- `RAILED` + click any toggle → `RAILED`, drawer open on that dock.
- `RAILED` + click the active toggle, or the close button → `RAILED`, drawer closed.
- Either mode + the side's shortcut → toggles between `DOCKED` and `RAILED`, restoring
  the last drawer dock when returning to `RAILED`.
- `RAILED` + the rail's expand button → `DOCKED`.

The rail is **always visible** in both modes, mirroring the bottom strip, which is always
visible whether or not the drawer is open. This gives the collapse and restore transitions
a permanent affordance and makes the width cost of a side honest.

### 4.2 How "one dock at a time" is implemented

No reparenting.

- **Right side.** `right_tabs` is already a `TabContainer`; one-at-a-time is what it does.
  `RAILED` with the drawer open is `right_tabs->show()` plus `set_current_tab(D)`;
  `RAILED` with the drawer closed is `right_tabs->hide()`. Tab order, tab identity and the
  persisted `tile_dock_right` / `tile_dock_right_selected_tab_idx` keys are untouched.
- **Left side.** Today the left holds exactly one dock (Scene tree), so `RAILED` with the
  drawer open is that dock visible, and closed is that dock hidden. The implementation
  must nonetheless iterate the `EditorDock` children of `body` that precede `content_host`,
  because `place_left` (`editor/editor_tile_dock_region.cpp:60-68`) admits more than one.

Consequences that fall out for free:

- `HSplitContainer` drops a hidden child from its valid-children set
  (`scene/gui/split_container.cpp:978`, `:995`, `:1044`, `:1065`), so a closed-drawer side
  collapses to nothing with no extra code. **The same fact is what makes §4.6 mandatory.**
- Nothing about dock ownership, tab order, or the tile's dock set changes, so a collapsed
  tile saves the same dock assignments it had.

### 4.3 Mounting: rails live outside the split (D8)

```
focus_frame (PanelContainer)
└── rail_hbox (HBoxContainer)             ← new
    ├── left rail   (EditorSideRailStrip)
    ├── body        (HSplitContainer)     ← unchanged, still holds the three columns
    └── right rail  (EditorSideRailStrip)
```

The rails must **not** be children of `body`. `body`'s split offsets are persisted by
position (`tile_dock_hsplit_1`, `tile_dock_hsplit_2`;
`editor/editor_tile_dock_region.cpp:139-146`), and `SplitContainer::get_split_offsets()`
returns one entry per gap between *visible* children. Adding two always-visible rails to
`body` would turn two offsets into four, silently re-aliasing every saved key and giving
the user drag handles on the rails themselves.

The same constraint is why the earlier revision's plan to mount rails in `main_hsplit` was
rejected outright: `main_hsplit` is seeded with exactly two offsets for "only 3 visible"
children (`editor/editor_node.cpp:11418-11422`) and its offsets are mapped positionally
onto `dock_hsplit_N` in both directions (`editor/docks/editor_dock_manager.cpp:741-750`,
`:838-844`).

### 4.4 `EditorSideRailStrip`

New control, `editor/gui/editor_side_rail_strip.{h,cpp}`, modelled directly on
`EditorBottomDrawerStrip` and carrying over its structure:

- Constructed with the side it represents and the source it mirrors — a `TabContainer` for
  the right side, the ordered left-dock list for the left. A pure mirror, owning no dock
  state.
- Rebuilds its toggles on `NOTIFICATION_THEME_CHANGED` and
  `NOTIFICATION_TRANSLATION_CHANGED`, and on the mirrored source's `child_order_changed`.
- Mirrors **enabled docks only**: a dock hidden by `set_dock_enabled` (§3.2) gets no toggle.
- Per-toggle refresh mirrors `_refresh_toggle`
  (`editor/gui/editor_bottom_drawer_strip.cpp:100-138`) in intent: dock icon with
  `get_icon_name()` theme fallback, tooltip from the shared helper of §4.7, and the
  `get_title_color()` font overrides with the transparent-means-no-override rule.
- Connects to each dock's `_tab_style_changed` the same way, so title and tint changes
  reach the rail.
- Right-click on a toggle opens the tile's dock context affordance at the mouse. Note the
  bottom strip routes this through `EditorDockManager::show_dock_context_popup`
  (`editor/gui/editor_bottom_drawer_strip.cpp:153-166`), which is a manager-owned popup
  operating on manager-owned docks; tile docks are not registered there. Right-click on a
  tile rail toggle is therefore **out of scope for the first implementation** unless a
  tile-scoped equivalent is built — see §7.
- Close button injected immediately **below** the active toggle and hidden when the drawer
  is closed — the vertical form of `_update_active_states`.
- The far end of the rail holds the expand button, shown only in `RAILED` mode.

### 4.5 Rail buttons: icon plus rotated label (D1)

`TabBar`/`TabContainer` have no vertical mode, and `Control::set_rotation` is ignored by
container layout, so the toggle content must be drawn. New `EditorSideRailButton : Button`
overriding `_draw` and `get_minimum_size`:

- Icon and label are composed as one horizontal strip (`[label][separation][icon]`), then
  drawn under a shared `-PI/2` transform so both share the same bottom-to-top quarter-turn
  orientation with the icon at the top of the toggle (revises the earlier unrotated-icon
  contract; see #2039). The canvas editor's vertical ruler labels already use this transform
  pattern (`editor/scene/canvas_item_editor_view.cpp:2307-2311`).
- Icon bounds and label bounds are disjoint and separated by the theme's icon/label
  separation; their union stays inside the button's stylebox content rect.
- `get_minimum_size` returns the composed strip's post-rotation size plus stylebox margins:
  width = max(icon height, font height) plus padding, and height = icon width +
  separation + rendered text width.
- Icon-only overflow mode centers an upright, unrotated icon and does not reserve label
  space.

**Overflow fallback.** The right rail mirrors up to four docks (Inspector, Signals, Groups,
History); labelled toggles for four will not fit a short tile, and a tile is shorter than
the window whenever the workspace is split. The rail measures the summed minimum height of
its labelled toggles against its own available height and, when they do not fit, rebuilds
itself icon-only. This is automatic, not a preference (D1). The fallback re-evaluates on
resize and on the dock set changing, and must be hysteretic (§4.5.1) so it cannot oscillate.

#### 4.5.1 No oscillation

Rebuilding icon-only shrinks the rail's own minimum width, which can change the layout that
produced the height measurement. The fit decision must therefore be computed against the
rail's *available height*, which the parent `HBoxContainer` fixes independently of the
rail's width, and the return-to-labelled threshold must exceed the drop-to-icon threshold by
at least one toggle's label height. Both thresholds are parameters of the pure function in
§8, and the non-oscillation property is a test case, not an assumption.

### 4.6 Persistence, and the offset-aliasing hazard this feature would otherwise trip

New per-side keys in the tile's own layout section, written by
`EditorTileDockRegion::save_layout`:

| Key | Type | Meaning |
|---|---|---|
| `tile_rail_left`, `tile_rail_right` | bool | `true` when that side is `RAILED` |
| `tile_drawer_dock_left`, `tile_drawer_dock_right` | String | `get_effective_layout_key()` of the active drawer dock, empty when the drawer is closed |

Absent keys mean `DOCKED` with no drawer dock, which is exactly today's behaviour. Per the
project's clean-break rule there is no migration of older layouts.

**Existing bug this feature would otherwise trip over.** `EditorTileDockRegion::save_layout`
writes the body's split offsets by position, with no visibility guard at all
(`editor/editor_tile_dock_region.cpp:139-146`):

```cpp
PackedInt32Array offsets = body->get_split_offsets();
if (offsets.size() >= 1) { p_config->set_value(p_section, "tile_dock_hsplit_1", int(offsets[0] / EDSCALE)); }
if (offsets.size() >= 2) { p_config->set_value(p_section, "tile_dock_hsplit_2", int(offsets[1] / EDSCALE)); }
```

`get_split_offsets()` has one entry per gap between **visible** children. With all three
columns visible there are two gaps: `offsets[0]` is *scene-tree ↔ center* and `offsets[1]`
is *center ↔ right tabs*. Collapse the left side and there is one gap, and `offsets[0]` now
means *center ↔ right tabs* — which `save_layout` happily writes into `tile_dock_hsplit_1`,
the scene tree's width. `load_layout` (`editor/editor_tile_dock_region.cpp:174-186`) reads
it back with the same positional assumption. Saving a layout while either side is collapsed
therefore corrupts the other side's width.

Today this is unreachable, because neither side is ever hidden. It becomes reachable the
moment §4.1 exists, which is why the fix is a prerequisite issue rather than a follow-up.

The fix is to key the two offsets by **identity rather than position** — resolve which gap
each key refers to from the current visible-child list, and skip writing a key whose gap
does not currently exist, leaving its previous value intact. The layout config is amended in
place rather than rewritten, and `load_layout` already guards on `has_section_key`, so a
skipped key keeps its stored value.

This is the concrete form of the general rule inherited from the bottom drawer, where
closing the drawer never overwrites the stored body height: **collapsing must not overwrite
remembered geometry.**

### 4.7 Tooltips

Two call sites build the `"<Shortcut Name> (<keys>)"` tooltip behind a `has_valid_event()`
guard and produce an **empty string** when the guard fails: the bottom strip
(`editor/gui/editor_bottom_drawer_strip.cpp:115-121`) and the dock manager's tab tooltips
(`editor/docks/editor_dock_manager.cpp:625`). Tile docks have no shortcut at all (§3.1), so
for the rails the fallback is not polish — it is the *only* tooltip source, and it is
load-bearing the moment the icon-only fallback of §4.5 triggers.

One shared helper, used by all three sites (both existing ones and the rail), returning the
shortcut form when a bound key exists and the dock's display title otherwise. Changing the
two existing sites from "empty" to "the title" is additive and is the sole permitted touch
of bottom-panel code under D5.

### 4.8 Shortcuts

Two new bindings via `ED_SHORTCUT_AND_COMMAND`, so they also appear in the Command Palette:

- `docks/toggle_left_tile_rail`
- `docks/toggle_right_tile_rail`

The `docks/` namespace is the established one for dock-related shortcuts. Each acts on the
**focused tile only**, toggling that side between `DOCKED` and `RAILED` and restoring the
last active drawer dock when re-entering `RAILED` — the per-side analogue of `Ctrl/Cmd+J`'s
"reopen the last opened bottom dock" (`editor/editor_node.cpp:11080`). Neither may rebind
`Ctrl/Cmd+J`, `Escape`, or `Shift+F12` (D5).

The existing `docks/open_*` command-palette entries continue to route through
`EditorNode::_focus_leaf_*_dock` (`editor/editor_node.cpp:11407-11416`); those handlers must
be taught that focusing a dock on a `RAILED` side sets that side's active drawer dock rather
than doing nothing.

### 4.9 Interaction with Distraction Free

Distraction Free (`EditorDockManager::set_docks_visible`,
`editor/docks/editor_dock_manager.cpp:1035-1046`) hides the **global** dock slots. It does
not currently touch the tile dock region, and this design does not change that: tile rails
and tile docks are unaffected by Distraction Free, exactly as tile docks are today.

Extending Distraction Free to collapse tile sides to rails is a natural follow-up and is
**not** in this design (§7).

### 4.10 An empty side keeps its rail

A side whose docks have all been disabled shows an empty rail rather than disappearing, so
the mode toggle and the shortcut retain a visible target. Unlike the global slots there is
no drag-and-drop of docks into a tile region today, so the rail is not a drop target and no
drop machinery is in scope.

## 5. Design — inspector density

### 5.1 The setting

New setting **`interface/theme/inspector_density`**, hint `Compact,Default,Spacious`,
defaulting to `Default` (D9). The value order deliberately matches the sibling
`interface/theme/spacing_preset`, which is `Compact,Default,Spacious,Custom` (§3.4); no
`Custom` level is offered.

The `interface/theme/` prefix is load-bearing, not cosmetic.
`EditorThemeManager::is_generated_theme_outdated`
(`editor/themes/editor_theme_manager.cpp:753-778`) decides whether to regenerate the theme
by testing an **explicit allowlist of setting groups**, the first of which is
`interface/theme`. A density setting named `interface/inspector/density` would be absent
from that allowlist and the theme would never regenerate when it changed — the setting
would appear inert until the next restart.

### 5.2 What must actually be scaled

Scaling `inspector_property_height` alone is close to a no-op. Both consumers treat it as a
**floor**, and it is derived from precisely the stylebox and font that set the floor anyway
(§3.4). To make `Compact` visible, the density factor must be applied to the inspector's
own metrics before the constant is derived from them:

- the content margins of the `EditorInspectorButton` stylebox family, from which
  `inspector_property_height` is computed (`editor/themes/theme_modern.cpp:2523-2527`,
  `editor/themes/theme_classic.cpp:2130-2134`);
- `inspector_property_height` itself, which then follows;
- `separation` on `EditorPropertyContainer`, and `h_separation` / `indent_size` on
  `EditorInspectorSection` — the constants read into the inspector's section theme cache
  (`editor/inspector/editor_inspector.cpp:3588-3591`). The factor is applied where these
  constants are **set in the themes**, never at the read site, so that custom themes and the
  theme hash stay authoritative.

Correctness bound: no scaled value may fall below the LineEdit font height plus
`2 * EDSCALE`, so text can never clip at any editor scale or font size.

The inspector already partly follows the global spacing setting — the line below those reads
`interface/theme/base_spacing` directly for key padding
(`editor/inspector/editor_inspector.cpp:3592`) — which is further reason to compose with the
global preset rather than introduce a second, competing spacing authority. The two settings
**compose**: the global preset scales the whole editor; the density scales the inspector on
top of it.

### 5.3 The regeneration contract, and a dead hash entry

Three mechanical requirements, all of which must hold or the setting silently does nothing:

1. The setting name must sit under a group `is_generated_theme_outdated` already watches —
   hence `interface/theme/`. This is what makes the editor *decide to* regenerate.
2. The new field must be added to `EditorThemeManager::ThemeConfiguration`, populated in
   `_create_theme_config` (`editor/themes/editor_theme_manager.cpp:238-256`), and folded
   into `ThemeConfiguration::hash()`. This is what makes the regenerated theme actually
   *differ*; without it the cached theme is reused.
3. It must **not** be registered with `set_restart_if_changed`. Density is a setting people
   flip while working; it applies live through theme regeneration.

**Hash the setting string, not the derived height.** `hash()` is taken immediately after
`_create_theme_config()` and *before* theme generation
(`editor/themes/editor_theme_manager.cpp:171-173`), while
`ThemeConfiguration::inspector_property_height` is only assigned *during* generation
(`editor/themes/theme_modern.cpp:2526`). The existing
`hash_murmur3_one_32(inspector_property_height, hash)` entry
(`editor/themes/editor_theme_manager.cpp:81`) therefore always hashes the struct's default
of `28` (`editor/themes/editor_theme_manager.h:80`) and contributes nothing. It is a
pre-existing latent bug, noted here so an implementer does not mistake it for a working
precedent; the density field must be hashed the way `spacing_preset` is (line 61), as the
setting string read in `_create_theme_config`.

Requirements 1 and 2 are independent failure modes and need separate coverage (§8).

Label layout stays inline at every level — no stacked-label mode is proposed here.

## 6. Work breakdown

Ordered by dependency. Sizes are relative.

| # | Slug | Scope | Depends on | Size |
|---|---|---|---|---|
| 1 | `inspector-density-setting` | The setting, the theme-config field, the hash entry, and the density factor applied to the inspector styleboxes, the derived height and the section constants in both themes (§5). | — | M |
| 2 | `tile-dock-offset-identity` | Make `EditorTileDockRegion` save/load its body split offsets by identity rather than position, skipping absent gaps (§4.6). Standalone bugfix; lands before any collapse state exists. | — | M |
| 3 | `dock-tooltip-fallback` | One shared tooltip helper falling back to the display title; adopted by the bottom strip and the dock manager's tab tooltips (§4.7). | — | S |
| 4 | `editor-side-rail-widget` | `EditorSideRailStrip` and `EditorSideRailButton`: mirroring, restyling, rotated labels, hysteretic icon-only overflow fallback, mounted in the tile's rail wrapper (§4.3, §4.4, §4.5). Rendering only; no collapse behaviour yet. | 3 | L |
| 5 | `tile-side-collapse-state` | The per-side state machine, the show/hide implementation, `focus_dock` integration, per-tile persistence (§4.1, §4.2, §4.6). | 2, 4 | L |
| 6 | `tile-side-collapse-shortcuts` | The two `ED_SHORTCUT_AND_COMMAND` bindings, focused-tile scoped, plus `_focus_leaf_*_dock` awareness (§4.8). | 5 | S |
| 7 | `tile-empty-side-keeps-rail` | A side with no enabled docks keeps its rail (§4.10). | 5 | S |

Issues 1, 2 and 3 are independent of each other and of the rail work, and can land first.

The global-column bug found during this review — `save_docks_to_config` writing `0` into
`dock_hsplit_N` for an invisible vsplit (`editor/docks/editor_dock_manager.cpp:741-750`) —
is **real but no longer on this feature's path**, since D7 moves the rails off the global
columns. It is filed separately as an independent bugfix, not as part of this epic.

## 7. Out of scope

- **Any bottom-panel behaviour change** (D5), other than the additive tooltip fallback.
- **Rails on the global `EditorDockManager` side columns** (D7). Import and FileSystem keep
  today's behaviour.
- **Unpinned / floating side drawers** (D6).
- **Right-click dock context menu on a tile rail toggle** (§4.4). The existing popup is
  manager-scoped and tile docks are not registered with the manager; a tile-scoped
  equivalent is a follow-up.
- **Distraction Free collapsing tile sides** (§4.9).
- **Stacked (label-above-field) inspector layout.** Inline labels at every density (§5).
- **Dragging docks into or out of a tile region.** No such affordance exists today.

## 8. Test plan

Following the repo rule that a test asserts on observable behaviour, never on source text.

**The available seams, verified.**

- `EditorSceneWorkspace::create_single_leaf_workspace(selection, &editor_data)` builds real
  tiles headlessly today (`tests/editor/test_scene_workspace.h:243`), so tile-level
  integration tests are feasible and are the primary route for §4.
- `EditorDockManager` is **not** constructible without an `EditorNode`: its constructor
  dereferences `EditorNode::get_singleton()` unconditionally
  (`editor/docks/editor_dock_manager.cpp:1119-1134`). Any coverage of manager-owned
  behaviour must go through a pure extracted function, never a constructed manager.

**Headless unit tests.** Any pure geometry or state-transition logic goes in a scene-free
header with a matching doctest, exactly as `editor/gui/bottom_drawer_geometry.h` pairs with
`tests/editor/test_bottom_drawer_geometry.h`. At minimum: the per-side state machine's
transition table (§4.1) table-driven; the labelled-versus-icon-only overflow decision as a
function of available height, measured toggle heights and the two hysteresis thresholds,
including a no-oscillation case (§4.5.1); and the offset-gap identity mapping of §4.6 as a
function of the visible-child list.

**Tile region doctests.**

- Collapsing one side of a tile hides exactly that side and leaves the other visible.
- Collapsing a side of one tile leaves a sibling tile untouched.
- `focus_dock` on a dock belonging to a `RAILED` side sets that side's active drawer dock
  and results in exactly one dock visible on that side.
- Save/load round trip: collapse a side, save, load, and assert both `tile_dock_hsplit_*`
  values survive — the regression test for §4.6.
- Save/load round trip: mode and active drawer dock restore per side, per tile.

**Inspector density doctests.**

- Each of the three levels produces a distinct `inspector_property_height`, ordered
  `Spacious > Default > Compact`.
- A measured `EditorProperty::get_minimum_size().height` is strictly smaller under `Compact`
  than under `Default` — the criterion that would have caught §5.2's floor problem.
- Changing the setting changes `ThemeConfiguration::hash()` — the guarantee that a
  regenerated theme differs from the cached one.
- The setting's name begins with a group that `is_generated_theme_outdated` watches — the
  guarantee that regeneration is attempted at all. Assert on the setting name against that
  allowlist, so renaming the setting out of `interface/theme/` fails a test rather than
  silently disabling the feature.
- Density and `spacing_preset` compose: `Compact` density under each of the three
  `spacing_preset` values yields a smaller height than `Default` density at that preset.
- No level produces a height below the LineEdit font height plus `2 * EDSCALE`.

**Editor automation walkthrough.** A GUI-path test through the MCP automation surface:
launch the editor, collapse the tile's right side from the rail, assert the inspector is
gone and the rail remains, open Signals from the rail, assert exactly one dock is visible on
that side, restore, assert the column returns at its original width. If a rail toggle cannot
be addressed by a stable role/name selector, add the missing role or metadata to
`editor/automation/` as part of the change rather than working around it.

**Visual review.** This is substantial editor UI work judged visually, so the implementing
change should ship a `scripts/review_gallery.py` walkthrough board: docked → railed →
drawer open → restored, captioned with what the reviewer should confirm at each step, plus a
design board pairing the three density levels before/after.

## 9. Open decisions

1. **Rotated label reading direction on the right rail.** This design specifies
   bottom-to-top on both sides for consistency. Mirroring the right rail to read
   top-to-bottom is the other defensible choice.
2. **Shortcut chords for §4.8.** No specific chord is proposed; whichever is chosen must be
   verified against the existing `ED_SHORTCUT` set on all platforms, including macOS
   overrides, before landing.
3. **Density factors.** `Spacious` ×1.25 / `Default` ×1.0 / `Compact` ×0.75 is the proposed
   starting point, to be tuned against the visual review board before the issue closes.

## 10. Revision log

The 2026-08-09 adversarial review changed the following, each grounded in the checkout:

- **Retargeted the rails from the global side columns to the tile dock region (D7).** The
  original design assumed Inspector/Scene tree/Signals/Groups/History lived in the global
  slots; `editor/editor_tile_dock_region.h:42-43` says otherwise, and the default layout
  leaves the global right column empty. The original feature would have collapsed Import,
  FileSystem and an empty column.
- **Forbade mounting rails inside a persisted split (D8).** The original mounting plan added
  rails to `main_hsplit`, which would have re-aliased every `dock_hsplit_N` key and added
  drag handles to the rails.
- **Replaced the persistence prerequisite.** The relevant bug is
  `EditorTileDockRegion::save_layout`'s positional offset writing, which has no visibility
  guard at all — strictly worse than the global-column bug the original design named.
- **Corrected the density implementation (D9).** `inspector_property_height` is a floor
  derived from the very styleboxes that set the floor, so the original single-constant scale
  would have been near-invisible; and `hash()` runs before the constant is computed, so the
  existing hash entry it cited as precedent is dead.
- **Corrected the tooltip premise.** Tile docks register no shortcut at all, so the fallback
  is the only tooltip source rather than polish — and there are two existing guard sites, not
  one.
- **Dropped the default-key-binding issue.** It targeted shortcuts that tile docks do not
  register.
- **Corrected the test seams.** `EditorDockManager` cannot be constructed headlessly;
  workspace tiles can.
