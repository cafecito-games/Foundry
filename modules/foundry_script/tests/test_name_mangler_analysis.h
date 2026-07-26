/**************************************************************************/
/*  test_name_mangler_analysis.h                                          */
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

#include "modules/foundry_script/fs_name_mangler_analysis.h"
#include "modules/foundry_script/fs_name_mangler_keep_rules.h"
#include "modules/foundry_script/tests/test_bytecode_serialization.h"

#include "modules/foundry_script/fs_conformance_registry.h"

#include "tests/test_macros.h"

namespace FSTests {

static bool name_analysis_has_reason(const FSNameManglerAnalysis::Result &p_result, const StringName &p_name,
		FSNameManglerAnalysis::KeepReason p_reason) {
	const FSNameManglerAnalysis::Classification *classification = p_result.find(p_name);
	if (classification == nullptr) {
		return false;
	}
	for (const FSNameManglerAnalysis::KeepEvidence &evidence : classification->keep_evidence) {
		if (evidence.reason == p_reason) {
			return true;
		}
	}
	return false;
}

static String name_analysis_snapshot(const FSNameManglerAnalysis::Result &p_result) {
	String snapshot;
	for (const FSNameManglerAnalysis::Classification &classification : p_result.classifications) {
		snapshot += String(classification.name) + "->" + String(classification.replacement) + "|";
		for (const FSNameManglerAnalysis::IdentifierKind kind : classification.kinds) {
			snapshot += String::num_int64(kind) + ",";
		}
		snapshot += "|";
		for (const FSNameManglerAnalysis::KeepEvidence &evidence : classification.keep_evidence) {
			snapshot += String::num_int64(evidence.reason) + ":" + evidence.detail + ",";
		}
		snapshot += "\n";
	}
	snapshot += "-- log --\n";
	for (const String &line : p_result.keep_log) {
		snapshot += line + "\n";
	}
	return snapshot;
}

TEST_CASE("[FoundryScript][NameManglerAnalysis] Classifies a compiled project conservatively") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"extends Node\n"
			"@export var scene_property: int\n"
			"var private_member: int\n"
			"signal scene_signal\n"
			"signal private_signal\n"
			"enum Mode:\n"
			"\tIDLE = 0\n"
			"\tACTIVE = 1\n"
			"class PrivateNested:\n"
			"\tvar nested_member: int\n"
			"@rpc func remote_call() -> void:\n"
			"\tpass\n"
			"func _process(_delta: float) -> void:\n"
			"\tpass\n"
			"func scene_handler() -> void:\n"
			"\tpass\n"
			"func string_named() -> void:\n"
			"\tpass\n"
			"func private_helper() -> void:\n"
			"\tprivate_member += 1\n"
			"func remember_name() -> String:\n"
			"\treturn \"string_named\"\n");

	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(script);
	input.add_keep(SNAME("scene_signal"), FSNameManglerAnalysis::KEEP_SCENE_OR_RESOURCE,
			"res://main.tscn connection signal");
	input.add_keep(SNAME("scene_handler"), FSNameManglerAnalysis::KEEP_SCENE_OR_RESOURCE,
			"res://main.tscn connection method");

	const FSNameManglerAnalysis::Result result = FSNameManglerAnalysis::analyze(input);
	REQUIRE(result.error == OK);
	CHECK(result.rename_map.has(SNAME("private_member")));
	CHECK(result.rename_map.has(SNAME("private_helper")));
	CHECK(result.rename_map.has(SNAME("private_signal")));
	CHECK(result.rename_map.has(SNAME("PrivateNested")));
	CHECK(result.rename_map.has(SNAME("Mode")));
	CHECK_FALSE(result.rename_map.has(SNAME("scene_property")));
	CHECK_FALSE(result.rename_map.has(SNAME("scene_signal")));
	CHECK_FALSE(result.rename_map.has(SNAME("scene_handler")));
	CHECK_FALSE(result.rename_map.has(SNAME("remote_call")));
	CHECK_FALSE(result.rename_map.has(SNAME("_process")));
	CHECK_FALSE(result.rename_map.has(SNAME("string_named")));
	CHECK(name_analysis_has_reason(result, SNAME("remote_call"), FSNameManglerAnalysis::KEEP_RPC));
	CHECK(name_analysis_has_reason(result, SNAME("_process"), FSNameManglerAnalysis::KEEP_NATIVE_VIRTUAL));
	CHECK(name_analysis_has_reason(result, SNAME("string_named"), FSNameManglerAnalysis::KEEP_STRING_LITERAL));
}

