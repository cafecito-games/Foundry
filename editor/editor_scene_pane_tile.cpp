/**************************************************************************/
/*  editor_scene_pane_tile.cpp                                            */
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

#include "editor_scene_pane_tile.h"

#include "editor/docks/inspector_dock.h"
#include "editor/docks/scene_tree_dock.h"
#include "editor/editor_scene_workspace.h"
#include "editor/editor_string_names.h"
#include "editor/scene/editor_scene_tabs.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/center_container.h"
#include "scene/gui/label.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/split_container.h"
#include "scene/gui/subviewport_container.h"
#include "scene/gui/texture_rect.h"
#include "scene/main/viewport.h"

void ScenePaneTile::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			if (preview_placeholder) {
				preview_placeholder->add_theme_style_override(SceneStringName(panel), get_theme_stylebox(SceneStringName(panel), SNAME("Panel")));
			}
		} break;
	}
}

void ScenePaneTile::_interaction_gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT) {
		_request_focus();
	}
}

void ScenePaneTile::_bind_focus_on_interaction(Control *p_control) {
	ERR_FAIL_NULL(p_control);
	p_control->connect(SceneStringName(gui_input), callable_mp(this, &ScenePaneTile::_interaction_gui_input));
}

void ScenePaneTile::_request_focus() {
	for (Node *node = get_parent(); node; node = node->get_parent()) {
		EditorSceneWorkspace *workspace = Object::cast_to<EditorSceneWorkspace>(node);
		if (workspace) {
			workspace->request_leaf_focus(tile_id);
			return;
		}
	}
}

void ScenePaneTile::input(const Ref<InputEvent> &p_event) {
	Viewport *tile_viewport = get_viewport();
	if (!tile_viewport || !is_visible_in_tree()) {
		return;
	}

	// input() ignores z-order, so a global position inside this tile's rect
	// may actually be over a control drawn on top of it (e.g. a dialog or the
	// bottom drawer). Only act when the control under the cursor really
	// belongs to this tile.
	auto cursor_is_over_this_tile = [&](const Point2 &p_global) {
		if (!get_global_rect().has_point(p_global)) {
			return false;
		}
		Control *hovered = tile_viewport->gui_get_hovered_control();
		return hovered == this || (hovered && is_ancestor_of(hovered));
	};

	// Focus-follows-drag: while a drag is under way, focus the tile the
	// cursor moves over so the drop is handled by that tile's live editor
	// (which only exists in the focused tile). request_leaf_focus() no-ops
	// once focused.
	if (tile_viewport->gui_is_dragging()) {
		Ref<InputEventMouseMotion> mm = p_event;
		if (mm.is_valid() && cursor_is_over_this_tile(mm->get_global_position())) {
			_request_focus();
		}
		return;
	}

	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT && cursor_is_over_this_tile(mb->get_global_position())) {
		_request_focus();
	}
}

void ScenePaneTile::set_focused_visual(bool p_focused) {
	if (!focus_frame) {
		return;
	}

	if (p_focused) {
		focus_frame->add_theme_style_override(SceneStringName(panel), get_theme_stylebox(SNAME("Focus"), EditorStringName(EditorStyles)));
	} else {
		focus_frame->remove_theme_style_override(SceneStringName(panel));
	}
}

void ScenePaneTile::set_preview_mode(bool p_live_2d, bool p_placeholder_3d, const String &p_scene_name, const Ref<Texture2D> &p_icon) {
	if (preview_container) {
		preview_container->set_visible(p_live_2d);
	}
	if (preview_placeholder) {
		preview_placeholder->set_visible(p_placeholder_3d);
	}
	if (preview_placeholder_label && p_placeholder_3d) {
		preview_placeholder_label->set_text(vformat(TTR("%s\nFocus to edit 3D scene"), p_scene_name));
	}
	if (preview_placeholder_icon) {
		preview_placeholder_icon->set_texture(p_icon);
	}
}

void ScenePaneTile::setup(int p_tile_id, EditorSelection *p_editor_selection, EditorData &p_editor_data) {
	tile_id = p_tile_id;
	set_process_input(true);
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
	add_theme_constant_override("separation", 0);

	scene_tabs = memnew(EditorSceneTabs(p_tile_id));
	add_child(scene_tabs);

	focus_frame = memnew(PanelContainer);
	focus_frame->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	focus_frame->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	focus_frame->set_clip_contents(true);
	add_child(focus_frame);

	body = memnew(HSplitContainer);
	body->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	body->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	focus_frame->add_child(body);

	// In-tile docks: constructed and bound per tile, never registered in the
	// global EditorDockManager slots. The false flag suppresses global
	// open-command/shortcut registration, which only makes sense for
	// manager-owned docks.
	scene_tree_dock = memnew(SceneTreeDock(p_editor_selection, p_editor_data, false));
	scene_tree_dock->set_custom_minimum_size(Size2(180, 0) * EDSCALE);
	scene_tree_dock->set_h_size_flags(Control::SIZE_FILL);
	body->add_child(scene_tree_dock);

	content_host = memnew(Control);
	content_host->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	content_host->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	content_host->set_clip_contents(true);
	content_host->set_mouse_filter(Control::MOUSE_FILTER_PASS);
	body->add_child(content_host);

	inspector_dock = memnew(InspectorDock(p_editor_data, false));
	inspector_dock->set_custom_minimum_size(Size2(180, 0) * EDSCALE);
	inspector_dock->set_h_size_flags(Control::SIZE_FILL);
	body->add_child(inspector_dock);

	preview_container = memnew(SubViewportContainer);
	preview_container->set_stretch(true);
	preview_container->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	preview_container->hide();
	content_host->add_child(preview_container);

	preview_placeholder = memnew(PanelContainer);
	preview_placeholder->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	preview_placeholder->hide();
	content_host->add_child(preview_placeholder);

	CenterContainer *placeholder_center = memnew(CenterContainer);
	placeholder_center->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	preview_placeholder->add_child(placeholder_center);

	VBoxContainer *placeholder_vb = memnew(VBoxContainer);
	placeholder_vb->set_alignment(BoxContainer::ALIGNMENT_CENTER);
	placeholder_center->add_child(placeholder_vb);

	preview_placeholder_icon = memnew(TextureRect);
	preview_placeholder_icon->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	preview_placeholder_icon->set_custom_minimum_size(Size2(64, 64) * EDSCALE);
	placeholder_vb->add_child(preview_placeholder_icon);

	preview_placeholder_label = memnew(Label);
	preview_placeholder_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	placeholder_vb->add_child(preview_placeholder_label);

	_bind_focus_on_interaction(scene_tabs);
	_bind_focus_on_interaction(scene_tree_dock);
	_bind_focus_on_interaction(inspector_dock);
	_bind_focus_on_interaction(content_host);
}

ScenePaneTile::ScenePaneTile() {
	set_mouse_filter(Control::MOUSE_FILTER_PASS);
	set_focus_mode(Control::FOCUS_ALL);
	set_clip_contents(true);
}
