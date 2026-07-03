/**************************************************************************/
/*  editor_scene_workspace.cpp                                            */
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

#include "editor_scene_workspace.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "editor/editor_data.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/scene/editor_scene_tabs.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/label.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/texture_rect.h"

void EditorScenePane::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			if (focus_frame) {
				focus_frame->add_theme_style_override(SceneStringName(panel), get_theme_stylebox(SNAME("Focus"), EditorStringName(EditorStyles)));
			}
			if (preview_placeholder) {
				preview_placeholder->add_theme_style_override(SceneStringName(panel), get_theme_stylebox(SceneStringName(panel), SNAME("Panel")));
			}
		} break;
	}
}

void EditorScenePane::_pane_gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT) {
		EditorNode::get_singleton()->focus_pane(pane_index);
	}
}

void EditorScenePane::_pane_focus_entered() {
	EditorNode::get_singleton()->focus_pane(pane_index);
}

void EditorScenePane::set_focused_visual(bool p_focused) {
	if (!focus_frame) {
		return;
	}

	if (p_focused) {
		focus_frame->add_theme_style_override(SceneStringName(panel), get_theme_stylebox(SNAME("Focus"), EditorStringName(EditorStyles)));
	} else {
		focus_frame->remove_theme_style_override(SceneStringName(panel));
	}
}

void EditorScenePane::set_preview_mode(bool p_show_live_preview, bool p_show_3d_placeholder, const String &p_scene_name, const Ref<Texture2D> &p_icon) {
	if (preview_container) {
		preview_container->set_visible(p_show_live_preview);
	}
	if (preview_placeholder) {
		preview_placeholder->set_visible(p_show_3d_placeholder);
	}
	if (preview_placeholder_label) {
		if (p_show_3d_placeholder) {
			preview_placeholder_label->set_text(vformat(TTR("%s\nFocus to edit 3D scene"), p_scene_name));
		}
	}
	if (preview_placeholder_icon) {
		preview_placeholder_icon->set_texture(p_icon);
	}
}

void EditorScenePane::_fit_content_child(Control *p_child) {
	if (!p_child) {
		return;
	}
	p_child->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
}

void EditorScenePane::setup(int p_pane_index) {
	pane_index = p_pane_index;
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
	add_theme_constant_override("separation", 0);

	focus_frame = memnew(PanelContainer);
	focus_frame->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	focus_frame->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	focus_frame->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	add_child(focus_frame);

	VBoxContainer *inner = memnew(VBoxContainer);
	inner->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	inner->add_theme_constant_override("separation", 0);
	focus_frame->add_child(inner);

	scene_tabs = memnew(EditorSceneTabs(p_pane_index));
	inner->add_child(scene_tabs);

	content_host = memnew(Control);
	content_host->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	content_host->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	content_host->set_mouse_filter(Control::MOUSE_FILTER_PASS);
	inner->add_child(content_host);

	preview_container = memnew(SubViewportContainer);
	preview_container->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	preview_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	preview_container->set_stretch(true);
	preview_container->hide();
	content_host->add_child(preview_container);
	_fit_content_child(preview_container);

	preview_placeholder = memnew(PanelContainer);
	preview_placeholder->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	preview_placeholder->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	preview_placeholder->hide();
	content_host->add_child(preview_placeholder);
	_fit_content_child(preview_placeholder);

	VBoxContainer *placeholder_vb = memnew(VBoxContainer);
	placeholder_vb->set_alignment(BoxContainer::ALIGNMENT_CENTER);
	preview_placeholder->add_child(placeholder_vb);

	preview_placeholder_icon = memnew(TextureRect);
	preview_placeholder_icon->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	preview_placeholder_icon->set_custom_minimum_size(Size2(64, 64) * EDSCALE);
	placeholder_vb->add_child(preview_placeholder_icon);

	preview_placeholder_label = memnew(Label);
	preview_placeholder_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	placeholder_vb->add_child(preview_placeholder_label);

	connect(SceneStringName(gui_input), callable_mp(this, &EditorScenePane::_pane_gui_input));
	connect(SceneStringName(focus_entered), callable_mp(this, &EditorScenePane::_pane_focus_entered));
	scene_tabs->connect(SceneStringName(gui_input), callable_mp(this, &EditorScenePane::_pane_gui_input));
}

EditorScenePane::EditorScenePane() {
	set_mouse_filter(Control::MOUSE_FILTER_PASS);
	set_focus_mode(Control::FOCUS_ALL);
}