TEST_CASE("[FoundryScript][NameManglerAnalysis] Class candidates are atomic and identities stay structured") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"namespace name_analysis\n"
			"class_name QualifiedCandidate\n"
			"class NestedCandidate[T]:\n"
			"\tpass\n");
	REQUIRE_EQ(script->get_local_name(), SNAME("QualifiedCandidate"));
	REQUIRE_EQ(script->get_global_name(), SNAME("name_analysis.QualifiedCandidate"));
	const Ref<FoundryScript> nested = script->get_subclasses()[SNAME("NestedCandidate")];
	REQUIRE(nested.is_valid());
	REQUIRE_EQ(nested->get_local_name(), SNAME("NestedCandidate"));
	REQUIRE_FALSE(nested->get_fully_qualified_name().is_empty());
	REQUIRE_NE(nested->get_fully_qualified_name(), String(nested->get_local_name()));

	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(script);
	const FSNameManglerAnalysis::Result result = FSNameManglerAnalysis::analyze(input);

	REQUIRE_EQ(result.error, OK);
	CHECK(result.rename_map.has(SNAME("QualifiedCandidate")));
	CHECK(result.rename_map.has(SNAME("NestedCandidate")));
	CHECK(result.find(SNAME("name_analysis.QualifiedCandidate")) == nullptr);
	CHECK(result.find(StringName(nested->get_fully_qualified_name())) == nullptr);
	CHECK(result.find(SNAME("T")) == nullptr);
}

TEST_CASE("[FoundryScript][NameManglerAnalysis] Recurses through constants and only treats NodePath subnames as evidence") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"var literal_kept: int\n"
			"var nodepath_property: int\n"
			"const NESTED = [{ \"names\": [\"literal_kept\"] }]\n"
			"const PROPERTY_PATH = NodePath(\"Child:nodepath_property\")\n"
			"const NODE_ONLY_PATH = NodePath(\"private_node_segment\")\n"
			"func lambda_named() -> void:\n"
			"\tpass\n"
			"func make_nested_lambda() -> Callable:\n"
			"\treturn func() -> Callable:\n"
			"\t\treturn func() -> String:\n"
			"\t\t\treturn \"lambda_named\"\n"
			"func private_node_segment() -> void:\n"
			"\tpass\n");

	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(script);
	const FSNameManglerAnalysis::Result result = FSNameManglerAnalysis::analyze(input);

	REQUIRE(result.error == OK);
	CHECK_FALSE(result.rename_map.has(SNAME("literal_kept")));
	CHECK_FALSE(result.rename_map.has(SNAME("nodepath_property")));
	CHECK_FALSE(result.rename_map.has(SNAME("lambda_named")));
	CHECK(result.rename_map.has(SNAME("private_node_segment")));
	CHECK(name_analysis_has_reason(result, SNAME("literal_kept"), FSNameManglerAnalysis::KEEP_STRING_LITERAL));
	CHECK(name_analysis_has_reason(result, SNAME("nodepath_property"), FSNameManglerAnalysis::KEEP_STRING_LITERAL));
	CHECK(name_analysis_has_reason(result, SNAME("lambda_named"), FSNameManglerAnalysis::KEEP_STRING_LITERAL));
}

TEST_CASE("[FoundryScript][NameManglerAnalysis] Callable and Signal constants provide nested name evidence") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"signal signal_kept\n"
			"func callable_kept() -> void:\n"
			"\tpass\n"
			"func bound_kept() -> void:\n"
			"\tpass\n");

	Ref<RefCounted> target;
	target.instantiate();
	Array nested_values;
	nested_values.push_back(Callable(target.ptr(), SNAME("callable_kept")).bind("bound_kept"));
	Dictionary nested_signal;
	nested_signal["value"] = Signal(target.ptr(), SNAME("signal_kept"));
	nested_values.push_back(nested_signal);
	HashMap<StringName, Variant> &constants =
			const_cast<HashMap<StringName, Variant> &>(script->get_constants());
	constants.insert(SNAME("RUNTIME_NAME_VALUES"), nested_values);

	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(script);
	const FSNameManglerAnalysis::Result result = FSNameManglerAnalysis::analyze(input);

	REQUIRE(result.error == OK);
	CHECK_FALSE(result.rename_map.has(SNAME("callable_kept")));
	CHECK_FALSE(result.rename_map.has(SNAME("bound_kept")));
	CHECK_FALSE(result.rename_map.has(SNAME("signal_kept")));
	CHECK(name_analysis_has_reason(result, SNAME("callable_kept"), FSNameManglerAnalysis::KEEP_STRING_LITERAL));
	CHECK(name_analysis_has_reason(result, SNAME("bound_kept"), FSNameManglerAnalysis::KEEP_STRING_LITERAL));
	CHECK(name_analysis_has_reason(result, SNAME("signal_kept"), FSNameManglerAnalysis::KEEP_STRING_LITERAL));
}

