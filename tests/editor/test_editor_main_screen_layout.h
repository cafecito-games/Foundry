/**************************************************************************/
/*  test_editor_main_screen_layout.h                                      */
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

#include "core/io/config_file.h"
#include "editor/editor_main_screen.h"
#include "editor/plugins/editor_plugin.h"

#include "scene/gui/box_container.h"
#include "scene/gui/button.h"

#include "tests/test_macros.h"

namespace TestEditorMainScreenLayout {

// Minimal main-screen plugin: only its name matters for layout persistence.
class NamedMainScreenPlugin : public EditorPlugin {
	FOUNDRY_CLASS(NamedMainScreenPlugin, EditorPlugin);

	String plugin_name;

public:
	virtual String get_plugin_name() const override { return plugin_name; }
	virtual bool has_main_screen() const override { return true; }

	void set_plugin_name(const String &p_name) { plugin_name = p_name; }
};

// Press a main-screen button directly (no signal) so no EditorNode-dependent
// select() path runs; the layout save only reads the button's pressed state.
static void press_button(HBoxContainer *p_button_hb, const String &p_name) {
	for (int i = 0; i < p_button_hb->get_child_count(); i++) {
		Button *button = Object::cast_to<Button>(p_button_hb->get_child(i));
		if (button && button->get_text() == p_name) {
			button->set_pressed_no_signal(true);
		} else if (button) {
			button->set_pressed_no_signal(false);
		}
	}
}

static NamedMainScreenPlugin *make_plugin(const String &p_name) {
	NamedMainScreenPlugin *plugin = memnew(NamedMainScreenPlugin);
	plugin->set_plugin_name(p_name);
	return plugin;
}

TEST_CASE("[EditorMainScreen][Editor] active screen persists by name, not index") {
	EditorMainScreen *main_screen = memnew(EditorMainScreen);
	HBoxContainer *button_hb = memnew(HBoxContainer);
	main_screen->set_button_container(button_hb);

	NamedMainScreenPlugin *plugin_2d = make_plugin("2D");
	NamedMainScreenPlugin *plugin_3d = make_plugin("3D");
	NamedMainScreenPlugin *plugin_game = make_plugin("Game");
	main_screen->add_main_plugin(plugin_2d);
	main_screen->add_main_plugin(plugin_3d);
	main_screen->add_main_plugin(plugin_game);

	press_button(button_hb, "3D");

	Ref<ConfigFile> config;
	config.instantiate();
	main_screen->save_layout_to_config(config, "EditorNode");

	CHECK(String(config->get_value("EditorNode", "selected_main_editor", String())) == "3D");
	// The brittle raw-index key is no longer written.
	CHECK_FALSE(config->has_section_key("EditorNode", "selected_main_editor_idx"));

	memdelete(main_screen);
	memdelete(button_hb);
	memdelete(plugin_2d);
	memdelete(plugin_3d);
	memdelete(plugin_game);
}

TEST_CASE("[EditorMainScreen][Editor] active global screen persists by name") {
	// A global screen (Game) hides the scene-mode workspace, so it is the active
	// surface and must be persisted by its own name rather than a script sentinel.
	EditorMainScreen *main_screen = memnew(EditorMainScreen);
	HBoxContainer *button_hb = memnew(HBoxContainer);
	main_screen->set_button_container(button_hb);

	NamedMainScreenPlugin *plugin_2d = make_plugin("2D");
	NamedMainScreenPlugin *plugin_game = make_plugin("Game");
	main_screen->add_main_plugin(plugin_2d);
	main_screen->add_main_plugin(plugin_game);

	press_button(button_hb, "Game");

	Ref<ConfigFile> config;
	config.instantiate();
	main_screen->save_layout_to_config(config, "EditorNode");
	CHECK(String(config->get_value("EditorNode", "selected_main_editor", String())) == "Game");

	memdelete(main_screen);
	memdelete(button_hb);
	memdelete(plugin_2d);
	memdelete(plugin_game);
}

TEST_CASE("[EditorMainScreen][Editor] name resolution survives plugin reordering") {
	EditorMainScreen *main_screen = memnew(EditorMainScreen);
	HBoxContainer *button_hb = memnew(HBoxContainer);
	main_screen->set_button_container(button_hb);

	NamedMainScreenPlugin *plugin_a = make_plugin("Alpha");
	NamedMainScreenPlugin *plugin_b = make_plugin("Beta");
	NamedMainScreenPlugin *plugin_c = make_plugin("Gamma");
	main_screen->add_main_plugin(plugin_a);
	main_screen->add_main_plugin(plugin_b);
	main_screen->add_main_plugin(plugin_c);

	CHECK(main_screen->get_button_index_by_name("Beta") == 1);

	// Persist "Beta" as the active screen.
	press_button(button_hb, "Beta");
	Ref<ConfigFile> config;
	config.instantiate();
	main_screen->save_layout_to_config(config, "EditorNode");
	CHECK(String(config->get_value("EditorNode", "selected_main_editor", String())) == "Beta");

	// Reorder: remove Beta and re-add it last, shifting Gamma into old index 1.
	// Move the pressed state off Beta first so removal doesn't run the
	// select() fallback (which needs a live EditorNode singleton).
	press_button(button_hb, "Alpha");
	main_screen->remove_main_plugin(plugin_b);
	main_screen->add_main_plugin(plugin_b);

	CHECK(main_screen->get_button_index_by_name("Gamma") == 1);
	CHECK(main_screen->get_button_index_by_name("Beta") == 2);

	// The saved name still resolves to Beta at its new index, not to whatever
	// now occupies the old raw index 1.
	const String saved = config->get_value("EditorNode", "selected_main_editor", String());
	CHECK(main_screen->get_button_index_by_name(saved) == 2);

	memdelete(main_screen);
	memdelete(button_hb);
	memdelete(plugin_a);
	memdelete(plugin_b);
	memdelete(plugin_c);
}

TEST_CASE("[EditorMainScreen][Editor] save no longer scrubs the retired key inline") {
	// The retired "selected_main_editor_idx" cleanup now lives in the
	// EditorLayoutStore version 0 -> 1 migration (see test_editor_layout_store.h),
	// not in save_layout_to_config. Saving must only write the name-based key and
	// must not touch a stray retired key, so unversioned callers do not silently
	// mutate keys the migration layer owns.
	EditorMainScreen *main_screen = memnew(EditorMainScreen);
	HBoxContainer *button_hb = memnew(HBoxContainer);
	main_screen->set_button_container(button_hb);

	NamedMainScreenPlugin *plugin_2d = make_plugin("2D");
	NamedMainScreenPlugin *plugin_3d = make_plugin("3D");
	main_screen->add_main_plugin(plugin_2d);
	main_screen->add_main_plugin(plugin_3d);

	press_button(button_hb, "3D");

	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("EditorNode", "selected_main_editor_idx", 1);

	main_screen->save_layout_to_config(config, "EditorNode");

	CHECK(String(config->get_value("EditorNode", "selected_main_editor", String())) == "3D");
	// Not the save path's job anymore: the retired key is left for the migration.
	CHECK(config->has_section_key("EditorNode", "selected_main_editor_idx"));

	memdelete(main_screen);
	memdelete(button_hb);
	memdelete(plugin_2d);
	memdelete(plugin_3d);
}

TEST_CASE("[EditorMainScreen][Editor] legacy and unknown layouts fall back gracefully") {
	EditorMainScreen *main_screen = memnew(EditorMainScreen);
	HBoxContainer *button_hb = memnew(HBoxContainer);
	main_screen->set_button_container(button_hb);

	NamedMainScreenPlugin *plugin_2d = make_plugin("2D");
	NamedMainScreenPlugin *plugin_3d = make_plugin("3D");
	main_screen->add_main_plugin(plugin_2d);
	main_screen->add_main_plugin(plugin_3d);

	// A pre-name layout only carries the raw-index key; the new loader ignores it
	// (name-based resolution finds nothing) and no selection is forced.
	Ref<ConfigFile> legacy;
	legacy.instantiate();
	legacy->set_value("EditorNode", "selected_main_editor_idx", 1);
	main_screen->load_layout_from_config(legacy, "EditorNode");
	CHECK(main_screen->get_selected_index() == -1);

	// An unknown plugin name (e.g. one removed since the layout was saved) does
	// not resolve to any current button.
	CHECK(main_screen->get_button_index_by_name("RemovedPlugin") == -1);

	// Missing key: nothing selected, no crash.
	Ref<ConfigFile> empty;
	empty.instantiate();
	main_screen->load_layout_from_config(empty, "EditorNode");
	CHECK(main_screen->get_selected_index() == -1);

	memdelete(main_screen);
	memdelete(button_hb);
	memdelete(plugin_2d);
	memdelete(plugin_3d);
}

} // namespace TestEditorMainScreenLayout
