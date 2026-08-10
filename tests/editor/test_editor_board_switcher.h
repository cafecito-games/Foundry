/**************************************************************************/
/*  test_editor_board_switcher.h                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                              GODOT ENGINE                              */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
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

#include "editor/editor_board.h"
#include "editor/editor_board_strip.h"
#include "editor/editor_data.h"
#include "editor/gui/editor_board_switcher.h"

#include "core/io/config_file.h"

#include "scene/gui/button.h"
#include "scene/gui/control.h"
#include "scene/gui/line_edit.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"

namespace TestEditorBoardSwitcher {

struct BoardSwitcherHarness {
	Control *host = nullptr;
	EditorData editor_data;
	EditorSelection *selection = nullptr;
	EditorBoardStrip *strip = nullptr;
	EditorBoardSwitcher *switcher = nullptr;

	void mount() {
		host = memnew(Control);
		SceneTree::get_singleton()->get_root()->add_child(host);
		selection = memnew(EditorSelection);
		strip = EditorBoardStrip::create(selection, &editor_data);
		host->add_child(strip);
		switcher = memnew(EditorBoardSwitcher);
		host->add_child(switcher);
		switcher->setup(strip);
	}

	void unmount() {
		SceneTree::get_singleton()->get_root()->remove_child(host);
		memdelete(host);
		memdelete(selection);
	}

	// Board buttons are always the first strip->get_board_count() children, followed by
	// the add button and the overview toggle.
	Button *board_button(int p_index) const {
		if (p_index < 0 || p_index >= switcher->get_child_count()) {
			return nullptr;
		}
		return Object::cast_to<Button>(switcher->get_child(p_index));
	}

	LineEdit *find_rename_edit() const {
		for (int i = 0; i < switcher->get_child_count(); i++) {
			if (LineEdit *edit = Object::cast_to<LineEdit>(switcher->get_child(i))) {
				return edit;
			}
		}
		return nullptr;
	}

	void double_click(Button *p_button) const {
		Ref<InputEventMouseButton> event;
		event.instantiate();
		event->set_button_index(MouseButton::LEFT);
		event->set_pressed(true);
		event->set_double_click(true);
		p_button->emit_signal(SceneStringName(gui_input), event);
	}
};

TEST_CASE("[Editor][BoardSwitcher] Rebuilds one button per board and marks the active one") {
	BoardSwitcherHarness harness;
	harness.mount();

	// One board button plus the add button and the overview toggle.
	REQUIRE(harness.switcher->get_child_count() == 3);
	Button *first = harness.board_button(0);
	REQUIRE(first != nullptr);
	if (!first) {
		harness.unmount();
		return;
	}
	CHECK(first->get_text() == harness.strip->get_board(0)->get_title());
	CHECK(first->is_pressed());

	harness.strip->add_board();
	REQUIRE(harness.switcher->get_child_count() == 4);
	Button *second = harness.board_button(1);
	REQUIRE(second != nullptr);
	if (!second) {
		harness.unmount();
		return;
	}
	CHECK(second->get_text() == harness.strip->get_board(1)->get_title());
	CHECK_FALSE(second->is_pressed());

	harness.strip->set_active_board(1);
	first = harness.board_button(0);
	second = harness.board_button(1);
	REQUIRE(first != nullptr);
	REQUIRE(second != nullptr);
	if (!first || !second) {
		harness.unmount();
		return;
	}
	CHECK_FALSE(first->is_pressed());
	CHECK(second->is_pressed());

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] The add button appends and activates a new board") {
	BoardSwitcherHarness harness;
	harness.mount();

	const int board_count_before = harness.strip->get_board_count();
	// The add button is the second-to-last child; the overview toggle is last.
	Button *add_button = harness.board_button(harness.switcher->get_child_count() - 2);
	REQUIRE(add_button != nullptr);
	if (!add_button) {
		harness.unmount();
		return;
	}
	add_button->emit_signal(SceneStringName(pressed));

	CHECK(harness.strip->get_board_count() == board_count_before + 1);
	CHECK(harness.strip->get_active_index() == board_count_before);

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Double-clicking a board button renames it inline") {
	BoardSwitcherHarness harness;
	harness.mount();

