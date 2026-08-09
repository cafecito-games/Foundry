/**************************************************************************/
/*  dock_split_layout.h                                                   */
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

#include "core/templates/vector.h"

// Scale-free, node-free helpers that compute the `dock_hsplit_N` layout keys to
// save or load for the editor's main horizontal split. They exist because the
// save loop previously zeroed the stored width of any hidden dock column, and
// because the load loop shifted positional offsets when a key was absent; both
// are fixed by centralizing the visible/hidden bookkeeping here. See #1995.
//
// `EDSCALE` conversion stays at the call sites, so these helpers operate on raw
// integer offsets and have no dependency on `EditorNode`, `EDSCALE`, or any node
// type. They are therefore unit-testable from a headless doctest with no editor
// singletons alive.
//
// The offset type is `Vector<int32_t>`, the exact typedef underlying
// `PackedInt32Array`, so callers pass `SplitContainer::get_split_offsets()` /
// `set_split_offsets()` values directly without conversion.

// One `dock_hsplit_N` key to write. A column that is absent from the returned
// list must keep whatever value the layout config already holds for it -- it is
// never zeroed.
struct DockHSplitWrite {
	int split_index = 0;
	int offset = 0;
};

// A `dock_hsplit_N` value read back from a layout config.
struct DockHSplitStored {
	bool present = false;
	int offset = 0;
};

// Save side. `p_split_visible[i]` is `vsplits[i]->is_visible()`; `p_split_offsets`
// is `main_hsplit->get_split_offsets()`. Only visible columns consume an entry,
// in order, because SplitContainer builds its valid-children set -- and therefore
// its offset array -- from `is_visible()`. Hidden columns are skipped (the caller
// writes no key for them), and a short offset array leaves the remaining visible
// columns skipped rather than zeroed.
Vector<DockHSplitWrite> dock_split_layout_writes(
		const Vector<bool> &p_split_visible, const Vector<int32_t> &p_split_offsets);

// Load side. Returns one offset per visible column, in order, so entry k always
// lands on dragger k. A missing key contributes `0` in place rather than
// shortening the array and shifting later columns onto the wrong dragger.
// Reports an error and returns an empty array when `p_stored` and
// `p_split_visible` disagree in size.
Vector<int32_t> dock_split_layout_offsets(
		const Vector<bool> &p_split_visible, const Vector<DockHSplitStored> &p_stored);
