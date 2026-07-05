/**************************************************************************/
/*  editor_scene_tab_bar.cpp                                              */
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

#include "editor_scene_tab_bar.h"

#include "editor/editor_node.h"

void EditorSceneTabBar::drop_data(const Point2 &p_point, const Variant &p_data) {
	if (p_data.get_type() != Variant::DICTIONARY) {
		TabBar::drop_data(p_point, p_data);
		return;
	}
	const Dictionary d = p_data;
	if (String(d.get("type", "")) != "tab" || String(d.get("tab_type", "")) != "tab_bar_tab") {
		TabBar::drop_data(p_point, p_data);
		return;
	}
	if (!d.has("from_path")) {
		TabBar::drop_data(p_point, p_data);
		return;
	}
	const NodePath from_path = d["from_path"];
	if (from_path == get_path()) {
		TabBar::drop_data(p_point, p_data);
		return;
	}

	// Cross-strip drop: TabBar::_move_tab_from emits no signal and only moves
	// the visual tab, so route through EditorNode tile ownership instead.
	if (EditorNode::get_singleton()) {
		EditorNode::get_singleton()->handle_tile_scene_tab_bar_drop(tile_id, p_data, p_point);
	}
}

EditorSceneTabBar::EditorSceneTabBar() {
}
