/**************************************************************************/
/*  scene_tab.h                                                           */
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

#include "core/object/object_id.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

#include "editor/workspace/workspace_tab_type.h"

class EditorData;
class WorkspacePane;

class SceneTabType : public WorkspaceTabType {
	HashMap<int, ObjectID> mounted_panes;
	HashSet<int> activating_tabs;

	WorkspacePane *_get_mounted_pane(const WorkspaceTab &p_tab) const;

protected:
	virtual WorkspaceTabCloseResult request_editor_close(int p_scene_idx);

public:
	static String resource_key_for_scene(const EditorData &p_editor_data, int p_scene_idx);
	static int find_scene_index(const EditorData &p_editor_data, const WorkspaceTab &p_tab);
	static WorkspaceTab make_tab_for_scene(const EditorData &p_editor_data, int p_scene_idx, int p_stable_id);

	StringName type_id() const override;
	bool can_open(const String &p_resource) const override;
	WorkspaceTab make_tab(const String &p_resource, int p_stable_id) const override;
	String get_title(const WorkspaceTab &p_tab) const override;
	Ref<Texture2D> get_icon(const WorkspaceTab &p_tab) const override;
	void mount(WorkspaceTab &p_tab, Control *p_chrome_host) override;
	void unmount(WorkspaceTab &p_tab) override;
	void activate(WorkspaceTab &p_tab) override;
	WorkspaceTabCloseResult request_close(WorkspaceTab &p_tab, const Callable &p_on_deferred_close = Callable()) override;
	Dictionary save_payload(const WorkspaceTab &p_tab) const override;
	void restore_payload(WorkspaceTab &p_tab, const Dictionary &p_payload) const override;
};
