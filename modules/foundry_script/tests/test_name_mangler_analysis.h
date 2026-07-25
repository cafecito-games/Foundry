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

TEST_CASE("[FoundryScript][NameManglerAnalysis] Classifies a compiled project conservatively") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"extends Node\n"
			"@export var scene_property: int\n"
			"var private_member: int\n"
			"signal scene_signal\n"
			"signal private_signal\n"
			"enum Mode { IDLE, ACTIVE }\n"
			"class PrivateNested:\n"
			"\tvar nested_member: int\n"
			"@rpc func remote_call() -> void:\n"
			"\tpass\n"
			"func _process(_delta: double) -> void:\n"
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

} // namespace FSTests

#endif // TOOLS_ENABLED