TEST_CASE("[FoundryScript][NameManglerAnalysis] Parameter annotation values provide name evidence") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"var method_parameter_arg_kept: int\n"
			"var method_parameter_kwarg_kept: int\n"
			"var signal_parameter_arg_kept: int\n"
			"var signal_parameter_kwarg_kept: int\n"
			"signal annotated_signal(value: int)\n"
			"func annotated_method(value: int) -> void:\n"
			"\tpass\n");

	FoundryScript::AnnotationUsage method_usage;
	method_usage.args.push_back("method_parameter_arg_kept");
	Array method_kwarg_values;
	method_kwarg_values.push_back("method_parameter_kwarg_kept");
	method_usage.kwargs["nested"] = method_kwarg_values;
	HashMap<StringName, Vector<FoundryScript::AnnotationUsage>> method_parameters;
	method_parameters[SNAME("value")].push_back(method_usage);
	auto &method_parameter_annotations = const_cast<
			HashMap<StringName, HashMap<StringName, Vector<FoundryScript::AnnotationUsage>>> &>(
			script->get_method_parameter_annotations());
	method_parameter_annotations.insert(SNAME("annotated_method"), method_parameters);

	FoundryScript::AnnotationUsage signal_usage;
	signal_usage.args.push_back("signal_parameter_arg_kept");
	Array signal_kwarg_values;
	signal_kwarg_values.push_back("signal_parameter_kwarg_kept");
	signal_usage.kwargs["nested"] = signal_kwarg_values;
	HashMap<StringName, Vector<FoundryScript::AnnotationUsage>> signal_parameters;
	signal_parameters[SNAME("value")].push_back(signal_usage);
	auto &signal_parameter_annotations = const_cast<
			HashMap<StringName, HashMap<StringName, Vector<FoundryScript::AnnotationUsage>>> &>(
			script->get_signal_parameter_annotations());
	signal_parameter_annotations.insert(SNAME("annotated_signal"), signal_parameters);

	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(script);
	const FSNameManglerAnalysis::Result result = FSNameManglerAnalysis::analyze(input);

	REQUIRE(result.error == OK);
	const StringName kept_names[] = {
		SNAME("method_parameter_arg_kept"),
		SNAME("method_parameter_kwarg_kept"),
		SNAME("signal_parameter_arg_kept"),
		SNAME("signal_parameter_kwarg_kept"),
	};
	for (const StringName &name : kept_names) {
		CHECK_FALSE(result.rename_map.has(name));
		CHECK(name_analysis_has_reason(result, name, FSNameManglerAnalysis::KEEP_STRING_LITERAL));
	}
}

TEST_CASE("[FoundryScript][NameManglerAnalysis] Trait contracts and conformance dispatch keys are classified") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"trait SourceTrait:\n"
			"\tabstract func source_witness() -> int\n"
			"class Target:\n"
			"\tpass\n"
			"extend Target uses SourceTrait:\n"
			"\tfunc source_witness() -> int:\n"
			"\t\treturn 1\n"
			"func ExternalConformanceTrait() -> void:\n"
			"\tpass\n");

	auto &requirements =
			const_cast<HashMap<StringName, FoundryScript::AbstractTraitRequirement> &>(
					script->get_abstract_trait_requirements());
	FoundryScript::AbstractTraitRequirement requirement;
	requirement.method_info.name = SNAME("requirement_only_method");
	requirements.insert(requirement.method_info.name, requirement);

	const String source = script->get_script_path();
	Vector<FSConformanceRegistry::RuntimeConformance> conformances =
			FSConformanceRegistry::get_singleton()->get_runtime_witnesses(source);
	REQUIRE_FALSE(conformances.is_empty());
	FSFunction *witness_function = nullptr;
	if (!conformances.is_empty() && !conformances[0].functions.is_empty()) {
		witness_function = conformances[0].functions.begin()->value;
	}
	REQUIRE(witness_function != nullptr);
	if (!conformances.is_empty() && witness_function != nullptr) {
		conformances.write[0].target_keys.clear();
		conformances.write[0].target_keys.push_back("_fsb_0");
		conformances.write[0].trait_name = SNAME("ExternalConformanceTrait");
		conformances.write[0].functions.clear();
		conformances.write[0].functions.insert(SNAME("registry_only_witness"), witness_function);
		FSConformanceRegistry::get_singleton()->register_runtime_witnesses(source, conformances);
	}

	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(script);
	const FSNameManglerAnalysis::Result result = FSNameManglerAnalysis::analyze(input);

	REQUIRE(result.error == OK);
	const FSNameManglerAnalysis::Classification *requirement_classification =
			result.find(SNAME("requirement_only_method"));
	REQUIRE(requirement_classification != nullptr);
	if (requirement_classification != nullptr) {
		CHECK(requirement_classification->kinds.has(FSNameManglerAnalysis::IDENTIFIER_METHOD));
	}
	const FSNameManglerAnalysis::Classification *witness_classification =
			result.find(SNAME("registry_only_witness"));
	REQUIRE(witness_classification != nullptr);
	if (witness_classification != nullptr) {
		CHECK(witness_classification->kinds.has(FSNameManglerAnalysis::IDENTIFIER_METHOD));
	}
	CHECK_FALSE(result.rename_map.has(SNAME("ExternalConformanceTrait")));
	CHECK(name_analysis_has_reason(result, SNAME("ExternalConformanceTrait"),
			FSNameManglerAnalysis::KEEP_EXTERNAL_OR_UNPROVABLE));
	CHECK(result.find(SNAME("_fsb_0")) == nullptr);
	for (const KeyValue<StringName, StringName> &rename : result.rename_map) {
		CHECK_NE(rename.value, SNAME("_fsb_0"));
	}
}

