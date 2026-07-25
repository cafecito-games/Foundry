/**************************************************************************/
/*  test_name_mangler_binding_safety.h                                   */
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

#ifdef TOOLS_ENABLED

#include "modules/foundry_script/fs_cache.h"
#include "modules/foundry_script/fs_name_mangler_application.h"
#include "modules/foundry_script/fs_name_mangler_binding_safety.h"
#include "modules/foundry_script/tests/fs_temporary_project_tree.h"
#include "modules/foundry_script/tests/test_bytecode_serialization.h"

#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/script_language.h"
#include "scene/animation/animation_player.h"
#include "scene/resources/animation.h"
#include "scene/resources/animation_library.h"
#include "scene/resources/packed_scene.h"

#include "tests/test_macros.h"

namespace FSTests {

class BindingSafetyGlobalClassGuard {
	StringName class_name;

public:
	BindingSafetyGlobalClassGuard(
			const StringName &p_class_name,
			const StringName &p_base,
			const String &p_path) :
			class_name(p_class_name) {
		ScriptServer::remove_global_class(class_name);
		ScriptServer::add_global_class(
				class_name, p_base,
				FSLanguage::get_singleton()->get_name(), p_path,
				false, false, false);
	}

	~BindingSafetyGlobalClassGuard() {
		ScriptServer::remove_global_class(class_name);
	}
};

static bool binding_safety_has_scene_reason(
		const FSNameManglerAnalysis::Result &p_result,
		const StringName &p_name,
		const String &p_detail) {
	const FSNameManglerAnalysis::Classification *classification =
			p_result.find(p_name);
	if (classification == nullptr) {
		return false;
	}
	for (const FSNameManglerAnalysis::KeepEvidence &evidence :
			classification->keep_evidence) {
		if (evidence.reason ==
						FSNameManglerAnalysis::KEEP_SCENE_OR_RESOURCE &&
				evidence.detail == p_detail) {
			return true;
		}
	}
	return false;
}

static String binding_safety_snapshot(
		const FSNameManglerBindingSafety::Result &p_result) {
	String snapshot =
			vformat("error=%d complete=%d\n", p_result.error, (int)p_result.complete);
	for (const FSNameManglerBindingSafety::Evidence &evidence :
			p_result.evidence) {
		snapshot += vformat(
				"%s|%d|%s|%s\n", evidence.name, evidence.kind,
				evidence.source, evidence.owner);
	}
	for (const FSNameManglerBindingSafety::Diagnostic &diagnostic :
			p_result.diagnostics) {
		snapshot += "diagnostic|" + diagnostic.format() + "\n";
	}
	return snapshot;
}

static bool binding_safety_has_diagnostic(
		const FSNameManglerBindingSafety::Result &p_result,
		const String &p_text) {
	for (const FSNameManglerBindingSafety::Diagnostic &diagnostic :
			p_result.diagnostics) {
		if (diagnostic.format().contains(p_text)) {
			return true;
		}
	}
	return false;
}

static bool binding_safety_has_evidence(
		const FSNameManglerBindingSafety::Result &p_result,
		const StringName &p_name,
		FSNameManglerBindingSafety::BindingKind p_kind,
		const String &p_source) {
	for (const FSNameManglerBindingSafety::Evidence &evidence :
			p_result.evidence) {
		if (evidence.name == p_name && evidence.kind == p_kind &&
				evidence.source == p_source) {
			return true;
		}
	}
	return false;
}

static bool binding_safety_has_applied_evidence(
		const FSNameManglerBindingSafety::Result &p_binding_result,
		const FSNameManglerAnalysis::Result &p_analysis_result,
		const StringName &p_name,
		FSNameManglerBindingSafety::BindingKind p_kind,
		const String &p_source) {
	for (const FSNameManglerBindingSafety::Evidence &evidence :
			p_binding_result.evidence) {
		if (evidence.name == p_name && evidence.kind == p_kind &&
				evidence.source == p_source) {
			return binding_safety_has_scene_reason(
					p_analysis_result, p_name, evidence.detail());
		}
	}
	return false;
}

static bool binding_safety_buffer_contains(
		const Vector<uint8_t> &p_buffer, const String &p_text) {
	const CharString needle = p_text.utf8();
	if (needle.length() == 0 || needle.length() > p_buffer.size()) {
		return false;
	}
	for (int offset = 0;
			offset + needle.length() <= p_buffer.size(); offset++) {
		if (memcmp(
					p_buffer.ptr() + offset, needle.get_data(),
					needle.length()) == 0) {
			return true;
		}
	}
	return false;
}

struct BindingSafetyRuntimeSnapshot {
	double stored_value = 0.0;
	double signal_value = 0.0;
	double animation_value = 0.0;
	int64_t config_value = 0;
	StringName config_class;

	bool operator==(const BindingSafetyRuntimeSnapshot &p_other) const {
		return stored_value == p_other.stored_value &&
				signal_value == p_other.signal_value &&
				animation_value == p_other.animation_value &&
				config_value == p_other.config_value &&
				config_class == p_other.config_class;
	}
};

static BindingSafetyRuntimeSnapshot binding_safety_run_runtime(
		const Ref<PackedScene> &p_scene,
		const Ref<Resource> &p_config,
		bool p_expect_compiled_binary) {
	BindingSafetyRuntimeSnapshot snapshot;
	Node *runtime_root = p_scene->instantiate();
	REQUIRE(runtime_root != nullptr);
	Node *emitter =
			runtime_root->get_node_or_null(NodePath("Emitter"));
	Node *receiver =
			runtime_root->get_node_or_null(NodePath("Receiver"));
	AnimationPlayer *player = Object::cast_to<AnimationPlayer>(
			runtime_root->get_node_or_null(NodePath("AnimationPlayer")));
	CHECK(emitter != nullptr);
	CHECK(receiver != nullptr);
	CHECK(player != nullptr);
	if (emitter == nullptr || receiver == nullptr || player == nullptr) {
		memdelete(runtime_root);
		return snapshot;
	}

	const Ref<FoundryScript> emitter_script = emitter->get_script();
	const Ref<FoundryScript> receiver_script = receiver->get_script();
	const Ref<FoundryScript> base_script =
			receiver_script.is_valid() ? Ref<FoundryScript>(receiver_script->get_base_script()) : Ref<FoundryScript>();
	const Ref<FoundryScript> config_script = p_config->get_script();
	CHECK(emitter_script.is_valid());
	CHECK(receiver_script.is_valid());
	CHECK(base_script.is_valid());
	CHECK(config_script.is_valid());
	if (emitter_script.is_null() || receiver_script.is_null() ||
			base_script.is_null() || config_script.is_null()) {
		memdelete(runtime_root);
		return snapshot;
	}
	CHECK_EQ(
			emitter_script->is_compiled_binary(),
			p_expect_compiled_binary);
	CHECK_EQ(
			receiver_script->is_compiled_binary(),
			p_expect_compiled_binary);
	CHECK_EQ(
			base_script->is_compiled_binary(),
			p_expect_compiled_binary);
	CHECK_EQ(
			config_script->is_compiled_binary(),
			p_expect_compiled_binary);

	snapshot.stored_value = receiver->get(SNAME("inherited_override"));
	CHECK_EQ(
			emitter->emit_signal(SNAME("acceptance_signal"), 5), OK);
	snapshot.signal_value = receiver->get(SNAME("inherited_override"));
	player->play(SNAME("acceptance"));
	player->advance(0.1);
	snapshot.animation_value = receiver->get(SNAME("animated_value"));
	snapshot.config_value = p_config->get(SNAME("config_value"));
	snapshot.config_class = config_script->get_global_name();

	memdelete(runtime_root);
	return snapshot;
}

TEST_CASE("[FoundryScript][NameManglerBindingSafety] Public result applies atomically") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"func scene_method() -> void:\n"
			"\tpass\n"
			"func private_control() -> void:\n"
			"\tpass\n");
	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(script);

	FSNameManglerBindingSafety::Evidence evidence;
	evidence.name = SNAME("scene_method");
	evidence.kind =
			FSNameManglerBindingSafety::BINDING_CONNECTION_METHOD;
	evidence.source = "res://main.tscn";
	evidence.owner = script->get_fully_qualified_name();

	FSNameManglerBindingSafety::Result result;
	result.evidence.push_back(evidence);
	REQUIRE_EQ(result.apply_to_input(input), OK);
	REQUIRE_EQ(input.keep_evidence.size(), 1);
	const String expected_detail = evidence.detail();
	const FSNameManglerAnalysis::Result analysis =
			FSNameManglerAnalysis::analyze(input);
	REQUIRE_EQ(analysis.error, OK);
	CHECK(binding_safety_has_scene_reason(
			analysis, SNAME("scene_method"), expected_detail));
	CHECK_FALSE(analysis.rename_map.has(SNAME("scene_method")));
	CHECK(analysis.rename_map.has(SNAME("private_control")));

	FSNameManglerBindingSafety::Result incomplete;
	incomplete.error = ERR_INVALID_DATA;
	incomplete.complete = false;
	incomplete.evidence.push_back(evidence);
	const int previous_evidence_count = input.keep_evidence.size();
	CHECK_EQ(incomplete.apply_to_input(input), ERR_INVALID_DATA);
	CHECK_EQ(input.keep_evidence.size(), previous_evidence_count);
}

