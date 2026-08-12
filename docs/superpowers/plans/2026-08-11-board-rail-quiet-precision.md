# Quiet Precision Board Rail Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Polish the merged Board Rail into the approved Quiet Precision / Soft Lift treatment with a short, accessible active-selection glide.

**Architecture:** Keep the existing board buttons as the input and accessibility layer. `EditorBoardSwitcher` draws one shared active surface, accent marker, and menu divider behind them, while editor themes supply all visual resources and metrics. Active-board changes tween the decoration only; structural/layout changes and reduced-motion mode snap it into place.

**Tech Stack:** Foundry/Godot C++, `Control` custom drawing, `Tween`, editor theme styleboxes/constants/colors, doctest, Foundry editor MCP, review gallery.

---

## File Map

- `editor/gui/editor_board_switcher.h`: active-decoration state, synchronization API, tween ownership, and test hooks.
- `editor/gui/editor_board_switcher.cpp`: drawing, post-layout targeting, reduced-motion handling, and active-change animation.
- `editor/themes/theme_modern.cpp`: Modern Quiet Precision surface, hover, active pill, indicator, and divider tokens.
- `editor/themes/theme_classic.cpp`: equivalent Classic tokens using the Classic palette.
- `tests/editor/test_editor_board_switcher.h`: observable geometry, animation-policy, and theme-resource coverage.

### Task 1: Add the shared active decoration with accessible motion

**Files:**
- Modify: `tests/editor/test_editor_board_switcher.h`
- Modify: `editor/gui/editor_board_switcher.h`
- Modify: `editor/gui/editor_board_switcher.cpp`

- [ ] **Step 1: Write failing behavior tests**

Add test-only accessors under `#ifdef TESTS_ENABLED`, then add cases that compare the decoration rectangle with the active button in switcher-local coordinates:

```cpp
static Rect2 active_button_rect(const BoardSwitcherHarness &p_harness) {
	Button *button = p_harness.board_button(p_harness.strip->get_active_index());
	if (!button) {
		return Rect2();
	}
	return Rect2(
			button->get_global_position() - p_harness.switcher->get_global_position(),
			button->get_size());
}

TEST_CASE("[Editor][BoardSwitcher] Active decoration targets the selected board") {
	BoardSwitcherHarness harness;
	harness.mount(true);
	harness.strip->add_board("Second");
	harness.switcher->set_reduce_motion_override_for_tests(true);
	harness.strip->set_active_board(1);
	harness.pump();
	CHECK(harness.switcher->get_active_surface_rect_for_tests() == active_button_rect(harness));
	CHECK_FALSE(harness.switcher->is_active_surface_animating_for_tests());
}

TEST_CASE("[Editor][BoardSwitcher] Active decoration glides unless motion is reduced") {
	BoardSwitcherHarness harness;
	harness.mount(true);
	harness.strip->add_board("Second");
	harness.switcher->set_reduce_motion_override_for_tests(false);
	harness.strip->set_active_board(1);
	harness.pump(0.001);
	CHECK(harness.switcher->is_active_surface_animating_for_tests());

	harness.switcher->set_reduce_motion_override_for_tests(true);
	harness.strip->set_active_board(0);
	harness.pump();
	CHECK_FALSE(harness.switcher->is_active_surface_animating_for_tests());
	CHECK(harness.switcher->get_active_surface_rect_for_tests() == active_button_rect(harness));
}
```

Add a third case that starts a glide, adds a board, pumps once, and verifies the structural rebuild stopped the tween and snapped the rectangle to the still-active button.

```cpp
TEST_CASE("[Editor][BoardSwitcher] Structural rebuild snaps an active glide") {
	BoardSwitcherHarness harness;
	harness.mount(true);
	harness.strip->add_board("Second");
	harness.switcher->set_reduce_motion_override_for_tests(false);
	harness.strip->set_active_board(1);
	harness.pump(0.001);
	REQUIRE(harness.switcher->is_active_surface_animating_for_tests());

	harness.strip->add_board("Third");
	harness.pump();
	CHECK_FALSE(harness.switcher->is_active_surface_animating_for_tests());
	CHECK(harness.switcher->get_active_surface_rect_for_tests() == active_button_rect(harness));
}
```

- [ ] **Step 2: Run the focused test and confirm the new contract fails**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test --case '*BoardSwitcher*'
```

Expected: compilation fails because the three test accessors do not exist yet, proving the new tests execute against missing behavior.

- [ ] **Step 3: Implement the minimal active-decoration state**

Add these members and methods to `EditorBoardSwitcher`:

```cpp
class Tween;

