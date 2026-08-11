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
#include "editor/gui/editor_board_actions_menu.h"
#include "editor/gui/editor_board_switcher.h"
#include "editor/themes/editor_theme_manager.h"

#include "core/io/config_file.h"
#include "core/object/message_queue.h"
#include "core/string/translation_server.h"

#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/control.h"
#include "scene/gui/line_edit.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"

namespace TestEditorBoardSwitcher {

struct BoardMovedCounter {
	int count = 0;
};

static BoardMovedCounter board_moved_counter;

static void record_board_moved(int, int) {
	board_moved_counter.count++;
}

struct BoardRequestedRecorder {
	Vector<int> indices;
};

static BoardRequestedRecorder board_requested_recorder;

static void record_board_requested(int p_index) {
	board_requested_recorder.indices.push_back(p_index);
}

struct BoardSwitcherHarness {
	Control *host = nullptr;
	EditorData editor_data;
	EditorSelection *selection = nullptr;
	EditorBoardStrip *strip = nullptr;
	EditorBoardSwitcher *switcher = nullptr;
	Ref<EditorTheme> theme;

	~BoardSwitcherHarness() {
		unmount();
	}

	void mount(bool p_with_editor_theme = false, bool p_container_host = false) {
		unmount();
		if (p_container_host) {
			host = memnew(HBoxContainer);
		} else {
			host = memnew(Control);
		}
		host->set_size(Size2(1400, 48));
		if (p_with_editor_theme) {
			theme = EditorThemeManager::generate_theme();
			host->set_theme(theme);
		}
		SceneTree::get_singleton()->get_root()->add_child(host);
		selection = memnew(EditorSelection);
		strip = EditorBoardStrip::create(selection, &editor_data);
		host->add_child(strip);
		switcher = memnew(EditorBoardSwitcher);
		host->add_child(switcher);
		switcher->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		switcher->setup(strip);
		pump();
	}

	void unmount() {
		if (switcher && switcher->is_renaming()) {
			if (LineEdit *edit = find_rename_edit()) {
				edit->emit_signal(SceneStringName(text_submitted), edit->get_text());
			}
		}
		if (EditorBoardActionsMenu *menu = switcher ? switcher->get_actions_menu() : nullptr) {
			menu->hide();
		}
		if (host) {
			MessageQueue::get_singleton()->flush();
			if (host->get_parent()) {
				host->get_parent()->remove_child(host);
			}
			memdelete(host);
		}
		host = nullptr;
		strip = nullptr;
		switcher = nullptr;
		if (selection) {
			memdelete(selection);
		}
		selection = nullptr;
		theme.unref();
	}

	void pump(double p_delta = 0.016) {
		SceneTree::get_singleton()->process(p_delta);
		MessageQueue::get_singleton()->flush();
	}

	void resize_host(const Size2 &p_size) {
		host->set_size(p_size);
		pump();
	}

	// The responsive rail plus the owned, non-layout actions menu.
	int chrome_child_count() const {
		return 2;
	}

	Button *board_button(int p_index) const {
		return switcher->get_board_button(p_index);
	}

	Button *menu_button() const {
		return switcher->get_menu_button();
	}

	void right_click(Button *p_button) const {
		Ref<InputEventMouseButton> event;
		event.instantiate();
		event->set_button_index(MouseButton::RIGHT);
		event->set_pressed(true);
		p_button->emit_signal(SceneStringName(gui_input), event);
	}

	void activate_menu_item(EditorBoardActionsMenu::ItemID p_id) const {
		EditorBoardActionsMenu *menu = switcher->get_actions_menu();
		REQUIRE(menu != nullptr);
		const int idx = menu->get_item_index(p_id);
		REQUIRE(idx >= 0);
		menu->activate_item(idx);
	}

	LineEdit *find_rename_edit() const {
		for (int i = 0; i < switcher->get_child_count(); i++) {
			Node *child = switcher->get_child(i);
			if (LineEdit *edit = Object::cast_to<LineEdit>(child)) {
				return edit;
			}
			for (int j = 0; j < child->get_child_count(); j++) {
				if (LineEdit *edit = Object::cast_to<LineEdit>(child->get_child(j))) {
					return edit;
				}
			}
		}
		return nullptr;
	}

