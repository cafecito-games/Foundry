/**************************************************************************/
/*  editor_tile_drop_overlay.cpp                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "editor/editor_scene_pane_tile.h"
#include "editor/scene/editor_scene_tabs.h"
#include "scene/gui/tab_bar.h"
#include "scene/main/viewport.h"

// Fraction of a tile's width/height that counts as an edge drop region.
static constexpr float EDGE_REGION_FRACTION = 0.25f;

EditorSceneTabs *EditorTileDropOverlay::_resolve_source_tabs(const Variant &p_data) const {
	Dictionary d = p_data;
	if (d.get("type", "").operator String() != "tab" || d.get("tab_type", "").operator String() != "tab_bar_tab") {
		return nullptr;
	}
	if (!d.has("from_path") || !is_inside_tree()) {
		return nullptr;
	}
	Node *from_node = get_node_or_null(d["from_path"]);
	if (!from_node) {
		return nullptr;
	}
	for (Node *node = from_node; node; node = node->get_parent()) {
		EditorSceneTabs *tabs = Object::cast_to<EditorSceneTabs>(node);
		if (tabs) {
			return tabs;
		}
	}
	return nullptr;
}

ScenePaneTile *EditorTileDropOverlay::_tile_at(const Point2 &p_local_point) const {
	if (!workspace) {
		return nullptr;
	}
	const Point2 global_point = get_global_transform().xform(p_local_point);
	for (ScenePaneTile *tile : workspace->get_tiles()) {
		if (tile->get_global_rect().has_point(global_point)) {
			return tile;
		}
	}
	return nullptr;
}

Rect2 EditorTileDropOverlay::_tile_local_rect(ScenePaneTile *p_tile) const {
	const Transform2D to_local = get_global_transform().affine_inverse();
	const Rect2 global_rect = p_tile->get_global_rect();
	return Rect2(to_local.xform(global_rect.position), global_rect.size);
}

EditorSceneWorkspace::DropRegion EditorTileDropOverlay::_region_for(ScenePaneTile *p_tile, const Point2 &p_local_point) const {
	const Rect2 rect = _tile_local_rect(p_tile);
	if (rect.size.x <= 0 || rect.size.y <= 0) {
		return EditorSceneWorkspace::DROP_REGION_CENTER;
	}
	const float u = (p_local_point.x - rect.position.x) / rect.size.x;
	const float v = (p_local_point.y - rect.position.y) / rect.size.y;

	// Pick the closest boundary among the edge bands the point falls in;
	// outside every band it is a center drop.
	float best_depth = EDGE_REGION_FRACTION;
	EditorSceneWorkspace::DropRegion region = EditorSceneWorkspace::DROP_REGION_CENTER;
	struct Candidate {
		float depth;
		EditorSceneWorkspace::DropRegion region;
	};
	const Candidate candidates[4] = {
		{ u, EditorSceneWorkspace::DROP_REGION_LEFT },
		{ 1.0f - u, EditorSceneWorkspace::DROP_REGION_RIGHT },
		{ v, EditorSceneWorkspace::DROP_REGION_TOP },
		{ 1.0f - v, EditorSceneWorkspace::DROP_REGION_BOTTOM },
	};
	for (const Candidate &candidate : candidates) {
		if (candidate.depth < best_depth) {
			best_depth = candidate.depth;
			region = candidate.region;
		}
	}
	return region;
}

Rect2 EditorTileDropOverlay::_region_rect(const Rect2 &p_tile_rect, EditorSceneWorkspace::DropRegion p_region) const {
	const Size2 edge(p_tile_rect.size.x * EDGE_REGION_FRACTION, p_tile_rect.size.y * EDGE_REGION_FRACTION);
	switch (p_region) {
		case EditorSceneWorkspace::DROP_REGION_LEFT:
			return Rect2(p_tile_rect.position, Size2(edge.x, p_tile_rect.size.y));
		case EditorSceneWorkspace::DROP_REGION_RIGHT:
			return Rect2(p_tile_rect.position + Point2(p_tile_rect.size.x - edge.x, 0), Size2(edge.x, p_tile_rect.size.y));
		case EditorSceneWorkspace::DROP_REGION_TOP:
			return Rect2(p_tile_rect.position, Size2(p_tile_rect.size.x, edge.y));
		case EditorSceneWorkspace::DROP_REGION_BOTTOM:
			return Rect2(p_tile_rect.position + Point2(0, p_tile_rect.size.y - edge.y), Size2(p_tile_rect.size.x, edge.y));
		case EditorSceneWorkspace::DROP_REGION_CENTER:
			return p_tile_rect.grow_individual(-edge.x, -edge.y, -edge.x, -edge.y);
	}
	return p_tile_rect;
}

void EditorTileDropOverlay::_begin_drag(EditorSceneTabs *p_source_tabs) {
	source_tabs_id = p_source_tabs->get_instance_id();
	source_tile_id = p_source_tabs->get_tile_id();
	hovered_tile_id = -1;
	set_mouse_filter(Control::MOUSE_FILTER_STOP);
	show();
	queue_redraw();
}

void EditorTileDropOverlay::_end_drag() {
	source_tabs_id = ObjectID();
	source_tile_id = -1;
	hovered_tile_id = -1;
	set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	hide();
}

void EditorTileDropOverlay::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_DRAG_BEGIN: {
			Viewport *viewport = get_viewport();
			if (!viewport || !workspace) {
				break;
			}
			EditorSceneTabs *source_tabs = _resolve_source_tabs(viewport->gui_get_drag_data());
			if (source_tabs && workspace->is_ancestor_of(source_tabs)) {
				_begin_drag(source_tabs);
			}
		} break;

		case NOTIFICATION_DRAG_END: {
			_end_drag();
		} break;

		case NOTIFICATION_DRAW: {
			if (!is_drag_active() || !workspace) {
				break;
			}
			ScenePaneTile *hovered_tile = workspace->get_tile_by_id(hovered_tile_id);
			if (!hovered_tile) {
				break;
			}
			const Rect2 tile_rect = _tile_local_rect(hovered_tile);
			const Color accent = get_theme_color(SNAME("accent_color"), SNAME("Editor"));

			const EditorSceneWorkspace::DropRegion regions[5] = {
				EditorSceneWorkspace::DROP_REGION_CENTER,
				EditorSceneWorkspace::DROP_REGION_LEFT,
				EditorSceneWorkspace::DROP_REGION_RIGHT,
				EditorSceneWorkspace::DROP_REGION_TOP,
				EditorSceneWorkspace::DROP_REGION_BOTTOM,
			};
			for (EditorSceneWorkspace::DropRegion region : regions) {
				const Rect2 rect = _region_rect(tile_rect, region);
				if (region == hovered_region) {
					draw_rect(rect, Color(accent, 0.35), true);
					draw_rect(rect, accent, false, 2.0);
				} else {
					draw_rect(rect, Color(accent, 0.08), true);
					draw_rect(rect, Color(accent, 0.4), false, 1.0);
				}
			}
		} break;
	}
}

bool EditorTileDropOverlay::has_point(const Point2 &p_point) const {
	if (!is_drag_active()) {
		return false;
	}
	// Leave the source strip's own rect to the TabBar so native same-strip
	// tab reordering keeps working during the drag.
	EditorSceneTabs *source_tabs = ObjectDB::get_instance<EditorSceneTabs>(source_tabs_id);
	if (source_tabs) {
		const Point2 global_point = get_global_transform().xform(p_point);
		if (source_tabs->get_global_rect().has_point(global_point)) {
			return false;
		}
	}
	return Control::has_point(p_point);
}

bool EditorTileDropOverlay::can_drop_data(const Point2 &p_point, const Variant &p_data) const {
	if (!is_drag_active() || !workspace) {
		return false;
	}
	EditorSceneTabs *source_tabs = _resolve_source_tabs(p_data);
	if (!source_tabs || !workspace->is_ancestor_of(source_tabs)) {
		return false;
	}
	ScenePaneTile *tile = _tile_at(p_point);
	const int new_hovered_tile_id = tile ? tile->get_tile_id() : -1;
	const EditorSceneWorkspace::DropRegion new_region = tile ? _region_for(tile, p_point) : EditorSceneWorkspace::DROP_REGION_CENTER;
	if (new_hovered_tile_id != hovered_tile_id || new_region != hovered_region) {
		hovered_tile_id = new_hovered_tile_id;
		hovered_region = new_region;
		const_cast<EditorTileDropOverlay *>(this)->queue_redraw();
	}
	if (!tile) {
		return false;
	}
	// A center drop on the source tile is a no-op; edges still split it.
	if (tile->get_tile_id() == source_tabs->get_tile_id() && hovered_region == EditorSceneWorkspace::DROP_REGION_CENTER) {
		return false;
	}
	return true;
}

void EditorTileDropOverlay::drop_data(const Point2 &p_point, const Variant &p_data) {
	if (!workspace) {
		return;
	}
	EditorSceneTabs *source_tabs = _resolve_source_tabs(p_data);
	if (!source_tabs || !workspace->is_ancestor_of(source_tabs)) {
		return;
	}
	ScenePaneTile *tile = _tile_at(p_point);
	if (!tile) {
		return;
	}
	Dictionary d = p_data;
	const int src_tab = d.get("tab_index", -1);
	const EditorSceneWorkspace::DropRegion region = _region_for(tile, p_point);
	workspace->perform_tab_drop(source_tabs->get_tile_id(), src_tab, tile->get_tile_id(), region);
}

void EditorTileDropOverlay::setup(EditorSceneWorkspace *p_workspace) {
	workspace = p_workspace;
}

EditorTileDropOverlay::EditorTileDropOverlay() {
	set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	hide();
}
