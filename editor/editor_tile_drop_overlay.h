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

// Drop target painted over a tile's content while a scene tab is being dragged.
// It splits the tile into five hit regions (center + four edges) and, on drop,
// asks EditorNode to move the dragged scene into the tile (center) or a new
// tile split off the chosen edge.
class EditorTileDropOverlay : public Control {
	FOUNDRY_CLASS(EditorTileDropOverlay, Control);

	int owning_tile_id = 0;
	bool drag_active = false;

	EditorSceneWorkspace::TileDropRegion _region_at(const Point2 &p_local) const;
	static bool _is_scene_tab_drag(const Variant &p_data);
	static bool _resolve_source(const Variant &p_data, int &r_source_tile_id, int &r_source_tab);

protected:
	void _notification(int p_what);

public:
	void set_owning_tile_id(int p_tile_id) { owning_tile_id = p_tile_id; }

	virtual bool can_drop_data(const Point2 &p_point, const Variant &p_data) const override;
	virtual void drop_data(const Point2 &p_point, const Variant &p_data) override;

	EditorTileDropOverlay();
};
