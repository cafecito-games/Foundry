/**************************************************************************/
/*  editor_side_rail_button.cpp                                           */
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

#include "editor_side_rail_button.h"

#include "scene/resources/font.h"
#include "scene/resources/style_box.h"

void EditorSideRailButton::_update_theme_cache() {
	theme_cache.font = get_theme_font(SceneStringName(font));
	theme_cache.font_size = get_theme_font_size(SceneStringName(font_size));
	theme_cache.font_color = get_theme_color(SceneStringName(font_color));
	theme_cache.icon_label_separation = get_theme_constant(SNAME("h_separation"));
}

Size2 EditorSideRailButton::_compute_minimum_size(bool p_with_label) const {
	if (theme_cache.font.is_null()) {
		return Size2();
	}

	const Size2 icon_size = rail_icon.is_valid() ? rail_icon->get_size() : Size2();
	const Ref<StyleBox> stylebox = _get_current_stylebox();
	const Size2 stylebox_min_size = stylebox.is_valid() ? stylebox->get_minimum_size() : Size2();

	if (!p_with_label || rail_label.is_empty()) {
		return Size2(icon_size.width, icon_size.height) + stylebox_min_size;
	}

	const real_t font_height = theme_cache.font->get_height(theme_cache.font_size);
	const real_t text_width = theme_cache.font->get_string_size(rail_label, HORIZONTAL_ALIGNMENT_LEFT, -1, theme_cache.font_size).x;

	const real_t width = MAX(icon_size.width, font_height);
	const real_t height = icon_size.height + theme_cache.icon_label_separation + text_width;
	return Size2(width, height) + stylebox_min_size;
}

Transform2D EditorSideRailButton::_get_label_transform(real_t p_cursor_y, real_t p_text_width) const {
	const Size2 size = get_size();
	const real_t ascent = theme_cache.font->get_ascent(theme_cache.font_size);
	const real_t descent = theme_cache.font->get_descent(theme_cache.font_size);

	// -90° so the local +x (left-to-right glyph advance) maps to local -y,
	// i.e. the label reads bottom-to-top. The origin is the bottom of the
	// label region, offset horizontally so the font's ascent/descent band is
	// centred in the button. Expressed entirely in the button's own local
	// space, independent of get_transform() (see the NOTIFICATION_DRAW
	// comment at the call site), so it does not shift with the button's
	// position in its parent container.
	const Point2 origin(Math::round(size.width / 2.0 + (ascent - descent) / 2.0), Math::round(p_cursor_y + p_text_width));
	return Transform2D(-Math::PI / 2.0, origin);
}

void EditorSideRailButton::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			_update_theme_cache();
			update_minimum_size();
			queue_redraw();
		} break;

		case NOTIFICATION_DRAW: {
			if (theme_cache.font.is_null()) {
				break;
			}

			const Size2 size = get_size();
			const Ref<StyleBox> stylebox = _get_current_stylebox();
			const real_t margin_left = stylebox.is_valid() ? stylebox->get_margin(SIDE_LEFT) : 0;
			const real_t margin_top = stylebox.is_valid() ? stylebox->get_margin(SIDE_TOP) : 0;

			real_t cursor_y = margin_top;
			if (rail_icon.is_valid()) {
				const Point2 icon_pos(Math::round((size.width - rail_icon->get_width()) / 2.0), cursor_y);
				rail_icon->draw(get_canvas_item(), icon_pos);
				cursor_y += rail_icon->get_height();
			}

			if (label_visible && !rail_label.is_empty()) {
				cursor_y += theme_cache.icon_label_separation;

				const real_t text_width = theme_cache.font->get_string_size(rail_label, HORIZONTAL_ALIGNMENT_LEFT, -1, theme_cache.font_size).x;
				const Transform2D text_xform = _get_label_transform(cursor_y, text_width);

				// NOTIFICATION_DRAW is already emitted in the control's local
				// space (the engine applies get_transform() when compositing
				// this item), so composing get_transform() here again would
				// apply the button's own layout position a second time. Every
				// draw_set_transform_matrix() call in this scope is therefore
				// expressed purely in local space, and reset to identity
				// rather than back to get_transform() when done.
				draw_set_transform_matrix(text_xform);
				draw_string(theme_cache.font, Point2(margin_left, 0), rail_label, HORIZONTAL_ALIGNMENT_LEFT, -1, theme_cache.font_size, theme_cache.font_color);
				draw_set_transform_matrix(Transform2D());
			}
		} break;
	}
}

void EditorSideRailButton::set_rail_icon(const Ref<Texture2D> &p_icon) {
	if (rail_icon == p_icon) {
		return;
	}
	rail_icon = p_icon;
	update_minimum_size();
	queue_redraw();
}

void EditorSideRailButton::set_rail_label(const String &p_label) {
	if (rail_label == p_label) {
		return;
	}
	rail_label = p_label;
	update_minimum_size();
	queue_redraw();
}

void EditorSideRailButton::set_label_visible(bool p_visible) {
	if (label_visible == p_visible) {
		return;
	}
	label_visible = p_visible;
	update_minimum_size();
	queue_redraw();
}

Size2 EditorSideRailButton::get_minimum_size() const {
	return _compute_minimum_size(label_visible);
}

Size2 EditorSideRailButton::get_labelled_minimum_size() const {
	return _compute_minimum_size(true);
}

EditorSideRailButton::EditorSideRailButton() {
	set_theme_type_variation("FlatMenuButton");
	set_toggle_mode(true);
	set_focus_mode(Control::FOCUS_ACCESSIBILITY);
}