	Button *button = harness.board_button(0);
	REQUIRE(button != nullptr);
	if (!button) {
		harness.unmount();
		return;
	}
	CHECK_FALSE(harness.switcher->is_renaming());

	harness.double_click(button);
	CHECK(harness.switcher->is_renaming());

	LineEdit *rename_edit = harness.find_rename_edit();
	REQUIRE(rename_edit != nullptr);
	if (!rename_edit) {
		harness.unmount();
		return;
	}
	CHECK(rename_edit->get_text() == harness.strip->get_board(0)->get_title());

	rename_edit->set_text("face shader");
	rename_edit->emit_signal(SceneStringName(text_submitted), String("face shader"));

	CHECK_FALSE(harness.switcher->is_renaming());
	CHECK(harness.strip->get_board(0)->get_title() == "face shader");
	Button *renamed_button = harness.board_button(0);
	REQUIRE(renamed_button != nullptr);
	if (!renamed_button) {
		harness.unmount();
		return;
	}
	CHECK(renamed_button->get_text() == "face shader");

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Losing focus commits the rename") {
	BoardSwitcherHarness harness;
	harness.mount();

	Button *button = harness.board_button(0);
	REQUIRE(button != nullptr);
	if (!button) {
		harness.unmount();
		return;
	}
	harness.double_click(button);
	REQUIRE(harness.switcher->is_renaming());

	LineEdit *rename_edit = harness.find_rename_edit();
	REQUIRE(rename_edit != nullptr);
	if (!rename_edit) {
		harness.unmount();
		return;
	}
	rename_edit->set_text("renamed by focus loss");
	rename_edit->emit_signal(SceneStringName(focus_exited));

