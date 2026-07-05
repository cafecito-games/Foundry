/**************************************************************************/
/*  editor_tile_drop_overlay.cpp                                          */
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

#include "editor_tile_drop_overlay.h"

#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/scene/editor_scene_tabs.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/tab_bar.h"
#include "scene/main/viewport.h"

// Guide rosette dimensions from the locked #933 spec (scaled by EDSCALE).
static constexpr float ROSETTE_GUIDE_SIZE = 100.0f;
static constexpr float ROSETTE_BUTTON_SIZE = 30.0f;
static constexpr float ROSETTE_BUTTON_GAP = 5.0f;
static constexpr float ROSETTE_BUTTON_RADIUS = 7.0f;
static constexpr float PREVIEW_CORNER_RADIUS = 8.0f;

static const Color ROSETTE_IDLE_FILL = Color(0.10980392f, 0.13333334f, 0.17254902f); // #1C222C
static const Color ROSETTE_IDLE_BORDER = Color(0.47058824f, 0.54901963f, 0.65882355f, 0.5f); // #788CA8 @ 50%
static const Color ROSETTE_AIMED_BORDER = Color(0.62352943f, 0.827451f, 1.0f); // #9FD3FF
static const Color ROSETTE_IDLE_ICON = Color(0.68235296f, 0.72156864f, 0.7764706f); // #AEB8C6
static const Color ROSETTE_AIMED_ICON = Color(0.02352941f, 0.12941177f, 0.23529412f); // #06213C

