/**************************************************************************/
/*  test_scene_pane_tile_mode.h                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             FOUNDRY ENGINE                             */
/*          A fork of the Godot Engine (https://godotengine.org)          */
/*                       https://www.cafecito.games                       */
/**************************************************************************/
/* Copyright (c) 2026-present Cafecito Games LLC.                         */
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

#include "core/io/config_file.h"
#include "editor/editor_data.h"
#include "editor/editor_scene_context.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/gui/editor_scene_mode_switcher.h"
#include "editor/scene/editor_scene_tabs.h"
#include "editor/workspace/workspace_pane.h"

#include "scene/2d/node_2d.h"
#include "scene/3d/node_3d.h"
#include "scene/gui/button.h"
#include "scene/scene_string_names.h"

#include "tests/test_macros.h"

namespace TestScenePaneTileMode {

static int requested_count = 0;
static int requested_tile_id = -1;
static int requested_mode = -1;

static void record_requested_mode(int p_tile_id, int p_mode) {
	requested_count++;
	requested_tile_id = p_tile_id;
	requested_mode = p_mode;
}

TEST_CASE("[Editor][ScenePaneTileMode] first scene infers mode and manual choice is sticky") {
	EditorData editor_data;
	EditorSelection selection;
	editor_data.set_focused_tile_id(7);
	const int scene_index = editor_data.add_edited_scene(-1);
	EditorSceneContext *context_3d = editor_data.get_scene_context(scene_index);
	REQUIRE(context_3d != nullptr);
	context_3d->set_scene_root_node(memnew(Node3D));

	ScenePaneTile *tile = memnew(ScenePaneTile);
	tile->setup(7, &selection, editor_data);
	EditorSceneModeSwitcher *switcher = tile->get_scene_mode_switcher();
	REQUIRE(switcher != nullptr);
	CHECK(switcher->get_accessibility_description() == "Editing mode for scene tile 7");

	requested_count = 0;
	requested_tile_id = -1;
	requested_mode = -1;
	tile->connect(SNAME("scene_editor_mode_requested"), callable_mp_static(&record_requested_mode));
	tile->get_scene_tabs()->emit_signal(SNAME("tabs_updated"));
	CHECK(tile->get_scene_editor_mode() == SceneEditorMode::MODE_3D);
	CHECK(tile->is_scene_editor_mode_initialized());
	CHECK(switcher->is_mode_available());
	CHECK(requested_count == 0);

	switcher->get_2d_button()->emit_signal(SceneStringName(pressed));
	CHECK(tile->get_scene_editor_mode() == SceneEditorMode::MODE_2D);
	CHECK(requested_count == 1);
	CHECK(requested_tile_id == 7);
	CHECK(requested_mode == int(SceneEditorMode::MODE_2D));

	EditorSceneContext *context_2d = memnew(EditorSceneContext);
	context_2d->set_scene_root_node(memnew(Node2D));
	tile->initialize_scene_editor_mode(context_2d);
	CHECK(tile->get_scene_editor_mode() == SceneEditorMode::MODE_2D);
	CHECK(requested_count == 1);

	memdelete(context_2d);
	memdelete(tile);
}

TEST_CASE("[Editor][ScenePaneTileMode] rootless context defers inference until a rooted scene arrives") {
	EditorData editor_data;
	EditorSelection selection;

	ScenePaneTile *tile = memnew(ScenePaneTile);
	tile->setup(20, &selection, editor_data);
	requested_count = 0;
	tile->connect(SNAME("scene_editor_mode_requested"), callable_mp_static(&record_requested_mode));

	EditorSceneContext *rootless_context = memnew(EditorSceneContext);
	REQUIRE(rootless_context->get_scene_root_node() == nullptr);
	tile->initialize_scene_editor_mode(rootless_context);
	CHECK_FALSE(tile->is_scene_editor_mode_initialized());
	CHECK(requested_count == 0);

	EditorSceneContext *context_3d = memnew(EditorSceneContext);
	context_3d->set_scene_root_node(memnew(Node3D));
	tile->initialize_scene_editor_mode(context_3d);
	CHECK(tile->is_scene_editor_mode_initialized());
	CHECK(tile->get_scene_editor_mode() == SceneEditorMode::MODE_3D);
	CHECK(requested_count == 0);

	ScenePaneTile *tile_2d = memnew(ScenePaneTile);
	tile_2d->setup(21, &selection, editor_data);
	EditorSceneContext *rootless_first = memnew(EditorSceneContext);
	tile_2d->initialize_scene_editor_mode(rootless_first);
	CHECK_FALSE(tile_2d->is_scene_editor_mode_initialized());
	EditorSceneContext *context_2d = memnew(EditorSceneContext);
	context_2d->set_scene_root_node(memnew(Node2D));
	tile_2d->initialize_scene_editor_mode(context_2d);
	CHECK(tile_2d->is_scene_editor_mode_initialized());
	CHECK(tile_2d->get_scene_editor_mode() == SceneEditorMode::MODE_2D);

	memdelete(context_2d);
	memdelete(rootless_first);
	memdelete(tile_2d);
	memdelete(context_3d);
	memdelete(rootless_context);
	memdelete(tile);
}

TEST_CASE("[Editor][ScenePaneTileMode] layout round-trips mode and missing key stays uninitialized") {
	EditorData editor_data;
	EditorSelection selection;

	ScenePaneTile *source = memnew(ScenePaneTile);
	source->setup(8, &selection, editor_data);
	source->set_scene_editor_mode(SceneEditorMode::MODE_3D, false);

	Ref<ConfigFile> saved;
	saved.instantiate();
	source->save_layout(saved, "Tile");
	const Variant saved_mode = saved->get_value("Tile", "scene_editor_mode");
	CHECK(saved_mode.get_type() == Variant::STRING);
	CHECK(String(saved_mode) == "3d");

	ScenePaneTile *restored = memnew(ScenePaneTile);
	restored->setup(9, &selection, editor_data);
	restored->load_layout(saved, "Tile");
	CHECK(restored->is_scene_editor_mode_initialized());
	CHECK(restored->get_scene_editor_mode() == SceneEditorMode::MODE_3D);

	Ref<ConfigFile> legacy;
	legacy.instantiate();
	ScenePaneTile *legacy_tile = memnew(ScenePaneTile);
	legacy_tile->setup(10, &selection, editor_data);
	legacy_tile->load_layout(legacy, "Tile");
	CHECK_FALSE(legacy_tile->is_scene_editor_mode_initialized());

	Ref<ConfigFile> invalid;
	invalid.instantiate();
	invalid->set_value("Tile", "scene_editor_mode", StringName("sideways"));
	ScenePaneTile *invalid_tile = memnew(ScenePaneTile);
	invalid_tile->setup(11, &selection, editor_data);
	invalid_tile->load_layout(invalid, "Tile");
	CHECK_FALSE(invalid_tile->is_scene_editor_mode_initialized());

	memdelete(invalid_tile);
	memdelete(legacy_tile);
	memdelete(restored);
	memdelete(source);
}

TEST_CASE("[Editor][ScenePaneTileMode] durable mode is independent from preview presentation") {
	EditorData editor_data;
	EditorSelection selection;
	ScenePaneTile *tile = memnew(ScenePaneTile);
	tile->setup(12, &selection, editor_data);

	tile->set_scene_editor_mode(SceneEditorMode::MODE_2D, false);
	tile->set_preview_mode(TilePreviewMode::LIVE_3D);
	CHECK(tile->get_scene_editor_mode() == SceneEditorMode::MODE_2D);
	CHECK(tile->get_preview_mode() == TilePreviewMode::LIVE_3D);

	memdelete(tile);
}

TEST_CASE("[Editor][ScenePaneTileMode] disabled 3D feature forces 2D inference") {
	EditorData editor_data;
	EditorSelection selection;
	ScenePaneTile *tile = memnew(ScenePaneTile);
	tile->setup(13, &selection, editor_data);

	EditorSceneContext *context_3d = memnew(EditorSceneContext);
	context_3d->set_scene_root_node(memnew(Node3D));
	tile->set_3d_scene_mode_enabled(false);
	tile->initialize_scene_editor_mode(context_3d);
	CHECK(tile->get_scene_editor_mode() == SceneEditorMode::MODE_2D);
	CHECK(tile->is_scene_editor_mode_initialized());
	CHECK_FALSE(tile->get_scene_mode_switcher()->is_visible());

	tile->set_3d_scene_mode_enabled(true);
	CHECK(tile->get_scene_mode_switcher()->is_visible());
	CHECK(tile->get_scene_editor_mode() == SceneEditorMode::MODE_2D);

	memdelete(context_3d);
	memdelete(tile);
}

TEST_CASE("[Editor][ScenePaneTileMode] switcher stays visible with workspace scene tabs") {
	EditorData editor_data;
	EditorSelection selection;
	const int tile_id = 23;
	const int scene_index = editor_data.add_edited_scene(-1);
	EditorSceneContext *context = editor_data.get_scene_context(scene_index);
	REQUIRE(context != nullptr);
	context->set_scene_root_node(memnew(Node2D));
	editor_data.set_scene_tile(scene_index, tile_id);
	editor_data.set_tile_current_scene(tile_id, scene_index);

	WorkspacePane *pane = memnew(WorkspacePane);
	SceneTree::get_singleton()->get_root()->add_child(pane);
	pane->set_size(Size2(800, 600));
	pane->setup(tile_id, &selection, &editor_data);
	pane->sync_scene_tabs_from_editor_data(false);
	SceneTree::get_singleton()->process(0.016);
	MessageQueue::get_singleton()->flush();

	ScenePaneTile *tile = pane->get_scene_tile();
	REQUIRE(tile != nullptr);
	REQUIRE(tile->get_scene_mode_switcher() != nullptr);
	CHECK_FALSE(tile->get_scene_tabs()->is_visible());
	CHECK(tile->get_scene_mode_switcher()->is_visible_in_tree());

	SceneTree::get_singleton()->get_root()->remove_child(pane);
	memdelete(pane);
}

TEST_CASE("[Editor][ScenePaneTileMode] disabled 3D feature constrains durable assignments and restore") {
	EditorData editor_data;
	EditorSelection selection;

	ScenePaneTile *direct = memnew(ScenePaneTile);
	direct->setup(14, &selection, editor_data);
	direct->set_3d_scene_mode_enabled(false);
	requested_count = 0;
	direct->connect(SNAME("scene_editor_mode_requested"), callable_mp_static(&record_requested_mode));
	direct->set_scene_editor_mode(SceneEditorMode::MODE_3D, false);
	CHECK(direct->is_scene_editor_mode_initialized());
	CHECK(direct->get_scene_editor_mode() == SceneEditorMode::MODE_2D);
	CHECK(requested_count == 0);

	ScenePaneTile *user_routed = memnew(ScenePaneTile);
	user_routed->setup(19, &selection, editor_data);
	user_routed->set_3d_scene_mode_enabled(false);
	requested_count = 0;
	requested_tile_id = -1;
	requested_mode = -1;
	user_routed->connect(SNAME("scene_editor_mode_requested"), callable_mp_static(&record_requested_mode));
	user_routed->get_scene_mode_switcher()->emit_signal(SNAME("mode_selected"), int(SceneEditorMode::MODE_3D));
	CHECK(user_routed->get_scene_editor_mode() == SceneEditorMode::MODE_2D);
	CHECK(requested_count == 1);
	CHECK(requested_tile_id == 19);
	CHECK(requested_mode == int(SceneEditorMode::MODE_2D));

	Ref<ConfigFile> saved_3d;
	saved_3d.instantiate();
	saved_3d->set_value("Tile", "scene_editor_mode", StringName("3d"));
	ScenePaneTile *restored = memnew(ScenePaneTile);
	restored->setup(15, &selection, editor_data);
	restored->set_3d_scene_mode_enabled(false);
	requested_count = 0;
	restored->connect(SNAME("scene_editor_mode_requested"), callable_mp_static(&record_requested_mode));
	restored->load_layout(saved_3d, "Tile");
	CHECK(restored->is_scene_editor_mode_initialized());
	CHECK(restored->get_scene_editor_mode() == SceneEditorMode::MODE_2D);
	CHECK(requested_count == 0);

	Ref<ConfigFile> normalized;
	normalized.instantiate();
	restored->save_layout(normalized, "Tile");
	CHECK(StringName(normalized->get_value("Tile", "scene_editor_mode")) == StringName("2d"));

	ScenePaneTile *fallback = memnew(ScenePaneTile);
	fallback->setup(16, &selection, editor_data);
	fallback->set_scene_editor_mode(SceneEditorMode::MODE_3D, false);
	CHECK(fallback->get_scene_editor_mode() == SceneEditorMode::MODE_3D);
	requested_count = 0;
	fallback->connect(SNAME("scene_editor_mode_requested"), callable_mp_static(&record_requested_mode));
	fallback->set_3d_scene_mode_enabled(false);
	CHECK(fallback->get_scene_editor_mode() == SceneEditorMode::MODE_2D);
	CHECK(requested_count == 0);

	memdelete(fallback);
	memdelete(restored);
	memdelete(user_routed);
	memdelete(direct);
}

TEST_CASE("[Editor][ScenePaneTileMode] invalid modes leave durable state unchanged") {
	EditorData editor_data;
	EditorSelection selection;

	ScenePaneTile *direct = memnew(ScenePaneTile);
	direct->setup(17, &selection, editor_data);
	requested_count = 0;
	direct->connect(SNAME("scene_editor_mode_requested"), callable_mp_static(&record_requested_mode));
	direct->set_scene_editor_mode(SceneEditorMode(99), true);
	CHECK_FALSE(direct->is_scene_editor_mode_initialized());
	CHECK(direct->get_scene_editor_mode() == SceneEditorMode::MODE_2D);
	CHECK(requested_count == 0);

	Ref<ConfigFile> uninitialized_layout;
	uninitialized_layout.instantiate();
	direct->save_layout(uninitialized_layout, "Tile");
	CHECK_FALSE(uninitialized_layout->has_section_key("Tile", "scene_editor_mode"));

	direct->set_scene_editor_mode(SceneEditorMode::MODE_3D, false);
	CHECK(direct->is_scene_editor_mode_initialized());
	CHECK(direct->get_scene_editor_mode() == SceneEditorMode::MODE_3D);
	requested_count = 0;
	direct->set_scene_editor_mode(SceneEditorMode(-1), true);
	CHECK(direct->get_scene_editor_mode() == SceneEditorMode::MODE_3D);
	CHECK(requested_count == 0);

	Ref<ConfigFile> initialized_layout;
	initialized_layout.instantiate();
	direct->save_layout(initialized_layout, "Tile");
	CHECK(StringName(initialized_layout->get_value("Tile", "scene_editor_mode")) == StringName("3d"));

	ScenePaneTile *signal_tile = memnew(ScenePaneTile);
	signal_tile->setup(18, &selection, editor_data);
	requested_count = 0;
	signal_tile->connect(SNAME("scene_editor_mode_requested"), callable_mp_static(&record_requested_mode));
	signal_tile->get_scene_mode_switcher()->emit_signal(SNAME("mode_selected"), 99);
	CHECK_FALSE(signal_tile->is_scene_editor_mode_initialized());
	CHECK(signal_tile->get_scene_editor_mode() == SceneEditorMode::MODE_2D);
	CHECK(requested_count == 0);

	memdelete(signal_tile);
	memdelete(direct);
}

} // namespace TestScenePaneTileMode
