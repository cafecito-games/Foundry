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

#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/run/editor_run_bar.h"
#include "editor/script/script_editor_plugin.h"
#include "scene/main/window.h"

namespace {

static const char *MAIN_SCREEN_NAMES[] = {
	"2d",
	"3d",
	"script",
	"game",
	"assetlib",
};

Dictionary _node_selection_entry(Node *p_node) {
	Dictionary entry;
	if (p_node == nullptr) {
		return entry;
	}
	entry["path"] = p_node->get_path();
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
		entry["root_path"] = p_root->get_path();
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

} // namespace

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
		state["filesystem"] = Dictionary();
		state["script"] = Dictionary();
		state["playing"] = Dictionary();
		state["unsaved"] = Dictionary();
		state["main_screen"] = Dictionary();
		state["modal_stack"] = Array();
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

	return state;
}
