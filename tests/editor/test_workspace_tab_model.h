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

#include "editor/workspace/help_tab.h"
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

// A HelpTabType whose mount is a no-op so the workspace open/reveal/persist logic
// can be driven headless. The real HelpTabType mounts a live EditorHelp, which on
// enter-tree assumes a full editor (ScriptEditor singleton, editor theme); those
// are absent in the doctest harness. Identity, title, dedup key, payload, and
// is_resource_available are inherited unchanged, so only the surface creation is
// stubbed. Registered under "help" so EditorSceneWorkspace::open_help_tab resolves
// it in place of the real type for these cases.
class HeadlessHelpTabType : public HelpTabType {
public:
	// A page whose class matches this key is reported unavailable, so a restore-time
	// drop can be exercised without a generated doc database (the real
	// is_resource_available keeps every page while the database is still loading).
	String unavailable_class;

	// Stable ids of tabs a refresh was requested for. The real refresh_docs re-renders
	// a live EditorHelp; there is none headless, so recording the request lets a test
	// assert an open page is asked to refresh without a generated doc database.
	Vector<int> refreshed_stable_ids;

	void mount(WorkspaceTab &p_tab, Control *p_chrome_host) override {}
	void unmount(WorkspaceTab &p_tab) override {}
	void activate(WorkspaceTab &p_tab) override {}
	void refresh_docs(const WorkspaceTab &p_tab) override {
		refreshed_stable_ids.push_back(p_tab.get_stable_id());
	}
	bool is_resource_available(const WorkspaceTab &p_tab) const override {
		if (!unavailable_class.is_empty() && p_tab.get_resource_key() == unavailable_class) {
			return false;
		}
		return HelpTabType::is_resource_available(p_tab);
	}
};

// The single headless help type registered under "help"; tests reach it to set
// which page should report unavailable for a restore-drop check.
static HeadlessHelpTabType *headless_help_type_singleton = nullptr;

// Install the headless help type into the shared registry for a workspace test.
// Static so the pointer handed to the registry stays valid for the process.
static void install_headless_help_type() {
	static HeadlessHelpTabType headless_help_type;
	headless_help_type.unavailable_class = String();
	headless_help_type.refreshed_stable_ids.clear();
	headless_help_type_singleton = &headless_help_type;
	WorkspacePane::get_shared_tab_registry().register_type(&headless_help_type);
}

// HelpTab (issue #1054): the first real workspace tab type beyond scene/script.
// Identity, title, dedup, and payload are asserted at the model level (no doc DB
// or live EditorHelp), matching the fast [workspace-tab] cases above.

TEST_CASE("[workspace-tab] help-tab-title-format") {
	WorkspaceTabRegistry registry;
	WorkspaceTabType *help_type = registry.find_type(StringName("help"));
	REQUIRE(help_type != nullptr);
	CHECK(help_type->type_id() == StringName("help"));

	WorkspaceTab tab = help_type->make_tab("Node2D", registry.allocate_stable_id());
	CHECK(tab.get_resource_key() == "Node2D");
	CHECK(help_type->get_title(tab) == "Help: Node2D");
}

TEST_CASE("[workspace-tab] help-tab-close-always") {
	WorkspaceTabRegistry registry;
	WorkspaceTabType *help_type = registry.find_type(StringName("help"));
	REQUIRE(help_type != nullptr);

	WorkspaceTab tab = help_type->make_tab("Node2D", registry.allocate_stable_id());
	CHECK(help_type->request_close(tab) == WorkspaceTabCloseResult::CLOSE);
}

TEST_CASE("[workspace-tab] help-tab-topic-class-key") {
	// A bare class name is its own key; a deep topic parses down to its class so
	// the dedup key is the class, not the full anchor.
	CHECK(HelpTabType::class_key_for_topic("Node2D") == "Node2D");
	CHECK(HelpTabType::class_key_for_topic("class:Node2D") == "Node2D");
	CHECK(HelpTabType::class_key_for_topic("class_name:Node2D") == "Node2D");
	CHECK(HelpTabType::class_key_for_topic("class_method:Node2D:queue_free") == "Node2D");
	CHECK(HelpTabType::class_key_for_topic("class_signal:Node2D:renamed") == "Node2D");
	CHECK(HelpTabType::class_key_for_topic("").is_empty());
	// A built-in script class name keeps its '::' so nested pages do not collide
	// under the wrong key.
	CHECK(HelpTabType::class_key_for_topic("Outer::Inner") == "Outer::Inner");
	CHECK(HelpTabType::class_key_for_topic("class_method:Outer::Inner:foo") == "Outer::Inner");
}

