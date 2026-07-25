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

} // namespace FSTests

#endif // TOOLS_ENABLED
