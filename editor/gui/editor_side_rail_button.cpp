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
Rect2 strip_rect_to_local(const Point2 &p_origin, const Rect2 &p_strip_rect) {
	return Rect2(
			Point2(p_origin.x + p_strip_rect.position.y, p_origin.y - p_strip_rect.position.x - p_strip_rect.size.x),
			Size2(p_strip_rect.size.y, p_strip_rect.size.x));
}

} // namespace

void EditorSideRailButton::_update_theme_cache() {
	theme_cache.font = get_theme_font(SceneStringName(font));
	theme_cache.font_size = get_theme_font_size(SceneStringName(font_size));
	// Resolve colors through the theme cascade (default theme always defines
	// Button font/icon colors). Match Button's BIND_THEME_ITEM caching rather
	// than branching on has_theme_color — those branches are unreachable while
	// ThemeDB's default theme is installed.
	theme_cache.font_color = get_theme_color(SceneStringName(font_color));
	theme_cache.font_hover_color = get_theme_color(SNAME("font_hover_color"));
	theme_cache.font_pressed_color = get_theme_color(SNAME("font_pressed_color"));
	theme_cache.font_hover_pressed_color = get_theme_color(SNAME("font_hover_pressed_color"));
	theme_cache.font_focus_color = get_theme_color(SNAME("font_focus_color"));
	theme_cache.font_disabled_color = get_theme_color(SNAME("font_disabled_color"));

	theme_cache.icon_normal_color = get_theme_color(SNAME("icon_normal_color"));
	theme_cache.icon_hover_color = get_theme_color(SNAME("icon_hover_color"));
	theme_cache.icon_pressed_color = get_theme_color(SNAME("icon_pressed_color"));
	theme_cache.icon_hover_pressed_color = get_theme_color(SNAME("icon_hover_pressed_color"));
	theme_cache.icon_focus_color = get_theme_color(SNAME("icon_focus_color"));
	theme_cache.icon_disabled_color = get_theme_color(SNAME("icon_disabled_color"));

	theme_cache.icon_label_separation = get_theme_constant(SNAME("h_separation"));
	theme_cache.align_to_largest_stylebox = get_theme_constant(SNAME("align_to_largest_stylebox"));

	// Mirror Button's align_to_largest_stylebox behaviour so active/hovered
	// toggles keep the same content margins as their neighbours.
	// RTL *_mirrored styleboxes are intentionally not consulted: the rail is
	// LTR in production (strip order and -PI/2 rotation assume LTR reading),
	// and editor-theme margins are symmetric.
	theme_cache.style_margin_left = 0;
	theme_cache.style_margin_top = 0;
	theme_cache.style_margin_right = 0;
	theme_cache.style_margin_bottom = 0;
	const StringName style_names[] = {
		SNAME("normal"),
		SNAME("hover"),
		SNAME("pressed"),
		SNAME("hover_pressed"),
		SNAME("disabled"),
	};
	for (const StringName &name : style_names) {
		const Ref<StyleBox> stylebox = get_theme_stylebox(name);
		if (stylebox.is_null()) {
			continue;
		}
		theme_cache.style_margin_left = MAX(theme_cache.style_margin_left, stylebox->get_margin(SIDE_LEFT));
		theme_cache.style_margin_top = MAX(theme_cache.style_margin_top, stylebox->get_margin(SIDE_TOP));
		theme_cache.style_margin_right = MAX(theme_cache.style_margin_right, stylebox->get_margin(SIDE_RIGHT));
		theme_cache.style_margin_bottom = MAX(theme_cache.style_margin_bottom, stylebox->get_margin(SIDE_BOTTOM));
	}
}

void EditorSideRailButton::_get_layout_margins(real_t &r_left, real_t &r_top, real_t &r_right, real_t &r_bottom) const {
	// Ceil fractional theme margins so the content rect is an integer pixel
	// box. That keeps a rounded strip origin inside the content rect instead
	// of spilling past a fractional edge (e.g. margin 3.6 + strip 69).
	if (theme_cache.align_to_largest_stylebox) {
		r_left = Math::ceil(theme_cache.style_margin_left);
		r_top = Math::ceil(theme_cache.style_margin_top);
		r_right = Math::ceil(theme_cache.style_margin_right);
		r_bottom = Math::ceil(theme_cache.style_margin_bottom);
		return;
	}

	const Ref<StyleBox> stylebox = _get_current_stylebox();
	r_left = stylebox.is_valid() ? Math::ceil(stylebox->get_margin(SIDE_LEFT)) : 0;
	r_top = stylebox.is_valid() ? Math::ceil(stylebox->get_margin(SIDE_TOP)) : 0;
	r_right = stylebox.is_valid() ? Math::ceil(stylebox->get_margin(SIDE_RIGHT)) : 0;
	r_bottom = stylebox.is_valid() ? Math::ceil(stylebox->get_margin(SIDE_BOTTOM)) : 0;
}

