/**************************************************************************/
/*  editor_automation_acceptance_workflow.cpp                             */
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

#include "editor_automation_acceptance_workflow.h"

#include "editor/automation/editor_automation_driver.h"
#include "editor/automation/editor_automation_input.h"
#include "editor/automation/editor_automation_mcp_dispatcher.h"
#include "editor/automation/editor_automation_screenshot.h"
#include "editor/automation/editor_automation_snapshot.h"
#include "editor/automation/editor_workflow_test_driver.h"
#include "editor/debugger/debugger_editor_plugin.h"
#include "editor/docks/editor_dock.h"
#include "editor/docks/groups_dock.h"
#include "editor/docks/scene_tree_dock.h"
#include "editor/docks/signals_dock.h"
#include "editor/editor_board.h"
#include "editor/editor_board_strip.h"
#include "editor/editor_data.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/editor_scene_context.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/editor_scene_workspace.h"
#include "editor/editor_script_leaf.h"
#include "editor/editor_tile_dock_region.h"
#include "editor/editor_tile_drop_overlay.h"
#include "editor/file_system/editor_paths.h"
#include "editor/gui/code_editor.h"
#include "editor/gui/editor_side_rail_strip.h"
#include "editor/gui/progress_dialog.h"
#include "editor/gui/side_rail_state.h"
#include "editor/project_manager/known_project_store.h"
#include "editor/project_manager/startup_dialog.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/scene/canvas_item_editor_plugin.h"
#include "editor/scene/canvas_item_editor_view.h"
#include "editor/scene/canvas_item_editor_view_state.h"
#include "editor/script/script_editor_controller.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/script/script_editor_view.h"
#include "editor/settings/editor_settings.h"
#include "editor/workspace/workspace_pane.h"
#include "editor/workspace/workspace_tab_type.h"

#include "core/config/project_settings.h"
#include "core/crypto/crypto_core.h"
#include "core/input/input.h"
#include "core/input/input_event.h"
#include "core/input/shortcut.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/keyboard.h"
#include "core/os/os.h"
#include "scene/2d/node_2d.h"
#include "scene/3d/node_3d.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_bar.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/tree.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "servers/display/display_server.h"

namespace {

static constexpr const char *MIXED_WORKSPACE_SCENE = "res://scenes/main.tscn";
static constexpr const char *MIXED_WORKSPACE_SCRIPT = "res://scripts/player.fs";
// Root is a Node3D, so opening it gives the owning tile 3D content and makes a
// non-focused tile eligible for a secondary 3D viewport preview.
static constexpr const char *BOARD_SWITCH_3D_SCENE = "res://scenes/secondary.tscn";
static constexpr const char *BOARD_SWITCH_2D_SCENE = "res://scenes/main.tscn";

ScenePaneTile *_resolve_focused_scene_tile(EditorBoard *p_board) {
	EditorSceneWorkspace *workspace = p_board ? p_board->get_workspace() : nullptr;
	if (workspace == nullptr) {
		return nullptr;
	}
	WorkspaceLeafNode *leaf = workspace->get_focused_leaf();
	WorkspacePane *pane = leaf ? leaf->get_workspace_pane() : nullptr;
	if (pane != nullptr && pane->get_scene_tile() != nullptr) {
		return pane->get_scene_tile();
	}
	for (WorkspaceLeafNode *candidate : workspace->get_leaves()) {
		WorkspacePane *candidate_pane = candidate->get_workspace_pane();
		if (candidate_pane != nullptr && candidate_pane->get_scene_tile() != nullptr) {
			return candidate_pane->get_scene_tile();
		}
	}
	return nullptr;
}

ScriptLeaf *_find_mounted_script_leaf(WorkspacePane *p_pane) {
	Control *host = p_pane ? p_pane->get_chrome_host() : nullptr;
	if (host == nullptr) {
		return nullptr;
	}
	for (int i = 0; i < host->get_child_count(false); i++) {
		if (ScriptLeaf *leaf = Object::cast_to<ScriptLeaf>(host->get_child(i, false))) {
			return leaf;
		}
	}
	return nullptr;
}

ConfirmationDialog *_find_erase_confirm(ScriptEditorView *p_view) {
	if (p_view == nullptr) {
		return nullptr;
	}
	for (int i = 0; i < p_view->get_child_count(); i++) {
		if (ConfirmationDialog *dialog = Object::cast_to<ConfirmationDialog>(p_view->get_child(i))) {
			return dialog;
		}
	}
	return nullptr;
}

int _find_workspace_tab_index(WorkspacePane *p_pane, const StringName &p_type_id, const String &p_resource_key = String()) {
	if (p_pane == nullptr) {
		return -1;
	}
	for (int i = 0; i < p_pane->get_tab_count(); i++) {
		const WorkspaceTab &tab = p_pane->get_tab(i);
		if (tab.get_type_id() == p_type_id && (p_resource_key.is_empty() || tab.get_resource_key() == p_resource_key)) {
			return i;
		}
	}
	return -1;
}

bool _pane_has_tab(WorkspacePane *p_pane, const StringName &p_type_id, const String &p_resource_key = String()) {
	return _find_workspace_tab_index(p_pane, p_type_id, p_resource_key) >= 0;
}

EditorAutomationAcceptanceWorkflow::Result _failure_with_message(EditorWorkflowTestDriver &p_driver, const String &p_workflow, const String &p_message) {
	EditorAutomationAcceptanceWorkflow::Result fail;
	fail.ok = false;
	fail.workflow = p_workflow;
	fail.message = p_message;
	fail.details = p_driver.make_failure_details();
	return fail;
}

// Drives a pointer gesture at a control through the same EditorAutomationInput
// primitives the MCP "click" action uses (see _push_mouse_click in
// editor_automation_driver.cpp) rather than invoking a private input handler. Routing
// through Viewport::push_input -> Input::parse_input_event exercises the real
// gui_input signal wiring, so a passive surface that ignores the mouse genuinely
// receives nothing instead of being skipped by a test that never dispatched.
struct RealPointerDispatch {
	Viewport *viewport = nullptr;
	Vector2 position;
	bool local_coords = false;

	bool bind(Control *p_control) {
		if (p_control == nullptr) {
			return false;
		}
		if (!EditorAutomationInput::ensure_window_focus(p_control).ok) {
			return false;
		}
		const Vector2 global_position = p_control->get_global_rect().get_center();
		position = global_position;
		viewport = EditorAutomationInput::input_viewport_for_control(p_control, position, global_position);
		if (viewport == nullptr) {
			return false;
		}
		local_coords = Object::cast_to<SubViewport>(viewport) != nullptr;
		return true;
	}

	bool button(MouseButton p_button, bool p_pressed, MouseButtonMask p_mask, PackedStringArray &r_events) const {
		const EditorAutomationInputModifiers modifiers;
		return EditorAutomationInput::push_mouse_button(viewport, position, p_button, p_pressed, p_mask, modifiers, r_events, local_coords);
	}

	// gui_get_hovered_control() -- which ScenePaneTile::input() consults before treating a
	// press as its own -- is only refreshed by mouse motion, so a gesture has to establish
	// hover at the target before pressing.
	bool hover(PackedStringArray &r_events) const {
		const EditorAutomationInputModifiers modifiers;
		return EditorAutomationInput::push_mouse_motion(viewport, position, Vector2(), MouseButtonMask::NONE, modifiers, r_events, local_coords);
	}

	bool motion(const Vector2 &p_relative, MouseButtonMask p_mask, PackedStringArray &r_events) {
		position += p_relative;
		const EditorAutomationInputModifiers modifiers;
		return EditorAutomationInput::push_mouse_motion(viewport, position, p_relative, p_mask, modifiers, r_events, local_coords);
	}

	bool key(Key p_key, PackedStringArray &r_events) const {
		const EditorAutomationInputModifiers modifiers;
		return EditorAutomationInput::push_key_event(viewport, p_key, true, 0, modifiers, r_events) &&
				EditorAutomationInput::push_key_event(viewport, p_key, false, 0, modifiers, r_events);
	}
};

// Everything a passive preview must leave untouched, sampled before and after input.
struct PassivePreviewSnapshot {
	Transform3D camera_transform;
	Transform2D canvas_transform;
	real_t canvas_zoom = 0.0;
	int selected_count = 0;
	int scene_child_count = 0;
	Transform3D first_child_transform;

	bool operator==(const PassivePreviewSnapshot &p_other) const {
		return camera_transform == p_other.camera_transform &&
				canvas_transform == p_other.canvas_transform &&
				Math::is_equal_approx(canvas_zoom, p_other.canvas_zoom) &&
				selected_count == p_other.selected_count &&
				scene_child_count == p_other.scene_child_count &&
				first_child_transform == p_other.first_child_transform;
	}
};

// Captured chrome for a demoted tile so promotion can prove exact restoration.
struct TileChromeSnapshot {
	SideRailMode left_mode = SideRailMode::DOCKED;
	SideRailMode right_mode = SideRailMode::DOCKED;
	ObjectID left_drawer_id;
	ObjectID right_drawer_id;
	int right_tab_index = -1;
	PackedInt32Array split_offsets;
	bool left_rail_visible = true;
	bool right_rail_visible = true;
	bool left_column_visible = true;
	bool right_column_visible = true;
};

// Optional proof-gallery dumps. Set FOUNDRY_CAPTURE_DIR to a writable directory
// before running passive_preview_input_policy; unset leaves the workflow unchanged.
// Failures are silent (no WARN/ERROR) so headless/dummy-renderer runs do not trip
// assert_no_new_errors when capture is requested without a real GPU path.
void _prepare_capture_frame(EditorWorkflowTestDriver &p_driver) {
	if (ProgressDialog *progress = ProgressDialog::get_singleton()) {
		// Layout-load progress can linger visually under llvmpipe and occlude the
		// workspace; hide it so gallery shots show the tile chrome under test.
		progress->hide();
	}
	p_driver.flush_frames(8);
}

void _maybe_capture_editor_png(EditorWorkflowTestDriver &p_driver, const String &p_filename) {
	const String capture_dir = OS::get_singleton()->get_environment("FOUNDRY_CAPTURE_DIR").strip_edges();
	if (capture_dir.is_empty() || p_filename.is_empty()) {
		return;
	}
	DisplayServer *display_server = DisplayServer::get_singleton();
	if (display_server == nullptr || display_server->get_name() == StringName("headless") ||
			OS::get_singleton()->get_current_rendering_method().to_lower() == "dummy") {
		OS::get_singleton()->print("FOUNDRY_CAPTURE_SKIP %s (screenshot_unsupported_renderer)\n", p_filename.utf8().get_data());
		return;
	}
	const Error mkdir_err = DirAccess::make_dir_recursive_absolute(capture_dir);
	if (mkdir_err != OK && mkdir_err != ERR_ALREADY_EXISTS) {
		OS::get_singleton()->print("FOUNDRY_CAPTURE_SKIP mkdir %s (%d)\n", capture_dir.utf8().get_data(), (int)mkdir_err);
		return;
	}

	_prepare_capture_frame(p_driver);

	EditorAutomationScreenshotOptions options;
	options.enabled = true;
	options.force_draw = true;
	options.max_bytes = 8 * 1024 * 1024;
	const EditorAutomationScreenshotAttachment attachment =
			EditorAutomationScreenshot::capture_on_demand(options, false, Rect2i());
	if (attachment.status != "available" || attachment.data.is_empty()) {
		OS::get_singleton()->print("FOUNDRY_CAPTURE_SKIP %s (%s)\n", p_filename.utf8().get_data(), attachment.reason.utf8().get_data());
		return;
	}

	const CharString encoded = attachment.data.utf8();
	Vector<uint8_t> png_bytes;
	png_bytes.resize(encoded.length());
	size_t decoded_len = 0;
	const Error decode_err = CryptoCore::b64_decode(png_bytes.ptrw(), png_bytes.size(), &decoded_len,
			(const uint8_t *)encoded.get_data(), encoded.length());
	if (decode_err != OK || decoded_len == 0) {
		OS::get_singleton()->print("FOUNDRY_CAPTURE_SKIP %s (decode)\n", p_filename.utf8().get_data());
		return;
	}
	png_bytes.resize((int)decoded_len);

	const String path = capture_dir.path_join(p_filename);
	Error write_err = OK;
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE, &write_err);
	if (file.is_null() || write_err != OK) {
		OS::get_singleton()->print("FOUNDRY_CAPTURE_SKIP %s (write %d)\n", path.utf8().get_data(), (int)write_err);
		return;
	}
	file->store_buffer(png_bytes);
	OS::get_singleton()->print("FOUNDRY_CAPTURE %s\n", path.utf8().get_data());
}

void _capture_demoted_chrome_budget_pair(EditorWorkflowTestDriver &p_driver, ScenePaneTile *p_demoted_tile) {
	if (p_demoted_tile == nullptr || OS::get_singleton()->get_environment("FOUNDRY_CAPTURE_DIR").strip_edges().is_empty()) {
		return;
	}
	DisplayServer *display_server = DisplayServer::get_singleton();
	if (display_server == nullptr || display_server->get_name() == StringName("headless") ||
			OS::get_singleton()->get_current_rendering_method().to_lower() == "dummy") {
		// Skip chrome mutation under the dummy renderer; capture cannot succeed
		// and temporarily restoring docks would only add flaky layout churn.
		return;
	}
	EditorTileDockRegion *region = p_demoted_tile->get_dock_region();
	if (region == nullptr || !region->is_presentation_hidden()) {
		return;
	}
	// Capture the fixed preview-only state first, then temporarily restore per-tile
	// chrome without promoting focus to recreate the pre-fix chrome-budget collapse.
	_maybe_capture_editor_png(p_driver, "2067_after_preview_only_horizontal.png");

	region->set_presentation_hidden(false);
	if (EditorSideRailStrip *left_rail = p_demoted_tile->get_left_rail()) {
		left_rail->set_visible(true);
	}
	if (EditorSideRailStrip *right_rail = p_demoted_tile->get_right_rail()) {
		right_rail->set_visible(true);
	}
	p_driver.flush_frames(20);
	_maybe_capture_editor_png(p_driver, "2067_before_chrome_budget.png");

	p_demoted_tile->set_preview_mode(p_demoted_tile->get_preview_mode());
	p_driver.flush_frames(20);
	_prepare_capture_frame(p_driver);
}

TileChromeSnapshot _sample_tile_chrome(ScenePaneTile *p_tile) {
	TileChromeSnapshot snapshot;
	if (p_tile == nullptr) {
		return snapshot;
	}
	EditorTileDockRegion *region = p_tile->get_dock_region();
	snapshot.left_mode = region->get_side_mode(EditorTileDockRegion::Side::LEFT);
	snapshot.right_mode = region->get_side_mode(EditorTileDockRegion::Side::RIGHT);
	if (EditorDock *drawer = region->get_drawer_dock(EditorTileDockRegion::Side::LEFT)) {
		snapshot.left_drawer_id = drawer->get_instance_id();
	}
	if (EditorDock *drawer = region->get_drawer_dock(EditorTileDockRegion::Side::RIGHT)) {
		snapshot.right_drawer_id = drawer->get_instance_id();
	}
	if (TabContainer *right_tabs = region->get_right_tabs()) {
		snapshot.right_tab_index = right_tabs->get_current_tab();
		snapshot.right_column_visible = right_tabs->is_visible();
	}
	if (HSplitContainer *body = region->get_body()) {
		snapshot.split_offsets = body->get_split_offsets();
	}
	if (SceneTreeDock *left = p_tile->get_scene_tree_dock()) {
		snapshot.left_column_visible = left->is_visible();
	}
	if (EditorSideRailStrip *left_rail = p_tile->get_left_rail()) {
		snapshot.left_rail_visible = left_rail->is_visible();
	}
	if (EditorSideRailStrip *right_rail = p_tile->get_right_rail()) {
		snapshot.right_rail_visible = right_rail->is_visible();
	}
	return snapshot;
}

String _describe_tile_preview_geometry(ScenePaneTile *p_tile, Control *p_preview_surface) {
	if (p_tile == nullptr) {
		return "tile=null";
	}
	const Rect2 tile_rect = p_tile->get_global_rect();
	const Rect2 host_rect = p_tile->get_content_host() ? p_tile->get_content_host()->get_global_rect() : Rect2();
	const Rect2 surface_rect = p_preview_surface ? p_preview_surface->get_global_rect() : Rect2();
	const EditorTileDockRegion *region = p_tile->get_dock_region();
	const bool left_rail_visible = p_tile->get_left_rail() && p_tile->get_left_rail()->is_visible();
	const bool right_rail_visible = p_tile->get_right_rail() && p_tile->get_right_rail()->is_visible();
	const bool left_column_visible = p_tile->get_scene_tree_dock() && p_tile->get_scene_tree_dock()->is_visible();
	const bool right_column_visible = region && region->get_right_tabs() && region->get_right_tabs()->is_visible();
	return vformat(
			"tile_id=%d tile_rect=%s host_rect=%s surface_rect=%s presentation_hidden=%s left_rail=%s right_rail=%s left_column=%s right_column=%s left_mode=%d right_mode=%d",
			p_tile->get_tile_id(),
			String(tile_rect),
			String(host_rect),
			String(surface_rect),
			region && region->is_presentation_hidden() ? "true" : "false",
			left_rail_visible ? "visible" : "hidden",
			right_rail_visible ? "visible" : "hidden",
			left_column_visible ? "visible" : "hidden",
			right_column_visible ? "visible" : "hidden",
			region ? (int)region->get_side_mode(EditorTileDockRegion::Side::LEFT) : -1,
			region ? (int)region->get_side_mode(EditorTileDockRegion::Side::RIGHT) : -1);
}

bool _assert_preview_only_chrome(ScenePaneTile *p_tile, Control *p_preview_surface, String &r_message) {
	if (p_tile == nullptr) {
		r_message = "Preview tile is null.";
		return false;
	}
	EditorTileDockRegion *region = p_tile->get_dock_region();
	if (region == nullptr || !region->is_presentation_hidden()) {
		r_message = vformat("Demoted tile is not presentation-hidden. %s", _describe_tile_preview_geometry(p_tile, p_preview_surface));
		return false;
	}
	if (p_tile->get_left_rail() == nullptr || p_tile->get_left_rail()->is_visible()) {
		r_message = vformat("Demoted tile left rail is still visible. %s", _describe_tile_preview_geometry(p_tile, p_preview_surface));
		return false;
	}
	if (p_tile->get_right_rail() == nullptr || p_tile->get_right_rail()->is_visible()) {
		r_message = vformat("Demoted tile right rail is still visible. %s", _describe_tile_preview_geometry(p_tile, p_preview_surface));
		return false;
	}
	if (p_tile->get_scene_tree_dock() == nullptr || p_tile->get_scene_tree_dock()->is_visible()) {
		r_message = vformat("Demoted tile left dock column is still visible. %s", _describe_tile_preview_geometry(p_tile, p_preview_surface));
		return false;
	}
	if (region->get_right_tabs() == nullptr || region->get_right_tabs()->is_visible()) {
		r_message = vformat("Demoted tile right dock column is still visible. %s", _describe_tile_preview_geometry(p_tile, p_preview_surface));
		return false;
	}
	if (p_preview_surface == nullptr || !p_preview_surface->is_visible_in_tree()) {
		r_message = vformat("Preview surface is missing or not on screen. %s", _describe_tile_preview_geometry(p_tile, p_preview_surface));
		return false;
	}
	const real_t tile_width = p_tile->get_size().x;
	const real_t surface_width = p_preview_surface->get_size().x;
	const real_t surface_area = p_preview_surface->get_global_rect().get_area();
	if (tile_width <= 0.0 || surface_width <= 0.0 || surface_area <= 0.0) {
		r_message = vformat("Preview surface has no usable geometry. %s", _describe_tile_preview_geometry(p_tile, p_preview_surface));
		return false;
	}
	if (surface_width < tile_width * 0.8) {
		r_message = vformat(
				"Preview surface width %.1f is below 80%% of tile width %.1f. %s",
				surface_width, tile_width, _describe_tile_preview_geometry(p_tile, p_preview_surface));
		return false;
	}
	return true;
}

bool _assert_chrome_restored(ScenePaneTile *p_tile, const TileChromeSnapshot &p_before, String &r_message) {
	if (p_tile == nullptr) {
		r_message = "Promoted tile is null.";
		return false;
	}
	const TileChromeSnapshot after = _sample_tile_chrome(p_tile);
	if (p_tile->get_dock_region()->is_presentation_hidden()) {
		r_message = vformat("Promoted tile is still presentation-hidden. %s", _describe_tile_preview_geometry(p_tile, p_tile->get_content_host()));
		return false;
	}
	if (!after.left_rail_visible || !after.right_rail_visible) {
		r_message = vformat("Promoted tile rails were not restored. %s", _describe_tile_preview_geometry(p_tile, p_tile->get_content_host()));
		return false;
	}
	if (after.left_mode != p_before.left_mode || after.right_mode != p_before.right_mode) {
		r_message = vformat(
				"Promoted tile side modes changed (left %d->%d, right %d->%d). %s",
				(int)p_before.left_mode, (int)after.left_mode, (int)p_before.right_mode, (int)after.right_mode,
				_describe_tile_preview_geometry(p_tile, p_tile->get_content_host()));
		return false;
	}
	if (after.left_drawer_id != p_before.left_drawer_id || after.right_drawer_id != p_before.right_drawer_id) {
		r_message = vformat("Promoted tile drawer docks changed. %s", _describe_tile_preview_geometry(p_tile, p_tile->get_content_host()));
		return false;
	}
	if (after.right_tab_index != p_before.right_tab_index) {
		r_message = vformat(
				"Promoted tile right tab changed (%d->%d). %s",
				p_before.right_tab_index, after.right_tab_index,
				_describe_tile_preview_geometry(p_tile, p_tile->get_content_host()));
		return false;
	}
	if (after.left_column_visible != p_before.left_column_visible || after.right_column_visible != p_before.right_column_visible) {
		r_message = vformat("Promoted tile column visibility changed. %s", _describe_tile_preview_geometry(p_tile, p_tile->get_content_host()));
		return false;
	}
	// Split offsets are only meaningful while columns are visible. When both
	// sides show a column again, both body gaps must exist; the remembered
	// widths survive demotion via EditorTileDockRegion gap identity.
	if (after.left_column_visible && after.right_column_visible && after.split_offsets.size() != 2) {
		r_message = vformat(
				"Promoted tile did not restore both dock split gaps (got %d). %s",
				after.split_offsets.size(),
				_describe_tile_preview_geometry(p_tile, p_tile->get_content_host()));
		return false;
	}
	return true;
}

