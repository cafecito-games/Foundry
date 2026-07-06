/**************************************************************************/
/*  editor_script_leaf.cpp                                               */
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

#include "editor_script_leaf.h"

#include "core/io/config_file.h"
#include "core/io/resource_loader.h"
#include "editor/editor_scene_workspace.h"
#include "editor/editor_string_names.h"
#include "editor/script/script_editor_controller.h"
#include "editor/script/script_editor_view.h"
#include "scene/gui/label.h"
#include "scene/main/node.h"
#include "scene/main/viewport.h"

void ScriptLeaf::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			if (placeholder_label) {
				placeholder_label->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("font_color"), EditorStringName(Editor)));
			}
		} break;
	}
}

void ScriptLeaf::_ensure_script_editor_view() {
	if (script_editor_view || !surface_host) {
		return;
	}
	ScriptEditorController *controller = ScriptEditorController::get_singleton();
	if (!controller) {
		return;
	}
	controller->create_view_for_leaf(this);
	if (placeholder_label) {
		placeholder_label->hide();
	}
}

void ScriptLeaf::set_script_editor_view(ScriptEditorView *p_view) {
	script_editor_view = p_view;
	if (placeholder_label) {
		placeholder_label->set_visible(script_editor_view == nullptr);
	}
}

void ScriptLeaf::_interaction_gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT) {
		_request_focus();
	}
}

void ScriptLeaf::_request_focus() {
	request_workspace_focus();
}

void ScriptLeaf::request_workspace_focus() {
	for (Node *node = get_parent(); node; node = node->get_parent()) {
		EditorSceneWorkspace *workspace = Object::cast_to<EditorSceneWorkspace>(node);
		if (workspace) {
			for (WorkspaceLeafNode *leaf : workspace->get_leaves()) {
				if (leaf->get_leaf_content() && leaf->get_leaf_content()->get_root_control() == this) {
					workspace->request_leaf_focus(leaf->get_leaf_id());
					return;
				}
			}
			return;
		}
	}
}

void ScriptLeaf::set_associated_scene_root(Node *p_scene_root) {
	associated_scene_root_id = p_scene_root ? p_scene_root->get_instance_id() : ObjectID();
	if (p_scene_root) {
		const String scene_file_path = p_scene_root->get_scene_file_path();
		associated_scene_path = scene_file_path.is_empty() ? String(p_scene_root->get_path()) : scene_file_path;
	} else {
		associated_scene_path.clear();
	}
}

Node *ScriptLeaf::get_associated_scene_root() const {
	if (associated_scene_root_id.is_valid()) {
		return Object::cast_to<Node>(ObjectDB::get_instance(associated_scene_root_id));
	}
	return nullptr;
}

void ScriptLeaf::on_focus_entered() {
	_ensure_script_editor_view();
	if (script_editor_view && ScriptEditorController::get_singleton()) {
		ScriptEditorController::get_singleton()->set_focused_view(script_editor_view);
	}
	_request_focus();
}

void ScriptLeaf::gui_input(const Ref<InputEvent> &p_event) {
	_interaction_gui_input(p_event);
}

void ScriptLeaf::set_tab_title(const String &p_title) {
	tab_title = p_title;
	if (placeholder_label) {
		placeholder_label->set_text(tab_title);
	}
}

void ScriptLeaf::set_script_path(const String &p_path) {
	script_path = p_path;
	set_tab_title(script_path.is_empty() ? String("Script") : script_path.get_file());
}

StringName ScriptLeaf::get_content_type() const {
	return StringName("script");
}

Control *ScriptLeaf::get_root_control() const {
	return const_cast<ScriptLeaf *>(this);
}

String ScriptLeaf::get_tab_title() const {
	return tab_title;
}

Ref<Texture2D> ScriptLeaf::get_tab_icon() const {
	if (has_theme_icon(SNAME("Script"), EditorStringName(EditorIcons))) {
		return get_theme_icon(SNAME("Script"), EditorStringName(EditorIcons));
	}
	return Ref<Texture2D>();
}

EditorSceneContext *ScriptLeaf::get_scene_context() const {
	return nullptr;
}

void ScriptLeaf::save_layout(const Ref<ConfigFile> &p_config, const String &p_section) const {
	ERR_FAIL_COND(p_config.is_null());
	p_config->set_value(p_section, "tab_title", tab_title);
	p_config->set_value(p_section, "script_path", script_path);
	p_config->set_value(p_section, "associated_scene_path", associated_scene_path);
	if (script_editor_view) {
		script_editor_view->get_view_layout(p_config, p_section);
	}
}

void ScriptLeaf::load_layout(const Ref<ConfigFile> &p_config, const String &p_section) {
	ERR_FAIL_COND(p_config.is_null());
	set_tab_title(p_config->get_value(p_section, "tab_title", tab_title));
	// set_script_path derives the tab title from the file name, so apply it after
	// the stored title and only when a path was actually persisted.
	const String stored_path = p_config->get_value(p_section, "script_path", String());
	if (!stored_path.is_empty()) {
		set_script_path(stored_path);
	}
	associated_scene_path = p_config->get_value(p_section, "associated_scene_path", String());
	associated_scene_root_id = ObjectID();
	_ensure_script_editor_view();
	if (script_editor_view) {
		script_editor_view->set_view_layout(p_config, p_section);
	} else if (!stored_path.is_empty() && ResourceLoader::exists(stored_path)) {
		Ref<Resource> resource = ResourceLoader::load(stored_path);
		if (resource.is_valid()) {
			_ensure_script_editor_view();
			if (script_editor_view) {
				script_editor_view->edit(resource, false);
			}
		}
	}
}

ScriptLeaf::ScriptLeaf() {
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_h_size_flags(Control::SIZE_EXPAND_FILL);

	// Host for the per-leaf ScriptEditorView.
	surface_host = memnew(Control);
	surface_host->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	surface_host->set_mouse_filter(Control::MOUSE_FILTER_PASS);
	add_child(surface_host);

	placeholder_label = memnew(Label);
	placeholder_label->set_text(tab_title);
	placeholder_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	placeholder_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	placeholder_label->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	add_child(placeholder_label);

	_ensure_script_editor_view();
}
