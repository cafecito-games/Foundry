/**************************************************************************/
/*  editor_scene_pane_tile.h                                              */
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
#include "scene/gui/split_container.h"
#include "scene/gui/subviewport_container.h"

class World3D;

class Camera3D;
class CanvasItemEditorView;
class Control;
class Node3DEditorViewport;
class EditorData;
class EditorSceneTabs;
class EditorSelection;
class EditorTileDropOverlay;
class InspectorDock;
class Label;
class PanelContainer;
class SceneTreeDock;
class TextureRect;

enum class TilePreviewMode {
	FOCUSED_LIVE,
	LIVE_2D,
	LIVE_3D,
	PLACEHOLDER_3D,
};

// A self-contained editing tile: a scene tab strip over a row of
// [scene tree dock | content host | inspector dock]. The content host shows the
// reparented main screen when this tile is focused, or a live 2D preview / 3D
// placeholder otherwise. Tiles are joined into a recursive workspace tree by
// EditorSceneWorkspace.
class ScenePaneTile : public VBoxContainer {
	FOUNDRY_CLASS(ScenePaneTile, VBoxContainer);

	int tile_id = 0;
	EditorSceneTabs *scene_tabs = nullptr;
	HSplitContainer *body = nullptr;
	SceneTreeDock *scene_tree_dock = nullptr;
	PanelContainer *focus_frame = nullptr;
	Control *content_host = nullptr;
	InspectorDock *inspector_dock = nullptr;
	SubViewportContainer *preview_container = nullptr;
	SubViewportContainer *preview_3d_container = nullptr;
	SubViewport *preview_3d_viewport = nullptr;
	Camera3D *preview_3d_camera = nullptr;
	SubViewportContainer *context_viewport_host = nullptr;
	CanvasItemEditorView *canvas_view = nullptr;
	Node3DEditorViewport *spatial_view = nullptr;
	PanelContainer *preview_placeholder = nullptr;
	Label *preview_placeholder_label = nullptr;
	TextureRect *preview_placeholder_icon = nullptr;
	EditorTileDropOverlay *drop_overlay = nullptr;

	void _request_focus();
	void _fit_content_child(Control *p_child);
	void _fit_content_children();

protected:
	void _notification(int p_what);
	virtual void input(const Ref<InputEvent> &p_event) override;

public:
	int get_tile_id() const { return tile_id; }
	EditorSceneTabs *get_scene_tabs() const { return scene_tabs; }
	SceneTreeDock *get_scene_tree_dock() const { return scene_tree_dock; }
	InspectorDock *get_inspector_dock() const { return inspector_dock; }
	Control *get_content_host() const { return content_host; }
	SubViewportContainer *get_preview_container() const { return preview_container; }
	SubViewportContainer *get_preview_3d_container() const { return preview_3d_container; }
	SubViewportContainer *get_context_viewport_host() const { return context_viewport_host; }
	CanvasItemEditorView *get_canvas_view() const { return canvas_view; }
	Node3DEditorViewport *get_spatial_view() const { return spatial_view; }
	Camera3D *get_preview_3d_camera() const { return preview_3d_camera; }

	void set_canvas_view(CanvasItemEditorView *p_view) { canvas_view = p_view; }
	void set_spatial_view(Node3DEditorViewport *p_view) { spatial_view = p_view; }

	void set_focused_visual(bool p_focused);
	void set_preview_mode(TilePreviewMode p_mode, const String &p_scene_name = String(), const Ref<Texture2D> &p_icon = Ref<Texture2D>());
	void bind_3d_preview_world(const Ref<World3D> &p_world);
	void apply_3d_preview_camera_state(const Dictionary &p_viewport_state);

	void setup(int p_tile_id, EditorSelection *p_editor_selection, EditorData &p_editor_data);

	ScenePaneTile();
};
