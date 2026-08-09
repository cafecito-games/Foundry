# Editor side rails and inspector density — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: use `superpowers-extended-cc:subagent-driven-development` (recommended) or `superpowers-extended-cc:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the left and right dock sides of each scene tile a persistent vertical rail
and an independent collapsed mode showing one dock at a time, and add a three-level
inspector density setting. Per
`docs/superpowers/specs/2026-08-09-editor-side-rails-and-inspector-density-design.md`.

**Architecture:** The rails are a vertical transposition of the shipping bottom drawer.
`EditorSideRailStrip` mirrors one side of a tile's dock region exactly as
`EditorBottomDrawerStrip` mirrors the bottom `TabContainer`, owning no state. The collapsed
mode is expressed purely through control visibility — no dock is ever reparented and no tab
order changes. All decidable logic lives in a scene-free, headlessly testable header
(`SideRailState`), following the `BottomDrawerGeometry` precedent. Inspector density is a
scale factor applied where the themes already derive the inspector's metrics.

**Tech stack:** C++ editor code (`editor/`, `editor/gui`, `editor/themes`,
`editor/settings`), doctest headers under `tests/editor/`, `doc/classes/EditorSettings.xml`,
`scripts/agent_build.py`.

**Scope:** 7 issues under one epic. Issues 1–3 are independent of each other and of the rail
work and can land in parallel immediately.

---

## Read this first — what the design targets, and what it does not

The rails act on the **per-tile dock region**, not on the global `EditorDockManager` side
columns. This is locked decision D7 and it is the single most load-bearing fact in the plan.

Verified in this checkout:

- `editor/editor_tile_dock_region.h:42-43` — *"Manages the in-tile dock strip:
  [left dock | center host | right tab stack]. Docks live here instead of the global
  EditorDockManager slots."*
- `editor/editor_scene_pane_tile.cpp:224-258` — every tile constructs its own
  `SceneTreeDock`, `InspectorDock`, `SignalsDock`, `GroupsDock`, `HistoryDock`.
- `editor/editor_node.cpp:11391-11398`, `:11477` — only `ImportDock`, `FileSystemDock` and
  the bottom docks are added to the global manager.
- `editor/editor_node.cpp:11429-11431` — the default layout fills only `dock_3`
  (`DOCK_SLOT_LEFT_UR`, Import) and `dock_4` (`DOCK_SLOT_LEFT_BR`, FileSystem). **The
  global right column is empty by default.**

An implementer who starts editing `EditorDockManager` slot visibility is on the wrong
surface. The tile structure to work against is:

```
ScenePaneTile
└── focus_frame (PanelContainer)              editor/editor_scene_pane_tile.cpp:209-218
    └── body (HSplitContainer)                                              :215
        ├── scene_tree_dock                   left dock    (place_left)
        ├── content_host                      center host                   :236
        └── right_tabs (TabContainer)         Inspector, Signals, Groups, History
```

## Harness constraints — read before writing any test

Verified in this checkout. Getting these wrong wastes a build cycle each time.

- **Editor doctests are headers, and must be registered.** Every test file under
  `tests/editor/` is a `#pragma once` header included from `tests/test_main.cpp`
  (the editor block starts at `tests/test_main.cpp:50`). A new test header that is not
  added to that include list compiles nothing and runs nothing, silently. Keep the list
  alphabetical.
- **Skeletons:** `python tests/create_test.py <Name> <path>`, where `path` is relative to
  `tests/`.
- **Case naming drives filtering.** Existing editor cases are tagged in the case *name*,
  e.g. `TEST_CASE("[Editor][BottomDrawerGeometry] Clamp body height")`
  (`tests/editor/test_bottom_drawer_geometry.h:39`) and
  `TEST_CASE("[SceneWorkspace][SceneTree][Editor] tile-self-contained")`
  (`tests/editor/test_scene_workspace.h:263`).
  Filter with `--case "*SideRail*"`. **A filter that matches nothing exits non-zero** — it
  is a hard failure, not an empty pass.
- **Tiles are constructible headlessly; `EditorDockManager` is not.**
  `EditorSceneWorkspace::create_single_leaf_workspace(selection, &editor_data)` builds real
  tiles in existing tests (`tests/editor/test_scene_workspace.h:243`), and individual docks
  construct without an `EditorNode` (`tests/editor/test_dock_scene_context_binding.h:171`,
  `:189`). By contrast `EditorDockManager::EditorDockManager()` dereferences
  `EditorNode::get_singleton()` unconditionally three times
  (`editor/docks/editor_dock_manager.cpp:1119-1134`), so **no test may construct one**.
  Coverage of manager-owned logic must go through a pure extracted function.
- **Tests that write files** must use the shared scratch space
  (`FOUNDRY_TEST_SCRATCH`, set to `$REPO_ROOT/.test_scratch` by `scripts/agent_build.py`),
  never the repo root or a fixture directory.
- **Editor settings are documented.** `interface/theme/*` settings have `<member>` entries
  in `doc/classes/EditorSettings.xml` (see `interface/theme/spacing_preset` at
  `doc/classes/EditorSettings.xml:1230`). A new setting without a doc entry fails the
  pre-commit doc check.
- **Test authoring rules** (`CLAUDE.md`): assert on observable behaviour — a returned
  value, a computed constant, a written config key. Never assert that a source file
  contains a substring, never slice a source file on a signature, never assert that
  another test exists.