TEST_CASE("[FoundryScript][NameManglerAnalysis] Serialized global stores reserve replacement spellings") {
	const String scene_path = TestUtils::get_temp_path("name_analysis_global_store.tscn");
	{
		Ref<FileAccess> scene_file = FileAccess::open(scene_path, FileAccess::WRITE);
		REQUIRE(scene_file.is_valid());
		scene_file->store_string("[gd_scene format=3]\n\n[node name=\"Root\" type=\"Node\"]\n");
	}
	const StringName autoload_name = SNAME("_fsb_0");
	ProjectSettings::AutoloadInfo autoload;
	autoload.name = autoload_name;
	autoload.path = scene_path;
	autoload.is_singleton = true;
	ProjectSettings::get_singleton()->add_autoload(autoload);
	FSLanguage::get_singleton()->add_global_constant(autoload_name, Variant());

	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"var collision_candidate: int\n"
			"func load_global() -> Variant:\n"
			"\treturn _fsb_0\n");
	ProjectSettings::get_singleton()->remove_autoload(autoload_name);
	DirAccess::remove_absolute(scene_path);

	const FSFunction *function = script->get_member_functions()[SNAME("load_global")];
	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(script);
	const FSNameManglerAnalysis::Result result = FSNameManglerAnalysis::analyze(input);
	TestFSLanguageGlobalsAccessor::remove_global(autoload_name);

	REQUIRE(function != nullptr);
	if (function != nullptr) {
		REQUIRE_EQ(function->export_fixups.global_stores.size(), 1);
		CHECK_EQ(function->export_fixups.global_stores[0].global_name, autoload_name);
		CHECK_EQ(function->get_global_names_count(), 0);
	}
	REQUIRE(result.error == OK);
	REQUIRE(result.rename_map.has(SNAME("collision_candidate")));
	CHECK_NE(result.rename_map[SNAME("collision_candidate")], autoload_name);
}

TEST_CASE("[FoundryScript][NameManglerAnalysis] Empty and invalid inputs have no partial map") {
	const FSNameManglerAnalysis::Result empty = FSNameManglerAnalysis::analyze(FSNameManglerAnalysis::Input());
	CHECK_EQ(empty.error, OK);
	CHECK(empty.classifications.is_empty());
	CHECK(empty.rename_map.is_empty());
	CHECK(empty.keep_log.is_empty());

	FSNameManglerAnalysis::Input invalid_input;
	invalid_input.scripts.push_back(Ref<FoundryScript>());
	const FSNameManglerAnalysis::Result invalid = FSNameManglerAnalysis::analyze(invalid_input);
	CHECK_EQ(invalid.error, ERR_INVALID_PARAMETER);
	CHECK(invalid.classifications.is_empty());
	CHECK(invalid.rename_map.is_empty());
	CHECK(invalid.keep_log.is_empty());
}

