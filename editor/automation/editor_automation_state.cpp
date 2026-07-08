/**************************************************************************/
/*  editor_automation_state.cpp                                           */
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

#include "editor_automation_state.h"

#include "core/config/project_settings.h"

#include "editor/automation/editor_automation_workspace.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/run/editor_run_bar.h"
#include "editor/scene/canvas_item_editor_plugin.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/script/script_editor_plugin.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/window.h"

namespace {

static const char *MAIN_SCREEN_NAMES[] = {
	"2d",
	"3d",
	"script",
	"game",
	"assetlib",
};

String _node_tree_path(Node *p_node) {
	if (p_node == nullptr || !p_node->is_inside_tree()) {
		return String();
	}
	return String(p_node->get_path());
}

Dictionary _node_selection_entry(Node *p_node) {
	Dictionary entry;
	if (p_node == nullptr) {
		return entry;
	}
	entry["path"] = _node_tree_path(p_node);
	entry["name"] = p_node->get_name();
	entry["class"] = p_node->get_class();
	return entry;
}

Dictionary _scene_entry(int p_index, const String &p_path, Node *p_root, bool p_unsaved) {
	Dictionary entry;
	entry["index"] = p_index;
	entry["path"] = p_path;
	entry["active"] = p_index == EditorNode::get_editor_data().get_edited_scene();
	if (p_root != nullptr) {
		entry["root_name"] = p_root->get_name();
		entry["root_class"] = p_root->get_class();
		entry["root_path"] = _node_tree_path(p_root);
	}
	entry["unsaved"] = p_unsaved;
	return entry;
}

String _main_screen_name(int p_index) {
	static const int k_main_screen_count = 5;
	if (p_index >= 0 && p_index < k_main_screen_count) {
		return MAIN_SCREEN_NAMES[p_index];
	}
	return String();
}

String _canvas_tool_name(CanvasItemEditor::Tool p_tool) {
	switch (p_tool) {
		case CanvasItemEditor::TOOL_SELECT:
			return "select";
		case CanvasItemEditor::TOOL_LIST_SELECT:
			return "list_select";
		case CanvasItemEditor::TOOL_MOVE:
			return "move";
		case CanvasItemEditor::TOOL_SCALE:
			return "scale";
		case CanvasItemEditor::TOOL_ROTATE:
			return "rotate";
		case CanvasItemEditor::TOOL_EDIT_PIVOT:
			return "edit_pivot";
		case CanvasItemEditor::TOOL_PAN:
			return "pan";
		case CanvasItemEditor::TOOL_RULER:
			return "ruler";
		default:
			return "unknown";
	}
}

String _node3d_tool_name(Node3DEditor::ToolMode p_tool) {
	switch (p_tool) {
		case Node3DEditor::TOOL_MODE_TRANSFORM:
			return "transform";
		case Node3DEditor::TOOL_MODE_MOVE:
			return "move";
		case Node3DEditor::TOOL_MODE_ROTATE:
			return "rotate";
		case Node3DEditor::TOOL_MODE_SCALE:
			return "scale";
		case Node3DEditor::TOOL_MODE_SELECT:
			return "select";
		case Node3DEditor::TOOL_MODE_LIST_SELECT:
			return "list_select";
		default:
			return "unknown";
	}
}

Dictionary _empty_subsystem_state() {
	Dictionary state;
	state["supported"] = false;
	return state;
}

} // namespace

Dictionary _scene_node_tree(Node *p_node, int p_depth, int p_max_depth) {
	Dictionary dict;
	if (p_node == nullptr) {
		return dict;
	}
	dict["name"] = p_node->get_name();
	dict["class"] = p_node->get_class();
	dict["path"] = String(p_node->get_path());
	if (p_depth >= p_max_depth) {
		return dict;
	}
	Array children;
	for (int i = 0; i < p_node->get_child_count(); i++) {
		children.push_back(_scene_node_tree(p_node->get_child(i), p_depth + 1, p_max_depth));
	}
	dict["children"] = children;
	return dict;
}