	// Finds a board button by its visible label rather than its position, so callers can
	// identify a board after a reorder without relying on the index it used to occupy.
	Button *board_button_with_text(const String &p_text) const {
		for (int i = 0; i < strip->get_board_count(); i++) {
			Button *button = board_button(i);
			if (button && button->get_text() == p_text) {
				return button;
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

TEST_CASE("[Editor][BoardSwitcher] Rail compacts without losing active board") {
	BoardSwitcherHarness harness;
	harness.mount(true);

	harness.strip->add_board("Materials");
	harness.strip->add_board("Lighting");
	harness.strip->add_board("Animation");
	harness.strip->set_active_board(2);
	harness.resize_host(Size2(280, 48));

	CHECK(harness.strip->get_active_index() == 2);
	for (int i = 0; i < harness.strip->get_board_count(); i++) {
		Button *button = harness.board_button(i);
		REQUIRE(button != nullptr);
		CHECK(button->is_visible() == (i == 2));
	}

	harness.resize_host(Size2(1400, 48));
	CHECK(harness.strip->get_active_index() == 2);
	for (int i = 0; i < harness.strip->get_board_count(); i++) {
		Button *button = harness.board_button(i);
		REQUIRE(button != nullptr);
		CHECK(button->is_visible());
	}

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Rail responds to sibling layout changes without a parent resize") {
	BoardSwitcherHarness harness;
	harness.mount(true, true);
	harness.strip->add_board("Materials");
	harness.strip->add_board("Lighting");
	harness.strip->add_board("Animation");
	harness.strip->set_active_board(2);

	Control *side_control = memnew(Control);
	side_control->set_custom_minimum_size(Size2(100, 48));
	harness.host->add_child(side_control);
	harness.host->move_child(side_control, harness.switcher->get_index());
	harness.pump();

	const Size2 fixed_host_size = harness.host->get_size();
	for (int i = 0; i < harness.strip->get_board_count(); i++) {
		Button *button = harness.board_button(i);
		REQUIRE(button != nullptr);
		CHECK(button->is_visible());
	}

	side_control->set_custom_minimum_size(Size2(600, 48));
	harness.pump();
	CHECK(harness.host->get_size() == fixed_host_size);
	CHECK(harness.strip->get_active_index() == 2);
	for (int i = 0; i < harness.strip->get_board_count(); i++) {
		Button *button = harness.board_button(i);
		REQUIRE(button != nullptr);
		CHECK(button->is_visible() == (i == 2));
	}

	side_control->set_custom_minimum_size(Size2(100, 48));
	harness.pump();
	CHECK(harness.host->get_size() == fixed_host_size);
	CHECK(harness.strip->get_active_index() == 2);
	for (int i = 0; i < harness.strip->get_board_count(); i++) {
		Button *button = harness.board_button(i);
		REQUIRE(button != nullptr);
		CHECK(button->is_visible());
	}

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Every segment emits a board request") {
	BoardSwitcherHarness harness;
	harness.mount();
	harness.strip->add_board("Second");

	board_requested_recorder.indices.clear();
	harness.switcher->connect(SNAME("board_requested"), callable_mp_static(&record_board_requested));
	harness.board_button(0)->emit_signal(SceneStringName(pressed));
	harness.board_button(1)->emit_signal(SceneStringName(pressed));

	REQUIRE(board_requested_recorder.indices.size() == 2);
	if (board_requested_recorder.indices.size() == 2) {
		CHECK(board_requested_recorder.indices[0] == 0);
		CHECK(board_requested_recorder.indices[1] == 1);
	}

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Compact menu lists and activates every board") {
	BoardSwitcherHarness harness;
	harness.mount(true);
	harness.strip->add_board("Materials");
	harness.strip->add_board("Lighting");
	harness.strip->add_board("Animation");
	harness.resize_host(Size2(280, 48));

	EditorBoardActionsMenu *menu = harness.switcher->get_actions_menu();
	REQUIRE(menu != nullptr);
	harness.menu_button()->emit_signal(SceneStringName(pressed));
	for (int i = 0; i < harness.strip->get_board_count(); i++) {
		CHECK(menu->get_item_index(EditorBoardActionsMenu::ITEM_BOARD_BASE + i) >= 0);
	}

	for (int i = 0; i < harness.strip->get_board_count(); i++) {
		// Reopen before each activation: selecting a board closes the popup and the switcher
		// may rebuild its segments, while the owned menu instance itself stays stable.
		harness.menu_button()->emit_signal(SceneStringName(pressed));
		harness.activate_menu_item(EditorBoardActionsMenu::ItemID(EditorBoardActionsMenu::ITEM_BOARD_BASE + i));
		harness.pump();
		CHECK(harness.strip->get_active_index() == i);
		for (int j = 0; j < harness.strip->get_board_count(); j++) {
			Button *button = harness.board_button(j);
			REQUIRE(button != nullptr);
			CHECK(button->is_visible() == (j == i));
		}
	}

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Rebuilds one button per board and marks the active one") {
	BoardSwitcherHarness harness;
	harness.mount();

	// One board button plus the dedicated menu button and the actions menu.
	REQUIRE(harness.switcher->get_child_count() == harness.chrome_child_count());
	Button *first = harness.board_button(0);
	REQUIRE(first != nullptr);
	if (!first) {
		harness.unmount();
		return;
	}
	CHECK(first->get_text() == harness.strip->get_board(0)->get_title());
	CHECK(first->is_pressed());

	harness.strip->add_board();
	REQUIRE(harness.switcher->get_child_count() == harness.chrome_child_count());
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

TEST_CASE("[Editor][BoardSwitcher] User-authored titles stay literal under an active locale") {
	const StringName domain_name = "board.title.literal.test";
	const String authored_title = "Lighting Notes";
	const String localized_collision = "NOTES TRADUITES";
	Ref<TranslationDomain> domain = TranslationServer::get_singleton()->get_or_add_domain(domain_name);
	Ref<Translation> translation;
	translation.instantiate();
	translation->set_locale("fr");
	translation->add_message(authored_title, localized_collision);
	domain->add_translation(translation);
	domain->set_locale_override("fr");
	REQUIRE(domain->translate(authored_title, StringName()) == localized_collision);

	BoardSwitcherHarness harness;
	harness.mount();
	harness.host->set_translation_domain(domain_name);
	harness.strip->add_board(authored_title);
	harness.pump();

	Button *rail_button = harness.board_button(1);
	REQUIRE(rail_button != nullptr);
	CHECK(rail_button->get_text() == authored_title);
	CHECK(rail_button->get_auto_translate_mode() == Node::AUTO_TRANSLATE_MODE_DISABLED);
	CHECK(rail_button->get_tooltip_auto_translate_mode() == Node::AUTO_TRANSLATE_MODE_DISABLED);
	CHECK(rail_button->get_accessibility_name().is_empty());
	for (const String &warning : rail_button->get_accessibility_configuration_warnings()) {
		CHECK_FALSE(warning.contains("Accessibility Name must not be empty"));
	}

	EditorBoardActionsMenu *menu = harness.switcher->get_actions_menu();
	REQUIRE(menu != nullptr);
	harness.switcher->popup_active_board_menu(Point2(), true);
	const int board_item = menu->get_item_index(EditorBoardActionsMenu::ITEM_BOARD_BASE + 1);
	REQUIRE(board_item >= 0);
	CHECK(menu->get_item_text(board_item) == authored_title);
	CHECK(menu->get_item_xl_text(board_item) == authored_title);
	menu->hide();

	harness.strip->set_overview(true);
	harness.pump();
	Control *overlay = harness.strip->get_caption_overlay();
	REQUIRE(overlay != nullptr);
	Button *caption = Object::cast_to<Button>(overlay->get_child(1));
	REQUIRE(caption != nullptr);
	CHECK(caption->get_text() == authored_title);
	CHECK(caption->get_auto_translate_mode() == Node::AUTO_TRANSLATE_MODE_DISABLED);
	CHECK(caption->get_tooltip_auto_translate_mode() == Node::AUTO_TRANSLATE_MODE_DISABLED);
	CHECK(caption->get_accessibility_name().is_empty());
	for (const String &warning : caption->get_accessibility_configuration_warnings()) {
		CHECK_FALSE(warning.contains("Accessibility Name must not be empty"));
	}

	harness.unmount();
	TranslationServer::get_singleton()->remove_domain(domain_name);
}

TEST_CASE("[Editor][BoardSwitcher] The board menu appends and activates a new board") {
	BoardSwitcherHarness harness;
	harness.mount();

	const int board_count_before = harness.strip->get_board_count();
	harness.switcher->popup_active_board_menu(Point2());
	harness.activate_menu_item(EditorBoardActionsMenu::ITEM_NEW_BOARD);
	harness.pump();

	CHECK(harness.strip->get_board_count() == board_count_before + 1);
	CHECK(harness.strip->get_active_index() == board_count_before);

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] The board menu enters overview") {
	BoardSwitcherHarness harness;
	harness.mount();

	CHECK_FALSE(harness.strip->is_overview_active());
	harness.switcher->popup_active_board_menu(Point2());
	harness.activate_menu_item(EditorBoardActionsMenu::ITEM_OVERVIEW);
	harness.pump();

	CHECK(harness.strip->is_overview_active());

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

TEST_CASE("[Editor][BoardSwitcher] Compact transition refreshes a pending renamed segment") {
	BoardSwitcherHarness harness;
	harness.mount(true);
	harness.strip->add_board("B");
	harness.strip->add_board("C");

	Button *button = harness.board_button(1);
	REQUIRE(button != nullptr);
	const float old_minimum_width = button->get_custom_minimum_size().x;
	harness.double_click(button);
	LineEdit *rename_edit = harness.find_rename_edit();
	REQUIRE(rename_edit != nullptr);
	const String new_title = "A much longer renamed board";
	rename_edit->set_text(new_title);

	harness.resize_host(Size2(120, 48));

	CHECK_FALSE(harness.switcher->is_renaming());
	CHECK(harness.strip->get_board(1)->get_title() == new_title);
	Button *renamed_button = harness.board_button(1);
	REQUIRE(renamed_button != nullptr);
	CHECK(renamed_button->get_text() == new_title);
	CHECK(renamed_button->get_tooltip_text() == new_title);
	CHECK(renamed_button->get_accessibility_name().is_empty());
	CHECK(renamed_button->get_custom_minimum_size().x > old_minimum_width);
	CHECK(harness.board_button(0)->is_visible());
	CHECK_FALSE(renamed_button->is_visible());
	CHECK_FALSE(harness.board_button(2)->is_visible());

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

	REQUIRE(harness.switcher->get_child_count() == harness.chrome_child_count());
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
	REQUIRE(harness.switcher->get_child_count() == harness.chrome_child_count());

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
	REQUIRE(harness.switcher->get_child_count() == harness.chrome_child_count());
	harness.strip->set_active_board(1);
	REQUIRE(harness.strip->get_active_index() == 1);

	// Closing the active board fires active_board_changed (the strip activates a neighbor
	// first) and then board_removed, each triggering its own switcher rebuild; what matters
	// is that the buttons are left consistent once both have landed.
	CHECK(harness.strip->close_board(1));

	CHECK(harness.switcher->get_child_count() == harness.chrome_child_count());
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

TEST_CASE("[Editor][BoardSwitcher] A reorder rebuilds the switcher's entry order without add/remove/restore") {
	BoardSwitcherHarness harness;
	harness.mount();

	EditorBoard *board_a = harness.strip->get_board(0);
	REQUIRE(board_a != nullptr);
	if (!board_a) {
		harness.unmount();
		return;
	}
	board_a->set_title("A");
	harness.strip->add_board();
	EditorBoard *board_b = harness.strip->get_board(1);
	REQUIRE(board_b != nullptr);
	if (!board_b) {
		harness.unmount();
		return;
	}
	board_b->set_title("B");
	harness.strip->add_board();
	EditorBoard *board_c = harness.strip->get_board(2);
	REQUIRE(board_c != nullptr);
	if (!board_c) {
		harness.unmount();
		return;
	}
	board_c->set_title("C");
	REQUIRE(harness.strip->get_board_count() == 3);

	// Drag C (index 2) to the first position: the strip's order becomes C, A, B.
	harness.strip->move_board(2, 0);
	EditorBoard *reordered_first = harness.strip->get_board(0);
	EditorBoard *reordered_second = harness.strip->get_board(1);
	EditorBoard *reordered_third = harness.strip->get_board(2);
	REQUIRE(reordered_first != nullptr);
	REQUIRE(reordered_second != nullptr);
	REQUIRE(reordered_third != nullptr);
	if (!reordered_first || !reordered_second || !reordered_third) {
		harness.unmount();
		return;
	}
	REQUIRE(reordered_first->get_title() == "C");
	REQUIRE(reordered_second->get_title() == "A");
	REQUIRE(reordered_third->get_title() == "B");

	for (int i = 0; i < harness.strip->get_board_count(); i++) {
		Button *button = harness.board_button(i);
		REQUIRE(button != nullptr);
		if (!button) {
			harness.unmount();
			return;
		}
		CHECK(button->get_text() == harness.strip->get_board(i)->get_title());
	}

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Activating the Nth entry after a reorder activates the board at index N") {
	BoardSwitcherHarness harness;
	harness.mount();

	EditorBoard *board_a = harness.strip->get_board(0);
	REQUIRE(board_a != nullptr);
	if (!board_a) {
		harness.unmount();
		return;
	}
	board_a->set_title("A");
	harness.strip->add_board();
	EditorBoard *board_b = harness.strip->get_board(1);
	REQUIRE(board_b != nullptr);
	if (!board_b) {
		harness.unmount();
		return;
	}
	board_b->set_title("B");
	harness.strip->add_board();
	EditorBoard *board_c = harness.strip->get_board(2);
	REQUIRE(board_c != nullptr);
	if (!board_c) {
		harness.unmount();
		return;
	}
	board_c->set_title("C");
	REQUIRE(harness.strip->get_board_count() == 3);

	// Drag C (index 2) to the first position: the strip's order becomes C, A, B.
	harness.strip->move_board(2, 0);
	EditorBoard *reordered_first = harness.strip->get_board(0);
	EditorBoard *reordered_second = harness.strip->get_board(1);
	EditorBoard *reordered_third = harness.strip->get_board(2);
	REQUIRE(reordered_first != nullptr);
	REQUIRE(reordered_second != nullptr);
	REQUIRE(reordered_third != nullptr);
	if (!reordered_first || !reordered_second || !reordered_third) {
		harness.unmount();
		return;
	}
	REQUIRE(reordered_first->get_title() == "C");
	REQUIRE(reordered_second->get_title() == "A");
	REQUIRE(reordered_third->get_title() == "B");

	// Each switcher button's pressed signal is bound to its build-time index
	// (EditorBoardSwitcher::_rebuild()), so pressing whichever button currently sits at a
	// given position always activates that position's board -- that would hold trivially
	// even if the switcher never refreshed its labels after a reorder. Identify the button
	// by its visible text instead: board "A" now lives at strip index 1, but if the
	// switcher failed to rebuild after board_moved, its buttons would still read the
	// pre-reorder labels (A, B, C at positions 0, 1, 2), so the button labeled "A" would
	// still sit at position 0 and pressing it would activate the board actually at index 0
	// ("C"), not "A". That mismatch is what this test catches.
	Button *entry = harness.board_button_with_text("A");
	REQUIRE(entry != nullptr);
	if (!entry) {
		harness.unmount();
		return;
	}
	entry->emit_signal(SceneStringName(pressed));

	EditorBoard *active_board = harness.strip->get_board(harness.strip->get_active_index());
	REQUIRE(active_board != nullptr);
	if (!active_board) {
		harness.unmount();
		return;
	}
	CHECK(active_board == board_a);
	CHECK(active_board->get_title() == "A");

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] A reorder triggers exactly one rebuild, matching a single board_moved emission") {
	BoardSwitcherHarness harness;
	harness.mount();

	harness.strip->add_board();
	harness.strip->add_board();
	REQUIRE(harness.strip->get_board_count() == 3);

	// The switcher rebuilds by connecting its handler directly to board_moved, so a single
	// emission of that signal must drive exactly one rebuild. A probe connected the same way
	// pins the emission count board_moved actually produces for one reorder; if the switcher
	// ever connected _rebuild() more than once (or board_moved fired more than once per
	// move_board() call), this count would betray it.
	board_moved_counter.count = 0;
	Callable counter_callable = callable_mp_static(&record_board_moved);
	harness.strip->connect(SNAME("board_moved"), counter_callable);

	harness.strip->move_board(2, 0);

	CHECK(board_moved_counter.count == 1);

	EditorBoard *first_board = harness.strip->get_board(0);
	Button *first_button = harness.board_button(0);
	REQUIRE(first_board != nullptr);
	REQUIRE(first_button != nullptr);
	if (first_board && first_button) {
		CHECK(first_board->get_title() == first_button->get_text());
	}

	harness.strip->disconnect(SNAME("board_moved"), counter_callable);
	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Pressing the active board opens no menu and stays pressed") {
	BoardSwitcherHarness harness;
	harness.mount();

	Button *active = harness.board_button(0);
	REQUIRE(active != nullptr);
	if (!active) {
		harness.unmount();
		return;
	}
	REQUIRE(active->is_pressed());

	active->emit_signal(SceneStringName(pressed));

	EditorBoardActionsMenu *menu = harness.switcher->get_actions_menu();
	REQUIRE(menu != nullptr);
	CHECK_FALSE(menu->is_visible());
	CHECK(active->is_pressed());
	CHECK(harness.strip->get_active_index() == 0);

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Pressing the dedicated board menu opens it immediately") {
	BoardSwitcherHarness harness;
	harness.mount();
	harness.strip->add_board("B");

	Button *menu_button = harness.menu_button();
	REQUIRE(menu_button != nullptr);
	if (!menu_button) {
		harness.unmount();
		return;
	}

	EditorBoardActionsMenu *menu = harness.switcher->get_actions_menu();
	REQUIRE(menu != nullptr);
	CHECK_FALSE(menu->is_visible());

	menu_button->emit_signal(SceneStringName(pressed));

	CHECK(menu->is_visible());
	CHECK(menu->get_target_board_id() == harness.strip->get_active_board()->get_instance_id());
	CHECK(menu->get_item_index(EditorBoardActionsMenu::ITEM_BOARD_BASE) == -1);
	CHECK(menu->get_item_index(EditorBoardActionsMenu::ITEM_NEW_BOARD) >= 0);
	CHECK(menu->get_item_index(EditorBoardActionsMenu::ITEM_OVERVIEW) >= 0);
	CHECK(menu->get_item_index(EditorBoardActionsMenu::ITEM_RENAME) >= 0);
	CHECK(menu->get_item_index(EditorBoardActionsMenu::ITEM_CLOSE) >= 0);

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Pressing an inactive board switches boards and opens no menu") {
	BoardSwitcherHarness harness;
	harness.mount();

	harness.strip->add_board();
	Button *inactive = harness.board_button(1);
	REQUIRE(inactive != nullptr);
	if (!inactive) {
		harness.unmount();
		return;
	}

	EditorBoardActionsMenu *menu = harness.switcher->get_actions_menu();
	REQUIRE(menu != nullptr);
	CHECK_FALSE(menu->is_visible());

	inactive->emit_signal(SceneStringName(pressed));

	CHECK(harness.strip->get_active_index() == 1);
	CHECK_FALSE(menu->is_visible());

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Pressing a board while overview is active exits overview") {
	BoardSwitcherHarness harness;
	harness.mount();

	harness.strip->set_overview(true);
	REQUIRE(harness.strip->is_overview_active());
	Button *active = harness.board_button(0);
	REQUIRE(active != nullptr);
	if (!active) {
		harness.unmount();
		return;
	}

	board_requested_recorder.indices.clear();
	harness.switcher->connect(SNAME("board_requested"), callable_mp_static(&record_board_requested));
	// BaseButton toggles a pressed toggle button before emitting pressed. Reproduce that
	// ordering explicitly instead of bypassing it with a direct signal emission.
	active->set_pressed_no_signal(false);
	active->emit_signal(SceneStringName(pressed));

	CHECK_FALSE(harness.strip->is_overview_active());
	CHECK(harness.strip->get_active_index() == 0);
	CHECK(active->is_pressed());
	REQUIRE(board_requested_recorder.indices.size() == 1);
	if (board_requested_recorder.indices.size() == 1) {
		CHECK(board_requested_recorder.indices[0] == 0);
	}
	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Right-clicking any board button opens the menu for that board without switching") {
	BoardSwitcherHarness harness;
	harness.mount();

	harness.strip->add_board();
	REQUIRE(harness.strip->get_active_index() == 0);

	Button *inactive = harness.board_button(1);
	REQUIRE(inactive != nullptr);
	if (!inactive) {
		harness.unmount();
		return;
	}

	harness.right_click(inactive);

	EditorBoardActionsMenu *menu = harness.switcher->get_actions_menu();
	REQUIRE(menu != nullptr);
	CHECK(menu->is_visible());
	CHECK(harness.strip->get_active_index() == 0);
	CHECK(menu->get_target_board_id() == harness.strip->get_board(1)->get_instance_id());

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] With one board, Close Board and Close Other Boards are disabled") {
	BoardSwitcherHarness harness;
	harness.mount();

	Button *active = harness.board_button(0);
	REQUIRE(active != nullptr);
	if (!active) {
		harness.unmount();
		return;
	}
	harness.switcher->popup_active_board_menu(Point2());

	EditorBoardActionsMenu *menu = harness.switcher->get_actions_menu();
	REQUIRE(menu != nullptr);
	CHECK(menu->is_item_disabled(menu->get_item_index(EditorBoardActionsMenu::ITEM_CLOSE)));
	CHECK(menu->is_item_disabled(menu->get_item_index(EditorBoardActionsMenu::ITEM_CLOSE_OTHERS)));

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Move Left/Right disable at the ends and enable in the middle") {
	BoardSwitcherHarness harness;
	harness.mount();

	harness.strip->add_board();
	harness.strip->add_board();
	REQUIRE(harness.strip->get_board_count() == 3);

	EditorBoardActionsMenu *menu = harness.switcher->get_actions_menu();
	REQUIRE(menu != nullptr);

	menu->popup_for_board(0, Point2());
	CHECK(menu->is_item_disabled(menu->get_item_index(EditorBoardActionsMenu::ITEM_MOVE_LEFT)));
	CHECK_FALSE(menu->is_item_disabled(menu->get_item_index(EditorBoardActionsMenu::ITEM_MOVE_RIGHT)));

	menu->popup_for_board(1, Point2());
	CHECK_FALSE(menu->is_item_disabled(menu->get_item_index(EditorBoardActionsMenu::ITEM_MOVE_LEFT)));
	CHECK_FALSE(menu->is_item_disabled(menu->get_item_index(EditorBoardActionsMenu::ITEM_MOVE_RIGHT)));

	menu->popup_for_board(2, Point2());
	CHECK_FALSE(menu->is_item_disabled(menu->get_item_index(EditorBoardActionsMenu::ITEM_MOVE_LEFT)));
	CHECK(menu->is_item_disabled(menu->get_item_index(EditorBoardActionsMenu::ITEM_MOVE_RIGHT)));

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Activating Move Right emits one board_moved and keeps the same board on screen") {
	BoardSwitcherHarness harness;
	harness.mount();

	EditorBoard *board_a = harness.strip->get_board(0);
	REQUIRE(board_a != nullptr);
	if (!board_a) {
		harness.unmount();
		return;
	}
	board_a->set_title("A");
	harness.strip->add_board("B");
	REQUIRE(harness.strip->get_active_index() == 0);

	board_moved_counter.count = 0;
	Callable counter_callable = callable_mp_static(&record_board_moved);
	harness.strip->connect(SNAME("board_moved"), counter_callable);

	EditorBoardActionsMenu *menu = harness.switcher->get_actions_menu();
	REQUIRE(menu != nullptr);
	menu->popup_for_board(0, Point2());
	harness.activate_menu_item(EditorBoardActionsMenu::ITEM_MOVE_RIGHT);
	harness.pump();

	CHECK(board_moved_counter.count == 1);
	CHECK(harness.strip->get_board(1) == board_a);
	CHECK(harness.strip->get_active_board() == board_a);

	harness.strip->disconnect(SNAME("board_moved"), counter_callable);
	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] A deferred move follows its board identity after an interleaved reorder") {
	BoardSwitcherHarness harness;
	harness.mount();

	EditorBoard *board_a = harness.strip->get_board(0);
	EditorBoard *board_b = harness.strip->add_board("B");
	EditorBoard *board_c = harness.strip->add_board("C");
	REQUIRE(board_a != nullptr);
	REQUIRE(board_b != nullptr);
	REQUIRE(board_c != nullptr);

	EditorBoardActionsMenu *menu = harness.switcher->get_actions_menu();
	REQUIRE(menu != nullptr);
	menu->popup_for_board(0, Point2());
	harness.activate_menu_item(EditorBoardActionsMenu::ITEM_MOVE_RIGHT);

	// A is now in the middle before the queued command runs. The command must resolve A
	// again and move it right from its current position, rather than moving stale index 0.
	harness.strip->move_board(0, 1);
	harness.pump();

	CHECK(harness.strip->get_board(0) == board_b);
	CHECK(harness.strip->get_board(1) == board_c);
	CHECK(harness.strip->get_board(2) == board_a);
	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Activating Rename Board starts an inline rename with the title selected") {
	BoardSwitcherHarness harness;
	harness.mount();

	const String original = harness.strip->get_board(0)->get_title();
	EditorBoardActionsMenu *menu = harness.switcher->get_actions_menu();
	REQUIRE(menu != nullptr);
	menu->popup_for_board(0, Point2());
	harness.activate_menu_item(EditorBoardActionsMenu::ITEM_RENAME);

	CHECK(harness.switcher->is_renaming());
	LineEdit *rename_edit = harness.find_rename_edit();
	REQUIRE(rename_edit != nullptr);
	if (!rename_edit) {
		harness.unmount();
		return;
	}
	CHECK(rename_edit->get_text() == original);

	rename_edit->set_text("from menu");
	rename_edit->emit_signal(SceneStringName(text_submitted), String("from menu"));
	CHECK(harness.strip->get_board(0)->get_title() == "from menu");

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Renaming a hidden compact segment preserves its desired width") {
	BoardSwitcherHarness harness;
	harness.mount(true);
	harness.strip->add_board("Hidden board with a useful title");
	Button *hidden_button = harness.board_button(1);
	REQUIRE(hidden_button != nullptr);
	hidden_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	harness.pump();

	const float segment_minimum_width = hidden_button->get_combined_minimum_size().x;
	CHECK(segment_minimum_width > 1.0f);
	CHECK(hidden_button->get_size().x > segment_minimum_width);

	harness.resize_host(Size2(280, 48));
	CHECK_FALSE(hidden_button->is_visible());
	CHECK(hidden_button->get_size().x != segment_minimum_width);

	EditorBoardActionsMenu *menu = harness.switcher->get_actions_menu();
	REQUIRE(menu != nullptr);
	menu->popup_for_board(1, Point2());
	harness.activate_menu_item(EditorBoardActionsMenu::ITEM_RENAME);

	LineEdit *rename_edit = harness.find_rename_edit();
	REQUIRE(rename_edit != nullptr);
	CHECK(rename_edit->get_custom_minimum_size().x == segment_minimum_width);
	CHECK(rename_edit->get_custom_minimum_size().y > 1.0f);

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Activating Close Board removes a scene-less board and rebuilds") {
	BoardSwitcherHarness harness;
	harness.mount();

	harness.strip->add_board("doomed");
	harness.strip->set_active_board(1);
	REQUIRE(harness.strip->get_board_count() == 2);

	EditorBoardActionsMenu *menu = harness.switcher->get_actions_menu();
	REQUIRE(menu != nullptr);
	menu->popup_for_board(1, Point2());
	harness.activate_menu_item(EditorBoardActionsMenu::ITEM_CLOSE);
	harness.pump();

	CHECK(harness.strip->get_board_count() == 1);
	CHECK(harness.switcher->get_child_count() == harness.chrome_child_count());
	Button *remaining = harness.board_button(0);
	REQUIRE(remaining != nullptr);
	if (!remaining) {
		harness.unmount();
		return;
	}
	CHECK(remaining->is_pressed());

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] A deferred close keeps its original target after a repopup") {
	BoardSwitcherHarness harness;
	harness.mount();

	EditorBoard *board_b = harness.strip->add_board("B");
	REQUIRE(board_b != nullptr);
	EditorBoardActionsMenu *menu = harness.switcher->get_actions_menu();
	REQUIRE(menu != nullptr);
	menu->popup_for_board(0, Point2());
	harness.activate_menu_item(EditorBoardActionsMenu::ITEM_CLOSE);
	menu->popup_for_board(1, Point2());
	harness.pump();

	CHECK(harness.strip->get_board_count() == 1);
	CHECK(harness.strip->get_board(0) == board_b);
	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] A menu whose target board was freed activates nothing") {
	BoardSwitcherHarness harness;
	harness.mount();

	harness.strip->add_board("ephemeral");
	EditorBoard *ephemeral = harness.strip->get_board(1);
	REQUIRE(ephemeral != nullptr);
	if (!ephemeral) {
		harness.unmount();
		return;
	}
	const ObjectID ephemeral_id = ephemeral->get_instance_id();

	EditorBoardActionsMenu *menu = harness.switcher->get_actions_menu();
	REQUIRE(menu != nullptr);
	menu->popup_for_board(1, Point2());
	CHECK(menu->get_target_board_id() == ephemeral_id);

	CHECK(harness.strip->close_board(1));
	CHECK(harness.strip->resolve_board_index(ephemeral_id) == -1);

	// The menu still holds the freed board's id. Activating Close must not touch the survivor.
	harness.activate_menu_item(EditorBoardActionsMenu::ITEM_CLOSE);
	harness.pump();
	CHECK(harness.strip->get_board_count() == 1);

	harness.activate_menu_item(EditorBoardActionsMenu::ITEM_RENAME);
	CHECK_FALSE(harness.switcher->is_renaming());

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Board list activation follows board identity across a reorder") {
	BoardSwitcherHarness harness;
	harness.mount();

	EditorBoard *board_b = harness.strip->add_board("B");
	REQUIRE(board_b != nullptr);
	EditorBoardActionsMenu *menu = harness.switcher->get_actions_menu();
	REQUIRE(menu != nullptr);
	menu->popup_for_board(0, Point2(), true);

	const int board_b_item = menu->get_item_index(EditorBoardActionsMenu::ITEM_BOARD_BASE + 1);
	REQUIRE(board_b_item >= 0);
	menu->activate_item(board_b_item);
	harness.strip->move_board(1, 0);
	harness.pump();

	CHECK(harness.strip->get_active_board() == board_b);
	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] The actions menu instance survives board rebuilds") {
	BoardSwitcherHarness harness;
	harness.mount();