TEST_CASE("[FoundryScript][NameManglerAnalysis] Reflection enumeration keeps the relevant declaration set") {
	struct ReflectionCase {
		const char *enumerator;
		const char *call_expression;
		StringName kept_name;
		StringName mangled_name_a;
		StringName mangled_name_b;
	};
	const ReflectionCase cases[] = {
		{ "get_method_list", "get_method_list()", SNAME("reflected_method"), SNAME("reflected_member"), SNAME("reflected_signal") },
		{ "get_property_list", "get_property_list()", SNAME("reflected_member"), SNAME("reflected_method"), SNAME("reflected_signal") },
		{ "get_signal_list", "get_signal_list()", SNAME("reflected_signal"), SNAME("reflected_method"), SNAME("reflected_member") },
		{ "FSReflection.get_methods", "foundry.reflection.get_methods(self)", SNAME("reflected_method"), SNAME("reflected_member"), SNAME("reflected_signal") },
		{ "FSReflection.get_method_descriptors", "foundry.reflection.get_method_descriptors(self)", SNAME("reflected_method"), SNAME("reflected_member"), SNAME("reflected_signal") },
		{ "FSReflection.get_properties", "foundry.reflection.get_properties(self)", SNAME("reflected_member"), SNAME("reflected_method"), SNAME("reflected_signal") },
		{ "FSReflection.get_property_descriptors", "foundry.reflection.get_property_descriptors(self)", SNAME("reflected_member"), SNAME("reflected_method"), SNAME("reflected_signal") },
	};

	for (const ReflectionCase &test_case : cases) {
		const Ref<FoundryScript> script = compile_bytecode_test_source(vformat(
				"extends RefCounted\n"
				"var reflected_member: int\n"
				"signal reflected_signal\n"
				"func reflected_method() -> void:\n"
				"\tpass\n"
				"func inspect_surface() -> void:\n"
				"\t%s\n",
				test_case.call_expression));
		FSNameManglerAnalysis::Input input;
		input.scripts.push_back(script);
		const FSNameManglerAnalysis::Result result = FSNameManglerAnalysis::analyze(input);

		REQUIRE(result.error == OK);
		CAPTURE(test_case.enumerator);
		CHECK_FALSE(result.rename_map.has(test_case.kept_name));
		CHECK(name_analysis_has_reason(result, test_case.kept_name, FSNameManglerAnalysis::KEEP_REFLECTION));
		CHECK(result.rename_map.has(test_case.mangled_name_a));
		CHECK(result.rename_map.has(test_case.mangled_name_b));
	}
}

TEST_CASE("[FoundryScript][NameMangler][Reflection][Analysis] Self enumeration scopes retention to the receiver hierarchy") {
	const StringName base_method = SNAME("reflection_scope_base_method_1237");
	const StringName owner_method = SNAME("reflection_scope_owner_method_1237");
	const StringName unrelated_method = SNAME("reflection_scope_unrelated_method_1237");
	const StringName shared_method = SNAME("reflection_scope_shared_method_1237");

	const Ref<FoundryScript> base = compile_bytecode_test_source(
			"extends RefCounted\n"
			"func reflection_scope_base_method_1237() -> void:\n"
			"\tpass\n");
	const Ref<FoundryScript> owner = compile_bytecode_test_source(vformat(
			"extends \"%s\"\n"
			"func reflection_scope_owner_method_1237() -> void:\n"
			"\tpass\n"
			"func reflection_scope_shared_method_1237() -> void:\n"
			"\tpass\n"
			"func inspect_reflection_scope_1237() -> void:\n"
			"\tget_method_list()\n",
			base->get_script_path()));
	const Ref<FoundryScript> unrelated = compile_bytecode_test_source(
			"extends RefCounted\n"
			"func reflection_scope_unrelated_method_1237() -> void:\n"
			"\tpass\n"
			"func reflection_scope_shared_method_1237() -> void:\n"
			"\tpass\n");

	FSNameManglerAnalysis::Input forward_input;
	forward_input.scripts.push_back(base);
	forward_input.scripts.push_back(owner);
	forward_input.scripts.push_back(unrelated);
	const FSNameManglerAnalysis::Result forward =
			FSNameManglerAnalysis::analyze(forward_input);

	FSNameManglerAnalysis::Input reverse_input;
	reverse_input.scripts.push_back(unrelated);
	reverse_input.scripts.push_back(owner);
	reverse_input.scripts.push_back(base);
	const FSNameManglerAnalysis::Result reverse =
			FSNameManglerAnalysis::analyze(reverse_input);

	REQUIRE_EQ(forward.error, OK);
	REQUIRE_EQ(reverse.error, OK);
	CHECK_EQ(name_analysis_snapshot(forward), name_analysis_snapshot(reverse));
	CHECK_FALSE(forward.rename_map.has(base_method));
	CHECK(name_analysis_has_reason(
			forward, base_method, FSNameManglerAnalysis::KEEP_REFLECTION));
	CHECK_FALSE(forward.rename_map.has(owner_method));
	CHECK(name_analysis_has_reason(
			forward, owner_method, FSNameManglerAnalysis::KEEP_REFLECTION));
	CHECK(forward.rename_map.has(unrelated_method));
	CHECK_FALSE(name_analysis_has_reason(
			forward, unrelated_method, FSNameManglerAnalysis::KEEP_REFLECTION));
	CHECK_FALSE(forward.rename_map.has(shared_method));
	CHECK(name_analysis_has_reason(
			forward, shared_method, FSNameManglerAnalysis::KEEP_REFLECTION));

	const StringName unresolved_method_a =
			SNAME("reflection_scope_unresolved_method_a_1237");
	const StringName unresolved_method_b =
			SNAME("reflection_scope_unresolved_method_b_1237");
	const Ref<FoundryScript> unresolved_declarations =
			compile_bytecode_test_source(
					"extends RefCounted\n"
					"func reflection_scope_unresolved_method_a_1237() -> void:\n"
					"\tpass\n"
					"func reflection_scope_unresolved_method_b_1237() -> void:\n"
					"\tpass\n");
	const Ref<FoundryScript> unresolved_caller =
			compile_bytecode_test_source(
					"extends RefCounted\n"
					"func inspect_unresolved_scope_1237(target: Object) -> void:\n"
					"\ttarget.get_method_list()\n");
	FSNameManglerAnalysis::Input unresolved_input;
	unresolved_input.scripts.push_back(unresolved_declarations);
	unresolved_input.scripts.push_back(unresolved_caller);
	const FSNameManglerAnalysis::Result unresolved =
			FSNameManglerAnalysis::analyze(unresolved_input);
	REQUIRE_EQ(unresolved.error, OK);
	CHECK_FALSE(unresolved.rename_map.has(unresolved_method_a));
	CHECK_FALSE(unresolved.rename_map.has(unresolved_method_b));
	CHECK(name_analysis_has_reason(
			unresolved, unresolved_method_a,
			FSNameManglerAnalysis::KEEP_REFLECTION));
	CHECK(name_analysis_has_reason(
			unresolved, unresolved_method_b,
			FSNameManglerAnalysis::KEEP_REFLECTION));
}

