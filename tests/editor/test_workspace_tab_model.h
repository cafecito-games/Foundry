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

#include "tests/editor/test_scene_workspace.h"
#include "tests/test_macros.h"
#include "tests/test_tools.h"

namespace TestWorkspaceTabModel {

// A workspace tab type with no scene or script identity, used by the extension
// guardrails (issue #1030) to prove the generic pane mechanics -- add, move,
// edge-split, close, collapse, and persistence -- drive a brand new type through
// the registry alone, with zero scene/script branching. It owns no live surface:
// mount/unmount/activate are no-ops and its payload round-trips verbatim.
class FakeTabType : public WorkspaceTabType {
	StringName type_id_value;

public:
	explicit FakeTabType(const StringName &p_type_id = StringName("fake_guardrail")) :
			type_id_value(p_type_id) {}

	StringName type_id() const override { return type_id_value; }
	bool can_open(const String &p_resource) const override { return true; }
	WorkspaceTab make_tab(const String &p_resource, int p_stable_id) const override {
		WorkspaceTab tab;
		tab.set_stable_id(p_stable_id);
		tab.set_type_id(type_id_value);
		tab.set_resource_key(p_resource);
		tab.set_title_cache(p_resource.get_file());
		tab.set_icon_key_cache("Object");
		return tab;
	}
	String get_title(const WorkspaceTab &p_tab) const override { return p_tab.get_title_cache(); }
	Ref<Texture2D> get_icon(const WorkspaceTab &p_tab) const override { return Ref<Texture2D>(); }
	void mount(WorkspaceTab &p_tab, Control *p_chrome_host) override {}
	void unmount(WorkspaceTab &p_tab) override {}
	void activate(WorkspaceTab &p_tab) override {}
	WorkspaceTabCloseResult request_close(WorkspaceTab &p_tab, const Callable &p_on_deferred_close = Callable()) override {
		return WorkspaceTabCloseResult::CLOSE;
	}
	Dictionary save_payload(const WorkspaceTab &p_tab) const override { return p_tab.get_payload(); }
	void restore_payload(WorkspaceTab &p_tab, const Dictionary &p_payload) const override { p_tab.set_payload(p_payload); }
};

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
	WorkspaceTabType *script_type = registry.find_type(StringName("script"));
	REQUIRE(script_type != nullptr);

	WorkspaceTab original = script_type->make_tab("res://roundtrip.fs", 42);
	original.set_title_cache("Roundtrip Script");
	original.set_icon_key_cache("EditorScript");
	Dictionary payload;
	payload["caret_line"] = 7;
	payload["fold_state"] = "collapsed";
	original.set_payload(payload);

	Ref<ConfigFile> config;
	config.instantiate();
	const String section = "Tab_42";
	original.save_to_config(config, section, script_type);

	WorkspaceTab restored;
	restored.load_from_config(config, section, script_type);
	CHECK(restored == original);
}

// Guardrails (issue #1030): a registered type with no scene/script identity must
// drive the generic pane mechanics end to end. These reuse the full workspace
// harness from TestSceneWorkspace so the fake type flows through the real drag,
// split, close, collapse, and persistence code paths -- not a stub.

TEST_CASE("[workspace-tab][SceneTree][Editor] fake-tab-type-move-split-close") {
	using namespace TestSceneWorkspace;
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	// Static so the pointer handed to the shared registry stays valid for the
	// whole process; the registry keeps raw pointers and never unregisters.
	static FakeTabType fake_type;
	WorkspaceTabRegistry &registry = WorkspacePane::get_shared_tab_registry();
	registry.register_type(&fake_type);

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	WorkspacePane *pane_a = get_leaf_pane(leaf_a);
	REQUIRE(pane_a != nullptr);

	// Two fake tabs so moving one out does not empty (and collapse) the source.
	pane_a->add_tab(fake_type.make_tab("fake://keep", registry.allocate_stable_id()));
	pane_a->add_tab(fake_type.make_tab("fake://move", registry.allocate_stable_id()));
	REQUIRE(pane_a->get_tab_count() == 2);

	// Edge-drop splits the pane and moves the fake tab into the new pane through
	// the same generic mechanics scene/script tabs use -- no type_id branching.
	WorkspaceLeafNode *dest = h.workspace->handle_tab_drop(leaf_a->get_leaf_id(), 1, leaf_a, EditorSceneWorkspace::DROP_RIGHT);
	h.pump();
	REQUIRE(dest != nullptr);
	CHECK(dest != leaf_a);
	CHECK(h.workspace->get_leaf_count() == 2);

	WorkspacePane *dest_pane = get_leaf_pane(dest);
	REQUIRE(dest_pane != nullptr);
	CHECK(dest_pane->get_tab_count() == 1);
	CHECK(dest_pane->get_tab(0).get_type_id() == StringName("fake_guardrail"));
	CHECK(dest_pane->get_tab(0).get_resource_key() == "fake://move");
	CHECK(pane_a->get_tab_count() == 1);
	CHECK(pane_a->get_tab(0).get_resource_key() == "fake://keep");

	// Center-drop the fake tab back; emptying the split pane collapses it, proving
	// the collapse path is type-agnostic too.
	WorkspaceLeafNode *back = h.workspace->handle_tab_drop(dest->get_leaf_id(), 0, leaf_a, EditorSceneWorkspace::DROP_CENTER);
	h.pump();
	CHECK(back == leaf_a);
	CHECK(h.workspace->get_leaf_count() == 1);
	REQUIRE(pane_a->get_tab_count() == 2);

	// Closing a fake tab routes through request_close -> CLOSE and removes it.
	const WorkspaceTabCloseResult result = pane_a->request_close_tab(pane_a->get_tab_count() - 1);
	CHECK(result == WorkspaceTabCloseResult::CLOSE);
	CHECK(pane_a->get_tab_count() == 1);
	CHECK(pane_a->get_tab(0).get_resource_key() == "fake://keep");

	h.unmount();
}