PassivePreviewSnapshot _sample_passive_preview(Node3DEditorViewport *p_spatial_view, CanvasItemEditorView *p_canvas_view, Node *p_scene_root) {
	PassivePreviewSnapshot snapshot;
	if (p_spatial_view != nullptr && p_spatial_view->get_camera_3d() != nullptr) {
		snapshot.camera_transform = p_spatial_view->get_camera_3d()->get_global_transform();
	}
	if (p_canvas_view != nullptr) {
		snapshot.canvas_transform = p_canvas_view->get_canvas_transform();
		snapshot.canvas_zoom = p_canvas_view->get_view_state().zoom;
	}
	EditorSelection *selection = EditorNode::get_singleton() ? EditorNode::get_singleton()->get_editor_selection() : nullptr;
	if (selection != nullptr) {
		snapshot.selected_count = (int)selection->get_selection().size();
	}
	if (p_scene_root != nullptr) {
		snapshot.scene_child_count = p_scene_root->get_child_count();
		if (Node3D *first = Object::cast_to<Node3D>(p_scene_root->get_child_count() > 0 ? p_scene_root->get_child(0) : nullptr)) {
			snapshot.first_child_transform = first->get_global_transform();
		}
	}
	return snapshot;
}

bool _assert_visible_workspace_tab(EditorWorkflowTestDriver &p_driver, const String &p_type_id, const String &p_resource_key, int p_tile_id, const String &p_context) {
	Dictionary metadata;
	metadata["type_id"] = p_type_id;
	metadata["resource_key"] = p_resource_key;
	if (p_tile_id >= 0) {
		metadata["tile_id"] = p_tile_id;
	}

	Dictionary selector;
	selector["role"] = "tab";
	selector["metadata"] = metadata;

	const Dictionary found = p_driver.find(selector);
	if (!p_driver.require_ok(found, p_context)) {
		return false;
	}
	return int(found.get("match_count", 0)) >= 1;
}

String _rendered_scene_tree_root_name(SceneTreeDock *p_dock) {
	SceneTreeEditor *tree_editor = p_dock ? p_dock->get_tree_editor() : nullptr;
	Tree *tree = tree_editor ? tree_editor->get_scene_tree() : nullptr;
	TreeItem *root_item = tree ? tree->get_root() : nullptr;
	return root_item ? root_item->get_text(0) : String("<none>");
}

struct MixedWorkspaceContext {
	EditorNode *editor_node = nullptr;
	EditorSceneWorkspace *workspace = nullptr;
	WorkspaceLeafNode *scene_leaf = nullptr;
	WorkspacePane *scene_pane = nullptr;
	int scene_leaf_id = -1;
};

Dictionary _inspector_visible_property_selector() {
	Dictionary selector;
	selector["role"] = "property_row";
	selector["class"] = "EditorPropertyCheck";
	Dictionary metadata;
	metadata["property_path"] = "visible";
	selector["metadata"] = metadata;
	return selector;
}

Dictionary _selector_within_modal(const Dictionary &p_selector) {
	Dictionary within;
	within["role"] = "dialog";
	within["focused"] = true;
	Dictionary selector = p_selector;
	selector["within"] = within;
	return selector;
}

void _expand_inspector(EditorWorkflowTestDriver &p_driver) {
	if (!p_driver.require_ok(p_driver.run_command("property_editor/expand_all"), "expand_inspector")) {
		return;
	}
	p_driver.flush_frames(15);
}

bool _load_mixed_workspace_scene(EditorWorkflowTestDriver &p_driver, const String &p_workflow, MixedWorkspaceContext &r_context, EditorAutomationAcceptanceWorkflow::Result &r_failure) {
	p_driver.set_step("setup_open_scene");

	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node == nullptr || !editor_node->is_editor_ready()) {
		r_failure = _failure_with_message(p_driver, p_workflow, "EditorNode is not ready.");
		return false;
	}
	if (editor_node->load_scene(MIXED_WORKSPACE_SCENE) != OK) {
		r_failure = _failure_with_message(p_driver, p_workflow, vformat("Failed to load scene '%s'.", MIXED_WORKSPACE_SCENE));
		return false;
	}
	p_driver.flush_frames(30);

	EditorSceneWorkspace *workspace = EditorNode::get_scene_workspace();
	if (workspace == nullptr) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Scene workspace is unavailable.");
		return false;
	}
	workspace->sync_scene_tabs_from_editor_data();
	p_driver.flush_frames(20);

	WorkspaceLeafNode *scene_leaf = workspace->get_focused_leaf();
	if (scene_leaf == nullptr || scene_leaf->get_pane_tile() == nullptr) {
		for (WorkspaceLeafNode *leaf : workspace->get_leaves()) {
			if (leaf->get_pane_tile() != nullptr) {
				scene_leaf = leaf;
				break;
			}
		}
	}
	WorkspacePane *scene_pane = scene_leaf ? scene_leaf->get_workspace_pane() : nullptr;
	if (scene_leaf == nullptr || scene_pane == nullptr || scene_pane->get_scene_tile() == nullptr) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Could not resolve the scene workspace pane.");
		return false;
	}

	r_context.editor_node = editor_node;
	r_context.workspace = workspace;
	r_context.scene_leaf = scene_leaf;
	r_context.scene_pane = scene_pane;
	r_context.scene_leaf_id = scene_leaf->get_leaf_id();
	return true;
}

bool _add_script_tab_to_scene_pane(EditorWorkflowTestDriver &p_driver, const String &p_workflow, MixedWorkspaceContext &r_context, EditorAutomationAcceptanceWorkflow::Result &r_failure) {
	p_driver.set_step("open_script_in_scene_pane");

	WorkspaceTabRegistry &registry = WorkspacePane::get_shared_tab_registry();
	WorkspaceTabType *script_type = registry.find_type(StringName("script"));
	if (script_type == nullptr) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Workspace script tab type is unavailable.");
		return false;
	}

	WorkspaceTab script_tab = script_type->make_tab(MIXED_WORKSPACE_SCRIPT, registry.allocate_stable_id());
	r_context.scene_pane->add_tab(script_tab);
	r_context.scene_pane->set_active_tab(r_context.scene_pane->get_tab_count() - 1);
	p_driver.flush_frames(30);

	if (r_context.scene_pane->get_tab_count() != 2 ||
			!_pane_has_tab(r_context.scene_pane, StringName("scene"), MIXED_WORKSPACE_SCENE) ||
			!_pane_has_tab(r_context.scene_pane, StringName("script"), MIXED_WORKSPACE_SCRIPT)) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Expected scene and script tabs to share the focused pane.");
		return false;
	}

	if (!_assert_visible_workspace_tab(p_driver, "scene", MIXED_WORKSPACE_SCENE, r_context.scene_leaf_id, "find_scene_tab_same_pane") ||
			!_assert_visible_workspace_tab(p_driver, "script", MIXED_WORKSPACE_SCRIPT, r_context.scene_leaf_id, "find_script_tab_same_pane")) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Scene/script tabs were not visible through semantic tab selectors.");
		return false;
	}

	return true;
}

WorkspaceLeafNode *_split_script_tab_to_right(EditorWorkflowTestDriver &p_driver, const String &p_workflow, MixedWorkspaceContext &r_context, EditorAutomationAcceptanceWorkflow::Result &r_failure) {
	p_driver.set_step("edge_split_script_tab");
	const int script_index = _find_workspace_tab_index(r_context.scene_pane, StringName("script"), MIXED_WORKSPACE_SCRIPT);
	if (script_index < 0) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Could not find script tab before edge split.");
		return nullptr;
	}

	WorkspaceLeafNode *script_leaf = r_context.workspace->handle_tab_drop(
			r_context.scene_leaf_id,
			script_index,
			r_context.scene_leaf,
			EditorSceneWorkspace::DROP_RIGHT);
	p_driver.flush_frames(40);

	WorkspacePane *script_pane = script_leaf ? script_leaf->get_workspace_pane() : nullptr;
	if (script_leaf == nullptr || script_pane == nullptr ||
			r_context.workspace->get_leaf_count() != 2 ||
			script_pane->get_tab_count() != 1 ||
			!_pane_has_tab(script_pane, StringName("script"), MIXED_WORKSPACE_SCRIPT) ||
			!_pane_has_tab(r_context.scene_pane, StringName("scene"), MIXED_WORKSPACE_SCENE)) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Edge-drop did not split the script tab into its own pane.");
		return nullptr;
	}

	return script_leaf;
}

bool _assert_command_routing(EditorWorkflowTestDriver &p_driver, const String &p_workflow, MixedWorkspaceContext &r_context, WorkspaceLeafNode *p_script_leaf, EditorAutomationAcceptanceWorkflow::Result &r_failure) {
	p_driver.set_step("verify_command_routing");

	if (p_script_leaf == nullptr || p_script_leaf->get_workspace_pane() == nullptr) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Script pane is unavailable for command routing verification.");
		return false;
	}

	r_context.workspace->set_focused_leaf(r_context.scene_leaf_id);
	p_driver.flush_frames(5);
	ScenePaneTile *scene_tile = r_context.scene_pane->get_scene_tile();
	if (scene_tile == nullptr || r_context.workspace->get_effective_focused_tile() != scene_tile) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Scene command routing did not target the focused scene tile.");
		return false;
	}

	r_context.workspace->set_focused_leaf(p_script_leaf->get_leaf_id());
	p_driver.flush_frames(15);
	if (r_context.workspace->get_focused_tile() != nullptr || r_context.workspace->get_effective_focused_tile() != scene_tile) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Scene command routing did not fall back to the last focused scene tile while a script pane was focused.");
		return false;
	}

	if (!p_driver.require_ok(p_driver.run_command("docks/open_scene"), "scene_command_from_script_focus")) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Scene dock command failed while script pane was focused.");
		return false;
	}

	WorkspacePane *script_pane = p_script_leaf->get_workspace_pane();
	script_pane->on_focus_entered();
	p_driver.flush_frames(10);
	ScriptEditorController *controller = ScriptEditorController::get_singleton();
	if (controller == nullptr) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Script editor controller is unavailable.");
		return false;
	}
	String script_path;
	int line = -1;
	int column = -1;
	if (!controller->get_current_script_view_state(script_path, line, column) || script_path != MIXED_WORKSPACE_SCRIPT) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Script command routing did not target the focused script tab.");
		return false;
	}

	return true;
}

bool _move_script_tab_back_to_scene_pane(EditorWorkflowTestDriver &p_driver, const String &p_workflow, MixedWorkspaceContext &r_context, WorkspaceLeafNode *p_script_leaf, EditorAutomationAcceptanceWorkflow::Result &r_failure) {
	p_driver.set_step("center_drop_script_back");
	if (p_script_leaf == nullptr) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Script pane is unavailable before center-drop.");
		return false;
	}

	WorkspaceLeafNode *target_scene_leaf = r_context.workspace->get_leaf_by_id(r_context.scene_leaf_id);
	if (target_scene_leaf == nullptr) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Scene pane disappeared before center-drop.");
		return false;
	}

	WorkspaceLeafNode *dest_leaf = r_context.workspace->handle_tab_drop(
			p_script_leaf->get_leaf_id(),
			0,
			target_scene_leaf,
			EditorSceneWorkspace::DROP_CENTER);
	p_driver.flush_frames(80);

	target_scene_leaf = r_context.workspace->get_leaf_by_id(r_context.scene_leaf_id);
	WorkspacePane *target_scene_pane = target_scene_leaf ? target_scene_leaf->get_workspace_pane() : nullptr;
	if (dest_leaf == nullptr ||
			target_scene_leaf == nullptr ||
			target_scene_pane == nullptr ||
			r_context.workspace->get_leaf_count() != 1 ||
			target_scene_pane->get_tab_count() != 2 ||
			!_pane_has_tab(target_scene_pane, StringName("scene"), MIXED_WORKSPACE_SCENE) ||
			!_pane_has_tab(target_scene_pane, StringName("script"), MIXED_WORKSPACE_SCRIPT)) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Center-drop did not move the script tab back and collapse the empty pane.");
		return false;
	}

	r_context.scene_leaf = target_scene_leaf;
	r_context.scene_pane = target_scene_pane;
	return true;
}

bool _close_dirty_script_tab(EditorWorkflowTestDriver &p_driver, const String &p_workflow, MixedWorkspaceContext &r_context, EditorAutomationAcceptanceWorkflow::Result &r_failure) {
	p_driver.set_step("dirty_script_close_prompt");
	const int script_index = _find_workspace_tab_index(r_context.scene_pane, StringName("script"), MIXED_WORKSPACE_SCRIPT);
	if (script_index < 0) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Could not find script tab before dirty close.");
		return false;
	}
	r_context.scene_pane->set_active_tab(script_index);
	p_driver.flush_frames(20);

	ScriptLeaf *script_leaf = _find_mounted_script_leaf(r_context.scene_pane);
	ScriptEditorView *view = script_leaf ? script_leaf->get_script_editor_view() : nullptr;
	TabContainer *tabs = view ? view->get_tab_container() : nullptr;
	ScriptEditorBase *editor = (tabs && tabs->get_tab_count() > 0) ? Object::cast_to<ScriptEditorBase>(tabs->get_tab_control(tabs->get_current_tab())) : nullptr;
	if (editor == nullptr || editor->get_code_editor() == nullptr || editor->get_code_editor()->get_text_editor() == nullptr) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Could not resolve the active script editor for dirty close.");
		return false;
	}

	editor->get_code_editor()->get_text_editor()->insert_text_at_caret("# dirty close acceptance edit\n");
	p_driver.flush_frames(10);
	if (!editor->is_unsaved()) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Script edit did not mark the tab dirty.");
		return false;
	}

	ConfirmationDialog *prompt = _find_erase_confirm(view);
	const WorkspaceTabCloseResult close_result = r_context.scene_pane->request_close_active_tab();
	p_driver.flush_frames(10);
	if (close_result != WorkspaceTabCloseResult::DEFERRED || prompt == nullptr || r_context.scene_pane->get_tab_count() != 2) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Dirty script close did not route through the save/discard prompt.");
		return false;
	}

	prompt->emit_signal(SNAME("custom_action"), "discard");
	p_driver.flush_frames(60);
	if (r_context.scene_pane->get_tab_count() != 1 ||
			!_pane_has_tab(r_context.scene_pane, StringName("scene"), MIXED_WORKSPACE_SCENE) ||
			_pane_has_tab(r_context.scene_pane, StringName("script"), MIXED_WORKSPACE_SCRIPT)) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Discarding the dirty script prompt did not close the workspace script tab.");
		return false;
	}

	r_context.workspace->set_focused_leaf(r_context.scene_leaf_id);
	p_driver.flush_frames(10);
	if (r_context.workspace->get_effective_focused_tile() != r_context.scene_pane->get_scene_tile()) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Scene tile focus was not retained after closing the dirty script tab.");
		return false;
	}
	return true;
}

bool _save_mixed_workspace_layout(EditorWorkflowTestDriver &p_driver, const String &p_workflow, MixedWorkspaceContext &r_context, EditorAutomationAcceptanceWorkflow::Result &r_failure) {
	p_driver.set_step("save_mixed_workspace_layout");
	r_context.workspace->set_focused_leaf(r_context.scene_leaf_id);
	r_context.editor_node->save_editor_layout_delayed();
	p_driver.flush_frames(120);
	if (!p_driver.wait_editor_idle(10000)) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Editor did not become idle after saving mixed workspace layout.");
		return false;
	}
	return true;
}

EditorAutomationAcceptanceWorkflow::Result _failure_from_driver(EditorWorkflowTestDriver &p_driver, const String &p_workflow, const String &p_message = String()) {
	EditorAutomationAcceptanceWorkflow::Result result;
	result.ok = false;
	result.workflow = p_workflow;
	result.message = p_message.is_empty() ? p_driver.get_failure().message : p_message;
	result.details = p_driver.make_failure_details();
	return result;
}

// One leg of the empty-pane drop workflow: drag a scene tab out of the pane that
// currently hosts it and drop it in the center of a pane that holds nothing at
// all. The destination is deliberately never seeded, because a seeded pane shows
// its chrome and would hide the defect this leg exists to catch.
struct EmptyPaneDropLeg {
	String step;
	int scene_index = -1;
	int source_leaf_id = -1;
	int destination_leaf_id = -1;
	EditorSceneWorkspace *source_workspace = nullptr;
	EditorSceneWorkspace *destination_workspace = nullptr;
};

bool _drop_scene_tab_on_empty_pane(EditorWorkflowTestDriver &p_driver, const String &p_workflow, const EmptyPaneDropLeg &p_leg, EditorAutomationAcceptanceWorkflow::Result &r_failure) {
	p_driver.set_step(p_leg.step);

	if (p_leg.source_workspace == nullptr || p_leg.destination_workspace == nullptr) {
		r_failure = _failure_with_message(p_driver, p_workflow, "A board workspace was unavailable for the empty-pane drop.");
		return false;
	}
	WorkspaceLeafNode *source_leaf = p_leg.source_workspace->get_leaf_by_id(p_leg.source_leaf_id);
	WorkspaceLeafNode *destination_leaf = p_leg.destination_workspace->get_leaf_by_id(p_leg.destination_leaf_id);
	WorkspacePane *source_pane = source_leaf != nullptr ? source_leaf->get_workspace_pane() : nullptr;
	WorkspacePane *destination_pane = destination_leaf != nullptr ? destination_leaf->get_workspace_pane() : nullptr;
	if (source_pane == nullptr || destination_pane == nullptr) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Could not resolve the source and destination panes for the empty-pane drop.");
		return false;
	}

	if (destination_pane->get_tab_count() != 0 || destination_pane->has_bridge_content() ||
			destination_pane->get_chrome_host() == nullptr || destination_pane->get_chrome_host()->is_visible() ||
			destination_pane->get_empty_placeholder() == nullptr || !destination_pane->get_empty_placeholder()->is_visible()) {
		r_failure = _failure_with_message(p_driver, p_workflow, "The destination pane is not a genuinely empty pane showing its placeholder.");
		return false;
	}

	EditorTileDropOverlay *destination_overlay = destination_pane->get_drop_overlay();
	if (destination_overlay == nullptr) {
		r_failure = _failure_with_message(p_driver, p_workflow, "The empty destination pane has no drop overlay.");
		return false;
	}
	const ObjectID destination_overlay_id = destination_overlay->get_instance_id();
	const Rect2 overlay_rect_before_drag = destination_overlay->get_global_rect();
	if (!destination_overlay->is_visible_in_tree() || overlay_rect_before_drag.size.x < 4 || overlay_rect_before_drag.size.y < 4) {
		EditorAutomationAcceptanceWorkflow::Result fail;
		fail.ok = false;
		fail.workflow = p_workflow;
		fail.message = "The empty destination pane's drop overlay is not visible and sized, so nothing can be dropped onto it.";
		Dictionary details = p_driver.make_failure_details(p_leg.step);
		details["overlay_visible"] = destination_overlay->is_visible_in_tree();
		details["overlay_rect"] = overlay_rect_before_drag;
		details["destination_leaf_rect"] = destination_leaf->get_global_rect();
		fail.details = details;
		r_failure = fail;
		return false;
	}

	TabBar *source_tab_strip = source_pane->get_tab_strip();
	const int source_tab_index = source_pane->find_scene_tab_index(p_leg.scene_index);
	if (source_tab_strip == nullptr || source_tab_index < 0) {
		r_failure = _failure_with_message(p_driver, p_workflow, "The dragged scene has no tab in the source pane's strip.");
		return false;
	}

	const Vector2 grab_point = source_tab_strip->get_global_transform().xform(source_tab_strip->get_tab_rect(source_tab_index).get_center());
	// The overlay's own center is the rosette's center region, which moves the tab
	// into the destination pane rather than splitting it.
	const Vector2 drop_point = overlay_rect_before_drag.get_center();

	EditorNode *editor_node = EditorNode::get_singleton();
	Viewport *viewport = editor_node != nullptr ? editor_node->get_viewport() : nullptr;
	if (viewport == nullptr) {
		r_failure = _failure_with_message(p_driver, p_workflow, "No viewport is available for pointer input.");
		return false;
	}

	PackedStringArray pointer_events;
	const EditorAutomationInputModifiers modifiers;
	if (!EditorAutomationInput::begin_mouse_gesture(viewport, grab_point, MouseButton::LEFT, modifiers, pointer_events)) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Could not press the pointer on the source tab.");
		return false;
	}
	if (!EditorAutomationInput::move_mouse_gesture(viewport, drop_point, Vector<Vector2>(), modifiers, pointer_events)) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Could not drag the pointer onto the empty destination pane.");
		return false;
	}
	if (!viewport->gui_is_dragging()) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Dragging the tab did not start a workspace tab drag.");
		return false;
	}

	// The overlay is re-resolved by id: the gesture advances frames, and the drag
	// itself relayouts the boards.
	destination_overlay = ObjectDB::get_instance<EditorTileDropOverlay>(destination_overlay_id);
	if (destination_overlay == nullptr) {
		r_failure = _failure_with_message(p_driver, p_workflow, "The destination overlay did not survive the drag gesture.");
		return false;
	}

	Dictionary drag_diagnostics;
	const bool overlay_visible = destination_overlay->is_visible_in_tree();
	const Rect2 overlay_rect = destination_overlay->get_global_rect();
	const bool overlay_hit_testable = destination_overlay->get_mouse_filter() == Control::MOUSE_FILTER_STOP;
	const bool overlay_painting = destination_overlay->is_drag_active();
	const EditorSceneWorkspace::TileDropRegion aimed_region = destination_overlay->get_hovered_region();
	drag_diagnostics["overlay_visible"] = overlay_visible;
	drag_diagnostics["overlay_rect"] = overlay_rect;
	drag_diagnostics["overlay_hit_testable"] = overlay_hit_testable;
	drag_diagnostics["overlay_painting"] = overlay_painting;
	drag_diagnostics["overlay_aimed_region"] = int(aimed_region);
	drag_diagnostics["grab_point"] = grab_point;
	drag_diagnostics["drop_point"] = drop_point;
	drag_diagnostics["pointer_events"] = pointer_events;
	if (!overlay_visible || overlay_rect.size.x < 4 || overlay_rect.size.y < 4 || !overlay_hit_testable ||
			!overlay_painting || aimed_region != EditorSceneWorkspace::DROP_CENTER) {
		EditorAutomationAcceptanceWorkflow::Result fail;
		fail.ok = false;
		fail.workflow = p_workflow;
		fail.message = "The empty destination pane's drop overlay did not become a sized, hit-testable, center-aimed drop target during the drag.";
		Dictionary details = p_driver.make_failure_details(p_leg.step);
		details["drag_diagnostics"] = drag_diagnostics;
		details["editor_log"] = p_driver.read_editor_log();
		fail.details = details;
		r_failure = fail;
		EditorAutomationInput::end_mouse_gesture(viewport, drop_point, modifiers, pointer_events);
		return false;
	}

	if (!EditorAutomationInput::end_mouse_gesture(viewport, drop_point, modifiers, pointer_events)) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Could not release the pointer over the empty destination pane.");
		return false;
	}
	p_driver.flush_frames(30);
	if (!p_driver.wait_workspace_settled(10000)) {
		r_failure = _failure_from_driver(p_driver, p_workflow, "Workspace did not settle after the empty-pane drop.");
		return false;
	}

	// Assert against p_leg.step's own marker -- set before the drag began -- rather
	// than opening a fresh step here. A drop that lands on a non-active board still
	// has to route its post-drop focus call through EditorNode, and that call has to
	// stay error-free across the drop itself, not just after a new step boundary
	// that would fence the error out of this assertion's window.
	if (!p_driver.assert_no_new_errors_since_step()) {
		r_failure = _failure_from_driver(p_driver, p_workflow);
		return false;
	}

	p_driver.set_step(p_leg.step + "_verify");

	EditorData &editor_data = EditorNode::get_editor_data();
	WorkspaceLeafNode *landed_leaf = p_leg.destination_workspace->get_leaf_by_id(p_leg.destination_leaf_id);
	WorkspacePane *landed_pane = landed_leaf != nullptr ? landed_leaf->get_workspace_pane() : nullptr;
	// Moving the only tab out of a board leaves that board's final pane empty; the
	// last pane in a workspace is never collapsed.
	WorkspaceLeafNode *vacated_leaf = p_leg.source_workspace->get_leaf_by_id(p_leg.source_leaf_id);
	WorkspacePane *vacated_pane = vacated_leaf != nullptr ? vacated_leaf->get_workspace_pane() : nullptr;
	const int landed_tile_id = editor_data.get_scene_tile(p_leg.scene_index);
	const int landed_tab_count = landed_pane != nullptr ? landed_pane->get_tab_count() : -1;
	const int vacated_tab_count = vacated_pane != nullptr ? vacated_pane->get_tab_count() : -1;
	if (landed_tile_id != p_leg.destination_leaf_id || landed_tab_count != 1 || vacated_pane == nullptr || vacated_tab_count != 0) {
		EditorAutomationAcceptanceWorkflow::Result fail;
		fail.ok = false;
		fail.workflow = p_workflow;
		fail.message = "Dropping the tab on the empty pane did not move it and leave the source pane empty.";
		Dictionary details = p_driver.make_failure_details(p_leg.step + "_verify");
		details["source_leaf_id"] = p_leg.source_leaf_id;
		details["destination_leaf_id"] = p_leg.destination_leaf_id;
		details["landed_tile_id"] = landed_tile_id;
		details["landed_tab_count"] = landed_tab_count;
		details["vacated_tab_count"] = vacated_tab_count;
		details["drag_diagnostics"] = drag_diagnostics;
		details["editor_log"] = p_driver.read_editor_log();
		fail.details = details;
		r_failure = fail;
		return false;
	}

	// The null-target-leaf failure this workflow guards against is reported through
	// the error handler rather than by returning, so the log has to be clean too.
	if (!p_driver.assert_no_new_errors_since_step()) {
		r_failure = _failure_from_driver(p_driver, p_workflow);
		return false;
	}
	return true;
}

} // namespace

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_basic_scene_editing(EditorWorkflowTestDriver &p_driver, const String &p_scene_path) {
	Result result;
	result.workflow = "basic_scene_editing";

	p_driver.begin_workflow();

	// Setup: open the fixture scene directly. Post-setup steps use automation only.
	p_driver.set_step("setup_open_scene");
#ifdef TOOLS_ENABLED
	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node == nullptr || !editor_node->is_editor_ready()) {
		result.ok = false;
		result.message = "EditorNode is not ready.";
		return result;
	}
	if (editor_node->load_scene(p_scene_path) != OK) {
		result.ok = false;
		result.message = vformat("Failed to load setup scene '%s'.", p_scene_path);
		return result;
	}