Rect2 active_surface_rect;
Ref<Tween> active_surface_tween;
bool active_surface_visible = false;
bool active_surface_sync_queued = false;
bool animate_pending_active_surface_sync = false;
#ifdef TESTS_ENABLED
int reduce_motion_override_for_tests = -1;
#endif

void _on_active_board_changed(int p_index);
void _queue_active_surface_sync(bool p_animate);
void _sync_active_surface();
void _set_active_surface_rect(const Rect2 &p_rect);
bool _should_reduce_motion() const;
Rect2 _get_active_button_rect() const;
void _stop_active_surface_tween();
```

Expose these hooks only in test builds:

```cpp
#ifdef TESTS_ENABLED
Rect2 get_active_surface_rect_for_tests() const { return active_surface_rect; }
bool is_active_surface_animating_for_tests() const;
void set_reduce_motion_override_for_tests(bool p_reduce) { reduce_motion_override_for_tests = p_reduce ? 1 : 0; }
#endif
```

Define the animation query in the `.cpp`, where `Tween` is complete:

```cpp
#ifdef TESTS_ENABLED
bool EditorBoardSwitcher::is_active_surface_animating_for_tests() const {
	return active_surface_tween.is_valid() && active_surface_tween->is_running();
}
#endif
```

Replace the active-board signal's direct `_rebuild` connection with `_on_active_board_changed`. That handler preserves the current decoration rectangle, rebuilds buttons, and queues an animated synchronization. Existing structural signals continue to call `_rebuild` and queue a non-animated synchronization.

Implement `_sync_active_surface()` so it resolves the active button after layout. Snap when there is no previous rectangle, when animation was not requested, or when `DisplayServer::accessibility_should_reduce_animation()` returns `1`. Otherwise replace the existing tween and interpolate for `0.115` seconds:

```cpp
active_surface_tween = create_tween();
active_surface_tween->tween_method(
		callable_mp(this, &EditorBoardSwitcher::_set_active_surface_rect),
		active_surface_rect, target_rect, 0.115)
		->set_trans(Tween::TRANS_CUBIC)
		->set_ease(Tween::EASE_OUT);
```

`_set_active_surface_rect()` updates the rectangle and calls `queue_redraw()`. Theme refresh, compact/full transitions, resize, setup, reorder, add/remove, and restore stop the tween and queue a snap.

- [ ] **Step 4: Draw the decoration behind the existing controls**

Handle `NOTIFICATION_DRAW` in `_notification()`:

```cpp
if (active_surface_visible) {
	draw_style_box(get_theme_stylebox(SNAME("active_surface")), active_surface_rect);
	const float indicator_width = MIN(
			float(get_theme_constant(SNAME("active_indicator_width"))),
			active_surface_rect.size.x);
	const float indicator_height = get_theme_constant(SNAME("active_indicator_height"));
	const Rect2 indicator(
			Point2(active_surface_rect.get_center().x - indicator_width * 0.5,
					active_surface_rect.end.y - indicator_height),
			Size2(indicator_width, indicator_height));
	draw_rect(indicator, get_theme_color(SNAME("active_indicator_color")));
}
```

When the menu button is visible, draw a centered vertical divider immediately before it using `menu_divider_color`, `menu_divider_height`, and `menu_divider_width`. Keep child buttons above this drawing and preserve their existing focus/input behavior.

- [ ] **Step 5: Run the focused suite until green**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test --case '*BoardSwitcher*'
```

Expected: all Board Switcher cases pass, including the new target, glide, reduced-motion, and structural-snap cases.

- [ ] **Step 6: Commit the behavior**

```sh
git add editor/gui/editor_board_switcher.h editor/gui/editor_board_switcher.cpp tests/editor/test_editor_board_switcher.h
git commit -m "Animate the board rail active surface"
```

### Task 2: Apply the Quiet Precision theme treatment

**Files:**
- Modify: `tests/editor/test_editor_board_switcher.h`
- Modify: `editor/themes/theme_modern.cpp`
- Modify: `editor/themes/theme_classic.cpp`

- [ ] **Step 1: Replace the old margin-only theme test with resource-contract tests**

For both Modern and Classic at `EDSCALE == 2`, assert that `BoardRail` provides a valid `active_surface` stylebox, a transparent pressed `BoardRailButton` style preserving normal margins, a positive short indicator width/height, and a positive divider width/height. Also assert the indicator is narrower than a normal 180-pixel segment maximum:

