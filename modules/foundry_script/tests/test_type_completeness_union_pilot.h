/**************************************************************************/
/*  test_type_completeness_union_pilot.h                                 */
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

#include "fs_type_completeness_union_adapter.h"

#include "tests/test_macros.h"

namespace FSTests {

static const String type_completeness_union_pilot_root = "modules/foundry_script/tests/type_completeness";

static FSCompletenessResolution load_union_pilot_completeness_resolution() {
	FSCompletenessCatalog catalog;
	FSCompletenessManifest manifest;
	Vector<String> errors;
	REQUIRE_MESSAGE(catalog.load(type_completeness_union_pilot_root, errors) == OK, String(" | ").join(errors));
	REQUIRE_MESSAGE(FSCompletenessManifest::load(
							type_completeness_union_pilot_root.path_join("rules/union_destination_membership.json"), manifest, errors) == OK,
			String(" | ").join(errors));
	REQUIRE_MESSAGE(validate_manifest_vocabulary(manifest, catalog, errors) == OK, String(" | ").join(errors));

	FSCompletenessResolution resolution;
	REQUIRE_MESSAGE(FSCompletenessGraph::resolve(manifest, catalog, resolution, errors) == OK,
			String(" | ").join(errors));
	return resolution;
}

static void check_union_pilot_program_cleared(const FSCompletenessProgram &p_program) {
	CHECK(p_program.case_id.is_empty());
	CHECK(p_program.surface.is_empty());
	CHECK(p_program.source.is_empty());
	CHECK(p_program.expected_output.is_empty());
}

static FSCompletenessProgram stale_union_pilot_program() {
	FSCompletenessProgram program;
	program.case_id = "stale_case";
	program.surface = "stale_surface";
	program.source = "stale_source";
	program.expected_output = "stale_output";
	return program;
}

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness][UnionPilot]") {
	TEST_CASE("TypeCompleteness UnionPilot renders and observes all resolved static semantics") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		REQUIRE_EQ(resolution.cells.size(), 40);

		for (const FSCompletenessResolvedCell &cell : resolution.cells) {
			CAPTURE(cell.case_id);
			const FSCompletenessResolvedDimension *expected_analysis = cell.find_dimension("analysis");
			REQUIRE(expected_analysis != nullptr);

			FSCompletenessProgram program;
			REQUIRE_EQ(FSUnionCompletenessAdapter::render(cell, program), OK);
			CAPTURE(program.source);
			const String surface = cell.coordinates.get("surface", String());
			CHECK_EQ(program.case_id, cell.case_id);
			CHECK_EQ(program.surface, surface);
			CHECK_EQ(program.expected_output, "uint 5\n");

			const FSCompletenessObservation observation = FSUnionCompletenessAdapter::analyze(program, surface);
			CHECK_EQ(observation.case_id, cell.case_id);
			CHECK_EQ(observation.surface, surface);
			CHECK_EQ(String(observation.dimensions.get("analysis", String())), String(expected_analysis->expected));
			CHECK(observation.diagnostics.is_empty());
		}
	}

	TEST_CASE("TypeCompleteness UnionPilot render rejects missing unknown and wrongly typed coordinates") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		REQUIRE_FALSE(resolution.cells.is_empty());
		const FSCompletenessResolvedCell &valid = resolution.cells[0];
		const Vector<String> axes = { "destination", "source_proof", "boundary", "surface" };

		for (const String &axis : axes) {
			CAPTURE(axis);
			FSCompletenessResolvedCell missing = valid;
			missing.coordinates = valid.coordinates.duplicate();
			missing.coordinates.erase(axis);
			FSCompletenessProgram program = stale_union_pilot_program();
			CHECK_EQ(FSUnionCompletenessAdapter::render(missing, program), ERR_INVALID_DATA);
			check_union_pilot_program_cleared(program);

			FSCompletenessResolvedCell unknown = valid;
			unknown.coordinates = valid.coordinates.duplicate();
			unknown.coordinates[axis] = "unknown";
			program = stale_union_pilot_program();
			CHECK_EQ(FSUnionCompletenessAdapter::render(unknown, program), ERR_INVALID_DATA);
			check_union_pilot_program_cleared(program);

			FSCompletenessResolvedCell wrong_type = valid;
			wrong_type.coordinates = valid.coordinates.duplicate();
			wrong_type.coordinates[axis] = 7;
			program = stale_union_pilot_program();
			CHECK_EQ(FSUnionCompletenessAdapter::render(wrong_type, program), ERR_INVALID_DATA);
			check_union_pilot_program_cleared(program);
		}

		FSCompletenessResolvedCell empty_case = valid;
		empty_case.case_id.clear();
		FSCompletenessProgram program = stale_union_pilot_program();
		CHECK_EQ(FSUnionCompletenessAdapter::render(empty_case, program), ERR_INVALID_DATA);
		check_union_pilot_program_cleared(program);
	}

	TEST_CASE("TypeCompleteness UnionPilot witnesses resolve closed complete coordinates") {
		struct WitnessExpectation {
			const char *id;
			const char *destination;
			const char *source_proof;
			const char *boundary;
			const char *surface;
		};
		const WitnessExpectation expectations[] = {
			{ "text_gradual_argument_binding", "union", "gradual", "argument_binding", "text" },
			{ "bytecode_erased_reflective_write", "union", "erased", "reflective_write", "bytecode" },
			{ "text_static_member_argument_binding", "union", "static_member", "argument_binding", "text" },
		};

		for (const WitnessExpectation &expectation : expectations) {
			CAPTURE(expectation.id);
			Dictionary coordinates;
			REQUIRE_EQ(FSUnionCompletenessAdapter::witness_coordinates(expectation.id, coordinates), OK);
			CHECK_EQ(coordinates.size(), 4);
			CHECK_EQ(String(coordinates.get("destination", String())), expectation.destination);
			CHECK_EQ(String(coordinates.get("source_proof", String())), expectation.source_proof);
			CHECK_EQ(String(coordinates.get("boundary", String())), expectation.boundary);
			CHECK_EQ(String(coordinates.get("surface", String())), expectation.surface);
		}

		Dictionary unknown;
		unknown["stale"] = true;
		CHECK_EQ(FSUnionCompletenessAdapter::witness_coordinates("unknown", unknown), ERR_DOES_NOT_EXIST);
		CHECK(unknown.is_empty());
	}

	TEST_CASE("TypeCompleteness UnionPilot analysis rejects malformed parser input with diagnostics") {
		FSCompletenessProgram malformed;
		malformed.case_id = "malformed_parser_input";
		malformed.surface = "text";
		malformed.source = "func test(:\n";

		const FSCompletenessObservation observation = FSUnionCompletenessAdapter::analyze(malformed, "text");
		CHECK_EQ(observation.case_id, malformed.case_id);
		CHECK_EQ(observation.surface, malformed.surface);
		CHECK_EQ(String(observation.dimensions.get("analysis", String())), "reject");
		CHECK_FALSE(observation.diagnostics.is_empty());
	}

	TEST_CASE("TypeCompleteness UnionPilot analysis rejects analyzer invalid input with diagnostics") {
		FSCompletenessProgram invalid;
		invalid.case_id = "invalid_static_assignment";
		invalid.surface = "bytecode";
		invalid.source = "func test() -> void:\n\tvar value: int = \"not an integer\"\n";

		const FSCompletenessObservation observation = FSUnionCompletenessAdapter::analyze(invalid, "bytecode");
		CHECK_EQ(observation.case_id, invalid.case_id);
		CHECK_EQ(observation.surface, invalid.surface);
		CHECK_EQ(String(observation.dimensions.get("analysis", String())), "reject");
		CHECK_FALSE(observation.diagnostics.is_empty());
	}

	TEST_CASE("TypeCompleteness UnionPilot analysis rejects unknown and mismatched surfaces") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		const FSCompletenessResolvedCell *text_cell = nullptr;
		for (const FSCompletenessResolvedCell &cell : resolution.cells) {
			if (cell.coordinates.get("surface", String()) == "text") {
				text_cell = &cell;
				break;
			}
		}
		REQUIRE(text_cell != nullptr);
		FSCompletenessProgram program;
		REQUIRE_EQ(FSUnionCompletenessAdapter::render(*text_cell, program), OK);

		const FSCompletenessObservation mismatch = FSUnionCompletenessAdapter::analyze(program, "bytecode");
		CHECK_EQ(mismatch.case_id, program.case_id);
		CHECK_EQ(mismatch.surface, "bytecode");
		CHECK_EQ(String(mismatch.dimensions.get("analysis", String())), "reject");
		CHECK_FALSE(mismatch.diagnostics.is_empty());

		const FSCompletenessObservation unknown = FSUnionCompletenessAdapter::analyze(program, "unknown");
		CHECK_EQ(unknown.case_id, program.case_id);
		CHECK_EQ(unknown.surface, "unknown");
		CHECK_EQ(String(unknown.dimensions.get("analysis", String())), "reject");
		CHECK_FALSE(unknown.diagnostics.is_empty());
	}
}

} // namespace FSTests