	EditorBoardActionsMenu *menu = harness.switcher->get_actions_menu();
	REQUIRE(menu != nullptr);

	harness.strip->add_board();
	CHECK(harness.switcher->get_actions_menu() == menu);

	CHECK(harness.strip->close_board(1));
	CHECK(harness.switcher->get_actions_menu() == menu);

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Close Other Boards from the menu drains every other board") {
	BoardSwitcherHarness harness;
	harness.mount();

	EditorBoard *keep = harness.strip->get_board(0);
	REQUIRE(keep != nullptr);
	if (!keep) {
		harness.unmount();
		return;
	}
	keep->set_title("keep");
	harness.strip->add_board("A");
	harness.strip->add_board("B");

	EditorBoardActionsMenu *menu = harness.switcher->get_actions_menu();
	REQUIRE(menu != nullptr);
	menu->popup_for_board(0, Point2());
	harness.activate_menu_item(EditorBoardActionsMenu::ITEM_CLOSE_OTHERS);
	harness.pump();
	harness.pump();
	harness.pump();

	CHECK(harness.strip->get_board_count() == 1);
	CHECK(harness.strip->get_active_board() == keep);

	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Close Other Boards keeps its original snapshot after a repopup") {
	BoardSwitcherHarness harness;
	harness.mount();

