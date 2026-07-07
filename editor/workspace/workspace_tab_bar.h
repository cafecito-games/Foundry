/**************************************************************************/
/*  workspace_tab_bar.h                                                   */
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

#include "scene/gui/tab_bar.h"

class WorkspacePane;

// Tab strip owned by a WorkspacePane. Panes share one rearrange group so a tab
// drag started in one pane's strip is accepted by another pane's strip. A stock
// TabBar would then run its visual-only cross-bar move (_move_tab_from), which
// mutates the destination TabBar's items while bypassing WorkspacePane::tabs,
// EditorData, collapse state and persistence. This subclass intercepts that
// cross-pane drop and routes it through the workspace model instead, landing the
// tab at the hovered insertion index. Intra-pane reorders and non-tab payloads
// keep the stock TabBar behavior.
class WorkspaceTabBar : public TabBar {
	FOUNDRY_CLASS(WorkspaceTabBar, TabBar);

	WorkspacePane *pane = nullptr;

	// Insertion index for a cross-pane drop at p_point, matching TabBar's own
	// cross-bar hover math so a strip drop lands where a stock move would have.
	int _cross_pane_insert_index(const Point2 &p_point) const;

protected:
	void drop_data(const Point2 &p_point, const Variant &p_data) override;

public:
	// Shared rearrange group for every workspace pane's tab strip. Distinct from
	// EditorSceneTabs' group (100) so the two tab surfaces never accept each other.
	static constexpr int WORKSPACE_TABS_REARRANGE_GROUP = 101;

	void set_pane(WorkspacePane *p_pane) { pane = p_pane; }
	WorkspacePane *get_pane() const { return pane; }
};
