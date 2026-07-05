/**************************************************************************/
/*  editor_scene_workspace.cpp                                            */
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

#include "editor_scene_workspace.h"

#include "core/io/config_file.h"
#include "editor/editor_data.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/editor_script_leaf.h"
#include "editor/editor_workspace_leaf_content.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/split_container.h"

// Central inset for tab (center) drops: matches the rosette center button (30/100).
static constexpr float DROP_CENTER_INSET_FRACTION = 0.30f;

EditorSceneWorkspace::TileDropRegion EditorSceneWorkspace::drop_region_at(const Size2 &p_size, const Point2 &p_local) {
	if (p_size.x <= 0.0f || p_size.y <= 0.0f) {
		return DROP_CENTER;
	}

	const Point2 center = p_size * 0.5f;
	const float dx = Math::abs(p_local.x - center.x);
	const float dy = Math::abs(p_local.y - center.y);
	const float inset_x = p_size.x * DROP_CENTER_INSET_FRACTION * 0.5f;
	const float inset_y = p_size.y * DROP_CENTER_INSET_FRACTION * 0.5f;

	if (dx <= inset_x && dy <= inset_y) {
		return DROP_CENTER;
	}

	if (dx > dy) {
		return p_local.x < center.x ? DROP_LEFT : DROP_RIGHT;
	}
	if (dy > dx) {
		return p_local.y < center.y ? DROP_TOP : DROP_BOTTOM;
	}

	// On the diagonal (near-corner), favor the axis with the larger relative offset.
	const float rel_x = dx / (p_size.x * 0.5f);
	const float rel_y = dy / (p_size.y * 0.5f);
	if (rel_x >= rel_y) {
		return p_local.x < center.x ? DROP_LEFT : DROP_RIGHT;
	}
	return p_local.y < center.y ? DROP_TOP : DROP_BOTTOM;
}

Rect2 EditorSceneWorkspace::drop_preview_rect(const Size2 &p_size, TileDropRegion p_region) {
	const Point2 origin;
	switch (p_region) {
		case DROP_LEFT:
			return Rect2(origin, Size2(p_size.x * 0.5f, p_size.y));
		case DROP_RIGHT:
			return Rect2(origin + Point2(p_size.x * 0.5f, 0), Size2(p_size.x * 0.5f, p_size.y));
		case DROP_TOP:
			return Rect2(origin, Size2(p_size.x, p_size.y * 0.5f));
		case DROP_BOTTOM:
			return Rect2(origin + Point2(0, p_size.y * 0.5f), Size2(p_size.x, p_size.y * 0.5f));
		case DROP_CENTER:
			return Rect2(origin, p_size);
	}
	return Rect2(origin, p_size);
}

void EditorSceneWorkspace::_collapse_if_empty(int p_tile_id) {
	WorkspaceLeafNode *leaf = get_leaf_by_id(p_tile_id);
	if (!leaf || !editor_data) {
		return;
	}
	if (editor_data->get_tile_scene_indices(p_tile_id).is_empty() && leaves.size() > 1) {
		collapse(leaf);
	}
}

WorkspaceLeafNode *EditorSceneWorkspace::handle_scene_drop(int p_scene_idx, WorkspaceLeafNode *p_target_leaf, TileDropRegion p_region) {
	ERR_FAIL_NULL_V(p_target_leaf, nullptr);
	ERR_FAIL_NULL_V(editor_data, nullptr);
	ERR_FAIL_INDEX_V(p_scene_idx, editor_data->get_edited_scene_count(), nullptr);
	ERR_FAIL_COND_V(!leaves.has(p_target_leaf), nullptr);

	const int source_tile_id = editor_data->get_scene_tile(p_scene_idx);
	WorkspaceLeafNode *source_leaf = get_leaf_by_id(source_tile_id);

	WorkspaceLeafNode *dest_leaf = p_target_leaf;
	if (p_region != DROP_CENTER) {
		const bool vertical = p_region == DROP_TOP || p_region == DROP_BOTTOM;
		const SplitSide side = (p_region == DROP_LEFT || p_region == DROP_TOP) ? SPLIT_SIDE_FIRST : SPLIT_SIDE_SECOND;
		dest_leaf = split(p_target_leaf, vertical, side);
		ERR_FAIL_NULL_V(dest_leaf, nullptr);
	} else if (source_tile_id == p_target_leaf->get_leaf_id()) {
		return nullptr;
	}

	editor_data->set_scene_tile(p_scene_idx, dest_leaf->get_leaf_id());

	if (source_leaf && source_leaf != dest_leaf && editor_data->get_tile_scene_indices(source_tile_id).is_empty() && leaves.size() > 1) {
		callable_mp(this, &EditorSceneWorkspace::_collapse_if_empty).call_deferred(source_tile_id);
	}

	return dest_leaf;
}

