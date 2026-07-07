/**************************************************************************/
/*  workspace_tab_bar.cpp                                                 */
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

#include "editor/workspace/workspace_tab_bar.h"

#include "editor/workspace/workspace_pane.h"

int WorkspaceTabBar::_cross_pane_insert_index(const Point2 &p_point) const {
	const int count = get_tab_count();
	int hover_now = get_closest_tab_idx_to_point(p_point);
	if (hover_now != -1) {
		// Drop to the left or right of the hovered tab depending on which half of
		// the tab the pointer is over (mirrored under RTL).
		const Rect2 tab_rect = get_tab_rect(hover_now);
		if (is_layout_rtl() ^ (p_point.x > tab_rect.position.x + tab_rect.size.width / 2)) {
			hover_now += 1;
		}
	} else {
		// No tab under the pointer: an empty strip inserts at 0, otherwise the
		// pointer falls before the first tab (index 0) or past the last (append).
		hover_now = (count == 0 || (is_layout_rtl() ^ (p_point.x < get_tab_rect(0).position.x))) ? 0 : count;
	}
	return hover_now;
}

void WorkspaceTabBar::drop_data(const Point2 &p_point, const Variant &p_data) {
	// Non-tab payloads and drops without an owning pane keep the stock behavior.
	Dictionary d = p_data;
	if (!pane || d.get("type", "").operator String() != "tab") {
		TabBar::drop_data(p_point, p_data);
		return;
	}

	// A same-strip drop is an ordinary intra-pane reorder handled by the base
	// TabBar (it emits active_tab_rearranged, which the pane routes to move_tab).
	const NodePath from_path = d.get("from_path", NodePath());
	if (from_path.is_empty() || from_path == get_path()) {
		TabBar::drop_data(p_point, p_data);
		return;
	}

	// A cross-strip drop from another workspace pane in the shared group is routed
	// through the workspace model so the move updates tabs/EditorData/collapse/
	// persistence instead of TabBar's visual-only cross-bar move. Anything else
	// (a foreign TabBar sharing the group, a missing source pane) falls back.
	WorkspaceTabBar *from_bar = Object::cast_to<WorkspaceTabBar>(get_node_or_null(from_path));
	if (!from_bar || !from_bar->get_pane() || from_bar->get_tabs_rearrange_group() != get_tabs_rearrange_group()) {
		TabBar::drop_data(p_point, p_data);
		return;
	}

	const int source_tab_index = d.get("tab_index", -1);
	if (source_tab_index < 0) {
		return;
	}

	const int insert_index = _cross_pane_insert_index(p_point);
	pane->handle_cross_pane_strip_drop(from_bar->get_pane(), source_tab_index, insert_index);
}