TEST_CASE("[workspace-tab] help-tab-availability-without-docs") {
	WorkspaceTabRegistry registry;
	WorkspaceTabType *help_type = registry.find_type(StringName("help"));
	REQUIRE(help_type != nullptr);

	// While the doc database is unavailable (null/empty, as in this harness) a page
	// cannot be verified, so it is kept rather than dropping a valid layout; an
	// empty key is never a valid page.
	CHECK(help_type->is_resource_available(help_type->make_tab("@GlobalScope", 1)));
	WorkspaceTab blank;
	blank.set_stable_id(2);
	blank.set_type_id(StringName("help"));
	CHECK_FALSE(help_type->is_resource_available(blank));
}

TEST_CASE("[workspace-tab] help-tab-open-reveal") {
	WorkspaceTabRegistry registry;
	registry.reset_stable_id_counter();
	registry.clear_canonical_index();

	WorkspaceTabType *help_type = registry.find_type(StringName("help"));
	REQUIRE(help_type != nullptr);

	WorkspaceTab node_tab = help_type->make_tab("Node2D", registry.allocate_stable_id());
	WorkspaceTabLocation node_location;
	node_location.pane_id = 0;
	node_location.tab_index = 3;
	CHECK(registry.insert_canonical(node_tab, node_location) == WorkspaceTabInsertResult::INSERTED);

	// Opening the same class again reveals the existing tab: same location and
	// stable id, no second record.
	WorkspaceTab duplicate_tab = help_type->make_tab("Node2D", registry.allocate_stable_id());
	WorkspaceTab existing_tab;
	WorkspaceTabLocation existing_location;
	CHECK(registry.insert_canonical(duplicate_tab, node_location, &existing_tab, &existing_location) == WorkspaceTabInsertResult::REVEALED_EXISTING);
	CHECK(existing_tab.get_stable_id() == node_tab.get_stable_id());
	CHECK(existing_location == node_location);
}

TEST_CASE("[workspace-tab] help-tab-distinct-classes") {
	WorkspaceTabRegistry registry;
	registry.reset_stable_id_counter();
	registry.clear_canonical_index();

	WorkspaceTabType *help_type = registry.find_type(StringName("help"));
	REQUIRE(help_type != nullptr);

	WorkspaceTab node_tab = help_type->make_tab("Node2D", registry.allocate_stable_id());
	WorkspaceTab sprite_tab = help_type->make_tab("Sprite2D", registry.allocate_stable_id());
	WorkspaceTabLocation node_location;
	node_location.pane_id = 0;
	node_location.tab_index = 0;
	WorkspaceTabLocation sprite_location;
	sprite_location.pane_id = 0;
	sprite_location.tab_index = 1;

	CHECK(registry.insert_canonical(node_tab, node_location) == WorkspaceTabInsertResult::INSERTED);
	CHECK(registry.insert_canonical(sprite_tab, sprite_location) == WorkspaceTabInsertResult::INSERTED);

	WorkspaceTab found_node;
	WorkspaceTabLocation found_node_location;
	WorkspaceTab found_sprite;
	WorkspaceTabLocation found_sprite_location;
	CHECK(registry.find_canonical(StringName("help"), "Node2D", found_node, found_node_location));
	CHECK(registry.find_canonical(StringName("help"), "Sprite2D", found_sprite, found_sprite_location));
	CHECK(found_node.get_stable_id() != found_sprite.get_stable_id());
}

TEST_CASE("[workspace-tab] help-tab-payload-roundtrip") {
	WorkspaceTabRegistry registry;
	WorkspaceTabType *help_type = registry.find_type(StringName("help"));
	REQUIRE(help_type != nullptr);

	WorkspaceTab original = help_type->make_tab("Node2D", 7);
	Dictionary payload;
	payload["help_class"] = "Node2D";
	payload["scroll"] = 128;
	original.set_payload(payload);

	Ref<ConfigFile> config;
	config.instantiate();
	const String section = "Tab_7";
	original.save_to_config(config, section, help_type);

	WorkspaceTab restored;
	restored.load_from_config(config, section, help_type);
	CHECK(restored.get_resource_key() == "Node2D");
	REQUIRE(restored.get_payload().has("scroll"));
	CHECK(int(restored.get_payload()["scroll"]) == 128);
}