void EditorSceneWorkspace::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			if (split) {
				split->add_theme_constant_override("separation", 0);
			}
		} break;
	}
}

void EditorSceneWorkspace::_configure_pane_layout(EditorScenePane *p_pane, bool p_in_split) {
	ERR_FAIL_NULL(p_pane);
	p_pane->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	p_pane->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	if (p_in_split) {
		p_pane->set_anchors_preset(Control::PRESET_TOP_LEFT);
		p_pane->set_offsets_preset(Control::PRESET_TOP_LEFT);
	} else {
		p_pane->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	}
}

void EditorSceneWorkspace::_ensure_split_offset() {
	if (!split || panes.size() < 2) {
		return;
	}

	const Size2 size = split->get_size();
	const int axis = split_vertical ? size.height : size.width;
	if (axis <= 0) {
		if (!is_connected(SceneStringName(resized), callable_mp(this, &EditorSceneWorkspace::_ensure_split_offset))) {
			connect(SceneStringName(resized), callable_mp(this, &EditorSceneWorkspace::_ensure_split_offset), CONNECT_ONE_SHOT);
		}
		return;
	}

	split->set_split_offset(axis / 2);
}

void EditorSceneWorkspace::_create_pane(int p_index) {
	EditorScenePane *pane = memnew(EditorScenePane);
	pane->setup(p_index);
	panes.push_back(pane);
	if (panes.size() == 1) {
		add_child(pane);
		_configure_pane_layout(pane, false);
	} else {
		_configure_pane_layout(pane, true);
		split->add_child(pane);
	}
}

void EditorSceneWorkspace::_bind_methods() {
	ADD_SIGNAL(MethodInfo("pane_focus_requested", PropertyInfo(Variant::INT, "pane_index")));
}

EditorSceneWorkspace *EditorSceneWorkspace::create_single_pane_workspace() {
	EditorSceneWorkspace *workspace = memnew(EditorSceneWorkspace);
	workspace->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	workspace->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	workspace->_create_pane(0);
	workspace->update_focus_visuals();
	return workspace;
}

void EditorSceneWorkspace::split_workspace(bool p_vertical) {
	if (is_split()) {
		split_vertical = p_vertical;
		split->set_vertical(p_vertical);
		return;
	}

	split_vertical = p_vertical;
	remove_child(panes[0]);
	_configure_pane_layout(panes[0], true);

	split = memnew(SplitContainer);
	split->set_vertical(p_vertical);
	split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	split->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	split->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	add_child(split);

	split->add_child(panes[0]);
	_create_pane(1);
	update_focus_visuals();
	callable_mp(this, &EditorSceneWorkspace::_ensure_split_offset).call_deferred();
}

void EditorSceneWorkspace::unsplit_workspace() {
	ERR_FAIL_COND(!is_split());

	EditorScenePane *pane_1 = panes[1];
	split->remove_child(pane_1);
	split->remove_child(panes[0]);
	remove_child(split);
	memdelete(split);
	split = nullptr;

	add_child(panes[0]);
	_configure_pane_layout(panes[0], false);

	panes.remove_at(1);
	memdelete(pane_1);
	update_focus_visuals();
}

void EditorSceneWorkspace::set_focused_pane(int p_pane) {
	ERR_FAIL_INDEX(p_pane, panes.size());
	focused_pane = p_pane;
	update_focus_visuals();
}

EditorScenePane *EditorSceneWorkspace::get_pane(int p_pane) const {
	ERR_FAIL_INDEX_V(p_pane, panes.size(), nullptr);
	return panes[p_pane];
}

void EditorSceneWorkspace::update_focus_visuals() {
	for (int i = 0; i < panes.size(); i++) {
		panes[i]->set_focused_visual(i == focused_pane);
	}
}

