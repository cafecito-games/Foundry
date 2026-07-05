/**************************************************************************/
/*  editor_script_leaf.h                                                 */
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

#include "editor/editor_workspace_leaf_content.h"

#include "scene/gui/control.h"

class Label;

/**
 * Non-scene workspace leaf that hosts the editor's script surface (U15a). It
 * carries the path of the script it represents but has no EditorSceneContext,
 * since a script is a project resource rather than a scene. The live script
 * editing surface is mounted into get_surface_host() by EditorNode; a
 * placeholder label is shown while no surface is mounted.
 */
class ScriptLeaf : public Control, public WorkspaceLeafContent {
	FOUNDRY_CLASS(ScriptLeaf, Control);

	String tab_title = "Script";
	String script_path;
	Label *placeholder_label = nullptr;
	Control *surface_host = nullptr;

	void _update_placeholder_visibility();

protected:
	void _notification(int p_what);

public:
	void set_tab_title(const String &p_title);

	// The res:// path of the script this leaf represents (empty if none yet).
	void set_script_path(const String &p_path);
	String get_script_path() const { return script_path; }

	// Container the live script editing surface is reparented into by EditorNode.
	Control *get_surface_host() const { return surface_host; }

	StringName get_content_type() const override;
	Control *get_root_control() const override;
	String get_tab_title() const override;
	Ref<Texture2D> get_tab_icon() const override;
	EditorSceneContext *get_scene_context() const override;
	void save_layout(const Ref<ConfigFile> &p_config, const String &p_section) const override;
	void load_layout(const Ref<ConfigFile> &p_config, const String &p_section) override;

	ScriptLeaf();
};
