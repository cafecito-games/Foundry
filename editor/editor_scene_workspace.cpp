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

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/resource_loader.h"
#include "editor/editor_data.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/editor_script_leaf.h"
#include "editor/editor_workspace_leaf_content.h"
#include "editor/script/script_editor_view.h"
#include "editor/doc/editor_help.h"
#include "editor/themes/editor_scale.h"
#include "editor/workspace/help_tab.h"
#include "editor/workspace/scene_tab.h"
#include "editor/workspace/workspace_pane.h"
#include "editor/workspace/workspace_tab_registry.h"
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

void EditorSceneWorkspace::_collapse_if_empty(int p_leaf_id) {
	WorkspaceLeafNode *leaf = get_leaf_by_id(p_leaf_id);
	if (!leaf) {
		return;
	}
	WorkspacePane *pane = leaf->get_workspace_pane();
	if (!pane) {
		return;
	}
	// Collapse the emptied pane as long as another leaf remains. If it merges into
	// a script-leaf sibling, collapse() keeps focus on a scene tile.
	if (pane->get_tab_count() == 0 && !pane->has_bridge_content() && get_leaf_count() > 1) {
		collapse(leaf);
	}
}

void EditorSceneWorkspace::collapse_if_empty_deferred(int p_leaf_id) {
	callable_mp(this, &EditorSceneWorkspace::_collapse_if_empty).call_deferred(p_leaf_id);
}

void EditorSceneWorkspace::reconcile_empty_leaves() {
	// Iterate a snapshot because collapse() mutates `leaves`; re-check membership and
	// the leaf count each step since collapsing can free a leaf and must never drop
	// the workspace below its sole/default pane.
	const Vector<WorkspaceLeafNode *> snapshot = leaves;
	for (WorkspaceLeafNode *leaf : snapshot) {
		if (get_leaf_count() <= 1) {
			break;
		}
		if (!leaves.has(leaf)) {
			continue;
		}
		WorkspacePane *pane = leaf->get_workspace_pane();
		if (!pane) {
			continue;
		}
		if (pane->get_tab_count() == 0 && !pane->has_bridge_content()) {
			collapse(leaf);
		}
	}
}

