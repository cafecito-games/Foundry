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

class Control;
class EditorDock;
class HSplitContainer;
class TabContainer;

/**
 * Manages the in-tile dock strip: [left dock | center host | right tab stack].
 * Docks live here instead of the global EditorDockManager slots.
 */
class EditorTileDockRegion {
	HSplitContainer *body = nullptr;
	TabContainer *right_tabs = nullptr;
	Control *center_host = nullptr;

public:
	static String layout_key_for_tile(const String &p_base_key, int p_tile_id);

	void attach(HSplitContainer *p_body, Control *p_center_host);
	HSplitContainer *get_body() const { return body; }
	TabContainer *get_right_tabs() const { return right_tabs; }
	Control *get_center_host() const { return center_host; }

	void place_left(EditorDock *p_dock);
	void add_right(EditorDock *p_dock);

	void focus_dock(EditorDock *p_dock);
	void set_dock_enabled(EditorDock *p_dock, bool p_enabled);

	void save_layout(const Ref<ConfigFile> &p_config, const String &p_section) const;
	void load_layout(const Ref<ConfigFile> &p_config, const String &p_section);
};