- **Build and run:**

  ```sh
  # fast iteration
  python3 scripts/agent_build.py --backend ninja --test --case "*SideRail*"
  # required strict validation before any PR
  python3 scripts/agent_build.py
  ./bin/foundry.* --headless test run --force-colors
  ```

  On Linux prefix full-suite runs with `DISPLAY=:1` so GUI-dependent tests run instead of
  self-skipping. Trust the `[doctest] Status: SUCCESS!` line; the run may still exit
  non-zero at cleanup from leak reporting.

## File map and ownership

| Path | Role |
|---|---|
| `editor/gui/side_rail_state.h` | **New.** Scene-free per-side state machine, rail-fit math, and offset-gap identity mapping. Headlessly testable, mirrors `editor/gui/bottom_drawer_geometry.h`. |
| `editor/gui/editor_side_rail_strip.{h,cpp}` | **New.** The rail control; mirrors `editor/gui/editor_bottom_drawer_strip.{h,cpp}`. |
| `editor/gui/editor_side_rail_button.{h,cpp}` | **New.** Icon + rotated-label toggle button. |
| `editor/gui/dock_tooltip.h` | **New.** The one shared tooltip helper (Issue 3). |
| `editor/editor_tile_dock_region.{h,cpp}` | Per-side mode, visibility application, offset identity mapping, persistence. |
| `editor/editor_scene_pane_tile.{h,cpp}` | The rail wrapper `HBoxContainer`, rail construction and mounting. |
| `editor/editor_node.cpp` | The two new shortcuts; `_focus_leaf_*_dock` railed-side awareness. |
| `editor/themes/theme_modern.cpp`, `theme_classic.cpp` | Density scaling of inspector styleboxes and constants; rail styleboxes. |
| `editor/themes/editor_theme_manager.{h,cpp}` | `ThemeConfiguration` field + `hash()` entry. |
| `editor/settings/editor_settings.cpp` | The density setting registration. |
| `doc/classes/EditorSettings.xml` | Density setting documentation. |
| `tests/editor/test_*.h` + `tests/test_main.cpp` | Coverage, registered. |

## Do not touch

- Per D5, observable bottom-panel behaviour must be identical before and after this epic.
  Do not edit behaviour in `editor/gui/editor_bottom_panel.{h,cpp}`,
  `editor/gui/bottom_drawer_geometry.h`, or `editor/gui/bottom_drawer_layout.h`. Issue 3's
  touch of `editor_bottom_drawer_strip.cpp` is the sole exception and is additive only.
- Per D7, do not change `EditorDockManager` slot visibility, `dock_slot_index`, or the
  `dock_hsplit_N` / `dock_split_N` persistence. The global-column offset-zeroing bug found
  during review is **out of this epic's scope** — see "Filing notes".
- Per D8, no rail may become a child of `body`, `main_hsplit`, or any other
  `SplitContainer` whose offsets are persisted positionally.

---

## Epic body

> **Title:** Tile side rails and inspector density
>
> Collapse the left and right dock sides of each scene tile independently into persistent
> icon+label rails, showing one dock at a time when collapsed, and add a three-level
> inspector density setting.
>
> Design: `docs/superpowers/specs/2026-08-09-editor-side-rails-and-inspector-density-design.md`
> Plan: `docs/superpowers/plans/2026-08-09-editor-side-rails-and-inspector-density.md`
>
> **Locked decisions**
> - Rail buttons show icon **and** label, the label drawn rotated. Icon-only is an
>   automatic overflow fallback, not a user preference.
> - A collapsed side shows **exactly one dock at a time**.
> - Left and right are **fully independent** — no shared setting, shortcut or state. Every
>   tile keeps its own state.
> - Inspector density ships three levels, defaulting to `Default`, and scales the
>   inspector's styleboxes as well as its row-height constant.
> - **The rails target the per-tile dock region, not the global dock columns.** The global
>   columns hold only Import and FileSystem, and the global right column is empty by
>   default.
> - **A rail is never a child of a persisted `SplitContainer`.**
> - **The bottom panel does not change.** Issue 3 is additive only.
> - Unpinned/floating side drawers, tile-scoped right-click dock menus, and Distraction
>   Free collapsing tile sides are out of scope.
>
> **Sequencing:** issues 1, 2 and 3 are independent and can start immediately. Issue 4
> (rail widget) needs 3. Issue 5 (collapse state) needs 2 and 4, and is the keystone.
>
> **Issue 2 is a bugfix that must land before issue 5.** `EditorTileDockRegion::save_layout`
> writes the tile body's split offsets by array position with no visibility guard; once
> either side can be hidden, saving re-aliases the two width keys onto the wrong dividers.

---

## Issue 1 — Inspector density setting

**Depends on:** nothing.

### Context

Every inspector property row derives its height from one theme constant.
`EditorProperty::get_minimum_size` seeds from `theme_cache.inspector_property_height`
(`editor/inspector/editor_inspector.cpp:218-224`, also used at `:317`), and
`EditorSpinSlider::get_minimum_size` reads the same constant
(`editor/gui/editor_spin_slider.cpp:68`). It is computed once per theme, identically in
both themes, as the inspector button stylebox min height plus the LineEdit font height
(`editor/themes/theme_modern.cpp:2523-2527`, `editor/themes/theme_classic.cpp:2130-2134`).

**Scaling that constant alone is close to a no-op.** Both consumers use it as a *floor*:
`EditorProperty` seeds `Size2(0, height)` and then maxes against its children's minimum
sizes; `EditorSpinSlider` computes `MAX(stylebox + font height, constant)`. Because the
constant is itself derived from the stylebox and font that produce those child minimums,
lowering it just lets the children win. The density factor must reach the inspector's
styleboxes, not only the constant derived from them.