TEST_CASE("[FoundryScript][NameManglerBindingSafety] SceneState keeps only owned serialized properties") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"extends Node\n"
			"@export var scene_bound: int\n"
			"var private_control: int\n");

	Ref<PackedScene> scene;
	scene.instantiate();
	const Ref<SceneState> state = scene->get_state();
	const int node_type = state->add_name(SNAME("Node"));
	const int root_name = state->add_name(SNAME("Root"));
	const int child_name = state->add_name(SNAME("Scripted"));
	const int root_node = state->add_node(-1, -1, node_type, root_name, -1, -1, 1);
	const int child = state->add_node(root_node, root_node, node_type, child_name, -1, -1, 2);
	state->add_node_property(
			child, state->add_name(SNAME("script")), state->add_value(script));
	state->add_node_property(
			child, state->add_name(SNAME("scene_bound")), state->add_value(42));
	state->add_node_property(
			child, state->add_name(SNAME("process_mode")), state->add_value(0));

	FSNameManglerAnalysis::Input analysis_input;
	analysis_input.scripts.push_back(script);
	FSNameManglerBindingSafety::Input binding_input;
	binding_input.add_resource(scene, "res://property_scene.tscn");

	const FSNameManglerBindingSafety::Result result =
			FSNameManglerBindingSafety::collect(binding_input, analysis_input);
	REQUIRE_EQ(result.error, OK);
	REQUIRE(result.complete);
	CHECK(binding_safety_has_evidence(
			result, SNAME("scene_bound"),
			FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY,
			"res://property_scene.tscn"));
	CHECK_FALSE(binding_safety_has_evidence(
			result, SNAME("private_control"),
			FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY,
			"res://property_scene.tscn"));

	REQUIRE_EQ(result.apply_to_input(analysis_input), OK);
	const FSNameManglerAnalysis::Result analysis =
			FSNameManglerAnalysis::analyze(analysis_input);
	REQUIRE_EQ(analysis.error, OK);
	CHECK_FALSE(analysis.rename_map.has(SNAME("scene_bound")));
	CHECK(analysis.rename_map.has(SNAME("private_control")));
}

TEST_CASE("[FoundryScript][NameManglerBindingSafety] SceneState composes inheritance and repeated instances") {
	const Ref<FoundryScript> base_script = compile_bytecode_test_source(
			"extends Node\n"
			"@export var base_value: int\n");
	const Ref<FoundryScript> instance_script = compile_bytecode_test_source(
			"extends Node\n"
			"@export var instance_value: int\n");
	const Ref<FoundryScript> nested_script = compile_bytecode_test_source(
			"extends Node\n"
			"@export var nested_value: int\n");

	Ref<PackedScene> base_scene;
	base_scene.instantiate();
	const Ref<SceneState> base_state = base_scene->get_state();
	const int base_node_type = base_state->add_name(SNAME("Node"));
	const int base_root = base_state->add_node(
			-1, -1, base_node_type, base_state->add_name(SNAME("Root")),
			-1, -1, 10);
	const int base_child = base_state->add_node(
			base_root, base_root, base_node_type,
			base_state->add_name(SNAME("BaseChild")), -1, -1, 11);
	base_state->add_node_property(
			base_child, base_state->add_name(SNAME("script")),
			base_state->add_value(base_script));
	base_state->add_node_property(
			base_child, base_state->add_name(SNAME("base_value")),
			base_state->add_value(1));

	Ref<PackedScene> instance_scene;
	instance_scene.instantiate();
	const Ref<SceneState> instance_state = instance_scene->get_state();
	const int instance_node_type = instance_state->add_name(SNAME("Node"));
	const int instance_root = instance_state->add_node(
			-1, -1, instance_node_type,
			instance_state->add_name(SNAME("InstanceRoot")), -1, -1, 20);
	instance_state->add_node_property(
			instance_root, instance_state->add_name(SNAME("script")),
			instance_state->add_value(instance_script));
	instance_state->add_node_property(
			instance_root, instance_state->add_name(SNAME("instance_value")),
			instance_state->add_value(2));
	const int nested_child = instance_state->add_node(
			instance_root, instance_root, instance_node_type,
			instance_state->add_name(SNAME("Nested")), -1, -1, 21);
	instance_state->add_node_property(
			nested_child, instance_state->add_name(SNAME("script")),
			instance_state->add_value(nested_script));
	instance_state->add_node_property(
			nested_child, instance_state->add_name(SNAME("nested_value")),
			instance_state->add_value(3));

	Ref<PackedScene> derived_scene;
	derived_scene.instantiate();
	const Ref<SceneState> derived_state = derived_scene->get_state();
	derived_state->set_base_scene(derived_state->add_value(base_scene));
	const int derived_root = derived_state->add_node(
			-1, -1, SceneState::TYPE_INSTANTIATED,
			derived_state->add_name(SNAME("Root")), -1, -1, 30);
	const int base_overlay = derived_state->add_node(
			derived_root, derived_root, SceneState::TYPE_INSTANTIATED,
			derived_state->add_name(SNAME("BaseChild")), -1, -1, 31);
	derived_state->add_node_property(
			base_overlay, derived_state->add_name(SNAME("base_value")),
			derived_state->add_value(4));
	derived_state->add_node(
			derived_root, derived_root, SceneState::TYPE_INSTANTIATED,
			derived_state->add_name(SNAME("InstanceA")),
			derived_state->add_value(instance_scene), -1, 32);
	derived_state->add_node(
			derived_root, derived_root, SceneState::TYPE_INSTANTIATED,
			derived_state->add_name(SNAME("InstanceB")),
			derived_state->add_value(instance_scene), -1, 33);

	FSNameManglerAnalysis::Input analysis_input;
	analysis_input.scripts.push_back(base_script);
	analysis_input.scripts.push_back(instance_script);
	analysis_input.scripts.push_back(nested_script);
	FSNameManglerBindingSafety::Input binding_input;
	binding_input.add_resource(derived_scene, "res://derived_scene.tscn");

	const FSNameManglerBindingSafety::Result result =
			FSNameManglerBindingSafety::collect(binding_input, analysis_input);
	REQUIRE_EQ(result.error, OK);
	REQUIRE(result.complete);
	CHECK(binding_safety_has_evidence(
			result, SNAME("base_value"),
			FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY,
			"res://derived_scene.tscn"));
	CHECK(binding_safety_has_evidence(
			result, SNAME("instance_value"),
			FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY,
			"res://derived_scene.tscn"));
	CHECK(binding_safety_has_evidence(
			result, SNAME("nested_value"),
			FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY,
			"res://derived_scene.tscn"));
}