TEST_CASE("[workspace-tab][SceneTree][Editor] help-tab-stale-canonical-creates-fresh") {
	using namespace TestSceneWorkspace;
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();
	install_headless_help_type();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *source = h.workspace->get_focused_leaf();
	REQUIRE(source != nullptr);
	WorkspaceTabRegistry &registry = WorkspacePane::get_shared_tab_registry();
	WorkspaceTabType *help_type = registry.find_type(StringName("help"));
	REQUIRE(help_type != nullptr);

	// Poison the canonical index with an entry that points at a tab index the pane
	// does not have (as a restore that reused pane ids would leave behind). Opening
	// the page must not trust it: it creates a real tab whose location resolves.
	WorkspaceTabLocation stale_location;
	stale_location.pane_id = source->get_leaf_id();
	stale_location.tab_index = 999;
	registry.set_canonical(help_type->make_tab("PoisonClass", 42), stale_location);

	WorkspaceLeafNode *leaf = h.workspace->open_help_tab(source, "PoisonClass");
	h.pump();
	REQUIRE(leaf != nullptr);

	WorkspaceTab resolved_tab;
	WorkspaceTabLocation resolved_location;
	REQUIRE(registry.find_canonical(StringName("help"), "PoisonClass", resolved_tab, resolved_location));
	WorkspacePane *resolved_pane = get_leaf_pane(h.workspace->get_leaf_by_id(resolved_location.pane_id));
	REQUIRE(resolved_pane != nullptr);
	REQUIRE(resolved_location.tab_index < resolved_pane->get_tab_count());
	CHECK(resolved_pane->get_tab(resolved_location.tab_index).get_type_id() == StringName("help"));
	CHECK(resolved_pane->get_tab(resolved_location.tab_index).get_resource_key() == "PoisonClass");

	// A second open now reveals the real tab rather than stacking a duplicate.
	int help_tab_count = 0;
	for (int i = 0; i < resolved_pane->get_tab_count(); i++) {
		if (resolved_pane->get_tab(i).get_type_id() == StringName("help") && resolved_pane->get_tab(i).get_resource_key() == "PoisonClass") {
			help_tab_count++;
		}
	}
	CHECK(help_tab_count == 1);
	h.workspace->open_help_tab(source, "PoisonClass");
	h.pump();
	int help_tab_count_after = 0;
	for (int i = 0; i < resolved_pane->get_tab_count(); i++) {
		if (resolved_pane->get_tab(i).get_type_id() == StringName("help") && resolved_pane->get_tab(i).get_resource_key() == "PoisonClass") {
			help_tab_count_after++;
		}
	}
	CHECK(help_tab_count_after == 1);

	h.unmount();
}

TEST_CASE("[workspace-tab][SceneTree][Editor] help-tab-open-reveal-workspace") {
	using namespace TestSceneWorkspace;
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();
	install_headless_help_type();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *source = h.workspace->get_focused_leaf();
	REQUIRE(source != nullptr);

	WorkspaceLeafNode *first = h.workspace->open_help_tab(source, "Node2D");
	h.pump();
	REQUIRE(first != nullptr);
	WorkspacePane *pane = get_leaf_pane(first);
	REQUIRE(pane != nullptr);

	int help_tab_count = 0;
	int node_stable_id = -1;
	for (int i = 0; i < pane->get_tab_count(); i++) {
		if (pane->get_tab(i).get_type_id() == StringName("help")) {
			help_tab_count++;
			node_stable_id = pane->get_tab(i).get_stable_id();
		}
	}
	CHECK(help_tab_count == 1);

	// Requesting the same class again reveals the existing tab instead of adding a
	// duplicate: the help-tab count and its stable id are unchanged.
	WorkspaceLeafNode *revealed = h.workspace->open_help_tab(source, "Node2D");
	h.pump();
	CHECK(revealed == first);
	int help_tab_count_after = 0;
	for (int i = 0; i < pane->get_tab_count(); i++) {
		if (pane->get_tab(i).get_type_id() == StringName("help")) {
			help_tab_count_after++;
			CHECK(pane->get_tab(i).get_stable_id() == node_stable_id);
		}
	}
	CHECK(help_tab_count_after == 1);

	h.unmount();
}