// --- WorkspaceLeafNode ---

String EditorSceneWorkspace::leaf_layout_section(int p_leaf_id) {
	return vformat("WorkspaceLeaf_%d", p_leaf_id);
}

void WorkspaceLeafNode::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_SORT_CHILDREN: {
			if (leaf_content && leaf_content->get_root_control()) {
				fit_child_in_rect(leaf_content->get_root_control(), Rect2(Point2(), get_size()));
			}
		} break;
	}
}

WorkspaceLeafNode *WorkspaceLeafNode::create(int p_leaf_id, EditorSelection *p_editor_selection, EditorData *p_editor_data, const StringName &p_content_type) {
	ERR_FAIL_NULL_V(p_editor_data, nullptr);
	WorkspaceLeafNode *leaf = memnew(WorkspaceLeafNode);
	leaf->leaf_id = p_leaf_id;

	WorkspaceLeafContent *content = nullptr;
	if (p_content_type == StringName("script")) {
		content = memnew(ScriptLeaf);
	} else {
		ScenePaneTile *tile = memnew(ScenePaneTile);
		tile->setup(p_leaf_id, p_editor_selection, *p_editor_data);
		content = tile;
	}
	leaf->set_leaf_content(content);

	leaf->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	leaf->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	leaf->set_custom_minimum_size(Size2(120, 120) * EDSCALE);
	return leaf;
}

void WorkspaceLeafNode::set_leaf_content(WorkspaceLeafContent *p_content) {
	if (leaf_content && leaf_content->get_root_control() && leaf_content->get_root_control()->get_parent() == this) {
		remove_child(leaf_content->get_root_control());
	}
	leaf_content = p_content;
	if (leaf_content && leaf_content->get_root_control()) {
		add_child(leaf_content->get_root_control());
		queue_sort();
	}
}

WorkspaceLeafContent *WorkspaceLeafNode::take_leaf_content() {
	WorkspaceLeafContent *content = leaf_content;
	if (content && content->get_root_control() && content->get_root_control()->get_parent() == this) {
		remove_child(content->get_root_control());
	}
	leaf_content = nullptr;
	return content;
}

ScenePaneTile *WorkspaceLeafNode::get_pane_tile() const {
	if (leaf_content && leaf_content->get_content_type() == StringName("scene")) {
		return static_cast<ScenePaneTile *>(leaf_content->get_root_control());
	}
	return nullptr;
}

Control *WorkspaceLeafNode::get_content_host() const {
	ScenePaneTile *tile = get_pane_tile();
	return tile ? tile->get_content_host() : nullptr;
}

WorkspaceLeafNode::WorkspaceLeafNode() {
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
}

WorkspaceLeafNode::~WorkspaceLeafNode() {
	// Leaf content controls are freed by the Container child cleanup.
	leaf_content = nullptr;
}

// --- WorkspaceSplitNode ---

void WorkspaceSplitNode::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_SORT_CHILDREN: {
			if (split_container) {
				fit_child_in_rect(split_container, Rect2(Point2(), get_size()));
			}
		} break;
	}
}

WorkspaceSplitNode *WorkspaceSplitNode::create(bool p_vertical) {
	WorkspaceSplitNode *node = memnew(WorkspaceSplitNode);
	node->split_container = memnew(SplitContainer);
	node->split_container->set_vertical(p_vertical);
	node->split_container->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	node->split_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	node->add_child(node->split_container);
	node->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	node->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	return node;
}

bool WorkspaceSplitNode::is_vertical() const {
	return split_container && split_container->is_vertical();
}

void WorkspaceSplitNode::set_vertical(bool p_vertical) {
	ERR_FAIL_NULL(split_container);
	split_container->set_vertical(p_vertical);
}

int WorkspaceSplitNode::get_split_offset() const {
	ERR_FAIL_NULL_V(split_container, 0);
	return split_container->get_split_offset();
}

void WorkspaceSplitNode::set_split_offset(int p_offset) {
	ERR_FAIL_NULL(split_container);
	split_container->set_split_offset(p_offset);
}

WorkspaceSplitNode::WorkspaceSplitNode() {
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
}

