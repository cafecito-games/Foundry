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
#include "editor/editor_data.h"
#include "editor/editor_scene_workspace.h"
#include "editor/editor_string_names.h"
#include "editor/editor_tile_drop_overlay.h"
#include "editor/scene/editor_scene_tabs.h"
#include "editor/scene/canvas_item_editor_plugin.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/center_container.h"
#include "scene/gui/label.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/texture_rect.h"
#include "scene/main/viewport.h"
#include "scene/3d/camera_3d.h"
#include "scene/resources/3d/world_3d.h"

void ScenePaneTile::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_RESIZED: {
			_fit_content_children();
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			if (preview_placeholder) {
				preview_placeholder->add_theme_style_override(SceneStringName(panel), get_theme_stylebox(SceneStringName(panel), SNAME("Panel")));
			}
		} break;
	}
}

void ScenePaneTile::_request_focus() {
	for (Node *node = get_parent(); node; node = node->get_parent()) {
		EditorSceneWorkspace *workspace = Object::cast_to<EditorSceneWorkspace>(node);
		if (workspace) {
			workspace->request_tile_focus(tile_id);
			return;
		}
	}
}

void ScenePaneTile::input(const Ref<InputEvent> &p_event) {
	Viewport *tile_viewport = get_viewport();
	if (!tile_viewport || !is_visible_in_tree()) {
		return;
	}

	// input() ignores z-order, so a global position inside this tile's rect may
	// actually be over a control drawn on top of it (e.g. a dock sharing the
	// bottom drawer). Only act when the control under the cursor really belongs
	// to this tile.
	auto cursor_is_over_this_tile = [&](const Point2 &p_global) {
		if (!get_global_rect().has_point(p_global)) {
			return false;
		}
		Control *hovered = tile_viewport->gui_get_hovered_control();
		return hovered == this || (hovered && is_ancestor_of(hovered));
	};

	// Focus-follows-drag: while a drag is under way, focus the tile the cursor
	// moves over so the drop is handled by that tile's live editor (which only
	// exists in the focused tile). request_tile_focus() no-ops once focused.
	if (tile_viewport->gui_is_dragging()) {
		Ref<InputEventMouseMotion> mm = p_event;
		if (mm.is_valid() && cursor_is_over_this_tile(mm->get_global_position())) {
			_request_focus();
		}
		return;
	}

	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->is_pressed() && cursor_is_over_this_tile(mb->get_global_position())) {
		_request_focus();
	}
}

void ScenePaneTile::set_focused_visual(bool p_focused) {
	if (!focus_frame) {
		return;
	}

	if (p_focused) {
		const Ref<StyleBox> focus_style = get_theme_stylebox(SNAME("Focus"), EditorStringName(EditorStyles));
		if (focus_style.is_valid()) {
			focus_frame->add_theme_style_override(SceneStringName(panel), focus_style);
		}
	} else {
		focus_frame->remove_theme_style_override(SceneStringName(panel));
	}
}

void ScenePaneTile::set_preview_mode(TilePreviewMode p_mode, const String &p_scene_name, const Ref<Texture2D> &p_icon) {
	if (preview_container) {
		preview_container->set_visible(p_mode == TilePreviewMode::LIVE_2D);
	}
	if (preview_3d_container) {
		preview_3d_container->set_visible(p_mode == TilePreviewMode::LIVE_3D && !spatial_view);
	}
	if (spatial_view) {
		spatial_view->set_visible(p_mode == TilePreviewMode::LIVE_3D);
	}
	if (canvas_view && canvas_view->get_viewport_scrollable()) {
		canvas_view->get_viewport_scrollable()->set_visible(p_mode == TilePreviewMode::LIVE_2D);
	}
	if (context_viewport_host) {
		context_viewport_host->set_visible(p_mode == TilePreviewMode::LIVE_3D);
	}
	if (preview_placeholder) {
		preview_placeholder->set_visible(p_mode == TilePreviewMode::PLACEHOLDER_3D);
	}
	if (p_mode == TilePreviewMode::PLACEHOLDER_3D && preview_placeholder_label) {
		preview_placeholder_label->set_text(vformat(TTR("%s\nFocus to edit 3D scene"), p_scene_name));
	}
	if (preview_placeholder_icon) {
		preview_placeholder_icon->set_texture(p_icon);
	}
}

void ScenePaneTile::bind_3d_preview_world(const Ref<World3D> &p_world) {
	if (preview_3d_viewport) {
		preview_3d_viewport->set_world_3d(p_world);
	}
}