#endif
	p_driver.flush_frames(30);

	// A loaded project must report project mode with the workspace exposed, the
	// inverse of the projectless launcher contract.
	p_driver.set_step("verify_project_mode");
	{
		const Dictionary mode_state = p_driver.read_editor_state();
		if (!(bool)mode_state.get("project_loaded", false)) {
			return _failure_with_message(p_driver, result.workflow, "Loaded project should report project_loaded == true.");
		}
		if (String(mode_state.get("mode", String())) != "project") {
			return _failure_with_message(p_driver, result.workflow, "Loaded project should report mode == project.");
		}
		if ((bool)mode_state.get("projectless_shell", true)) {
			return _failure_with_message(p_driver, result.workflow, "Loaded project must not report projectless_shell.");
		}
		if (!(bool)mode_state.get("workspace_exposed", false)) {
			return _failure_with_message(p_driver, result.workflow, "Loaded project should expose the workspace.");
		}
	}

	// 1. Open the Create/Add Node dialog from the focused tile's scene tree dock.
	p_driver.set_step("focus_scene_tree_dock");
	if (!p_driver.require_ok(p_driver.run_command("docks/open_scene"), "focus_scene_tree_dock")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	p_driver.flush_frames(10);

	p_driver.set_step("open_add_child_node_dialog");
	SceneTreeDock *scene_dock = EditorNode::get_singleton()->get_focused_scene_tree_dock();
	if (scene_dock == nullptr) {
		result.ok = false;
		result.message = "Focused scene tree dock is unavailable.";
		return result;
	}
	scene_dock->open_add_child_dialog();
	p_driver.flush_frames(10);

	// 2. Search for Node2D and create it through the dialog.
	p_driver.set_step("search_node2d");
	Dictionary search_field_base;
	search_field_base["role"] = "text_field";
	search_field_base["name"] = "Search";
	Dictionary search_field = _selector_within_modal(search_field_base);
	Dictionary search_args;
	search_args["text"] = "Node2D";
	if (!p_driver.require_ok(p_driver.act(search_field, "set_text", search_args), "search_node2d")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	p_driver.flush_frames(10);

	p_driver.set_step("select_node2d_match");
	Dictionary node2d_base;
	node2d_base["role"] = "tree_item";
	node2d_base["name"] = "Node2D";
	Dictionary node2d_item = _selector_within_modal(node2d_base);
	node2d_item["nth"] = 0;
	if (!p_driver.require_ok(p_driver.act(node2d_item, "select"), "select_node2d_match")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	p_driver.flush_frames(3);

	p_driver.set_step("confirm_create_node");
	Dictionary create_button_base;
	create_button_base["role"] = "button";
	create_button_base["name"] = "Create";
	Dictionary create_button = _selector_within_modal(create_button_base);
	if (!p_driver.require_ok(p_driver.act(create_button, "click"), "confirm_create_node")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	p_driver.flush_frames(20);

	// 3. Confirm Node2D is selected in the scene tree.
	p_driver.set_step("verify_node2d_selected");
	if (!p_driver.wait_editor_idle(5000)) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	if (!p_driver.assert_selected_node_class("Node2D", "Main")) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Expected the new Node2D child to be selected in the scene tree.";
		fail.details = p_driver.make_failure_details("verify_node2d_selected");
		return fail;
	}

	// 4. Edit the Visible property through the inspector property row UI.
	p_driver.set_step("open_inspector_dock");
	if (!p_driver.require_ok(p_driver.run_command("docks/open_inspector"), "open_inspector_dock")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
#ifdef TOOLS_ENABLED
	if (editor_node != nullptr) {
		editor_node->edit_current();
	}
#endif
	p_driver.flush_frames(30);

	p_driver.set_step("expand_inspector");
	_expand_inspector(p_driver);
	if (p_driver.has_failed()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	p_driver.set_step("edit_inspector_visible");
	const Dictionary visible_property = _inspector_visible_property_selector();
	const uint64_t inspector_deadline = OS::get_singleton()->get_ticks_msec() + 15000;
	bool edited_visible = false;
	int last_match_count = 0;
	while (OS::get_singleton()->get_ticks_msec() < inspector_deadline) {
		const Dictionary found = p_driver.find(visible_property);
		last_match_count = (int)found.get("match_count", 0);
		if ((bool)found.get("ok", false) && last_match_count >= 1) {
			Dictionary act_selector = visible_property;
			if (last_match_count > 1) {
				act_selector["nth"] = 0;
			}
			if (p_driver.require_ok(p_driver.act(act_selector, "click"), "edit_inspector_visible")) {
				edited_visible = true;
				break;
			}
			return _failure_from_driver(p_driver, result.workflow);
		}
		p_driver.flush_frames(5);
	}
	if (!edited_visible) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Could not find the Visible inspector property row.";
		Dictionary details;
		details["step"] = "edit_inspector_visible";
		details["last_match_count"] = last_match_count;
		const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_editor();
		details["snapshot_element_count"] = snapshot.get_element_count();
		int property_row_hits = 0;
		for (int i = 0; i < snapshot.get_element_count(); i++) {
			if (snapshot.get_element(i).role == "property_row") {
				property_row_hits++;
			}
		}
		details["property_row_hits"] = property_row_hits;
		fail.details = details;
		return fail;
	}
	p_driver.flush_frames(10);

	p_driver.set_step("verify_undo_unsaved_state");
	if (!p_driver.assert_scene_unsaved()) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Inspector edit did not mark the scene unsaved (undo/redo history).";
		fail.details = p_driver.make_failure_details("verify_undo_unsaved_state");
		return fail;
	}

	// 5. Save the scene through the command palette.
	p_driver.set_step("save_scene");
	if (!p_driver.require_ok(p_driver.run_command("editor/save_scene"), "save_scene")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	p_driver.flush_frames(30);

	p_driver.set_step("wait_after_save");
	if (!p_driver.wait_editor_idle(15000)) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	// 6. Run and stop the current scene through command palette paths.
	p_driver.set_step("run_current_scene");
	if (!p_driver.require_ok(p_driver.run_command("editor/run_current_scene"), "run_current_scene")) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	p_driver.set_step("wait_for_playing");
	if (!p_driver.assert_playing(true, 20000)) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Scene did not enter playing state after run_current_scene.";
		fail.details = p_driver.make_failure_details("wait_for_playing");
		return fail;
	}

	p_driver.set_step("stop_running_scene");
	if (!p_driver.require_ok(p_driver.run_command("editor/stop_running_project"), "stop_running_scene")) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	p_driver.set_step("wait_for_stopped");
	if (!p_driver.assert_playing(false, 20000)) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Scene did not stop after stop_running_project.";
		fail.details = p_driver.make_failure_details("wait_for_stopped");
		return fail;
	}

	// 7. Assert no new editor errors during the workflow.
	p_driver.set_step("assert_no_new_errors");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "Basic scene-editing automation workflow completed.";
	return result;
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_canvas_2d_zoom_automation(EditorWorkflowTestDriver &p_driver, const String &p_scene_path) {
	Result result;
	result.workflow = "canvas_2d_zoom_automation";

	p_driver.begin_workflow();

	p_driver.set_step("setup_open_scene");
#ifdef TOOLS_ENABLED
	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node == nullptr || !editor_node->is_editor_ready()) {
		result.ok = false;
		result.message = "EditorNode is not ready.";
		return result;
	}
	if (editor_node->load_scene(p_scene_path) != OK) {
		result.ok = false;
		result.message = vformat("Failed to load setup scene '%s'.", p_scene_path);
		return result;
	}
#endif
	p_driver.flush_frames(30);

	p_driver.set_step("select_2d_workspace");
#ifdef TOOLS_ENABLED
	EditorMainScreen *main_screen = EditorNode::get_editor_main_screen();
	if (main_screen == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Editor main screen is unavailable.");
	}
	main_screen->select(EditorMainScreen::EDITOR_2D);
#endif
	p_driver.flush_frames(20);
#ifdef TOOLS_ENABLED
	if (main_screen->get_selected_index() != EditorMainScreen::EDITOR_2D) {
		return _failure_with_message(p_driver, result.workflow, "Failed to select the 2D workspace.");
	}
#endif

	p_driver.set_step("seed_canvas_zoom");
	Dictionary seed_args;
	seed_args["zoom"] = 1.0;
	const Dictionary seed_result = p_driver.act(Dictionary(), "set_canvas_2d_zoom", seed_args);
	if (!p_driver.require_ok(seed_result, "seed_canvas_zoom")) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	p_driver.set_step("capture_pre_zoom_state");
	const Dictionary pre_state = p_driver.read_editor_state();
	const Dictionary pre_view_2d = pre_state.get("view_2d", Dictionary());
	if (!(bool)pre_view_2d.get("zoom_settable", false)) {
		return _failure_with_message(p_driver, result.workflow, "Expected view_2d.zoom_settable before set_canvas_2d_zoom.");
	}
	Point2 scene_under_center_before;
	bool have_center_sample = false;
#ifdef TOOLS_ENABLED
	if (CanvasItemEditor *canvas_editor = CanvasItemEditor::get_singleton()) {
		if (CanvasItemEditorView *focused_view = canvas_editor->get_focused_view()) {
			if (Control *scrollable = focused_view->get_viewport_scrollable()) {
				const Point2 center = scrollable->get_size() / 2.0;
				const CanvasItemEditorViewState &view_state = focused_view->get_view_state();
				scene_under_center_before = center / view_state.zoom + view_state.view_offset;
				have_center_sample = center.length_squared() > 0.0;
			}
		}
	}
#endif

	p_driver.set_step("set_canvas_2d_zoom");
	Dictionary zoom_args;
	zoom_args["zoom"] = 2.0;
	const Dictionary zoom_result = p_driver.act(Dictionary(), "set_canvas_2d_zoom", zoom_args);
	if (!p_driver.require_ok(zoom_result, "set_canvas_2d_zoom")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	if (!(bool)zoom_result.get("changed", false)) {
		return _failure_with_message(p_driver, result.workflow, "Expected set_canvas_2d_zoom to report changed:true for zoom 2.0.");
	}
	if (!Math::is_equal_approx(real_t(zoom_result.get("effective_zoom", 0.0)), real_t(2.0))) {
		return _failure_with_message(p_driver, result.workflow, "Expected effective_zoom == 2.0.");
	}
	if (!Math::is_equal_approx(real_t(zoom_result.get("requested_zoom", 0.0)), real_t(2.0))) {
		return _failure_with_message(p_driver, result.workflow, "Expected requested_zoom == 2.0.");
	}
	if (!zoom_result.has("tile_id")) {
		return _failure_with_message(p_driver, result.workflow, "Expected tile_id in set_canvas_2d_zoom result.");
	}

	p_driver.set_step("verify_view_2d_zoom");
	const Dictionary state = p_driver.read_editor_state();
	const Dictionary view_2d = state.get("view_2d", Dictionary());
	if (!(bool)view_2d.get("supported", false)) {
		return _failure_with_message(p_driver, result.workflow, "view_2d is unsupported after setting canvas zoom.");
	}
	if (!Math::is_equal_approx(real_t(view_2d.get("zoom", 0.0)), real_t(2.0))) {
		return _failure_with_message(p_driver, result.workflow, "Expected read_editor_state.view_2d.zoom == 2.0.");
	}

	const Vector2 post_offset = view_2d.get("view_offset", Vector2());
#ifdef TOOLS_ENABLED
	p_driver.set_step("verify_center_anchor");
	if (have_center_sample) {
		if (CanvasItemEditor *canvas_editor = CanvasItemEditor::get_singleton()) {
			if (CanvasItemEditorView *focused_view = canvas_editor->get_focused_view()) {
				if (Control *scrollable = focused_view->get_viewport_scrollable()) {
					const Point2 center = scrollable->get_size() / 2.0;
					const CanvasItemEditorViewState &view_state = focused_view->get_view_state();
					const Point2 scene_under_center_after = center / view_state.zoom + view_state.view_offset;
					// Integer zoom alignment can nudge the offset by a sub-pixel scene amount;
					// require the anchored scene point to stay within one screen pixel.
					const real_t screen_drift = scene_under_center_before.distance_to(scene_under_center_after) * view_state.zoom;
					if (screen_drift > 1.0 + CMP_EPSILON) {
						return _failure_with_message(p_driver, result.workflow, "Zoom around viewport center moved the anchored scene point.");
					}
				}
			}
		}
	}
#endif

	p_driver.set_step("verify_idempotent_zoom");
	const Dictionary repeat_result = p_driver.act(Dictionary(), "set_canvas_2d_zoom", zoom_args);
	if (!p_driver.require_ok(repeat_result, "verify_idempotent_zoom")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	if ((bool)repeat_result.get("changed", true)) {
		return _failure_with_message(p_driver, result.workflow, "Expected repeating set_canvas_2d_zoom(2.0) to report changed:false.");
	}
	const Dictionary repeat_state = p_driver.read_editor_state();
	const Dictionary repeat_view = repeat_state.get("view_2d", Dictionary());
	if (!Vector2(repeat_view.get("view_offset", Vector2())).is_equal_approx(post_offset)) {
		return _failure_with_message(p_driver, result.workflow, "Idempotent zoom changed view_offset.");
	}

	p_driver.set_step("reject_out_of_range_zoom");
	// Call the driver directly so the expected failure does not poison workflow failure state.
	Dictionary out_of_range_args;
	out_of_range_args["zoom"] = 1000.0;
	const EditorAutomationSnapshot out_of_range_snapshot;
	const EditorAutomationActionResult out_of_range_action = EditorAutomationDriver::perform(
			out_of_range_snapshot, "set_canvas_2d_zoom", Dictionary(), out_of_range_args);
	const Dictionary out_of_range_result = out_of_range_action.to_dictionary();
	if ((bool)out_of_range_result.get("ok", true)) {
		return _failure_with_message(p_driver, result.workflow, "Expected set_canvas_2d_zoom(1000) to fail.");
	}
	if (String(out_of_range_result.get("kind", String())) != "value_out_of_range") {
		return _failure_with_message(p_driver, result.workflow, "Expected kind value_out_of_range for oversized zoom.");
	}
	if (!out_of_range_result.has("minimum") || !out_of_range_result.has("maximum") || !out_of_range_result.has("requested_zoom")) {
		return _failure_with_message(p_driver, result.workflow, "Expected minimum/maximum/requested_zoom on value_out_of_range.");
	}
	if (!Math::is_equal_approx(real_t(out_of_range_result.get("requested_zoom", 0.0)), real_t(1000.0))) {
		return _failure_with_message(p_driver, result.workflow, "Expected requested_zoom == 1000.0 on value_out_of_range.");
	}
	if (!(real_t(out_of_range_result.get("minimum", 0.0)) < real_t(out_of_range_result.get("maximum", 0.0)))) {
		return _failure_with_message(p_driver, result.workflow, "Expected minimum < maximum on value_out_of_range.");
	}
	const Dictionary unchanged_state = p_driver.read_editor_state();
	const Dictionary unchanged_view = unchanged_state.get("view_2d", Dictionary());
	if (!Math::is_equal_approx(real_t(unchanged_view.get("zoom", 0.0)), real_t(2.0))) {
		return _failure_with_message(p_driver, result.workflow, "Out-of-range zoom mutated view_2d.zoom.");
	}

	p_driver.set_step("run_inapplicable_shortcut");
#ifdef TOOLS_ENABLED
	Ref<Shortcut> orphan_shortcut;
	orphan_shortcut.instantiate();
	orphan_shortcut->set_name("Automation Orphan Shortcut");
	Ref<InputEventKey> orphan_key;
	orphan_key.instantiate();
	orphan_key->set_keycode(Key::F21);
	Array orphan_events;
	orphan_events.push_back(orphan_key);
	orphan_shortcut->set_events(orphan_events);
	EditorSettings::get_singleton()->add_shortcut("automation/orphan_shortcut_probe", orphan_shortcut);

	EditorAutomationMCPDispatcher dispatcher;
	Dictionary run_params;
	run_params["name"] = "run_command";
	Dictionary run_args;
	run_args["command"] = "automation/orphan_shortcut_probe";
	run_params["arguments"] = run_args;
	Dictionary request;
	request["jsonrpc"] = "2.0";
	request["id"] = 1;
	request["method"] = "tools/call";
	request["params"] = run_params;
	const Dictionary run_response = dispatcher.handle_message(request);
	EditorSettings::get_singleton()->remove_shortcut("automation/orphan_shortcut_probe");

	const Dictionary run_result = run_response.get("result", Dictionary());
	const Dictionary shortcut_result = run_result.get("structuredContent", Dictionary());
	if (!(bool)run_result.get("isError", false)) {
		return _failure_with_message(p_driver, result.workflow, "Expected MCP isError:true for unhandled shortcut.");
	}
	if ((bool)shortcut_result.get("ok", true)) {
		return _failure_with_message(p_driver, result.workflow, "Expected inapplicable standalone shortcut to fail.");
	}
	if (String(shortcut_result.get("kind", String())) != "shortcut_unhandled") {
		return _failure_with_message(p_driver, result.workflow, vformat("Expected shortcut_unhandled, got '%s'.", String(shortcut_result.get("kind", String()))));
	}
	if (String(shortcut_result.get("route", String())) != "shortcut") {
		return _failure_with_message(p_driver, result.workflow, "Expected route shortcut for unhandled shortcut.");
	}
#endif

	p_driver.set_step("assert_no_new_errors");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "Canvas 2D zoom automation workflow completed.";
	return result;
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_close_last_scene_empty_pane(EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "close_last_scene_empty_pane";

	p_driver.begin_workflow();

	p_driver.set_step("verify_initial_scene");
	Dictionary state = p_driver.read_editor_state();
	Array open_scenes = state.get("open_scenes", Array());
	if (open_scenes.size() != 1 || String(state.get("active_scene_path", String())) != MIXED_WORKSPACE_SCENE) {
		return _failure_with_message(p_driver, result.workflow, "Expected the workflow to start with one active scene.");
	}

	p_driver.set_step("close_last_scene");
	if (!p_driver.require_ok(p_driver.run_command("editor/close_scene"), "close_last_scene")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	p_driver.flush_frames(5);
	if (!p_driver.wait_workspace_settled(5000)) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	p_driver.set_step("verify_empty_pane");
	state = p_driver.read_editor_state();
	open_scenes = state.get("open_scenes", Array());
	if (!open_scenes.is_empty()) {
		return _failure_with_message(p_driver, result.workflow, "Closing the last scene left an open scene entry.");
	}
	if ((int)state.get("active_scene_index", 0) != -1 || !String(state.get("active_scene_path", String())).is_empty()) {
		return _failure_with_message(p_driver, result.workflow, "Closing the last scene did not leave the editor in the no-scene state.");
	}

	const Dictionary workspace = state.get("workspace", Dictionary());
	if (!(bool)workspace.get("supported", false) || (int)workspace.get("tile_count", 0) != 1) {
		return _failure_with_message(p_driver, result.workflow, "Closing the last scene did not preserve one workspace pane.");
	}
	const Array tiles = workspace.get("tiles", Array());
	if (tiles.size() != 1) {
		return _failure_with_message(p_driver, result.workflow, "Workspace state did not report exactly one tile after closing the last scene.");
	}
	const Dictionary tile = tiles[0];
	if ((int)tile.get("current_scene", 0) != -1) {
		return _failure_with_message(p_driver, result.workflow, "The remaining workspace tile still points at a scene.");
	}
	const Array tile_scenes = tile.get("scenes", Array());
	if (!tile_scenes.is_empty()) {
		return _failure_with_message(p_driver, result.workflow, "The remaining workspace tile still lists scene tabs.");
	}

	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "EditorNode singleton was unavailable after closing the last scene.");
	}

	p_driver.set_step("close_scene_api_after_empty");
	if (editor_node->close_scene()) {
		return _failure_with_message(p_driver, result.workflow, "Closing a no-scene editor state reported success.");
	}
	p_driver.flush_frames(5);

	p_driver.set_step("save_empty_layout");
	editor_node->save_editor_layout_delayed();
	p_driver.flush_frames(120);
	if (!p_driver.wait_editor_idle(10000)) {
		return _failure_from_driver(p_driver, result.workflow, "Editor did not become idle after saving the empty layout.");
	}
	Ref<ConfigFile> saved_layout;
	saved_layout.instantiate();
	const String layout_path = EditorPaths::get_singleton()->get_project_settings_dir().path_join("editor_layout.cfg");
	if (saved_layout->load(layout_path) != OK) {
		return _failure_with_message(p_driver, result.workflow, "The empty layout save did not write editor_layout.cfg.");
	}
	if ((bool)saved_layout->get_value("EditorNode", "current_scene_active", true)) {
		return _failure_with_message(p_driver, result.workflow, "The empty layout save did not persist the no-current-scene state.");
	}

	p_driver.set_step("create_root_after_empty");
	Node2D *new_root = memnew(Node2D);
	new_root->set_name("RootAfterEmpty");
	editor_node->get_focused_scene_tree_dock()->add_root_node(new_root);
	p_driver.flush_frames(20);
	if (editor_node->get_edited_scene() != new_root) {
		return _failure_with_message(p_driver, result.workflow, "Creating a root from the empty pane did not install it as the edited scene.");
	}
	state = p_driver.read_editor_state();
	open_scenes = state.get("open_scenes", Array());
	if (open_scenes.size() != 1 || (int)state.get("active_scene_index", -1) != 0) {
		return _failure_with_message(p_driver, result.workflow, "Creating a root from the empty pane did not create a new unsaved scene tab.");
	}

	p_driver.set_step("assert_no_new_errors");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "Closing the last scene left one empty workspace pane.";
	Dictionary details;
	details["workspace"] = workspace;
	details["active_scene_index"] = state.get("active_scene_index", -1);
	details["open_scenes"] = open_scenes;
	result.details = details;
	return result;
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_split_scene_root_button_context(
		EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "split_scene_root_button_context";

	p_driver.begin_workflow();

#ifdef TOOLS_ENABLED
	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node == nullptr || !editor_node->is_editor_ready()) {
		return _failure_with_message(p_driver, result.workflow, "EditorNode is not ready.");
	}

	p_driver.set_step("create_first_scene");
	SceneTreeDock *scene_dock = editor_node->get_focused_scene_tree_dock();
	if (scene_dock == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Focused scene tree dock is unavailable.");
	}
	Node2D *first_root = memnew(Node2D);
	first_root->set_name("First");
	scene_dock->add_root_node(first_root);
	p_driver.flush_frames(20);

	p_driver.set_step("create_second_scene");
	const int second_scene = editor_node->new_scene();
	if (second_scene != 1) {
		return _failure_with_message(p_driver, result.workflow, vformat("Expected second scene index 1, got %d.", second_scene));
	}
	scene_dock = editor_node->get_focused_scene_tree_dock();
	if (scene_dock == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Focused scene tree dock is unavailable after creating the second scene.");
	}
	Node2D *second_root = memnew(Node2D);
	second_root->set_name("Second");
	scene_dock->add_root_node(second_root);
	p_driver.flush_frames(20);

	p_driver.set_step("dock_second_scene_right");
	EditorSceneWorkspace *workspace = EditorNode::get_scene_workspace();
	if (workspace == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Scene workspace is unavailable.");
	}
	editor_node->handle_tile_scene_drop(0, (int)EditorSceneWorkspace::DROP_RIGHT, 0, 1);
	p_driver.flush_frames(30);
	if (!p_driver.wait_workspace_settled(5000)) {
		return _failure_from_driver(p_driver, result.workflow, "Workspace did not settle after docking the second scene.");
	}

	p_driver.set_step("verify_split_baseline");
	Dictionary state = p_driver.read_editor_state();
	const Dictionary workspace_state = state.get("workspace", Dictionary());
	const Array baseline_tiles = workspace_state.get("tiles", Array());
	if ((int)workspace_state.get("tile_count", 0) != 2 || baseline_tiles.size() != 2 ||
			(int)state.get("active_scene_index", -1) != 1 ||
			(int)workspace_state.get("focused_tile_id", -1) != 1) {
		return _failure_with_message(p_driver, result.workflow, "Docking the second scene did not create the expected focused two-pane workspace.");
	}

	ScenePaneTile *left_tile = workspace->get_tile_by_id(0);
	ScenePaneTile *right_tile = workspace->get_tile_by_id(1);
	if (left_tile == nullptr || right_tile == nullptr ||
			left_tile->get_scene_tree_dock() == nullptr || right_tile->get_scene_tree_dock() == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Could not resolve both scene tree docks after docking the second scene.");
	}
	auto get_dock_context_root_name = [](SceneTreeDock *p_dock) {
		EditorSceneContext *context = p_dock ? p_dock->get_scene_context() : nullptr;
		Node *root = context ? context->get_scene_root_node() : nullptr;
		return root ? String(root->get_name()) : String("<none>");
	};
	const String left_tile_root = left_tile->get_current_scene_root() ? String(left_tile->get_current_scene_root()->get_name()) : String("<none>");
	const String right_tile_root = right_tile->get_current_scene_root() ? String(right_tile->get_current_scene_root()->get_name()) : String("<none>");
	const String left_dock_root = get_dock_context_root_name(left_tile->get_scene_tree_dock());
	const String right_dock_root = get_dock_context_root_name(right_tile->get_scene_tree_dock());
	if (left_tile_root != "First" || right_tile_root != "Second" || left_dock_root != "First" || right_dock_root != "Second") {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Docking the second scene did not keep the two scene panes bound to distinct scene roots.";
		Dictionary details = p_driver.make_failure_details("verify_split_baseline");
		details["editor_state"] = state;
		details["left_tile_root"] = left_tile_root;
		details["right_tile_root"] = right_tile_root;
		details["left_dock_root"] = left_dock_root;
		details["right_dock_root"] = right_dock_root;
		fail.details = details;
		return fail;
	}

	const String left_rendered_root = _rendered_scene_tree_root_name(left_tile->get_scene_tree_dock());
	const String right_rendered_root = _rendered_scene_tree_root_name(right_tile->get_scene_tree_dock());
	if (left_rendered_root != "First" || right_rendered_root != "Second") {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Docking the second scene rendered the focused scene in both scene tree panes.";
		Dictionary details = p_driver.make_failure_details("verify_split_baseline");
		details["editor_state"] = state;
		details["left_rendered_root"] = left_rendered_root;
		details["right_rendered_root"] = right_rendered_root;
		details["left_dock_root"] = left_dock_root;
		details["right_dock_root"] = right_dock_root;
		fail.details = details;
		return fail;
	}

	p_driver.set_step("create_third_scene");
	if (!p_driver.require_ok(p_driver.run_command("editor/new_scene"), "create_third_scene")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	p_driver.flush_frames(30);

	state = p_driver.read_editor_state();
	const Dictionary post_workspace = state.get("workspace", Dictionary());
	if ((int)state.get("active_scene_index", -1) != 2 || (int)post_workspace.get("focused_tile_id", -1) != 1) {
		return _failure_with_message(p_driver, result.workflow, "Creating the third scene did not keep the right scene tile focused.");
	}

	p_driver.set_step("verify_root_dialog_decisions");
	const bool left_shows_root_dialog = left_tile->get_scene_tree_dock()->should_show_create_root_dialog();
	const bool right_shows_root_dialog = right_tile->get_scene_tree_dock()->should_show_create_root_dialog();
	if (left_shows_root_dialog || !right_shows_root_dialog) {
		EditorSceneContext *left_context = left_tile->get_scene_tree_dock()->get_scene_context();
		EditorSceneContext *right_context = right_tile->get_scene_tree_dock()->get_scene_context();
		Node *left_context_root = left_context ? left_context->get_scene_root_node() : nullptr;
		Node *right_context_root = right_context ? right_context->get_scene_root_node() : nullptr;

		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = vformat("Root dialog decisions were not context-aware: left=%s right=%s.",
				left_shows_root_dialog ? "true" : "false",
				right_shows_root_dialog ? "true" : "false");
		Dictionary details = p_driver.make_failure_details("verify_root_dialog_decisions");
		details["editor_state"] = state;
		details["left_context_root"] = left_context_root ? String(left_context_root->get_name()) : String("<none>");
		details["right_context_root"] = right_context_root ? String(right_context_root->get_name()) : String("<none>");
		fail.details = details;
		return fail;
	}

	p_driver.set_step("verify_contextual_root_buttons");
	Dictionary root_button_selector;
	root_button_selector["role"] = "button";
	root_button_selector["name"] = "2D Scene";
	root_button_selector["visible_only"] = true;
	Dictionary root_buttons = p_driver.find(root_button_selector, 10);
	const int root_button_count = (int)root_buttons.get("match_count", 0);
	if (root_button_count != 1) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = vformat(
				"Expected only the focused empty scene tile to show root buttons, found %d visible 2D Scene buttons.",
				root_button_count);
		Dictionary details = p_driver.make_failure_details("verify_contextual_root_buttons");
		details["root_buttons"] = root_buttons;
		details["editor_state"] = state;
		fail.details = details;
		return fail;
	}

	const Array root_button_elements = root_buttons.get("elements", Array());
	if (!root_button_elements.is_empty()) {
		const Dictionary element = root_button_elements[0];
		const Dictionary metadata = element.get("metadata", Dictionary());
		if ((int)metadata.get("tile_id", -1) != 1) {
			Result fail;
			fail.ok = false;
			fail.workflow = result.workflow;
			fail.message = "The remaining visible root button belongs to the wrong workspace tile.";
			Dictionary details = p_driver.make_failure_details("verify_contextual_root_buttons");
			details["root_buttons"] = root_buttons;
			details["editor_state"] = state;
			fail.details = details;
			return fail;
		}
	}

	p_driver.set_step("create_third_scene_root");
	workspace->request_leaf_focus(0);
	p_driver.flush_frames(30);
	Dictionary right_root_button_selector = root_button_selector;
	Dictionary right_root_button_metadata;
	right_root_button_metadata["tile_id"] = 1;
	right_root_button_selector["metadata"] = right_root_button_metadata;
	if (!p_driver.require_ok(p_driver.act(right_root_button_selector, "click", Dictionary(), "semantic"), "click_right_root_button_from_left_focus")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	p_driver.flush_frames(30);
	const String created_root_name = "Node2D";
	if (left_tile->get_current_scene_root() == nullptr || right_tile->get_current_scene_root() == nullptr ||
			String(left_tile->get_current_scene_root()->get_name()) != "First" ||
			String(right_tile->get_current_scene_root()->get_name()) != created_root_name ||
			get_dock_context_root_name(left_tile->get_scene_tree_dock()) != "First" ||
			get_dock_context_root_name(right_tile->get_scene_tree_dock()) != created_root_name ||
			_rendered_scene_tree_root_name(left_tile->get_scene_tree_dock()) != "First" ||
			_rendered_scene_tree_root_name(right_tile->get_scene_tree_dock()) != created_root_name) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Clicking the right root button while the left tile was focused did not keep the split panes bound to distinct scene roots.";
		Dictionary details = p_driver.make_failure_details("create_third_scene_root");
		details["editor_state"] = p_driver.read_editor_state();
		details["left_tile_root"] = left_tile->get_current_scene_root() ? String(left_tile->get_current_scene_root()->get_name()) : String("<none>");
		details["right_tile_root"] = right_tile->get_current_scene_root() ? String(right_tile->get_current_scene_root()->get_name()) : String("<none>");
		details["left_dock_root"] = get_dock_context_root_name(left_tile->get_scene_tree_dock());
		details["right_dock_root"] = get_dock_context_root_name(right_tile->get_scene_tree_dock());
		details["left_rendered_root"] = _rendered_scene_tree_root_name(left_tile->get_scene_tree_dock());
		details["right_rendered_root"] = _rendered_scene_tree_root_name(right_tile->get_scene_tree_dock());
		fail.details = details;
		return fail;
	}

	p_driver.set_step("focus_right_scene_tile_before_left_focus");
	workspace->request_leaf_focus(0);
	p_driver.flush_frames(30);
	workspace->request_leaf_focus(1);
	p_driver.flush_frames(30);
	state = p_driver.read_editor_state();
	const Dictionary right_focus_workspace = state.get("workspace", Dictionary());
	if (workspace->get_focused_leaf_id() != 1 ||
			(int)right_focus_workspace.get("focused_leaf_id", -1) != 1 ||
			(int)right_focus_workspace.get("focused_tile_id", -1) != 1 ||
			(int)state.get("active_scene_index", -1) != 2) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Expected the right scene tile to be active before probing left tile focus.";
		Dictionary details = p_driver.make_failure_details("focus_right_scene_tile_before_left_focus");
		details["editor_state"] = state;
		details["workspace_focused_leaf_id"] = workspace->get_focused_leaf_id();
		fail.details = details;
		return fail;
	}

	p_driver.set_step("focus_left_scene_tile");
	workspace->request_leaf_focus(0);
	state = p_driver.read_editor_state();
	const Dictionary immediate_focus_workspace = state.get("workspace", Dictionary());
	if (workspace->get_focused_leaf_id() != 0 ||
			(int)immediate_focus_workspace.get("focused_leaf_id", -1) != 0 ||
			(int)immediate_focus_workspace.get("focused_tile_id", -1) != 1 ||
			(int)state.get("focused_tile_id", -1) != 1 ||
			(int)state.get("active_scene_index", -1) != 2) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Requesting scene tile focus should update pane focus before activating the scene.";
		Dictionary details = p_driver.make_failure_details("focus_left_scene_tile_immediate");
		details["editor_state"] = state;
		details["workspace_focused_leaf_id"] = workspace->get_focused_leaf_id();
		fail.details = details;
		return fail;
	}
	p_driver.flush_frames(30);
	state = p_driver.read_editor_state();
	if ((int)state.get("active_scene_index", -1) != 0 ||
			_rendered_scene_tree_root_name(left_tile->get_scene_tree_dock()) != "First" ||
			_rendered_scene_tree_root_name(right_tile->get_scene_tree_dock()) != created_root_name) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Focusing the left scene tile changed the right scene tree pane's rendered root.";
		Dictionary details = p_driver.make_failure_details("focus_left_scene_tile");
		details["editor_state"] = state;
		details["left_rendered_root"] = _rendered_scene_tree_root_name(left_tile->get_scene_tree_dock());
		details["right_rendered_root"] = _rendered_scene_tree_root_name(right_tile->get_scene_tree_dock());
		fail.details = details;
		return fail;
	}

	p_driver.set_step("assert_no_new_errors");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "Split scene root buttons follow each tile's scene context.";
	Dictionary details;
	details["workspace"] = p_driver.read_editor_state().get("workspace", Dictionary());
	details["root_buttons"] = root_buttons;
	result.details = details;
	return result;
#else
	return _failure_with_message(p_driver, result.workflow, "Split scene root button workflow requires an editor (TOOLS_ENABLED) build.");
#endif
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_cross_board_tile_body_drop(
		EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "cross_board_tile_body_drop";

	p_driver.begin_workflow();

#ifdef TOOLS_ENABLED
	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node == nullptr || !editor_node->is_editor_ready()) {
		return _failure_with_message(p_driver, result.workflow, "EditorNode is not ready.");
	}

	p_driver.set_step("create_scene_root");
	SceneTreeDock *scene_dock = editor_node->get_focused_scene_tree_dock();
	if (scene_dock == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Focused scene tree dock is unavailable.");
	}
	Node2D *scene_root = memnew(Node2D);
	scene_root->set_name("Dragged");
	scene_dock->add_root_node(scene_root);
	p_driver.flush_frames(20);

	p_driver.set_step("add_destination_board");
	EditorBoardStrip *strip = EditorNode::get_board_strip();
	if (strip == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Board strip is unavailable.");
	}
	EditorBoard *destination_board = strip->add_board("Destination");
	if (destination_board == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Adding a second board failed.");
	}
	// Boards are re-resolved by id after every frame advance: activation, overview
	// layout and the drop itself all mutate the board list, so a raw pointer held
	// across the gesture could outlive its referent.
	const ObjectID destination_board_id = destination_board->get_instance_id();
	p_driver.flush_frames(20);

	EditorSceneWorkspace *source_workspace = strip->get_active_workspace();
	EditorSceneWorkspace *destination_workspace = destination_board->get_workspace();
	if (source_workspace == nullptr || destination_workspace == nullptr || source_workspace == destination_workspace) {
		return _failure_with_message(p_driver, result.workflow, "The two boards did not resolve to distinct workspaces.");
	}
	if (strip->get_active_index() != 0 || !destination_board->is_dormant()) {
		return _failure_with_message(p_driver, result.workflow, "The destination board is not the dormant, non-active board.");
	}

	EditorData &editor_data = EditorNode::get_editor_data();
	const int scene_index = editor_data.get_edited_scene();
	const int source_leaf_id = source_workspace->get_focused_leaf_id();
	const int destination_leaf_id = destination_workspace->get_focused_leaf_id();
	if (scene_index < 0 || editor_data.get_scene_tile(scene_index) != source_leaf_id) {
		return _failure_with_message(p_driver, result.workflow, "The dragged scene does not start on the active board's pane.");
	}

	p_driver.set_step("enter_overview");
	strip->set_overview(true);
	p_driver.flush_frames(30);
	Dictionary settled_condition;
	settled_condition["type"] = "board_transition_settled";
	if (!p_driver.require_ok(p_driver.wait_for(settled_condition, 10000), "enter_overview")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	if (!strip->is_overview_active()) {
		return _failure_with_message(p_driver, result.workflow, "The board overview did not come up.");
	}

	// Re-resolve every participant after the overview relayout rather than reusing
	// pointers captured before it.
	strip = EditorNode::get_board_strip();
	EditorBoard *live_destination_board = ObjectDB::get_instance<EditorBoard>(destination_board_id);
	if (strip == nullptr || live_destination_board == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The board strip or destination board did not survive the overview transition.");
	}
	source_workspace = strip->get_active_workspace();
	destination_workspace = live_destination_board->get_workspace();
	if (source_workspace == nullptr || destination_workspace == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "A board workspace did not survive the overview transition.");
	}

	// First leg: the destination board is dormant and holds nothing at all. It is
	// deliberately never seeded -- an empty pane is the state this workflow guards.
	Result leg_failure;
	EmptyPaneDropLeg dormant_leg;
	dormant_leg.step = "drop_tab_on_empty_dormant_board";
	dormant_leg.scene_index = scene_index;
	dormant_leg.source_leaf_id = source_leaf_id;
	dormant_leg.destination_leaf_id = destination_leaf_id;
	dormant_leg.source_workspace = source_workspace;
	dormant_leg.destination_workspace = destination_workspace;
	if (!_drop_scene_tab_on_empty_pane(p_driver, result.workflow, dormant_leg, leg_failure)) {
		return leg_failure;
	}

	// Second leg: drag the same tab back onto the board it came from, which the
	// first leg just left empty. That board is the active one, so this covers the
	// active-destination half of the same defect without seeding anything.
	strip = EditorNode::get_board_strip();
	live_destination_board = ObjectDB::get_instance<EditorBoard>(destination_board_id);
	if (strip == nullptr || live_destination_board == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The board strip or destination board did not survive the first drop.");
	}
	source_workspace = strip->get_active_workspace();
	destination_workspace = live_destination_board->get_workspace();
	if (source_workspace == nullptr || destination_workspace == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "A board workspace did not survive the first drop.");
	}
	if (strip->get_active_index() != 0) {
		return _failure_with_message(p_driver, result.workflow, "The first drop changed which board is active.");
	}

	EmptyPaneDropLeg return_leg;
	return_leg.step = "drop_tab_back_on_empty_active_board";
	return_leg.scene_index = scene_index;
	return_leg.source_leaf_id = destination_leaf_id;
	return_leg.destination_leaf_id = source_leaf_id;
	return_leg.source_workspace = destination_workspace;
	return_leg.destination_workspace = source_workspace;
	if (!_drop_scene_tab_on_empty_pane(p_driver, result.workflow, return_leg, leg_failure)) {
		return leg_failure;
	}

	result.ok = true;
	result.message = "A tab dropped on a genuinely empty pane moves to it on both a dormant and an active board.";
	Dictionary details;
	details["source_leaf_id"] = source_leaf_id;
	details["destination_leaf_id"] = destination_leaf_id;
	details["workspace"] = p_driver.read_editor_state().get("workspace", Dictionary());
	result.details = details;
	return result;