Dictionary EditorAutomationState::read_scene_tree(Node *p_snapshot_root) {
	Dictionary payload;
	if (p_snapshot_root != nullptr) {
		payload["root"] = _scene_node_tree(p_snapshot_root, 0, 16);
		payload["source"] = "snapshot_root";
		return payload;
	}

	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node == nullptr || !editor_node->is_editor_ready()) {
		payload["root"] = Dictionary();
		payload["source"] = "unavailable";
		return payload;
	}

	Node *edited_root = EditorNode::get_editor_data().get_edited_scene_root();
	if (edited_root == nullptr) {
		payload["root"] = Dictionary();
		payload["source"] = "no_active_scene";
		return payload;
	}

	payload["root"] = _scene_node_tree(edited_root, 0, 16);
	payload["active_scene_path"] = EditorNode::get_editor_data().get_scene_path(EditorNode::get_editor_data().get_edited_scene());
	payload["source"] = "edited_scene_root";
	return payload;
}

Array EditorAutomationState::capture_modal_stack(Node *p_root) {
	Array stack;
	Node *root_node = p_root;
	if (root_node == nullptr) {
		EditorNode *editor_node = EditorNode::get_singleton();
		if (editor_node != nullptr && editor_node->is_editor_ready()) {
			root_node = editor_node;
		}
	}
	if (root_node == nullptr || !root_node->is_inside_tree()) {
		return stack;
	}

	Window *root_window = root_node->get_window();
	if (root_window == nullptr) {
		return stack;
	}

	Window *exclusive = root_window->get_exclusive_child();
	while (exclusive != nullptr) {
		Dictionary entry;
		entry["class"] = exclusive->get_class();
		entry["title"] = exclusive->get_title();
		entry["name"] = exclusive->get_name();
		entry["visible"] = exclusive->is_visible();
		stack.push_back(entry);
		exclusive = exclusive->get_exclusive_child();
	}
	return stack;
}

