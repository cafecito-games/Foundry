/**************************************************************************/
/*  test_workspace_tab_model.h                                            */
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

#include "core/io/config_file.h"

#include "editor/workspace/workspace_tab_registry.h"
#include "editor/workspace/workspace_tab_type.h"

#include "tests/test_macros.h"

namespace TestWorkspaceTabModel {

TEST_CASE("[workspace-tab] tab-identity-unique") {
	WorkspaceTabRegistry registry;
	registry.reset_stable_id_counter();

	const int first_id = registry.allocate_stable_id();
	const int second_id = registry.allocate_stable_id();
	CHECK(first_id != second_id);

	WorkspaceTabType *scene_type = registry.find_type(StringName("scene"));
	REQUIRE(scene_type != nullptr);

	WorkspaceTab unsaved_a = scene_type->make_tab("unsaved:generated", first_id);
	WorkspaceTab unsaved_b = scene_type->make_tab("unsaved:generated", second_id);
	CHECK(unsaved_a.get_resource_key() == unsaved_b.get_resource_key());
	CHECK(unsaved_a.get_stable_id() != unsaved_b.get_stable_id());
}

TEST_CASE("[workspace-tab] registry-lookup-by-type") {
	WorkspaceTabRegistry registry;

	WorkspaceTabType *scene_type = registry.find_type(StringName("scene"));
	WorkspaceTabType *script_type = registry.find_type(StringName("script"));
	REQUIRE(scene_type != nullptr);
	REQUIRE(script_type != nullptr);
	CHECK(scene_type->type_id() == StringName("scene"));
	CHECK(script_type->type_id() == StringName("script"));
	CHECK(registry.find_type(StringName("unknown_tab_type")) == nullptr);
}

TEST_CASE("[workspace-tab] canonical-resource-dedup") {
	WorkspaceTabRegistry registry;
	registry.reset_stable_id_counter();
	registry.clear_canonical_index();

	WorkspaceTabType *scene_type = registry.find_type(StringName("scene"));
	WorkspaceTabType *script_type = registry.find_type(StringName("script"));
	REQUIRE(scene_type != nullptr);
	REQUIRE(script_type != nullptr);

	const String scene_path = "res://a.tscn";
	const String script_path = "res://player.fs";

	WorkspaceTab scene_tab = scene_type->make_tab(scene_path, registry.allocate_stable_id());
	WorkspaceTabLocation scene_location;
	scene_location.pane_id = 0;
	scene_location.tab_index = 0;

	CHECK(registry.insert_canonical(scene_tab, scene_location) == WorkspaceTabInsertResult::INSERTED);

	WorkspaceTab duplicate_scene_tab = scene_type->make_tab(scene_path, registry.allocate_stable_id());
	WorkspaceTabLocation duplicate_scene_location;
	duplicate_scene_location.pane_id = 1;
	duplicate_scene_location.tab_index = 0;

	WorkspaceTab existing_scene_tab;
	WorkspaceTabLocation existing_scene_location;
	CHECK(registry.insert_canonical(duplicate_scene_tab, duplicate_scene_location, &existing_scene_tab, &existing_scene_location) == WorkspaceTabInsertResult::REVEALED_EXISTING);
	CHECK(existing_scene_tab == scene_tab);
	CHECK(existing_scene_location == scene_location);

	WorkspaceTab script_tab = script_type->make_tab(script_path, registry.allocate_stable_id());
	WorkspaceTabLocation script_location;
	script_location.pane_id = 0;
	script_location.tab_index = 1;

	CHECK(registry.insert_canonical(script_tab, script_location) == WorkspaceTabInsertResult::INSERTED);

	WorkspaceTab duplicate_script_tab = script_type->make_tab(script_path, registry.allocate_stable_id());
	WorkspaceTab existing_script_tab;
	CHECK(registry.insert_canonical(duplicate_script_tab, script_location, &existing_script_tab) == WorkspaceTabInsertResult::REVEALED_EXISTING);
	CHECK(existing_script_tab == script_tab);
}

TEST_CASE("[workspace-tab] canonical-resource-distinct-per-type") {
	WorkspaceTabRegistry registry;
	registry.reset_stable_id_counter();
	registry.clear_canonical_index();

	WorkspaceTabType *scene_type = registry.find_type(StringName("scene"));
	WorkspaceTabType *script_type = registry.find_type(StringName("script"));
	REQUIRE(scene_type != nullptr);
	REQUIRE(script_type != nullptr);

	const String shared_key = "res://shared/path";

	WorkspaceTab scene_tab = scene_type->make_tab(shared_key, registry.allocate_stable_id());
	WorkspaceTab script_tab = script_type->make_tab(shared_key, registry.allocate_stable_id());

	WorkspaceTabLocation scene_location;
	scene_location.pane_id = 0;
	scene_location.tab_index = 0;
	WorkspaceTabLocation script_location;
	script_location.pane_id = 1;
	script_location.tab_index = 0;

	CHECK(registry.insert_canonical(scene_tab, scene_location) == WorkspaceTabInsertResult::INSERTED);
	CHECK(registry.insert_canonical(script_tab, script_location) == WorkspaceTabInsertResult::INSERTED);

	WorkspaceTab found_scene_tab;
	WorkspaceTabLocation found_scene_location;
	WorkspaceTab found_script_tab;
	WorkspaceTabLocation found_script_location;
	CHECK(registry.find_canonical(scene_type->type_id(), shared_key, found_scene_tab, found_scene_location));
	CHECK(registry.find_canonical(script_type->type_id(), shared_key, found_script_tab, found_script_location));
	CHECK(found_scene_tab.get_stable_id() != found_script_tab.get_stable_id());
	CHECK(found_scene_location == scene_location);
	CHECK(found_script_location == script_location);
}

TEST_CASE("[workspace-tab] tab-record-roundtrip") {
	WorkspaceTabRegistry registry;
	WorkspaceTabType *scene_type = registry.find_type(StringName("scene"));
	REQUIRE(scene_type != nullptr);

	WorkspaceTab original = scene_type->make_tab("res://roundtrip.tscn", 42);
	original.set_title_cache("Roundtrip Scene");
	original.set_icon_key_cache("EditorScene");
	Dictionary payload;
	payload["caret_line"] = 7;
	payload["fold_state"] = "collapsed";
	original.set_payload(payload);

	Ref<ConfigFile> config;
	config.instantiate();
	const String section = "Tab_42";
	original.save_to_config(config, section, scene_type);

	WorkspaceTab restored;
	restored.load_from_config(config, section, scene_type);
	CHECK(restored == original);
}

} // namespace TestWorkspaceTabModel