#else
	return _failure_with_message(p_driver, result.workflow, "Cross-board tile body drop workflow requires an editor (TOOLS_ENABLED) build.");
#endif
}

// #2071: handle_tile_tab_strip_drop calls _focus_tile(dest_leaf_id) unconditionally
// after a successful drop, but _focus_tile_internal resolves the leaf only through
// the active board's workspace. cross_board_tile_body_drop exercises the rosette
// overlay path onto an empty pane; this exercises the tab-strip path onto a pane
// that already has a resident tab, so the tab strip itself -- rather than the
// empty-pane placeholder -- is the real drop surface.
EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_cross_board_tab_strip_drop(
		EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "cross_board_tab_strip_drop";

	p_driver.begin_workflow();

#ifdef TOOLS_ENABLED
	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node == nullptr || !editor_node->is_editor_ready()) {
		return _failure_with_message(p_driver, result.workflow, "EditorNode is not ready.");
	}

	p_driver.set_step("create_dragged_scene");
	SceneTreeDock *scene_dock = editor_node->get_focused_scene_tree_dock();
	if (scene_dock == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Focused scene tree dock is unavailable.");
	}
	Node2D *dragged_root = memnew(Node2D);
	dragged_root->set_name("Dragged");
	scene_dock->add_root_node(dragged_root);
	p_driver.flush_frames(20);

	EditorData &editor_data = EditorNode::get_editor_data();
	const int dragged_scene_index = editor_data.get_edited_scene();
	if (dragged_scene_index < 0) {
		return _failure_with_message(p_driver, result.workflow, "The dragged scene has no editor-data index.");
	}

	p_driver.set_step("add_destination_board");
	EditorBoardStrip *strip = EditorNode::get_board_strip();
	if (strip == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Board strip is unavailable.");
	}
	EditorBoard *destination_board = strip->add_board("Destination");
	if (destination_board == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Adding a second board failed.");
	}
	// Boards are re-resolved by id after every frame advance, exactly as the
	// tile-body drop workflow re-resolves them.
	const ObjectID destination_board_id = destination_board->get_instance_id();
	p_driver.flush_frames(20);

	EditorSceneWorkspace *source_workspace = strip->get_active_workspace();
	EditorSceneWorkspace *destination_workspace = destination_board->get_workspace();
	if (source_workspace == nullptr || destination_workspace == nullptr || source_workspace == destination_workspace) {
		return _failure_with_message(p_driver, result.workflow, "The two boards did not resolve to distinct workspaces.");
	}
	if (strip->get_active_index() != 0 || !destination_board->is_dormant()) {
		return _failure_with_message(p_driver, result.workflow, "The destination board is not the dormant, non-active board.");
	}

	const int source_leaf_id = source_workspace->get_focused_leaf_id();
	if (editor_data.get_scene_tile(dragged_scene_index) != source_leaf_id) {
		return _failure_with_message(p_driver, result.workflow, "The dragged scene does not start on the active board's pane.");
	}

	// The destination pane needs a resident tab of its own so its tab strip -- not
	// the empty-pane placeholder -- is the visible drop surface. Seeded directly
	// through EditorData, the same low-level path EditorNode::new_scene() itself
	// goes through, so the dormant board never has to become active to acquire it.
	p_driver.set_step("seed_destination_board");
	WorkspaceLeafNode *destination_leaf = destination_workspace->get_focused_leaf();
	if (destination_leaf == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The destination board has no focused leaf.");
	}
	const int destination_leaf_id = destination_leaf->get_leaf_id();
	editor_data.register_tile(destination_leaf_id);
	const int resident_scene_index = editor_data.add_edited_scene(-1);
	if (editor_data.get_scene_tile(resident_scene_index) != destination_leaf_id) {
		editor_data.set_scene_tile(resident_scene_index, destination_leaf_id);
	}
	destination_workspace->sync_scene_tabs_from_editor_data();
	p_driver.flush_frames(20);

	WorkspacePane *destination_pane = destination_leaf->get_workspace_pane();
	if (destination_pane == nullptr || destination_pane->get_tab_count() != 1) {
		return _failure_with_message(p_driver, result.workflow, "Seeding the destination board's pane did not produce a resident tab.");
	}

	p_driver.set_step("enter_overview");
	strip->set_overview(true);
	p_driver.flush_frames(30);
	Dictionary settled_condition;
	settled_condition["type"] = "board_transition_settled";
	if (!p_driver.require_ok(p_driver.wait_for(settled_condition, 10000), "enter_overview")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	if (!strip->is_overview_active()) {
		return _failure_with_message(p_driver, result.workflow, "The board overview did not come up.");
	}

	// Re-resolve every participant after the overview relayout rather than reusing
	// pointers captured before it.
	strip = EditorNode::get_board_strip();
	EditorBoard *live_destination_board = ObjectDB::get_instance<EditorBoard>(destination_board_id);
	if (strip == nullptr || live_destination_board == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The board strip or destination board did not survive the overview transition.");
	}
	source_workspace = strip->get_active_workspace();
	destination_workspace = live_destination_board->get_workspace();
	if (source_workspace == nullptr || destination_workspace == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "A board workspace did not survive the overview transition.");
	}

	WorkspaceLeafNode *source_leaf = source_workspace->get_leaf_by_id(source_leaf_id);
	destination_leaf = destination_workspace->get_leaf_by_id(destination_leaf_id);
	WorkspacePane *source_pane = source_leaf != nullptr ? source_leaf->get_workspace_pane() : nullptr;
	destination_pane = destination_leaf != nullptr ? destination_leaf->get_workspace_pane() : nullptr;
	if (source_pane == nullptr || destination_pane == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The source or destination pane did not survive the overview transition.");
	}

	TabBar *source_tab_strip = source_pane->get_tab_strip();
	const int source_tab_index = source_pane->find_scene_tab_index(dragged_scene_index);
	if (source_tab_strip == nullptr || source_tab_index < 0) {
		return _failure_with_message(p_driver, result.workflow, "The dragged scene has no tab in the source pane's strip.");
	}
	TabBar *destination_tab_strip = destination_pane->get_tab_strip();
	if (destination_tab_strip == nullptr || destination_tab_strip->get_tab_count() != 1) {
		return _failure_with_message(p_driver, result.workflow, "The destination pane's tab strip did not survive the overview transition.");
	}

	const Rect2 destination_strip_rect_before_drag = destination_tab_strip->get_global_rect();
	if (!destination_tab_strip->is_visible_in_tree() || destination_strip_rect_before_drag.size.x < 4 || destination_strip_rect_before_drag.size.y < 4) {
		return _failure_with_message(p_driver, result.workflow, "The destination pane's tab strip is not visible and sized, so nothing can be dropped onto it.");
	}

	Viewport *viewport = editor_node->get_viewport();
	if (viewport == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "No viewport is available for pointer input.");
	}

	const Vector2 grab_point = source_tab_strip->get_global_transform().xform(source_tab_strip->get_tab_rect(source_tab_index).get_center());
	// Aimed at the resident tab's leading half so the requested strip position
	// (before the resident tab) matches the ordinal a scene-tab rebuild lands it
	// at anyway (scene tabs are ordered by EditorData index, and the dragged scene
	// was created before the resident one). Landing where requested keeps this
	// workflow isolated to the tab-strip drop's own focus call; a mismatch would
	// additionally trigger WorkspacePane::move_tab's reorder path, which has its
	// own, unrelated focus side effect.
	const Rect2 resident_tab_rect = destination_tab_strip->get_tab_rect(0);
	const Vector2 drop_local(resident_tab_rect.position.x + resident_tab_rect.size.width * 0.25f, resident_tab_rect.size.height * 0.5f);
	const Vector2 drop_point = destination_tab_strip->get_global_transform().xform(drop_local);

	p_driver.set_step("drop_tab_on_dormant_board_tab_strip");
	PackedStringArray pointer_events;
	const EditorAutomationInputModifiers modifiers;
	if (!EditorAutomationInput::begin_mouse_gesture(viewport, grab_point, MouseButton::LEFT, modifiers, pointer_events)) {
		return _failure_with_message(p_driver, result.workflow, "Could not press the pointer on the source tab.");
	}
	if (!EditorAutomationInput::move_mouse_gesture(viewport, drop_point, Vector<Vector2>(), modifiers, pointer_events)) {
		return _failure_with_message(p_driver, result.workflow, "Could not drag the pointer onto the destination pane's tab strip.");
	}
	if (!viewport->gui_is_dragging()) {
		return _failure_with_message(p_driver, result.workflow, "Dragging the tab did not start a workspace tab drag.");
	}
	if (!EditorAutomationInput::end_mouse_gesture(viewport, drop_point, modifiers, pointer_events)) {
		return _failure_with_message(p_driver, result.workflow, "Could not release the pointer over the destination pane's tab strip.");
	}
	p_driver.flush_frames(30);
	if (!p_driver.wait_workspace_settled(10000)) {
		return _failure_from_driver(p_driver, result.workflow, "Workspace did not settle after the tab-strip drop.");
	}

	// Assert against the drop_tab_on_dormant_board_tab_strip step's own marker,
	// set before the drag began, so the assertion window spans the drop itself
	// rather than a fresh step boundary opened after it -- the exact gap that let
	// the null-leaf focus error in #2071 pass unnoticed for the tile-body path.
	if (!p_driver.assert_no_new_errors_since_step()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	p_driver.set_step("verify_landed");
	WorkspaceLeafNode *landed_leaf = destination_workspace->get_leaf_by_id(destination_leaf_id);
	WorkspacePane *landed_pane = landed_leaf != nullptr ? landed_leaf->get_workspace_pane() : nullptr;
	WorkspaceLeafNode *vacated_leaf = source_workspace->get_leaf_by_id(source_leaf_id);
	WorkspacePane *vacated_pane = vacated_leaf != nullptr ? vacated_leaf->get_workspace_pane() : nullptr;
	const int landed_tile_id = editor_data.get_scene_tile(dragged_scene_index);
	const int landed_tab_count = landed_pane != nullptr ? landed_pane->get_tab_count() : -1;
	const int vacated_tab_count = vacated_pane != nullptr ? vacated_pane->get_tab_count() : -1;
	if (landed_tile_id != destination_leaf_id || landed_tab_count != 2 || vacated_pane == nullptr || vacated_tab_count != 0) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Dropping the tab on the dormant board's tab strip did not move it into that pane.";
		Dictionary details = p_driver.make_failure_details("verify_landed");
		details["source_leaf_id"] = source_leaf_id;
		details["destination_leaf_id"] = destination_leaf_id;
		details["landed_tile_id"] = landed_tile_id;
		details["landed_tab_count"] = landed_tab_count;
		details["vacated_tab_count"] = vacated_tab_count;
		fail.details = details;
		return fail;
	}

	// The drop itself must not have switched which board is active -- that is a
	// distinct guarantee from which leaf holds editor-wide focus, and #2071 is
	// scoped to the latter's null-leaf error, not every focus-adjacent side effect
	// a tab move can have on the destination pane.
	if (strip->get_active_index() != 0) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "The cross-board drop changed which board is active.";
		Dictionary details = p_driver.make_failure_details("verify_landed");
		details["active_index"] = strip->get_active_index();
		fail.details = details;
		return fail;
	}

	result.ok = true;
	result.message = "A tab dropped on a dormant board's tab strip moves without logging a null-leaf focus error.";
	Dictionary details;
	details["source_leaf_id"] = source_leaf_id;
	details["destination_leaf_id"] = destination_leaf_id;
	details["workspace"] = p_driver.read_editor_state().get("workspace", Dictionary());
	result.details = details;
	return result;
