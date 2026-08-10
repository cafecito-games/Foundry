/**************************************************************************/
/*  test_side_rail_strip.h                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             FOUNDRY ENGINE                             */
/*          A fork of the Godot Engine (https://godotengine.org)          */
/*                       https://www.cafecito.games                       */
/**************************************************************************/
/* Copyright (c) 2026-present Cafecito Games LLC.                         */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/input/input_event.h"
#include "core/input/shortcut.h"
#include "core/io/config_file.h"
#include "core/io/image.h"
#include "core/object/message_queue.h"
#include "editor/docks/editor_dock.h"
#include "editor/editor_data.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/editor_scene_workspace.h"
#include "editor/editor_tile_dock_region.h"
#include "editor/gui/editor_side_rail_button.h"
#include "editor/gui/editor_side_rail_strip.h"
#include "editor/gui/side_rail_state.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "editor/themes/editor_theme_manager.h"
#include "scene/gui/box_container.h"
#include "scene/gui/split_container.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/font.h"
#include "scene/resources/image_texture.h"

#include "tests/test_macros.h"

namespace TestSideRailStrip {

// A tile dock region with no scene-tree membership and no EditorDockManager
// dependency, mirroring TileDockFixture (tests/editor/test_tile_dock_offsets.h)
// but populated with real EditorDock instances so title/icon/enabled state
// behave exactly as the rail sees them in production.
struct SideRailFixture {
	HSplitContainer *body = nullptr;
	Control *center = nullptr;
	EditorTileDockRegion region;

	SideRailFixture() {
		body = memnew(HSplitContainer);
		center = memnew(Control);
		body->add_child(center);
		region.attach(body, center);
	}

	~SideRailFixture() {
		memdelete(body);
	}

	EditorDock *add_left_dock(const String &p_title) {
		EditorDock *dock = memnew(EditorDock);
		dock->set_title(p_title);
		region.place_left(dock);
		return dock;
	}

	EditorDock *add_right_dock(const String &p_title) {
		EditorDock *dock = memnew(EditorDock);
		dock->set_title(p_title);
		region.add_right(dock);
		return dock;
	}