TEST_CASE("[workspace-tab][SceneTree][Editor] help-tab-distinct-classes-workspace") {
	using namespace TestSceneWorkspace;
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();
	install_headless_help_type();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *source = h.workspace->get_focused_leaf();
	REQUIRE(source != nullptr);

	h.workspace->open_help_tab(source, "Node2D");
	h.pump();
	h.workspace->open_help_tab(source, "Sprite2D");
	h.pump();

	WorkspaceTabRegistry &registry = WorkspacePane::get_shared_tab_registry();
	WorkspaceTab node_tab;
	WorkspaceTabLocation node_location;
	WorkspaceTab sprite_tab;
	WorkspaceTabLocation sprite_location;
	REQUIRE(registry.find_canonical(StringName("help"), "Node2D", node_tab, node_location));
	REQUIRE(registry.find_canonical(StringName("help"), "Sprite2D", sprite_tab, sprite_location));
	CHECK(node_tab.get_stable_id() != sprite_tab.get_stable_id());

	// Both help tabs stack in one pane; one can be moved to a split pane through
	// the generic drop path with no help-specific branching.
	WorkspaceLeafNode *host_leaf = h.workspace->get_leaf_by_id(node_location.pane_id);
	REQUIRE(host_leaf != nullptr);
	WorkspacePane *host_pane = get_leaf_pane(host_leaf);
	REQUIRE(host_pane != nullptr);

	int sprite_index = -1;
	for (int i = 0; i < host_pane->get_tab_count(); i++) {
		if (host_pane->get_tab(i).get_type_id() == StringName("help") && host_pane->get_tab(i).get_resource_key() == "Sprite2D") {
			sprite_index = i;
			break;
		}
	}
	REQUIRE(sprite_index >= 0);

	const int leaf_count_before = h.workspace->get_leaf_count();
	WorkspaceLeafNode *dest = h.workspace->handle_tab_drop(host_leaf->get_leaf_id(), sprite_index, host_leaf, EditorSceneWorkspace::DROP_RIGHT);
	h.pump();
	REQUIRE(dest != nullptr);
	CHECK(dest != host_leaf);
	CHECK(h.workspace->get_leaf_count() == leaf_count_before + 1);
	WorkspacePane *dest_pane = get_leaf_pane(dest);
	REQUIRE(dest_pane != nullptr);
	CHECK(dest_pane->get_tab(0).get_type_id() == StringName("help"));
	CHECK(dest_pane->get_tab(0).get_resource_key() == "Sprite2D");

	h.unmount();
}

TEST_CASE("[workspace-tab][SceneTree][Editor] help-tab-deep-topic-reveals-class") {
	using namespace TestSceneWorkspace;
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();
	install_headless_help_type();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *source = h.workspace->get_focused_leaf();
	REQUIRE(source != nullptr);

	// A deep topic opens the class tab; requesting the bare class then reveals the
	// same tab because the dedup key is the class, not the full topic.
	WorkspaceLeafNode *first = h.workspace->open_help_tab(source, "class_method:Node2D:queue_free");
	h.pump();
	REQUIRE(first != nullptr);
	WorkspacePane *pane = get_leaf_pane(first);
	REQUIRE(pane != nullptr);

	WorkspaceLeafNode *revealed = h.workspace->open_help_tab(source, "Node2D");
	h.pump();
	CHECK(revealed == first);

	int help_tab_count = 0;
	for (int i = 0; i < pane->get_tab_count(); i++) {
		if (pane->get_tab(i).get_type_id() == StringName("help")) {
			help_tab_count++;
		}
	}
	CHECK(help_tab_count == 1);

	h.unmount();
}