TEST_CASE("[FoundryScript][NameManglerBindingSafety] Connection keeps script-owned endpoints") {
	const Ref<FoundryScript> emitter_script = compile_bytecode_test_source(
			"extends Node\n"
			"signal script_signal\n"
			"func private_emitter_control() -> void:\n"
			"\tpass\n");
	const Ref<FoundryScript> receiver_script = compile_bytecode_test_source(
			"extends Node\n"
			"func script_method() -> void:\n"
			"\tpass\n"
			"func private_receiver_control() -> void:\n"
			"\tpass\n");

	Ref<PackedScene> scene;
	scene.instantiate();
	const Ref<SceneState> state = scene->get_state();
	const int node_type = state->add_name(SNAME("Node"));
	const int root_node = state->add_node(
			-1, -1, node_type, state->add_name(SNAME("Root")),
			-1, -1, 40);
	const int emitter = state->add_node(
			root_node, root_node, node_type,
			state->add_name(SNAME("Emitter")), -1, -1, 41);
	state->add_node_property(
			emitter, state->add_name(SNAME("script")),
			state->add_value(emitter_script));
	const int receiver = state->add_node(
			root_node, root_node, node_type,
			state->add_name(SNAME("Receiver")), -1, -1, 42);
	state->add_node_property(
			receiver, state->add_name(SNAME("script")),
			state->add_value(receiver_script));
	state->add_connection(
			emitter, receiver, state->add_name(SNAME("script_signal")),
			state->add_name(SNAME("script_method")), 0, 0, {});
	state->add_connection(
			root_node, receiver, state->add_name(SNAME("ready")),
			state->add_name(SNAME("script_method")), 0, 0, {});
	state->add_connection(
			emitter, root_node, state->add_name(SNAME("script_signal")),
			state->add_name(SNAME("queue_free")), 0, 0, {});

	FSNameManglerAnalysis::Input analysis_input;
	analysis_input.scripts.push_back(emitter_script);
	analysis_input.scripts.push_back(receiver_script);
	FSNameManglerBindingSafety::Input binding_input;
	binding_input.add_resource(scene, "res://connections.tscn");

	const FSNameManglerBindingSafety::Result result =
			FSNameManglerBindingSafety::collect(binding_input, analysis_input);
	REQUIRE_EQ(result.error, OK);
	REQUIRE(result.complete);
	CHECK(binding_safety_has_evidence(
			result, SNAME("script_signal"),
			FSNameManglerBindingSafety::BINDING_CONNECTION_SIGNAL,
			"res://connections.tscn"));
	CHECK(binding_safety_has_evidence(
			result, SNAME("script_method"),
			FSNameManglerBindingSafety::BINDING_CONNECTION_METHOD,
			"res://connections.tscn"));
	CHECK_FALSE(binding_safety_has_evidence(
			result, SNAME("private_emitter_control"),
			FSNameManglerBindingSafety::BINDING_CONNECTION_METHOD,
			"res://connections.tscn"));
	CHECK_FALSE(binding_safety_has_evidence(
			result, SNAME("private_receiver_control"),
			FSNameManglerBindingSafety::BINDING_CONNECTION_METHOD,
			"res://connections.tscn"));
}

TEST_CASE("[FoundryScript][NameManglerBindingSafety] Resource keeps owned properties and semantic class identities") {
	const Ref<FoundryScript> resource_script = compile_bytecode_test_source(
			"class_name BindingConfig798 extends Resource\n"
			"@export var stored_value: int\n"
			"@export var ordinary_text: String\n"
			"@export var typed_items: Array\n"
			"@export var cycle_data: Array\n"
			"var private_control: int\n");
	const Ref<FoundryScript> typed_script = compile_bytecode_test_source(
			"class_name BindingTyped798 extends Resource\n"
			"var typed_private_control: int\n");

	Ref<Resource> resource;
	resource.instantiate();
	resource->set_script(resource_script);
	bool valid = false;
	resource->set(SNAME("stored_value"), 7, &valid);
	REQUIRE(valid);
	resource->set(SNAME("ordinary_text"), "private_control", &valid);
	REQUIRE(valid);
	Array typed_items;
	typed_items.set_typed(
			Variant::OBJECT, SNAME("Resource"), typed_script);
	resource->set(SNAME("typed_items"), typed_items, &valid);
	REQUIRE(valid);
	Array cycle_data;
	cycle_data.push_back(cycle_data);
	resource->set(SNAME("cycle_data"), cycle_data, &valid);
	REQUIRE(valid);

	FSNameManglerAnalysis::Input analysis_input;
	analysis_input.scripts.push_back(resource_script);
	analysis_input.scripts.push_back(typed_script);
	FSNameManglerBindingSafety::Input binding_input;
	binding_input.add_resource(resource, "res://config.tres");

	const FSNameManglerBindingSafety::Result result =
			FSNameManglerBindingSafety::collect(binding_input, analysis_input);
	REQUIRE_EQ(result.error, OK);
	REQUIRE(result.complete);
	CHECK(binding_safety_has_evidence(
			result, SNAME("stored_value"),
			FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY,
			"res://config.tres"));
	CHECK(binding_safety_has_evidence(
			result, SNAME("BindingConfig798"),
			FSNameManglerBindingSafety::BINDING_RESOURCE_SCRIPT_CLASS,
			"res://config.tres"));
	CHECK(binding_safety_has_evidence(
			result, SNAME("BindingTyped798"),
			FSNameManglerBindingSafety::BINDING_TYPED_CONTAINER_SCRIPT_CLASS,
			"res://config.tres"));
	CHECK_FALSE(binding_safety_has_evidence(
			result, SNAME("private_control"),
			FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY,
			"res://config.tres"));
	CHECK_FALSE(binding_safety_has_evidence(
			result, SNAME("typed_private_control"),
			FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY,
			"res://config.tres"));
}

