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

namespace {

// Axis-aligned bounds of a strip-space rect after the labelled-mode -PI/2
// content transform (p -> (y, -x) + origin). Computed analytically so the
// measured geometry is not subject to corner-transform float drift.
Rect2 _strip_rect_to_local(const Point2 &p_origin, const Rect2 &p_strip_rect) {
	return Rect2(
			Point2(p_origin.x + p_strip_rect.position.y, p_origin.y - p_strip_rect.position.x - p_strip_rect.size.x),
			Size2(p_strip_rect.size.y, p_strip_rect.size.x));
}

} // namespace

void EditorSideRailButton::_update_theme_cache() {
	theme_cache.font = get_theme_font(SceneStringName(font));
	theme_cache.font_size = get_theme_font_size(SceneStringName(font_size));
	theme_cache.font_color = get_theme_color(SceneStringName(font_color));
	theme_cache.icon_label_separation = get_theme_constant(SNAME("h_separation"));
}

Size2 EditorSideRailButton::_compute_minimum_size(bool p_with_label) const {
	const Ref<StyleBox> stylebox = _get_current_stylebox();
	const Size2 stylebox_min_size = stylebox.is_valid() ? stylebox->get_minimum_size() : Size2();
	const Size2 icon_size = rail_icon.is_valid() ? rail_icon->get_size() : Size2();

	if (!p_with_label || rail_label.is_empty() || theme_cache.font.is_null()) {
		return icon_size + stylebox_min_size;
	}

	const real_t font_height = theme_cache.font->get_height(theme_cache.font_size);
	const real_t text_width = theme_cache.font->get_string_size(rail_label, HORIZONTAL_ALIGNMENT_LEFT, -1, theme_cache.font_size).x;
	const real_t separation = icon_size != Size2() ? theme_cache.icon_label_separation : 0;

	// Horizontal strip [icon][sep][label], then quarter-turned: the strip's
	// short axis becomes button width and its long axis becomes button height.
	// Ceil fractional font advances so the stylebox content rect always covers
	// the composed draw bounds after pixel snapping.
	const real_t strip_width = Math::ceil(icon_size.width + separation + text_width);
	const real_t strip_height = Math::ceil(MAX(icon_size.height, font_height));
	return Size2(strip_height, strip_width) + stylebox_min_size;
}

