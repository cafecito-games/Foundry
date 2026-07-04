/**************************************************************************/
/*  editor_scene_pane_tile.h                                              */
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

#pragma once

#include "scene/gui/box_container.h"

class EditorData;
class EditorSceneTabs;
class EditorSelection;
class HSplitContainer;
class InspectorDock;
class Label;
class MarginContainer;
class PanelContainer;
class SceneTreeDock;
class SceneTreeEditor;
class SubViewportContainer;
class TextureRect;

/**
 * One self-contained editing unit of the scene workspace:
 * [scene tab strip] + [scene tree dock | content host | inspector dock].
 *
 * The focused tile's content host hosts the single EditorMainScreen (as a
 * real laid-out child); non-focused tiles show a live 2D preview or a 3D
 * placeholder instead. Any interaction inside the tile focuses it first.
 */
class ScenePaneTile : public VBoxContainer {
	FOUNDRY_CLASS(ScenePaneTile, VBoxContainer);

	int tile_id = 0;
	EditorSceneTabs *scene_tabs = nullptr;
	HSplitContainer *body = nullptr;
	SceneTreeDock *scene_tree_dock = nullptr; // Left, in-tile.
	MarginContainer *content_host = nullptr; // Center.
	InspectorDock *inspector_dock = nullptr; // Right, in-tile.
	SubViewportContainer *preview_container = nullptr; // Non-focused 2D live preview.
	PanelContainer *preview_placeholder = nullptr; // Non-focused 3D placeholder.
	Label *preview_placeholder_label = nullptr;
	TextureRect *preview_placeholder_icon = nullptr;
	PanelContainer *focus_frame = nullptr; // Accent border when focused.

	void _request_focus();

protected:
	void _notification(int p_what);
	virtual void input(const Ref<InputEvent> &p_event) override;

public:
	int get_tile_id() const { return tile_id; }
	EditorSceneTabs *get_scene_tabs() const { return scene_tabs; }
	SceneTreeDock *get_scene_tree_dock() const { return scene_tree_dock; }
	InspectorDock *get_inspector_dock() const { return inspector_dock; }
	Control *get_content_host() const;
	SubViewportContainer *get_preview_container() const { return preview_container; }

	void set_focused_visual(bool p_focused);
	void set_preview_mode(bool p_live_2d, bool p_placeholder_3d, const String &p_scene_name, const Ref<Texture2D> &p_icon);

	void setup(int p_tile_id, EditorSelection *p_editor_selection, EditorData &p_editor_data);

	ScenePaneTile();
};