WorkspaceLeafNode *EditorSceneWorkspace::handle_tab_drop(int p_source_pane_id, int p_source_tab_index, WorkspaceLeafNode *p_target_leaf, TileDropRegion p_region) {
	ERR_FAIL_NULL_V(p_target_leaf, nullptr);
	ERR_FAIL_COND_V(!leaves.has(p_target_leaf), nullptr);

	WorkspaceLeafNode *source_leaf = get_leaf_by_id(p_source_pane_id);
	ERR_FAIL_NULL_V(source_leaf, nullptr);
	WorkspacePane *source_pane = source_leaf->get_workspace_pane();
	ERR_FAIL_NULL_V(source_pane, nullptr);
	ERR_FAIL_INDEX_V(p_source_tab_index, source_pane->get_tab_count(), nullptr);

	const WorkspaceTab source_tab = source_pane->get_tab(p_source_tab_index);
	const bool is_scene_tab = source_tab.get_type_id() == StringName("scene");

	// A center drop onto the same pane is a no-op; intra-pane reordering is handled
	// by the tab strip, not the rosette overlay.
	if (p_region == DROP_CENTER && source_leaf == p_target_leaf) {
		return nullptr;
	}

	// A scene tab bridges through the destination pane's scene tile; a script-only
	// pane has none, so a center drop there would drop the scene from the model.
	// Reject it (an edge drop still works: it splits into a fresh scene pane).
	if (p_region == DROP_CENTER && is_scene_tab) {
		WorkspacePane *target_pane = p_target_leaf->get_workspace_pane();
		if (!target_pane || !target_pane->is_scene_pane()) {
			return nullptr;
		}
	}

	WorkspaceLeafNode *dest_leaf = p_target_leaf;
	if (p_region != DROP_CENTER) {
		const bool vertical = p_region == DROP_TOP || p_region == DROP_BOTTOM;
		const SplitSide side = (p_region == DROP_LEFT || p_region == DROP_TOP) ? SPLIT_SIDE_FIRST : SPLIT_SIDE_SECOND;
		const StringName content_type = is_scene_tab ? StringName("scene") : StringName("script");
		dest_leaf = split_with_content(p_target_leaf, vertical, side, content_type);
		ERR_FAIL_NULL_V(dest_leaf, nullptr);
	}

	const int dest_leaf_id = dest_leaf->get_leaf_id();

	if (is_scene_tab) {
		ERR_FAIL_NULL_V(editor_data, nullptr);
		const int scene_idx = SceneTabType::find_scene_index(*editor_data, source_tab);
		if (scene_idx < 0) {
			return nullptr;
		}
		// Route scene-tab moves through EditorData membership so the scene tile
		// ownership stays canonical, then rebuild every pane's scene tabs.
		editor_data->set_scene_tile(scene_idx, dest_leaf_id);
		editor_data->set_tile_current_scene(dest_leaf_id, scene_idx);
		sync_scene_tabs_from_editor_data();
	} else {
		// Generic move: take_tab captures the type payload and removes it from the
		// source pane; add_tab appends it to the destination. Never prompts.
		WorkspaceTab taken = source_pane->take_tab(p_source_tab_index);
		WorkspacePane *dest_pane = dest_leaf->get_workspace_pane();
		ERR_FAIL_NULL_V(dest_pane, nullptr);
		dest_pane->add_tab(taken);
		// add_tab only auto-activates the first tab in a pane; a tab dropped into a
		// pane that already has tabs would otherwise stay inactive and unmounted,
		// unlike a scene move which makes the moved scene current. Activate it so
		// the dropped tab is the one shown, matching the scene path.
		dest_pane->set_active_tab(dest_pane->get_tab_count() - 1);
	}

	if (source_leaf != dest_leaf) {
		collapse_if_empty_deferred(p_source_pane_id);
	}
	return dest_leaf;
}

WorkspaceLeafNode *EditorSceneWorkspace::handle_tab_strip_drop(int p_source_pane_id, int p_source_tab_index, int p_dest_pane_id, int p_dest_index) {
	WorkspaceLeafNode *source_leaf = get_leaf_by_id(p_source_pane_id);
	ERR_FAIL_NULL_V(source_leaf, nullptr);
	WorkspacePane *source_pane = source_leaf->get_workspace_pane();
	ERR_FAIL_NULL_V(source_pane, nullptr);
	WorkspaceLeafNode *dest_leaf = get_leaf_by_id(p_dest_pane_id);
	ERR_FAIL_NULL_V(dest_leaf, nullptr);
	WorkspacePane *dest_pane = dest_leaf->get_workspace_pane();
	ERR_FAIL_NULL_V(dest_pane, nullptr);
	ERR_FAIL_INDEX_V(p_source_tab_index, source_pane->get_tab_count(), nullptr);

	// A same-pane strip drop is an ordinary intra-pane reorder; the tab strip
	// already handles this via active_tab_rearranged, but route defensively so a
	// direct model caller still lands the tab at the requested index.
	if (source_leaf == dest_leaf) {
		if (source_pane->get_tab_count() > 0) {
			source_pane->move_tab(p_source_tab_index, CLAMP(p_dest_index, 0, source_pane->get_tab_count() - 1));
		}
		return dest_leaf;
	}

	const WorkspaceTab source_tab = source_pane->get_tab(p_source_tab_index);
	const bool is_scene_tab = source_tab.get_type_id() == StringName("scene");

	if (is_scene_tab) {
		ERR_FAIL_NULL_V(editor_data, nullptr);
		// A scene tab bridges through the destination pane's scene tile; a
		// script-only pane has none, so refuse rather than drop it from the model.
		if (!dest_pane->is_scene_pane()) {
			return nullptr;
		}
		const int scene_idx = SceneTabType::find_scene_index(*editor_data, source_tab);
		if (scene_idx < 0) {
			return nullptr;
		}
		// Translate the requested strip insertion index into a destination scene-tab
		// ordinal before the scene is moved in. p_dest_index is a full strip position,
		// but EditorData only orders scene tabs (a synced pane groups its scene tabs
		// ahead of script tabs), so the ordinal is the count of scene tabs preceding
		// the insertion point. Computing it in the pre-drop frame -- and reordering
		// through EditorData rather than the pane's tab vector -- keeps the order
		// canonical for mixed scene/script panes, where a raw strip index would not
		// map to a scene ordinal and would leave EditorData stale.
		const int pre_slot = CLAMP(p_dest_index, 0, dest_pane->get_tab_count());
		int scene_ordinal = 0;
		for (int i = 0; i < pre_slot; i++) {
			if (dest_pane->get_tab(i).get_type_id() == StringName("scene")) {
				scene_ordinal++;
			}
		}

		// Move scene-tile ownership, then rebuild every pane's scene tabs so the
		// moved scene materializes in the destination before it is reordered.
		editor_data->set_scene_tile(scene_idx, p_dest_pane_id);
		editor_data->set_tile_current_scene(p_dest_pane_id, scene_idx);
		sync_scene_tabs_from_editor_data();

		// move_tab's scene path reorders through EditorData; the moved scene's current
		// tab index equals its scene ordinal (scenes are grouped first), and the
		// target ordinal is bounded to the scene-tab count so it never falls through
		// to a visual-only reorder.
		const int landed = dest_pane->find_scene_tab_index(scene_idx);
		if (landed >= 0 && landed != scene_ordinal) {
			dest_pane->move_tab(landed, scene_ordinal);
		}
	} else {
		// Generic move: take_tab captures the type payload and removes it from the
		// source pane; insert_tab lands it at the hovered index and activates it so
		// the dropped tab is the one shown (matching the scene path making its scene
		// current).
		WorkspaceTab taken = source_pane->take_tab(p_source_tab_index);
		const int clamped = CLAMP(p_dest_index, 0, dest_pane->get_tab_count());
		dest_pane->insert_tab(clamped, taken);
		dest_pane->set_active_tab(clamped);
	}

	collapse_if_empty_deferred(p_source_pane_id);
	return dest_leaf;
}