### Scope

Add `interface/theme/inspector_density` with hint `Compact,Default,Spacious`, defaulting to
`Default`, applied live through theme regeneration to the inspector's styleboxes, its
derived row height, and its section spacing constants.

**The `interface/theme/` prefix is required, not cosmetic.**
`EditorThemeManager::is_generated_theme_outdated`
(`editor/themes/editor_theme_manager.cpp:753-778`) decides whether to regenerate by testing
an explicit allowlist of setting groups, the first being `interface/theme`. A setting named
`interface/inspector/density` would be absent from that allowlist and would appear inert
until restart.

Two independent failure modes, both of which must be closed:
1. Wrong group → regeneration is never attempted.
2. Missing `hash()` entry → regeneration is attempted but the cached theme is reused
   because the config hash is unchanged.

**Hash the setting string, not the derived height.** `hash()` runs immediately after
`_create_theme_config()` and *before* theme generation
(`editor/themes/editor_theme_manager.cpp:171-173`), while
`ThemeConfiguration::inspector_property_height` is assigned *during* generation
(`theme_modern.cpp:2526`). The existing `hash_murmur3_one_32(inspector_property_height, hash)`
at `editor/themes/editor_theme_manager.cpp:81` therefore always hashes the struct default
`28` (`editor/themes/editor_theme_manager.h:80`) and contributes nothing. Do not copy it as
a pattern; hash the density field the way `spacing_preset` is hashed at line 61.

### Implementation

- [ ] Register the setting beside `interface/theme/spacing_preset`
      (`editor/settings/editor_settings.cpp:577`) using `EDITOR_SETTING_BASIC` with
      `Variant::STRING`, `PROPERTY_HINT_ENUM`, default `"Default"`, hint string
      `"Compact,Default,Spacious"` — same value order as the sibling setting. Do **not**
      call `set_restart_if_changed` for it.
- [ ] Add a `String inspector_density` field to `EditorThemeManager::ThemeConfiguration`
      (`editor/themes/editor_theme_manager.h:53-90`), populate it in `_create_theme_config`
      alongside `spacing_preset` (`editor/themes/editor_theme_manager.cpp:245`), and fold it
      into `ThemeConfiguration::hash()` as a string hash beside the `spacing_preset` entry
      (`editor/themes/editor_theme_manager.cpp:61`).
- [ ] Define the factor once — `Compact` ×0.75, `Default` ×1.0, `Spacious` ×1.25 — as a
      single helper both themes call, so the two themes cannot drift.
- [ ] In both themes, apply the factor to the content margins of the
      `EditorInspectorButton` stylebox family before `inspector_property_height` is derived
      from it (`theme_modern.cpp:2523-2527`, `theme_classic.cpp:2130-2134`), so the derived
      constant and the child controls move together. Round to int.
- [ ] Clamp every scaled value to a floor of `font->get_height(font_size) + 2 * EDSCALE` so
      text can never clip at any editor scale or font size.
- [ ] Apply the same factor where the themes **set** `separation` on
      `EditorPropertyContainer` and `h_separation` / `indent_size` on
      `EditorInspectorSection` — the constants the inspector reads into its section theme
      cache at `editor/inspector/editor_inspector.cpp:3588-3591`. Apply it at the theme
      write sites, **never** at that read site: scaling on read would bypass custom themes
      and desynchronise the theme hash.
- [ ] Add a `<member name="interface/theme/inspector_density" type="String" ...>` entry to
      `doc/classes/EditorSettings.xml`, cross-referencing
      `[member interface/theme/spacing_preset]` the way the neighbouring spacing settings do
      (`doc/classes/EditorSettings.xml:1182-1230`).

### Acceptance criteria

- [ ] The setting exists at exactly `interface/theme/inspector_density`, type String, hint
      enum `Compact,Default,Spacious`, default `Default`.
- [ ] The setting is **not** in the restart-required set.
- [ ] The three levels produce three **distinct** `inspector_property_height` values,
      strictly ordered `Spacious > Default > Compact`.
- [ ] **A measured `EditorProperty::get_minimum_size().height` for a representative property
      is strictly smaller under `Compact` than under `Default`, and strictly larger under
      `Spacious`.** This is the criterion that distinguishes a working change from one that
      only moves an unused floor.
- [ ] No level produces any scaled value below `font height + 2 * EDSCALE`.
- [ ] `ThemeConfiguration::hash()` differs between any two levels with all other settings
      held equal.
- [ ] The setting name's group prefix is one that `is_generated_theme_outdated` watches.
- [ ] Density composes with `spacing_preset`: for each of the three `spacing_preset`
      values, `Compact` density yields a smaller measured property height than `Default`
      density at that same preset.
- [ ] Both `theme_modern` and `theme_classic` honour the setting (no theme is left behind).
- [ ] `doc/classes/EditorSettings.xml` documents the setting and `pre-commit run --all-files`
      passes its doc checks.

### Tests

New `tests/editor/test_inspector_density.h`, registered in `tests/test_main.cpp`, cases
named `[Editor][InspectorDensity] ...`:

- [ ] Three levels produce distinct, correctly ordered `inspector_property_height` values.
- [ ] Measured `EditorProperty` minimum height is ordered `Spacious > Default > Compact`.
- [ ] Minimum-height clamp holds at the smallest level and smallest font size.
- [ ] `hash()` changes across levels.
- [ ] The setting name is under a group the outdated-check watches. Assert this against the
      allowlist, so renaming the setting out of `interface/theme/` fails a test rather than
      silently disabling the feature.