TEST_CASE("[FoundryScript][NameMangler][Reflection][Analysis] Base-owned self enumeration keeps derived declarations") {
	const StringName derived_method =
			SNAME("reflection_dynamic_derived_method_1237");
	const StringName derived_property =
			SNAME("reflection_dynamic_derived_property_1237");
	const StringName derived_signal =
			SNAME("reflection_dynamic_derived_signal_1237");
	const StringName unrelated_method =
			SNAME("reflection_dynamic_unrelated_method_1237");
	const StringName unrelated_property =
			SNAME("reflection_dynamic_unrelated_property_1237");
	const StringName unrelated_signal =
			SNAME("reflection_dynamic_unrelated_signal_1237");

	const Ref<FoundryScript> base = compile_bytecode_test_source(
			"extends RefCounted\n"
			"var reflection_dynamic_base_property_1237: int\n"
			"signal reflection_dynamic_base_signal_1237\n"
			"func reflection_dynamic_base_method_1237() -> void:\n"
			"\tpass\n"
			"func inspect_reflection_dynamic_methods_1237() -> void:\n"
			"\tget_method_list()\n"
			"func inspect_reflection_dynamic_properties_1237() -> void:\n"
			"\tget_property_list()\n"
			"func inspect_reflection_dynamic_signals_1237() -> void:\n"
			"\tget_signal_list()\n");
	const Ref<FoundryScript> derived = compile_bytecode_test_source(vformat(
			"extends \"%s\"\n"
			"var reflection_dynamic_derived_property_1237: int\n"
			"signal reflection_dynamic_derived_signal_1237\n"
			"func reflection_dynamic_derived_method_1237() -> void:\n"
			"\tpass\n",
			base->get_script_path()));
	const Ref<FoundryScript> unrelated = compile_bytecode_test_source(
			"extends RefCounted\n"
			"var reflection_dynamic_unrelated_property_1237: int\n"
			"signal reflection_dynamic_unrelated_signal_1237\n"
			"func reflection_dynamic_unrelated_method_1237() -> void:\n"
			"\tpass\n");

	FSNameManglerAnalysis::Input forward_input;
	forward_input.scripts.push_back(base);
	forward_input.scripts.push_back(derived);
	forward_input.scripts.push_back(unrelated);
	const FSNameManglerAnalysis::Result forward =
			FSNameManglerAnalysis::analyze(forward_input);
	FSNameManglerAnalysis::Input reverse_input;
	reverse_input.scripts.push_back(unrelated);
	reverse_input.scripts.push_back(derived);
	reverse_input.scripts.push_back(base);
	const FSNameManglerAnalysis::Result reverse =
			FSNameManglerAnalysis::analyze(reverse_input);
	REQUIRE_EQ(forward.error, OK);
	REQUIRE_EQ(reverse.error, OK);
	CHECK_EQ(name_analysis_snapshot(forward), name_analysis_snapshot(reverse));

	const StringName derived_names[] = {
		derived_method,
		derived_property,
		derived_signal,
	};
	const StringName unrelated_names[] = {
		unrelated_method,
		unrelated_property,
		unrelated_signal,
	};
	for (int i = 0; i < 3; i++) {
		CAPTURE(derived_names[i]);
		CHECK_FALSE(forward.rename_map.has(derived_names[i]));
		CHECK(name_analysis_has_reason(
				forward, derived_names[i],
				FSNameManglerAnalysis::KEEP_REFLECTION));
		CAPTURE(unrelated_names[i]);
		CHECK(forward.rename_map.has(unrelated_names[i]));
		CHECK_FALSE(name_analysis_has_reason(
				forward, unrelated_names[i],
				FSNameManglerAnalysis::KEEP_REFLECTION));
	}
}