WorkspaceLeafNode *EditorSceneWorkspace::handle_scene_drop(int p_scene_idx, WorkspaceLeafNode *p_target_leaf, TileDropRegion p_region) {
	ERR_FAIL_NULL_V(p_target_leaf, nullptr);
	ERR_FAIL_NULL_V(editor_data, nullptr);
	ERR_FAIL_INDEX_V(p_scene_idx, editor_data->get_edited_scene_count(), nullptr);
	ERR_FAIL_COND_V(!leaves.has(p_target_leaf), nullptr);

	const int source_tile_id = editor_data->get_scene_tile(p_scene_idx);

	// Scenes are always represented as scene tabs in their owning pane; resolve the
	// tab index and delegate to the generic path, syncing once if the pane's tab
	// strip has not been materialized yet.
	for (int attempt = 0; attempt < 2; attempt++) {
		WorkspaceLeafNode *source_leaf = get_leaf_by_id(source_tile_id);
		WorkspacePane *source_pane = source_leaf ? source_leaf->get_workspace_pane() : nullptr;
		const int tab_index = source_pane ? source_pane->find_scene_tab_index(p_scene_idx) : -1;
		if (tab_index >= 0) {
			return handle_tab_drop(source_tile_id, tab_index, p_target_leaf, p_region);
		}
		if (attempt == 0) {
			sync_scene_tabs_from_editor_data();
		}
	}
	return nullptr;
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

	const StringName initial_content_type = p_content_type == StringName("script") ? StringName("script") : StringName("scene");
	WorkspacePane *pane = memnew(WorkspacePane);
	pane->setup(p_leaf_id, p_editor_selection, p_editor_data, initial_content_type);
	leaf->set_leaf_content(pane);

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

WorkspacePane *WorkspaceLeafNode::get_workspace_pane() const {
	if (leaf_content && leaf_content->get_content_type() == StringName("pane")) {
		return Object::cast_to<WorkspacePane>(leaf_content->get_root_control());
	}
	return nullptr;
}

ScenePaneTile *WorkspaceLeafNode::get_pane_tile() const {
	WorkspacePane *pane = get_workspace_pane();
	if (pane) {
		pane->sync_from_editor_data();
		return pane->get_scene_tile();
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
	const StringName initial_content_type = p_content_type == StringName("script") ? StringName("script") : StringName("scene");
	WorkspacePane *pane = memnew(WorkspacePane);
	pane->setup(p_leaf_id, editor_selection, editor_data, initial_content_type);
	pane->set_workspace(this);
	return pane;
}

void EditorSceneWorkspace::_mount_leaf_content(WorkspaceLeafNode *p_leaf, WorkspaceLeafContent *p_content) {
	ERR_FAIL_NULL(p_leaf);
	p_leaf->set_leaf_content(p_content);
}

WorkspaceLeafNode *EditorSceneWorkspace::_create_leaf(int p_leaf_id, const StringName &p_content_type) {
	ERR_FAIL_NULL_V(editor_data, nullptr);
	WorkspaceLeafNode *leaf = WorkspaceLeafNode::create(p_leaf_id, editor_selection, editor_data, p_content_type);
	if (WorkspacePane *pane = leaf->get_workspace_pane()) {
		pane->set_workspace(this);
	}
	leaves.push_back(leaf);
	if (!restoring_from_config) {
		emit_signal(SNAME("leaf_added"), p_leaf_id);
	}
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
	return split_with_content(p_leaf, p_vertical, p_side, StringName("scene"));
}

WorkspaceLeafNode *EditorSceneWorkspace::split_with_content(WorkspaceLeafNode *p_leaf, bool p_vertical, SplitSide p_side, const StringName &p_content_type) {
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

	WorkspaceLeafNode *new_leaf = _create_leaf(next_leaf_id++, p_content_type);
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

WorkspaceLeafNode *EditorSceneWorkspace::get_script_leaf() const {
	if (WorkspaceLeafNode *focused = get_focused_script_leaf()) {
		return focused;
	}
	Vector<WorkspaceLeafNode *> script_leaves = get_script_leaves();
	return script_leaves.is_empty() ? nullptr : script_leaves[0];
}

Vector<WorkspaceLeafNode *> EditorSceneWorkspace::get_script_leaves() const {
	Vector<WorkspaceLeafNode *> script_leaves;
	for (WorkspaceLeafNode *leaf : leaves) {
		WorkspacePane *pane = leaf->get_workspace_pane();
		if (pane && pane->is_script_pane()) {
			script_leaves.push_back(leaf);
		}
	}
	return script_leaves;
}

WorkspaceLeafNode *EditorSceneWorkspace::get_focused_script_leaf() const {
	WorkspaceLeafNode *focused = get_focused_leaf();
	if (!focused) {
		return nullptr;
	}
	WorkspacePane *pane = focused->get_workspace_pane();
	if (!pane) {
		return nullptr;
	}
	if (pane->is_script_pane() && pane->get_script_leaf()) {
		return focused;
	}
	const int active_tab = pane->get_active_tab_index();
	if (active_tab >= 0 && pane->get_tab(active_tab).get_type_id() == StringName("script") && pane->get_script_leaf()) {
		return focused;
	}
	return nullptr;
}

WorkspaceLeafNode *EditorSceneWorkspace::find_script_leaf_for_path(const String &p_script_path) const {
	if (p_script_path.is_empty()) {
		return nullptr;
	}
	for (WorkspaceLeafNode *leaf : get_script_leaves()) {
		WorkspacePane *pane = leaf->get_workspace_pane();
		ScriptLeaf *script_leaf = pane ? pane->get_script_leaf() : nullptr;
		if (!script_leaf) {
			continue;
		}
		if (ScriptEditorView *view = script_leaf->get_script_editor_view()) {
			if (view->get_open_editor_for_path(p_script_path)) {
				return leaf;
			}
		}
		if (script_leaf->get_script_path() == p_script_path) {
			return leaf;
		}
	}
	return nullptr;
}

void EditorSceneWorkspace::resolve_script_leaf_associated_scenes(EditorData &p_editor_data) {
	for (WorkspaceLeafNode *script_leaf_node : get_script_leaves()) {
		WorkspacePane *pane = script_leaf_node->get_workspace_pane();
		ScriptLeaf *script_leaf = pane ? pane->get_script_leaf() : nullptr;
		if (!script_leaf || script_leaf->get_associated_scene_root() || script_leaf->get_associated_scene_path().is_empty()) {
			continue;
		}

		const String scene_path = script_leaf->get_associated_scene_path();
		for (int i = 0; i < p_editor_data.get_edited_scene_count(); i++) {
			if (p_editor_data.get_scene_path(i) == scene_path) {
				script_leaf->set_associated_scene_root(p_editor_data.get_edited_scene_root(i));
				break;
			}
		}
	}
}

WorkspaceLeafNode *EditorSceneWorkspace::open_script_leaf(WorkspaceLeafNode *p_source_leaf, const String &p_script_path, bool p_force_new_leaf) {
	ERR_FAIL_NULL_V(p_source_leaf, nullptr);
	ERR_FAIL_COND_V(!leaves.has(p_source_leaf), nullptr);

	WorkspaceLeafNode *target = nullptr;
	if (!p_force_new_leaf && !p_script_path.is_empty()) {
		target = find_script_leaf_for_path(p_script_path);
	}
	if (!target && !p_force_new_leaf) {
		target = get_focused_script_leaf();
	}
	if (!target) {
		target = split_with_content(p_source_leaf, false, SPLIT_SIDE_SECOND, StringName("script"));
		ERR_FAIL_NULL_V(target, nullptr);
	}

	if (!p_script_path.is_empty()) {
		WorkspacePane *target_pane = target->get_workspace_pane();
		ScriptLeaf *script_leaf = target_pane ? target_pane->get_script_leaf() : nullptr;
		if (script_leaf) {
			script_leaf->set_script_path(p_script_path);
			script_leaf->on_focus_entered();
			if (ScriptEditorView *view = script_leaf->get_script_editor_view()) {
				if (ResourceLoader::exists(p_script_path)) {
					Ref<Resource> resource = ResourceLoader::load(p_script_path);
					if (resource.is_valid()) {
						view->edit(resource, true);
					}
				}
			}
		}
	}

	if (WorkspacePane *target_pane = target->get_workspace_pane()) {
		if (ScriptLeaf *script_leaf = target_pane->get_script_leaf()) {
			Node *associated_scene = nullptr;
			if (ScenePaneTile *source_tile = p_source_leaf->get_pane_tile()) {
				associated_scene = source_tile->get_current_scene_root();
			}
			script_leaf->set_associated_scene_root(associated_scene);
		}
	}
	return target;
}

WorkspaceLeafNode *EditorSceneWorkspace::_find_leaf_hosting_type(const StringName &p_type_id) const {
	for (WorkspaceLeafNode *leaf : leaves) {
		WorkspacePane *pane = leaf->get_workspace_pane();
		if (!pane) {
			continue;
		}
		for (int i = 0; i < pane->get_tab_count(); i++) {
			if (pane->get_tab(i).get_type_id() == p_type_id) {
				return leaf;
			}
		}
	}
	return nullptr;
}

WorkspaceLeafNode *EditorSceneWorkspace::open_help_tab(WorkspaceLeafNode *p_source_leaf, const String &p_topic, bool p_force_new_leaf) {
	ERR_FAIL_NULL_V(p_source_leaf, nullptr);
	ERR_FAIL_COND_V(!leaves.has(p_source_leaf), nullptr);

	const String class_key = HelpTabType::class_key_for_topic(p_topic);
	if (class_key.is_empty()) {
		return nullptr;
	}

	WorkspaceTabRegistry &registry = WorkspacePane::get_shared_tab_registry();
	HelpTabType *help_type = static_cast<HelpTabType *>(registry.find_type(StringName("help")));
	ERR_FAIL_NULL_V(help_type, nullptr);

	// Only a topic with a member anchor needs scrolling within the page; a bare
	// class name lands at the top, which mount()/go_to_class already does.
	const bool is_deep_topic = p_topic.contains(":");

	// Reveal an already-open page for this class instead of duplicating it. The
	// canonical index is process-global and is not rebuilt on workspace restore, so
	// an entry can be stale (its pane/tab reused by a different tab). Confirm the
	// located tab is still this help page before revealing it; otherwise fall
	// through and create a fresh tab.
	WorkspaceTab existing_tab;
	WorkspaceTabLocation existing_location;
	if (registry.find_canonical(StringName("help"), class_key, existing_tab, existing_location) && existing_location.is_valid()) {
		WorkspaceLeafNode *leaf = get_leaf_by_id(existing_location.pane_id);
		WorkspacePane *pane = leaf ? leaf->get_workspace_pane() : nullptr;
		if (pane && existing_location.tab_index < pane->get_tab_count()) {
			const WorkspaceTab &located = pane->get_tab(existing_location.tab_index);
			if (located.get_type_id() == StringName("help") && located.get_resource_key() == class_key) {
				pane->set_active_tab(existing_location.tab_index);
				request_leaf_focus(existing_location.pane_id);
				if (is_deep_topic && EditorHelp::get_doc_data()) {
					if (EditorHelp *help = help_type->get_mounted_help(existing_tab.get_stable_id())) {
						help->go_to_help(p_topic);
					}
				}
				return leaf;
			}
		}
		// The entry is stale (its pane/tab no longer holds this page). Drop it so the
		// create path below re-registers a fresh canonical location instead of
		// add_tab's insert_canonical treating the stale key as already present.
		registry.remove_canonical(StringName("help"), class_key);
	}

	// Otherwise pick a target pane: reuse a pane already hosting help so pages
	// stack together, else fall back to the focused (or source) leaf's pane.
	WorkspaceLeafNode *target = nullptr;
	if (!p_force_new_leaf) {
		target = _find_leaf_hosting_type(StringName("help"));
	}
	if (!target) {
		target = get_focused_leaf();
	}
	if (!target || !leaves.has(target)) {
		target = p_source_leaf;
	}
	WorkspacePane *pane = target->get_workspace_pane();
	ERR_FAIL_NULL_V(pane, nullptr);

	const int stable_id = registry.allocate_stable_id();
	pane->add_tab(help_type->make_tab(class_key, stable_id));
	for (int i = pane->get_tab_count() - 1; i >= 0; i--) {
		if (pane->get_tab(i).get_stable_id() == stable_id) {
			pane->set_active_tab(i);
			break;
		}
	}
	request_leaf_focus(target->get_leaf_id());

	if (is_deep_topic && EditorHelp::get_doc_data()) {
		if (EditorHelp *help = help_type->get_mounted_help(stable_id)) {
			help->go_to_help(p_topic);
		}
	}
	return target;
}

WorkspaceLeafNode *EditorSceneWorkspace::open_text_tab(WorkspaceLeafNode *p_source_leaf, const String &p_path, bool p_force_new_leaf) {
	ERR_FAIL_NULL_V(p_source_leaf, nullptr);
	ERR_FAIL_COND_V(!leaves.has(p_source_leaf), nullptr);
	ERR_FAIL_COND_V(p_path.is_empty(), nullptr);

	// Canonicalize to a project-relative res:// path so the tab identity is stable
	// regardless of how the caller addressed the file (a res:// FileSystem-dock open
	// vs. an absolute path from the new-file dialog). Otherwise the same file could
	// open as two independent tabs with divergent, last-save-wins documents.
	const String path = ProjectSettings::get_singleton()->localize_path(p_path);

	const StringName text_type = StringName("text");
	WorkspaceTabRegistry &registry = WorkspacePane::get_shared_tab_registry();

	// Reveal an existing tab for this file so there is only ever one tab per file.
	if (!p_force_new_leaf) {
		WorkspaceTab existing_tab;
		WorkspaceTabLocation existing_location;
		if (registry.find_canonical(text_type, path, existing_tab, existing_location)) {
			if (WorkspaceLeafNode *leaf = get_leaf_by_id(existing_location.pane_id)) {
				if (WorkspacePane *pane = leaf->get_workspace_pane()) {
					int index = existing_location.tab_index;
					if (index < 0 || index >= pane->get_tab_count() || pane->get_tab(index).get_stable_id() != existing_tab.get_stable_id()) {
						index = -1;
						for (int i = 0; i < pane->get_tab_count(); i++) {
							if (pane->get_tab(i).get_stable_id() == existing_tab.get_stable_id()) {
								index = i;
								break;
							}
						}
					}
					if (index >= 0) {
						pane->set_active_tab(index);
						request_leaf_focus(leaf->get_leaf_id());
						return leaf;
					}
				}
			}
		}
	}

	// Choose a target pane: reuse one that already hosts text tabs (so multiple
	// files stack in one strip), else split beside the source leaf for a new one.
	WorkspaceLeafNode *target = nullptr;
	if (!p_force_new_leaf) {
		for (WorkspaceLeafNode *leaf : leaves) {
			WorkspacePane *pane = leaf->get_workspace_pane();
			if (!pane) {
				continue;
			}
			bool hosts_text = false;
			for (int i = 0; i < pane->get_tab_count(); i++) {
				if (pane->get_tab(i).get_type_id() == text_type) {
					hosts_text = true;
					break;
				}
			}
			if (hosts_text) {
				target = leaf;
				break;
			}
		}
	}
	if (!target) {
		target = split_with_content(p_source_leaf, false, SPLIT_SIDE_SECOND, StringName("script"));
		ERR_FAIL_NULL_V(target, nullptr);
	}

	WorkspacePane *target_pane = target->get_workspace_pane();
	ERR_FAIL_NULL_V(target_pane, nullptr);
	WorkspaceTabType *type = registry.find_type(text_type);
	ERR_FAIL_NULL_V(type, nullptr);

	WorkspaceTab tab = type->make_tab(path, registry.allocate_stable_id());
	target_pane->add_tab(tab);
	target_pane->set_active_tab(target_pane->get_tab_count() - 1);
	request_leaf_focus(target->get_leaf_id());
	return target;
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
		// Focus must land on a scene tile; the structural successor may be a script
		// leaf (which is not a focusable scene tile). Prefer a scene tile if so.
		int focus_target = successor_leaf_id;
		if (!successor_leaf->get_pane_tile()) {
			for (WorkspaceLeafNode *candidate : leaves) {
				if (candidate->get_pane_tile()) {
					focus_target = candidate->get_leaf_id();
					break;
				}
			}
		}
		set_focused_leaf(focus_target);
		emit_signal(SNAME("leaf_focus_requested"), focus_target);
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
	if (leaf && leaf->get_pane_tile()) {
		last_focused_tile_id = p_id;
	}
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

ScenePaneTile *EditorSceneWorkspace::get_effective_focused_tile() const {
	if (ScenePaneTile *tile = get_tile_by_id(focused_leaf_id)) {
		return tile;
	}
	// Focused leaf hosts no tile (e.g. a script leaf); fall back to the last
	// focused scene tile, then to any scene tile, so scene/inspector docks stay
	// valid instead of being resolved against a null tile.
	if (ScenePaneTile *tile = get_tile_by_id(last_focused_tile_id)) {
		return tile;
	}
	Vector<ScenePaneTile *> tiles = get_tiles();
	return tiles.is_empty() ? nullptr : tiles[0];
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

void EditorSceneWorkspace::sync_scene_tabs_from_editor_data() {
	WorkspacePane::get_shared_tab_registry().clear_canonical_for_type(StringName("scene"));
	for (WorkspaceLeafNode *leaf : leaves) {
		if (WorkspacePane *pane = leaf->get_workspace_pane()) {
			// Activate only the focused pane; a non-focused pane re-mounting its
			// active scene tab must not reparent the shared scene editor into it.
			pane->sync_scene_tabs_from_editor_data(leaf->get_leaf_id() == focused_leaf_id);
		}
	}
}

// Resolve a restored scene tab to its edited-scene index by its stable path only.
// The persisted scene_history_id is reassigned every session, so it cannot identify
// a scene across a restart. Pathless (unsaved) scenes are never reopened on restart,
// so a stale unsaved tab must resolve to -1 rather than matching -- by a recycled
// history id -- an unrelated fresh scene that would then be moved into its old pane.
static int _resolve_restored_scene_index(const EditorData &p_editor_data, const WorkspaceTab &p_tab) {
	const Dictionary &payload = p_tab.get_payload();
	String path;
	if (payload.has("scene_path")) {
		path = payload["scene_path"];
	} else if (p_tab.get_resource_key().begins_with("res://")) {
		path = p_tab.get_resource_key();
	}
	if (path.is_empty()) {
		return -1;
	}
	return p_editor_data.get_edited_scene_from_path(path);
}

void EditorSceneWorkspace::restore_scene_tile_ownership_from_tabs() {
	if (!editor_data) {
		return;
	}
	for (WorkspaceLeafNode *leaf : leaves) {
		WorkspacePane *pane = leaf->get_workspace_pane();
		if (!pane || !pane->is_scene_pane()) {
			continue;
		}
		const int tile_id = leaf->get_leaf_id();
		const int active_index = pane->get_restored_active_tab_index();
		int active_scene_idx = -1;
		for (int i = 0; i < pane->get_tab_count(); i++) {
			const WorkspaceTab &tab = pane->get_tab(i);
			if (tab.get_type_id() != StringName("scene")) {
				continue;
			}
			const int scene_idx = _resolve_restored_scene_index(*editor_data, tab);
			if (scene_idx < 0) {
				continue;
			}
			editor_data->set_scene_tile(scene_idx, tile_id);
			if (i == active_index) {
				active_scene_idx = scene_idx;
			}
		}
		// Point the tile at its restored active scene so the tab sync (and the focused
		// tile's current scene) reflect the persisted active tab, not tile 0's leftover.
		if (active_scene_idx >= 0) {
			editor_data->set_tile_current_scene(tile_id, active_scene_idx);
		}
	}
}

bool EditorSceneWorkspace::focus_scene_tab(int p_scene_idx) {
	ERR_FAIL_NULL_V(editor_data, false);
	ERR_FAIL_INDEX_V(p_scene_idx, editor_data->get_edited_scene_count(), false);

	sync_scene_tabs_from_editor_data();
	const String key = SceneTabType::resource_key_for_scene(*editor_data, p_scene_idx);
	for (WorkspaceLeafNode *leaf : leaves) {
		WorkspacePane *pane = leaf->get_workspace_pane();
		if (!pane) {
			continue;
		}
		for (int i = 0; i < pane->get_tab_count(); i++) {
			if (pane->get_tab(i).get_type_id() == StringName("scene") && pane->get_tab(i).get_resource_key() == key) {
				pane->set_active_tab(i);
				set_focused_leaf(leaf->get_leaf_id());
				return true;
			}
		}
	}
	return false;
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
//
// focused_leaf_id doubles as the focused pane id (leaf_id == pane id). Each
// leaf delegates its own content to WorkspaceLeafContent::save_layout under the
// leaf_layout_section(leaf_id) section; the per-pane per-tab schema (tab_count,
// active_tab, and the tab_<i> records) is documented at WorkspacePane::save_layout.

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
	StringName initial_content_type = StringName("scene");
	if (content_type == "script") {
		initial_content_type = StringName("script");
	} else if (content_type == "pane") {
		const String layout_section = leaf_layout_section(leaf_id);
		initial_content_type = p_config->get_value(layout_section, "initial_content_type", StringName("scene"));
	} else if (content_type.is_empty()) {
		// Legacy sessions stored an opaque content descriptor string.
		initial_content_type = StringName("scene");
	}
	WorkspaceLeafNode *leaf = _create_leaf(leaf_id, initial_content_type);
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

	restoring_from_config = true;
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
	restoring_from_config = false;
}

EditorSceneWorkspace::EditorSceneWorkspace() {
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
	set_mouse_filter(Control::MOUSE_FILTER_PASS);
}
