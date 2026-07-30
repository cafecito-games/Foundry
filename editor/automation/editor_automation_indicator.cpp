/**************************************************************************/
/*  editor_automation_indicator.cpp                                       */
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

#include "editor_automation_indicator.h"

#include "editor/editor_string_names.h"
#include "editor/themes/editor_scale.h"

#include "scene/gui/box_container.h"
#include "scene/gui/label.h"
#include "scene/gui/texture_rect.h"

EditorAutomationIndicator::EditorAutomationIndicator() {
	set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	set_visible(false);
	set_accessibility_name(TTRC("Automation Active"));

	HBoxContainer *row = memnew(HBoxContainer);
	row->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	add_child(row);

	icon = memnew(TextureRect);
	icon->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
	icon->set_custom_minimum_size(Size2(16, 16) * EDSCALE);
	icon->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	row->add_child(icon);

	status_label = memnew(Label);
	status_label->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	status_label->set_text(TTR("Automation Active"));
	row->add_child(status_label);
}

void EditorAutomationIndicator::_apply_theme() {
	if (!is_inside_tree() || !is_ready()) {
		return;
	}

	if (has_theme_icon(SNAME("Tools"), EditorStringName(EditorIcons))) {
		icon->set_texture(get_editor_theme_icon(SNAME("Tools")));
	}

	if (has_theme_font(SNAME("main"), EditorStringName(EditorFonts))) {
		const Ref<Font> font = get_theme_font(SNAME("main"), EditorStringName(EditorFonts));
		const int font_size = get_theme_font_size(SNAME("main_size"), EditorStringName(EditorFonts));
		status_label->add_theme_font_override(SceneStringName(font), font);
		status_label->add_theme_font_size_override(SceneStringName(font_size), font_size);
	}

	if (has_theme_color(SNAME("accent_color"), EditorStringName(Editor))) {
		const Color accent = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
		status_label->add_theme_color_override(SceneStringName(font_color), accent.lightened(0.15));
	}

	if (has_theme_stylebox(SNAME("LaunchPadMovieMode"), EditorStringName(EditorStyles))) {
		add_theme_style_override(SceneStringName(panel), get_theme_stylebox(SNAME("LaunchPadMovieMode"), EditorStringName(EditorStyles)));
	}
}

void EditorAutomationIndicator::set_active(const String &p_transport, int p_port, const String &p_endpoint) {
	const String summary = vformat(TTR("Automation Active (%s :%d)"), p_transport, p_port);
	status_label->set_text(summary);
	set_tooltip_text(vformat(TTR("Editor automation is active.\nTransport: %s\nEndpoint: %s"), p_transport, p_endpoint));
	active = true;
	set_visible(true);
}

void EditorAutomationIndicator::clear_active() {
	active = false;
	set_visible(false);
	set_tooltip_text(String());
}

void EditorAutomationIndicator::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (active) {
				_apply_theme();
			}
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			if (active) {
				_apply_theme();
			}
		} break;
	}
}
