/**************************************************************************/
/*  workspace_tab_stub_types.cpp                                          */
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

#include "workspace_tab_stub_types.h"

#include "scene/gui/control.h"

SceneTabStub::SceneTabStub(const StringName &p_type_id) :
		type_id_value(p_type_id) {
}

StringName SceneTabStub::type_id() const {
	return type_id_value;
}

bool SceneTabStub::can_open(const String &p_resource) const {
	return !p_resource.is_empty();
}

WorkspaceTab SceneTabStub::make_tab(const String &p_resource, int p_stable_id) const {
	WorkspaceTab tab;
	tab.set_stable_id(p_stable_id);
	tab.set_type_id(type_id_value);
	tab.set_resource_key(p_resource);
	tab.set_title_cache(p_resource.get_file());
	tab.set_icon_key_cache("EditorScene");
	return tab;
}

String SceneTabStub::get_title(const WorkspaceTab &p_tab) const {
	return p_tab.get_title_cache();
}

Ref<Texture2D> SceneTabStub::get_icon(const WorkspaceTab &p_tab) const {
	return Ref<Texture2D>();
}

void SceneTabStub::mount(WorkspaceTab &p_tab, Control *p_chrome_host) {
}

void SceneTabStub::unmount(WorkspaceTab &p_tab) {
}

void SceneTabStub::activate(WorkspaceTab &p_tab) {
}

WorkspaceTabCloseResult SceneTabStub::request_close(WorkspaceTab &p_tab) {
	return WorkspaceTabCloseResult::CLOSE;
}

Dictionary SceneTabStub::save_payload(const WorkspaceTab &p_tab) const {
	return p_tab.get_payload();
}

void SceneTabStub::restore_payload(WorkspaceTab &p_tab, const Dictionary &p_payload) const {
	p_tab.set_payload(p_payload);
}