// --- EditorSceneWorkspace ---

void EditorSceneWorkspace::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_SORT_CHILDREN: {
			for (int i = 0; i < get_child_count(false); i++) {
				Control *child = Object::cast_to<Control>(get_child(i, false));
				if (child) {
					fit_child_in_rect(child, Rect2(Point2(), get_size()));
				}
			}
		} break;
	}
}

void EditorSceneWorkspace::_bind_methods() {
	ADD_SIGNAL(MethodInfo("leaf_focus_requested", PropertyInfo(Variant::INT, "leaf_id")));
	ADD_SIGNAL(MethodInfo("leaf_added", PropertyInfo(Variant::INT, "leaf_id")));
	ADD_SIGNAL(MethodInfo("leaf_removed", PropertyInfo(Variant::INT, "leaf_id"), PropertyInfo(Variant::INT, "successor_leaf_id")));
}

bool EditorSceneWorkspace::_is_leaf_node(Control *p_node) const {
	return Object::cast_to<WorkspaceLeafNode>(p_node) != nullptr;
}

bool EditorSceneWorkspace::_is_split_node(Control *p_node) const {
	return Object::cast_to<WorkspaceSplitNode>(p_node) != nullptr;
}

WorkspaceLeafNode *EditorSceneWorkspace::_find_first_leaf(Control *p_node) const {
	ERR_FAIL_NULL_V(p_node, nullptr);

	WorkspaceLeafNode *leaf = Object::cast_to<WorkspaceLeafNode>(p_node);
	if (leaf) {
		return leaf;
	}

	WorkspaceSplitNode *split_node = Object::cast_to<WorkspaceSplitNode>(p_node);
	ERR_FAIL_NULL_V(split_node, nullptr);
	SplitContainer *sc = split_node->get_split_container();
	ERR_FAIL_NULL_V(sc, nullptr);

	for (int i = 0; i < sc->get_child_count(false); i++) {
		Control *child = Object::cast_to<Control>(sc->get_child(i, false));
		if (!child) {
			continue;
		}
		WorkspaceLeafNode *child_leaf = _find_first_leaf(child);
		if (child_leaf) {
			return child_leaf;
		}
	}
	return nullptr;
}

WorkspaceLeafContent *EditorSceneWorkspace::_create_leaf_content(int p_leaf_id, const StringName &p_content_type) {
	if (p_content_type == StringName("script")) {
		return memnew(ScriptLeaf);
	}
	ScenePaneTile *tile = memnew(ScenePaneTile);
	tile->setup(p_leaf_id, editor_selection, *editor_data);
	return tile;
}

void EditorSceneWorkspace::_mount_leaf_content(WorkspaceLeafNode *p_leaf, WorkspaceLeafContent *p_content) {
	ERR_FAIL_NULL(p_leaf);
	p_leaf->set_leaf_content(p_content);
}

WorkspaceLeafNode *EditorSceneWorkspace::_create_leaf(int p_leaf_id, const StringName &p_content_type) {
	ERR_FAIL_NULL_V(editor_data, nullptr);
	WorkspaceLeafNode *leaf = WorkspaceLeafNode::create(p_leaf_id, editor_selection, editor_data, p_content_type);
	leaves.push_back(leaf);
	emit_signal(SNAME("leaf_added"), p_leaf_id);
	return leaf;
}

Control *EditorSceneWorkspace::_get_structural_root() const {
	if (get_child_count(false) == 0) {
		return nullptr;
	}
	return Object::cast_to<Control>(get_child(0, false));
}

EditorSceneWorkspace *EditorSceneWorkspace::create_single_leaf_workspace(EditorSelection *p_editor_selection, EditorData *p_editor_data) {
	ERR_FAIL_NULL_V(p_editor_data, nullptr);
	EditorSceneWorkspace *workspace = memnew(EditorSceneWorkspace);
	workspace->editor_selection = p_editor_selection;
	workspace->editor_data = p_editor_data;
	WorkspaceLeafNode *leaf = workspace->_create_leaf(workspace->next_leaf_id++, StringName("scene"));
	workspace->add_child(leaf);
	workspace->set_focused_leaf(leaf->get_leaf_id());
	return workspace;
}