TEST_CASE("[FoundryScript][NameManglerAnalysis] Project ordering and keep evidence produce one stable global decision") {
	const Ref<FoundryScript> first = compile_bytecode_test_source(
			"var shared_name: int\n"
			"var _fsb_0: int\n"
			"func alpha_helper() -> void:\n"
			"\tpass\n");
	const Ref<FoundryScript> second = compile_bytecode_test_source(
			"signal beta_signal\n"
			"func shared_name() -> void:\n"
			"\tpass\n");

	FSNameManglerAnalysis::Input forward_input;
	forward_input.scripts.push_back(first);
	forward_input.scripts.push_back(second);
	const FSNameManglerAnalysis::Result forward = FSNameManglerAnalysis::analyze(forward_input);

	FSNameManglerAnalysis::Input reverse_input;
	reverse_input.scripts.push_back(second);
	reverse_input.scripts.push_back(first);
	const FSNameManglerAnalysis::Result reverse = FSNameManglerAnalysis::analyze(reverse_input);

	REQUIRE(forward.error == OK);
	REQUIRE(reverse.error == OK);
	CHECK_EQ(name_analysis_snapshot(forward), name_analysis_snapshot(reverse));
	CHECK(forward.rename_map.has(SNAME("shared_name")));
	for (const KeyValue<StringName, StringName> &rename : forward.rename_map) {
		CHECK_NE(rename.value, SNAME("_fsb_0"));
	}

	forward_input.add_keep(SNAME("shared_name"), FSNameManglerAnalysis::KEEP_RULE, "keep-names.cfg:4");
	const FSNameManglerAnalysis::Result kept = FSNameManglerAnalysis::analyze(forward_input);
	REQUIRE(kept.error == OK);
	CHECK_FALSE(kept.rename_map.has(SNAME("shared_name")));
	CHECK(name_analysis_has_reason(kept, SNAME("shared_name"), FSNameManglerAnalysis::KEEP_RULE));
	REQUIRE(kept.keep_log.size() == 1);
	CHECK_EQ(kept.keep_log[0], "Keeping \"shared_name\": explicit keep rule (keep-names.cfg:4).");
}

TEST_CASE("[FoundryScript][NameManglerAnalysis] keep_name and keep rules preserve dynamic dispatch escapes") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"@keep_name\n"
			"func annotated_dispatch() -> void:\n"
			"\tpass\n"
			"func ruled_dispatch() -> void:\n"
			"\tpass\n"
			"func unkept_dispatch() -> void:\n"
			"\tpass\n"
			"enum DynamicDispatch:\n"
			"\tVALUE = 0\n"
			"\t@keep_name func enum_instance_dispatch() -> void:\n"
			"\t\tpass\n"
			"\t@keep_name static func enum_static_dispatch() -> void:\n"
			"\t\tpass\n"
			"func invoke(suffix: String) -> void:\n"
			"\tcall(\"annotated_\" + suffix)\n"
			"\tcall(\"ruled_\" + suffix)\n"
			"\tcall(\"unkept_\" + suffix)\n");

	FSNameManglerKeepRules rules;
	Vector<FSNameManglerKeepRules::Diagnostic> diagnostics;
	CHECK_EQ(FSNameManglerKeepRules::parse(
					 "-keepclassmembers class ** {\n"
					 "\truled_dispatch;\n"
					 "}\n",
					 "res://analysis-keep.pro", rules, diagnostics),
			OK);
	CHECK(diagnostics.is_empty());

	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(script);
	CHECK_EQ(rules.apply_to_input(input, diagnostics), OK);
	CHECK(diagnostics.is_empty());
	const FSNameManglerAnalysis::Result result = FSNameManglerAnalysis::analyze(input);
	REQUIRE(result.error == OK);

	const FSNameManglerAnalysis::Classification *annotated = result.find(SNAME("annotated_dispatch"));
	const FSNameManglerAnalysis::Classification *ruled = result.find(SNAME("ruled_dispatch"));
	const FSNameManglerAnalysis::Classification *unkept = result.find(SNAME("unkept_dispatch"));
	const FSNameManglerAnalysis::Classification *enum_instance = result.find(SNAME("enum_instance_dispatch"));
	const FSNameManglerAnalysis::Classification *enum_static = result.find(SNAME("enum_static_dispatch"));
	REQUIRE(annotated != nullptr);
	REQUIRE(ruled != nullptr);
	REQUIRE(unkept != nullptr);
	REQUIRE(enum_instance != nullptr);
	REQUIRE(enum_static != nullptr);

	bool annotated_detail_found = false;
	for (const FSNameManglerAnalysis::KeepEvidence &evidence : annotated->keep_evidence) {
		if (evidence.reason == FSNameManglerAnalysis::KEEP_RULE &&
				evidence.detail.contains("@keep_name") &&
				evidence.detail.ends_with("::annotated_dispatch")) {
			annotated_detail_found = true;
		}
	}
	bool ruled_detail_found = false;
	for (const FSNameManglerAnalysis::KeepEvidence &evidence : ruled->keep_evidence) {
		if (evidence.reason == FSNameManglerAnalysis::KEEP_RULE &&
				evidence.detail.begins_with("res://analysis-keep.pro:1 -keepclassmembers") &&
				evidence.detail.ends_with("::ruled_dispatch")) {
			ruled_detail_found = true;
		}
	}
	CHECK(annotated_detail_found);
	CHECK(ruled_detail_found);
	CHECK_FALSE(result.rename_map.has(SNAME("annotated_dispatch")));
	CHECK_FALSE(result.rename_map.has(SNAME("ruled_dispatch")));
	CHECK(name_analysis_has_reason(result, SNAME("enum_instance_dispatch"), FSNameManglerAnalysis::KEEP_RULE));
	CHECK(name_analysis_has_reason(result, SNAME("enum_static_dispatch"), FSNameManglerAnalysis::KEEP_RULE));
	CHECK_FALSE(result.rename_map.has(SNAME("enum_instance_dispatch")));
	CHECK_FALSE(result.rename_map.has(SNAME("enum_static_dispatch")));
	CHECK(result.rename_map.has(SNAME("unkept_dispatch")));
}

