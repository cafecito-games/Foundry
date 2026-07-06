/**************************************************************************/
/*  editor_automation_workspace.h                                         */
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

#pragma once

#include "editor/automation/editor_automation_snapshot.h"
#include "editor/editor_scene_workspace.h"

class EditorData;
class EditorNode;
class ScenePaneTile;

class EditorAutomationWorkspace {
public:
	static Dictionary capture_workspace_state(EditorData *p_editor_data, EditorSceneWorkspace *p_workspace);

	static bool selector_is_tile_container(const Dictionary &p_selector);
	static EditorAutomationSelectorResult resolve_tile_container(const EditorAutomationSnapshot &p_snapshot, const Dictionary &p_selector);

	static EditorSceneWorkspace::TileDropRegion parse_drop_region(const String &p_region);
	static String drop_region_name(EditorSceneWorkspace::TileDropRegion p_region);
	static Vector2 global_drop_point(ScenePaneTile *p_tile, EditorSceneWorkspace::TileDropRegion p_region);

	static bool resolve_scene_tab_source(const EditorAutomationElement &p_element, int &r_tile_id, int &r_tab_index);
	static int resolve_target_tile_id(const Dictionary &p_options, const EditorAutomationSnapshot &p_snapshot);

	static bool dock_scene_tab(
			EditorData *p_editor_data,
			EditorSceneWorkspace *p_workspace,
			int p_source_tile_id,
			int p_source_tab,
			int p_target_tile_id,
			EditorSceneWorkspace::TileDropRegion p_region,
			EditorNode *p_editor_node = nullptr);

	static int get_focused_tile_id();
	static int get_tile_count(EditorSceneWorkspace *p_workspace);
};