- [ ] Composition with all three `spacing_preset` values.

Runtime-liveness (the setting taking effect without restart) is guaranteed by the two hash
and group criteria above; additionally verify it once by hand or through editor automation
before closing.

### Out of scope

Stacked labels, property-description rendering, and any other inspector layout change.
Tuning the three factors beyond the ×0.75/×1.0/×1.25 starting point is expected during
visual review and does not need its own issue.

---

## Issue 2 — Tile dock split offsets keyed by identity, not position

**Depends on:** nothing. **Bugfix; must land before Issue 5.**

### Context

`EditorTileDockRegion::save_layout` writes the tile body's split offsets by array position,
with no visibility guard at all (`editor/editor_tile_dock_region.cpp:139-146`):

```cpp
PackedInt32Array offsets = body->get_split_offsets();
if (offsets.size() >= 1) { p_config->set_value(p_section, "tile_dock_hsplit_1", int(offsets[0] / EDSCALE)); }
if (offsets.size() >= 2) { p_config->set_value(p_section, "tile_dock_hsplit_2", int(offsets[1] / EDSCALE)); }
```

`SplitContainer::get_split_offsets()` returns one entry per gap between **visible** children
— hidden children are excluded from `valid_children`
(`scene/gui/split_container.cpp:978`, `:995`, `:1044`, `:1065`), and the array is sized
`valid_children - 1` (`scene/gui/split_container.cpp:1174`, `:372`).

So with all three columns visible there are two gaps: `offsets[0]` is *scene tree ↔ center*
and `offsets[1]` is *center ↔ right tabs*. Hide the scene tree and there is one gap, and
`offsets[0]` now means *center ↔ right tabs* — which `save_layout` writes into
`tile_dock_hsplit_1`, the scene tree's width. `load_layout`
(`editor/editor_tile_dock_region.cpp:174-186`) reads it back under the same positional
assumption.

Today neither side is ever hidden, so this is unreachable. Issue 5 makes both sides hideable
and persistently so, at which point saving while collapsed silently swaps the two widths.

Two facts make "resolve by identity, skip absent gaps" correct and sufficient:
- The layout config is amended in place rather than rewritten, so a skipped key retains its
  previous value.
- `load_layout` already guards every read on `has_section_key`, so an absent key is handled.

### Implementation

- [ ] Add a pure function to `editor/gui/side_rail_state.h` that maps the tile body's
      ordered visible-child list to gap identities — `LEFT_CENTER`, `CENTER_RIGHT` — and
      returns, for each identity, the offset index or "absent". It must take the visible
      child list as data, so it is testable with no controls.
- [ ] Rewrite `save_layout` to use it: write `tile_dock_hsplit_1` only when `LEFT_CENTER`
      is present, `tile_dock_hsplit_2` only when `CENTER_RIGHT` is present, and leave an
      absent key untouched.
- [ ] Rewrite `load_layout` to use it symmetrically: apply a stored key only to the offset
      index that currently corresponds to its identity, and ignore a key whose gap does not
      currently exist. Do not resize the offsets array to a fixed 2 as the current code does
      at `editor/editor_tile_dock_region.cpp:176-178`.
- [ ] Keep the `EDSCALE` divide/multiply on both paths exactly as today.

### Acceptance criteria

- [ ] With all three columns visible, save writes both keys with today's meanings and
      values — byte-identical to current behaviour.
- [ ] With the left dock hidden, save leaves `tile_dock_hsplit_1` at its previous stored
      value and writes the surviving gap to `tile_dock_hsplit_2`.
- [ ] With the right tabs hidden, save leaves `tile_dock_hsplit_2` at its previous stored
      value and writes the surviving gap to `tile_dock_hsplit_1`.
- [ ] Round trip: set both widths, hide one side, save, show the side, load — both original
      widths are restored.
- [ ] Load ignores a key whose gap is currently absent rather than applying it to the wrong
      divider.
- [ ] No behaviour change for the all-visible case (the overwhelmingly common one).

### Tests

New `tests/editor/test_tile_dock_offsets.h`, registered in `tests/test_main.cpp`, cases
named `[Editor][TileDockOffsets] ...`:

- [ ] The identity-mapping function, table-driven over every visible-child combination,
      asserted with no controls constructed.
- [ ] All five behavioural criteria above against a real `ConfigFile` and a real tile built
      with `EditorSceneWorkspace::create_single_leaf_workspace` — the seam existing tests
      already use (`tests/editor/test_scene_workspace.h:243`).

If a real tile turns out not to be constructible in some configuration, cover the
behavioural criteria against a bare `HSplitContainer` with stand-in children rather than
dropping them, and record which route was taken in the PR description.

---

## Issue 3 — Dock toggle tooltip falls back to the dock title

**Depends on:** nothing. **Additive only; the sole permitted touch of bottom-panel code.**

### Context

Two call sites build the same tooltip behind the same guard, and both produce an **empty
string** when it fails:

```cpp
// editor/gui/editor_bottom_drawer_strip.cpp:115-121
String tooltip;
Ref<Shortcut> shortcut = dock->get_dock_shortcut();
if (shortcut.is_valid() && shortcut->has_valid_event()) {
    tooltip = TTR(shortcut->get_name()) + " (" + shortcut->get_as_text() + ")";
}
toggle->set_tooltip_text(tooltip);
```

