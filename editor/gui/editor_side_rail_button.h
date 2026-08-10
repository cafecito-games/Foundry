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

// A tile side-rail toggle: an unrotated icon above a label rotated 90° so it
// reads bottom-to-top, the vertical analogue of a horizontal tab. Drawing is
// fully self-managed (Button's own icon/text layout is not used, since it has
// no vertical mode) so get_minimum_size and _draw agree on the same geometry.
//
// The icon-only fallback (side_rail_state.h) hides the label without changing
// the icon or losing track of what the label would have measured; callers ask
// for that via get_labelled_minimum_size() independent of the current mode.
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
	Transform2D _get_label_transform(real_t p_cursor_y, real_t p_text_width) const;

protected:
	void _notification(int p_what);

public:
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

	// The exact transform NOTIFICATION_DRAW applies before drawing the rotated
	// label, expressed purely in this control's local space. Exposed for
	// tests, which use it to prove the label transform does not depend on the
	// button's position within its parent (get_transform() must never be
	// composed into it; NOTIFICATION_DRAW already runs in local space).
	Transform2D get_label_transform_for_test(real_t p_cursor_y, real_t p_text_width) const { return _get_label_transform(p_cursor_y, p_text_width); }

	EditorSideRailButton();
};
