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
#include "core/object/message_queue.h"
#include "editor/docks/editor_dock.h"
#include "editor/editor_data.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/editor_scene_workspace.h"
#include "editor/editor_tile_dock_region.h"
#include "editor/gui/editor_side_rail_button.h"
#include "editor/gui/editor_side_rail_strip.h"
#include "editor/gui/side_rail_state.h"
#include "editor/themes/editor_scale.h"
#include "editor/themes/editor_theme_manager.h"
#include "scene/gui/split_container.h"
#include "scene/main/scene_tree.h"

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

	REQUIRE(strip->get_toggle_docks().size() == 3);
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

	// The rail's rebuild-on-enabled_changed connection is deferred (matching
	// the bottom drawer strip's child_order_changed connection), so a real
	// enable/disable round trip needs the message queue flushed, not a
	// manual rebuild_toggles() call, to prove the automatic path works.
	fixture.region.set_dock_enabled(b, false);
	SideRailFixture::pump();
	REQUIRE(strip->get_toggle_docks().size() == 2);
	CHECK(strip->get_toggle_docks()[0] == a);
	CHECK(strip->get_toggle_docks()[1] == c);

	fixture.region.set_dock_enabled(b, true);
	SideRailFixture::pump();
	REQUIRE(strip->get_toggle_docks().size() == 3);
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
	CHECK_FALSE(strip->get_toggle_buttons()[0]->get_tooltip_text().is_empty());
	CHECK_FALSE(strip->get_toggle_buttons()[1]->get_tooltip_text().is_empty());

	memdelete(strip);
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
	const int font_size = button->get_theme_font_size(SceneStringName(font_size));
	const real_t text_width = font->get_string_size("Inspector", HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x;

	const Size2 labelled_size = button->get_minimum_size();
	CHECK(labelled_size.height >= text_width);

	memdelete(button);
}

TEST_CASE("[Editor][SideRailButton] icon-only minimum size drops the label contribution") {
	Ref<EditorTheme> theme = EditorThemeManager::generate_theme();

	EditorSideRailButton *button = memnew(EditorSideRailButton);
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

TEST_CASE("[Editor][SideRail] rails do not change split-offset count or saved values") {
	SideRailWorkspaceHarness h;
	h.mount();
	h.pump();

	ScenePaneTile *tile = h.workspace->get_focused_tile();
	REQUIRE(tile != nullptr);
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
