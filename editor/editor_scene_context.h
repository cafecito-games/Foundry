/**************************************************************************/
/*  editor_scene_context.h                                                */
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

#include "editor/editor_data.h"

class Node;
class SubViewport;

/**
 * Owns the editing state of a single edited scene: the scene root, the
 * SubViewport hosting it, the selection, the inspector navigation history,
 * the per-plugin editor state, and the undo history id.
 *
 * The scene root stays parented to the context's viewport for its whole
 * lifetime. Activating a context attaches its viewport to the display
 * container (entering the tree); deactivating detaches it, so inactive
 * scenes neither render nor leak content into the shared editor world.
 * While a context is inactive its selection is tracked by object id, since
 * out-of-tree nodes cannot live in an EditorSelection.
 */
class EditorSceneContext {
	SubViewport *viewport = nullptr;
	Node *scene_root_node = nullptr;
	EditorSelection *selection = nullptr;
	EditorSelectionHistory history;
	Dictionary editor_plugin_states;
	Dictionary main_state;
	int history_id = 0;
	Vector<ObjectID> retained_selection_ids;
	bool active = false;
	bool has_3d_content = false;

	void _recompute_3d_content();

public:
	SubViewport *get_viewport() const { return viewport; }
	EditorSelection *get_selection() const { return selection; }
	EditorSelectionHistory *get_history() { return &history; }

	void set_scene_root_node(Node *p_scene_root, bool p_attach_to_viewport = true);
	Node *get_scene_root_node() const { return scene_root_node; }
	// Parents a still-unparented scene root under the viewport. Used when the
	// caller needs the SceneTree's edited-scene root updated before the scene
	// enters the tree.
	void attach_scene_root_node();

	void set_editor_plugin_states(const Dictionary &p_states) { editor_plugin_states = p_states; }
	Dictionary get_editor_plugin_states() const { return editor_plugin_states; }

	void set_main_state(const Dictionary &p_state) { main_state = p_state; }
	Dictionary get_main_state() const { return main_state; }

	void set_history_id(int p_history_id) { history_id = p_history_id; }
	int get_history_id() const { return history_id; }

	bool is_active() const { return active; }
	void activate(Node *p_display_parent);
	void deactivate();
	void set_display_parent(Node *p_parent, bool p_audio_listener_2d, bool p_exclusive_viewport_parent = false);
	bool scene_has_3d_content() const { return has_3d_content; }

	Vector<ObjectID> get_selected_node_ids() const;
	void set_selected_node_ids(const Vector<ObjectID> &p_ids);

	EditorSceneContext();
	~EditorSceneContext();
};