bool EditorTileDropOverlay::_is_scene_tab_drag(const Variant &p_data) {
	if (p_data.get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary d = p_data;
	if (String(d.get("type", "")) != "tab" || String(d.get("tab_type", "")) != "tab_bar_tab") {
		return false;
	}
	int source_tile_id = -1;
	int source_tab = -1;
	return _resolve_source(p_data, source_tile_id, source_tab);
}

bool EditorTileDropOverlay::_resolve_source(const Variant &p_data, int &r_source_tile_id, int &r_source_tab) {
	r_source_tile_id = -1;
	r_source_tab = -1;
	if (p_data.get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary d = p_data;
	if (!d.has("from_path") || !d.has("tab_index")) {
		return false;
	}
	SceneTree *tree = SceneTree::get_singleton();
	if (!tree || !tree->get_root()) {
		return false;
	}
	Node *from_node = tree->get_root()->get_node_or_null(d["from_path"]);
	TabBar *from_bar = Object::cast_to<TabBar>(from_node);
	if (!from_bar) {
		return false;
	}
	for (Node *node = from_bar; node; node = node->get_parent()) {
		EditorSceneTabs *tabs = Object::cast_to<EditorSceneTabs>(node);
		if (tabs) {
			r_source_tile_id = tabs->get_tile_id();
			r_source_tab = d["tab_index"];
			return true;
		}
	}
	return false;
}

EditorSceneWorkspace::TileDropRegion EditorTileDropOverlay::_region_at(const Point2 &p_local) const {
	return EditorSceneWorkspace::drop_region_at(get_size(), p_local);
}

void EditorTileDropOverlay::_draw_region_preview(const Rect2 &p_preview_rect, const Color &p_accent) const {
	const float radius = PREVIEW_CORNER_RADIUS * EDSCALE;
	draw_rect(p_preview_rect, Color(p_accent, 0.17f), true, radius);
	draw_rect(p_preview_rect, Color(p_accent, 0.95f), false, radius, 2.0f * EDSCALE);
}

void EditorTileDropOverlay::_draw_rosette_button(const Rect2 &p_rect, bool p_aimed, const Ref<Texture2D> &p_icon, const Color &p_accent) const {
	const float radius = ROSETTE_BUTTON_RADIUS * EDSCALE;
	const Color fill = p_aimed ? p_accent : ROSETTE_IDLE_FILL;
	const Color border = p_aimed ? ROSETTE_AIMED_BORDER : ROSETTE_IDLE_BORDER;
	const Color icon_mod = p_aimed ? ROSETTE_AIMED_ICON : ROSETTE_IDLE_ICON;

	draw_rect(p_rect, fill, true, radius);
	draw_rect(p_rect, border, false, radius, 1.0f * EDSCALE);

	if (p_icon.is_valid()) {
		const Size2 icon_size = p_icon->get_size();
		const Point2 icon_pos = p_rect.position + (p_rect.size - icon_size) * 0.5f;
		draw_texture_rect(p_icon, Rect2(icon_pos, icon_size), false, icon_mod);
	}
}

void EditorTileDropOverlay::_draw_guide_rosette(const Point2 &p_center, EditorSceneWorkspace::TileDropRegion p_aimed_region) const {
	const float scale = EDSCALE;
	const float guide = ROSETTE_GUIDE_SIZE * scale;
	const float button = ROSETTE_BUTTON_SIZE * scale;
	const float gap = ROSETTE_BUTTON_GAP * scale;
	const float stride = button + gap;
	const Point2 guide_origin = p_center - Point2(guide, guide) * 0.5f;
	const Color accent = get_theme_color(SNAME("accent_color"), SNAME("Editor"));

	const Ref<Texture2D> icon_left = get_theme_icon(SNAME("ArrowLeft"), EditorStringName(EditorIcons));
	const Ref<Texture2D> icon_right = get_theme_icon(SNAME("ArrowRight"), EditorStringName(EditorIcons));
	const Ref<Texture2D> icon_up = get_theme_icon(SNAME("ArrowUp"), EditorStringName(EditorIcons));
	const Ref<Texture2D> icon_down = get_theme_icon(SNAME("ArrowDown"), EditorStringName(EditorIcons));
	const Ref<Texture2D> icon_tab = get_theme_icon(SNAME("GuiTab"), EditorStringName(EditorIcons));

	const Rect2 center_rect(guide_origin + Point2(stride, stride), Size2(button, button));
	const Rect2 left_rect(guide_origin + Point2(0, stride), Size2(button, button));
	const Rect2 right_rect(guide_origin + Point2(stride * 2.0f, stride), Size2(button, button));
	const Rect2 top_rect(guide_origin + Point2(stride, 0), Size2(button, button));
	const Rect2 bottom_rect(guide_origin + Point2(stride, stride * 2.0f), Size2(button, button));

	_draw_rosette_button(left_rect, p_aimed_region == EditorSceneWorkspace::DROP_LEFT, icon_left, accent);
	_draw_rosette_button(right_rect, p_aimed_region == EditorSceneWorkspace::DROP_RIGHT, icon_right, accent);
	_draw_rosette_button(top_rect, p_aimed_region == EditorSceneWorkspace::DROP_TOP, icon_up, accent);
	_draw_rosette_button(bottom_rect, p_aimed_region == EditorSceneWorkspace::DROP_BOTTOM, icon_down, accent);
	_draw_rosette_button(center_rect, p_aimed_region == EditorSceneWorkspace::DROP_CENTER, icon_tab, accent);
}

void EditorTileDropOverlay::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_DRAG_BEGIN: {
			drag_active = get_viewport() && _is_scene_tab_drag(get_viewport()->gui_get_drag_data());
			set_mouse_filter(drag_active ? Control::MOUSE_FILTER_STOP : Control::MOUSE_FILTER_IGNORE);
			set_process_internal(drag_active);
			if (drag_active) {
				hovered_region = _region_at(get_local_mouse_position());
			}
			queue_redraw();
		} break;

		case NOTIFICATION_DRAG_END: {
			drag_active = false;
			set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			set_process_internal(false);
			queue_redraw();
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			const EditorSceneWorkspace::TileDropRegion region = _region_at(get_local_mouse_position());
			if (region != hovered_region) {
				hovered_region = region;
				queue_redraw();
			}
		} break;

		case NOTIFICATION_DRAW: {
			if (!drag_active) {
				break;
			}
			const Size2 size = get_size();
			const Color accent = get_theme_color(SNAME("accent_color"), SNAME("Editor"));
			const Rect2 preview_rect = EditorSceneWorkspace::drop_preview_rect(size, hovered_region);
			_draw_region_preview(preview_rect, accent);
			_draw_guide_rosette(size * 0.5f, hovered_region);
		} break;
	}
}

bool EditorTileDropOverlay::can_drop_data(const Point2 &p_point, const Variant &p_data) const {
	if (!_is_scene_tab_drag(p_data)) {
		return false;
	}
	int source_tile_id = -1;
	int source_tab = -1;
	if (!_resolve_source(p_data, source_tile_id, source_tab)) {
		return false;
	}
	const EditorSceneWorkspace::TileDropRegion region = _region_at(p_point);
	if (source_tile_id == owning_tile_id && region == EditorSceneWorkspace::DROP_CENTER) {
		return false;
	}
	return true;
}

void EditorTileDropOverlay::drop_data(const Point2 &p_point, const Variant &p_data) {
	int source_tile_id = -1;
	int source_tab = -1;
	if (!_resolve_source(p_data, source_tile_id, source_tab)) {
		return;
	}
	if (!EditorNode::get_singleton()) {
		return;
	}
	EditorNode::get_singleton()->handle_tile_scene_drop(owning_tile_id, _region_at(p_point), source_tile_id, source_tab);
}

EditorTileDropOverlay::EditorTileDropOverlay() {
	set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
}
