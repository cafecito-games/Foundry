/**************************************************************************/
/*  test_side_rail_collapse.h                                             */
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
#include "core/os/keyboard.h"
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
#include "scene/gui/split_container.h"
#include "scene/gui/tab_container.h"
#include "scene/main/scene_tree.h"

#include "tests/test_macros.h"
#include "tests/test_tools.h"

namespace TestSideRailCollapse {

using Side = EditorTileDockRegion::Side;

// A tile dock region with real EditorDock instances but no EditorDockManager
// dependency, mirroring TileDockFixture (tests/editor/test_tile_dock_offsets.h).
// The body is rooted in the scene tree because TabContainer::set_current_tab
// only defers its selection until the container enters the tree, so a detached
// region could never express "the right side shows this dock".
struct CollapseFixture {
	HSplitContainer *body = nullptr;
	Control *center = nullptr;
	EditorTileDockRegion region;
	EditorDock *scene = nullptr;
	EditorDock *filesystem = nullptr;
	EditorDock *inspector = nullptr;
	EditorDock *signals = nullptr;
	EditorDock *groups = nullptr;

	CollapseFixture(bool p_make_docks_focusable = true, bool p_add_second_left_dock = false) {
		body = memnew(HSplitContainer);
		SceneTree::get_singleton()->get_root()->add_child(body);
		center = memnew(Control);
		body->add_child(center);
		region.attach(body, center);

		scene = memnew(EditorDock);
		scene->set_title("Scene");
		scene->set_layout_key("Scene");
		if (p_make_docks_focusable) {
			scene->set_focus_mode(Control::FOCUS_ALL);
		}
		region.place_left(scene);
		if (p_add_second_left_dock) {
			filesystem = memnew(EditorDock);
			filesystem->set_title("FileSystem");
			filesystem->set_layout_key("FileSystem");
			if (p_make_docks_focusable) {
				filesystem->set_focus_mode(Control::FOCUS_ALL);
			}
			region.place_left(filesystem);
		}

		inspector = _add_right("Inspector", p_make_docks_focusable);
		signals = _add_right("Signals", p_make_docks_focusable);
		groups = _add_right("Groups", p_make_docks_focusable);
	}

	~CollapseFixture() {
		SceneTree::get_singleton()->get_root()->remove_child(body);
		memdelete(body);
	}

	EditorDock *_add_right(const String &p_title, bool p_make_focusable = true) {
		EditorDock *dock = memnew(EditorDock);
		dock->set_title(p_title);
		dock->set_layout_key(p_title);
		if (p_make_focusable) {
			dock->set_focus_mode(Control::FOCUS_ALL);
		}
		region.add_right(dock);
		return dock;
	}

	TabContainer *right_tabs() const { return region.get_right_tabs(); }

	// The docks the side actually displays right now, read from live control
	// state rather than from the region's own bookkeeping.
	Vector<EditorDock *> shown_docks(Side p_side) const {
		Vector<EditorDock *> shown;
		if (p_side == Side::LEFT) {
			for (EditorDock *dock : region.get_side_docks(Side::LEFT)) {
				if (dock->is_visible()) {
					shown.push_back(dock);
				}
			}
			return shown;
		}
		if (!right_tabs()->is_visible()) {
			return shown;
		}
		EditorDock *current = Object::cast_to<EditorDock>(right_tabs()->get_current_tab_control());
		if (current) {
			shown.push_back(current);
		}
		return shown;
	}
};

struct RailStripFixture {
	EditorSideRailStrip *strip = nullptr;

	RailStripFixture(Side p_side, EditorTileDockRegion *p_region) {
		strip = memnew(EditorSideRailStrip(p_side, p_region));
		SceneTree::get_singleton()->get_root()->add_child(strip);
		strip->rebuild_toggles();
	}