TEST_CASE("[FoundryScript][NameManglerAnalysis] Incomplete and external graph boundaries are kept") {
	const Ref<FoundryScript> incomplete_script = compile_bytecode_test_source(
			"var private_member: int\n"
			"func private_method() -> void:\n"
			"\tpass\n");
	FSNameManglerAnalysis::Input incomplete_input;
	incomplete_input.scripts.push_back(incomplete_script);
	incomplete_input.complete_project_graph = false;
	const FSNameManglerAnalysis::Result incomplete = FSNameManglerAnalysis::analyze(incomplete_input);
	REQUIRE(incomplete.error == OK);
	CHECK(incomplete.rename_map.is_empty());
	for (const FSNameManglerAnalysis::Classification &classification : incomplete.classifications) {
		CHECK(name_analysis_has_reason(incomplete, classification.name,
				FSNameManglerAnalysis::KEEP_EXTERNAL_OR_UNPROVABLE));
	}

	const Ref<FoundryScript> external = compile_bytecode_test_source(
			"func shared_external() -> void:\n"
			"\tpass\n");
	const Ref<FoundryScript> host = compile_bytecode_test_source(vformat(
			"const External = preload(\"%s\")\n"
			"func shared_external() -> void:\n"
			"\tpass\n",
			external->get_script_path()));
	FSNameManglerAnalysis::Input external_input;
	external_input.scripts.push_back(host);
	const FSNameManglerAnalysis::Result external_result = FSNameManglerAnalysis::analyze(external_input);
	REQUIRE(external_result.error == OK);
	CHECK_FALSE(external_result.rename_map.has(SNAME("shared_external")));
	CHECK(name_analysis_has_reason(external_result, SNAME("shared_external"),
			FSNameManglerAnalysis::KEEP_EXTERNAL_OR_UNPROVABLE));

	const Ref<FoundryScript> external_base = compile_bytecode_test_source(
			"func inherited_external() -> void:\n"
			"\tpass\n");
	const Ref<FoundryScript> derived = compile_bytecode_test_source(vformat(
			"extends \"%s\"\n"
			"func inherited_external() -> void:\n"
			"\tpass\n",
			external_base->get_script_path()));
	FSNameManglerAnalysis::Input derived_input;
	derived_input.scripts.push_back(derived);
	const FSNameManglerAnalysis::Result derived_result = FSNameManglerAnalysis::analyze(derived_input);
	REQUIRE(derived_result.error == OK);
	CHECK_FALSE(derived_result.rename_map.has(SNAME("inherited_external")));
	CHECK(name_analysis_has_reason(derived_result, SNAME("inherited_external"),
			FSNameManglerAnalysis::KEEP_EXTERNAL_OR_UNPROVABLE));
}

} // namespace FSTests

#endif // TOOLS_ENABLED