void EditorSceneWorkspace::save_to_config(const Ref<ConfigFile> &p_config, const EditorData &p_data, const EditorSceneWorkspace *p_workspace) {
	ERR_FAIL_COND(p_config.is_null());
	ERR_FAIL_NULL(p_workspace);

	const int pane_count = p_workspace->get_pane_count();
	p_config->set_value(WORKSPACE_CONFIG_SECTION, "pane_count", pane_count);
	p_config->set_value(WORKSPACE_CONFIG_SECTION, "split_vertical", p_workspace->is_split_vertical());
	if (p_workspace->split) {
		p_config->set_value(WORKSPACE_CONFIG_SECTION, "split_offset", p_workspace->split->get_split_offset());
	} else {
		p_config->set_value(WORKSPACE_CONFIG_SECTION, "split_offset", 0);
	}
	p_config->set_value(WORKSPACE_CONFIG_SECTION, "focused_pane", p_workspace->get_focused_pane());

	for (int p = 0; p < pane_count; p++) {
		PackedStringArray scenes;
		const Vector<int> indices = p_data.get_pane_scene_indices(p);
		for (int idx : indices) {
			const String path = p_data.get_scene_path(idx);
			if (!path.is_empty()) {
				scenes.push_back(path);
			}
		}
		p_config->set_value(WORKSPACE_CONFIG_SECTION, vformat("pane_%d_scenes", p), scenes);

		const int current = p_data.get_pane_current_scene(p);
		const String current_path = current >= 0 ? p_data.get_scene_path(current) : String();
		p_config->set_value(WORKSPACE_CONFIG_SECTION, vformat("pane_%d_current", p), current_path);
	}
}

bool EditorSceneWorkspace::has_workspace_session(const Ref<ConfigFile> &p_config) {
	ERR_FAIL_COND_V(p_config.is_null(), false);
	if (!p_config->has_section(WORKSPACE_CONFIG_SECTION)) {
		return false;
	}
	for (int p = 0; p < 2; p++) {
		const String key = vformat("pane_%d_scenes", p);
		if (p_config->has_section_key(WORKSPACE_CONFIG_SECTION, key)) {
			const PackedStringArray scenes = p_config->get_value(WORKSPACE_CONFIG_SECTION, key);
			if (!scenes.is_empty()) {
				return true;
			}
		}
	}
	return false;
}

int EditorSceneWorkspace::get_saved_pane_count(const Ref<ConfigFile> &p_config) {
	ERR_FAIL_COND_V(p_config.is_null(), 1);
	if (!p_config->has_section_key(WORKSPACE_CONFIG_SECTION, "pane_count")) {
		return 1;
	}
	return p_config->get_value(WORKSPACE_CONFIG_SECTION, "pane_count");
}

bool EditorSceneWorkspace::get_saved_split_vertical(const Ref<ConfigFile> &p_config) {
	ERR_FAIL_COND_V(p_config.is_null(), false);
	if (!p_config->has_section_key(WORKSPACE_CONFIG_SECTION, "split_vertical")) {
		return false;
	}
	return p_config->get_value(WORKSPACE_CONFIG_SECTION, "split_vertical");
}

int EditorSceneWorkspace::get_saved_split_offset(const Ref<ConfigFile> &p_config) {
	ERR_FAIL_COND_V(p_config.is_null(), 0);
	if (!p_config->has_section_key(WORKSPACE_CONFIG_SECTION, "split_offset")) {
		return 0;
	}
	return p_config->get_value(WORKSPACE_CONFIG_SECTION, "split_offset");
}

int EditorSceneWorkspace::get_saved_focused_pane(const Ref<ConfigFile> &p_config) {
	ERR_FAIL_COND_V(p_config.is_null(), 0);
	if (!p_config->has_section_key(WORKSPACE_CONFIG_SECTION, "focused_pane")) {
		return 0;
	}
	return p_config->get_value(WORKSPACE_CONFIG_SECTION, "focused_pane");
}

PackedStringArray EditorSceneWorkspace::get_saved_pane_scenes(const Ref<ConfigFile> &p_config, int p_pane) {
	ERR_FAIL_COND_V(p_config.is_null(), PackedStringArray());
	const String key = vformat("pane_%d_scenes", p_pane);
	if (!p_config->has_section_key(WORKSPACE_CONFIG_SECTION, key)) {
		return PackedStringArray();
	}
	return p_config->get_value(WORKSPACE_CONFIG_SECTION, key);
}

String EditorSceneWorkspace::get_saved_pane_current(const Ref<ConfigFile> &p_config, int p_pane) {
	ERR_FAIL_COND_V(p_config.is_null(), String());
	const String key = vformat("pane_%d_current", p_pane);
	if (!p_config->has_section_key(WORKSPACE_CONFIG_SECTION, key)) {
		return String();
	}
	return p_config->get_value(WORKSPACE_CONFIG_SECTION, key);
}

EditorSceneWorkspace::EditorSceneWorkspace() {
	set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
}