void ScenePaneTile::apply_3d_preview_camera_state(const Dictionary &p_viewport_state) {
	if (!preview_3d_camera) {
		return;
	}

	const Vector3 pos = p_viewport_state.get("position", Vector3());
	const real_t x_rot = p_viewport_state.get("x_rotation", 0.35);
	const real_t y_rot = p_viewport_state.get("y_rotation", 0.5);
	const real_t distance = p_viewport_state.get("distance", 4.0);

	Transform3D camera_transform;
	camera_transform.translate_local(pos);
	camera_transform.basis.rotate(Vector3(1, 0, 0), -x_rot);
	camera_transform.basis.rotate(Vector3(0, 1, 0), -y_rot);
	camera_transform.translate_local(0, 0, distance);
	preview_3d_camera->set_transform(camera_transform);
}

void ScenePaneTile::_fit_content_child(Control *p_child) {
	if (!p_child || !content_host) {
		return;
	}
	p_child->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
}

void ScenePaneTile::_fit_content_children() {
	if (!content_host) {
		return;
	}
	for (int i = 0; i < content_host->get_child_count(); i++) {
		_fit_content_child(Object::cast_to<Control>(content_host->get_child(i)));
	}
}

void ScenePaneTile::setup(int p_tile_id, EditorSelection *p_editor_selection, EditorData &p_editor_data, bool p_register_open_commands) {
	tile_id = p_tile_id;
	set_process_input(true);
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
	add_theme_constant_override("separation", 0);

	scene_tabs = memnew(EditorSceneTabs(p_tile_id));
	add_child(scene_tabs);

	body = memnew(HSplitContainer);
	body->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	body->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	add_child(body);

	scene_tree_dock = memnew(SceneTreeDock(p_editor_selection, p_editor_data, p_register_open_commands));
	scene_tree_dock->set_custom_minimum_size(Size2(220, 0) * EDSCALE);
	body->add_child(scene_tree_dock);

	focus_frame = memnew(PanelContainer);
	focus_frame->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	focus_frame->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	focus_frame->set_clip_contents(true);
	body->add_child(focus_frame);

	content_host = memnew(Control);
	content_host->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	content_host->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	content_host->set_clip_contents(true);
	content_host->set_mouse_filter(Control::MOUSE_FILTER_PASS);
	focus_frame->add_child(content_host);

	inspector_dock = memnew(InspectorDock(p_editor_data, p_register_open_commands));
	inspector_dock->set_custom_minimum_size(Size2(220, 0) * EDSCALE);
	body->add_child(inspector_dock);

	preview_container = memnew(SubViewportContainer);
	preview_container->set_stretch(true);
	preview_container->hide();
	content_host->add_child(preview_container);

	context_viewport_host = memnew(SubViewportContainer);
	context_viewport_host->set_stretch(true);
	context_viewport_host->hide();
	context_viewport_host->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	content_host->add_child(context_viewport_host);

	preview_3d_container = memnew(SubViewportContainer);
	preview_3d_container->set_stretch(true);
	preview_3d_container->hide();
	content_host->add_child(preview_3d_container);

	preview_3d_viewport = memnew(SubViewport);
	preview_3d_viewport->set_disable_input(true);
	preview_3d_viewport->set_disable_3d(false);
	preview_3d_container->add_child(preview_3d_viewport);

	preview_3d_camera = memnew(Camera3D);
	preview_3d_camera->set_disable_gizmos(true);
	preview_3d_viewport->add_child(preview_3d_camera);
	preview_3d_camera->make_current();
	apply_3d_preview_camera_state(Dictionary());

	preview_placeholder = memnew(PanelContainer);
	preview_placeholder->hide();
	content_host->add_child(preview_placeholder);

	CenterContainer *placeholder_center = memnew(CenterContainer);
	placeholder_center->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	preview_placeholder->add_child(placeholder_center);

	VBoxContainer *placeholder_vbox = memnew(VBoxContainer);
	placeholder_vbox->set_alignment(BoxContainer::ALIGNMENT_CENTER);
	placeholder_center->add_child(placeholder_vbox);

	preview_placeholder_icon = memnew(TextureRect);
	preview_placeholder_icon->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	preview_placeholder_icon->set_custom_minimum_size(Size2(64, 64) * EDSCALE);
	placeholder_vbox->add_child(preview_placeholder_icon);

	preview_placeholder_label = memnew(Label);
	preview_placeholder_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	placeholder_vbox->add_child(preview_placeholder_label);

	// The drop overlay sits inside focus_frame (a PanelContainer) after
	// content_host, so it stacks on top of the content — including a reparented
	// main screen — while a scene tab is being dragged over this tile.
	drop_overlay = memnew(EditorTileDropOverlay);
	drop_overlay->set_owning_tile_id(p_tile_id);
	focus_frame->add_child(drop_overlay);

	_fit_content_children();
}

ScenePaneTile::ScenePaneTile() {
	set_mouse_filter(Control::MOUSE_FILTER_PASS);
	set_focus_mode(Control::FOCUS_ALL);
	set_clip_contents(true);
}