	const ObjectID board_a_id = harness.strip->get_board(0)->get_instance_id();
	EditorBoard *board_b = harness.strip->add_board("B");
	EditorBoard *board_c = harness.strip->add_board("C");
	REQUIRE(board_b != nullptr);
	REQUIRE(board_c != nullptr);
	const ObjectID board_b_id = board_b->get_instance_id();
	const ObjectID board_c_id = board_c->get_instance_id();

	EditorBoardActionsMenu *menu = harness.switcher->get_actions_menu();
	REQUIRE(menu != nullptr);
	menu->popup_for_board(0, Point2());
	harness.activate_menu_item(EditorBoardActionsMenu::ITEM_CLOSE_OTHERS);
	menu->popup_for_board(1, Point2());
	for (int i = 0; i < 4; i++) {
		harness.pump();
	}

	CHECK(harness.strip->get_board_count() == 1);
	CHECK(harness.strip->resolve_board_index(board_a_id) == 0);
	CHECK(harness.strip->resolve_board_index(board_b_id) == -1);
	CHECK(harness.strip->resolve_board_index(board_c_id) == -1);
	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] Close Other Boards no-ops when its original keeper is gone") {
	BoardSwitcherHarness harness;
	harness.mount();

	EditorBoard *board_b = harness.strip->add_board("B");
	EditorBoard *board_c = harness.strip->add_board("C");
	REQUIRE(board_b != nullptr);
	REQUIRE(board_c != nullptr);
	const ObjectID board_b_id = board_b->get_instance_id();
	const ObjectID board_c_id = board_c->get_instance_id();

