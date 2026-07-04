/**************************************************************************/
/*  test_editor_main_screen.h                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "editor/editor_main_screen.h"
#include "editor/plugins/editor_plugin.h"

#include "tests/test_macros.h"

class EditorMainScreenTestAccess {
public:
	static void set_selected_plugin(EditorMainScreen *p_main_screen, EditorPlugin *p_plugin) {
		p_main_screen->selected_plugin = p_plugin;
	}

	static bool reselect_if_current(EditorMainScreen *p_main_screen, EditorPlugin *p_plugin) {
		return p_main_screen->_reselect_if_current(p_plugin);
	}
};

namespace TestEditorMainScreen {

class CountingMainScreenPlugin : public EditorPlugin {
	FOUNDRY_CLASS(CountingMainScreenPlugin, EditorPlugin);

public:
	int visible_true_count = 0;
	int visible_false_count = 0;

	virtual String get_plugin_name() const override {
		return "Counting";
	}

	virtual bool has_main_screen() const override {
		return true;
	}

	virtual void make_visible(bool p_visible) override {
		if (p_visible) {
			visible_true_count++;
		} else {
			visible_false_count++;
		}
	}
};

TEST_CASE("[Editor][MainScreen] reselecting current plugin reapplies visibility") {
	EditorMainScreen *main_screen = memnew(EditorMainScreen);
	CountingMainScreenPlugin *plugin = memnew(CountingMainScreenPlugin);
	CountingMainScreenPlugin *other_plugin = memnew(CountingMainScreenPlugin);

	EditorMainScreenTestAccess::set_selected_plugin(main_screen, plugin);

	CHECK(EditorMainScreenTestAccess::reselect_if_current(main_screen, plugin));
	CHECK(plugin->visible_true_count == 1);
	CHECK(plugin->visible_false_count == 0);

	CHECK_FALSE(EditorMainScreenTestAccess::reselect_if_current(main_screen, other_plugin));
	CHECK(plugin->visible_true_count == 1);
	CHECK(other_plugin->visible_true_count == 0);

	memdelete(other_plugin);
	memdelete(plugin);
	memdelete(main_screen);
}

} // namespace TestEditorMainScreen
