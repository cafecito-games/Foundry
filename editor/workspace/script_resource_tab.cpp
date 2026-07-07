/**************************************************************************/
/*  script_resource_tab.cpp                                               */
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

#include "script_resource_tab.h"

#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/object/object.h"
#include "editor/editor_script_leaf.h"
#include "editor/script/script_editor_controller.h"
#include "editor/script/script_editor_view.h"
#include "scene/gui/control.h"

ScriptResourceTabType::ScriptResourceTabType(const StringName &p_type_id) :
		type_id_value(p_type_id) {
}

String ScriptResourceTabType::derive_title(const String &p_resource_key) {
	if (p_resource_key.is_empty()) {
		return String("Script");
	}
	return p_resource_key.get_file();
}

ScriptLeaf *ScriptResourceTabType::_resolve_surface(int p_stable_id) const {
	const ObjectID *id = mounted_surfaces.getptr(p_stable_id);
	if (!id) {
		return nullptr;
	}
	return Object::cast_to<ScriptLeaf>(ObjectDB::get_instance(*id));
}

ScriptLeaf *ScriptResourceTabType::_create_surface(const WorkspaceTab &p_tab, Control *p_chrome_host) {
	ScriptLeaf *leaf = memnew(ScriptLeaf);
	leaf->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	leaf->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	leaf->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	p_chrome_host->add_child(leaf);

	const String path = p_tab.get_resource_key();
	if (!path.is_empty()) {
		leaf->set_script_path(path);
	}

	ScriptEditorController *controller = ScriptEditorController::get_singleton();
	if (controller) {
		ScriptEditorView *view = leaf->get_script_editor_view();
		if (!view) {
			view = controller->create_view_for_leaf(leaf);
		}
		// open_file resolves the correct editor for the resource kind (script or
		// plain text) the way the file-open path does; it is a no-op if the script
		// is already open in this view.
		if (view && !path.is_empty() && !view->get_open_editor_for_path(path)) {
			view->open_file(path);
		}
	}
	return leaf;
}

void ScriptResourceTabType::_apply_payload(ScriptLeaf *p_leaf, const Dictionary &p_payload) const {
	if (!p_leaf) {
		return;
	}
	ScriptEditorView *view = p_leaf->get_script_editor_view();
	if (!view || !p_payload.has("view_layout")) {
		return;
	}
	const String text = p_payload["view_layout"];
	if (text.is_empty()) {
		return;
	}
	Ref<ConfigFile> config;
	config.instantiate();
	if (config->parse(text) == OK) {
		view->set_view_layout(config, "view");
	}
}

Dictionary ScriptResourceTabType::_capture_payload(ScriptLeaf *p_leaf) const {
	Dictionary payload;
	if (!p_leaf) {
		return payload;
	}
	payload["script_path"] = p_leaf->get_script_path();
	ScriptEditorView *view = p_leaf->get_script_editor_view();
	if (view) {
		Ref<ConfigFile> config;
		config.instantiate();
		view->get_view_layout(config, "view");
		payload["view_layout"] = config->encode_to_text();
	}
	return payload;
}

void ScriptResourceTabType::_focus_surface(ScriptLeaf *p_leaf) const {
	if (!p_leaf) {
		return;
	}
	ScriptEditorController *controller = ScriptEditorController::get_singleton();
	if (controller && p_leaf->get_script_editor_view()) {
		controller->set_focused_view(p_leaf->get_script_editor_view());
	}
}

StringName ScriptResourceTabType::type_id() const {
	return type_id_value;
}

bool ScriptResourceTabType::can_open(const String &p_resource) const {
	return !p_resource.is_empty();
}

WorkspaceTab ScriptResourceTabType::make_tab(const String &p_resource, int p_stable_id) const {
	WorkspaceTab tab;
	tab.set_stable_id(p_stable_id);
	tab.set_type_id(type_id_value);
	tab.set_resource_key(p_resource);
	tab.set_title_cache(derive_title(p_resource));
	tab.set_icon_key_cache("Script");
	return tab;
}

String ScriptResourceTabType::get_title(const WorkspaceTab &p_tab) const {
	return derive_title(p_tab.get_resource_key());
}

Ref<Texture2D> ScriptResourceTabType::get_icon(const WorkspaceTab &p_tab) const {
	ScriptLeaf *leaf = _resolve_surface(p_tab.get_stable_id());
	if (leaf) {
		return leaf->get_tab_icon();
	}
	return Ref<Texture2D>();
}

void ScriptResourceTabType::mount(WorkspaceTab &p_tab, Control *p_chrome_host) {
	ERR_FAIL_NULL(p_chrome_host);

	ScriptLeaf *leaf = _resolve_surface(p_tab.get_stable_id());
	if (leaf) {
		if (leaf->get_parent() != p_chrome_host) {
			if (leaf->get_parent()) {
				leaf->get_parent()->remove_child(leaf);
			}
			p_chrome_host->add_child(leaf);
		}
		leaf->show();
	} else {
		leaf = _create_surface(p_tab, p_chrome_host);
		mounted_surfaces[p_tab.get_stable_id()] = leaf->get_instance_id();
		_apply_payload(leaf, p_tab.get_payload());
	}
	_focus_surface(leaf);
}

void ScriptResourceTabType::unmount(WorkspaceTab &p_tab) {
	ScriptLeaf *leaf = _resolve_surface(p_tab.get_stable_id());
	if (leaf) {
		// Persist caret/scroll/fold state before the pane frees the detached
		// surface so a later mount (including one in another pane) restores it.
		p_tab.set_payload(_capture_payload(leaf));
	}
	mounted_surfaces.erase(p_tab.get_stable_id());
}

void ScriptResourceTabType::activate(WorkspaceTab &p_tab) {
	_focus_surface(_resolve_surface(p_tab.get_stable_id()));
}

WorkspaceTabCloseResult ScriptResourceTabType::request_close(WorkspaceTab &p_tab, const Callable &p_on_deferred_close) {
	ScriptLeaf *leaf = _resolve_surface(p_tab.get_stable_id());
	if (!leaf) {
		return WorkspaceTabCloseResult::CLOSE;
	}
	ScriptEditorView *view = leaf->get_script_editor_view();
	if (!view) {
		return WorkspaceTabCloseResult::CLOSE;
	}
	// Route to the existing per-view save/discard/cancel prompt. When the active
	// editor is dirty the prompt is shown and the close outcome is deferred: the
	// view invokes p_on_deferred_close once the prompt resolves to Save/Discard so
	// the workspace can then drop the tab, and leaves it untouched on Cancel.
	if (view->request_close_active_tab(p_on_deferred_close)) {
		return WorkspaceTabCloseResult::DEFERRED;
	}
	return WorkspaceTabCloseResult::CLOSE;
}

Dictionary ScriptResourceTabType::save_payload(const WorkspaceTab &p_tab) const {
	ScriptLeaf *leaf = _resolve_surface(p_tab.get_stable_id());
	if (leaf) {
		return _capture_payload(leaf);
	}
	return p_tab.get_payload();
}

void ScriptResourceTabType::restore_payload(WorkspaceTab &p_tab, const Dictionary &p_payload) const {
	p_tab.set_payload(p_payload);
}

bool ScriptResourceTabType::is_resource_available(const WorkspaceTab &p_tab) const {
	// The script surface (and its file) is owned by the tab, so the tab type is
	// responsible for detecting that its backing script was deleted between
	// sessions. An empty key is a blank leaf and stays available.
	const String &path = p_tab.get_resource_key();
	if (path.is_empty()) {
		return true;
	}
	return FileAccess::exists(path);
}
