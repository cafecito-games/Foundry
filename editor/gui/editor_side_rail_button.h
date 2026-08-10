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

// A tile side-rail toggle: icon and label composed as one horizontal strip,
// then drawn with a shared -90° transform so both read bottom-to-top. Drawing
// is fully self-managed (Button's own icon/text layout is not used, since it
// has no vertical mode) so get_minimum_size and _draw agree on the same
// geometry.
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
		int icon_label_separation = 0;
	} theme_cache;

	void _update_theme_cache();
	Size2 _compute_minimum_size(bool p_with_label) const;

protected:
	void _notification(int p_what);

public:
	// Measured layout of the composed toggle in this control's local space.
	// Tests assert rotation, separation, containment, and min-size coverage
	// against these rects rather than against source text.
	struct ComposedGeometry {
		Rect2 content_rect;
		Rect2 icon_rect;
		Rect2 label_rect;
		Transform2D content_transform;
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

	// Exact geometry NOTIFICATION_DRAW uses. Exposed for tests that prove
	// common rotation, separation, containment, and min-size coverage.
	ComposedGeometry get_composed_geometry() const;

	EditorSideRailButton();
};
