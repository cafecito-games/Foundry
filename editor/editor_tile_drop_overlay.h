/**************************************************************************/
/*  editor_tile_drop_overlay.h                                            */
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

#include "editor/editor_scene_workspace.h"

#include "scene/gui/control.h"

class EditorSceneTabs;
class ScenePaneTile;

/**
 * Full-workspace overlay that turns a scene-tab drag into tile operations.
 * While a scene tab from this workspace is being dragged, the overlay covers
 * every tile and hit-tests five drop regions per tile: center (move the scene
 * into that tile) and four edges (split the tile in that direction, the new
 * tile receiving the scene). The source strip's own rect is excluded from the
 * overlay hit area so native same-strip tab reordering keeps working.
 */
class EditorTileDropOverlay : public Control {
	FOUNDRY_CLASS(EditorTileDropOverlay, Control);

	EditorSceneWorkspace *workspace = nullptr;
	ObjectID source_tabs_id; // The strip the current drag originates from.
	int source_tile_id = -1;

	mutable int hovered_tile_id = -1;
	mutable EditorSceneWorkspace::DropRegion hovered_region = EditorSceneWorkspace::DROP_REGION_CENTER;

	EditorSceneTabs *_resolve_source_tabs(const Variant &p_data) const;
	ScenePaneTile *_tile_at(const Point2 &p_local_point) const;
	EditorSceneWorkspace::DropRegion _region_for(ScenePaneTile *p_tile, const Point2 &p_local_point) const;
	Rect2 _tile_local_rect(ScenePaneTile *p_tile) const;
	Rect2 _region_rect(const Rect2 &p_tile_rect, EditorSceneWorkspace::DropRegion p_region) const;
	void _begin_drag(EditorSceneTabs *p_source_tabs);
	void _end_drag();

protected:
	void _notification(int p_what);

public:
	virtual bool has_point(const Point2 &p_point) const override;
	virtual bool can_drop_data(const Point2 &p_point, const Variant &p_data) const override;
	virtual void drop_data(const Point2 &p_point, const Variant &p_data) override;

	bool is_drag_active() const { return source_tile_id >= 0; }

	void setup(EditorSceneWorkspace *p_workspace);

	EditorTileDropOverlay();
};