Dictionary EditorAutomationState::read_editor_state() {
	Dictionary state;
	state["supported"] = false;

	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node == nullptr || !editor_node->is_editor_ready()) {
		state["open_scenes"] = Array();
		state["active_scene_path"] = String();
		state["edited_scene_root"] = Dictionary();
		state["selected_nodes"] = Array();
		state["selected_paths"] = Array();
		Dictionary filesystem_state;
		filesystem_state["supported"] = false;
		state["filesystem"] = filesystem_state;
		state["script"] = Dictionary();
		state["playing"] = Dictionary();
		state["unsaved"] = Dictionary();
		state["main_screen"] = Dictionary();
		state["modal_stack"] = Array();
		state["focused_tile_id"] = 0;
		state["workspace"] = Dictionary();
		state["inspector"] = _empty_subsystem_state();
		state["undo_redo"] = _empty_subsystem_state();
		state["view_2d"] = _empty_subsystem_state();
		state["view_3d"] = _empty_subsystem_state();
		return state;
	}

	state["supported"] = true;
	EditorInterface *editor_interface = EditorInterface::get_singleton();
	EditorData &editor_data = EditorNode::get_editor_data();
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();

	Array open_scenes;
	const int scene_count = editor_data.get_edited_scene_count();
	const int active_scene_index = editor_data.get_edited_scene();
	for (int i = 0; i < scene_count; i++) {
		const String scene_path = editor_data.get_scene_path(i);
		Node *scene_root = editor_data.get_edited_scene_root(i);
		bool unsaved = false;
		if (undo_redo != nullptr) {
			unsaved = undo_redo->is_history_unsaved(editor_data.get_scene_history_id(i));
		}
		open_scenes.push_back(_scene_entry(i, scene_path, scene_root, unsaved));
	}
	state["open_scenes"] = open_scenes;
	state["active_scene_index"] = active_scene_index;
	state["active_scene_path"] = active_scene_index >= 0 ? editor_data.get_scene_path(active_scene_index) : String();

	Node *edited_root = editor_data.get_edited_scene_root();
	if (edited_root != nullptr) {
		state["edited_scene_root"] = _node_selection_entry(edited_root);
	} else {
		state["edited_scene_root"] = Dictionary();
	}

	Array selected_nodes;
	EditorSelection *selection = editor_node->get_editor_selection();
	if (selection != nullptr) {
		List<Node *> selected = selection->get_top_selected_node_list();
		for (Node *node : selected) {
			selected_nodes.push_back(_node_selection_entry(node));
		}
	}
	state["selected_nodes"] = selected_nodes;

	Array selected_paths;
	if (editor_interface != nullptr) {
		const Vector<String> paths = editor_interface->get_selected_paths();
		for (const String &path : paths) {
			selected_paths.push_back(path);
		}
	}
	state["selected_paths"] = selected_paths;

	Dictionary filesystem_state;
	if (editor_interface != nullptr) {
		filesystem_state["supported"] = true;
		filesystem_state["current_path"] = editor_interface->get_current_path();
		filesystem_state["current_directory"] = editor_interface->get_current_directory();
		filesystem_state["selected_paths"] = selected_paths;
	} else {
		filesystem_state["supported"] = false;
	}
	state["filesystem"] = filesystem_state;

	Dictionary script_state;
	ScriptEditor *script_editor = ScriptEditor::get_singleton();
	if (script_editor != nullptr) {
		String script_path;
		int line = -1;
		int column = -1;
		if (script_editor->get_current_script_view_state(script_path, line, column)) {
			script_state["supported"] = true;
			script_state["path"] = script_path;
			script_state["line"] = line;
			script_state["column"] = column;
		} else {
			script_state["supported"] = false;
		}
	} else {
		script_state["supported"] = false;
	}
	state["script"] = script_state;

	Dictionary playing_state;
	EditorRunBar *run_bar = EditorRunBar::get_singleton();
	if (run_bar != nullptr) {
		playing_state["supported"] = true;
		playing_state["is_playing"] = run_bar->is_playing();
		playing_state["scene"] = run_bar->get_playing_scene();
	} else if (editor_interface != nullptr) {
		playing_state["supported"] = true;
		playing_state["is_playing"] = editor_interface->is_playing_scene();
		playing_state["scene"] = editor_interface->get_playing_scene();
	} else {
		playing_state["supported"] = false;
	}
	state["playing"] = playing_state;

	Dictionary unsaved_state;
	if (undo_redo != nullptr) {
		unsaved_state["supported"] = true;
		unsaved_state["current_scene"] = active_scene_index >= 0 ? undo_redo->is_history_unsaved(editor_data.get_scene_history_id(active_scene_index)) : false;
		unsaved_state["global"] = undo_redo->is_history_unsaved(EditorUndoRedoManager::GLOBAL_HISTORY);
		Array scene_unsaved;
		for (int i = 0; i < scene_count; i++) {
			Dictionary entry;
			entry["index"] = i;
			entry["path"] = editor_data.get_scene_path(i);
			entry["unsaved"] = undo_redo->is_history_unsaved(editor_data.get_scene_history_id(i));
			scene_unsaved.push_back(entry);
		}
		unsaved_state["scenes"] = scene_unsaved;
	} else {
		unsaved_state["supported"] = false;
	}
	state["unsaved"] = unsaved_state;

	Dictionary main_screen;
	EditorMainScreen *main_screen_editor = EditorNode::get_editor_main_screen();
	if (main_screen_editor != nullptr) {
		const int selected_index = main_screen_editor->get_selected_index();
		main_screen["supported"] = true;
		main_screen["index"] = selected_index;
		main_screen["name"] = _main_screen_name(selected_index);
	} else {
		main_screen["supported"] = false;
	}
	state["main_screen"] = main_screen;
	state["modal_stack"] = capture_modal_stack(editor_node);

	const int focused_tile_id = editor_data.get_focused_tile_id();
	state["focused_tile_id"] = focused_tile_id;
	state["workspace"] = EditorAutomationWorkspace::capture_workspace_state(&editor_data, EditorNode::get_scene_workspace());

	Dictionary inspector_state;
	EditorInspector *inspector = editor_node->get_focused_inspector();
	if (inspector != nullptr) {
		Object *edited_object = inspector->get_edited_object();
		inspector_state["supported"] = true;
		if (edited_object != nullptr) {
			inspector_state["target_class"] = edited_object->get_class();
			if (Node *edited_node = Object::cast_to<Node>(edited_object)) {
				inspector_state["target_name"] = edited_node->get_name();
				inspector_state["target_path"] = _node_tree_path(edited_node);
			} else {
				inspector_state["target_name"] = String();
			}
		} else {
			inspector_state["target_class"] = String();
			inspector_state["target_name"] = String();
		}
	} else {
		inspector_state["supported"] = false;
	}
	state["inspector"] = inspector_state;

	Dictionary undo_redo_state;
	if (undo_redo != nullptr) {
		undo_redo_state["supported"] = true;
		undo_redo_state["current_action_name"] = undo_redo->get_current_action_name();
		undo_redo_state["current_history_id"] = undo_redo->get_current_action_history_id();
		undo_redo_state["current_redo_action_name"] = undo_redo->get_current_redo_action_name();
		undo_redo_state["current_redo_history_id"] = undo_redo->get_current_redo_action_history_id();
		undo_redo_state["has_undo"] = undo_redo->has_undo();
		undo_redo_state["has_redo"] = undo_redo->has_redo();
	} else {
		undo_redo_state["supported"] = false;
	}
	state["undo_redo"] = undo_redo_state;

	Dictionary view_2d_state;
	if (CanvasItemEditor *canvas_editor = CanvasItemEditor::get_singleton()) {
		view_2d_state["supported"] = true;
		view_2d_state["tool"] = _canvas_tool_name(canvas_editor->get_current_tool());
		const Dictionary geometry = canvas_editor->get_state();
		if (geometry.has("zoom")) {
			view_2d_state["zoom"] = geometry["zoom"];
		}
		if (geometry.has("ofs")) {
			view_2d_state["view_offset"] = geometry["ofs"];
		}
	} else {
		view_2d_state["supported"] = false;
	}
	state["view_2d"] = view_2d_state;

	Dictionary view_3d_state;
	if (Node3DEditor *node_3d_editor = Node3DEditor::get_singleton()) {
		view_3d_state["supported"] = true;
		view_3d_state["tool"] = _node3d_tool_name(node_3d_editor->get_tool_mode());
		const Dictionary spatial_state = node_3d_editor->get_state();
		view_3d_state["editor_state"] = spatial_state;
		if (Node3DEditorViewport *viewport = node_3d_editor->get_focused_viewport()) {
			const Dictionary viewport_state = viewport->get_state();
			view_3d_state["viewport_state"] = viewport_state;
			if (viewport_state.has("position")) {
				view_3d_state["camera_position"] = viewport_state["position"];
			}
			if (Camera3D *camera = viewport->get_camera_3d()) {
				view_3d_state["camera_transform"] = camera->get_global_transform();
				view_3d_state["camera_projection"] = camera->get_projection() == Camera3D::PROJECTION_ORTHOGONAL ? "orthogonal" : "perspective";
			}
		}
	} else {
		view_3d_state["supported"] = false;
	}
	state["view_3d"] = view_3d_state;

	state["projectless_shell"] = editor_node->is_projectless_shell();
	state["project_loaded"] = ProjectSettings::get_singleton()->is_project_loaded();

	return state;
}