WorkspaceLeafNode *EditorSceneWorkspace::split(WorkspaceLeafNode *p_leaf, bool p_vertical, SplitSide p_side) {
	ERR_FAIL_NULL_V(p_leaf, nullptr);
	ERR_FAIL_COND_V(!leaves.has(p_leaf), nullptr);

	Node *parent = p_leaf->get_parent();
	ERR_FAIL_NULL_V(parent, nullptr);
	const int idx = p_leaf->get_index(false);

	WorkspaceSplitNode *split_node = WorkspaceSplitNode::create(p_vertical);
	SplitContainer *sc = split_node->get_split_container();

	parent->remove_child(p_leaf);
	parent->add_child(split_node);
	parent->move_child(split_node, idx);

	WorkspaceLeafNode *new_leaf = _create_leaf(next_leaf_id++);
	const bool insert_before = p_side == SPLIT_SIDE_FIRST;
	if (insert_before) {
		sc->add_child(new_leaf);
		sc->add_child(p_leaf);
	} else {
		sc->add_child(p_leaf);
		sc->add_child(new_leaf);
	}

	_update_focus_visuals();
	queue_sort();
	return new_leaf;
}

void EditorSceneWorkspace::collapse(WorkspaceLeafNode *p_leaf) {
	ERR_FAIL_NULL(p_leaf);
	ERR_FAIL_COND(!leaves.has(p_leaf));
	if (leaves.size() <= 1) {
		return;
	}

	SplitContainer *sc = Object::cast_to<SplitContainer>(p_leaf->get_parent());
	ERR_FAIL_NULL(sc);
	WorkspaceSplitNode *split_node = Object::cast_to<WorkspaceSplitNode>(sc->get_parent());
	ERR_FAIL_NULL(split_node);

	Control *sibling = nullptr;
	for (int i = 0; i < sc->get_child_count(false); i++) {
		Control *child = Object::cast_to<Control>(sc->get_child(i, false));
		if (child && child != p_leaf) {
			sibling = child;
			break;
		}
	}
	ERR_FAIL_NULL(sibling);
	WorkspaceLeafNode *successor_leaf = _find_first_leaf(sibling);
	ERR_FAIL_NULL(successor_leaf);

	Node *grand = split_node->get_parent();
	ERR_FAIL_NULL(grand);
	const int split_index = split_node->get_index(false);

	sc->remove_child(sibling);
	sc->remove_child(p_leaf);
	grand->remove_child(split_node);
	memdelete(split_node);

	grand->add_child(sibling);
	grand->move_child(sibling, split_index);

	const int collapsed_leaf_id = p_leaf->get_leaf_id();
	const int successor_leaf_id = successor_leaf->get_leaf_id();
	leaves.erase(p_leaf);
	emit_signal(SNAME("leaf_removed"), collapsed_leaf_id, successor_leaf_id);
	memdelete(p_leaf);

	if (focused_leaf_id == collapsed_leaf_id && !leaves.is_empty()) {
		set_focused_leaf(successor_leaf_id);
		emit_signal(SNAME("leaf_focus_requested"), successor_leaf_id);
	}

	_update_focus_visuals();
	queue_sort();
}

bool EditorSceneWorkspace::move_content(WorkspaceLeafNode *p_from_leaf, WorkspaceLeafNode *p_to_leaf) {
	ERR_FAIL_NULL_V(p_from_leaf, false);
	ERR_FAIL_NULL_V(p_to_leaf, false);
	ERR_FAIL_COND_V(p_from_leaf == p_to_leaf, false);
	ERR_FAIL_COND_V(!leaves.has(p_from_leaf), false);
	ERR_FAIL_COND_V(!leaves.has(p_to_leaf), false);

	WorkspaceLeafContent *from_content = p_from_leaf->take_leaf_content();
	WorkspaceLeafContent *to_content = p_to_leaf->take_leaf_content();
	p_from_leaf->set_leaf_content(to_content);
	p_to_leaf->set_leaf_content(from_content);
	return true;
}

WorkspaceLeafNode *EditorSceneWorkspace::get_leaf_by_id(int p_id) const {
	for (WorkspaceLeafNode *leaf : leaves) {
		if (leaf->get_leaf_id() == p_id) {
			return leaf;
		}
	}
	return nullptr;
}

void EditorSceneWorkspace::set_focused_leaf(int p_id) {
	ERR_FAIL_NULL(get_leaf_by_id(p_id));
	if (focused_leaf_id == p_id) {
		return;
	}
	focused_leaf_id = p_id;
	_update_focus_visuals();
	WorkspaceLeafNode *leaf = get_leaf_by_id(p_id);
	if (leaf && leaf->get_leaf_content()) {
		leaf->get_leaf_content()->on_focus_entered();
	}
}

