/**************************************************************************/
/*  dock_split_layout.cpp                                                 */
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

#include "dock_split_layout.h"

#include "core/error/error_macros.h"

Vector<DockHSplitWrite> dock_split_layout_writes(
		const Vector<bool> &p_split_visible, const Vector<int32_t> &p_split_offsets) {
	Vector<DockHSplitWrite> out;
	int index = 0;
	const int n = p_split_visible.size();
	for (int i = 0; i < n; i++) {
		if (!p_split_visible[i]) {
			continue; // Skip: the caller writes no key for a hidden column.
		}
		if (index >= p_split_offsets.size()) {
			break; // Offsets exhausted: skip remaining visible columns, never zero them.
		}
		DockHSplitWrite write;
		write.split_index = i;
		write.offset = p_split_offsets[index];
		out.push_back(write);
		index++;
	}
	return out;
}

Vector<int32_t> dock_split_layout_offsets(
		const Vector<bool> &p_split_visible, const Vector<DockHSplitStored> &p_stored) {
	ERR_FAIL_COND_V_MSG(p_stored.size() != p_split_visible.size(), Vector<int32_t>(),
			"dock_split_layout_offsets: the stored and visible vectors must be the same size.");

	Vector<int32_t> out;
	const int n = p_split_visible.size();
	for (int i = 0; i < n; i++) {
		if (!p_split_visible[i]) {
			continue;
		}
		out.push_back(p_stored[i].present ? p_stored[i].offset : 0);
	}
	return out;
}