EditorSideRailButton::ComposedGeometry EditorSideRailButton::get_composed_geometry() const {
	ComposedGeometry geometry;

	const Ref<StyleBox> stylebox = _get_current_stylebox();
	const real_t margin_left = stylebox.is_valid() ? stylebox->get_margin(SIDE_LEFT) : 0;
	const real_t margin_top = stylebox.is_valid() ? stylebox->get_margin(SIDE_TOP) : 0;
	const real_t margin_right = stylebox.is_valid() ? stylebox->get_margin(SIDE_RIGHT) : 0;
	const real_t margin_bottom = stylebox.is_valid() ? stylebox->get_margin(SIDE_BOTTOM) : 0;
	const Size2 size = get_size();
	geometry.content_rect = Rect2(
			Point2(margin_left, margin_top),
			Size2(MAX(0.0, size.width - margin_left - margin_right), MAX(0.0, size.height - margin_top - margin_bottom)));
	geometry.icon_label_separation = theme_cache.icon_label_separation;
	geometry.has_icon = rail_icon.is_valid();
	geometry.has_label = label_visible && !rail_label.is_empty() && theme_cache.font.is_valid();

	if (!geometry.has_label) {
		// Icon-only (and empty) mode: upright, unrotated icon centered in the
		// stylebox content rect. No label space is reserved.
		if (geometry.has_icon) {
			const Size2 icon_size = rail_icon->get_size();
			geometry.icon_rect = Rect2(
					Point2(
							geometry.content_rect.position.x + Math::floor((geometry.content_rect.size.x - icon_size.width) / 2.0),
							geometry.content_rect.position.y + Math::floor((geometry.content_rect.size.y - icon_size.height) / 2.0)),
					icon_size);
		}
		geometry.content_transform = Transform2D();
		return geometry;
	}

	const Size2 icon_size = geometry.has_icon ? rail_icon->get_size() : Size2();
	const real_t font_height = theme_cache.font->get_height(theme_cache.font_size);
	const real_t text_width = theme_cache.font->get_string_size(rail_label, HORIZONTAL_ALIGNMENT_LEFT, -1, theme_cache.font_size).x;
	const real_t separation = geometry.has_icon ? theme_cache.icon_label_separation : 0;
	const real_t strip_width = Math::ceil(icon_size.width + separation + text_width);
	const real_t strip_height = Math::ceil(MAX(icon_size.height, font_height));

	// Strip space lays the toggle out as [label][sep][icon]. The -PI/2
	// transform maps strip +x onto local -y, so the composed unit reads
	// bottom-to-top with the icon at the top of the button — matching the
	// previous labelled-rail visual hierarchy while keeping icon and label on
	// the same quarter-turn.
	//
	// Expressed entirely in this control's local space so it does not shift
	// with the button's position in its parent (NOTIFICATION_DRAW already runs
	// in local space; composing get_transform() would double-apply it).
	// Floor the centering offsets so rounded inner draw positions cannot spill
	// outside the stylebox content rect.
	const Point2 strip_origin(
			geometry.content_rect.position.x + Math::floor((geometry.content_rect.size.x - strip_height) / 2.0),
			geometry.content_rect.position.y + Math::floor((geometry.content_rect.size.y - strip_width) / 2.0) + strip_width);
	geometry.content_transform = Transform2D(-Math::PI / 2.0, strip_origin);

	const Rect2 label_strip_rect(
			Point2(0, Math::floor((strip_height - font_height) / 2.0)),
			Size2(text_width, font_height));
	geometry.label_rect = _strip_rect_to_local(strip_origin, label_strip_rect);

	if (geometry.has_icon) {
		const Rect2 icon_strip_rect(
				Point2(text_width + separation, Math::floor((strip_height - icon_size.height) / 2.0)),
				icon_size);
		geometry.icon_rect = _strip_rect_to_local(strip_origin, icon_strip_rect);
	}

	return geometry;
}

void EditorSideRailButton::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			_update_theme_cache();
			update_minimum_size();
			queue_redraw();
		} break;

		case NOTIFICATION_DRAW: {
			const ComposedGeometry geometry = get_composed_geometry();

			if (!geometry.has_label) {
				if (geometry.has_icon) {
					draw_texture(rail_icon, geometry.icon_rect.position);
				}
				break;
			}

			// NOTIFICATION_DRAW is already emitted in the control's local
			// space (the engine applies get_transform() when compositing
			// this item), so the content transform is expressed purely in
			// local space and reset to identity when done.
			draw_set_transform_matrix(geometry.content_transform);

			{
				const Size2 icon_size = geometry.has_icon ? rail_icon->get_size() : Size2();
				const real_t separation = geometry.has_icon ? theme_cache.icon_label_separation : 0;
				const real_t font_height = theme_cache.font->get_height(theme_cache.font_size);
				const real_t ascent = theme_cache.font->get_ascent(theme_cache.font_size);
				const real_t strip_height = Math::ceil(MAX(icon_size.height, font_height));
				const Point2 text_pos(0, Math::floor((strip_height - font_height) / 2.0) + ascent);
				draw_string(theme_cache.font, text_pos, rail_label, HORIZONTAL_ALIGNMENT_LEFT, -1, theme_cache.font_size, theme_cache.font_color);

				if (geometry.has_icon) {
					const real_t text_width = theme_cache.font->get_string_size(rail_label, HORIZONTAL_ALIGNMENT_LEFT, -1, theme_cache.font_size).x;
					const Point2 icon_pos(
							text_width + separation,
							Math::floor((strip_height - icon_size.height) / 2.0));
					draw_texture(rail_icon, icon_pos);
				}
			}

			draw_set_transform_matrix(Transform2D());
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
