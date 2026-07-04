/**************************************************************************/
/*  editor_tile_drop_overlay.cpp                                          */
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

#include "editor_tile_drop_overlay.h"

#include "editor/editor_node.h"
#include "editor/scene/editor_scene_tabs.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/tab_bar.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"

// Each edge region is a quarter of the tile in its axis; the remainder is the
// center (move-into-tile) region.
static constexpr float EDGE_FRACTION = 0.25f;

bool EditorTileDropOverlay::_is_scene_tab_drag(const Variant &p_data) {
	if (p_data.get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary d = p_data;
	if (String(d.get("type", "")) != "tab" || String(d.get("tab_type", "")) != "tab_bar_tab") {
		return false;
	}
	int source_tile_id = -1;
	int source_tab = -1;
	return _resolve_source(p_data, source_tile_id, source_tab);
}

bool EditorTileDropOverlay::_resolve_source(const Variant &p_data, int &r_source_tile_id, int &r_source_tab) {
	r_source_tile_id = -1;
	r_source_tab = -1;
	if (p_data.get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary d = p_data;
	if (!d.has("from_path") || !d.has("tab_index")) {
		return false;
	}
	SceneTree *tree = SceneTree::get_singleton();
	if (!tree || !tree->get_root()) {
		return false;
	}
	Node *from_node = tree->get_root()->get_node_or_null(d["from_path"]);
	TabBar *from_bar = Object::cast_to<TabBar>(from_node);
	if (!from_bar || from_bar->get_tabs_rearrange_group() != EditorSceneTabs::TILE_TAB_REARRANGE_GROUP) {
		return false;
	}
	for (Node *node = from_bar; node; node = node->get_parent()) {
		EditorSceneTabs *tabs = Object::cast_to<EditorSceneTabs>(node);
		if (tabs) {
			r_source_tile_id = tabs->get_tile_id();
			r_source_tab = d["tab_index"];
			return true;
		}
	}
	return false;
}

EditorSceneWorkspace::TileDropRegion EditorTileDropOverlay::_region_at(const Point2 &p_local) const {
	const Size2 size = get_size();
	const float edge_x = size.x * EDGE_FRACTION;
	const float edge_y = size.y * EDGE_FRACTION;
	if (p_local.x < edge_x) {
		return EditorSceneWorkspace::DROP_LEFT;
	}
	if (p_local.x > size.x - edge_x) {
		return EditorSceneWorkspace::DROP_RIGHT;
	}
	if (p_local.y < edge_y) {
		return EditorSceneWorkspace::DROP_TOP;
	}
	if (p_local.y > size.y - edge_y) {
		return EditorSceneWorkspace::DROP_BOTTOM;
	}
	return EditorSceneWorkspace::DROP_CENTER;
}

void EditorTileDropOverlay::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_DRAG_BEGIN: {
			drag_active = get_viewport() && _is_scene_tab_drag(get_viewport()->gui_get_drag_data());
			// Only intercept mouse events (as a drop target) while a scene tab is
			// actually being dragged; stay transparent to input otherwise.
			set_mouse_filter(drag_active ? Control::MOUSE_FILTER_STOP : Control::MOUSE_FILTER_IGNORE);
			set_process_internal(drag_active);
			queue_redraw();
		} break;

		case NOTIFICATION_DRAG_END: {
			drag_active = false;
			set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			set_process_internal(false);
			queue_redraw();
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			// Follow the cursor so the highlighted region tracks the drag.
			queue_redraw();
		} break;

		case NOTIFICATION_DRAW: {
			if (!drag_active) {
				break;
			}
			const Size2 size = get_size();
			const Color base = Color(0.4f, 0.6f, 1.0f, 0.12f);
			const Color highlight = Color(0.4f, 0.6f, 1.0f, 0.35f);
			draw_rect(Rect2(Point2(), size), base, true);

			const EditorSceneWorkspace::TileDropRegion region = _region_at(get_local_mouse_position());
			const float edge_x = size.x * EDGE_FRACTION;
			const float edge_y = size.y * EDGE_FRACTION;
			Rect2 highlight_rect;
			switch (region) {
				case EditorSceneWorkspace::DROP_LEFT:
					highlight_rect = Rect2(0, 0, edge_x, size.y);
					break;
				case EditorSceneWorkspace::DROP_RIGHT:
					highlight_rect = Rect2(size.x - edge_x, 0, edge_x, size.y);
					break;
				case EditorSceneWorkspace::DROP_TOP:
					highlight_rect = Rect2(0, 0, size.x, edge_y);
					break;
				case EditorSceneWorkspace::DROP_BOTTOM:
					highlight_rect = Rect2(0, size.y - edge_y, size.x, edge_y);
					break;
				default:
					highlight_rect = Rect2(edge_x, edge_y, size.x - 2 * edge_x, size.y - 2 * edge_y);
					break;
			}
			draw_rect(highlight_rect, highlight, true);
		} break;
	}
}

bool EditorTileDropOverlay::can_drop_data(const Point2 &p_point, const Variant &p_data) const {
	return _is_scene_tab_drag(p_data);
}

void EditorTileDropOverlay::drop_data(const Point2 &p_point, const Variant &p_data) {
	int source_tile_id = -1;
	int source_tab = -1;
	if (!_resolve_source(p_data, source_tile_id, source_tab)) {
		return;
	}
	if (!EditorNode::get_singleton()) {
		return;
	}
	EditorNode::get_singleton()->handle_tile_scene_drop(owning_tile_id, _region_at(p_point), source_tile_id, source_tab);
}

EditorTileDropOverlay::EditorTileDropOverlay() {
	set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
}