	CHECK_FALSE(harness.switcher->is_renaming());
	CHECK(harness.strip->get_board(0)->get_title() == "renamed by focus loss");

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] An empty commit leaves the title unchanged") {
	BoardSwitcherHarness harness;
	harness.mount();

	const String original_title = harness.strip->get_board(0)->get_title();
	Button *button = harness.board_button(0);
	REQUIRE(button != nullptr);
	if (!button) {
		harness.unmount();
		return;
	}
	harness.double_click(button);
	REQUIRE(harness.switcher->is_renaming());

	LineEdit *rename_edit = harness.find_rename_edit();
	REQUIRE(rename_edit != nullptr);
	if (!rename_edit) {
		harness.unmount();
		return;
	}
	rename_edit->set_text("   ");
	rename_edit->emit_signal(SceneStringName(text_submitted), String("   "));

	CHECK_FALSE(harness.switcher->is_renaming());
	CHECK(harness.strip->get_board(0)->get_title() == original_title);

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] A restore rebuilds the switcher without board_added") {
	BoardSwitcherHarness harness;
	harness.mount();

	harness.strip->add_board();
	harness.strip->get_board(1)->set_title("kept across restore");

	Ref<ConfigFile> config;
	config.instantiate();
	EditorBoardStrip::save_to_config(config, harness.strip);

	// restore_from_config() replaces every EditorBoard instance without emitting
	// board_added (that would double-wire boards already covered by boards_restored),
	// so this exercises the switcher's boards_restored listener specifically.
	harness.strip->restore_from_config(config);

	REQUIRE(harness.switcher->get_child_count() == harness.strip->get_board_count() + 2);
	Button *restored = harness.board_button(1);
	REQUIRE(restored != nullptr);
	if (!restored) {
		harness.unmount();
		return;
	}
	CHECK(restored->get_text() == "kept across restore");

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Switching boards while a rename is pending commits the typed title") {
	BoardSwitcherHarness harness;
	harness.mount();

	harness.strip->add_board();
	REQUIRE(harness.switcher->get_child_count() == 4);

	Button *first = harness.board_button(0);
	Button *second = harness.board_button(1);
	REQUIRE(first != nullptr);
	REQUIRE(second != nullptr);
	if (!first || !second) {
		harness.unmount();
		return;
	}

	harness.double_click(first);
	REQUIRE(harness.switcher->is_renaming());

	LineEdit *rename_edit = harness.find_rename_edit();
	REQUIRE(rename_edit != nullptr);
	if (!rename_edit) {
		harness.unmount();
		return;
	}
	rename_edit->set_text("renamed mid-switch");

	// A single click on another board's button moves neither keyboard focus (the board
	// buttons use FOCUS_ACCESSIBILITY, so focus_exited only fires with a screen reader
	// enabled) nor submits the LineEdit -- it just fires the button's pressed signal, the
	// same path _on_board_button_gui_input takes for a real click. Regression coverage
	// for the rename being silently dropped when _rebuild() ran via _cancel_rename().
	second->emit_signal(SceneStringName(pressed));

	CHECK_FALSE(harness.switcher->is_renaming());
	CHECK(harness.strip->get_active_index() == 1);
	CHECK(harness.strip->get_board(0)->get_title() == "renamed mid-switch");

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Closing the active board through the strip rebuilds around the new active board") {
	BoardSwitcherHarness harness;
	harness.mount();

	harness.strip->add_board();
	REQUIRE(harness.switcher->get_child_count() == 4);
	harness.strip->set_active_board(1);
	REQUIRE(harness.strip->get_active_index() == 1);

	// EditorBoardSwitcher has no close control of its own yet, so this drives
	// close_board() directly the way a future close action will. Closing the active
	// board fires active_board_changed (the strip activates a neighbour first) and then
	// board_removed, each triggering its own switcher rebuild; what matters is that the
	// buttons are left consistent once both have landed.
	CHECK(harness.strip->close_board(1));

	CHECK(harness.switcher->get_child_count() == 3);
	Button *remaining = harness.board_button(0);
	REQUIRE(remaining != nullptr);
	if (!remaining) {
		harness.unmount();
		return;
	}
	CHECK(remaining->is_pressed());
	CHECK(harness.strip->get_active_index() == 0);

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Renaming while the overview is up refreshes the caption") {
	BoardSwitcherHarness harness;
	harness.mount();
	harness.strip->set_size(Size2(800, 600));

	harness.strip->set_overview(true);

	Control *overlay = harness.strip->get_caption_overlay();
	REQUIRE(overlay != nullptr);
	if (!overlay) {
		harness.unmount();
		return;
	}
	REQUIRE(overlay->get_child_count() == 1);
	Button *caption = Object::cast_to<Button>(overlay->get_child(0));
	REQUIRE(caption != nullptr);
	if (!caption) {
		harness.unmount();
		return;
	}

	// The switcher's own board button stays reachable and renameable while the overview
	// is up; nothing about it gates renaming on the strip's view mode.
	Button *button = harness.board_button(0);
	REQUIRE(button != nullptr);
	if (!button) {
		harness.unmount();
		return;
	}
	harness.double_click(button);
	REQUIRE(harness.switcher->is_renaming());

	LineEdit *rename_edit = harness.find_rename_edit();
	REQUIRE(rename_edit != nullptr);
	if (!rename_edit) {
		harness.unmount();
		return;
	}
	rename_edit->set_text("renamed during overview");
	rename_edit->emit_signal(SceneStringName(text_submitted), String("renamed during overview"));

	CHECK(harness.strip->get_board(0)->get_title() == "renamed during overview");
	// The caption is a snapshot taken when it was last built, not a live binding to the
	// board's title, so nothing but an explicit refresh keeps it in step with a rename
	// that happens while it is on screen. _rebuild_captions() replaces every caption
	// control outright, so the refreshed one is re-resolved from the overlay rather than
	// read off the pre-rename pointer, which the refresh may have already queued for
	// freeing.
	REQUIRE(overlay->get_child_count() == 1);
	Button *refreshed_caption = Object::cast_to<Button>(overlay->get_child(0));
	REQUIRE(refreshed_caption != nullptr);
	if (!refreshed_caption) {
		harness.unmount();
		return;
	}
	CHECK(refreshed_caption->get_text() == "renamed during overview");

	harness.unmount();
}

} // namespace TestEditorBoardSwitcher