	~RailStripFixture() {
		SceneTree::get_singleton()->get_root()->remove_child(strip);
		memdelete(strip);
	}
};

static void activate_rail_toggle_with_keyboard(EditorSideRailButton *p_button) {
	Ref<InputEventAction> accept;
	accept.instantiate();
	accept->set_action("ui_accept");
	accept->set_pressed(true);
	static_cast<Control *>(p_button)->gui_input(accept);

	accept->set_pressed(false);
	static_cast<Control *>(p_button)->gui_input(accept);
}

TEST_CASE("[Editor][SideRail] a fresh region is docked on both sides") {
	CollapseFixture fixture;
	CHECK(fixture.region.get_side_mode(Side::LEFT) == SideRailMode::DOCKED);
	CHECK(fixture.region.get_side_mode(Side::RIGHT) == SideRailMode::DOCKED);
	CHECK(fixture.shown_docks(Side::LEFT).size() == 1);
	CHECK(fixture.shown_docks(Side::RIGHT).size() == 1);
	CHECK(fixture.region.get_drawer_dock(Side::LEFT) == nullptr);
	CHECK(fixture.region.get_drawer_dock(Side::RIGHT) == nullptr);
}

TEST_CASE("[Editor][SideRail] collapsing one side leaves the other untouched") {
	SUBCASE("collapsing the left") {
		CollapseFixture fixture;
		fixture.region.press_rail_toggle(fixture.scene);

		CHECK(fixture.region.get_side_mode(Side::LEFT) == SideRailMode::RAILED);
		CHECK(fixture.shown_docks(Side::LEFT).is_empty());
		CHECK(fixture.region.get_side_mode(Side::RIGHT) == SideRailMode::DOCKED);
		CHECK(fixture.shown_docks(Side::RIGHT).size() == 1);
	}

	SUBCASE("collapsing the right") {
		CollapseFixture fixture;
		fixture.region.press_rail_toggle(fixture.inspector);

		CHECK(fixture.region.get_side_mode(Side::RIGHT) == SideRailMode::RAILED);
		CHECK(fixture.shown_docks(Side::RIGHT).is_empty());
		CHECK(fixture.region.get_side_mode(Side::LEFT) == SideRailMode::DOCKED);
		CHECK(fixture.shown_docks(Side::LEFT).size() == 1);
	}
}

TEST_CASE("[Editor][SideRail] the two sides are independent across every mode combination") {
	struct Case {
		const char *name = nullptr;
		bool left_collapsed = false;
		bool right_collapsed = false;
	};

	const Case cases[] = {
		{ "both docked", false, false },
		{ "left collapsed only", true, false },
		{ "right collapsed only", false, true },
		{ "both collapsed", true, true },
	};

	for (const Case &c : cases) {
		INFO(c.name);
		CollapseFixture fixture;
		if (c.left_collapsed) {
			fixture.region.close_drawer(Side::LEFT);
		}
		if (c.right_collapsed) {
			fixture.region.close_drawer(Side::RIGHT);
		}
		CHECK(fixture.shown_docks(Side::LEFT).size() == (c.left_collapsed ? 0 : 1));
		CHECK(fixture.shown_docks(Side::RIGHT).size() == (c.right_collapsed ? 0 : 1));
	}
}

TEST_CASE("[Editor][SideRail] a railed side shows exactly one dock and never two") {
	CollapseFixture fixture;
	fixture.region.press_rail_toggle(fixture.inspector); // Collapses the right side.
	REQUIRE(fixture.region.get_side_mode(Side::RIGHT) == SideRailMode::RAILED);

	fixture.region.press_rail_toggle(fixture.signals);
	Vector<EditorDock *> shown = fixture.shown_docks(Side::RIGHT);
	REQUIRE(shown.size() == 1);
	if (shown.size() != 1) {
		return;
	}
	CHECK(shown[0] == fixture.signals);
	CHECK(fixture.region.get_drawer_dock(Side::RIGHT) == fixture.signals);
	CHECK(fixture.region.is_dock_shown(fixture.signals));
	CHECK_FALSE(fixture.region.is_dock_shown(fixture.inspector));
	CHECK_FALSE(fixture.region.is_dock_shown(fixture.groups));

	SUBCASE("pressing another toggle switches without ever showing two") {
		fixture.region.press_rail_toggle(fixture.groups);
		shown = fixture.shown_docks(Side::RIGHT);
		REQUIRE(shown.size() == 1);
		if (shown.size() != 1) {
			return;
		}
		CHECK(shown[0] == fixture.groups);
		CHECK(fixture.region.get_side_mode(Side::RIGHT) == SideRailMode::RAILED);
	}

	SUBCASE("pressing the open toggle closes the drawer") {
		fixture.region.press_rail_toggle(fixture.signals);
		CHECK(fixture.shown_docks(Side::RIGHT).is_empty());
		CHECK(fixture.region.get_drawer_dock(Side::RIGHT) == nullptr);
		CHECK(fixture.region.get_side_mode(Side::RIGHT) == SideRailMode::RAILED);
	}

	SUBCASE("the close button closes the drawer") {
		fixture.region.close_drawer(Side::RIGHT);
		CHECK(fixture.shown_docks(Side::RIGHT).is_empty());
		CHECK(fixture.region.get_drawer_dock(Side::RIGHT) == nullptr);
	}

	SUBCASE("expanding returns the side to docked on the drawer's dock") {
		fixture.region.set_side_mode(Side::RIGHT, SideRailMode::DOCKED);
		CHECK(fixture.region.get_side_mode(Side::RIGHT) == SideRailMode::DOCKED);
		shown = fixture.shown_docks(Side::RIGHT);
		REQUIRE(shown.size() == 1);
		if (shown.size() != 1) {
			return;
		}
		CHECK(shown[0] == fixture.signals);
	}
}

// toggle_side_mode is exactly the primitive the docks/toggle_left_tile_rail and
// docks/toggle_right_tile_rail shortcuts drive (EditorNode::_toggle_focused_tile_rail),
// so exercising it directly against a real region is the highest seam that runs
// shipped code without constructing an EditorNode (#2010).
TEST_CASE("[Editor][SideRail] toggling the mode restores the last open drawer dock") {
	CollapseFixture fixture;
	fixture.region.press_rail_toggle(fixture.inspector); // Collapses the right side, drawer closed.
	fixture.region.press_rail_toggle(fixture.signals); // Opens the drawer on Signals.
	REQUIRE(fixture.region.get_side_mode(Side::RIGHT) == SideRailMode::RAILED);
	REQUIRE(fixture.region.get_drawer_dock(Side::RIGHT) == fixture.signals);

	fixture.region.toggle_side_mode(Side::RIGHT);
	CHECK(fixture.region.get_side_mode(Side::RIGHT) == SideRailMode::DOCKED);

	fixture.region.toggle_side_mode(Side::RIGHT);
	CHECK(fixture.region.get_side_mode(Side::RIGHT) == SideRailMode::RAILED);
	CHECK(fixture.region.get_drawer_dock(Side::RIGHT) == fixture.signals);
	const Vector<EditorDock *> shown = fixture.shown_docks(Side::RIGHT);
	REQUIRE(shown.size() == 1);
	if (shown.size() != 1) {
		return;
	}
	CHECK(shown[0] == fixture.signals);
}

TEST_CASE("[Editor][SideRail] toggling the mode on a side with no docks leaves the drawer closed") {
	CollapseFixture fixture;
	for (EditorDock *dock : fixture.region.get_side_docks(Side::RIGHT)) {
		fixture.region.set_dock_enabled(dock, false);
	}
	REQUIRE(fixture.shown_docks(Side::RIGHT).is_empty());

	fixture.region.toggle_side_mode(Side::RIGHT);
	CHECK(fixture.region.get_side_mode(Side::RIGHT) == SideRailMode::RAILED);
	CHECK(fixture.region.get_drawer_dock(Side::RIGHT) == nullptr);
	CHECK(fixture.shown_docks(Side::RIGHT).is_empty());

	fixture.region.toggle_side_mode(Side::RIGHT);
	CHECK(fixture.region.get_side_mode(Side::RIGHT) == SideRailMode::DOCKED);
	CHECK(fixture.shown_docks(Side::RIGHT).is_empty());
}

TEST_CASE("[Editor][SideRail] toggling one side's mode never affects the other side") {
	CollapseFixture fixture;
	fixture.region.press_rail_toggle(fixture.inspector); // Collapses the right side, drawer closed.
	fixture.region.press_rail_toggle(fixture.inspector); // Reopens the drawer on Inspector.
	REQUIRE(fixture.region.get_side_mode(Side::RIGHT) == SideRailMode::RAILED);
	REQUIRE(fixture.region.get_drawer_dock(Side::RIGHT) == fixture.inspector);
	REQUIRE(fixture.region.get_side_mode(Side::LEFT) == SideRailMode::DOCKED);

	fixture.region.toggle_side_mode(Side::LEFT);
	CHECK(fixture.region.get_side_mode(Side::LEFT) == SideRailMode::RAILED);
	CHECK(fixture.region.get_side_mode(Side::RIGHT) == SideRailMode::RAILED);
	CHECK(fixture.region.get_drawer_dock(Side::RIGHT) == fixture.inspector);

	fixture.region.toggle_side_mode(Side::LEFT);
	CHECK(fixture.region.get_side_mode(Side::LEFT) == SideRailMode::DOCKED);
	CHECK(fixture.region.get_side_mode(Side::RIGHT) == SideRailMode::RAILED);
	CHECK(fixture.region.get_drawer_dock(Side::RIGHT) == fixture.inspector);
}

TEST_CASE("[Editor][SideRail] pressing a non-shown toggle on a docked side only focuses it") {
	CollapseFixture fixture;
	fixture.region.press_rail_toggle(fixture.groups);

	CHECK(fixture.region.get_side_mode(Side::RIGHT) == SideRailMode::DOCKED);
	const Vector<EditorDock *> shown = fixture.shown_docks(Side::RIGHT);
	REQUIRE(shown.size() == 1);
	if (shown.size() != 1) {
		return;
	}
	CHECK(shown[0] == fixture.groups);
}

TEST_CASE("[Editor][SideRail] selecting production-focus docks is warning-free and preserves valid focus") {
	CollapseFixture fixture(false);
	REQUIRE(fixture.groups->get_focus_mode_with_override() == Control::FOCUS_NONE);

	fixture.center->set_focus_mode(Control::FOCUS_ALL);
	fixture.center->grab_focus();
	REQUIRE(SceneTree::get_singleton()->get_root()->gui_get_focus_owner() == fixture.center);

	SUBCASE("a docked rail toggle selects a different tab") {
		ErrorDetector error_detector;
		fixture.region.press_rail_toggle(fixture.groups);

		CHECK_FALSE(error_detector.has_error);
		CHECK(fixture.right_tabs()->get_current_tab_control() == fixture.groups);
	}

	SUBCASE("direct focus opens a closed railed drawer") {
		fixture.region.close_drawer(Side::RIGHT);
		REQUIRE(fixture.region.get_drawer_dock(Side::RIGHT) == nullptr);

		ErrorDetector error_detector;
		fixture.region.focus_dock(fixture.groups);

		CHECK_FALSE(error_detector.has_error);
		CHECK(fixture.region.get_drawer_dock(Side::RIGHT) == fixture.groups);
		CHECK(fixture.right_tabs()->get_current_tab_control() == fixture.groups);
	}

	Control *focus_owner = SceneTree::get_singleton()->get_root()->gui_get_focus_owner();
	REQUIRE(focus_owner != nullptr);
	CHECK(focus_owner == fixture.center);
	CHECK(focus_owner->is_visible_in_tree());
	CHECK(focus_owner->get_focus_mode_with_override() != Control::FOCUS_NONE);
}

TEST_CASE("[Editor][SideRail] production-focus rail buttons are warning-free on both sides") {
	const Side sides[] = { Side::LEFT, Side::RIGHT };
	for (Side side : sides) {
		const int toggle_count = side == Side::LEFT ? 2 : 3;
		for (int toggle_index = 0; toggle_index < toggle_count; toggle_index++) {
			INFO((side == Side::LEFT ? "left rail" : "right rail"));
			INFO(toggle_index);

			CollapseFixture fixture(false, true);
			RailStripFixture rail(side, &fixture.region);
			REQUIRE(rail.strip->get_toggle_buttons().size() == (uint32_t)toggle_count);
			EditorSideRailButton *button = rail.strip->get_toggle_buttons()[toggle_index];
			REQUIRE(button->get_focus_mode() == Control::FOCUS_ACCESSIBILITY);

			fixture.center->set_focus_mode(Control::FOCUS_ALL);
			fixture.center->grab_focus();
			REQUIRE(SceneTree::get_singleton()->get_root()->gui_get_focus_owner() == fixture.center);

			// Keyboard ui_accept and the accessibility click action both use
			// BaseButton::_pressed(), so this drives their shared production
			// signal path through EditorSideRailStrip::_toggle_pressed().
			ErrorDetector error_detector;
			activate_rail_toggle_with_keyboard(button);

			CHECK_FALSE(error_detector.has_error);
			Control *focus_owner = SceneTree::get_singleton()->get_root()->gui_get_focus_owner();
			REQUIRE(focus_owner != nullptr);
			CHECK(focus_owner == fixture.center);
			CHECK(focus_owner->is_visible_in_tree());
			CHECK(focus_owner->get_focus_mode_with_override() != Control::FOCUS_NONE);
		}
	}
}

TEST_CASE("[Editor][SideRail] production-focus rail buttons open and close both drawers without warnings") {
	const Side sides[] = { Side::LEFT, Side::RIGHT };
	for (Side side : sides) {
		INFO((side == Side::LEFT ? "left rail" : "right rail"));

		CollapseFixture fixture(false, true);
		RailStripFixture rail(side, &fixture.region);
		REQUIRE(rail.strip->get_toggle_buttons().size() > 1);
		EditorSideRailButton *button = rail.strip->get_toggle_buttons()[1];
		EditorDock *dock = rail.strip->get_toggle_docks()[1];

		fixture.center->set_focus_mode(Control::FOCUS_ALL);
		fixture.center->grab_focus();
		fixture.region.close_drawer(side);
		REQUIRE(fixture.region.get_drawer_dock(side) == nullptr);

		ErrorDetector error_detector;
		activate_rail_toggle_with_keyboard(button);
		CHECK(fixture.region.get_drawer_dock(side) == dock);

		activate_rail_toggle_with_keyboard(button);
		CHECK(fixture.region.get_drawer_dock(side) == nullptr);

		CHECK_FALSE(error_detector.has_error);
		Control *focus_owner = SceneTree::get_singleton()->get_root()->gui_get_focus_owner();
		REQUIRE(focus_owner != nullptr);
		CHECK(focus_owner == fixture.center);
		CHECK(focus_owner->is_visible_in_tree());
		CHECK(focus_owner->get_focus_mode_with_override() != Control::FOCUS_NONE);
	}
}

TEST_CASE("[Editor][SideRail] disabling the drawer dock never leaves the drawer open on it") {
	CollapseFixture fixture;
	fixture.region.press_rail_toggle(fixture.inspector);
	fixture.region.press_rail_toggle(fixture.signals);
	REQUIRE(fixture.region.get_drawer_dock(Side::RIGHT) == fixture.signals);

	fixture.region.set_dock_enabled(fixture.signals, false);

	CHECK(fixture.region.get_drawer_dock(Side::RIGHT) == nullptr);
	CHECK(fixture.shown_docks(Side::RIGHT).is_empty());
	CHECK_FALSE(fixture.region.is_dock_shown(fixture.signals));
	CHECK(fixture.region.get_side_mode(Side::RIGHT) == SideRailMode::RAILED);

	SUBCASE("and re-enabling it does not silently reopen the drawer") {
		fixture.region.set_dock_enabled(fixture.signals, true);
		CHECK(fixture.shown_docks(Side::RIGHT).is_empty());
	}

	SUBCASE("another dock can still be opened afterwards") {
		fixture.region.press_rail_toggle(fixture.groups);
		const Vector<EditorDock *> shown = fixture.shown_docks(Side::RIGHT);
		REQUIRE(shown.size() == 1);
		if (shown.size() != 1) {
			return;
		}
		CHECK(shown[0] == fixture.groups);
	}
}

TEST_CASE("[Editor][SideRail] disabling a dock on a docked side keeps today's tab fallback") {
	CollapseFixture fixture;
	REQUIRE(fixture.right_tabs()->get_current_tab() == 0);

	fixture.region.set_dock_enabled(fixture.inspector, false);

	const Vector<EditorDock *> shown = fixture.shown_docks(Side::RIGHT);
	REQUIRE(shown.size() == 1);
	if (shown.size() != 1) {
		return;
	}
	CHECK(shown[0] == fixture.signals);
}

TEST_CASE("[Editor][SideRail] disabling the only left dock collapses its column without touching the right") {
	CollapseFixture fixture;
	fixture.region.set_dock_enabled(fixture.scene, false);

	CHECK(fixture.shown_docks(Side::LEFT).is_empty());
	CHECK(fixture.shown_docks(Side::RIGHT).size() == 1);
}

TEST_CASE("[Editor][SideRail] focusing a dock on a railed side opens it in the drawer") {
	CollapseFixture fixture;
	fixture.region.close_drawer(Side::RIGHT);
	REQUIRE(fixture.shown_docks(Side::RIGHT).is_empty());

	fixture.region.focus_dock(fixture.groups);

	CHECK(fixture.region.get_drawer_dock(Side::RIGHT) == fixture.groups);
	const Vector<EditorDock *> shown = fixture.shown_docks(Side::RIGHT);
	REQUIRE(shown.size() == 1);
	if (shown.size() != 1) {
		return;
	}
	CHECK(shown[0] == fixture.groups);

	SUBCASE("focusing a left dock on a railed left side shows it again") {
		fixture.region.close_drawer(Side::LEFT);
		REQUIRE(fixture.shown_docks(Side::LEFT).is_empty());
		fixture.region.focus_dock(fixture.scene);
		CHECK(fixture.shown_docks(Side::LEFT).size() == 1);
		CHECK(fixture.region.get_drawer_dock(Side::LEFT) == fixture.scene);
	}
}

TEST_CASE("[Editor][SideRail] restoring a side to docked restores its pre-collapse width") {
	CollapseFixture fixture;
	PackedInt32Array offsets;
	offsets.push_back(220);
	offsets.push_back(-240);
	fixture.body->set_split_offsets(offsets);

	SUBCASE("left") {
		fixture.region.close_drawer(Side::LEFT);
		PackedInt32Array collapsed = fixture.body->get_split_offsets();
		REQUIRE(collapsed.size() == 1);
		if (collapsed.size() != 1) {
			return;
		}
		CHECK(collapsed[0] == -240);

		fixture.region.set_side_mode(Side::LEFT, SideRailMode::DOCKED);
		PackedInt32Array restored = fixture.body->get_split_offsets();
		REQUIRE(restored.size() == 2);
		if (restored.size() != 2) {
			return;
		}
		CHECK(restored[0] == 220);
		CHECK(restored[1] == -240);
	}

	SUBCASE("right") {
		fixture.region.close_drawer(Side::RIGHT);
		PackedInt32Array collapsed = fixture.body->get_split_offsets();
		REQUIRE(collapsed.size() == 1);
		if (collapsed.size() != 1) {
			return;
		}
		CHECK(collapsed[0] == 220);

		fixture.region.set_side_mode(Side::RIGHT, SideRailMode::DOCKED);
		PackedInt32Array restored = fixture.body->get_split_offsets();
		REQUIRE(restored.size() == 2);
		if (restored.size() != 2) {
			return;
		}
		CHECK(restored[0] == 220);
		CHECK(restored[1] == -240);
	}

	SUBCASE("both sides collapsed and restored") {
		fixture.region.close_drawer(Side::LEFT);
		fixture.region.close_drawer(Side::RIGHT);
		fixture.region.set_side_mode(Side::LEFT, SideRailMode::DOCKED);
		fixture.region.set_side_mode(Side::RIGHT, SideRailMode::DOCKED);
		PackedInt32Array restored = fixture.body->get_split_offsets();
		REQUIRE(restored.size() == 2);
		if (restored.size() != 2) {
			return;
		}
		CHECK(restored[0] == 220);
		CHECK(restored[1] == -240);
	}
}

TEST_CASE("[Editor][SideRail] collapsing does not disturb the persisted right tab keys") {
	CollapseFixture fixture;
	fixture.right_tabs()->set_current_tab(1);

	Ref<ConfigFile> before;
	before.instantiate();
	const String section = "WorkspaceLeaf_0";
	fixture.region.save_layout(before, section);

	fixture.region.close_drawer(Side::RIGHT);

	Ref<ConfigFile> after;
	after.instantiate();
	fixture.region.save_layout(after, section);

	CHECK(String(after->get_value(section, "tile_dock_right")) == String(before->get_value(section, "tile_dock_right")));
	CHECK(int(after->get_value(section, "tile_dock_right_selected_tab_idx")) ==
			int(before->get_value(section, "tile_dock_right_selected_tab_idx")));
}

TEST_CASE("[Editor][SideRail] save and load round trip mode, drawer dock and both widths") {
	const String section = "WorkspaceLeaf_0";
	Ref<ConfigFile> config;
	config.instantiate();

	{
		CollapseFixture fixture;
		PackedInt32Array offsets;
		offsets.push_back(220);
		offsets.push_back(-240);
		fixture.body->set_split_offsets(offsets);

		fixture.region.close_drawer(Side::LEFT);
		fixture.region.press_rail_toggle(fixture.inspector); // Collapses the right side.
		fixture.region.press_rail_toggle(fixture.groups); // Opens the drawer on Groups.
		fixture.region.save_layout(config, section);
	}

	CHECK(bool(config->get_value(section, "tile_rail_left")));
	CHECK(bool(config->get_value(section, "tile_rail_right")));
	CHECK(String(config->get_value(section, "tile_drawer_dock_left")).is_empty());
	CHECK(String(config->get_value(section, "tile_drawer_dock_right")) == "Groups");

	CollapseFixture restored;
	restored.region.load_layout(config, section);

	CHECK(restored.region.get_side_mode(Side::LEFT) == SideRailMode::RAILED);
	CHECK(restored.region.get_side_mode(Side::RIGHT) == SideRailMode::RAILED);
	CHECK(restored.region.get_drawer_dock(Side::LEFT) == nullptr);
	CHECK(restored.region.get_drawer_dock(Side::RIGHT) == restored.groups);
	CHECK(restored.shown_docks(Side::LEFT).is_empty());
	const Vector<EditorDock *> shown_right = restored.shown_docks(Side::RIGHT);
	REQUIRE(shown_right.size() == 1);
	if (shown_right.size() != 1) {
		return;
	}
	CHECK(shown_right[0] == restored.groups);

	// Expanding both sides after the load lands on the widths the layout was
	// saved with, even though neither gap existed while it was collapsed.
	restored.region.set_side_mode(Side::LEFT, SideRailMode::DOCKED);
	restored.region.set_side_mode(Side::RIGHT, SideRailMode::DOCKED);
	PackedInt32Array widths = restored.body->get_split_offsets();
	REQUIRE(widths.size() == 2);
	if (widths.size() != 2) {
		return;
	}
	CHECK(widths[0] == int(220 / EDSCALE) * EDSCALE);
	CHECK(widths[1] == int(-240 / EDSCALE) * EDSCALE);
}

TEST_CASE("[Editor][SideRail] a layout with no rail keys loads as docked") {
	const String section = "WorkspaceLeaf_0";
	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value(section, "tile_dock_hsplit_1", 180);
	config->set_value(section, "tile_dock_hsplit_2", -260);

	CollapseFixture fixture;
	// Collapsed first, so the load has to actively restore DOCKED rather than
	// simply leaving an already-docked side alone.
	fixture.region.close_drawer(Side::LEFT);
	fixture.region.close_drawer(Side::RIGHT);

	fixture.region.load_layout(config, section);

	CHECK(fixture.region.get_side_mode(Side::LEFT) == SideRailMode::DOCKED);
	CHECK(fixture.region.get_side_mode(Side::RIGHT) == SideRailMode::DOCKED);
	CHECK(fixture.region.get_drawer_dock(Side::LEFT) == nullptr);
	CHECK(fixture.region.get_drawer_dock(Side::RIGHT) == nullptr);
	CHECK(fixture.shown_docks(Side::LEFT).size() == 1);
	CHECK(fixture.shown_docks(Side::RIGHT).size() == 1);

	PackedInt32Array widths = fixture.body->get_split_offsets();
	REQUIRE(widths.size() == 2);
	if (widths.size() != 2) {
		return;
	}
	CHECK(widths[0] == 180 * EDSCALE);
	CHECK(widths[1] == -260 * EDSCALE);
}

TEST_CASE("[Editor][SideRail] a stored drawer dock that cannot be shown loads as a closed drawer") {
	const String section = "WorkspaceLeaf_0";

	SUBCASE("the dock no longer exists") {
		Ref<ConfigFile> config;
		config.instantiate();
		config->set_value(section, "tile_rail_right", true);
		config->set_value(section, "tile_drawer_dock_right", "AnimationPlayer");

		CollapseFixture fixture;
		fixture.region.load_layout(config, section);

		CHECK(fixture.region.get_side_mode(Side::RIGHT) == SideRailMode::RAILED);
		CHECK(fixture.region.get_drawer_dock(Side::RIGHT) == nullptr);
		CHECK(fixture.shown_docks(Side::RIGHT).is_empty());
	}

	SUBCASE("the dock is disabled in this session") {
		Ref<ConfigFile> config;
		config.instantiate();
		config->set_value(section, "tile_rail_right", true);
		config->set_value(section, "tile_drawer_dock_right", "Signals");

		CollapseFixture fixture;
		fixture.region.set_dock_enabled(fixture.signals, false);
		fixture.region.load_layout(config, section);

		CHECK(fixture.region.get_drawer_dock(Side::RIGHT) == nullptr);
		CHECK(fixture.shown_docks(Side::RIGHT).is_empty());
	}
}

struct RailWorkspaceHarness {
	Control *host = nullptr;
	EditorData editor_data;
	EditorSelection *selection = nullptr;
	EditorSceneWorkspace *workspace = nullptr;

