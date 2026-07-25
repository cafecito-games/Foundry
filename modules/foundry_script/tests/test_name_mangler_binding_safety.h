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

#include "modules/foundry_script/fs_name_mangler_binding_safety.h"
#include "modules/foundry_script/tests/test_bytecode_serialization.h"

#include "scene/resources/packed_scene.h"

#include "tests/test_macros.h"

namespace FSTests {

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

} // namespace FSTests

#endif // TOOLS_ENABLED