	EditorBoardActionsMenu *menu = harness.switcher->get_actions_menu();
	REQUIRE(menu != nullptr);
	menu->popup_for_board(0, Point2());
	harness.activate_menu_item(EditorBoardActionsMenu::ITEM_CLOSE_OTHERS);
	CHECK(harness.strip->close_board(0));
	for (int i = 0; i < 4; i++) {
		harness.pump();
	}

	CHECK(harness.strip->get_board_count() == 2);
	CHECK(harness.strip->resolve_board_index(board_b_id) >= 0);
	CHECK(harness.strip->resolve_board_index(board_c_id) >= 0);
	harness.unmount();
}

TEST_CASE("[Editor][BoardSwitcher] A deferred collection action keeps its original strip") {
	BoardSwitcherHarness harness;
	harness.mount();

	EditorData other_editor_data;
	EditorSelection *other_selection = memnew(EditorSelection);
	EditorBoardStrip *other_strip = EditorBoardStrip::create(other_selection, &other_editor_data);
	harness.host->add_child(other_strip);

	const int original_count = harness.strip->get_board_count();
	const int other_count = other_strip->get_board_count();
	EditorBoardActionsMenu *menu = harness.switcher->get_actions_menu();
	REQUIRE(menu != nullptr);
	menu->popup_for_board(0, Point2());
	harness.activate_menu_item(EditorBoardActionsMenu::ITEM_NEW_BOARD);
	harness.switcher->setup(other_strip);
	harness.pump();

	CHECK(harness.strip->get_board_count() == original_count + 1);
	CHECK(harness.strip->get_active_index() == original_count);
	CHECK(other_strip->get_board_count() == other_count);

	harness.unmount();
	memdelete(other_selection);
}

} // namespace TestEditorBoardSwitcher
