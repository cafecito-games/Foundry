/**************************************************************************/
/*  workspace_pane.h                                                      */
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

#include "scene/gui/box_container.h"
#include "scene/gui/tab_bar.h"

#include "editor/editor_workspace_leaf_content.h"
#include "editor/workspace/workspace_tab.h"
#include "editor/workspace/workspace_tab_registry.h"

class ConfigFile;
class EditorData;
class EditorSceneContext;
class EditorSceneWorkspace;
class EditorSelection;
class Label;
class ScenePaneTile;
class ScriptLeaf;
class Texture2D;

/**
 * Generic workspace leaf content: one tab strip, one chrome host for the
 * active tab, and an explicit empty-pane placeholder.
 */
class WorkspacePane : public VBoxContainer, public WorkspaceLeafContent {
	FOUNDRY_CLASS(WorkspacePane, VBoxContainer);

	int leaf_id = 0;
	EditorSceneWorkspace *workspace = nullptr;
	EditorData *editor_data = nullptr;
	EditorSelection *editor_selection = nullptr;
	StringName initial_content_type;
	Vector<WorkspaceTab> tabs;
	int active_tab_index = -1;
	WorkspaceTabRegistry *tab_registry = nullptr;
	TabBar *tab_strip = nullptr;
	Control *chrome_host = nullptr;
	Control *empty_placeholder = nullptr;
	ScenePaneTile *scene_tile = nullptr;
	ScriptLeaf *script_leaf = nullptr;
	int mounted_tab_stable_id = -1;
	bool suppress_tab_strip_callback = false;

	void _bind_tab_strip();
	void _on_tab_strip_changed(int p_index);
	void _sync_tab_strip();
	void _detach_ephemeral_chrome();
	void _set_bridge_visibility(bool p_scene_visible, bool p_script_visible);
	bool _has_legacy_scene_content() const;
	bool _has_legacy_script_content() const;
	void _mount_scene_bridge();
	void _mount_script_bridge();
	void _mount_active_tab();
	void _unmount_active_tab();
	void _update_pane_state();
	WorkspaceTabType *_active_tab_type() const;
	const WorkspaceTab *_active_tab() const;
	WorkspaceTab *_active_tab_mut();

protected:
	void _notification(int p_what);

public:
	static WorkspaceTabRegistry &get_shared_tab_registry();

	void setup(int p_leaf_id, EditorSelection *p_editor_selection, EditorData *p_editor_data, const StringName &p_initial_content_type = StringName("scene"));
	void set_workspace(EditorSceneWorkspace *p_workspace) { workspace = p_workspace; }

	int get_leaf_id() const { return leaf_id; }
	const StringName &get_initial_content_type() const { return initial_content_type; }
	bool is_scene_pane() const { return initial_content_type == StringName("scene"); }
	bool is_script_pane() const { return initial_content_type == StringName("script"); }

	int get_tab_count() const { return tabs.size(); }
	const WorkspaceTab &get_tab(int p_index) const { return tabs[p_index]; }
	int get_active_tab_index() const { return active_tab_index; }

	void set_tab_registry(WorkspaceTabRegistry *p_registry);

	void add_tab(const WorkspaceTab &p_tab);
	void remove_tab(int p_index);
	void set_active_tab(int p_index);

	void sync_from_editor_data() const;

	EditorData *get_editor_data() const { return editor_data; }
	EditorSelection *get_editor_selection() const { return editor_selection; }

	ScenePaneTile *get_scene_tile() const { return scene_tile; }
	ScriptLeaf *get_script_leaf() const { return script_leaf; }
	Control *get_chrome_host() const { return chrome_host; }
	Control *get_empty_placeholder() const { return empty_placeholder; }
	TabBar *get_tab_strip() const { return tab_strip; }

	StringName get_content_type() const override;
	Control *get_root_control() const override;
	String get_tab_title() const override;
	Ref<Texture2D> get_tab_icon() const override;
	EditorSceneContext *get_scene_context() const override;
	void on_focus_entered() override;
	void save_layout(const Ref<ConfigFile> &p_config, const String &p_section) const override;
	void load_layout(const Ref<ConfigFile> &p_config, const String &p_section) override;

	WorkspacePane();
	~WorkspacePane();
};