	void mount() {
		host = memnew(Control);
		host->set_custom_minimum_size(Size2(1200, 600));
		SceneTree::get_singleton()->get_root()->add_child(host);
		selection = memnew(EditorSelection);
		workspace = EditorSceneWorkspace::create_single_leaf_workspace(selection, &editor_data);
		host->add_child(workspace);
		workspace->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		host->set_size(Size2(1200, 600));
		workspace->set_size(Size2(1200, 600));
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

TEST_CASE("[Editor][SideRail] collapsing a side in one tile leaves its sibling tile untouched") {
	RailWorkspaceHarness h;
	h.mount();
	h.pump();
	h.workspace->split(h.workspace->get_focused_leaf(), false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();

	Vector<ScenePaneTile *> tiles = h.workspace->get_tiles();
	REQUIRE(tiles.size() == 2);
	if (tiles.size() != 2) {
		h.unmount();
		return;
	}

	EditorTileDockRegion *first = tiles[0]->get_dock_region();
	EditorTileDockRegion *second = tiles[1]->get_dock_region();

	first->close_drawer(Side::LEFT);
	first->close_drawer(Side::RIGHT);
	h.pump();

	CHECK(first->get_side_mode(Side::LEFT) == SideRailMode::RAILED);
	CHECK(first->get_side_mode(Side::RIGHT) == SideRailMode::RAILED);
	CHECK(second->get_side_mode(Side::LEFT) == SideRailMode::DOCKED);
	CHECK(second->get_side_mode(Side::RIGHT) == SideRailMode::DOCKED);
	CHECK_FALSE(first->get_right_tabs()->is_visible());
	CHECK(second->get_right_tabs()->is_visible());
	CHECK(second->get_side_docks(Side::LEFT).size() > 0);
	CHECK(second->is_dock_shown(second->get_side_docks(Side::LEFT)[0]));

	h.unmount();
}

// EditorNode::_toggle_focused_tile_rail resolves the target tile through
// EditorSceneWorkspace::get_focused_tile() and calls toggle_side_mode on that
// tile's own region, exactly what this drives against a real split workspace.
TEST_CASE("[Editor][SideRail] toggling the mode targets only the workspace's focused tile") {
	RailWorkspaceHarness h;
	h.mount();
	h.pump();
	h.workspace->split(h.workspace->get_focused_leaf(), false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();

	Vector<ScenePaneTile *> tiles = h.workspace->get_tiles();
	REQUIRE(tiles.size() == 2);
	if (tiles.size() != 2) {
		h.unmount();
		return;
	}

	ScenePaneTile *focused_tile = h.workspace->get_focused_tile();
	REQUIRE(focused_tile != nullptr);
	if (!focused_tile) {
		h.unmount();
		return;
	}
	ScenePaneTile *other_tile = focused_tile == tiles[0] ? tiles[1] : tiles[0];

	EditorTileDockRegion *focused_region = focused_tile->get_dock_region();
	EditorTileDockRegion *other_region = other_tile->get_dock_region();

	focused_region->toggle_side_mode(Side::LEFT);
	h.pump();

	CHECK(focused_region->get_side_mode(Side::LEFT) == SideRailMode::RAILED);
	CHECK(other_region->get_side_mode(Side::LEFT) == SideRailMode::DOCKED);
	CHECK(other_region->get_side_mode(Side::RIGHT) == SideRailMode::DOCKED);

	focused_region->toggle_side_mode(Side::RIGHT);
	h.pump();

	CHECK(focused_region->get_side_mode(Side::RIGHT) == SideRailMode::RAILED);
	CHECK(other_region->get_side_mode(Side::LEFT) == SideRailMode::DOCKED);
	CHECK(other_region->get_side_mode(Side::RIGHT) == SideRailMode::DOCKED);

	h.unmount();
}

TEST_CASE("[Editor][SideRail] a side whose docks are all disabled keeps its rail") {
	RailWorkspaceHarness h;
	h.mount();
	h.pump();

	ScenePaneTile *tile = h.workspace->get_focused_tile();
	REQUIRE(tile != nullptr);
	if (!tile) {
		h.unmount();
		return;
	}

	EditorTileDockRegion *region = tile->get_dock_region();
	for (EditorDock *dock : region->get_side_docks(Side::RIGHT)) {
		region->set_dock_enabled(dock, false);
	}
	for (EditorDock *dock : region->get_side_docks(Side::LEFT)) {
		region->set_dock_enabled(dock, false);
	}
	h.pump();

	REQUIRE(tile->get_left_rail() != nullptr);
	REQUIRE(tile->get_right_rail() != nullptr);
	if (!tile->get_left_rail() || !tile->get_right_rail()) {
		h.unmount();
		return;
	}
	CHECK(tile->get_left_rail()->is_visible());
	CHECK(tile->get_right_rail()->is_visible());

	h.unmount();
}

TEST_CASE("[Editor][SideRail] the rail reflects the collapse state it drives") {
	RailWorkspaceHarness h;
	h.mount();
	h.pump();

	ScenePaneTile *tile = h.workspace->get_focused_tile();
	REQUIRE(tile != nullptr);
	if (!tile) {
		h.unmount();
		return;
	}
	EditorSideRailStrip *rail = tile->get_right_rail();
	EditorTileDockRegion *region = tile->get_dock_region();
	REQUIRE(rail != nullptr);
	if (!rail) {
		h.unmount();
		return;
	}
	REQUIRE(rail->get_toggle_docks().size() > 1);
	if (rail->get_toggle_docks().size() <= 1) {
		h.unmount();
		return;
	}

	EditorDock *shown = Object::cast_to<EditorDock>(region->get_right_tabs()->get_current_tab_control());
	REQUIRE(shown != nullptr);
	if (!shown) {
		h.unmount();
		return;
	}

	// Pressing the shown dock's toggle collapses the side, exactly as clicking
	// the button does.
	region->press_rail_toggle(shown);
	h.pump();
	CHECK(region->get_side_mode(Side::RIGHT) == SideRailMode::RAILED);
	for (EditorSideRailButton *button : rail->get_toggle_buttons()) {
		CHECK_FALSE(button->is_pressed());
	}

	EditorDock *other = rail->get_toggle_docks()[1] == shown ? rail->get_toggle_docks()[0] : rail->get_toggle_docks()[1];
	region->press_rail_toggle(other);
	h.pump();
	CHECK(region->get_drawer_dock(Side::RIGHT) == other);
	int pressed_count = 0;
	for (EditorSideRailButton *button : rail->get_toggle_buttons()) {
		pressed_count += button->is_pressed() ? 1 : 0;
	}
	CHECK(pressed_count == 1);

	h.unmount();
}

// The real docks/toggle_left_tile_rail and docks/toggle_right_tile_rail
// shortcuts are registered from deep inside EditorNode's constructor, which no
// doctest can instantiate (#2010). This mirrors
// TestEditorBoardShortcuts::"Board switcher chords do not collide with script
// editor history chords": it exercises the same ED_SHORTCUT/ED_SHORTCUT_OVERRIDE
// registration entry points production code calls, under test-only paths
// carrying the exact chords involved, then checks the real
// Shortcut::matches_event() dispatch logic SceneTree's shortcut_input pass
// uses. KeyModifierMask::CMD_OR_CTRL resolves at compile time to META on macOS
// builds and CTRL everywhere else (core/os/keyboard.h), so this test is
// meaningful on every platform it is compiled for.
TEST_CASE("[Editor][SideRail] tile rail toggle chords do not collide with any other editor shortcut") {
	ERR_FAIL_NULL(EditorSettings::get_singleton());

	Ref<Shortcut> toggle_left = ED_SHORTCUT("test/toggle_left_tile_rail", "Toggle Left Tile Rail",
			KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::BRACKETLEFT);
	Ref<Shortcut> toggle_right = ED_SHORTCUT("test/toggle_right_tile_rail", "Toggle Right Tile Rail",
			KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::BRACKETRIGHT);

	Ref<InputEventKey> toggle_left_event = InputEventKey::create_reference(KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::BRACKETLEFT);
	Ref<InputEventKey> toggle_right_event = InputEventKey::create_reference(KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::BRACKETRIGHT);

	CHECK(toggle_left->matches_event(toggle_left_event));
	CHECK(toggle_right->matches_event(toggle_right_event));

	// editor/animation_editor: CMD_OR_CTRL + BracketLeft/Right (no Shift), for
	// nudging audio track start/end offsets.
	Ref<Shortcut> set_start_offset = ED_SHORTCUT("test/set_start_offset", "Set Start Offset", KeyModifierMask::CMD_OR_CTRL | Key::BRACKETLEFT);
	Ref<Shortcut> set_end_offset = ED_SHORTCUT("test/set_end_offset", "Set End Offset", KeyModifierMask::CMD_OR_CTRL | Key::BRACKETRIGHT);
	CHECK_FALSE(set_start_offset->matches_event(toggle_left_event));
	CHECK_FALSE(set_end_offset->matches_event(toggle_right_event));
	CHECK_FALSE(toggle_left->matches_event(InputEventKey::create_reference(KeyModifierMask::CMD_OR_CTRL | Key::BRACKETLEFT)));
	CHECK_FALSE(toggle_right->matches_event(InputEventKey::create_reference(KeyModifierMask::CMD_OR_CTRL | Key::BRACKETRIGHT)));

	// script_editor/history_previous and /history_next: Alt+Left/Right, with a
	// macOS override to Alt+Meta+Left/Right — the exact family of chord that
	// caused the #1977 board-shortcut collision.
	Ref<Shortcut> history_previous = ED_SHORTCUT("test/history_previous", "History Previous", KeyModifierMask::ALT | Key::LEFT);
	ED_SHORTCUT_OVERRIDE("test/history_previous", "macos", KeyModifierMask::ALT | KeyModifierMask::META | Key::LEFT);
	Ref<Shortcut> history_next = ED_SHORTCUT("test/history_next", "History Next", KeyModifierMask::ALT | Key::RIGHT);
	ED_SHORTCUT_OVERRIDE("test/history_next", "macos", KeyModifierMask::ALT | KeyModifierMask::META | Key::RIGHT);
	CHECK_FALSE(history_previous->matches_event(toggle_left_event));
	CHECK_FALSE(history_next->matches_event(toggle_right_event));
	CHECK_FALSE(toggle_left->matches_event(InputEventKey::create_reference(KeyModifierMask::ALT | Key::LEFT)));
	CHECK_FALSE(toggle_right->matches_event(InputEventKey::create_reference(KeyModifierMask::ALT | Key::RIGHT)));

	// sprite_frames/move_left and /move_right: CMD_OR_CTRL+Left/Right, with no
	// Shift, so textually close to the rail chords but not identical.
	Ref<Shortcut> move_left = ED_SHORTCUT("test/move_left", "Move Frame Left", KeyModifierMask::CMD_OR_CTRL | Key::LEFT);
	Ref<Shortcut> move_right = ED_SHORTCUT("test/move_right", "Move Frame Right", KeyModifierMask::CMD_OR_CTRL | Key::RIGHT);
	CHECK_FALSE(move_left->matches_event(toggle_left_event));
	CHECK_FALSE(move_right->matches_event(toggle_right_event));

	// editor/previous_board and /next_board: CMD_OR_CTRL+PageUp/PageDown,
	// unrelated keys, included as a sanity check that a different key on the
	// same modifier set is never mistaken for a match.
	Ref<Shortcut> previous_board = ED_SHORTCUT("test/previous_board", "Previous Board", KeyModifierMask::CMD_OR_CTRL | Key::PAGEUP);
	Ref<Shortcut> next_board = ED_SHORTCUT("test/next_board", "Next Board", KeyModifierMask::CMD_OR_CTRL | Key::PAGEDOWN);
	CHECK_FALSE(previous_board->matches_event(toggle_left_event));
	CHECK_FALSE(next_board->matches_event(toggle_right_event));
}

} // namespace TestSideRailCollapse