TEST_CASE("[FoundryScript][NameManglerBindingSafety] Resource traverses nested PackedScene bindings once") {
	const Ref<FoundryScript> catalog_script = compile_bytecode_test_source(
			"extends Resource\n"
			"@export var nested_scene: PackedScene\n"
			"@export var duplicate_scene: PackedScene\n");
	const Ref<FoundryScript> emitter_script = compile_bytecode_test_source(
			"extends Node\n"
			"signal scene_signal\n");
	const Ref<FoundryScript> receiver_script = compile_bytecode_test_source(
			"extends Node\n"
			"@export var stored_value: int\n"
			"@export var catalog: Resource\n"
			"var animated_value: float\n"
			"func scene_method() -> void:\n"
			"\tpass\n");

	Ref<Animation> animation;
	animation.instantiate();
	const int value_track = animation->add_track(Animation::TYPE_VALUE);
	animation->track_set_path(
			value_track, NodePath("Receiver:animated_value"));
	animation->track_insert_key(value_track, 0.0, 2.0);
	Ref<AnimationLibrary> library;
	library.instantiate();
	REQUIRE_EQ(
			library->add_animation(SNAME("nested"), animation), OK);

	Ref<PackedScene> scene;
	scene.instantiate();
	const Ref<SceneState> state = scene->get_state();
	const int node_type = state->add_name(SNAME("Node"));
	const int player_type = state->add_name(SNAME("AnimationPlayer"));
	const int root_node = state->add_node(
			-1, -1, node_type, state->add_name(SNAME("Root")),
			-1, -1, 45);
	const int emitter = state->add_node(
			root_node, root_node, node_type,
			state->add_name(SNAME("Emitter")), -1, -1, 46);
	state->add_node_property(
			emitter, state->add_name(SNAME("script")),
			state->add_value(emitter_script));
	const int receiver = state->add_node(
			root_node, root_node, node_type,
			state->add_name(SNAME("Receiver")), -1, -1, 47);
	state->add_node_property(
			receiver, state->add_name(SNAME("script")),
			state->add_value(receiver_script));
	state->add_node_property(
			receiver, state->add_name(SNAME("stored_value")),
			state->add_value(11));
	const int player = state->add_node(
			root_node, root_node, player_type,
			state->add_name(SNAME("AnimationPlayer")), -1, -1, 48);
	state->add_node_property(
			player, state->add_name(SNAME("root_node")),
			state->add_value(NodePath("..")));
	state->add_node_property(
			player, state->add_name(SNAME("libraries/")),
			state->add_value(library));
	state->add_connection(
			emitter, receiver, state->add_name(SNAME("scene_signal")),
			state->add_name(SNAME("scene_method")),
			Object::CONNECT_PERSIST, 0, {});

	Ref<Resource> catalog;
	catalog.instantiate();
	catalog->set_script(catalog_script);
	bool valid = false;
	catalog->set(SNAME("nested_scene"), scene, &valid);
	REQUIRE(valid);
	catalog->set(SNAME("duplicate_scene"), scene, &valid);
	REQUIRE(valid);
	state->add_node_property(
			receiver, state->add_name(SNAME("catalog")),
			state->add_value(catalog));

	FSNameManglerAnalysis::Input analysis_input;
	analysis_input.scripts.push_back(catalog_script);
	analysis_input.scripts.push_back(emitter_script);
	analysis_input.scripts.push_back(receiver_script);
	FSNameManglerBindingSafety::Input binding_input;
	binding_input.add_resource(catalog, "res://catalog.tres");

	const FSNameManglerBindingSafety::Result result =
			FSNameManglerBindingSafety::collect(
					binding_input, analysis_input);
	INFO(binding_safety_snapshot(result));
	REQUIRE_EQ(result.error, OK);
	REQUIRE(result.complete);
	REQUIRE_EQ(result.evidence.size(), 7);
	CHECK(binding_safety_has_evidence(
			result, SNAME("nested_scene"),
			FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY,
			"res://catalog.tres"));
	CHECK(binding_safety_has_evidence(
			result, SNAME("duplicate_scene"),
			FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY,
			"res://catalog.tres"));
	CHECK(binding_safety_has_evidence(
			result, SNAME("scene_signal"),
			FSNameManglerBindingSafety::BINDING_CONNECTION_SIGNAL,
			"res://catalog.tres"));
	CHECK(binding_safety_has_evidence(
			result, SNAME("scene_method"),
			FSNameManglerBindingSafety::BINDING_CONNECTION_METHOD,
			"res://catalog.tres"));
	CHECK(binding_safety_has_evidence(
			result, SNAME("stored_value"),
			FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY,
			"res://catalog.tres"));
	CHECK(binding_safety_has_evidence(
			result, SNAME("catalog"),
			FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY,
			"res://catalog.tres"));
	CHECK(binding_safety_has_evidence(
			result, SNAME("animated_value"),
			FSNameManglerBindingSafety::BINDING_ANIMATION_PROPERTY,
			"res://catalog.tres"));
}

TEST_CASE("[FoundryScript][NameManglerBindingSafety] Dynamic native scene properties stay native") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"extends Node\n"
			"@export var script_value: int\n");
	const Ref<FoundryScript> external_script = compile_bytecode_test_source(
			"extends Node\n"
			"@export var external_value: int\n");
	Ref<Animation> animation;
	animation.instantiate();
	const int current_track = animation->add_track(Animation::TYPE_VALUE);
	animation->track_set_path(
			current_track, NodePath(".:current"));
	animation->track_insert_key(current_track, 0.0, true);
	Ref<AnimationLibrary> library;
	library.instantiate();
	REQUIRE_EQ(
			library->add_animation(SNAME("dynamic_native"), animation), OK);

	Ref<PackedScene> scene;
	scene.instantiate();
	const Ref<SceneState> state = scene->get_state();
	const int listener_type =
			state->add_name(SNAME("AudioListener2D"));
	const int node_type = state->add_name(SNAME("Node"));
	const int player_type =
			state->add_name(SNAME("AnimationPlayer"));
	const int root_node = state->add_node(
			-1, -1, listener_type,
			state->add_name(SNAME("Listener")), -1, -1, 49);
	state->add_node_property(
			root_node, state->add_name(SNAME("current")),
			state->add_value(true));
	state->add_node_property(
			root_node, state->add_name(SNAME("metadata/native_tag")),
			state->add_value("listener"));
	const int scripted_node = state->add_node(
			root_node, root_node, node_type,
			state->add_name(SNAME("Scripted")), -1, -1, 50);
	state->add_node_property(
			scripted_node, state->add_name(SNAME("script")),
			state->add_value(script));
	state->add_node_property(
			scripted_node, state->add_name(SNAME("script_value")),
			state->add_value(3));
	const int external_node = state->add_node(
			root_node, root_node, node_type,
			state->add_name(SNAME("External")), -1, -1, 51);
	state->add_node_property(
			external_node, state->add_name(SNAME("script")),
			state->add_value(external_script));
	state->add_node_property(
			external_node, state->add_name(SNAME("external_dynamic_key")),
			state->add_value(4));
	const int player = state->add_node(
			root_node, root_node, player_type,
			state->add_name(SNAME("AnimationPlayer")), -1, -1, 52);
	state->add_node_property(
			player, state->add_name(SNAME("libraries/")),
			state->add_value(library));

	FSNameManglerAnalysis::Input analysis_input;
	analysis_input.scripts.push_back(script);
	FSNameManglerBindingSafety::Input binding_input;
	binding_input.add_resource(
			scene, "res://dynamic_native_properties.tscn");

	const int object_count_before = ObjectDB::get_object_count();
	const FSNameManglerBindingSafety::Result result =
			FSNameManglerBindingSafety::collect(
					binding_input, analysis_input);
	INFO(binding_safety_snapshot(result));
	REQUIRE_EQ(result.error, OK);
	REQUIRE(result.complete);
	CHECK_EQ(ObjectDB::get_object_count(), object_count_before);
	REQUIRE_EQ(result.evidence.size(), 1);
	CHECK(binding_safety_has_evidence(
			result, SNAME("script_value"),
			FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY,
			"res://dynamic_native_properties.tscn"));
	CHECK_FALSE(binding_safety_has_evidence(
			result, SNAME("current"),
			FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY,
			"res://dynamic_native_properties.tscn"));
	CHECK_FALSE(binding_safety_has_evidence(
			result, SNAME("metadata/native_tag"),
			FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY,
			"res://dynamic_native_properties.tscn"));
	CHECK_FALSE(binding_safety_has_evidence(
			result, SNAME("external_dynamic_key"),
			FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY,
			"res://dynamic_native_properties.tscn"));

	state->add_node_property(
			scripted_node, state->add_name(SNAME("stale_script_value")),
			state->add_value(4));
	const FSNameManglerBindingSafety::Result stale_result =
			FSNameManglerBindingSafety::collect(
					binding_input, analysis_input);
	CHECK_EQ(stale_result.error, ERR_INVALID_DATA);
	CHECK_FALSE(stale_result.complete);
	CHECK(binding_safety_has_diagnostic(
			stale_result, "stale_script_value"));
	CHECK_EQ(ObjectDB::get_object_count(), object_count_before);
}

