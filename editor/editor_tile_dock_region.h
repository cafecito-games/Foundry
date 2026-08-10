/**************************************************************************/
/*  editor_tile_dock_region.h                                             */
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

#include "core/io/config_file.h"
#include "core/object/object_id.h"
#include "core/templates/vector.h"
#include "core/variant/callable.h"
#include "editor/gui/side_rail_state.h"

class Control;
class EditorDock;
class HSplitContainer;
class TabContainer;

/**
 * Manages the in-tile dock strip: [left dock | center host | right tab stack].
 * Docks live here instead of the global EditorDockManager slots.
 *
 * Each side is independently either DOCKED (the column as it has always
 * behaved) or RAILED (collapsed to its rail, showing at most one dock in a
 * drawer). The mode is applied purely through control visibility, so tab order
 * and dock identity never change.
 */
class EditorTileDockRegion {
public:
	using Side = SideRailSide;

private:
	struct SideState {
		SideRailMode mode = SideRailMode::DOCKED;
		ObjectID drawer_dock;
		ObjectID last_drawer_dock;
		Callable changed_callback;
	};

	// The two body gaps the tile persists (tile_dock_hsplit_1 and _2), kept by
	// identity so a side that collapses away can be restored to the width it
	// had. SplitContainer's own split_offsets array loses both the entry and
	// the meaning of the surviving entries when a column is hidden.
	enum Gap {
		GAP_LEFT_CENTER,
		GAP_CENTER_RIGHT,
		GAP_MAX,
	};

	HSplitContainer *body = nullptr;
	TabContainer *right_tabs = nullptr;
	Control *center_host = nullptr;

	SideState sides[2];
	bool has_remembered_gap[GAP_MAX] = { false, false };
	int remembered_gap[GAP_MAX] = { 0, 0 };
	// Transient preview-only chrome override. Hides both dock columns without
	// mutating stored side modes, drawers, or selected tabs. Split gaps are
	// remembered by identity before either column is hidden so promotion can
	// restore the exact prior presentation.
	bool presentation_hidden = false;

	static int _side_index(Side p_side) { return p_side == Side::LEFT ? 0 : 1; }

	SideRailSideState _pure_state(Side p_side) const;
	void _store_state(Side p_side, const SideRailSideState &p_state);
	int _shown_dock_index(Side p_side) const;
	// Returns true when a body column's visibility actually changed, which is
	// the only case where the split offsets need re-deriving.
	bool _update_side_visibility(Side p_side);
	void _sync_remembered_gaps();
	void _reapply_gaps();
	void _apply_side(Side p_side);
	void _notify_side_changed(Side p_side) const;
	EditorDock *_drawer_dock(Side p_side) const;

public:
	static String layout_key_for_tile(const String &p_base_key, int p_tile_id);

	void attach(HSplitContainer *p_body, Control *p_center_host);
	HSplitContainer *get_body() const { return body; }
	TabContainer *get_right_tabs() const { return right_tabs; }
	Control *get_center_host() const { return center_host; }

	void place_left(EditorDock *p_dock);
	void add_right(EditorDock *p_dock);

	// Every dock the side owns, enabled or not, in the order the rail mirrors.
	Vector<EditorDock *> get_side_docks(Side p_side) const;
	bool find_dock_side(const EditorDock *p_dock, Side &r_side) const;
	// Whether the dock is one of the docks its side currently displays.
	bool is_dock_shown(EditorDock *p_dock) const;

	SideRailMode get_side_mode(Side p_side) const { return sides[_side_index(p_side)].mode; }
	EditorDock *get_drawer_dock(Side p_side) const;

	// Rail-driven transitions.
	void press_rail_toggle(EditorDock *p_dock);
	void close_drawer(Side p_side);
	void set_side_mode(Side p_side, SideRailMode p_mode);
	void toggle_side_mode(Side p_side);

	// Invoked whenever that side's mode, drawer dock or dock availability
	// changes, so a mirroring rail can refresh without polling.
	void set_side_changed_callback(Side p_side, const Callable &p_callback);

	void focus_dock(EditorDock *p_dock);
	void set_dock_enabled(EditorDock *p_dock, bool p_enabled);

	// Preview-only presentation: hide both dock columns as one operation while
	// leaving stored side state untouched. Idempotent. save_layout() continues
	// to write the underlying user state, never this override.
	void set_presentation_hidden(bool p_hidden);
	bool is_presentation_hidden() const { return presentation_hidden; }

	void save_layout(const Ref<ConfigFile> &p_config, const String &p_section) const;
	void load_layout(const Ref<ConfigFile> &p_config, const String &p_section);
};
