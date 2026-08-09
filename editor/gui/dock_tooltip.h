/**************************************************************************/
/*  dock_tooltip.h                                                        */
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

#include "core/input/shortcut.h"
#include "editor/docks/editor_dock.h"

// Appends the dock's shortcut clause ("<Shortcut Name> (<keys>)") to
// p_prefix, exactly as before, when the dock has a shortcut with a bound
// key. Docks that never register a global shortcut have no other tooltip
// source: every per-tile dock is constructed with `global = false`
// (`ScenePaneTile`), which gates the `set_dock_shortcut` call, so a dock
// with no shortcut — or a shortcut with no bound key, e.g. Import's — would
// otherwise surface an empty tooltip. When p_prefix is still empty after the
// shortcut check, the dock's display title is used instead, so the fallback
// never fires when a caller already composed non-empty prefix text (for
// example the icon-only tab style, which already shows the title).
_FORCE_INLINE_ String dock_tooltip_with_shortcut_fallback(const EditorDock *p_dock, const String &p_prefix = String()) {
	String tooltip = p_prefix;
	const Ref<Shortcut> shortcut = p_dock->get_dock_shortcut();
	if (shortcut.is_valid() && shortcut->has_valid_event()) {
		tooltip += (tooltip.is_empty() ? "" : "\n") + TTR(shortcut->get_name()) + " (" + shortcut->get_as_text() + ")";
	} else if (tooltip.is_empty()) {
		tooltip = TTR(p_dock->get_display_title());
	}
	return tooltip;
}