TEST_CASE("[FoundryScript][NameManglerBindingSafety] Animation keeps property and method track bindings") {
	const Ref<FoundryScript> target_script = compile_bytecode_test_source(
			"extends Node\n"
			"@export var animated_value: float\n"
			"@export var nested_resource: Resource\n"
			"func animation_method() -> void:\n"
			"\tpass\n"
			"func private_animation_control() -> void:\n"
			"\tpass\n");
	const Ref<FoundryScript> nested_script =
			compile_bytecode_test_source(
					"extends Resource\n"
					"@export var nested_value: float\n");
	Ref<Resource> nested_resource;
	nested_resource.instantiate();
	nested_resource->set_script(nested_script);
	bool nested_valid = false;
	nested_resource->set(SNAME("nested_value"), 4.0, &nested_valid);
	REQUIRE(nested_valid);

	Ref<Animation> animation;
	animation.instantiate();
	const int value_track = animation->add_track(Animation::TYPE_VALUE);
	animation->track_set_path(
			value_track, NodePath("Receiver:animated_value"));
	animation->track_insert_key(value_track, 0.0, 1.0);
	animation->track_set_enabled(value_track, false);
	const int bezier_track = animation->add_track(Animation::TYPE_BEZIER);
	animation->track_set_path(
			bezier_track, NodePath("Receiver:animated_value"));
	animation->bezier_track_insert_key(
			bezier_track, 0.0, 1.0, Vector2(), Vector2());
	const int method_track = animation->add_track(Animation::TYPE_METHOD);
	animation->track_set_path(method_track, NodePath("Receiver"));
	Dictionary method_key;
	method_key["method"] = SNAME("animation_method");
	method_key["args"] = Array();
	animation->track_insert_key(method_track, 0.0, method_key);
	const int native_track = animation->add_track(Animation::TYPE_VALUE);
	animation->track_set_path(
			native_track, NodePath("Receiver:process_mode"));
	animation->track_insert_key(native_track, 0.0, 0);
	const int nested_track = animation->add_track(Animation::TYPE_VALUE);
	animation->track_set_path(
			nested_track,
			NodePath("Receiver:nested_resource:nested_value"));
	animation->track_insert_key(nested_track, 0.0, 5.0);

	Ref<AnimationLibrary> library;
	library.instantiate();
	REQUIRE_EQ(library->add_animation(SNAME("test"), animation), OK);

	Ref<PackedScene> scene;
	scene.instantiate();
	const Ref<SceneState> state = scene->get_state();
	const int node_type = state->add_name(SNAME("Node"));
	const int player_type = state->add_name(SNAME("AnimationPlayer"));
	const int root_node = state->add_node(
			-1, -1, node_type,
			state->add_name(SNAME("PrivateAnimationControl")),
			-1, -1, 50);
	const int receiver = state->add_node(
			root_node, root_node, node_type,
			state->add_name(SNAME("Receiver")), -1, -1, 51);
	state->add_node_property(
			receiver, state->add_name(SNAME("script")),
			state->add_value(target_script));
	state->add_node_property(
			receiver, state->add_name(SNAME("nested_resource")),
			state->add_value(nested_resource));
	const int player = state->add_node(
			root_node, root_node, player_type,
			state->add_name(SNAME("AnimationPlayer")), -1, -1, 52);
	state->add_node_property(
			player, state->add_name(SNAME("root_node")),
			state->add_value(NodePath("..")));
	state->add_node_property(
			player, state->add_name(SNAME("libraries/")),
			state->add_value(library));

	FSNameManglerAnalysis::Input analysis_input;
	analysis_input.scripts.push_back(target_script);
	analysis_input.scripts.push_back(nested_script);
	FSNameManglerBindingSafety::Input binding_input;
	binding_input.add_resource(scene, "res://animation_scene.tscn");
	const int track_count_before = animation->get_track_count();
	const NodePath value_path_before =
			animation->track_get_path(value_track);
	const Variant value_key_before =
			animation->track_get_key_value(value_track, 0);
	const Variant method_key_before =
			animation->track_get_key_value(method_track, 0);
	const Variant nested_value_before =
			nested_resource->get(SNAME("nested_value"));

	const FSNameManglerBindingSafety::Result result =
			FSNameManglerBindingSafety::collect(binding_input, analysis_input);
	REQUIRE_EQ(result.error, OK);
	REQUIRE(result.complete);
	CHECK_EQ(animation->get_track_count(), track_count_before);
	CHECK_EQ(animation->track_get_path(value_track), value_path_before);
	CHECK_EQ(
			animation->track_get_key_value(value_track, 0),
			value_key_before);
	CHECK_EQ(
			animation->track_get_key_value(method_track, 0),
			method_key_before);
	CHECK_FALSE(animation->track_is_enabled(value_track));
	CHECK_EQ(
			nested_resource->get(SNAME("nested_value")),
			nested_value_before);
	CHECK(binding_safety_has_evidence(
			result, SNAME("animated_value"),
			FSNameManglerBindingSafety::BINDING_ANIMATION_PROPERTY,
			"res://animation_scene.tscn"));
	CHECK(binding_safety_has_evidence(
			result, SNAME("animation_method"),
			FSNameManglerBindingSafety::BINDING_ANIMATION_METHOD,
			"res://animation_scene.tscn"));
	CHECK(binding_safety_has_evidence(
			result, SNAME("nested_resource"),
			FSNameManglerBindingSafety::BINDING_ANIMATION_PROPERTY,
			"res://animation_scene.tscn"));
	CHECK(binding_safety_has_evidence(
			result, SNAME("nested_value"),
			FSNameManglerBindingSafety::BINDING_ANIMATION_PROPERTY,
			"res://animation_scene.tscn"));
	CHECK_FALSE(binding_safety_has_evidence(
			result, SNAME("private_animation_control"),
			FSNameManglerBindingSafety::BINDING_ANIMATION_METHOD,
			"res://animation_scene.tscn"));
}