```cpp
// editor/docks/editor_dock_manager.cpp:625 — tab tooltips
if (p_dock->shortcut.is_valid() && p_dock->shortcut->has_valid_event()) { ... }
```

`has_valid_event()` is false for a shortcut that exists but has no default key, and it is
also false when there is no shortcut at all.

**The tile docks the rails will mirror have no shortcut whatsoever.** `ScenePaneTile`
constructs them with the global-registration flag `false`
(`editor/editor_scene_pane_tile.cpp:224` and siblings), and that flag is exactly what gates
the `set_dock_shortcut` call (`editor/docks/signals_dock.cpp:52-57`,
`groups_dock.cpp:51`, `history_dock.cpp:270`, `inspector_dock.cpp:763`). So for the rails
the title fallback is not polish — it is the **only** tooltip source, and it becomes the
only affordance at all once the icon-only overflow fallback (Issue 4) triggers.

Among the manager-owned docks, only FileSystem binds a key
(`Alt+F`, `editor/docks/filesystem_dock.cpp:4345`); Import registers a shortcut with no
default event (`editor/docks/import_dock.cpp:749`), so it currently shows no tooltip either.

### Scope

One shared helper returning the `"<Shortcut Name> (<keys>)"` form when a bound key exists
and the dock's display title otherwise. Adopted by both existing call sites and, once it
exists, by the rail.

### Implementation

- [ ] Add the helper in a small new header (`editor/gui/dock_tooltip.h`) taking an
      `EditorDock *` and returning `String`, so the strip, the dock manager and the rail
      cannot drift.
- [ ] Use it in `EditorBottomDrawerStrip::_refresh_toggle` and in
      `EditorDockManager::_update_tab_style`. Note the dock-manager site *appends* to an
      existing `tooltip` string — preserve that composition, changing only what the shortcut
      clause contributes when there is no bound key.
- [ ] No other change to strip or tab behaviour: button text, icon resolution, colour
      overrides, ordering and close-button placement are untouched.

### Acceptance criteria

- [ ] A dock with a shortcut that has a bound key yields exactly the current string,
      `"<Shortcut Name> (<keys>)"` — byte-identical to today.
- [ ] A dock with no shortcut yields its display title, not an empty string.
- [ ] A dock with a shortcut that has no valid event is treated as having no shortcut and
      yields the title.
- [ ] Every bottom-strip toggle has a non-empty tooltip.
- [ ] The dock-manager tab tooltip keeps whatever prefix it composed before the shortcut
      clause, with the fallback appended under the same rules.
- [ ] No observable change to any other bottom-panel behaviour (D5).

### Tests

New `tests/editor/test_dock_toggle_tooltip.h`, registered in `tests/test_main.cpp`, cases
named `[Editor][DockToggleTooltip] ...`, covering the three tooltip states against real
`EditorDock` instances. Test the helper directly — it takes a dock and returns a string, so
it needs neither a strip nor a manager.

---

## Issue 4 — `EditorSideRailStrip` and `EditorSideRailButton`

**Depends on:** Issue 3. **Widget only — no collapse behaviour.**

### Context

The rail is the vertical transposition of `EditorBottomDrawerStrip`
(`editor/gui/editor_bottom_drawer_strip.h:45`), whose header comment states the contract to
carry over: *"It never owns drawer state: it is a pure mirror driven by the TabContainer's
signals."*

The right side of a tile is a plain `TabContainer` (`right_tabs`,
`editor/editor_tile_dock_region.cpp:50-57`), so the mirroring is structurally identical to
the bottom strip's. The left side is the ordered `EditorDock` children of `body` preceding
`content_host` — one dock today, but `place_left`
(`editor/editor_tile_dock_region.cpp:60-68`) admits more, so the rail must iterate rather
than assume one.

### Scope

Two new controls, mounted and rendering. Clicking a toggle focuses a dock (existing
`EditorTileDockRegion::focus_dock`); collapse arrives in Issue 5.

### Implementation

- [ ] `EditorSideRailButton : Button` drawing an unrotated icon above a 90°-rotated label
      reading bottom-to-top. Use the `draw_set_transform_matrix` pattern the canvas editor's
      vertical ruler labels already use — a `-PI/2` transform around the draw, restored
      immediately after (`editor/scene/canvas_item_editor_view.cpp:2307-2311`).
      `get_minimum_size` returns width = max(icon width, font height) + padding, and
      height = icon height + separation + rendered text width.
- [ ] `EditorSideRailStrip`, constructed with the side it represents and the source it
      mirrors. Carry over from the bottom strip, one-for-one:
  - rebuild on `NOTIFICATION_THEME_CHANGED` and `NOTIFICATION_TRANSLATION_CHANGED`
    (`editor_bottom_drawer_strip.cpp:43-53`) and on the mirrored source's
    `child_order_changed`;
  - mirror **enabled docks only** — a dock hidden by
    `EditorTileDockRegion::set_dock_enabled` (`editor/editor_tile_dock_region.cpp:86-97`)
    gets no toggle, and re-enabling it restores one;
  - per-toggle refresh mirroring `_refresh_toggle` (`:100-138`): dock icon with the
    `get_icon_name()` theme fallback, tooltip via the Issue 3 helper, and the
    `get_title_color()` font overrides with the transparent-means-no-override rule;
  - connection to each dock's `_tab_style_changed` (`:88-95`, `:140-146`);
  - a close button injected immediately **below** the active toggle and hidden when nothing
    is active — the vertical form of `_update_active_states` (`:172-192`);
  - an expand button at the far end, hidden in `DOCKED` mode.