void EditorSceneWorkspace::request_leaf_focus(int p_leaf_id) {
	if (p_leaf_id == focused_leaf_id) {
		return;
	}
	ERR_FAIL_NULL(get_leaf_by_id(p_leaf_id));
	emit_signal(SNAME("leaf_focus_requested"), p_leaf_id);
}

void EditorSceneWorkspace::_update_focus_visuals() {
	const bool show_focus = leaves.size() > 1;
	for (WorkspaceLeafNode *leaf : leaves) {
		ScenePaneTile *tile = leaf->get_pane_tile();
		if (tile) {
			tile->set_focused_visual(show_focus && leaf->get_leaf_id() == focused_leaf_id);
		}
	}
}

ScenePaneTile *EditorSceneWorkspace::get_tile_by_id(int p_id) const {
	WorkspaceLeafNode *leaf = get_leaf_by_id(p_id);
	return leaf ? leaf->get_pane_tile() : nullptr;
}

ScenePaneTile *EditorSceneWorkspace::get_focused_tile() const {
	return get_tile_by_id(focused_leaf_id);
}

Vector<ScenePaneTile *> EditorSceneWorkspace::get_tiles() const {
	Vector<ScenePaneTile *> tiles;
	for (WorkspaceLeafNode *leaf : leaves) {
		if (leaf->get_pane_tile()) {
			tiles.push_back(leaf->get_pane_tile());
		}
	}
	return tiles;
}

int EditorSceneWorkspace::get_tile_count() const {
	int count = 0;
	for (WorkspaceLeafNode *leaf : leaves) {
		if (leaf->get_pane_tile()) {
			count++;
		}
	}
	return count;
}

// Persistence.
//
// Flat, index-addressed node list:
//   node_count (int), root_node (int), focused_leaf_id (int)
//   node_<i>_type = "split" | "leaf"
//   split: node_<i>_vertical (bool), node_<i>_offset (int),
//          node_<i>_child_a (int), node_<i>_child_b (int)
//   leaf:  node_<i>_leaf_id (int), node_<i>_content_type (String)

struct WorkspaceSaveWalker {
	Ref<ConfigFile> config;
	int next_index = 0;

	int walk(Control *p_node) {
		const int index = next_index++;
		WorkspaceLeafNode *leaf = Object::cast_to<WorkspaceLeafNode>(p_node);
		if (leaf) {
			config->set_value("Workspace", vformat("node_%d_type", index), "leaf");
			config->set_value("Workspace", vformat("node_%d_leaf_id", index), leaf->get_leaf_id());
			if (leaf->get_leaf_content()) {
				config->set_value("Workspace", vformat("node_%d_content_type", index), leaf->get_leaf_content()->get_content_type());
				const String layout_section = EditorSceneWorkspace::leaf_layout_section(leaf->get_leaf_id());
				leaf->get_leaf_content()->save_layout(config, layout_section);
			}
			return index;
		}

		WorkspaceSplitNode *split_node = Object::cast_to<WorkspaceSplitNode>(p_node);
		ERR_FAIL_NULL_V(split_node, index);
		SplitContainer *sc = split_node->get_split_container();
		ERR_FAIL_NULL_V(sc, index);

		Vector<Control *> children;
		for (int i = 0; i < sc->get_child_count(false); i++) {
			Control *child = Object::cast_to<Control>(sc->get_child(i, false));
			if (child) {
				children.push_back(child);
			}
		}
		ERR_FAIL_COND_V(children.size() != 2, index);

		config->set_value("Workspace", vformat("node_%d_type", index), "split");
		config->set_value("Workspace", vformat("node_%d_vertical", index), split_node->is_vertical());
		config->set_value("Workspace", vformat("node_%d_offset", index), split_node->get_split_offset());
		const int child_a = walk(children[0]);
		const int child_b = walk(children[1]);
		config->set_value("Workspace", vformat("node_%d_child_a", index), child_a);
		config->set_value("Workspace", vformat("node_%d_child_b", index), child_b);
		return index;
	}
};