TEST_CASE("[FoundryScript][NameManglerBindingSafety] Acceptance text and binary resources have identical evidence") {
	TemporaryProjectTree project(
			"name_mangler_binding_safety_acceptance");
	project.write_file(
			"project.foundry",
			"[application]\nconfig/name=\"BindingSafety798\"\n");
	const String base_path = project.root.path_join("base.fs");
	const String emitter_path = project.root.path_join("emitter.fs");
	const String receiver_path = project.root.path_join("receiver.fs");
	const String config_path = project.root.path_join("config.fs");
	project.write_file(
			"base.fs",
			"extends Node\n"
			"@export var inherited_override: int\n"
			"func private_base_name() -> void:\n"
			"\tpass\n");
	project.write_file(
			"emitter.fs",
			"extends Node\n"
			"signal acceptance_signal(value: int)\n"
			"func private_emitter_name() -> void:\n"
			"\tpass\n");
	project.write_file(
			"receiver.fs",
			vformat(
					"extends \"%s\"\n"
					"var animated_value: float\n"
					"func acceptance_method(value: int) -> void:\n"
					"\tinherited_override += value\n"
					"func private_receiver_name() -> void:\n"
					"\tpass\n",
					base_path));
	project.write_file(
			"config.fs",
			"class_name BindingAcceptanceConfig798 extends Resource\n"
			"@export var config_value: int\n"
			"var private_config_name: int\n");
	const Ref<FoundryScript> base_script =
			name_mangler_application_load_source(base_path);
	const Ref<FoundryScript> emitter_script =
			name_mangler_application_load_source(emitter_path);
	const Ref<FoundryScript> receiver_script =
			name_mangler_application_load_source(receiver_path);
	const Ref<FoundryScript> config_script =
			name_mangler_application_load_source(config_path);
	BindingSafetyGlobalClassGuard global_class(
			SNAME("BindingAcceptanceConfig798"), SNAME("Resource"),
			config_script->get_script_path());

	Ref<Animation> animation;
	animation.instantiate();
	const int value_track = animation->add_track(Animation::TYPE_VALUE);
	animation->track_set_path(
			value_track, NodePath("Receiver:animated_value"));
	animation->track_insert_key(value_track, 0.0, 3.0);
	Ref<AnimationLibrary> library;
	library.instantiate();
	REQUIRE_EQ(
			library->add_animation(SNAME("acceptance"), animation), OK);

	Ref<PackedScene> scene;
	scene.instantiate();
	const Ref<SceneState> state = scene->get_state();
	const int node_type = state->add_name(SNAME("Node"));
	const int player_type = state->add_name(SNAME("AnimationPlayer"));
	const int root_node = state->add_node(
			-1, -1, node_type, state->add_name(SNAME("Root")),
			-1, -1, 60);
	const int emitter = state->add_node(
			root_node, root_node, node_type,
			state->add_name(SNAME("Emitter")), -1, -1, 61);
	state->add_node_property(
			emitter, state->add_name(SNAME("script")),
			state->add_value(emitter_script));
	const int receiver = state->add_node(
			root_node, root_node, node_type,
			state->add_name(SNAME("Receiver")), -1, -1, 62);
	state->add_node_property(
			receiver, state->add_name(SNAME("script")),
			state->add_value(receiver_script));
	state->add_node_property(
			receiver, state->add_name(SNAME("inherited_override")),
			state->add_value(2));
	const int player = state->add_node(
			root_node, root_node, player_type,
			state->add_name(SNAME("AnimationPlayer")), -1, -1, 63);
	state->add_node_property(
			player, state->add_name(SNAME("root_node")),
			state->add_value(NodePath("..")));
	state->add_node_property(
			player, state->add_name(SNAME("libraries/")),
			state->add_value(library));
	state->add_connection(
			emitter, receiver, state->add_name(SNAME("acceptance_signal")),
			state->add_name(SNAME("acceptance_method")),
			Object::CONNECT_PERSIST, 0, {});

	Ref<Resource> config;
	config.instantiate();
	config->set_script(config_script);
	bool valid = false;
	config->set(SNAME("config_value"), 9, &valid);
	REQUIRE(valid);

	const String text_scene_path = project.root.path_join("main.tscn");
	const String binary_scene_path = project.root.path_join("main.scn");
	const String text_resource_path = project.root.path_join("config.tres");
	const String binary_resource_path = project.root.path_join("config.res");
	REQUIRE_EQ(ResourceSaver::save(scene, text_scene_path), OK);
	REQUIRE_EQ(ResourceSaver::save(scene, binary_scene_path), OK);
	REQUIRE_EQ(ResourceSaver::save(config, text_resource_path), OK);
	REQUIRE_EQ(ResourceSaver::save(config, binary_resource_path), OK);

	Error load_error = OK;
	const Ref<PackedScene> text_scene = ResourceLoader::load(
			text_scene_path, "PackedScene",
			ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP, &load_error);
	REQUIRE_EQ(load_error, OK);
	REQUIRE(text_scene.is_valid());
	const Ref<Resource> text_config = ResourceLoader::load(
			text_resource_path, String(),
			ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP, &load_error);
	REQUIRE_EQ(load_error, OK);
	REQUIRE(text_config.is_valid());
	const Ref<PackedScene> binary_scene = ResourceLoader::load(
			binary_scene_path, "PackedScene",
			ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP, &load_error);
	REQUIRE_EQ(load_error, OK);
	REQUIRE(binary_scene.is_valid());
	const Ref<Resource> binary_config = ResourceLoader::load(
			binary_resource_path, String(),
			ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP, &load_error);
	REQUIRE_EQ(load_error, OK);
	REQUIRE(binary_config.is_valid());

	const Dictionary text_scene_before =
			text_scene->get_state()->get_bundled_scene();
	const Dictionary binary_scene_before =
			binary_scene->get_state()->get_bundled_scene();
	FSNameManglerAnalysis::Input analysis_input;
	analysis_input.scripts.push_back(base_script);
	analysis_input.scripts.push_back(emitter_script);
	analysis_input.scripts.push_back(receiver_script);
	analysis_input.scripts.push_back(config_script);
	FSNameManglerBindingSafety::Input text_input;
	text_input.add_resource(text_scene, "res://acceptance/main");
	text_input.add_resource(text_config, "res://acceptance/config");
	FSNameManglerBindingSafety::Input binary_input;
	binary_input.add_resource(binary_config, "res://acceptance/config");
	binary_input.add_resource(binary_scene, "res://acceptance/main");

	const FSNameManglerBindingSafety::Result text_result =
			FSNameManglerBindingSafety::collect(text_input, analysis_input);
	const FSNameManglerBindingSafety::Result binary_result =
			FSNameManglerBindingSafety::collect(binary_input, analysis_input);
	INFO("Text result:\n", binding_safety_snapshot(text_result));
	INFO("Binary result:\n", binding_safety_snapshot(binary_result));
	REQUIRE_EQ(text_result.error, OK);
	REQUIRE(text_result.complete);
	REQUIRE_EQ(binary_result.error, OK);
	REQUIRE(binary_result.complete);
	CHECK_EQ(
			binding_safety_snapshot(text_result),
			binding_safety_snapshot(binary_result));
	CHECK_EQ(
			text_scene->get_state()->get_bundled_scene(),
			text_scene_before);
	CHECK_EQ(
			binary_scene->get_state()->get_bundled_scene(),
			binary_scene_before);
	const BindingSafetyRuntimeSnapshot baseline_text =
			binding_safety_run_runtime(text_scene, text_config, false);
	const BindingSafetyRuntimeSnapshot baseline_binary =
			binding_safety_run_runtime(binary_scene, binary_config, false);
	CHECK(baseline_text == baseline_binary);
	CHECK_EQ(baseline_text.stored_value, 2.0);
	CHECK_EQ(baseline_text.signal_value, 7.0);
	CHECK_EQ(baseline_text.animation_value, 3.0);
	CHECK_EQ(baseline_text.config_value, 9);
	CHECK_EQ(
			baseline_text.config_class,
			SNAME("BindingAcceptanceConfig798"));

	FSNameManglerAnalysis::Input applied_input = analysis_input;
	REQUIRE_EQ(text_result.apply_to_input(applied_input), OK);
	const FSNameManglerAnalysis::Result analysis =
			FSNameManglerAnalysis::analyze(applied_input);
	REQUIRE_EQ(analysis.error, OK);
	CHECK(binding_safety_has_applied_evidence(
			text_result, analysis, SNAME("acceptance_signal"),
			FSNameManglerBindingSafety::BINDING_CONNECTION_SIGNAL,
			"res://acceptance/main"));
	CHECK(binding_safety_has_applied_evidence(
			text_result, analysis, SNAME("acceptance_method"),
			FSNameManglerBindingSafety::BINDING_CONNECTION_METHOD,
			"res://acceptance/main"));
	CHECK(binding_safety_has_applied_evidence(
			text_result, analysis, SNAME("inherited_override"),
			FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY,
			"res://acceptance/main"));
	CHECK(binding_safety_has_applied_evidence(
			text_result, analysis, SNAME("animated_value"),
			FSNameManglerBindingSafety::BINDING_ANIMATION_PROPERTY,
			"res://acceptance/main"));
	CHECK(binding_safety_has_applied_evidence(
			text_result, analysis,
			SNAME("BindingAcceptanceConfig798"),
			FSNameManglerBindingSafety::BINDING_RESOURCE_SCRIPT_CLASS,
			"res://acceptance/config"));
	CHECK(binding_safety_has_applied_evidence(
			text_result, analysis, SNAME("config_value"),
			FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY,
			"res://acceptance/config"));
	CHECK_FALSE(analysis.rename_map.has(SNAME("acceptance_signal")));
	CHECK_FALSE(analysis.rename_map.has(SNAME("acceptance_method")));
	CHECK_FALSE(analysis.rename_map.has(SNAME("inherited_override")));
	CHECK_FALSE(analysis.rename_map.has(SNAME("animated_value")));
	CHECK_FALSE(
			analysis.rename_map.has(SNAME("BindingAcceptanceConfig798")));
	CHECK(analysis.rename_map.has(SNAME("private_base_name")));
	CHECK(analysis.rename_map.has(SNAME("private_emitter_name")));
	CHECK(analysis.rename_map.has(SNAME("private_receiver_name")));
	CHECK(analysis.rename_map.has(SNAME("private_config_name")));

	FSNameManglerApplication::Transaction transaction;
	Vector<FSNameManglerApplication::Diagnostic> application_diagnostics;
	REQUIRE_EQ(
			transaction.begin(
					applied_input.scripts, analysis.rename_map,
					application_diagnostics),
			OK);
	REQUIRE(application_diagnostics.is_empty());
	const Vector<uint8_t> base_buffer =
			name_mangler_application_serialize(base_script);
	const Vector<uint8_t> emitter_buffer =
			name_mangler_application_serialize(emitter_script);
	const Vector<uint8_t> receiver_buffer =
			name_mangler_application_serialize(receiver_script);
	const Vector<uint8_t> config_buffer =
			name_mangler_application_serialize(config_script);
	transaction.rollback();

	CHECK(binding_safety_buffer_contains(
			emitter_buffer, "acceptance_signal"));
	CHECK(binding_safety_buffer_contains(
			receiver_buffer, "acceptance_method"));
	CHECK(binding_safety_buffer_contains(
			base_buffer, "inherited_override"));
	CHECK(binding_safety_buffer_contains(
			receiver_buffer, "animated_value"));
	CHECK(binding_safety_buffer_contains(
			config_buffer, "BindingAcceptanceConfig798"));
	CHECK(binding_safety_buffer_contains(config_buffer, "config_value"));
	CHECK_FALSE(binding_safety_buffer_contains(
			base_buffer, "private_base_name"));
	CHECK_FALSE(binding_safety_buffer_contains(
			emitter_buffer, "private_emitter_name"));
	CHECK_FALSE(binding_safety_buffer_contains(
			receiver_buffer, "private_receiver_name"));
	CHECK_FALSE(binding_safety_buffer_contains(
			config_buffer, "private_config_name"));

	const String base_binary_path =
			base_path.get_basename() + ".fsb";
	const String emitter_binary_path =
			emitter_path.get_basename() + ".fsb";
	const String receiver_binary_path =
			receiver_path.get_basename() + ".fsb";
	const String config_binary_path =
			config_path.get_basename() + ".fsb";
	bytecode_write_file(base_binary_path, base_buffer);
	bytecode_write_file(emitter_binary_path, emitter_buffer);
	bytecode_write_file(receiver_binary_path, receiver_buffer);
	bytecode_write_file(config_binary_path, config_buffer);
	bytecode_write_remap_file(base_path, base_binary_path);
	bytecode_write_remap_file(emitter_path, emitter_binary_path);
	bytecode_write_remap_file(receiver_path, receiver_binary_path);
	bytecode_write_remap_file(config_path, config_binary_path);
	REQUIRE_EQ(
			ResourceLoader::path_remap(base_path),
			base_binary_path);
	REQUIRE_EQ(
			ResourceLoader::path_remap(emitter_path),
			emitter_binary_path);
	REQUIRE_EQ(
			ResourceLoader::path_remap(receiver_path),
			receiver_binary_path);
	REQUIRE_EQ(
			ResourceLoader::path_remap(config_path),
			config_binary_path);

	CHECK_EQ(
			text_scene->get_state()->get_bundled_scene(),
			text_scene_before);
	CHECK_EQ(
			binary_scene->get_state()->get_bundled_scene(),
			binary_scene_before);
	CHECK_EQ(text_config->get(SNAME("config_value")), Variant(9));
	CHECK_EQ(binary_config->get(SNAME("config_value")), Variant(9));

	scene->set_path(String());
	config->set_path(String());
	text_scene->set_path(String());
	binary_scene->set_path(String());
	text_config->set_path(String());
	binary_config->set_path(String());
	FSCache::remove_script(base_path);
	FSCache::remove_script(emitter_path);
	FSCache::remove_script(receiver_path);
	FSCache::remove_script(config_path);
	FSCache::remove_static_script(
			base_script->get_fully_qualified_name());
	FSCache::remove_static_script(
			emitter_script->get_fully_qualified_name());
	FSCache::remove_static_script(
			receiver_script->get_fully_qualified_name());
	FSCache::remove_static_script(
			config_script->get_fully_qualified_name());

	const Ref<PackedScene> staged_text_scene = ResourceLoader::load(
			text_scene_path, "PackedScene",
			ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP, &load_error);
	REQUIRE_EQ(load_error, OK);
	REQUIRE(staged_text_scene.is_valid());
	const Ref<Resource> staged_text_config = ResourceLoader::load(
			text_resource_path, String(),
			ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP, &load_error);
	REQUIRE_EQ(load_error, OK);
	REQUIRE(staged_text_config.is_valid());
	const Ref<PackedScene> staged_binary_scene = ResourceLoader::load(
			binary_scene_path, "PackedScene",
			ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP, &load_error);
	REQUIRE_EQ(load_error, OK);
	REQUIRE(staged_binary_scene.is_valid());
	const Ref<Resource> staged_binary_config = ResourceLoader::load(
			binary_resource_path, String(),
			ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP, &load_error);
	REQUIRE_EQ(load_error, OK);
	REQUIRE(staged_binary_config.is_valid());

	const BindingSafetyRuntimeSnapshot staged_text =
			binding_safety_run_runtime(
					staged_text_scene, staged_text_config, true);
	const BindingSafetyRuntimeSnapshot staged_binary =
			binding_safety_run_runtime(
					staged_binary_scene, staged_binary_config, true);
	CHECK(staged_text == baseline_text);
	CHECK(staged_binary == baseline_binary);
	CHECK(staged_text == staged_binary);

	FSCache::remove_script(base_path);
	FSCache::remove_script(emitter_path);
	FSCache::remove_script(receiver_path);
	FSCache::remove_script(config_path);
	FSCache::remove_static_script(
			base_script->get_fully_qualified_name());
	FSCache::remove_static_script(
			emitter_script->get_fully_qualified_name());
	FSCache::remove_static_script(
			receiver_script->get_fully_qualified_name());
	FSCache::remove_static_script(
			config_script->get_fully_qualified_name());
}

