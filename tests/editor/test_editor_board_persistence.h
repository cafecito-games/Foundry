/**************************************************************************/
/*  test_editor_board_persistence.h                                       */
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
#include "editor/editor_data.h"
#include "editor/editor_scene_workspace.h"

#include "tests/test_macros.h"

namespace TestEditorBoardPersistence {

TEST_CASE("[Editor][Boards] Two workspaces persist to independent sections") {
	Ref<ConfigFile> config;
	config.instantiate();

	EditorData editor_data_a;
	EditorSelection *selection_a = memnew(EditorSelection);
	EditorSceneWorkspace *workspace_a = EditorSceneWorkspace::create_single_leaf_workspace(selection_a, &editor_data_a);
	SceneTree::get_singleton()->get_root()->add_child(workspace_a);
	workspace_a->split(workspace_a->get_focused_leaf(), false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);

	EditorData editor_data_b;
	EditorSelection *selection_b = memnew(EditorSelection);
	EditorSceneWorkspace *workspace_b = EditorSceneWorkspace::create_single_leaf_workspace(selection_b, &editor_data_b);
	SceneTree::get_singleton()->get_root()->add_child(workspace_b);

	EditorSceneWorkspace::save_to_config(config, workspace_a, "Board_0");
	EditorSceneWorkspace::save_to_config(config, workspace_b, "Board_1");

	CHECK(EditorSceneWorkspace::has_workspace_session(config, "Board_0"));
	CHECK(EditorSceneWorkspace::has_workspace_session(config, "Board_1"));
	// Two leaves in A, one in B -- the sections must not have merged.
	CHECK(int(config->get_value("Board_0", "node_count")) == 3);
	CHECK(int(config->get_value("Board_1", "node_count")) == 1);

	memdelete(workspace_a);
	memdelete(workspace_b);
	memdelete(selection_a);
	memdelete(selection_b);
}

} // namespace TestEditorBoardPersistence