```cpp
CHECK(theme->get_constant("active_indicator_width", "BoardRail") > 0);
CHECK(theme->get_constant("active_indicator_width", "BoardRail") <
		theme->get_constant("segment_maximum_width", "BoardRail"));
CHECK(theme->get_constant("active_indicator_height", "BoardRail") > 0);
CHECK(theme->get_constant("menu_divider_width", "BoardRail") > 0);
CHECK(theme->get_constant("menu_divider_height", "BoardRail") > 0);
REQUIRE(theme->get_stylebox("active_surface", "BoardRail").is_valid());
```

- [ ] **Step 2: Run the theme test and confirm failure**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test --case '*board rail*'
```

Expected: failure because `active_surface` and the new theme constants/colors are not defined.

- [ ] **Step 3: Define Quiet Precision resources in both editor themes**

In each theme's existing Board Rail block:

- soften `panel` by using a low-contrast theme-derived background, one scaled edge, a `12 * EDSCALE` radius, and `4 * EDSCALE` content margin;
- define `active_surface` by duplicating the theme's pressed button surface, removing its border, setting a `9 * EDSCALE` radius, and preserving no content margin because it is decoration;
- make Board Rail normal and pressed button styles transparent while preserving resolved content margins;
- give hover a local rounded, theme-derived neutral wash;
- set `active_indicator_color` to `p_config.accent_color` and `menu_divider_color` to `p_config.contrast_color_1` with reduced alpha;
- set `active_indicator_width = 24 * EDSCALE`, `active_indicator_height = 2 * EDSCALE`, `menu_divider_width = max(1, EDSCALE)`, and `menu_divider_height = 18 * EDSCALE`;
- give `BoardRailMenuButton` its own transparent/hover styleboxes so it visually belongs to the island.

Modern uses its existing subtle shadow color with a `4 * EDSCALE` shadow size and
a `Vector2(0, 1) * EDSCALE` offset on `active_surface`. Classic keeps the lift
fill-based and shadow-free. Do not change the shared `SceneModeButton` treatment
in this follow-up.

- [ ] **Step 4: Run focused tests and inspect the diff**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test --case '*BoardSwitcher*'
git diff --check
```

Expected: all Board Switcher/theme cases pass and `git diff --check` prints nothing.

- [ ] **Step 5: Commit the theme polish**

```sh
git add editor/themes/theme_modern.cpp editor/themes/theme_classic.cpp tests/editor/test_editor_board_switcher.h
git commit -m "Polish the board rail theme"
```

### Task 3: Verify the real editor and publish the follow-up PR

**Files:**
- No production files added beyond Tasks 1–2.
- Add finished screenshots only to the external review gallery, not the repository.

- [ ] **Step 1: Run the required strict native validation**

Run:

```sh
python3 scripts/agent_build.py --test --case '*BoardSwitcher*'
```

Expected: native strict build exits zero and all focused cases pass.

- [ ] **Step 2: Exercise the built editor through Foundry automation**

Launch the worktree binary against `tests/fixtures/editor_automation_mvp` with
`--automation`. Verify through semantic selectors that Board 1, Board 2, and
Board Menu remain reachable; switch boards; narrow the window until compact
mode appears; widen it again; and poll the editor log for new errors.

Capture final dark-theme and light-theme screenshots at the same scale. Confirm visually that the rail is a quiet island, the active selection is a raised pill with a short centered marker, the menu is integrated, and focus/hover states remain clear.

- [ ] **Step 3: Build a compact before/after design gallery**

```sh
python3 scripts/review_gallery.py add /tmp/quiet-precision-before.png --board "Quiet Precision Board Rail" --mode design --caption "Before: hard outline and full-width underline" --pair before
python3 scripts/review_gallery.py add /tmp/quiet-precision-after-dark.png --board "Quiet Precision Board Rail" --mode design --caption "After: Soft Lift active pill and short accent marker in the dark theme" --pair after
python3 scripts/review_gallery.py add /tmp/quiet-precision-after-light.png --board "Quiet Precision Board Rail — Light" --mode proof --caption "Light theme keeps the island, states, and divider legible"
QUIET_PRECISION_GALLERY_PASSWORD="$(openssl rand -hex 12)"
python3 scripts/review_gallery.py serve --public --basic-auth "reviewer:${QUIET_PRECISION_GALLERY_PASSWORD}"
```

Use `/tmp/quiet-precision-before.png` for the baseline capture and keep the generated password outside the repository.

- [ ] **Step 4: Review the final branch and open a new draft PR**

Confirm `git status --short`, review `git diff origin/develop...HEAD`, push `polish/board-rail-quiet-precision`, and open a draft PR targeting `develop`. The PR body must link merged PR #2116, summarize the visual-only scope, list focused/strict verification, and include the review gallery URL.