TEST_CASE("[workspace-tab][SceneTree][Editor] help-tab-refresh-open-page") {
	using namespace TestSceneWorkspace;
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();
	install_headless_help_type();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *source = h.workspace->get_focused_leaf();
	REQUIRE(source != nullptr);

	WorkspaceLeafNode *leaf = h.workspace->open_help_tab(source, "Node2D");
	h.pump();
	REQUIRE(leaf != nullptr);

	WorkspacePane *pane = get_leaf_pane(leaf);
	REQUIRE(pane != nullptr);
	int node_stable_id = -1;
	for (int i = 0; i < pane->get_tab_count(); i++) {
		if (pane->get_tab(i).get_type_id() == StringName("help") && pane->get_tab(i).get_resource_key() == "Node2D") {
			node_stable_id = pane->get_tab(i).get_stable_id();
		}
	}
	REQUIRE(node_stable_id >= 0);

	// A doc update for an open class asks exactly that page to refresh, identified by
	// its stable id (the affordance the doc-change notifications now call).
	headless_help_type_singleton->refreshed_stable_ids.clear();
	h.workspace->refresh_help_tab("Node2D");
	REQUIRE(headless_help_type_singleton->refreshed_stable_ids.size() == 1);
	CHECK(headless_help_type_singleton->refreshed_stable_ids[0] == node_stable_id);

	// A class with no open page is a silent no-op, so unrelated doc updates do not
	// touch the workspace.
	headless_help_type_singleton->refreshed_stable_ids.clear();
	h.workspace->refresh_help_tab("Sprite2D");
	CHECK(headless_help_type_singleton->refreshed_stable_ids.is_empty());

	// An empty class key is never a page and is ignored.
	h.workspace->refresh_help_tab(String());
	CHECK(headless_help_type_singleton->refreshed_stable_ids.is_empty());

	h.unmount();
}

TEST_CASE("[workspace-tab][SceneTree][Editor] help-tab-persist-roundtrip") {
	using namespace TestSceneWorkspace;
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();
	install_headless_help_type();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspacePane *pane = get_leaf_pane(h.workspace->get_focused_leaf());
	REQUIRE(pane != nullptr);

	WorkspaceTabRegistry &registry = WorkspacePane::get_shared_tab_registry();
	WorkspaceTabType *help_type = registry.find_type(StringName("help"));
	REQUIRE(help_type != nullptr);
	WorkspaceTab help_tab = help_type->make_tab("Node2D", registry.allocate_stable_id());
	Dictionary payload;
	payload["help_class"] = "Node2D";
	payload["scroll"] = 64;
	help_tab.set_payload(payload);
	pane->add_tab(help_tab);
	h.pump();

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
	int restored_scroll = -1;
	bool found_help = false;
	for (int i = 0; i < restored->get_tab_count(); i++) {
		const WorkspaceTab &tab = restored->get_tab(i);
		if (tab.get_type_id() == StringName("help") && tab.get_resource_key() == "Node2D") {
			found_help = true;
			if (tab.get_payload().has("scroll")) {
				restored_scroll = int(tab.get_payload()["scroll"]);
			}
		}
	}
	CHECK(found_help);
	CHECK(restored_scroll == 64);

	h2.unmount();
}

TEST_CASE("[workspace-tab][SceneTree][Editor] help-tab-missing-class-dropped") {
	using namespace TestSceneWorkspace;
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();
	install_headless_help_type();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspacePane *pane = get_leaf_pane(h.workspace->get_focused_leaf());
	REQUIRE(pane != nullptr);

	WorkspaceTabRegistry &registry = WorkspacePane::get_shared_tab_registry();
	WorkspaceTabType *help_type = registry.find_type(StringName("help"));
	REQUIRE(help_type != nullptr);
	// Report this class unavailable so the restore path drops it. (The real
	// is_resource_available consults the doc database, which cannot be generated in
	// this harness; here the headless type stands in for a class that no longer
	// exists so the workspace drop wiring is still exercised.)
	REQUIRE(headless_help_type_singleton != nullptr);
	headless_help_type_singleton->unavailable_class = "ZZZ_NotARealClass_Foundry";
	pane->add_tab(help_type->make_tab("ZZZ_NotARealClass_Foundry", registry.allocate_stable_id()));
	h.pump();

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

	// The page for a class that no longer exists is dropped with a diagnostic.
	CHECK(error_detector.has_error);

	WorkspacePane *restored = get_leaf_pane(h2.workspace->get_leaf_by_id(leaf_id));
	REQUIRE(restored != nullptr);
	for (int i = 0; i < restored->get_tab_count(); i++) {
		CHECK(restored->get_tab(i).get_resource_key() != "ZZZ_NotARealClass_Foundry");
	}

	h2.unmount();
}

} // namespace TestWorkspaceTabModel