TEST_CASE("[FoundryScript][NameManglerBindingSafety] Failure rejects invalid roots transactionally") {
	Ref<Resource> resource;
	resource.instantiate();
	FSNameManglerBindingSafety::Input binding_input;
	FSNameManglerBindingSafety::ResourceRoot null_root;
	null_root.source = "res://a_null.tres";
	binding_input.resources.push_back(null_root);
	binding_input.add_resource(resource, "res://z_duplicate.tres");
	binding_input.add_resource(resource, "res://z_duplicate.tres");
	FSNameManglerAnalysis::Input analysis_input;

	const FSNameManglerBindingSafety::Result result =
			FSNameManglerBindingSafety::collect(
					binding_input, analysis_input);
	CHECK_EQ(result.error, ERR_INVALID_DATA);
	CHECK_FALSE(result.complete);
	REQUIRE_EQ(result.diagnostics.size(), 2);
	CHECK(result.diagnostics[0].source == "res://a_null.tres");
	CHECK(result.diagnostics[1].source == "res://z_duplicate.tres");
	CHECK(binding_safety_has_diagnostic(result, "resource is null"));
	CHECK(binding_safety_has_diagnostic(
			result, "duplicate resource source"));

	FSNameManglerBindingSafety::Evidence evidence;
	evidence.name = SNAME("must_not_apply");
	evidence.source = "res://z_duplicate.tres";
	FSNameManglerBindingSafety::Result incomplete = result;
	incomplete.evidence.push_back(evidence);
	CHECK_EQ(incomplete.apply_to_input(analysis_input), ERR_INVALID_DATA);
	CHECK(analysis_input.keep_evidence.is_empty());
}