- [ ] **Do not** carry over the right-click context popup. The bottom strip routes it
      through `EditorDockManager::show_dock_context_popup`
      (`editor_bottom_drawer_strip.cpp:153-166`), which operates on manager-registered
      docks; tile docks are not registered there. Right-click on a tile rail toggle is out
      of scope (see the epic's out-of-scope list).
- [ ] Overflow fallback: put the fit decision in `editor/gui/side_rail_state.h` as a pure
      function of available height, measured toggle heights and two thresholds. The
      return-to-labelled threshold must exceed the drop-to-icon threshold by at least one
      toggle's label height, so a rebuild cannot flip the decision back and oscillate.
      Re-evaluate on resize and on the dock set changing. This is automatic, never a user
      setting.
- [ ] Mount the rails in a **new `HBoxContainer` inside `focus_frame`**, wrapping `body`:
      `focus_frame → rail_hbox → [left rail, body, right rail]`
      (`editor/editor_scene_pane_tile.cpp:209-218`). **Do not add a rail to `body`, to
      `main_hsplit`, or to any `SplitContainer`** — those containers persist their offsets
      by position, and an extra visible child silently re-aliases every saved width key
      (D8, and the mechanism in Issue 2).
- [ ] Add rail styleboxes to both themes, following the `BottomDrawerStrip` type variation
      (`editor/themes/theme_modern.cpp:1833-1834`).

### Acceptance criteria

- [ ] Each side of a tile shows a rail with exactly one toggle per enabled dock on that
      side, in the source's order.
- [ ] Disabling a dock removes its toggle; re-enabling restores it in the right position.
- [ ] Toggles render icon and rotated label; the label reads bottom-to-top on both sides.
- [ ] `get_minimum_size` accounts for the rotated text, so no label is clipped at any
      editor scale or font size.
- [ ] When the labelled toggles do not fit the available height, the rail rebuilds
      icon-only; when they fit again after a resize, labels return.
- [ ] The fit decision cannot oscillate: no sequence of resizes produces an unbounded
      rebuild loop.
- [ ] Every toggle has a non-empty tooltip in both label modes (via Issue 3).
- [ ] A dock's title, icon, or title-colour change is reflected on its toggle without a
      manual rebuild.
- [ ] Theme change and locale change both rebuild the toggles.
- [ ] **`body->get_split_offsets().size()` is unchanged by the presence of the rails**, and
      a tile's saved `tile_dock_hsplit_1` / `tile_dock_hsplit_2` values are identical
      before and after this issue.
- [ ] Each tile in a split workspace gets its own pair of rails, mirroring its own docks.
- [ ] Both `theme_modern` and `theme_classic` style the rail.

### Tests

New `tests/editor/test_side_rail_strip.h`, registered in `tests/test_main.cpp`, cases named
`[Editor][SideRail] ...`:

- [ ] Toggle set matches the side's enabled dock set, in order, and rebuilds when a dock is
      enabled or disabled.
- [ ] Rotated-label minimum size is at least the rendered text width in the long dimension.
- [ ] The fit decision exercised directly as a pure function of available height, measured
      toggle heights and the two thresholds, including a no-oscillation case.
- [ ] Tooltip is non-empty for a dock with a shortcut and for one without.
- [ ] Split-offset invariance: build a tile, record `tile_dock_hsplit_*`, and assert the
      rails did not change the offset count or the saved values.

---

## Issue 5 — Per-side collapse state (keystone)

**Depends on:** Issues 2 and 4.

### Context

The per-side, per-tile mode from the design (§4.1):

| Mode | Rail | Dock side of the tile body | Docks visible |
|---|---|---|---|
| `DOCKED` | visible | shown as today | left: Scene tree. right: the selected tab |
| `RAILED`, drawer open on *D* | visible | shown, holding only *D* | exactly one: *D* |
| `RAILED`, drawer closed | visible | hidden | none |

Implemented purely through control visibility — no reparenting, so tab order and dock
identity never change:

- **Right side.** `right_tabs` is already a `TabContainer`, so one-at-a-time is native.
  Drawer open on *D* is `right_tabs->show()` plus `set_current_tab(index_of(D))`; drawer
  closed is `right_tabs->hide()`. `tile_dock_right` and
  `tile_dock_right_selected_tab_idx` keep their current meanings.
- **Left side.** Show or hide the `EditorDock` children of `body` that precede
  `content_host`. Iterate them; do not hard-code the single Scene tree dock.

`HSplitContainer` drops hidden children from its valid-children set
(`scene/gui/split_container.cpp:978`, `:995`, `:1044`, `:1065`), so a closed-drawer side
collapses to nothing with no extra code — **and that same fact is exactly why Issue 2 must
land first.**

### Hazards — each has an acceptance criterion below

1. **Offset aliasing.** Hiding a side changes `body->get_split_offsets()`'s size and the
   meaning of each entry. Covered by Issue 2; this issue must not reintroduce a positional
   assumption anywhere.
2. **`set_dock_enabled` fights the mode.** `EditorTileDockRegion::set_dock_enabled`
   (`editor/editor_tile_dock_region.cpp:86-97`) sets dock visibility directly and moves the
   right-tab selection to the first visible tab. On a `RAILED` side it must instead update
   the active drawer dock, and disabling the active drawer dock must close the drawer or
   move it to another enabled dock — never leave the drawer open on a disabled dock.
3. **`focus_dock` on a railed side.** `EditorTileDockRegion::focus_dock`
   (`editor/editor_tile_dock_region.cpp:78-85`) selects a tab and grabs focus. On a `RAILED`
   side it must first set that side's active drawer dock, or the focus lands on a hidden
   control.
4. **Restore width.** Returning a side to `DOCKED` must restore the width it had before
   collapsing, which is what Issue 2's identity keying protects. If the split container
   needs a specific show/hide ordering to land on the right offset — the bottom-slot code
   documents one such constraint at `editor/docks/editor_dock_manager.cpp:1041` — establish
   it empirically and record it in a comment.

### Implementation

- [ ] Put the state machine in `editor/gui/side_rail_state.h` — scene-free, no editor
      dependencies, unit-testable headlessly, mirroring `editor/gui/bottom_drawer_geometry.h`.
      It owns the transition table and the per-side visibility decision; it owns no controls.
      It already holds Issue 2's gap-identity mapping and Issue 4's fit decision.
- [ ] Add per-side mode and active-drawer-dock state to `EditorTileDockRegion`, with the
      transitions from design §4.1, and apply visibility through the rules above.
- [ ] Handle hazards 2, 3 and 4.
- [ ] Wire the rail: clicking the toggle of the visible dock in `DOCKED` collapses the side;
      clicking a non-visible dock's toggle in `DOCKED` focuses it; any toggle in `RAILED`
      opens that dock; the active toggle or close button in `RAILED` closes the drawer; the
      rail's expand button returns to `DOCKED`.
- [ ] Persist `tile_rail_left` / `tile_rail_right` (bool) and
      `tile_drawer_dock_left` / `tile_drawer_dock_right` (the dock's
      `get_effective_layout_key()`, empty when closed) in the tile's own layout section,
      written by `EditorTileDockRegion::save_layout`
      (`editor/editor_tile_dock_region.cpp:131-168`). Absent keys mean `DOCKED` with no
      drawer dock — today's behaviour. Per the project's clean-break rule, no migration of
      older layouts.
- [ ] A stored `tile_drawer_dock_*` naming a dock that no longer exists, or that is
      currently disabled, loads as "drawer closed" rather than erroring.

### Acceptance criteria

- [ ] Collapsing the left side of a tile hides exactly the left dock and leaves the right
      tabs visible, and vice versa. The two sides never affect each other.
- [ ] In `RAILED` with a drawer open, exactly one dock is visible on that side.
- [ ] In `RAILED` with the drawer closed, that side occupies exactly the rail's width.
- [ ] Clicking the active toggle in `RAILED` closes the drawer; clicking another switches to
      it without ever showing two.
- [ ] Disabling the active drawer dock closes the drawer or moves it to another enabled
      dock; the drawer is never open on a disabled dock (hazard 2).
- [ ] `focus_dock` on a dock belonging to a `RAILED` side sets that side's active drawer
      dock and results in exactly one visible dock on that side (hazard 3).
- [ ] Restoring a side to `DOCKED` restores its pre-collapse width (hazard 4 and Issue 2).
- [ ] Save → load round trip restores mode, active drawer dock, and both widths, per side
      and per tile independently.
- [ ] Collapsing and saving does not modify `tile_dock_right` or
      `tile_dock_right_selected_tab_idx`.
- [ ] A stored drawer dock that no longer exists or is disabled loads as "drawer closed"
      without an error.
- [ ] Collapsing a side in one tile of a split workspace leaves every sibling tile
      unchanged.
- [ ] A side whose every dock is disabled still shows its rail (Issue 7 covers the rest of
      the empty-side behaviour; this criterion only requires the rail not to vanish).

### Tests

New `tests/editor/test_side_rail_state.h` (pure state machine) and
`tests/editor/test_side_rail_collapse.h` (integration against a real tile), both registered
in `tests/test_main.cpp`, cases named `[Editor][SideRail] ...`:

- [ ] Table-driven coverage of every transition in the design §4.1 table.
- [ ] Per-side visibility decision for all combinations of mode, active dock, and empty
      sides.
- [ ] One integration case per hazard (2, 3, 4).
- [ ] Save/load round trip preserving mode, active dock and both widths, per side.
- [ ] Independence: a matrix over the four combinations of left/right mode.
- [ ] Tile independence: collapse in one tile of a two-tile workspace, assert the other is
      untouched.

---

## Issue 6 — Collapse shortcuts

**Depends on:** Issue 5.

### Scope

Two `ED_SHORTCUT_AND_COMMAND` bindings, `docks/toggle_left_tile_rail` and
`docks/toggle_right_tile_rail`, each acting on the **focused tile only** and toggling that
side between `DOCKED` and `RAILED`, restoring the last active drawer dock when re-entering
`RAILED` — the per-side analogue of `Ctrl/Cmd+J`'s "reopen the last opened bottom dock"
(`editor/editor_node.cpp:11080`).

The `docks/` namespace is the established one for dock-related shortcuts (`docks/open_*`);
`bottom_panels/` is its bottom-slot counterpart.

### Implementation

- [ ] Register both near the existing editor shortcut block
      (`editor/editor_node.cpp:11078-11080`), with macOS overrides where the chosen chord
      needs one.
- [ ] Route each to the focused tile's `EditorTileDockRegion`.
- [ ] Teach the existing `_focus_leaf_*_dock` handlers
      (`editor/editor_node.cpp:11407-11416`, wired at `:468-476`) that focusing a dock on a
      `RAILED` side sets that side's active drawer dock — otherwise the existing
      `docks/open_*` palette commands become no-ops on a collapsed side.
- [ ] Verify no collision on any platform. Do not rebind `Ctrl/Cmd+J`, `Escape`, or
      `Shift+F12` (D5).

### Acceptance criteria

- [ ] Each shortcut toggles only its own side, in the focused tile only.
- [ ] Re-entering `RAILED` restores the dock that was last open in the drawer on that side.
- [ ] Re-entering `RAILED` with no previously-open dock leaves the drawer closed.
- [ ] The existing `docks/open_inspector` / `open_signals` / `open_groups` / `open_history`
      palette commands open the dock in the drawer when its side is `RAILED`.
- [ ] Both new commands appear in the Command Palette.
- [ ] No collision with any existing editor shortcut on any platform.
- [ ] `Ctrl/Cmd+J`, `Escape` and `Shift+F12` retain their current bindings and behaviour.

### Tests

Extend `tests/editor/test_side_rail_collapse.h`:

- [ ] Toggle-restores-last-dock, including the empty case.
- [ ] Side independence and tile independence under shortcut invocation.
- [ ] `_focus_leaf_*_dock` on a railed side opens the drawer.
- [ ] Shortcut-collision check across the registered editor shortcut set.

---

## Issue 7 — An empty side keeps its rail

**Depends on:** Issue 5.

### Context

Signals, Groups and History can each be disabled
(`ScenePaneTile::set_*_dock_enabled` → `EditorTileDockRegion::set_dock_enabled`,
`editor/editor_tile_dock_region.cpp:86-97`), so a tile's right side can end up with every
dock disabled. If the rail followed the docks and vanished, the mode toggle and the
shortcut would have no visible target and the side could not be restored from the UI.

Unlike the global dock slots there is no drag-and-drop of docks into a tile region today, so
the rail is **not** a drop target and no drop machinery is in scope.

### Scope

Keep the rail visible and coherent for a side with zero enabled docks.

### Implementation

- [ ] Keep the rail visible, rendering an empty toggle list, when the side has no enabled
      docks, in both modes.
- [ ] The expand button stays functional on an empty railed side, returning it to `DOCKED`
      (which then shows an empty dock area, exactly as today).
- [ ] Re-enabling a dock on an empty side repopulates the rail and, if the side is `RAILED`
      with the drawer closed, leaves the drawer closed rather than auto-opening.

### Acceptance criteria

- [ ] A side with zero enabled docks still shows its rail, in both `DOCKED` and `RAILED`.
- [ ] Re-enabling a dock repopulates the rail in the correct position.
- [ ] Re-enabling a dock on a `RAILED` side with the drawer closed does not auto-open it.
- [ ] The expand button on an empty railed side returns the side to `DOCKED`.

### Tests

Extend `tests/editor/test_side_rail_strip.h`:

- [ ] Rail visibility with zero enabled docks, in both modes.
- [ ] Repopulation on re-enable, and no auto-open.

---

## Sequencing

```
Issue 1 (density) ────────────────────────────────── independent, land first
Issue 3 (tooltip) ── Issue 4 (rail widget) ─┐
Issue 2 (offset identity bugfix) ───────────┴─ Issue 5 ─┬─ Issue 6
                                                        └─ Issue 7
```

Issues 1, 2 and 3 have no dependencies and no overlap — they can be worked in parallel.

Issue 5 is the keystone and should not start until 2 and 4 are merged — it needs the
persistence fix in place and a rail to drive. Starting 5 before 2 will produce a feature
that silently swaps the two tile column widths on every save while collapsed.

## Definition of done for the epic

- [ ] All seven issues merged, each with its acceptance criteria met and tests registered in
      `tests/test_main.cpp`.
- [ ] `python3 scripts/agent_build.py` (native, strict, warnings-as-errors) is clean.
- [ ] `./bin/foundry.* --headless test run --force-colors` reports
      `[doctest] Status: SUCCESS!`, run with `DISPLAY=:1` on Linux.
- [ ] `pre-commit run --all-files` passes, including the class-reference doc checks.
- [ ] Observable bottom-panel behaviour is unchanged (D5), verified by the bottom-panel
      tests still passing untouched.
- [ ] The global dock columns behave exactly as before (D7), verified by the existing dock
      and workspace tests passing untouched.
- [ ] A `scripts/review_gallery.py` walkthrough board covering docked → railed → drawer open
      → restored, plus a design board pairing the three density levels, with captions stating
      what the reviewer should confirm. Gallery URL included in the final PR.

## Filing notes

When creating the epic and its sub-issues:

- Create the epic first, then each sub-issue, then attach them as **native** sub-issues.
- Verify the parent link by GraphQL (`issue.parent`), not REST — the REST `parent` field
  reads as empty even when the link exists, so a REST check will wrongly report failure.
- Keep upstream engine names out of issue titles and bodies.

**File one separate, independent bugfix issue, outside this epic.**
`EditorDockManager::save_docks_to_config` writes `0` into `dock_hsplit_N` for an invisible
vsplit (`editor/docks/editor_dock_manager.cpp:741-750`), destroying that column's stored
width, where the `dock_split_N` loop directly above it correctly skips invisible splits
(`:735-739`). It is reachable today only through Distraction Free, which is transient, so it
is low severity — and since D7 moves the rails off the global columns, it is no longer on
this feature's path. Note in the issue that the two loops also disagree on the visibility
predicate (`is_visible_in_tree()` above versus `is_visible()` below); `is_visible()` is the
correct one, since `SplitContainer`'s valid-children set is built from `is_visible()`
(`scene/gui/split_container.cpp:978`).