TEST_CASE("[workspace-tab][SceneTree][Editor] fake-tab-type-persist-roundtrip") {
	using namespace TestSceneWorkspace;
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	static FakeTabType fake_type;
	WorkspaceTabRegistry &registry = WorkspacePane::get_shared_tab_registry();
	registry.register_type(&fake_type);

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspacePane *pane = get_leaf_pane(h.workspace->get_focused_leaf());
	REQUIRE(pane != nullptr);

	WorkspaceTab fake_tab = fake_type.make_tab("fake://persisted", registry.allocate_stable_id());
	Dictionary payload;
	payload["cursor"] = 99;
	payload["note"] = "guardrail";
	fake_tab.set_payload(payload);
	pane->add_tab(fake_tab);
	REQUIRE(pane->get_tab_count() == 1);

	const int leaf_id = h.workspace->get_focused_leaf_id();

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);

	h.unmount();

	WorkspaceHarness h2;
	h2.mount();
	h2.workspace->restore_from_config(config);
	h2.pump();

	WorkspacePane *restored = get_leaf_pane(h2.workspace->get_leaf_by_id(leaf_id));
	REQUIRE(restored != nullptr);
	REQUIRE(restored->get_tab_count() == 1);
	const WorkspaceTab &restored_tab = restored->get_tab(0);
	CHECK(restored_tab.get_type_id() == StringName("fake_guardrail"));
	CHECK(restored_tab.get_resource_key() == "fake://persisted");
	const Dictionary &restored_payload = restored_tab.get_payload();
	REQUIRE(restored_payload.has("cursor"));
	CHECK(int(restored_payload["cursor"]) == 99);
	CHECK(String(restored_payload["note"]) == "guardrail");

	h2.unmount();
}

TEST_CASE("[workspace-tab][SceneTree][Editor] registry-required-for-new-type") {
	using namespace TestSceneWorkspace;
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspacePane *pane = get_leaf_pane(h.workspace->get_focused_leaf());
	REQUIRE(pane != nullptr);

	// The tab's type is known only to a private save-side registry, so the shared
	// registry used on restore has never seen it. A new type that skips
	// registration cannot round-trip: it is dropped with a diagnostic. The id is
	// distinct from the other guardrails' type so the shared registry cannot
	// resolve it via a leftover registration.
	WorkspaceTabRegistry save_registry;
	FakeTabType unregistered_type(StringName("unregistered_guardrail"));
	save_registry.register_type(&unregistered_type);
	pane->set_tab_registry(&save_registry);

	WorkspaceTabType *scene_type = save_registry.find_type(StringName("scene"));
	REQUIRE(scene_type != nullptr);
	pane->add_tab(scene_type->make_tab("res://keep.tscn", save_registry.allocate_stable_id()));
	pane->add_tab(unregistered_type.make_tab("fake://unregistered", save_registry.allocate_stable_id()));
	REQUIRE(pane->get_tab_count() == 2);

	const int leaf_id = h.workspace->get_focused_leaf_id();

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);

	h.unmount();

	ErrorDetector error_detector;
	WorkspaceHarness h2;
	h2.mount();
	h2.workspace->restore_from_config(config);
	h2.pump();

	// The unregistered tab is skipped with a diagnostic; the registered scene tab
	// restores cleanly.
	CHECK(error_detector.has_error);

	WorkspacePane *restored = get_leaf_pane(h2.workspace->get_leaf_by_id(leaf_id));
	REQUIRE(restored != nullptr);
	CHECK(restored->get_tab_count() == 1);
	CHECK(restored->get_tab(0).get_type_id() == StringName("scene"));
	CHECK(restored->get_tab(0).get_resource_key() == "res://keep.tscn");

	h2.unmount();
}

} // namespace TestWorkspaceTabModel