EditorSideRailButton::StripMetrics EditorSideRailButton::_compute_strip_metrics(bool p_with_label) const {
	StripMetrics metrics;
	metrics.has_icon = rail_icon.is_valid() && rail_icon->get_size() != Size2();
	metrics.icon_size = metrics.has_icon ? rail_icon->get_size() : Size2();
	metrics.has_label = p_with_label && !rail_label.is_empty() && theme_cache.font.is_valid();

	if (metrics.has_label) {
		metrics.font_height = theme_cache.font->get_height(theme_cache.font_size);
		metrics.ascent = theme_cache.font->get_ascent(theme_cache.font_size);
		metrics.text_width = theme_cache.font->get_string_size(rail_label, HORIZONTAL_ALIGNMENT_LEFT, -1, theme_cache.font_size).x;
		metrics.separation = metrics.has_icon ? theme_cache.icon_label_separation : 0;
		// Horizontal strip [label][sep][icon], then quarter-turned: the strip's
		// short axis becomes button width and its long axis becomes button height.
		// Ceil fractional font advances so the stylebox content rect always covers
		// the composed draw bounds after pixel snapping.
		metrics.strip_width = Math::ceil(metrics.text_width + metrics.separation + metrics.icon_size.width);
		metrics.strip_height = Math::ceil(MAX(metrics.icon_size.height, metrics.font_height));
	}

	return metrics;
}

Size2 EditorSideRailButton::_compute_minimum_size(bool p_with_label) const {
	real_t margin_left = 0;
	real_t margin_top = 0;
	real_t margin_right = 0;
	real_t margin_bottom = 0;
	_get_layout_margins(margin_left, margin_top, margin_right, margin_bottom);
	const Size2 stylebox_min_size(margin_left + margin_right, margin_top + margin_bottom);

	const StripMetrics metrics = _compute_strip_metrics(p_with_label);
	if (!metrics.has_label) {
		return metrics.icon_size + stylebox_min_size;
	}

	return Size2(metrics.strip_height, metrics.strip_width) + stylebox_min_size;
}

Color EditorSideRailButton::_get_current_font_color() const {
	switch (get_draw_mode()) {
		case DRAW_HOVER_PRESSED:
			return theme_cache.font_hover_pressed_color;
		case DRAW_PRESSED:
			return theme_cache.font_pressed_color;
		case DRAW_HOVER:
			return theme_cache.font_hover_color;
		case DRAW_DISABLED:
			return theme_cache.font_disabled_color;
		default:
			// Focus colors only take precedence over the normal state, matching Button.
			if (has_focus(true)) {
				return theme_cache.font_focus_color;
			}
			return theme_cache.font_color;
	}
}

Color EditorSideRailButton::_get_current_icon_color() const {
	switch (get_draw_mode()) {
		case DRAW_HOVER_PRESSED:
			return theme_cache.icon_hover_pressed_color;
		case DRAW_PRESSED:
			return theme_cache.icon_pressed_color;
		case DRAW_HOVER:
			return theme_cache.icon_hover_color;
		case DRAW_DISABLED:
			return theme_cache.icon_disabled_color;
		default:
			if (has_focus(true)) {
				return theme_cache.icon_focus_color;
			}
			return theme_cache.icon_normal_color;
	}
}