TEST_CASE("[FoundryScript][NameManglerBindingSafety] Failure reports scene cycles placeholders and stale connections") {
	Ref<PackedScene> cyclic_scene;
	cyclic_scene.instantiate();
	const Ref<SceneState> cyclic_state = cyclic_scene->get_state();
	const int node_type = cyclic_state->add_name(SNAME("Node"));
	const int root_node = cyclic_state->add_node(
			-1, -1, node_type,
			cyclic_state->add_name(SNAME("Root")), -1, -1, 70);
	cyclic_state->add_node(
			root_node, root_node, SceneState::TYPE_INSTANTIATED,
			cyclic_state->add_name(SNAME("Cycle")),
			cyclic_state->add_value(cyclic_scene), -1, 71);
	cyclic_state->add_node(
			root_node, root_node, SceneState::TYPE_INSTANTIATED,
			cyclic_state->add_name(SNAME("Placeholder")),
			cyclic_state->add_value("res://missing.tscn") |
					SceneState::FLAG_INSTANCE_IS_PLACEHOLDER,
			-1, 72);
	cyclic_state->add_connection(
			root_node, root_node, cyclic_state->add_name(SNAME("ready")),
			cyclic_state->add_name(SNAME("missing_method")),
			Object::CONNECT_PERSIST, 0, {});

	FSNameManglerBindingSafety::Input binding_input;
	binding_input.add_resource(cyclic_scene, "res://invalid_scene.tscn");
	FSNameManglerAnalysis::Input analysis_input;
	const FSNameManglerBindingSafety::Result result =
			FSNameManglerBindingSafety::collect(
					binding_input, analysis_input);
	CHECK_EQ(result.error, ERR_INVALID_DATA);
	CHECK_FALSE(result.complete);
	CHECK(binding_safety_has_diagnostic(
			result, "scene inheritance or instance cycle"));
	CHECK(binding_safety_has_diagnostic(
			result, "instance placeholder cannot be expanded"));
	CHECK(binding_safety_has_diagnostic(
			result, "missing_method"));
	cyclic_state->clear();

	Ref<PackedScene> collision_scene;
	collision_scene.instantiate();
	const Ref<SceneState> collision_state = collision_scene->get_state();
	const int collision_type = collision_state->add_name(SNAME("Node"));
	const int collision_root = collision_state->add_node(
			-1, -1, collision_type,
			collision_state->add_name(SNAME("Root")), -1, -1, 80);
	collision_state->add_node(
			collision_root, collision_root, collision_type,
			collision_state->add_name(SNAME("Duplicate")), -1, -1, 81);
	collision_state->add_node(
			collision_root, collision_root, collision_type,
			collision_state->add_name(SNAME("Duplicate")), -1, -1, 82);
	FSNameManglerBindingSafety::Input collision_input;
	collision_input.add_resource(
			collision_scene, "res://collision_scene.tscn");
	const FSNameManglerBindingSafety::Result collision_result =
			FSNameManglerBindingSafety::collect(
					collision_input, analysis_input);
	CHECK_EQ(collision_result.error, ERR_INVALID_DATA);
	CHECK_FALSE(collision_result.complete);
	CHECK(binding_safety_has_diagnostic(
			collision_result, "collides"));
}

} // namespace FSTests

#endif // TOOLS_ENABLED
