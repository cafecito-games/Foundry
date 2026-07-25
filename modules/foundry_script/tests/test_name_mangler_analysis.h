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
#include "modules/foundry_script/tests/test_bytecode_serialization.h"

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
