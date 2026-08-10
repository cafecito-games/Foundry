/**************************************************************************/
/*  editor_side_rail_strip.h                                              */
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

#include "editor/gui/side_rail_state.h"
#include "scene/gui/panel_container.h"

class Button;
class EditorDock;
class EditorSideRailButton;
class EditorTileDockRegion;
class TabContainer;
class VBoxContainer;

// Vertical transposition of EditorBottomDrawerStrip (editor_bottom_drawer_strip.h),
// mirroring one side of a tile's dock region. A pure mirror: it owns no dock
// state of its own, exactly like the strip it is modelled on. The per-side
// collapse state lives in EditorTileDockRegion; the rail only drives its
// transitions and reflects the result.
class EditorSideRailStrip : public PanelContainer {
	FOUNDRY_CLASS(EditorSideRailStrip, PanelContainer);

public:
	using Side = SideRailSide;

private:
	Side side;
	EditorTileDockRegion *dock_region = nullptr;
	// Only the right rail mirrors a TabContainer and therefore has a
	// current-tab concept; the left rail mirrors an ordered EditorDock list
	// with no such notion, so this stays null there.
	TabContainer *active_source_tabs = nullptr;

	// PanelContainer stacks every direct child to the same full rect, so the
	// strip has exactly one: main_vbox holds the toggle stack and the expand
	// button in a real vertical order.
	VBoxContainer *main_vbox = nullptr;
	VBoxContainer *toggles_vbox = nullptr;
	Button *close_button = nullptr;
	Button *expand_button = nullptr;

	LocalVector<EditorSideRailButton *> toggle_buttons;
	LocalVector<EditorDock *> toggle_docks;

	SideRailLabelMode label_mode = SideRailLabelMode::LABELLED;

	void _rebuild_toggles();
	void _refresh_toggle(int p_toggle_index);
	void _dock_style_changed(EditorDock *p_dock);
	void _toggle_pressed(EditorDock *p_dock);
	void _close_pressed();
	void _expand_pressed();
	void _update_active_states();
	void _apply_label_mode();

protected:
	void _notification(int p_what);

public:
	void rebuild_toggles();

	// Toggle set matches _get_source_docks_in_order() filtered to is_enabled().
	// Exposed for tests; production code never needs the raw dock list.
	const LocalVector<EditorDock *> &get_toggle_docks() const { return toggle_docks; }
	const LocalVector<EditorSideRailButton *> &get_toggle_buttons() const { return toggle_buttons; }

	SideRailLabelMode get_label_mode() const { return label_mode; }

	EditorSideRailStrip(Side p_side, EditorTileDockRegion *p_dock_region);
};