	static void pump() {
		MessageQueue::get_singleton()->flush();
	}
};

TEST_CASE("[Editor][SideRail] toggle set matches the enabled dock set in order") {
	SideRailFixture fixture;
	EditorDock *a = fixture.add_right_dock("A");
	EditorDock *b = fixture.add_right_dock("B");
	EditorDock *c = fixture.add_right_dock("C");

	EditorSideRailStrip *strip = memnew(EditorSideRailStrip(EditorSideRailStrip::Side::RIGHT, &fixture.region));
	strip->rebuild_toggles();

	// A failed REQUIRE does not stop the test case in this build (doctest's
	// unwind is compiled out under disable_exceptions=yes), so guard the
	// indexing below by hand instead of trusting the REQUIRE above to abort.
	REQUIRE(strip->get_toggle_docks().size() == 3);
	if (strip->get_toggle_docks().size() != 3) {
		memdelete(strip);
		return;
	}
	CHECK(strip->get_toggle_docks()[0] == a);
	CHECK(strip->get_toggle_docks()[1] == b);
	CHECK(strip->get_toggle_docks()[2] == c);

	memdelete(strip);
}

TEST_CASE("[Editor][SideRail] disabling a dock removes its toggle and re-enabling restores it in place") {
	SideRailFixture fixture;
	EditorDock *a = fixture.add_right_dock("A");
	EditorDock *b = fixture.add_right_dock("B");
	EditorDock *c = fixture.add_right_dock("C");

	EditorSideRailStrip *strip = memnew(EditorSideRailStrip(EditorSideRailStrip::Side::RIGHT, &fixture.region));
	strip->rebuild_toggles();
	REQUIRE(strip->get_toggle_docks().size() == 3);
	if (strip->get_toggle_docks().size() != 3) {
		memdelete(strip);
		return;
	}

	// The rail's rebuild-on-enabled_changed connection is deferred (matching
	// the bottom drawer strip's child_order_changed connection), so a real
	// enable/disable round trip needs the message queue flushed, not a
	// manual rebuild_toggles() call, to prove the automatic path works.
	fixture.region.set_dock_enabled(b, false);
	SideRailFixture::pump();
	REQUIRE(strip->get_toggle_docks().size() == 2);
	if (strip->get_toggle_docks().size() != 2) {
		memdelete(strip);
		return;
	}
	CHECK(strip->get_toggle_docks()[0] == a);
	CHECK(strip->get_toggle_docks()[1] == c);

	fixture.region.set_dock_enabled(b, true);
	SideRailFixture::pump();
	REQUIRE(strip->get_toggle_docks().size() == 3);
	if (strip->get_toggle_docks().size() != 3) {
		memdelete(strip);
		return;
	}
	CHECK(strip->get_toggle_docks()[0] == a);
	CHECK(strip->get_toggle_docks()[1] == b);
	CHECK(strip->get_toggle_docks()[2] == c);

	memdelete(strip);
}

TEST_CASE("[Editor][SideRail] left rail mirrors the ordered EditorDock children preceding content_host") {
	SideRailFixture fixture;
	EditorDock *left = fixture.add_left_dock("Scene");
	fixture.add_right_dock("Inspector");

	EditorSideRailStrip *strip = memnew(EditorSideRailStrip(EditorSideRailStrip::Side::LEFT, &fixture.region));
	strip->rebuild_toggles();

	REQUIRE(strip->get_toggle_docks().size() == 1);
	if (strip->get_toggle_docks().size() != 1) {
		memdelete(strip);
		return;
	}
	CHECK(strip->get_toggle_docks()[0] == left);

	memdelete(strip);
}

TEST_CASE("[Editor][SideRail] toggle tooltip is non-empty with and without a shortcut") {
	SideRailFixture fixture;
	EditorDock *with_shortcut = fixture.add_right_dock("Inspector");
	Ref<Shortcut> shortcut;
	shortcut.instantiate();
	shortcut->set_name("Toggle Inspector");
	Array events;
	events.push_back(InputEventKey::create_reference(Key::I));
	shortcut->set_events(events);
	with_shortcut->set_dock_shortcut(shortcut);

	fixture.add_right_dock("Signals");

	EditorSideRailStrip *strip = memnew(EditorSideRailStrip(EditorSideRailStrip::Side::RIGHT, &fixture.region));
	strip->rebuild_toggles();

	REQUIRE(strip->get_toggle_buttons().size() == 2);
	if (strip->get_toggle_buttons().size() != 2) {
		memdelete(strip);
		return;
	}
	CHECK_FALSE(strip->get_toggle_buttons()[0]->get_tooltip_text().is_empty());
	CHECK_FALSE(strip->get_toggle_buttons()[1]->get_tooltip_text().is_empty());

	memdelete(strip);
}

TEST_CASE("[Editor][SideRail] deferred first rebuild does not lock the rail into icon-only") {
	// EditorSideRailStrip's constructor defers its first rebuild_toggles()
	// call (matching EditorBottomDrawerStrip's own construction pattern), so
	// it can run before any parent container has explicitly sized the strip.
	// A suspected bug: if get_size() were still the construction-time zero at
	// that point, deciding the label-mode fit against it would drop straight
	// to ICON_ONLY, and because the fit decision is hysteretic, a later real
	// height inside the hysteresis band would then never return to LABELLED.
	//
	// This does not happen: Control::_size_changed() always clamps a
	// control's actual size to at least its own combined minimum size, and a
	// PanelContainer's minimum size already includes its children's minimum
	// sizes. So by the time this deferred rebuild runs and creates the
	// toggle buttons, the strip's own get_size() is never smaller than the
	// sum of their labelled minimum heights -- the same quantity
	// drop_to_icon_only is computed from -- so the decision can never see a
	// bogus zero baseline. This test pins that behavior so a future change
	// to sizing (e.g. removing the minimum-size clamp, or computing minimum
	// size lazily) that reintroduces the bogus baseline gets caught.
	SideRailFixture fixture;
	fixture.add_right_dock("Inspector");
	fixture.add_right_dock("Signals");

	// A standalone strip outside the scene tree has no window ancestor to
	// resolve a theme through, so its buttons' labelled heights would stay
	// zero and the hysteresis thresholds below would be zero too -- unable to
	// exercise the scenario this test covers. Root it under the real tree,
	// with no explicit size set, exactly mirroring production construction
	// order (construct, then add_child), so its toggle buttons get real font
	// metrics by the time the deferred rebuild runs.
	Control *host = memnew(Control);
	SceneTree::get_singleton()->get_root()->add_child(host);

	EditorSideRailStrip *strip = memnew(EditorSideRailStrip(EditorSideRailStrip::Side::RIGHT, &fixture.region));
	host->add_child(strip);

	// The constructor's rebuild_toggles() call is deferred (matching
	// EditorBottomDrawerStrip's own construction pattern), so it fires here,
	// before this test has ever explicitly sized the strip.
	SideRailFixture::pump();

	REQUIRE(strip->get_toggle_buttons().size() == 2);
	if (strip->get_toggle_buttons().size() != 2) {
		host->remove_child(strip);
		memdelete(strip);
		SceneTree::get_singleton()->get_root()->remove_child(host);
		memdelete(host);
		return;
	}

	CHECK(strip->get_label_mode() == SideRailLabelMode::LABELLED);

	Vector<real_t> labelled_heights;
	labelled_heights.resize(strip->get_toggle_buttons().size());
	for (uint32_t i = 0; i < strip->get_toggle_buttons().size(); i++) {
		labelled_heights.write[i] = strip->get_toggle_buttons()[i]->get_labelled_minimum_size().height;
	}
	const SideRailFitThresholds thresholds = side_rail_fit_thresholds(labelled_heights);
	REQUIRE(thresholds.drop_to_icon_only > 0);
	if (!(thresholds.drop_to_icon_only > 0)) {
		host->remove_child(strip);
		memdelete(strip);
		SceneTree::get_singleton()->get_root()->remove_child(host);
		memdelete(host);
		return;
	}

	// A height inside the hysteresis band, applied as the strip's first
	// explicit resize, must still be measured against the true starting mode
	// (LABELLED) and therefore stay LABELLED. If the deferred rebuild above
	// had instead seen a bogus zero-height baseline, it would already have
	// dropped to ICON_ONLY, and this height (below return_to_labelled) would
	// then be stuck there.
	const real_t within_hysteresis_band = (thresholds.drop_to_icon_only + thresholds.return_to_labelled) / 2.0;
	strip->set_size(Size2(32, within_hysteresis_band));

	CHECK(strip->get_label_mode() == SideRailLabelMode::LABELLED);

	host->remove_child(strip);
	memdelete(strip);
	SceneTree::get_singleton()->get_root()->remove_child(host);
	memdelete(host);
}

struct EditorScaleGuard {
	float previous = 1.0f;
	explicit EditorScaleGuard(float p_scale) :
			previous(EditorScale::get_scale()) {
		EditorScale::set_scale(p_scale);
	}
	~EditorScaleGuard() {
		EditorScale::set_scale(previous);
	}
};

struct ThemeStyleGuard {
	String previous;
	explicit ThemeStyleGuard(const String &p_style) :
			previous(EDITOR_GET("interface/theme/style")) {
		EditorSettings::get_singleton()->set_manually("interface/theme/style", p_style);
	}
	~ThemeStyleGuard() {
		EditorSettings::get_singleton()->set_manually("interface/theme/style", previous);
	}
};

static Ref<Texture2D> _make_side_rail_test_icon(int p_width, int p_height = -1) {
	const int height = p_height > 0 ? p_height : p_width;
	Ref<Image> image = Image::create_empty(p_width, height, false, Image::FORMAT_RGBA8);
	image->fill(Color(1, 1, 1, 1));
	return ImageTexture::create_from_image(image);
}

static void _assert_side_rail_composed_geometry(EditorSideRailButton *p_button) {
	const Size2 min_size = p_button->get_minimum_size();
	p_button->set_size(min_size);
	const EditorSideRailButton::ComposedGeometry geometry = p_button->get_composed_geometry();

	REQUIRE(geometry.has_icon);
	REQUIRE(geometry.has_label);
	if (!(geometry.has_icon && geometry.has_label)) {
		return;
	}

	// Icon and label share one bottom-to-top quarter-turn.
	CHECK(geometry.content_transform.get_rotation() == doctest::Approx(-Math::PI / 2.0));

	// Icon sits above the label along the reading axis, separated by the theme gap.
	CHECK(geometry.icon_rect.get_end().y <= geometry.label_rect.position.y + 0.5);
	const real_t gap = geometry.label_rect.position.y - geometry.icon_rect.get_end().y;
	CHECK(gap >= geometry.icon_label_separation - 0.5);
	// Ceil of the strip long-axis can add up to just under 1px of slack.
	CHECK(gap <= geometry.icon_label_separation + 1.0);

	// Both rects stay inside the stylebox content rect (no clipping / neighbor bleed).
	CHECK(geometry.content_rect.encloses(geometry.icon_rect));
	CHECK(geometry.content_rect.encloses(geometry.label_rect));

	// Pixel-snapped transform origin and icon bounds keep rotated content crisp.
	CHECK(geometry.content_transform.get_origin().x == Math::floor(geometry.content_transform.get_origin().x));
	CHECK(geometry.content_transform.get_origin().y == Math::floor(geometry.content_transform.get_origin().y));
	CHECK(geometry.icon_rect.position.x == Math::floor(geometry.icon_rect.position.x));
	CHECK(geometry.icon_rect.position.y == Math::floor(geometry.icon_rect.position.y));

	// Minimum size is a tight fit around the composed content plus stylebox margins.
	const Rect2 union_rect = geometry.icon_rect.merge(geometry.label_rect);
	const Size2 stylebox_min = min_size - geometry.content_rect.size;
	CHECK(geometry.content_rect.size.width + 0.5 >= union_rect.size.width);
	CHECK(geometry.content_rect.size.height + 0.5 >= union_rect.size.height);
	CHECK(min_size.width + 0.5 >= union_rect.get_end().x);
	CHECK(min_size.height + 0.5 >= union_rect.get_end().y);
	CHECK(min_size.width <= union_rect.size.width + stylebox_min.width + 1.0);
	CHECK(min_size.height <= union_rect.size.height + stylebox_min.height + 1.0);
}

TEST_CASE("[Editor][SideRailButton] rotated-label minimum size covers the rendered text width") {
	Ref<EditorTheme> theme = EditorThemeManager::generate_theme();

	EditorSideRailButton *button = memnew(EditorSideRailButton);
	button->set_rail_label("Inspector");
	button->set_theme(theme);
	// set_theme only notifies descendants of NOTIFICATION_THEME_CHANGED when
	// the control is inside the scene tree; this button is deliberately
	// standalone, so refresh its theme item cache directly.
	button->notification(Control::NOTIFICATION_THEME_CHANGED);

	const Ref<Font> font = button->get_theme_font(SceneStringName(font));
	REQUIRE(font.is_valid());
	if (font.is_null()) {
		memdelete(button);
		return;
	}
	const int font_size = button->get_theme_font_size(SceneStringName(font_size));
	const real_t text_width = font->get_string_size("Inspector", HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x;

	const Size2 labelled_size = button->get_minimum_size();
	CHECK(labelled_size.height >= text_width);

	memdelete(button);
}

TEST_CASE("[Editor][SideRailButton] composed icon and label share rotation, separation, and containment") {
	// Issue #2039: labelled toggles must rotate icon and label together as one
	// unit, keep them separated by the theme gap, and keep their union inside
	// the stylebox content rect. Geometry is asserted from measured rects, not
	// from source text.
	for (const String &style : { String("Modern"), String("Classic") }) {
		ThemeStyleGuard style_guard(style);
		for (const float scale : { 1.0f, 1.5f, 2.0f }) {
			EditorScaleGuard scale_guard(scale);
			CAPTURE(style);
			CAPTURE(scale);
			Ref<EditorTheme> theme = EditorThemeManager::generate_theme();
			const int icon_px = MAX(1, (int)Math::round(16.0f * scale));

			for (const String &label : { String("Groups"), String("InspectorPanelConfiguration") }) {
				CAPTURE(label);
				EditorSideRailButton *button = memnew(EditorSideRailButton);
				button->set_rail_icon(_make_side_rail_test_icon(icon_px));
				button->set_rail_label(label);
				button->set_theme(theme);
				button->notification(Control::NOTIFICATION_THEME_CHANGED);
				_assert_side_rail_composed_geometry(button);
				memdelete(button);
			}
		}
	}
}

TEST_CASE("[Editor][SideRailButton] non-square icons swap axes under the shared rotation") {
	Ref<EditorTheme> theme = EditorThemeManager::generate_theme();

	EditorSideRailButton *button = memnew(EditorSideRailButton);
	button->set_rail_icon(_make_side_rail_test_icon(16, 24));
	button->set_rail_label("Groups");
	button->set_theme(theme);
	button->notification(Control::NOTIFICATION_THEME_CHANGED);

	_assert_side_rail_composed_geometry(button);
	const EditorSideRailButton::ComposedGeometry geometry = button->get_composed_geometry();
	CHECK(geometry.icon_rect.size.width == doctest::Approx(24));
	CHECK(geometry.icon_rect.size.height == doctest::Approx(16));
	CHECK(button->get_minimum_size().width >= 24);

	memdelete(button);
}

TEST_CASE("[Editor][SideRailButton] labelled mode without an icon still contains the rotated label") {
	Ref<EditorTheme> theme = EditorThemeManager::generate_theme();

	EditorSideRailButton *button = memnew(EditorSideRailButton);
	button->set_rail_label("Inspector");
	button->set_theme(theme);
	button->notification(Control::NOTIFICATION_THEME_CHANGED);
	button->set_size(button->get_minimum_size());

	const EditorSideRailButton::ComposedGeometry geometry = button->get_composed_geometry();
	CHECK_FALSE(geometry.has_icon);
	REQUIRE(geometry.has_label);
	if (!geometry.has_label) {
		memdelete(button);
		return;
	}
	CHECK(geometry.content_transform.get_rotation() == doctest::Approx(-Math::PI / 2.0));
	CHECK(geometry.content_rect.encloses(geometry.label_rect));

	memdelete(button);
}

TEST_CASE("[Editor][SideRailButton] pressed and normal draw modes share the same layout geometry") {
	ThemeStyleGuard style_guard("Modern");
	Ref<EditorTheme> theme = EditorThemeManager::generate_theme();

	EditorSideRailButton *button = memnew(EditorSideRailButton);
	button->set_rail_icon(_make_side_rail_test_icon(16));
	button->set_rail_label("Inspector");
	button->set_theme(theme);
	button->notification(Control::NOTIFICATION_THEME_CHANGED);

	const Size2 normal_min = button->get_minimum_size();
	button->set_size(normal_min);
	const EditorSideRailButton::ComposedGeometry normal = button->get_composed_geometry();

	button->set_pressed_no_signal(true);
	const Size2 pressed_min = button->get_minimum_size();
	button->set_size(pressed_min);
	const EditorSideRailButton::ComposedGeometry pressed = button->get_composed_geometry();

	CHECK(pressed_min == normal_min);
	CHECK(pressed.content_rect == normal.content_rect);
	CHECK(pressed.icon_rect == normal.icon_rect);
	CHECK(pressed.label_rect == normal.label_rect);

	memdelete(button);
}

TEST_CASE("[Editor][SideRailButton] content transform is independent of the button's position in its parent") {
	// Regression for content rendering displaced by its own layout position:
	// NOTIFICATION_DRAW already runs in the control's local space, so
	// composing get_transform() into the draw transform applies the button's
	// own position a second time. Two buttons with identical local geometry
	// sitting at different y-offsets in a VBoxContainer must produce the exact
	// same content transform.
	Ref<EditorTheme> theme = EditorThemeManager::generate_theme();

	Control *root = memnew(Control);
	root->set_size(Size2(400, 400));
	SceneTree::get_singleton()->get_root()->add_child(root);

	VBoxContainer *vbox = memnew(VBoxContainer);
	root->add_child(vbox);

	EditorSideRailButton *first = memnew(EditorSideRailButton);
	first->set_rail_icon(_make_side_rail_test_icon(16));
	first->set_rail_label("Inspector");
	first->set_custom_minimum_size(Size2(32, 140));
	vbox->add_child(first);

	EditorSideRailButton *second = memnew(EditorSideRailButton);
	second->set_rail_icon(_make_side_rail_test_icon(16));
	second->set_rail_label("Inspector");
	second->set_custom_minimum_size(Size2(32, 140));
	vbox->add_child(second);

	root->set_theme(theme);
	SceneTree::get_singleton()->process(0.016);
	MessageQueue::get_singleton()->flush();

	// The setup only proves anything if the two buttons actually land at
	// different y-positions; otherwise a position-dependent bug would be
	// invisible here too (the double-transform bug this test guards against
	// was invisible on the first toggle for exactly this reason).
	REQUIRE(second->get_position().y > first->get_position().y);
	if (!(second->get_position().y > first->get_position().y)) {
		memdelete(root);
		return;
	}

	const Transform2D first_transform = first->get_composed_geometry().content_transform;
	const Transform2D second_transform = second->get_composed_geometry().content_transform;
	CHECK(first_transform == second_transform);
	CHECK(first_transform.get_rotation() == doctest::Approx(-Math::PI / 2.0));

	memdelete(root);
}

TEST_CASE("[Editor][SideRailButton] icon-only mode centers an upright icon without label space") {
	Ref<EditorTheme> theme = EditorThemeManager::generate_theme();

	EditorSideRailButton *button = memnew(EditorSideRailButton);
	button->set_rail_icon(_make_side_rail_test_icon(16));
	button->set_rail_label("A very long dock title that would dominate the labelled height");
	button->set_theme(theme);
	button->notification(Control::NOTIFICATION_THEME_CHANGED);

	const Size2 labelled_size = button->get_minimum_size();
	button->set_label_visible(false);
	const Size2 icon_only_size = button->get_minimum_size();

	CHECK(icon_only_size.height < labelled_size.height);
	// The labelled-mode size is always available regardless of the current
	// mode, since the rail's fit decision needs it to know whether returning
	// to labelled mode would fit.
	CHECK(button->get_labelled_minimum_size().height == doctest::Approx(labelled_size.height));

	button->set_size(Size2(40, 40));
	const EditorSideRailButton::ComposedGeometry geometry = button->get_composed_geometry();
	CHECK_FALSE(geometry.has_label);
	REQUIRE(geometry.has_icon);
	if (!geometry.has_icon) {
		memdelete(button);
		return;
	}
	CHECK(geometry.content_transform == Transform2D());
	CHECK(geometry.icon_rect.size == Size2(16, 16));
	CHECK(geometry.content_rect.encloses(geometry.icon_rect));
	CHECK(Math::abs(geometry.icon_rect.get_center().x - geometry.content_rect.get_center().x) <= 0.5);
	CHECK(Math::abs(geometry.icon_rect.get_center().y - geometry.content_rect.get_center().y) <= 0.5);

	memdelete(button);
}

TEST_CASE("[Editor][SideRailState] fit decision hysteresis") {
	Vector<real_t> heights;
	heights.push_back(40);
	heights.push_back(40);
	heights.push_back(60);
	const SideRailFitThresholds thresholds = side_rail_fit_thresholds(heights);

	CHECK(thresholds.drop_to_icon_only == doctest::Approx(140));
	CHECK(thresholds.return_to_labelled == doctest::Approx(200));
	// Required by design: returning to labelled must need strictly more
	// available height than dropping to icon-only did, by at least the
	// tallest single toggle's height.
	CHECK(thresholds.return_to_labelled - thresholds.drop_to_icon_only >= 60);

	SUBCASE("labelled mode drops when it no longer fits") {
		CHECK(side_rail_fit_label_mode(SideRailLabelMode::LABELLED, 150, thresholds) == SideRailLabelMode::LABELLED);
		CHECK(side_rail_fit_label_mode(SideRailLabelMode::LABELLED, 139, thresholds) == SideRailLabelMode::ICON_ONLY);
	}

	SUBCASE("icon-only mode stays icon-only in the hysteresis band") {
		// Available height that would satisfy "fits when labelled" (>=
		// drop_to_icon_only) but not yet "return to labelled"
		// (>= return_to_labelled) must not oscillate back.
		CHECK(side_rail_fit_label_mode(SideRailLabelMode::ICON_ONLY, 150, thresholds) == SideRailLabelMode::ICON_ONLY);
		CHECK(side_rail_fit_label_mode(SideRailLabelMode::ICON_ONLY, 199, thresholds) == SideRailLabelMode::ICON_ONLY);
		CHECK(side_rail_fit_label_mode(SideRailLabelMode::ICON_ONLY, 200, thresholds) == SideRailLabelMode::LABELLED);
	}

	SUBCASE("no sequence of resizes within the hysteresis band produces an unbounded rebuild loop") {
		SideRailLabelMode mode = SideRailLabelMode::LABELLED;
		mode = side_rail_fit_label_mode(mode, 100, thresholds); // Drops to icon-only.
		REQUIRE(mode == SideRailLabelMode::ICON_ONLY);
		// Oscillate the available height across the old drop threshold, which
		// on a naive (non-hysteretic) decision would flip the mode every step.
		for (int i = 0; i < 20; i++) {
			const real_t available = (i % 2 == 0) ? thresholds.drop_to_icon_only : thresholds.drop_to_icon_only + 1;
			mode = side_rail_fit_label_mode(mode, available, thresholds);
			CHECK(mode == SideRailLabelMode::ICON_ONLY);
		}
	}
}

struct SideRailWorkspaceHarness {
	Control *host = nullptr;
	EditorData editor_data;
	EditorSelection *selection = nullptr;
	EditorSceneWorkspace *workspace = nullptr;

	void mount() {
		host = memnew(Control);
		host->set_custom_minimum_size(Size2(800, 600));
		SceneTree::get_singleton()->get_root()->add_child(host);
		selection = memnew(EditorSelection);
		workspace = EditorSceneWorkspace::create_single_leaf_workspace(selection, &editor_data);
		host->add_child(workspace);
		workspace->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		host->set_size(Size2(800, 600));
		workspace->set_size(Size2(800, 600));
	}

	void pump() {
		SceneTree::get_singleton()->process(0.016);
		MessageQueue::get_singleton()->flush();
	}

	void unmount() {
		memdelete(workspace);
		SceneTree::get_singleton()->get_root()->remove_child(host);
		memdelete(host);
		memdelete(selection);
	}
};

TEST_CASE("[Editor][SideRail] empty side keeps a non-zero rail width in DOCKED mode") {
	// A side that starts with zero enabled docks has neither a toggle nor
	// (DOCKED hides the expand button) any other visible child, so
	// PanelContainer's own get_minimum_size() would otherwise report a
	// near-zero width even though the control itself is not hidden.
	SideRailFixture fixture;
	EditorDock *dock = fixture.add_right_dock("Signals");
	fixture.region.set_dock_enabled(dock, false);

	Ref<EditorTheme> theme = EditorThemeManager::generate_theme();

	EditorSideRailStrip *strip = memnew(EditorSideRailStrip(EditorSideRailStrip::Side::RIGHT, &fixture.region));
	strip->set_theme(theme);
	strip->notification(Control::NOTIFICATION_THEME_CHANGED);

	CHECK(strip->get_toggle_docks().is_empty());
	CHECK(strip->is_visible());
	CHECK(strip->get_minimum_size().width > 0);

	memdelete(strip);
}

TEST_CASE("[Editor][SideRail] empty side keeps its rail visible and sized in RAILED mode") {
	// RAILED always shows the expand button regardless of dock count, so an
	// empty RAILED side already has a real child to size against; this pins
	// that it stays true once the DOCKED floor above is added.
	SideRailFixture fixture;
	EditorDock *dock = fixture.add_right_dock("Signals");
	fixture.region.set_dock_enabled(dock, false);
	fixture.region.set_side_mode(EditorTileDockRegion::Side::RIGHT, SideRailMode::RAILED);

	Ref<EditorTheme> theme = EditorThemeManager::generate_theme();

	EditorSideRailStrip *strip = memnew(EditorSideRailStrip(EditorSideRailStrip::Side::RIGHT, &fixture.region));
	strip->set_theme(theme);
	strip->notification(Control::NOTIFICATION_THEME_CHANGED);

	CHECK(strip->get_toggle_docks().is_empty());
	CHECK(strip->is_visible());
	CHECK(strip->get_minimum_size().width > 0);

	memdelete(strip);
}

TEST_CASE("[Editor][SideRail] expand button on an empty railed side returns it to DOCKED") {
	SideRailFixture fixture;
	EditorDock *dock = fixture.add_right_dock("Signals");
	fixture.region.set_dock_enabled(dock, false);
	fixture.region.set_side_mode(EditorTileDockRegion::Side::RIGHT, SideRailMode::RAILED);
	REQUIRE(fixture.region.get_side_mode(EditorTileDockRegion::Side::RIGHT) == SideRailMode::RAILED);

	fixture.region.set_side_mode(EditorTileDockRegion::Side::RIGHT, SideRailMode::DOCKED);

	CHECK(fixture.region.get_side_mode(EditorTileDockRegion::Side::RIGHT) == SideRailMode::DOCKED);
}

TEST_CASE("[Editor][SideRail] re-enabling a dock on an empty side repopulates the rail in position") {
	SideRailFixture fixture;
	EditorDock *a = fixture.add_right_dock("A");
	EditorDock *b = fixture.add_right_dock("B");
	EditorDock *c = fixture.add_right_dock("C");
	fixture.region.set_dock_enabled(a, false);
	fixture.region.set_dock_enabled(b, false);
	fixture.region.set_dock_enabled(c, false);

	EditorSideRailStrip *strip = memnew(EditorSideRailStrip(EditorSideRailStrip::Side::RIGHT, &fixture.region));
	strip->rebuild_toggles();
	REQUIRE(strip->get_toggle_docks().is_empty());

	fixture.region.set_dock_enabled(b, true);
	SideRailFixture::pump();

	REQUIRE(strip->get_toggle_docks().size() == 1);
	if (strip->get_toggle_docks().size() != 1) {
		memdelete(strip);
		return;
	}
	CHECK(strip->get_toggle_docks()[0] == b);

	fixture.region.set_dock_enabled(a, true);
	SideRailFixture::pump();
	REQUIRE(strip->get_toggle_docks().size() == 2);
	if (strip->get_toggle_docks().size() != 2) {
		memdelete(strip);
		return;
	}
	// Re-enabling A restores its position ahead of B, matching the source's
	// own ordering rather than insertion order.
	CHECK(strip->get_toggle_docks()[0] == a);
	CHECK(strip->get_toggle_docks()[1] == b);

	memdelete(strip);
}

TEST_CASE("[Editor][SideRail] re-enabling a dock on a RAILED empty side leaves the drawer closed") {
	SideRailFixture fixture;
	EditorDock *dock = fixture.add_right_dock("Signals");
	fixture.region.set_dock_enabled(dock, false);
	fixture.region.set_side_mode(EditorTileDockRegion::Side::RIGHT, SideRailMode::RAILED);
	REQUIRE(fixture.region.get_drawer_dock(EditorTileDockRegion::Side::RIGHT) == nullptr);

	fixture.region.set_dock_enabled(dock, true);

	CHECK(fixture.region.get_side_mode(EditorTileDockRegion::Side::RIGHT) == SideRailMode::RAILED);
	CHECK(fixture.region.get_drawer_dock(EditorTileDockRegion::Side::RIGHT) == nullptr);
	CHECK_FALSE(fixture.region.is_dock_shown(dock));
}

TEST_CASE("[Editor][SideRail] a side with no docks at all still shows a real rail") {
	// Distinct from the disabled-dock cases above: this side never had any
	// EditorDock placed on it, so get_side_docks() is empty from construction.
	SideRailFixture fixture;
	fixture.add_left_dock("Scene"); // Keep the left side non-empty for contrast.

	Ref<EditorTheme> theme = EditorThemeManager::generate_theme();

	EditorSideRailStrip *strip = memnew(EditorSideRailStrip(EditorSideRailStrip::Side::RIGHT, &fixture.region));
	strip->set_theme(theme);
	strip->notification(Control::NOTIFICATION_THEME_CHANGED);

	CHECK(strip->get_toggle_docks().is_empty());
	CHECK(strip->is_visible());
	CHECK(strip->get_minimum_size().width > 0);

	memdelete(strip);
}

TEST_CASE("[Editor][SideRail] removing a side's last dock empties the rail without losing the floor") {
	SideRailFixture fixture;
	EditorDock *dock = fixture.add_right_dock("Signals");

	Ref<EditorTheme> theme = EditorThemeManager::generate_theme();

	EditorSideRailStrip *strip = memnew(EditorSideRailStrip(EditorSideRailStrip::Side::RIGHT, &fixture.region));
	strip->set_theme(theme);
	strip->rebuild_toggles();
	REQUIRE(strip->get_toggle_docks().size() == 1);

	fixture.region.get_right_tabs()->remove_child(dock);
	memdelete(dock);
	SideRailFixture::pump();

	CHECK(strip->get_toggle_docks().is_empty());
	CHECK(strip->is_visible());
	CHECK(strip->get_minimum_size().width > 0);

	memdelete(strip);
}

TEST_CASE("[Editor][SideRail] rails do not change split-offset count or saved values") {
	SideRailWorkspaceHarness h;
	h.mount();
	h.pump();

	ScenePaneTile *tile = h.workspace->get_focused_tile();
	REQUIRE(tile != nullptr);
	if (tile == nullptr) {
		h.unmount();
		return;
	}
	REQUIRE(tile->get_left_rail() != nullptr);
	REQUIRE(tile->get_right_rail() != nullptr);

	EditorTileDockRegion *region = tile->get_dock_region();
	PackedInt32Array offsets;
	offsets.push_back(220);
	offsets.push_back(-240);
	region->get_body()->set_split_offsets(offsets);

	// The rails are mounted in rail_hbox, a sibling wrapper around body, so
	// body itself still has exactly its three original visible columns and
	// the same number of gaps between them.
	REQUIRE(region->get_body()->get_split_offsets().size() == 2);

	Ref<ConfigFile> config;
	config.instantiate();
	const String section = "WorkspaceLeaf_0";
	region->save_layout(config, section);

	REQUIRE(config->has_section_key(section, "tile_dock_hsplit_1"));
	REQUIRE(config->has_section_key(section, "tile_dock_hsplit_2"));
	CHECK(int(config->get_value(section, "tile_dock_hsplit_1")) == int(220 / EDSCALE));
	CHECK(int(config->get_value(section, "tile_dock_hsplit_2")) == int(-240 / EDSCALE));

	h.unmount();
}

} // namespace TestSideRailStrip
