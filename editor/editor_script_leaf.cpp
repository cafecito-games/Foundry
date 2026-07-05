/**************************************************************************/
/*  editor_script_leaf.cpp                                               */
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

#include "editor_script_leaf.h"

#include "core/io/config_file.h"
#include "editor/editor_string_names.h"
#include "scene/gui/label.h"

void ScriptLeaf::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			if (title_label) {
				title_label->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("font_color"), EditorStringName(Editor)));
			}
		} break;
	}
}

void ScriptLeaf::set_tab_title(const String &p_title) {
	tab_title = p_title;
	if (title_label) {
		title_label->set_text(tab_title);
	}
}

StringName ScriptLeaf::get_content_type() const {
	return StringName("script");
}

Control *ScriptLeaf::get_root_control() const {
	return const_cast<ScriptLeaf *>(this);
}

String ScriptLeaf::get_tab_title() const {
	return tab_title;
}

Ref<Texture2D> ScriptLeaf::get_tab_icon() const {
	if (has_theme_icon(SNAME("Script"), EditorStringName(EditorIcons))) {
		return get_theme_icon(SNAME("Script"), EditorStringName(EditorIcons));
	}
	return Ref<Texture2D>();
}

EditorSceneContext *ScriptLeaf::get_scene_context() const {
	return nullptr;
}

void ScriptLeaf::save_layout(const Ref<ConfigFile> &p_config, const String &p_section) const {
	ERR_FAIL_COND(p_config.is_null());
	p_config->set_value(p_section, "tab_title", tab_title);
}

void ScriptLeaf::load_layout(const Ref<ConfigFile> &p_config, const String &p_section) {
	ERR_FAIL_COND(p_config.is_null());
	set_tab_title(p_config->get_value(p_section, "tab_title", tab_title));
}

ScriptLeaf::ScriptLeaf() {
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_h_size_flags(Control::SIZE_EXPAND_FILL);

	title_label = memnew(Label);
	title_label->set_text(tab_title);
	title_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	title_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	title_label->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	title_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	add_child(title_label);
}