void EditorSceneWorkspace::save_to_config(const Ref<ConfigFile> &p_config, const EditorSceneWorkspace *p_workspace) {
	ERR_FAIL_COND(p_config.is_null());
	ERR_FAIL_NULL(p_workspace);

	if (p_config->has_section(WORKSPACE_CONFIG_SECTION)) {
		p_config->erase_section(WORKSPACE_CONFIG_SECTION);
	}

	Control *root = p_workspace->_get_structural_root();
	ERR_FAIL_NULL(root);

	WorkspaceSaveWalker walker;
	walker.config = p_config;
	const int root_index = walker.walk(root);

	p_config->set_value(WORKSPACE_CONFIG_SECTION, "node_count", walker.next_index);
	p_config->set_value(WORKSPACE_CONFIG_SECTION, "root_node", root_index);
	p_config->set_value(WORKSPACE_CONFIG_SECTION, "focused_leaf_id", p_workspace->get_focused_leaf_id());
}

bool EditorSceneWorkspace::has_workspace_session(const Ref<ConfigFile> &p_config) {
	ERR_FAIL_COND_V(p_config.is_null(), false);
	if (!p_config->has_section(WORKSPACE_CONFIG_SECTION)) {
		return false;
	}
	if (!p_config->has_section_key(WORKSPACE_CONFIG_SECTION, "node_count")) {
		return false;
	}
	const int node_count = int(p_config->get_value(WORKSPACE_CONFIG_SECTION, "node_count"));
	return node_count >= 1;
}

Control *EditorSceneWorkspace::_restore_node_from_config(const Ref<ConfigFile> &p_config, int p_node, int p_node_count, HashSet<int> &r_visited) {
	if (p_node < 0 || p_node >= p_node_count || r_visited.has(p_node)) {
		const int fallback_id = next_leaf_id + leaves.size();
		WorkspaceLeafNode *fallback = _create_leaf(fallback_id);
		next_leaf_id = MAX(next_leaf_id, fallback_id + 1);
		return fallback;
	}
	r_visited.insert(p_node);

	const String type = p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_type", p_node), "leaf");
	if (type == "split") {
		const bool vertical = bool(p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_vertical", p_node), false));
		WorkspaceSplitNode *split_node = WorkspaceSplitNode::create(vertical);
		const int child_a = int(p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_child_a", p_node), -1));
		const int child_b = int(p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_child_b", p_node), -1));
		SplitContainer *sc = split_node->get_split_container();
		Control *a = _restore_node_from_config(p_config, child_a, p_node_count, r_visited);
		Control *b = _restore_node_from_config(p_config, child_b, p_node_count, r_visited);
		if (a) {
			sc->add_child(a);
		}
		if (b) {
			sc->add_child(b);
		}
		split_node->set_split_offset(int(p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_offset", p_node), 0)));
		return split_node;
	}

	const int leaf_id = int(p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_leaf_id", p_node), 0));
	String content_type = p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_content_type", p_node), String());
	if (content_type.is_empty()) {
		// Legacy sessions stored an opaque content descriptor string.
		content_type = "scene";
	}
	WorkspaceLeafNode *leaf = _create_leaf(leaf_id, content_type);
	next_leaf_id = MAX(next_leaf_id, leaf_id + 1);
	if (leaf && leaf->get_leaf_content()) {
		const String layout_section = leaf_layout_section(leaf_id);
		leaf->get_leaf_content()->load_layout(p_config, layout_section);
	}
	return leaf;
}

void EditorSceneWorkspace::_clear_tree() {
	leaves.clear();
	while (get_child_count(false) > 0) {
		Node *child = get_child(0, false);
		remove_child(child);
		memdelete(child);
	}
}

void EditorSceneWorkspace::restore_from_config(const Ref<ConfigFile> &p_config) {
	ERR_FAIL_COND(p_config.is_null());
	if (!has_workspace_session(p_config)) {
		return;
	}

	const int root_node = int(p_config->get_value(WORKSPACE_CONFIG_SECTION, "root_node", 0));
	const int node_count = int(p_config->get_value(WORKSPACE_CONFIG_SECTION, "node_count", 0));

	_clear_tree();

	HashSet<int> visited;
	Control *new_root = _restore_node_from_config(p_config, root_node, node_count, visited);
	ERR_FAIL_NULL(new_root);
	add_child(new_root);

	ERR_FAIL_COND(leaves.is_empty());
	int saved_focus = int(p_config->get_value(WORKSPACE_CONFIG_SECTION, "focused_leaf_id", leaves[0]->get_leaf_id()));
	if (!get_leaf_by_id(saved_focus)) {
		saved_focus = leaves[0]->get_leaf_id();
	}
	set_focused_leaf(saved_focus);
	queue_sort();
}

EditorSceneWorkspace::EditorSceneWorkspace() {
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
	set_mouse_filter(Control::MOUSE_FILTER_PASS);
}