#else
	return _failure_with_message(p_driver, result.workflow, "Cross-board tab strip drop workflow requires an editor (TOOLS_ENABLED) build.");
#endif
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_continuous_drag_arms_drop_overlay(
		EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "continuous_drag_arms_drop_overlay";

	p_driver.begin_workflow();

#ifdef TOOLS_ENABLED
	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node == nullptr || !editor_node->is_editor_ready()) {
		return _failure_with_message(p_driver, result.workflow, "EditorNode is not ready.");
	}

	p_driver.set_step("create_dragged_scene");
	SceneTreeDock *scene_dock = editor_node->get_focused_scene_tree_dock();
	if (scene_dock == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Focused scene tree dock is unavailable.");
	}
	Node2D *dragged_root = memnew(Node2D);
	dragged_root->set_name("Dragged");
	scene_dock->add_root_node(dragged_root);
	p_driver.flush_frames(20);

	EditorData &editor_data = EditorNode::get_editor_data();
	const int dragged_scene_index = editor_data.get_edited_scene();
	if (dragged_scene_index < 0) {
		return _failure_with_message(p_driver, result.workflow, "The dragged scene has no editor-data index.");
	}

	// The destination tile needs content of its own: a pane with nothing in it
	// hides its chrome host and with it the overlay under test.
	p_driver.set_step("create_neighboring_tile");
	const int resident_scene_index = editor_node->new_scene();
	if (resident_scene_index < 0) {
		return _failure_with_message(p_driver, result.workflow, "Creating the neighboring tile's scene failed.");
	}
	scene_dock = editor_node->get_focused_scene_tree_dock();
	if (scene_dock == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Focused scene tree dock is unavailable for the neighboring scene.");
	}
	Node2D *resident_root = memnew(Node2D);
	resident_root->set_name("Resident");
	scene_dock->add_root_node(resident_root);
	p_driver.flush_frames(20);

	EditorSceneWorkspace *workspace = EditorNode::get_scene_workspace();
	if (workspace == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The scene workspace is unavailable.");
	}
	const int source_leaf_id = editor_data.get_scene_tile(dragged_scene_index);
	if (source_leaf_id != editor_data.get_scene_tile(resident_scene_index)) {
		return _failure_with_message(p_driver, result.workflow, "The two scenes did not start in the same pane.");
	}
	WorkspaceLeafNode *source_leaf = workspace->get_leaf_by_id(source_leaf_id);
	WorkspacePane *source_pane = source_leaf != nullptr ? source_leaf->get_workspace_pane() : nullptr;
	if (source_pane == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The source pane could not be resolved.");
	}
	const int resident_tab_index = source_pane->find_scene_tab_index(resident_scene_index);
	if (resident_tab_index < 0) {
		return _failure_with_message(p_driver, result.workflow, "The neighboring scene has no tab in the source pane.");
	}
	// Split the pane to the right so the drag has a sibling tile to travel into.
	editor_node->handle_tile_tab_drop(source_leaf_id, EditorSceneWorkspace::DROP_RIGHT, source_leaf_id, resident_tab_index);
	p_driver.flush_frames(30);
	if (!p_driver.wait_workspace_settled(10000)) {
		return _failure_from_driver(p_driver, result.workflow, "Workspace did not settle after splitting the pane.");
	}

	const int destination_leaf_id = editor_data.get_scene_tile(resident_scene_index);
	if (destination_leaf_id == source_leaf_id) {
		return _failure_with_message(p_driver, result.workflow, "Splitting the pane did not produce a second tile.");
	}

	p_driver.set_step("drag_into_neighboring_tile_in_one_motion");
	// Everything is re-resolved after the relayout rather than reusing pointers
	// captured before it.
	workspace = EditorNode::get_scene_workspace();
	if (workspace == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The scene workspace did not survive the split.");
	}
	source_leaf = workspace->get_leaf_by_id(source_leaf_id);
	WorkspaceLeafNode *destination_leaf = workspace->get_leaf_by_id(destination_leaf_id);
	source_pane = source_leaf != nullptr ? source_leaf->get_workspace_pane() : nullptr;
	WorkspacePane *destination_pane = destination_leaf != nullptr ? destination_leaf->get_workspace_pane() : nullptr;
	if (source_pane == nullptr || destination_pane == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The two tiles did not survive the split.");
	}
	EditorTileDropOverlay *destination_overlay = destination_pane->get_drop_overlay();
	if (destination_overlay == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The destination tile has no drop overlay.");
	}
	const int destination_tab_count_before = destination_pane->get_tab_count();
	TabBar *source_tab_strip = source_pane->get_tab_strip();
	const int source_tab_index = source_pane->find_scene_tab_index(dragged_scene_index);
	if (source_tab_strip == nullptr || source_tab_index < 0) {
		return _failure_with_message(p_driver, result.workflow, "The dragged scene has no tab in the source pane's strip.");
	}
	const Size2 destination_size = destination_leaf->get_size();
	if (destination_size.x < 4 || destination_size.y < 4) {
		return _failure_with_message(p_driver, result.workflow, "The destination tile has no usable on-screen area.");
	}

	const Vector2 grab_point = source_tab_strip->get_global_transform().xform(source_tab_strip->get_tab_rect(source_tab_index).get_center());
	const Vector2 drop_point = destination_leaf->get_global_transform().xform(destination_size * 0.5f);
	// Aimed well past the center inset (30% of the tile) toward the right edge, so a
	// correct region computation resolves to DROP_RIGHT rather than the DROP_CENTER
	// value hovered_region already defaults to before it is ever computed.
	const Point2 aim_local_point(destination_size.x * 0.9f, destination_size.y * 0.5f);
	const Vector2 aim_point = destination_leaf->get_global_transform().xform(aim_local_point);

	Viewport *viewport = editor_node->get_viewport();
	if (viewport == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "No viewport is available for pointer input.");
	}

	PackedStringArray pointer_events;
	const EditorAutomationInputModifiers modifiers;
	if (!EditorAutomationInput::begin_mouse_gesture(viewport, grab_point, MouseButton::LEFT, modifiers, pointer_events)) {
		return _failure_with_message(p_driver, result.workflow, "Could not press the pointer on the source tab.");
	}
	// One uninterrupted gesture: no frames are flushed and no second motion is
	// issued between arriving inside the destination tile and inspecting the
	// affordance. Anything that arms only on a later tick or a later motion fails
	// here, which is the defect under test. The motion lands off-center so the
	// resulting region is only correct if region computation actually ran.
	if (!EditorAutomationInput::move_mouse_gesture(viewport, aim_point, Vector<Vector2>(), modifiers, pointer_events)) {
		return _failure_with_message(p_driver, result.workflow, "Could not drag the pointer into the neighboring tile.");
	}
	if (!viewport->gui_is_dragging()) {
		return _failure_with_message(p_driver, result.workflow, "Dragging the tab did not start a workspace tab drag.");
	}

	Dictionary drag_diagnostics;
	const bool overlay_hit_testable = destination_overlay->get_mouse_filter() == Control::MOUSE_FILTER_STOP;
	const bool overlay_painting = destination_overlay->is_drag_active();
	const EditorSceneWorkspace::TileDropRegion aimed_region = destination_overlay->get_hovered_region();
	drag_diagnostics["overlay_hit_testable"] = overlay_hit_testable;
	drag_diagnostics["overlay_painting"] = overlay_painting;
	drag_diagnostics["overlay_aimed_region"] = int(aimed_region);
	drag_diagnostics["overlay_visible"] = destination_overlay->is_visible_in_tree();
	drag_diagnostics["overlay_rect"] = destination_overlay->get_global_rect();
	drag_diagnostics["aim_point"] = aim_point;
	drag_diagnostics["drop_point"] = drop_point;
	drag_diagnostics["pointer_events"] = pointer_events;
	if (!overlay_hit_testable || !overlay_painting || aimed_region != EditorSceneWorkspace::DROP_RIGHT) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "The drop overlay did not arm and compute the aimed region on the motion that carried the drag into the tile.";
		Dictionary details = p_driver.make_failure_details("drag_into_neighboring_tile_in_one_motion");
		details["drag_diagnostics"] = drag_diagnostics;
		details["editor_log"] = p_driver.read_editor_log();
		fail.details = details;
		EditorAutomationInput::end_mouse_gesture(viewport, aim_point, modifiers, pointer_events);
		return fail;
	}

	p_driver.set_step("recenter_before_release");
	// Recenters onto the pane's middle so the release below performs a plain tab
	// move rather than the edge split DROP_RIGHT would otherwise trigger. This
	// motion happens after arming and region computation are already proven above,
	// so it does not reintroduce the "arms on a later motion" defect being guarded
	// against.
	if (!EditorAutomationInput::move_mouse_gesture(viewport, drop_point, Vector<Vector2>(), modifiers, pointer_events)) {
		return _failure_with_message(p_driver, result.workflow, "Could not move the pointer to the center of the neighboring tile.");
	}
	if (destination_overlay->get_hovered_region() != EditorSceneWorkspace::DROP_CENTER) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "The drop overlay did not recompute its region after the pointer moved to the tile's center.";
		Dictionary details = p_driver.make_failure_details("recenter_before_release");
		details["drag_diagnostics"] = drag_diagnostics;
		details["recentered_region"] = int(destination_overlay->get_hovered_region());
		details["editor_log"] = p_driver.read_editor_log();
		fail.details = details;
		EditorAutomationInput::end_mouse_gesture(viewport, drop_point, modifiers, pointer_events);
		return fail;
	}

	p_driver.set_step("release_on_neighboring_tile");
	if (!EditorAutomationInput::end_mouse_gesture(viewport, drop_point, modifiers, pointer_events)) {
		return _failure_with_message(p_driver, result.workflow, "Could not release the pointer over the neighboring tile.");
	}
	p_driver.flush_frames(30);
	if (!p_driver.wait_workspace_settled(10000)) {
		return _failure_from_driver(p_driver, result.workflow, "Workspace did not settle after the drop.");
	}

	p_driver.set_step("verify_tab_moved");
	WorkspaceLeafNode *landed_leaf = workspace->get_leaf_by_id(destination_leaf_id);
	WorkspacePane *landed_pane = landed_leaf != nullptr ? landed_leaf->get_workspace_pane() : nullptr;
	const int landed_tile_id = editor_data.get_scene_tile(dragged_scene_index);
	const int destination_tab_count_after = landed_pane != nullptr ? landed_pane->get_tab_count() : -1;
	if (landed_tile_id != destination_leaf_id || destination_tab_count_after != destination_tab_count_before + 1) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "The armed drop did not move the dragged tab into the neighboring tile.";
		Dictionary details = p_driver.make_failure_details("verify_tab_moved");
		details["source_leaf_id"] = source_leaf_id;
		details["destination_leaf_id"] = destination_leaf_id;
		details["landed_tile_id"] = landed_tile_id;
		details["destination_tab_count_before"] = destination_tab_count_before;
		details["destination_tab_count_after"] = destination_tab_count_after;
		details["drag_diagnostics"] = drag_diagnostics;
		details["editor_log"] = p_driver.read_editor_log();
		fail.details = details;
		return fail;
	}

	if (!p_driver.assert_no_new_errors_since_step()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "A single continuous drag armed the destination tile's drop overlay before the release.";
	Dictionary details;
	details["source_leaf_id"] = source_leaf_id;
	details["destination_leaf_id"] = destination_leaf_id;
	details["drag_diagnostics"] = drag_diagnostics;
	result.details = details;
	return result;
#else
	return _failure_with_message(p_driver, result.workflow, "Continuous drag overlay workflow requires an editor (TOOLS_ENABLED) build.");
#endif
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_mixed_workspace_editing(EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "mixed_workspace_editing";

	p_driver.begin_workflow();

	MixedWorkspaceContext context;
	Result failure;
	if (!_load_mixed_workspace_scene(p_driver, result.workflow, context, failure)) {
		return failure;
	}
	if (!_add_script_tab_to_scene_pane(p_driver, result.workflow, context, failure)) {
		return failure;
	}

	WorkspaceLeafNode *script_leaf = _split_script_tab_to_right(p_driver, result.workflow, context, failure);
	if (script_leaf == nullptr) {
		return failure;
	}
	if (!_assert_command_routing(p_driver, result.workflow, context, script_leaf, failure)) {
		return failure;
	}
	if (!_move_script_tab_back_to_scene_pane(p_driver, result.workflow, context, script_leaf, failure)) {
		return failure;
	}
	if (!_close_dirty_script_tab(p_driver, result.workflow, context, failure)) {
		return failure;
	}

	p_driver.set_step("assert_no_new_errors");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "Mixed workspace editing automation workflow completed.";
	Dictionary details;
	details["workspace"] = p_driver.read_editor_state().get("workspace", Dictionary());
	result.details = details;
	return result;
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_mixed_workspace_seed(EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "mixed_workspace_seed";

	p_driver.begin_workflow();

	MixedWorkspaceContext context;
	Result failure;
	if (!_load_mixed_workspace_scene(p_driver, result.workflow, context, failure)) {
		return failure;
	}
	if (!_add_script_tab_to_scene_pane(p_driver, result.workflow, context, failure)) {
		return failure;
	}

	WorkspaceLeafNode *script_leaf = _split_script_tab_to_right(p_driver, result.workflow, context, failure);
	if (script_leaf == nullptr) {
		return failure;
	}
	if (!_assert_command_routing(p_driver, result.workflow, context, script_leaf, failure)) {
		return failure;
	}
	if (!_save_mixed_workspace_layout(p_driver, result.workflow, context, failure)) {
		return failure;
	}

	p_driver.set_step("assert_no_new_errors");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "Mixed workspace seed layout saved.";
	Dictionary details;
	details["workspace"] = p_driver.read_editor_state().get("workspace", Dictionary());
	result.details = details;
	return result;
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_mixed_workspace_restore(EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "mixed_workspace_restore";

	p_driver.begin_workflow();
	p_driver.set_step("verify_restored_workspace");
	p_driver.flush_frames(60);

	EditorSceneWorkspace *workspace = EditorNode::get_scene_workspace();
	if (workspace == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Scene workspace is unavailable.");
	}
	if (workspace->get_leaf_count() != 2) {
		return _failure_with_message(p_driver, result.workflow, vformat("Expected restored workspace to have 2 panes, found %d.", workspace->get_leaf_count()));
	}

	WorkspaceLeafNode *scene_leaf = nullptr;
	WorkspaceLeafNode *script_leaf = nullptr;
	WorkspacePane *scene_pane = nullptr;
	WorkspacePane *script_pane = nullptr;
	for (WorkspaceLeafNode *leaf : workspace->get_leaves()) {
		WorkspacePane *pane = leaf->get_workspace_pane();
		if (pane == nullptr) {
			continue;
		}
		if (_pane_has_tab(pane, StringName("scene"), MIXED_WORKSPACE_SCENE)) {
			scene_leaf = leaf;
			scene_pane = pane;
		}
		if (_pane_has_tab(pane, StringName("script"), MIXED_WORKSPACE_SCRIPT)) {
			script_leaf = leaf;
			script_pane = pane;
		}
	}

	if (scene_leaf == nullptr || scene_pane == nullptr || script_leaf == nullptr || script_pane == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Restored workspace did not contain the expected scene and script tabs.");
	}
	if (scene_pane->get_tab_count() != 1 || script_pane->get_tab_count() != 1) {
		return _failure_with_message(p_driver, result.workflow, "Restored workspace panes did not preserve expected tab counts.");
	}
	if (scene_pane->get_active_tab_index() != 0 || script_pane->get_active_tab_index() != 0) {
		return _failure_with_message(p_driver, result.workflow, "Restored workspace did not preserve active tabs.");
	}
	if (workspace->get_focused_leaf_id() != scene_leaf->get_leaf_id()) {
		return _failure_with_message(p_driver, result.workflow, "Restored workspace did not preserve the scene pane as focused.");
	}

	const Dictionary state = p_driver.read_editor_state();
	const Dictionary workspace_state = state.get("workspace", Dictionary());
	if (!(bool)workspace_state.get("supported", false) || int(workspace_state.get("tile_count", 0)) != 1) {
		return _failure_with_message(p_driver, result.workflow, "Restored workspace state readback did not report the expected scene tile.");
	}
	const Dictionary tree = workspace_state.get("tree", Dictionary());
	if (String(tree.get("type", String())) != "split") {
		return _failure_with_message(p_driver, result.workflow, "Restored workspace state did not preserve a split tree.");
	}

	if (!_assert_visible_workspace_tab(p_driver, "scene", MIXED_WORKSPACE_SCENE, scene_leaf->get_leaf_id(), "find_restored_scene_tab") ||
			!_assert_visible_workspace_tab(p_driver, "script", MIXED_WORKSPACE_SCRIPT, script_leaf->get_leaf_id(), "find_restored_script_tab")) {
		return _failure_from_driver(p_driver, result.workflow, "Restored tabs were not visible through semantic tab selectors.");
	}

	MixedWorkspaceContext context;
	context.editor_node = EditorNode::get_singleton();
	context.workspace = workspace;
	context.scene_leaf = scene_leaf;
	context.scene_pane = scene_pane;
	context.scene_leaf_id = scene_leaf->get_leaf_id();
	Result failure;
	if (!_assert_command_routing(p_driver, result.workflow, context, script_leaf, failure)) {
		return failure;
	}

	p_driver.set_step("assert_no_new_errors");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "Mixed workspace layout restored.";
	Dictionary details;
	details["workspace"] = workspace_state;
	result.details = details;
	return result;
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_board_switch_3d_scene(EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "board_switch_3d_scene";

	p_driver.begin_workflow();
	p_driver.set_step("open_3d_scene");

	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node == nullptr || !editor_node->is_editor_ready()) {
		return _failure_with_message(p_driver, result.workflow, "EditorNode is not ready.");
	}
	if (editor_node->load_scene(BOARD_SWITCH_3D_SCENE) != OK) {
		return _failure_with_message(p_driver, result.workflow, vformat("Failed to load scene '%s'.", BOARD_SWITCH_3D_SCENE));
	}
	p_driver.flush_frames(30);

	EditorBoardStrip *strip = EditorNode::get_board_strip();
	if (strip == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Board strip is unavailable.");
	}
	if (strip->get_board_count() != 1 || strip->get_active_index() != 0) {
		return _failure_with_message(p_driver, result.workflow,
				vformat("Expected a single active board at startup, found %d board(s) with active index %d.",
						strip->get_board_count(), strip->get_active_index()));
	}

	// Resolve the tile holding the 3D scene by instance id. The board switch below spans
	// many frames and rebuilds display attachments, so a raw pointer captured now must not
	// be trusted afterwards.
	ScenePaneTile *initial_tile = _resolve_focused_scene_tile(strip->get_board(0));
	if (initial_tile == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Could not resolve the scene tile holding the 3D scene.");
	}
	const ObjectID initial_tile_id = initial_tile->get_instance_id();
	if (initial_tile->get_spatial_view() != nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The focused tile already owns a secondary 3D viewport before any board switch.");
	}

	p_driver.set_step("add_second_board");
	if (strip->add_board() == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Failed to add a second board.");
	}
	p_driver.flush_frames(20);
	if (strip->get_board_count() != 2) {
		return _failure_with_message(p_driver, result.workflow,
				vformat("Expected 2 boards after adding one, found %d.", strip->get_board_count()));
	}

	p_driver.set_step("switch_to_second_board");
	strip->set_active_board(1);
	p_driver.flush_frames(90);

	if (strip->get_active_index() != 1) {
		return _failure_with_message(p_driver, result.workflow,
				vformat("Expected board 1 to be active after the switch, found %d.", strip->get_active_index()));
	}

	// Focusing a tile with no scene stops short of refreshing display attachments, so the
	// second board has to actually get a scene before the 3D tile left behind is demoted
	// to a preview. That demotion asks for a secondary 3D viewport, and building one is
	// the operation that used to abort the editor.
	p_driver.set_step("open_scene_on_second_board");
	if (editor_node->load_scene(BOARD_SWITCH_2D_SCENE) != OK) {
		return _failure_with_message(p_driver, result.workflow, vformat("Failed to load scene '%s'.", BOARD_SWITCH_2D_SCENE));
	}
	p_driver.flush_frames(60);

	ScenePaneTile *demoted_tile = ObjectDB::get_instance<ScenePaneTile>(initial_tile_id);
	if (demoted_tile == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The tile holding the 3D scene did not survive the board switch.");
	}
	if (demoted_tile->get_spatial_view() == nullptr) {
		return _failure_with_message(p_driver, result.workflow,
				"Demoting the 3D tile did not build a secondary 3D viewport, so this run never exercised the crash path.");
	}

	// A demoted 3D tile hosts a passive preview, not an editor. Assert the policy that
	// makes its chrome-less state safe rather than probing individual overlay members:
	// nothing is wired that could reach them.
	p_driver.set_step("assert_secondary_viewport_is_passive");
	{
		Node3DEditorViewport *spatial_view = demoted_tile->get_spatial_view();
		if (!spatial_view->is_secondary_view()) {
			return _failure_with_message(p_driver, result.workflow, "The demoted tile's viewport does not report the secondary (passive) role.");
		}
		Control *surface = spatial_view->get_surface();
		if (surface == nullptr) {
			return _failure_with_message(p_driver, result.workflow, "The secondary viewport has no surface.");
		}
		if (surface->get_focus_mode() != Control::FOCUS_NONE) {
			return _failure_with_message(p_driver, result.workflow, "A passive 3D preview's surface can take keyboard focus.");
		}
		if (surface->get_mouse_filter() != Control::MOUSE_FILTER_IGNORE) {
			return _failure_with_message(p_driver, result.workflow, "A passive 3D preview's surface does not pass pointer input through to its tile.");
		}
		if (surface->has_connections(SceneStringName(gui_input))) {
			return _failure_with_message(p_driver, result.workflow, "A passive 3D preview's surface is connected to an editor input handler.");
		}
		Dictionary drag_data;
		drag_data["type"] = "files";
		drag_data["files"] = PackedStringArray();
		if (spatial_view->can_drop_data_fw(Point2(10, 10), drag_data, nullptr)) {
			return _failure_with_message(p_driver, result.workflow, "A passive 3D preview accepted drag-and-drop data.");
		}
		if (Node3DEditor::get_singleton() != nullptr && Node3DEditor::get_singleton()->get_focused_viewport() == spatial_view) {
			return _failure_with_message(p_driver, result.workflow, "A passive 3D preview is reported as the focused 3D viewport.");
		}
	}

	p_driver.set_step("assert_no_new_errors_after_secondary_input");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	// Switching back and forth re-runs the demotion against an existing secondary
	// viewport, which is a different code path from building the first one.
	p_driver.set_step("switch_back_to_first_board");
	strip->set_active_board(0);
	p_driver.flush_frames(90);
	if (strip->get_active_index() != 0) {
		return _failure_with_message(p_driver, result.workflow,
				vformat("Expected board 0 to be active after switching back, found %d.", strip->get_active_index()));
	}

	p_driver.set_step("switch_to_second_board_again");
	strip->set_active_board(1);
	p_driver.flush_frames(90);
	if (strip->get_active_index() != 1) {
		return _failure_with_message(p_driver, result.workflow,
				vformat("Expected board 1 to be active after the second switch, found %d.", strip->get_active_index()));
	}

	ScenePaneTile *surviving_tile = ObjectDB::get_instance<ScenePaneTile>(initial_tile_id);
	if (surviving_tile == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The tile holding the 3D scene did not survive repeated board switches.");
	}
	if (surviving_tile->get_spatial_view() == nullptr) {
		return _failure_with_message(p_driver, result.workflow,
				"The 3D tile lost its secondary 3D viewport after switching boards a second time.");
	}

	p_driver.set_step("assert_no_new_errors");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "Board switching with a 3D scene open completed without crashing.";
	Dictionary details;
	details["board_count"] = strip->get_board_count();
	details["workspace"] = p_driver.read_editor_state().get("workspace", Dictionary());
	result.details = details;
	return result;
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_passive_preview_input_policy(EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "passive_preview_input_policy";

	p_driver.begin_workflow();

	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node == nullptr || !editor_node->is_editor_ready()) {
		return _failure_with_message(p_driver, result.workflow, "EditorNode is not ready.");
	}

	p_driver.set_step("open_3d_scene");
	if (editor_node->load_scene(BOARD_SWITCH_3D_SCENE) != OK) {
		return _failure_with_message(p_driver, result.workflow, vformat("Failed to load scene '%s'.", BOARD_SWITCH_3D_SCENE));
	}
	p_driver.flush_frames(30);

	// The fixture scene root has no children of its own, so first_child_transform would
	// never have anything to compare. Add the probe child at runtime instead of editing
	// the shared fixture -- board_switch_3d_scene also loads secondary.tscn, so mutating
	// it there would perturb that workflow too.
	Node *preview_probe_root = editor_node->get_edited_scene();
	if (preview_probe_root == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The 3D scene has no edited root after loading.");
	}
	Node3D *preview_probe_child = memnew(Node3D);
	preview_probe_child->set_name("PassivePreviewProbeChild");
	preview_probe_child->set_position(Vector3(1, 2, 3));
	preview_probe_root->add_child(preview_probe_child);
	preview_probe_child->set_owner(preview_probe_root);

	// A tile is demoted to a preview by editor-wide focus, not by board activity, so a
	// second board would only re-focus its lone tile and hide the preview. Splitting the
	// same active board into two tiles keeps the demoted preview on screen and clickable.
	p_driver.set_step("create_second_scene");
	const int second_scene = editor_node->new_scene();
	if (second_scene < 0) {
		return _failure_with_message(p_driver, result.workflow, "Failed to create a second scene.");
	}
	SceneTreeDock *scene_dock = editor_node->get_focused_scene_tree_dock();
	if (scene_dock == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Focused scene tree dock is unavailable after creating the second scene.");
	}
	Node2D *second_root = memnew(Node2D);
	second_root->set_name("PassiveSibling");
	scene_dock->add_root_node(second_root);
	p_driver.flush_frames(20);

	p_driver.set_step("constrain_editor_window");
	Window *root_window = SceneTree::get_singleton() ? SceneTree::get_singleton()->get_root() : nullptr;
	if (root_window == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Root window is unavailable for deterministic sizing.");
	}
	// Narrow enough that a horizontal demoted tile with both dock columns and both
	// rails visible collapses its preview surface; wide enough for a preview-only
	// demoted tile to keep a usable surface for pointer probes. Restore on every
	// exit so interactive MCP runs do not persist the temporary size.
	const Size2i constrained_size(1600, 900);
	const Size2i original_window_size = root_window->get_size();
	struct RestoreWindowSize {
		Window *window = nullptr;
		Size2i size;
		~RestoreWindowSize() {
			if (window) {
				window->set_size(size);
			}
		}
	} restore_window_size{ root_window, original_window_size };
	root_window->set_size(constrained_size);
	p_driver.flush_frames(10);
	if (Math::abs(root_window->get_size().x - constrained_size.x) > 64 ||
			Math::abs(root_window->get_size().y - constrained_size.y) > 64) {
		return _failure_with_message(p_driver, result.workflow,
				vformat("Failed to constrain the editor window (got %s, wanted %s).",
						String(root_window->get_size()), String(constrained_size)));
	}

	p_driver.set_step("configure_source_tile_chrome");
	EditorSceneWorkspace *workspace = EditorNode::get_scene_workspace();
	if (workspace == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Scene workspace is unavailable.");
	}
	ScenePaneTile *source_tile = workspace->get_tile_by_id(0);
	if (source_tile == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Could not resolve the source tile before the split.");
	}
	EditorTileDockRegion *source_region = source_tile->get_dock_region();
	source_region->set_side_mode(EditorTileDockRegion::Side::LEFT, SideRailMode::DOCKED);
	source_region->set_side_mode(EditorTileDockRegion::Side::RIGHT, SideRailMode::RAILED);
	source_region->press_rail_toggle(source_tile->get_signals_dock());
	p_driver.flush_frames(5);
	const TileChromeSnapshot source_chrome_before = _sample_tile_chrome(source_tile);

	p_driver.set_step("split_active_board_into_two_tiles");
	// handle_tile_scene_drop's last argument is a tab index local to the source tile,
	// not the global scene index that new_scene() returned.
	const int source_tab = editor_node->get_editor_data().scene_index_to_tile_tab(second_scene);
	if (source_tab < 0) {
		return _failure_with_message(p_driver, result.workflow, "Could not map the second scene to a tab on the source tile.");
	}
	// Split horizontally: demoted tiles hide their dock columns and rails so the
	// preview keeps usable width even when the combined chrome minima would
	// otherwise consume the tile.
	editor_node->handle_tile_scene_drop(0, (int)EditorSceneWorkspace::DROP_RIGHT, 0, source_tab);
	p_driver.flush_frames(30);
	if (!p_driver.wait_workspace_settled(5000)) {
		return _failure_from_driver(p_driver, result.workflow, "Workspace did not settle after splitting the board.");
	}

	ScenePaneTile *preview_tile = workspace->get_tile_by_id(0);
	if (preview_tile == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Could not resolve the demoted tile after the split.");
	}
	const ObjectID preview_tile_id = preview_tile->get_instance_id();
	if (editor_node->get_editor_data().get_focused_tile_id() == 0) {
		return _failure_with_message(p_driver, result.workflow, "Splitting the board left the 3D tile focused, so it never became a passive preview.");
	}
	Node3DEditorViewport *spatial_view = preview_tile->get_spatial_view();
	if (spatial_view == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The demoted 3D tile did not build a passive preview viewport.");
	}
	// Held across flush_frames(), so re-resolved from an ObjectID at each use following a
	// frame advance rather than trusted to still be alive.
	const ObjectID spatial_view_id = spatial_view->get_instance_id();
	auto resolve_spatial_view = [&]() -> Node3DEditorViewport * {
		return ObjectDB::get_instance<Node3DEditorViewport>(spatial_view_id);
	};
	Control *surface = spatial_view->get_surface();
	if (surface == nullptr || !surface->is_visible_in_tree()) {
		return _failure_with_message(p_driver, result.workflow,
				vformat("The passive 3D preview's surface is not on screen, so no real input could reach it. %s",
						_describe_tile_preview_geometry(preview_tile, surface)));
	}
	const ObjectID surface_id = surface->get_instance_id();
	auto resolve_surface = [&]() -> Control * {
		return ObjectDB::get_instance<Control>(surface_id);
	};
	p_driver.flush_frames(30);
	spatial_view = resolve_spatial_view();
	surface = resolve_surface();
	preview_tile = ObjectDB::get_instance<ScenePaneTile>(preview_tile_id);
	if (spatial_view == nullptr || surface == nullptr || preview_tile == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The passive 3D preview viewport or surface did not survive a frame advance.");
	}
	{
		String chrome_message;
		if (!_assert_preview_only_chrome(preview_tile, surface, chrome_message)) {
			return _failure_with_message(p_driver, result.workflow, chrome_message);
		}
	}
	if (preview_tile->get_dock_region()->get_side_mode(EditorTileDockRegion::Side::LEFT) != source_chrome_before.left_mode ||
			preview_tile->get_dock_region()->get_side_mode(EditorTileDockRegion::Side::RIGHT) != source_chrome_before.right_mode) {
		return _failure_with_message(p_driver, result.workflow,
				vformat("Demotion mutated stored side modes. %s", _describe_tile_preview_geometry(preview_tile, surface)));
	}
	p_driver.set_step("capture_demoted_chrome_budget_pair");
	_capture_demoted_chrome_budget_pair(p_driver, preview_tile);
	preview_tile = ObjectDB::get_instance<ScenePaneTile>(preview_tile_id);
	spatial_view = resolve_spatial_view();
	surface = resolve_surface();
	if (preview_tile == nullptr || spatial_view == nullptr || surface == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The demoted 3D preview did not survive the optional capture pair.");
	}
	{
		String chrome_message;
		if (!_assert_preview_only_chrome(preview_tile, surface, chrome_message)) {
			return _failure_with_message(p_driver, result.workflow,
					vformat("Optional capture pair left demoted chrome unrestored. %s", chrome_message));
		}
	}

	p_driver.set_step("configure_focused_2d_tile_chrome");
	ScenePaneTile *focused_2d_tile = workspace->get_tile_by_id(1);
	if (focused_2d_tile == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Could not resolve the focused 2D tile after the split.");
	}
	EditorTileDockRegion *focused_2d_region = focused_2d_tile->get_dock_region();
	// close_drawer collapses to RAILED with a closed drawer. set_side_mode(RAILED)
	// would reopen the last/shown dock, which is not the asymmetric chrome we want.
	focused_2d_region->close_drawer(EditorTileDockRegion::Side::LEFT);
	focused_2d_region->set_side_mode(EditorTileDockRegion::Side::RIGHT, SideRailMode::DOCKED);
	if (TabContainer *right_tabs = focused_2d_region->get_right_tabs()) {
		const int groups_tab = right_tabs->get_tab_idx_from_control(focused_2d_tile->get_groups_dock());
		if (groups_tab >= 0) {
			right_tabs->set_current_tab(groups_tab);
		}
	}
	p_driver.flush_frames(5);
	const TileChromeSnapshot canvas_chrome_before = _sample_tile_chrome(focused_2d_tile);

	p_driver.set_step("assert_passive_3d_policy");
	if (!spatial_view->is_secondary_view()) {
		return _failure_with_message(p_driver, result.workflow, "The preview viewport does not report the passive (secondary) role.");
	}
	if (surface->get_focus_mode() != Control::FOCUS_NONE) {
		return _failure_with_message(p_driver, result.workflow, "A passive 3D preview's surface can take keyboard focus.");
	}
	if (surface->get_mouse_filter() != Control::MOUSE_FILTER_IGNORE) {
		return _failure_with_message(p_driver, result.workflow, "A passive 3D preview's surface does not pass pointer input through to its tile.");
	}
	if (surface->has_connections(SceneStringName(gui_input))) {
		return _failure_with_message(p_driver, result.workflow, "A passive 3D preview's surface is connected to an editor input handler.");
	}
	if (spatial_view->is_physics_processing()) {
		return _failure_with_message(p_driver, result.workflow, "A passive 3D preview runs editing physics work.");
	}
	if (Node3DEditor::get_singleton() != nullptr && Node3DEditor::get_singleton()->get_focused_viewport() == spatial_view) {
		return _failure_with_message(p_driver, result.workflow, "A passive 3D preview is reported as the focused 3D viewport.");
	}
	{
		Dictionary drag_data;
		drag_data["type"] = "files";
		drag_data["files"] = PackedStringArray();
		if (spatial_view->can_drop_data_fw(Point2(10, 10), drag_data, nullptr)) {
			return _failure_with_message(p_driver, result.workflow, "A passive 3D preview accepted drag-and-drop data.");
		}
	}

	Node *preview_scene_root = preview_tile->get_current_scene_root();
	const PassivePreviewSnapshot before_3d = _sample_passive_preview(spatial_view, nullptr, preview_scene_root);

	// Non-promoting probes: wheel, keyboard, and a right-button drag. None of these
	// promote a tile, so every recorded value must survive them untouched.
	p_driver.set_step("drive_non_promoting_input_at_passive_3d_preview");
	{
		RealPointerDispatch dispatch;
		if (!dispatch.bind(surface)) {
			return _failure_with_message(p_driver, result.workflow, "Could not bind real pointer dispatch to the passive 3D preview's surface.");
		}
		PackedStringArray events;
		dispatch.hover(events);
		dispatch.button(MouseButton::WHEEL_UP, true, MouseButtonMask::NONE, events);
		dispatch.button(MouseButton::WHEEL_UP, false, MouseButtonMask::NONE, events);
		dispatch.button(MouseButton::RIGHT, true, MouseButtonMask::RIGHT, events);
		dispatch.motion(Vector2(24, 18), MouseButtonMask::RIGHT, events);
		dispatch.button(MouseButton::RIGHT, false, MouseButtonMask::NONE, events);
		dispatch.key(Key::F, events);
		dispatch.key(Key::T, events);
		p_driver.flush_frames(10);
	}

	spatial_view = resolve_spatial_view();
	if (spatial_view == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The passive 3D preview viewport did not survive the non-promoting input probe.");
	}
	if (!(_sample_passive_preview(spatial_view, nullptr, preview_scene_root) == before_3d)) {
		return _failure_with_message(p_driver, result.workflow, "Non-promoting input changed a passive 3D preview's camera, selection, or scene content.");
	}
	if (editor_node->get_editor_data().get_focused_tile_id() == 0) {
		return _failure_with_message(p_driver, result.workflow, "A wheel/keyboard/right-drag probe promoted the passive tile.");
	}

	// The promoting gesture: primary button press, motion while held, then release.
	// The tile must gain focus while the whole gesture stays edit-free.
	p_driver.set_step("promote_tile_with_primary_gesture");
	surface = resolve_surface();
	if (surface == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The passive 3D preview's surface did not survive the non-promoting input probe.");
	}
	{
		RealPointerDispatch dispatch;
		if (!dispatch.bind(surface)) {
			return _failure_with_message(p_driver, result.workflow, "Could not bind real pointer dispatch for the promoting gesture.");
		}
		PackedStringArray events;
		dispatch.hover(events);
		p_driver.flush_frames(2);
		if (!dispatch.button(MouseButton::LEFT, true, MouseButtonMask::LEFT, events)) {
			return _failure_with_message(p_driver, result.workflow, "Failed to synthesize the promoting button press.");
		}
		dispatch.motion(Vector2(30, 20), MouseButtonMask::LEFT, events);
		if (!dispatch.button(MouseButton::LEFT, false, MouseButtonMask::NONE, events)) {
			return _failure_with_message(p_driver, result.workflow, "Failed to synthesize the promoting button release.");
		}
		if (!events.has("mouse_pressed") || !events.has("mouse_released")) {
			return _failure_with_message(p_driver, result.workflow, "The promoting gesture did not report both press and release events.");
		}
		p_driver.flush_frames(30);
	}
	if (!p_driver.wait_workspace_settled(5000)) {
		return _failure_from_driver(p_driver, result.workflow, "Workspace did not settle after the promoting gesture.");
	}
	if (editor_node->get_editor_data().get_focused_tile_id() != 0) {
		return _failure_with_message(p_driver, result.workflow,
				vformat("Clicking a passive 3D preview did not focus its owning tile (focused_tile_id=%d).",
						editor_node->get_editor_data().get_focused_tile_id()));
	}

	ScenePaneTile *promoted_tile = ObjectDB::get_instance<ScenePaneTile>(preview_tile_id);
	if (promoted_tile == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The promoted tile did not survive promotion.");
	}
	Node *promoted_scene_root = promoted_tile->get_current_scene_root();
	if (promoted_scene_root == nullptr || promoted_scene_root->get_child_count() != before_3d.scene_child_count) {
		return _failure_with_message(p_driver, result.workflow, "The promoting gesture changed the scene it was promoting.");
	}
	{
		EditorSelection *selection = editor_node->get_editor_selection();
		if (selection != nullptr && (int)selection->get_selection().size() != before_3d.selected_count) {
			return _failure_with_message(p_driver, result.workflow, "The promoting gesture selected content instead of only promoting the tile.");
		}
	}
	{
		String chrome_message;
		// Column visibility in the pre-split snapshot still describes the stored
		// modes/drawer (left docked, right railed-open). Promotion must restore that.
		TileChromeSnapshot expected = source_chrome_before;
		expected.left_rail_visible = true;
		expected.right_rail_visible = true;
		expected.left_column_visible = true;
		expected.right_column_visible = true;
		if (!_assert_chrome_restored(promoted_tile, expected, chrome_message)) {
			return _failure_with_message(p_driver, result.workflow, chrome_message);
		}
	}
	p_driver.set_step("capture_promoted_chrome_restored");
	_maybe_capture_editor_png(p_driver, "2067_after_promoted_chrome_restored.png");

	// EditorAutomationState::read_editor_state() calls get_focused_viewport()->get_state(),
	// which dereferences the View menu and its display submenu. Routing a passive preview
	// there would abort here; a populated state Dictionary is the observable proof that a
	// click on a preview never made it the focused viewport.
	p_driver.set_step("read_focused_viewport_state_after_promotion");
	{
		Node3DEditor *spatial_editor = Node3DEditor::get_singleton();
		if (spatial_editor != nullptr) {
			Node3DEditorViewport *focused = spatial_editor->get_focused_viewport();
			if (focused == nullptr) {
				return _failure_with_message(p_driver, result.workflow, "No focused 3D viewport after promotion.");
			}
			if (focused->is_secondary_view()) {
				return _failure_with_message(p_driver, result.workflow, "A passive preview became the focused 3D viewport.");
			}
			if (focused->get_state().is_empty()) {
				return _failure_with_message(p_driver, result.workflow, "The focused 3D viewport reported no view state.");
			}
		}
		const Dictionary editor_state = p_driver.read_editor_state();
		if (!(bool)editor_state.get("supported", false)) {
			return _failure_with_message(p_driver, result.workflow, "Reading editor state after promotion failed.");
		}
	}

	// Promotion demoted the previously focused sibling, which is 2D. Its live preview is
	// the passive CanvasItemEditorView, so the same policy is asserted for 2D here.
	p_driver.set_step("assert_passive_2d_policy");
	ScenePaneTile *canvas_tile = workspace->get_tile_by_id(1);
	if (canvas_tile == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Could not resolve the demoted 2D tile after promotion.");
	}
	// Held across flush_frames(), so re-resolved from an ObjectID at each use following a
	// frame advance rather than trusted to still be alive.
	const ObjectID canvas_tile_id = canvas_tile->get_instance_id();
	auto resolve_canvas_tile = [&]() -> ScenePaneTile * {
		return ObjectDB::get_instance<ScenePaneTile>(canvas_tile_id);
	};
	CanvasItemEditorView *canvas_view = canvas_tile->get_canvas_view();
	if (canvas_view == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The demoted 2D tile did not build a passive canvas preview.");
	}
	const ObjectID canvas_view_id = canvas_view->get_instance_id();
	auto resolve_canvas_view = [&]() -> CanvasItemEditorView * {
		return ObjectDB::get_instance<CanvasItemEditorView>(canvas_view_id);
	};
	if (!canvas_view->is_passive_preview()) {
		return _failure_with_message(p_driver, result.workflow, "The demoted 2D tile's view does not report the passive role.");
	}
	if (canvas_view->is_plugin_forwarding_target()) {
		return _failure_with_message(p_driver, result.workflow, "A passive 2D preview forwards input to editor plugins.");
	}
	if (canvas_view->get_zoom_widget() != nullptr) {
		return _failure_with_message(p_driver, result.workflow, "A passive 2D preview exposes the zoom control.");
	}
	Control *canvas_viewport = canvas_view->get_viewport_control();
	if (canvas_viewport == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The passive 2D preview has no viewport control.");
	}
	if (canvas_view->get_scene_viewport_container() == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "The passive 2D preview lost its scene rendering surface.");
	}
	if (canvas_viewport->get_focus_mode() != Control::FOCUS_NONE) {
		return _failure_with_message(p_driver, result.workflow, "A passive 2D preview's viewport can take keyboard focus.");
	}
	if (canvas_viewport->get_mouse_filter() != Control::MOUSE_FILTER_IGNORE) {
		return _failure_with_message(p_driver, result.workflow, "A passive 2D preview's viewport does not pass pointer input through to its tile.");
	}
	if (canvas_viewport->has_connections(SceneStringName(gui_input))) {
		return _failure_with_message(p_driver, result.workflow, "A passive 2D preview's viewport is connected to an editor input handler.");
	}
	if (!canvas_viewport->has_connections(SceneStringName(draw))) {
		return _failure_with_message(p_driver, result.workflow, "A passive 2D preview lost the draw path that renders the editor visualization.");
	}
	{
		String chrome_message;
		if (!_assert_preview_only_chrome(canvas_tile, canvas_viewport, chrome_message)) {
			return _failure_with_message(p_driver, result.workflow, chrome_message);
		}
		if (canvas_tile->get_dock_region()->get_side_mode(EditorTileDockRegion::Side::LEFT) != canvas_chrome_before.left_mode ||
				canvas_tile->get_dock_region()->get_side_mode(EditorTileDockRegion::Side::RIGHT) != canvas_chrome_before.right_mode ||
				canvas_tile->get_dock_region()->get_right_tabs()->get_current_tab() != canvas_chrome_before.right_tab_index) {
			return _failure_with_message(p_driver, result.workflow,
					vformat("2D demotion mutated stored chrome. %s", _describe_tile_preview_geometry(canvas_tile, canvas_viewport)));
		}
	}

	const PassivePreviewSnapshot before_2d = _sample_passive_preview(nullptr, canvas_view, canvas_tile->get_current_scene_root());

	p_driver.set_step("drive_non_promoting_input_at_passive_2d_preview");
	if (!canvas_viewport->is_visible_in_tree()) {
		return _failure_with_message(p_driver, result.workflow,
				vformat("The passive 2D preview viewport is not visible for input probes. %s",
						_describe_tile_preview_geometry(canvas_tile, canvas_viewport)));
	}
	{
		RealPointerDispatch dispatch;
		if (!dispatch.bind(canvas_viewport)) {
			return _failure_with_message(p_driver, result.workflow, "Could not bind real pointer dispatch to the passive 2D preview.");
		}
		PackedStringArray events;
		dispatch.hover(events);
		dispatch.button(MouseButton::WHEEL_UP, true, MouseButtonMask::NONE, events);
		dispatch.button(MouseButton::WHEEL_UP, false, MouseButtonMask::NONE, events);
		dispatch.button(MouseButton::RIGHT, true, MouseButtonMask::RIGHT, events);
		dispatch.motion(Vector2(22, 16), MouseButtonMask::RIGHT, events);
		dispatch.button(MouseButton::RIGHT, false, MouseButtonMask::NONE, events);
		p_driver.flush_frames(10);

		canvas_view = resolve_canvas_view();
		canvas_tile = resolve_canvas_tile();
		if (canvas_view == nullptr || canvas_tile == nullptr) {
			return _failure_with_message(p_driver, result.workflow, "The passive 2D preview did not survive the non-promoting input probe.");
		}
		canvas_viewport = canvas_view->get_viewport_control();
		if (canvas_viewport == nullptr) {
			return _failure_with_message(p_driver, result.workflow, "The passive 2D preview lost its viewport control after the non-promoting input probe.");
		}
		if (!(_sample_passive_preview(nullptr, canvas_view, canvas_tile->get_current_scene_root()) == before_2d)) {
			return _failure_with_message(p_driver, result.workflow, "Non-promoting input changed a passive 2D preview's canvas transform, selection, or scene content.");
		}

		p_driver.set_step("promote_2d_tile_with_primary_gesture");
		if (!dispatch.bind(canvas_viewport)) {
			return _failure_with_message(p_driver, result.workflow, "Could not re-bind real pointer dispatch for the 2D promoting gesture.");
		}
		dispatch.hover(events);
		p_driver.flush_frames(2);
		if (!dispatch.button(MouseButton::LEFT, true, MouseButtonMask::LEFT, events)) {
			return _failure_with_message(p_driver, result.workflow, "Failed to synthesize the 2D promoting button press.");
		}
		dispatch.motion(Vector2(26, 14), MouseButtonMask::LEFT, events);
		if (!dispatch.button(MouseButton::LEFT, false, MouseButtonMask::NONE, events)) {
			return _failure_with_message(p_driver, result.workflow, "Failed to synthesize the 2D promoting button release.");
		}
		p_driver.flush_frames(30);
		if (!p_driver.wait_workspace_settled(5000)) {
			return _failure_from_driver(p_driver, result.workflow, "Workspace did not settle after the 2D promoting gesture.");
		}
		if (editor_node->get_editor_data().get_focused_tile_id() != 1) {
			return _failure_with_message(p_driver, result.workflow, "Clicking a passive 2D preview did not focus its owning tile.");
		}
		canvas_view = resolve_canvas_view();
		canvas_tile = resolve_canvas_tile();
		if (canvas_view == nullptr || canvas_tile == nullptr) {
			return _failure_with_message(p_driver, result.workflow, "The passive 2D preview did not survive the promoting gesture.");
		}
		if (!Math::is_equal_approx(canvas_view->get_view_state().zoom, before_2d.canvas_zoom)) {
			return _failure_with_message(p_driver, result.workflow, "The 2D promoting gesture changed the preview's zoom.");
		}
		{
			String chrome_message;
			TileChromeSnapshot expected = canvas_chrome_before;
			expected.left_rail_visible = true;
			expected.right_rail_visible = true;
			// Left is railed closed, so the left column stays hidden after restore.
			expected.left_column_visible = false;
			expected.right_column_visible = true;
			if (!_assert_chrome_restored(canvas_tile, expected, chrome_message)) {
				return _failure_with_message(p_driver, result.workflow, chrome_message);
			}
		}
	}

	p_driver.set_step("assert_no_new_errors");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "Passive 2D and 3D previews used preview-only chrome, ignored editor input, and restored chrome on promote.";
	Dictionary details;
	details["workspace"] = p_driver.read_editor_state().get("workspace", Dictionary());
	details["window_size"] = root_window->get_size();
	result.details = details;
	return result;
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_projectless_shell_smoke(EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "projectless_shell_smoke";

	p_driver.begin_workflow();

	p_driver.set_step("wait_for_editor_ready");
	if (!p_driver.wait_editor_idle(30000)) {
		return _failure_from_driver(p_driver, result.workflow, "Editor did not become ready in projectless mode.");
	}

	p_driver.set_step("wait_for_import_idle");
	if (!p_driver.wait_import_idle(30000)) {
		return _failure_from_driver(p_driver, result.workflow, "Filesystem/import pipeline did not become idle in projectless mode.");
	}

	p_driver.set_step("verify_projectless_state");
	const Dictionary state = p_driver.read_editor_state();
	if (!(bool)state.get("supported", false)) {
		return _failure_with_message(p_driver, result.workflow, "Editor automation state was not available.");
	}
	if (!(bool)state.get("projectless_shell", false)) {
		return _failure_with_message(p_driver, result.workflow, "Editor did not boot in projectless shell mode.");
	}
	if ((bool)state.get("project_loaded", true)) {
		return _failure_with_message(p_driver, result.workflow, "Projectless shell boot loaded a project resource path.");
	}
	if (!(bool)state.get("startup_dialog_visible", false)) {
		return _failure_with_message(p_driver, result.workflow, "Startup dialog should be visible in projectless shell mode.");
	}
	// mode/workspace_exposed encode the launcher contract: the project workspace
	// (scene workspace + scene tabs, tracked by workspace_exposed) is suppressed
	// behind the startup dialog until a project loads.
	if (String(state.get("mode", String())) != "projectless_shell") {
		return _failure_with_message(p_driver, result.workflow, "Projectless shell should report mode == projectless_shell.");
	}
	if ((bool)state.get("workspace_exposed", true)) {
		return _failure_with_message(p_driver, result.workflow, "Projectless shell must not expose the project workspace behind the dialog.");
	}

	// The bottom drawer's status strip (Output/Debugger/Audio/... tabs) must stay
	// hidden behind the dialog too; find_elements returns only visible elements, so
	// the strip's Output tab must not resolve.
	p_driver.set_step("verify_bottom_drawer_hidden");
	{
		Dictionary output_selector;
		output_selector["role"] = "button";
		output_selector["name"] = "Output";
		const Dictionary output_find = p_driver.find(output_selector);
		if ((bool)output_find.get("ok", false) && ((Array)output_find.get("elements", Array())).size() > 0) {
			return _failure_with_message(p_driver, result.workflow, "Bottom drawer strip must stay hidden in projectless shell mode.");
		}
	}

	const Array open_scenes = state.get("open_scenes", Array());
	if (!open_scenes.is_empty()) {
		if (open_scenes.size() != 1) {
			return _failure_with_message(p_driver, result.workflow, "Projectless shell should start with at most one empty workspace scene slot.");
		}
		const Dictionary scene_entry = open_scenes[0];
		if (!String(scene_entry.get("path", String())).is_empty()) {
			return _failure_with_message(p_driver, result.workflow, "Projectless shell should not open a scene file at startup.");
		}
	}

	const Dictionary workspace = state.get("workspace", Dictionary());
	if (!(bool)workspace.get("supported", false) || (int)workspace.get("tile_count", 0) != 1) {
		return _failure_with_message(p_driver, result.workflow, "Projectless shell did not present a single empty workspace pane.");
	}

	p_driver.set_step("verify_run_controls_hidden");
	Dictionary run_selector;
	run_selector["role"] = "button";
	run_selector["name"] = "Run Project";
	const Dictionary run_find = p_driver.find(run_selector);
	if ((bool)run_find.get("ok", false)) {
		return _failure_with_message(p_driver, result.workflow, "Run Project controls should be unavailable in projectless shell mode.");
	}

	p_driver.set_step("assert_no_new_errors");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "Projectless editor shell smoke workflow completed.";
	result.details = state;
	return result;
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_projectless_shell_dismiss_quits(EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "projectless_shell_dismiss_quits";

	p_driver.begin_workflow();

	p_driver.set_step("wait_for_editor_ready");
	if (!p_driver.wait_editor_idle(30000)) {
		return _failure_from_driver(p_driver, result.workflow, "Editor did not become ready in projectless mode.");
	}

	p_driver.set_step("verify_projectless_state");
	Dictionary state = p_driver.read_editor_state();
	if (!(bool)state.get("projectless_shell", false)) {
		return _failure_with_message(p_driver, result.workflow, "Editor did not boot in projectless shell mode.");
	}
	if (!(bool)state.get("startup_dialog_visible", false)) {
		return _failure_with_message(p_driver, result.workflow, "Startup dialog should be visible before dismissal.");
	}
	if ((bool)state.get("workspace_exposed", true)) {
		return _failure_with_message(p_driver, result.workflow, "Projectless shell must not expose the workspace before dismissal.");
	}

#ifdef TOOLS_ENABLED
	EditorNode *editor_node = EditorNode::get_singleton();
	StartupDialog *dialog = editor_node != nullptr ? editor_node->get_startup_dialog() : nullptr;
	if (dialog == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Startup dialog instance was unavailable.");
	}

	p_driver.set_step("dismiss_startup_dialog");
	// AcceptDialog funnels every dismissal path -- Escape (ui_close_dialog), the
	// window close affordance (NOTIFICATION_WM_CLOSE_REQUEST), and the cancel
	// button -- through the `canceled` signal, which is where the projectless
	// launcher hooks its quit. Emitting that signal exercises the exact wiring a
	// user dismissal reaches. The handler quits synchronously, so we avoid pumping
	// the main loop afterwards (an iteration would honor the quit and tear down
	// mid-assertion).
	dialog->emit_signal(SNAME("canceled"));

	p_driver.set_step("verify_quit_requested");
	SceneTree *tree = dialog->get_tree();
	if (tree == nullptr || !tree->is_quitting()) {
		return _failure_with_message(p_driver, result.workflow, "Dismissing the startup dialog must quit Foundry.");
	}

	// Dismissal must never reveal the project workspace on its way out.
	state = p_driver.read_editor_state();
	if ((bool)state.get("workspace_exposed", true)) {
		return _failure_with_message(p_driver, result.workflow, "Dismissing the startup dialog must not expose the workspace.");
	}
#else
	return _failure_with_message(p_driver, result.workflow, "Projectless shell dismissal requires an editor (TOOLS_ENABLED) build.");
#endif

	result.ok = true;
	result.message = "Projectless editor shell dismissal quit the application.";
	result.details = state;
	return result;
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_projectless_shell_open_in_process(EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "projectless_shell_open_in_process";

	p_driver.begin_workflow();

	p_driver.set_step("wait_for_editor_ready");
	if (!p_driver.wait_editor_idle(30000)) {
		return _failure_from_driver(p_driver, result.workflow, "Editor did not become ready in projectless mode.");
	}

#ifdef TOOLS_ENABLED
	EditorNode *editor_node = EditorNode::get_singleton();
	StartupDialog *dialog = editor_node != nullptr ? editor_node->get_startup_dialog() : nullptr;
	if (editor_node == nullptr || dialog == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Editor node or startup dialog was unavailable.");
	}

	p_driver.set_step("verify_projectless_state");
	Dictionary state = p_driver.read_editor_state();
	if (!(bool)state.get("projectless_shell", false)) {
		return _failure_with_message(p_driver, result.workflow, "Editor did not boot in projectless shell mode.");
	}
	if ((bool)state.get("workspace_exposed", true)) {
		return _failure_with_message(p_driver, result.workflow, "Projectless shell must not expose the workspace before a project loads.");
	}

	const int pid_before = OS::get_singleton()->get_process_id();

	// Materialize a throwaway project on disk with an empty (touched) project.foundry,
	// which also exercises first-open versioning: the in-process load must persist a
	// config_version and initial settings just like a normal editor open.
	p_driver.set_step("create_temp_project");
	const String project_dir = EditorPaths::get_singleton()->get_temp_dir().path_join("inproc_open_" + String::num_uint64(OS::get_singleton()->get_ticks_usec()));
	const String project_config_path = project_dir.path_join("project.foundry");
	{
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		if (dir.is_null() || dir->make_dir_recursive(project_dir) != OK) {
			return _failure_with_message(p_driver, result.workflow, "Could not create temp project directory.");
		}
		Ref<FileAccess> config = FileAccess::open(project_config_path, FileAccess::WRITE);
		if (config.is_null()) {
			return _failure_with_message(p_driver, result.workflow, "Could not write temp project.foundry.");
		}
		config->close();
	}

	// Drive the real startup-dialog open entry: an in-process load plus a single
	// recents recording, no process relaunch.
	p_driver.set_step("open_project_in_process");
	const Error open_err = dialog->open_project_path(project_dir);
	if (open_err != OK) {
		return _failure_with_message(p_driver, result.workflow, vformat("open_project_path returned error %d.", open_err));
	}

	// A relaunch would have torn this workflow's process down; an unchanged PID is an
	// explicit no-relaunch check.
	if (OS::get_singleton()->get_process_id() != pid_before) {
		return _failure_with_message(p_driver, result.workflow, "Process id changed; the open must not relaunch.");
	}

	p_driver.set_step("wait_for_project_scan");
	if (!p_driver.wait_editor_idle(30000)) {
		return _failure_from_driver(p_driver, result.workflow, "Editor did not become ready after in-process project load.");
	}
	if (!p_driver.wait_import_idle(30000)) {
		return _failure_from_driver(p_driver, result.workflow, "Filesystem/import pipeline did not settle after in-process load.");
	}

	p_driver.set_step("verify_project_state");
	state = p_driver.read_editor_state();
	if ((bool)state.get("projectless_shell", true)) {
		return _failure_with_message(p_driver, result.workflow, "projectless_shell must be false after in-process load.");
	}
	if (String(state.get("mode", String())) != "project") {
		return _failure_with_message(p_driver, result.workflow, "Editor must report mode == project after in-process load.");
	}
	if (!(bool)state.get("workspace_exposed", false)) {
		return _failure_with_message(p_driver, result.workflow, "Workspace must be exposed after in-process load.");
	}
	if (!(bool)state.get("project_loaded", false)) {
		return _failure_with_message(p_driver, result.workflow, "ProjectSettings must report the project loaded.");
	}
	if ((bool)state.get("startup_dialog_visible", true)) {
		return _failure_with_message(p_driver, result.workflow, "Startup dialog must be hidden after in-process load.");
	}

	if (!ProjectSettings::get_singleton()->is_project_loaded()) {
		return _failure_with_message(p_driver, result.workflow, "ProjectSettings singleton did not report a loaded project.");
	}

	// The bottom drawer strip and Run Project controls, hidden by the shell, must now
	// be reachable again.
	p_driver.set_step("verify_workspace_surfaces_revealed");
	{
		Dictionary run_selector;
		run_selector["role"] = "button";
		run_selector["name"] = "Run Project";
		const Dictionary run_find = p_driver.find(run_selector);
		if (!(bool)run_find.get("ok", false) || ((Array)run_find.get("elements", Array())).is_empty()) {
			return _failure_with_message(p_driver, result.workflow, "Run Project controls must be available after in-process load.");
		}
	}

	// First-open versioning: the empty project.foundry must now be persisted with a
	// config_version (finding parity with a normal editor open).
	p_driver.set_step("verify_project_config_persisted");
	if (FileAccess::get_size(project_config_path) < 10) {
		return _failure_with_message(p_driver, result.workflow, "Empty project.foundry was not versioned/persisted after in-process load.");
	}

	// Recents recorded exactly once: the on-disk known-project store lists the project.
	p_driver.set_step("verify_recents_recorded");
	{
		KnownProjectStore store;
		store.load();
		KnownProjectStore::KnownProject recorded;
		if (!store.get_project(project_dir, recorded)) {
			return _failure_with_message(p_driver, result.workflow, "Known-project store did not record the opened project.");
		}
	}

	p_driver.set_step("assert_no_new_errors");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "Projectless shell loaded a project in-process without a relaunch.";
	result.details = state;
	return result;
#else
	return _failure_with_message(p_driver, result.workflow, "In-process project load requires an editor (TOOLS_ENABLED) build.");
#endif
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_projectless_shell_open_in_process_fallback(EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "projectless_shell_open_in_process_fallback";

	p_driver.begin_workflow();

	p_driver.set_step("wait_for_editor_ready");
	if (!p_driver.wait_editor_idle(30000)) {
		return _failure_from_driver(p_driver, result.workflow, "Editor did not become ready in projectless mode.");
	}

#ifdef TOOLS_ENABLED
	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Editor node was unavailable.");
	}

	p_driver.set_step("verify_projectless_state");
	Dictionary state = p_driver.read_editor_state();
	if (!(bool)state.get("projectless_shell", false)) {
		return _failure_with_message(p_driver, result.workflow, "Editor did not boot in projectless shell mode.");
	}

	// A directory without a project.foundry must not be adoptable in-process. The load
	// must refuse cleanly and leave the projectless shell intact for the caller to fall
	// back to a relaunch -- it must never leave a half-loaded editor.
	p_driver.set_step("attempt_load_missing_project");
	const String empty_dir = EditorPaths::get_singleton()->get_temp_dir().path_join("inproc_missing_" + String::num_uint64(OS::get_singleton()->get_ticks_usec()));
	{
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		if (dir.is_null() || dir->make_dir_recursive(empty_dir) != OK) {
			return _failure_with_message(p_driver, result.workflow, "Could not create temp directory.");
		}
	}
	if (editor_node->load_project_in_process(empty_dir)) {
		return _failure_with_message(p_driver, result.workflow, "load_project_in_process must reject a directory without project.foundry.");
	}

	p_driver.set_step("verify_shell_intact");
	if (!p_driver.wait_editor_idle(30000)) {
		return _failure_from_driver(p_driver, result.workflow, "Editor did not settle after a rejected in-process load.");
	}
	state = p_driver.read_editor_state();
	if (!(bool)state.get("projectless_shell", false)) {
		return _failure_with_message(p_driver, result.workflow, "A rejected in-process load must leave the projectless shell intact.");
	}
	if ((bool)state.get("project_loaded", true)) {
		return _failure_with_message(p_driver, result.workflow, "A rejected in-process load must not load a project.");
	}
	if ((bool)state.get("workspace_exposed", true)) {
		return _failure_with_message(p_driver, result.workflow, "A rejected in-process load must not expose the workspace.");
	}
	if (!(bool)state.get("startup_dialog_visible", false)) {
		return _failure_with_message(p_driver, result.workflow, "The startup dialog must stay visible after a rejected load.");
	}

	result.ok = true;
	result.message = "Rejected in-process load left the projectless shell intact.";
	result.details = state;
	return result;
#else
	return _failure_with_message(p_driver, result.workflow, "In-process project load requires an editor (TOOLS_ENABLED) build.");
#endif
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_projectless_shell_open_in_process_autoload(EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "projectless_shell_open_in_process_autoload";

	p_driver.begin_workflow();

	p_driver.set_step("wait_for_editor_ready");
	if (!p_driver.wait_editor_idle(30000)) {
		return _failure_from_driver(p_driver, result.workflow, "Editor did not become ready in projectless mode.");
	}

#ifdef TOOLS_ENABLED
	EditorNode *editor_node = EditorNode::get_singleton();
	StartupDialog *dialog = editor_node != nullptr ? editor_node->get_startup_dialog() : nullptr;
	if (editor_node == nullptr || dialog == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Editor node or startup dialog was unavailable.");
	}

	// A project whose autoloads are only known after the project loads exercises the
	// autoload-cache rebuild: EditorAutoloadSettings caches autoloads at construction
	// (empty in the shell), so the in-process load must refresh it before the first
	// scan instantiates them. The autoload is a tool script so it is added to the
	// editor scene tree and observable as /root/InProcAutoload.
	p_driver.set_step("create_temp_project");
	const String project_dir = EditorPaths::get_singleton()->get_temp_dir().path_join("inproc_autoload_" + String::num_uint64(OS::get_singleton()->get_ticks_usec()));
	{
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		if (dir.is_null() || dir->make_dir_recursive(project_dir) != OK) {
			return _failure_with_message(p_driver, result.workflow, "Could not create temp project directory.");
		}
		Ref<FileAccess> script = FileAccess::open(project_dir.path_join("in_proc_autoload.fs"), FileAccess::WRITE);
		if (script.is_null()) {
			return _failure_with_message(p_driver, result.workflow, "Could not write autoload script.");
		}
		script->store_line("@tool");
		script->store_line("extends Node");
		script->store_line("");
		script->store_line("func _ready() -> void:");
		script->store_line("\tpass");
		script->close();

		Ref<FileAccess> config = FileAccess::open(project_dir.path_join("project.foundry"), FileAccess::WRITE);
		if (config.is_null()) {
			return _failure_with_message(p_driver, result.workflow, "Could not write temp project.foundry.");
		}
		config->store_line("config_version=5");
		config->store_line("");
		config->store_line("[autoload]");
		config->store_line("");
		config->store_line("InProcAutoload=\"*res://in_proc_autoload.fs\"");
		config->close();
	}

	p_driver.set_step("open_project_in_process");
	const Error open_err = dialog->open_project_path(project_dir);
	if (open_err != OK) {
		return _failure_with_message(p_driver, result.workflow, vformat("open_project_path returned error %d.", open_err));
	}

	// This project carries a Foundry Script autoload, so the first scan also compiles
	// and analyzes a script; give the scan/import pipeline extra headroom over the
	// scene-only workflows.
	p_driver.set_step("wait_for_project_scan");
	if (!p_driver.wait_editor_idle(60000)) {
		return _failure_from_driver(p_driver, result.workflow, "Editor did not become ready after in-process project load.");
	}
	if (!p_driver.wait_import_idle(60000)) {
		return _failure_from_driver(p_driver, result.workflow, "Filesystem/import pipeline did not settle after in-process load.");
	}
	p_driver.wait_script_analysis_idle(60000);

	p_driver.set_step("verify_autoload_instantiated");
	Dictionary state = p_driver.read_editor_state();
	if (String(state.get("mode", String())) != "project") {
		return _failure_with_message(p_driver, result.workflow, "Editor must report mode == project after in-process load.");
	}
	SceneTree *tree = editor_node->get_tree();
	Node *autoload_node = (tree != nullptr && tree->get_root() != nullptr) ? tree->get_root()->get_node_or_null(NodePath("InProcAutoload")) : nullptr;
	if (autoload_node == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Project autoload was not instantiated after in-process load.");
	}

	p_driver.set_step("assert_no_new_errors");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "Project autoloads were instantiated after in-process load.";
	result.details = state;
	return result;
#else
	return _failure_with_message(p_driver, result.workflow, "In-process project load requires an editor (TOOLS_ENABLED) build.");
#endif
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_projectless_shell_open_in_process_debug_options(EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "projectless_shell_open_in_process_debug_options";

	p_driver.begin_workflow();

	p_driver.set_step("wait_for_editor_ready");
	if (!p_driver.wait_editor_idle(30000)) {
		return _failure_from_driver(p_driver, result.workflow, "Editor did not become ready in projectless mode.");
	}

#ifdef TOOLS_ENABLED
	EditorNode *editor_node = EditorNode::get_singleton();
	StartupDialog *dialog = editor_node != nullptr ? editor_node->get_startup_dialog() : nullptr;
	if (editor_node == nullptr || dialog == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Editor node or startup dialog was unavailable.");
	}

	DebuggerEditorPlugin *debugger_plugin = DebuggerEditorPlugin::get_singleton();
	if (debugger_plugin == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "DebuggerEditorPlugin singleton was unavailable.");
	}

	// The projectless shell applied debug options once (NOTIFICATION_READY) against the
	// shell defaults: deploy-remote/live-debug/reload-scripts on, everything else off.
	// Verifying the pre-open state makes the post-open assertions meaningful.
	p_driver.set_step("verify_shell_debug_defaults");
	if (!debugger_plugin->is_debug_option_checked("run_deploy_remote_debug") ||
			!debugger_plugin->is_debug_option_checked("run_live_debug") ||
			!debugger_plugin->is_debug_option_checked("run_reload_scripts") ||
			debugger_plugin->is_debug_option_checked("run_debug_collisions") ||
			debugger_plugin->is_debug_option_checked("run_debug_navigation")) {
		return _failure_with_message(p_driver, result.workflow, "Debug menu did not start at the projectless shell defaults.");
	}

	// Materialize a throwaway project whose saved debug options differ from the shell
	// defaults in both directions: enable two options that default off and disable two
	// that default on. The metadata lives in the project's editor data dir, exactly
	// where EditorSettings::get_project_metadata() reads it after the in-process load
	// re-points EditorPaths at the project.
	p_driver.set_step("create_temp_project");
	const String project_dir = EditorPaths::get_singleton()->get_temp_dir().path_join("inproc_debug_opts_" + String::num_uint64(OS::get_singleton()->get_ticks_usec()));
	{
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		if (dir.is_null() || dir->make_dir_recursive(project_dir) != OK) {
			return _failure_with_message(p_driver, result.workflow, "Could not create temp project directory.");
		}
		Ref<FileAccess> config = FileAccess::open(project_dir.path_join("project.foundry"), FileAccess::WRITE);
		if (config.is_null()) {
			return _failure_with_message(p_driver, result.workflow, "Could not write temp project.foundry.");
		}
		config->store_line("config_version=5");
		config->close();

		Ref<ConfigFile> metadata;
		metadata.instantiate();
		metadata->set_value("debug_options", "run_debug_collisions", true);
		metadata->set_value("debug_options", "run_debug_navigation", true);
		metadata->set_value("debug_options", "run_deploy_remote_debug", false);
		metadata->set_value("debug_options", "run_live_debug", false);
		// ConfigFile::save() does not create parent directories, so materialize the
		// project's editor data dir first.
		const String metadata_dir = project_dir.path_join(ProjectSettings::get_singleton()->get_project_data_dir_name()).path_join("editor");
		if (dir->make_dir_recursive(metadata_dir) != OK) {
			return _failure_with_message(p_driver, result.workflow, "Could not create project editor data directory.");
		}
		const String metadata_path = metadata_dir.path_join("project_metadata.cfg");
		if (metadata->save(metadata_path) != OK) {
			return _failure_with_message(p_driver, result.workflow, "Could not write project debug-option metadata.");
		}
	}

	p_driver.set_step("open_project_in_process");
	const Error open_err = dialog->open_project_path(project_dir);
	if (open_err != OK) {
		return _failure_with_message(p_driver, result.workflow, vformat("open_project_path returned error %d.", open_err));
	}

	p_driver.set_step("wait_for_project_scan");
	if (!p_driver.wait_editor_idle(30000)) {
		return _failure_from_driver(p_driver, result.workflow, "Editor did not become ready after in-process project load.");
	}
	if (!p_driver.wait_import_idle(30000)) {
		return _failure_from_driver(p_driver, result.workflow, "Filesystem/import pipeline did not settle after in-process load.");
	}

	p_driver.set_step("verify_project_state");
	Dictionary state = p_driver.read_editor_state();
	if (String(state.get("mode", String())) != "project") {
		return _failure_with_message(p_driver, result.workflow, "Editor must report mode == project after in-process load.");
	}

	// The opened project's debug options must now be reflected: the options it enables
	// are checked, and -- the crux of this fix -- the shell defaults it disables are
	// turned back off rather than left checked.
	p_driver.set_step("verify_debug_options_applied");
	if (!debugger_plugin->is_debug_option_checked("run_debug_collisions")) {
		return _failure_with_message(p_driver, result.workflow, "run_debug_collisions should be enabled after in-process load.");
	}
	if (!debugger_plugin->is_debug_option_checked("run_debug_navigation")) {
		return _failure_with_message(p_driver, result.workflow, "run_debug_navigation should be enabled after in-process load.");
	}
	if (debugger_plugin->is_debug_option_checked("run_deploy_remote_debug")) {
		return _failure_with_message(p_driver, result.workflow, "run_deploy_remote_debug should be disabled after in-process load.");
	}
	if (debugger_plugin->is_debug_option_checked("run_live_debug")) {
		return _failure_with_message(p_driver, result.workflow, "run_live_debug should be disabled after in-process load.");
	}
	// run_reload_scripts defaulted on in the shell and the project does not override it,
	// so it must remain on (a re-apply must not clobber untouched options).
	if (!debugger_plugin->is_debug_option_checked("run_reload_scripts")) {
		return _failure_with_message(p_driver, result.workflow, "run_reload_scripts should remain enabled after in-process load.");
	}

	p_driver.set_step("assert_no_new_errors");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "Project debug options were re-applied after in-process load.";
	result.details = state;
	return result;
#else
	return _failure_with_message(p_driver, result.workflow, "In-process project load requires an editor (TOOLS_ENABLED) build.");
#endif
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_startup_dialog_projects_tab(EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "startup_dialog_projects_tab";

	p_driver.begin_workflow();

	p_driver.set_step("wait_for_editor_ready");
	if (!p_driver.wait_editor_idle(30000)) {
		return _failure_from_driver(p_driver, result.workflow, "Editor did not become ready in projectless mode.");
	}

	p_driver.set_step("verify_startup_dialog_visible");
	const Dictionary state = p_driver.read_editor_state();
	if (!(bool)state.get("startup_dialog_visible", false)) {
		return _failure_with_message(p_driver, result.workflow, "Startup dialog was not visible.");
	}

	auto find_named = [&](const String &p_name, const String &p_role = "button") -> bool {
		Dictionary selector;
		selector["role"] = p_role;
		selector["name"] = p_name;
		const Dictionary find_result = p_driver.find(selector);
		return (bool)find_result.get("ok", false) && ((Array)find_result.get("elements", Array())).size() > 0;
	};

	p_driver.set_step("verify_header_and_tabs");
	if (!find_named("Foundry Engine", "label") && !find_named("Startup Dialog", "dialog")) {
		return _failure_with_message(p_driver, result.workflow, "Startup dialog header was not found.");
	}
	if (!find_named("Create Project")) {
		return _failure_with_message(p_driver, result.workflow, "Create Project action was not found.");
	}
	if (!find_named("Open Existing Project")) {
		return _failure_with_message(p_driver, result.workflow, "Open Existing Project action was not found.");
	}
	if (!find_named("Projects", "tab")) {
		return _failure_with_message(p_driver, result.workflow, "Projects tab was not found.");
	}
	if (!find_named("Manage", "tab")) {
		return _failure_with_message(p_driver, result.workflow, "Manage tab was not found.");
	}
	if (!find_named("About", "tab")) {
		return _failure_with_message(p_driver, result.workflow, "About tab was not found.");
	}

	p_driver.set_step("assert_no_new_errors");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "Startup dialog Projects tab workflow completed.";
	result.details = state;
	return result;
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_startup_dialog_manage_tab(EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "startup_dialog_manage_tab";

	p_driver.begin_workflow();

	p_driver.set_step("wait_for_editor_ready");
	if (!p_driver.wait_editor_idle(30000)) {
		return _failure_from_driver(p_driver, result.workflow, "Editor did not become ready in projectless mode.");
	}

	p_driver.set_step("verify_startup_dialog_visible");
	const Dictionary state = p_driver.read_editor_state();
	if (!(bool)state.get("startup_dialog_visible", false)) {
		return _failure_with_message(p_driver, result.workflow, "Startup dialog was not visible.");
	}

	auto find_named = [&](const String &p_name, const String &p_role) -> Dictionary {
		Dictionary selector;
		selector["role"] = p_role;
		selector["name"] = p_name;
		return p_driver.find(selector);
	};
	auto has_named = [&](const String &p_name, const String &p_role) -> bool {
		const Dictionary found = find_named(p_name, p_role);
		return (bool)found.get("ok", false) && ((Array)found.get("elements", Array())).size() > 0;
	};

	p_driver.set_step("select_manage_tab");
	Dictionary manage_tab_selector;
	manage_tab_selector["role"] = "tab";
	manage_tab_selector["name"] = "Manage";
	if (!p_driver.require_ok(p_driver.act(manage_tab_selector, "select"), "select_manage_tab")) {
		return _failure_from_driver(p_driver, result.workflow, "Could not switch to the Manage tab.");
	}
	// Let the newly-shown tab page lay out and become visible before snapshotting.
	p_driver.flush_frames(2);
	p_driver.wait_editor_idle(5000);

	p_driver.set_step("verify_manage_controls");
	if (!has_named("Filter Projects", "text_field")) {
		return _failure_with_message(p_driver, result.workflow, "Manage filter field was not found.");
	}
	if (!has_named("Scan Folder", "button")) {
		return _failure_with_message(p_driver, result.workflow, "Scan Folder action was not found.");
	}
	if (!has_named("Remove Missing Projects", "button")) {
		return _failure_with_message(p_driver, result.workflow, "Remove Missing action was not found.");
	}

	// Asset Library must not be carried into the Manage surface.
	if (has_named("Asset Library", "button") || has_named("Asset Library", "tab")) {
		return _failure_with_message(p_driver, result.workflow, "Asset Library must not appear in Manage.");
	}

	p_driver.set_step("filter_smoke");
	Dictionary filter_selector;
	filter_selector["role"] = "text_field";
	filter_selector["name"] = "Filter Projects";
	Dictionary filter_args;
	filter_args["text"] = "zzzz-no-such-project";
	if (!p_driver.require_ok(p_driver.act(filter_selector, "set_text", filter_args), "filter_smoke")) {
		return _failure_from_driver(p_driver, result.workflow, "Could not type into the Manage filter field.");
	}

	p_driver.set_step("assert_no_new_errors");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "Startup dialog Manage tab workflow completed.";
	result.details = state;
	return result;
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_startup_dialog_about_tab(EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "startup_dialog_about_tab";

	p_driver.begin_workflow();

	p_driver.set_step("wait_for_editor_ready");
	if (!p_driver.wait_editor_idle(30000)) {
		return _failure_from_driver(p_driver, result.workflow, "Editor did not become ready in projectless mode.");
	}

	p_driver.set_step("verify_startup_dialog_visible");
	const Dictionary state = p_driver.read_editor_state();
	if (!(bool)state.get("startup_dialog_visible", false)) {
		return _failure_with_message(p_driver, result.workflow, "Startup dialog was not visible.");
	}

	auto has_named = [&](const String &p_name, const String &p_role) -> bool {
		Dictionary selector;
		selector["role"] = p_role;
		selector["name"] = p_name;
		const Dictionary found = p_driver.find(selector);
		return (bool)found.get("ok", false) && ((Array)found.get("elements", Array())).size() > 0;
	};

	p_driver.set_step("select_about_tab");
	Dictionary about_tab_selector;
	about_tab_selector["role"] = "tab";
	about_tab_selector["name"] = "About";
	if (!p_driver.require_ok(p_driver.act(about_tab_selector, "select"), "select_about_tab")) {
		return _failure_from_driver(p_driver, result.workflow, "Could not switch to the About tab.");
	}
	// Let the newly-shown tab page lay out and become visible before snapshotting.
	p_driver.flush_frames(2);
	p_driver.wait_editor_idle(5000);

	p_driver.set_step("verify_about_content");
	if (!has_named("Foundry Engine Version", "label")) {
		return _failure_with_message(p_driver, result.workflow, "About version string was not found.");
	}
	if (!has_named("Copyright", "label")) {
		return _failure_with_message(p_driver, result.workflow, "About copyright text was not found.");
	}
	if (!has_named("Full Credits", "button")) {
		return _failure_with_message(p_driver, result.workflow, "Full Credits action was not found.");
	}
	if (!has_named("Third-party Notices", "button")) {
		return _failure_with_message(p_driver, result.workflow, "Third-party Notices action was not found.");
	}

	p_driver.set_step("assert_no_new_errors");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "Startup dialog About tab workflow completed.";
	result.details = state;
	return result;
}

void EditorAutomationAcceptanceWorkflow::print_result(const Result &p_result) {
	Dictionary payload;
	payload["workflow"] = p_result.workflow;
	payload["ok"] = p_result.ok;
	payload["message"] = p_result.message;
	if (!p_result.details.is_empty()) {
		payload["details"] = p_result.details;
	}
	OS::get_singleton()->print("FOUNDRY_AUTOMATION_WORKFLOW %s\n", JSON::stringify(payload, "", false).utf8().get_data());
	if (!p_result.ok) {
		if (p_result.details.has("failure_report")) {
			OS::get_singleton()->printerr("%s", String(p_result.details["failure_report"]).utf8().get_data());
		} else {
			OS::get_singleton()->printerr("%s\n", p_result.message.utf8().get_data());
		}
	}
}
