/**************************************************************************/
/*  editor_side_rail_button.h                                             */
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

#include "scene/gui/button.h"

// A tile side-rail toggle: label and icon composed as one horizontal strip,
// then drawn with a shared -90° transform so both read bottom-to-top with the
// icon at the top of the button. Drawing is fully self-managed (Button's own
// icon/text layout is not used, since it has no vertical mode) so
// get_minimum_size and _draw agree on the same geometry.
//
// The icon-only fallback (side_rail_state.h) hides the label without changing
// the icon or losing track of what the label would have measured; callers ask
// for that via get_labelled_minimum_size() independent of the current mode.
// Icon-only mode centers an upright, unrotated icon and does not reserve
// label space.
class EditorSideRailButton : public Button {
	FOUNDRY_CLASS(EditorSideRailButton, Button);

	Ref<Texture2D> rail_icon;
	String rail_label;
	bool label_visible = true;

	struct ThemeCache {
		Ref<Font> font;
		int font_size = 0;
		Color font_color;
		Color font_hover_color;
		Color font_pressed_color;
		Color font_hover_pressed_color;
		Color font_focus_color;
		Color font_disabled_color;
		Color icon_normal_color;
		Color icon_hover_color;
		Color icon_pressed_color;
		Color icon_hover_pressed_color;
		Color icon_focus_color;
		Color icon_disabled_color;
		bool has_font_hover_color = false;
		bool has_font_pressed_color = false;
		bool has_font_hover_pressed_color = false;
		bool has_font_focus_color = false;
		bool has_font_disabled_color = false;
		bool has_icon_normal_color = false;
		bool has_icon_hover_color = false;
		bool has_icon_pressed_color = false;
		bool has_icon_hover_pressed_color = false;
		bool has_icon_focus_color = false;
		bool has_icon_disabled_color = false;
		int icon_label_separation = 0;
		bool align_to_largest_stylebox = false;
		real_t style_margin_left = 0;
		real_t style_margin_top = 0;
		real_t style_margin_right = 0;
		real_t style_margin_bottom = 0;
	} theme_cache;

	struct StripMetrics {
		Size2 icon_size;
		real_t text_width = 0;
		real_t font_height = 0;
		real_t ascent = 0;
		real_t separation = 0;
		real_t strip_width = 0;
		real_t strip_height = 0;
		bool has_icon = false;
		bool has_label = false;
	};

	void _update_theme_cache();
	void _get_layout_margins(real_t &r_left, real_t &r_top, real_t &r_right, real_t &r_bottom) const;
	StripMetrics _compute_strip_metrics(bool p_with_label) const;
	Size2 _compute_minimum_size(bool p_with_label) const;
	Color _get_current_font_color() const;
	Color _get_current_icon_color() const;

protected:
	void _notification(int p_what);

public:
	// Single production layout source used by NOTIFICATION_DRAW. Tests assert
	// against these measured rects rather than source text.
	struct ComposedGeometry {
		Rect2 content_rect;
		Rect2 icon_rect;
		Rect2 label_rect;
		Transform2D content_transform;
		Point2 label_strip_baseline;
		Point2 icon_strip_position;
		real_t icon_label_separation = 0;
		bool has_icon = false;
		bool has_label = false;
	};

	void set_rail_icon(const Ref<Texture2D> &p_icon);
	Ref<Texture2D> get_rail_icon() const { return rail_icon; }

	void set_rail_label(const String &p_label);
	String get_rail_label() const { return rail_label; }

	void set_label_visible(bool p_visible);
	bool is_label_visible() const { return label_visible; }

	virtual Size2 get_minimum_size() const override;

	// The size this button would report with the label shown, regardless of
	// is_label_visible(). Used by the rail's overflow fit decision to know
	// whether returning to labelled mode would fit.
	Size2 get_labelled_minimum_size() const;

	ComposedGeometry get_composed_geometry() const;

	EditorSideRailButton();
};