EditorSideRailButton::ComposedGeometry EditorSideRailButton::get_composed_geometry() const {
	ComposedGeometry geometry;

	real_t margin_left = 0;
	real_t margin_top = 0;
	real_t margin_right = 0;
	real_t margin_bottom = 0;
	_get_layout_margins(margin_left, margin_top, margin_right, margin_bottom);

	const Size2 size = get_size();
	geometry.content_rect = Rect2(
			Point2(margin_left, margin_top),
			Size2(MAX(0.0, size.width - margin_left - margin_right), MAX(0.0, size.height - margin_top - margin_bottom)));
	geometry.icon_label_separation = theme_cache.icon_label_separation;
	geometry.font_color = _get_current_font_color();
	geometry.icon_color = _get_current_icon_color();

	const StripMetrics metrics = _compute_strip_metrics(label_visible);
	geometry.has_icon = metrics.has_icon;
	geometry.has_label = metrics.has_label;

	if (!geometry.has_label) {
		// Icon-only (and empty) mode: upright, unrotated icon centered in the
		// stylebox content rect. No label space is reserved. Clamp centering so
		// an undersized button overflows in one direction only.
		if (geometry.has_icon) {
			const real_t icon_offset_x = MAX(0.0, (geometry.content_rect.size.x - metrics.icon_size.width) / 2.0);
			const real_t icon_offset_y = MAX(0.0, (geometry.content_rect.size.y - metrics.icon_size.height) / 2.0);
			geometry.icon_rect = Rect2(
					Point2(
							Math::round(geometry.content_rect.position.x + icon_offset_x),
							Math::round(geometry.content_rect.position.y + icon_offset_y)),
					metrics.icon_size);
			// Identity content_transform: strip-space positions equal local.
			geometry.icon_strip_position = geometry.icon_rect.position;
		}
		geometry.content_transform = Transform2D();
		return geometry;
	}

	// Strip space lays the toggle out as [label][sep][icon]. The -PI/2
	// transform maps strip +x onto local -y, so the composed unit reads
	// bottom-to-top with the icon at the top of the button. Icon artwork is
	// intentionally quarter-turned with the label (#2039).
	//
	// Expressed entirely in this control's local space so it does not shift
	// with the button's position in its parent (NOTIFICATION_DRAW already runs
	// in local space; composing get_transform() would double-apply it).
	// Round the final origin for crisp rasterization, and clamp centering so
	// undersized buttons overflow in one direction only.
	const real_t offset_x = MAX(0.0, (geometry.content_rect.size.x - metrics.strip_height) / 2.0);
	const real_t offset_y = MAX(0.0, (geometry.content_rect.size.y - metrics.strip_width) / 2.0);
	const Point2 strip_origin(
			Math::round(geometry.content_rect.position.x + offset_x),
			Math::round(geometry.content_rect.position.y + offset_y + metrics.strip_width));
	geometry.content_transform = Transform2D(-Math::PI / 2.0, strip_origin);

	const real_t label_strip_y = Math::floor((metrics.strip_height - metrics.font_height) / 2.0);
	// Round the baseline so strip-y (local x after the quarter-turn) lands on
	// a whole pixel; label_rect stays derived from the floored strip y.
	geometry.label_strip_baseline = Point2(0, Math::round(label_strip_y + metrics.ascent));
	const Rect2 label_strip_rect(Point2(0, label_strip_y), Size2(metrics.text_width, metrics.font_height));
	geometry.label_rect = strip_rect_to_local(strip_origin, label_strip_rect);

	if (geometry.has_icon) {
		// Anchor the icon to the ceiled strip end so its local bounds stay on
		// whole pixels after the shared quarter-turn.
		geometry.icon_strip_position = Point2(
				metrics.strip_width - metrics.icon_size.width,
				Math::floor((metrics.strip_height - metrics.icon_size.height) / 2.0));
		geometry.icon_rect = strip_rect_to_local(strip_origin, Rect2(geometry.icon_strip_position, metrics.icon_size));
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
			if (!geometry.has_icon && !geometry.has_label) {
				break;
			}

			// NOTIFICATION_DRAW is already emitted in the control's local
			// space (the engine applies get_transform() when compositing
			// this item), so the content transform is expressed purely in
			// local space and reset to identity when done. Icon-only mode
			// uses an identity transform so strip positions equal local.
			draw_set_transform_matrix(geometry.content_transform);
			if (geometry.has_label) {
				draw_string(theme_cache.font, geometry.label_strip_baseline, rail_label, HORIZONTAL_ALIGNMENT_LEFT, -1, theme_cache.font_size, geometry.font_color);
			}
			if (geometry.has_icon) {
				draw_texture(rail_icon, geometry.icon_strip_position, geometry.icon_color);
			}
			draw_set_transform_matrix(Transform2D());
		} break;
	}
}

void EditorSideRailButton::_rail_icon_changed() {
	update_minimum_size();
	queue_redraw();
}

void EditorSideRailButton::set_rail_icon(const Ref<Texture2D> &p_icon) {
	if (rail_icon == p_icon) {
		return;
	}
	if (rail_icon.is_valid()) {
		rail_icon->disconnect_changed(callable_mp(this, &EditorSideRailButton::_rail_icon_changed));
	}
	rail_icon = p_icon;
	if (rail_icon.is_valid()) {
		rail_icon->connect_changed(callable_mp(this, &EditorSideRailButton::_rail_icon_changed));
	}
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
