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

#include "../fs_analyzer.h"
#include "../fs_cache.h"
#include "../fs_parser.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/file_access_pack.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/templates/safe_refcount.h"
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

struct UnionPilotSemanticFingerprint {
	String destination;
	String source_proof;
	String boundary;
};

struct UnionPilotCacheCleanup {
	String path;

	explicit UnionPilotCacheCleanup(const String &p_path) :
			path(p_path) {}

	~UnionPilotCacheCleanup() {
		FSCache::remove_parser(path);
		FSCache::remove_script(path);
	}
};

struct UnionPilotPackedMarker {
	String path;

	explicit UnionPilotPackedMarker(const String &p_path) :
			path(p_path) {
		uint8_t marker_md5[16] = {};
		PackedData::get_singleton()->add_path(String(), path, 1, 0, marker_md5, nullptr, false);
	}

	~UnionPilotPackedMarker() {
		PackedData::get_singleton()->remove_path(path);
	}
};

static const FSParser::FunctionNode *union_pilot_find_function(
		const FSParser::ClassNode *p_class, const StringName &p_name) {
	if (p_class == nullptr || !p_class->has_member(p_name)) {
		return nullptr;
	}
	const FSParser::ClassNode::Member &member = p_class->get_member(p_name);
	return member.type == FSParser::ClassNode::Member::FUNCTION ? member.function : nullptr;
}

static const FSParser::VariableNode *union_pilot_find_local(
		const FSParser::FunctionNode *p_function, const StringName &p_name) {
	if (p_function == nullptr || p_function->body == nullptr) {
		return nullptr;
	}
	for (const FSParser::Node *statement : p_function->body->statements) {
		if (statement == nullptr || statement->type != FSParser::Node::VARIABLE) {
			continue;
		}
		const FSParser::VariableNode *variable = static_cast<const FSParser::VariableNode *>(statement);
		if (variable->identifier != nullptr && variable->identifier->name == p_name) {
			return variable;
		}
	}
	return nullptr;
}

static bool union_pilot_is_identifier(const FSParser::ExpressionNode *p_expression, const StringName &p_name) {
	return p_expression != nullptr && p_expression->type == FSParser::Node::IDENTIFIER &&
			static_cast<const FSParser::IdentifierNode *>(p_expression)->name == p_name;
}

static bool union_pilot_is_unsigned_five(const FSParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr || p_expression->type != FSParser::Node::LITERAL) {
		return false;
	}
	const FSParser::LiteralNode *literal = static_cast<const FSParser::LiteralNode *>(p_expression);
	return literal->value.get_type() == Variant::UINT && uint64_t(literal->value) == 5 &&
			literal->numeric_type_is_explicit && literal->numeric_type == NumericType::UINT32;
}

static String union_pilot_destination_fingerprint(const FSParser::DataType &p_type) {
	if (p_type.kind == FSParser::DataType::BUILTIN && p_type.builtin_type == Variant::UINT) {
		return "plain";
	}
	if (p_type.kind != FSParser::DataType::UNION || p_type.union_members.size() != 2) {
		return "invalid";
	}
	bool has_uint = false;
	bool has_string = false;
	for (const FSParser::DataType &member : p_type.union_members) {
		has_uint = has_uint || (member.kind == FSParser::DataType::BUILTIN && member.builtin_type == Variant::UINT);
		has_string = has_string || (member.kind == FSParser::DataType::BUILTIN && member.builtin_type == Variant::STRING);
	}
	return has_uint && has_string ? "union" : "invalid";
}

static String union_pilot_source_fingerprint(const FSParser::ExpressionNode *p_expression) {
	if (union_pilot_is_identifier(p_expression, SNAME("typed_source"))) {
		return "static_member";
	}
	if (union_pilot_is_identifier(p_expression, SNAME("variant_source"))) {
		return "variant";
	}
	if (p_expression != nullptr && p_expression->type == FSParser::Node::LITERAL) {
		const FSParser::LiteralNode *literal = static_cast<const FSParser::LiteralNode *>(p_expression);
		if (literal->value.get_type() == Variant::INT && int64_t(literal->value) == 5 &&
				!literal->numeric_type_is_explicit) {
			return "numeric_constant";
		}
	}
	if (p_expression == nullptr || p_expression->type != FSParser::Node::CALL) {
		return "invalid";
	}
	const FSParser::CallNode *call = static_cast<const FSParser::CallNode *>(p_expression);
	if (call->function_name == SNAME("supply") && call->arguments.size() == 1 &&
			union_pilot_is_identifier(call->callee, SNAME("supply")) &&
			union_pilot_is_unsigned_five(call->arguments[0])) {
		return "gradual";
	}
	if (call->function_name != SNAME("erase") || call->arguments.size() != 1 ||
			!union_pilot_is_unsigned_five(call->arguments[0]) || call->callee == nullptr ||
			call->callee->type != FSParser::Node::SUBSCRIPT) {
		return "invalid";
	}
	const FSParser::SubscriptNode *application = static_cast<const FSParser::SubscriptNode *>(call->callee);
	if (application->is_attribute || !union_pilot_is_identifier(application->base, SNAME("erase")) ||
			!union_pilot_is_identifier(application->index, SNAME("uint"))) {
		return "invalid";
	}
	return "erased";
}

static UnionPilotSemanticFingerprint union_pilot_semantic_fingerprint(const FSCompletenessProgram &p_program) {
	static SafeNumeric<uint64_t> sequence;
	static Mutex analysis_mutex;
	MutexLock lock(analysis_mutex);
	const String path = vformat("user://type_completeness/fingerprint/%d/%s/%d.fs",
			OS::get_singleton()->get_process_id(), p_program.case_id.sha256_text().substr(0, 24), sequence.increment());
	UnionPilotCacheCleanup cleanup(path);
	UnionPilotPackedMarker marker(path);
	HashMap<String, String> overrides;
	overrides[path] = p_program.source;
	FSCacheSourceOverrideGuard source_override(overrides);
	FSCache::remove_parser(path);
	FSCache::remove_script(path);

	FSParser parser;
	REQUIRE_EQ(parser.parse(p_program.source, path, false), OK);
	FSAnalyzer analyzer(&parser);
	REQUIRE_EQ(analyzer.analyze(), OK);
	REQUIRE(parser.get_errors().is_empty());

	UnionPilotSemanticFingerprint fingerprint;
	const FSParser::ClassNode *root_class = parser.get_tree();
	const FSParser::FunctionNode *accept = union_pilot_find_function(root_class, SNAME("accept"));
	REQUIRE(accept != nullptr);
	REQUIRE_EQ(accept->parameters.size(), 1);
	fingerprint.destination = union_pilot_destination_fingerprint(accept->parameters[0]->get_datatype());

	const FSParser::FunctionNode *test = union_pilot_find_function(root_class, SNAME("test"));
	REQUIRE(test != nullptr);
	const FSParser::VariableNode *stored = union_pilot_find_local(test, SNAME("stored"));
	REQUIRE(stored != nullptr);
	const FSParser::ExpressionNode *source_expression = nullptr;
	if (stored->initializer != nullptr && stored->initializer->type == FSParser::Node::CALL) {
		const FSParser::CallNode *accept_call = static_cast<const FSParser::CallNode *>(stored->initializer);
		REQUIRE_EQ(accept_call->function_name, SNAME("accept"));
		REQUIRE(union_pilot_is_identifier(accept_call->callee, SNAME("accept")));
		REQUIRE_EQ(accept_call->arguments.size(), 1);
		source_expression = accept_call->arguments[0];
		fingerprint.boundary = "argument_binding";
	} else {
		const FSParser::VariableNode *holder = union_pilot_find_local(test, SNAME("holder"));
		REQUIRE(holder != nullptr);
		REQUIRE(holder->initializer != nullptr);
		REQUIRE_EQ(holder->initializer->type, FSParser::Node::CALL);
		const FSParser::CallNode *constructor = static_cast<const FSParser::CallNode *>(holder->initializer);
		REQUIRE_EQ(constructor->function_name, SNAME("new"));
		REQUIRE(constructor->callee != nullptr);
		REQUIRE_EQ(constructor->callee->type, FSParser::Node::SUBSCRIPT);
		const FSParser::SubscriptNode *new_attribute = static_cast<const FSParser::SubscriptNode *>(constructor->callee);
		REQUIRE(new_attribute->is_attribute);
		REQUIRE_EQ(new_attribute->attribute->name, SNAME("new"));
		REQUIRE(union_pilot_is_identifier(new_attribute->base, SNAME("Holder")));

		const FSParser::CallNode *set_call = nullptr;
		for (const FSParser::Node *statement : test->body->statements) {
			if (statement != nullptr && statement->type == FSParser::Node::CALL) {
				const FSParser::CallNode *candidate = static_cast<const FSParser::CallNode *>(statement);
				if (candidate->function_name == SNAME("set")) {
					set_call = candidate;
					break;
				}
			}
		}
		REQUIRE(set_call != nullptr);
		REQUIRE_EQ(set_call->arguments.size(), 2);
		REQUIRE(set_call->callee != nullptr);
		REQUIRE_EQ(set_call->callee->type, FSParser::Node::SUBSCRIPT);
		const FSParser::SubscriptNode *set_attribute = static_cast<const FSParser::SubscriptNode *>(set_call->callee);
		REQUIRE(set_attribute->is_attribute);
		REQUIRE_EQ(set_attribute->attribute->name, SNAME("set"));
		REQUIRE(union_pilot_is_identifier(set_attribute->base, SNAME("holder")));
		REQUIRE_EQ(set_call->arguments[0]->type, FSParser::Node::LITERAL);
		CHECK_EQ(StringName(static_cast<const FSParser::LiteralNode *>(set_call->arguments[0])->value), SNAME("value"));
		source_expression = set_call->arguments[1];

		REQUIRE(stored->initializer != nullptr);
		REQUIRE_EQ(stored->initializer->type, FSParser::Node::SUBSCRIPT);
		const FSParser::SubscriptNode *value_attribute = static_cast<const FSParser::SubscriptNode *>(stored->initializer);
		REQUIRE(value_attribute->is_attribute);
		REQUIRE_EQ(value_attribute->attribute->name, SNAME("value"));
		REQUIRE(union_pilot_is_identifier(value_attribute->base, SNAME("holder")));
		fingerprint.boundary = "reflective_write";
	}
	fingerprint.source_proof = union_pilot_source_fingerprint(source_expression);
	return fingerprint;
}

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness][UnionPilot]") {
	TEST_CASE("TypeCompleteness UnionPilot renders and observes all resolved static semantics") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		REQUIRE_EQ(resolution.cells.size(), 40);
		HashMap<String, String> semantic_fingerprints;
		HashMap<String, int> surface_counts;

		for (const FSCompletenessResolvedCell &cell : resolution.cells) {
			CAPTURE(cell.case_id);
			const FSCompletenessResolvedDimension *expected_analysis = cell.find_dimension("analysis");
			REQUIRE(expected_analysis != nullptr);

			FSCompletenessProgram program;
			REQUIRE_EQ(FSUnionCompletenessAdapter::render(cell, program), OK);
			const String surface = cell.coordinates.get("surface", String());
			CHECK_EQ(program.case_id, cell.case_id);
			CHECK_EQ(program.surface, surface);
			CHECK_EQ(program.expected_output, "uint 5\n");

			const FSCompletenessObservation observation = FSUnionCompletenessAdapter::analyze(program, surface);
			CHECK_EQ(observation.case_id, cell.case_id);
			CHECK_EQ(observation.surface, surface);
			CHECK_EQ(String(observation.dimensions.get("analysis", String())), String(expected_analysis->expected));
			CHECK(observation.diagnostics.is_empty());

			const UnionPilotSemanticFingerprint fingerprint = union_pilot_semantic_fingerprint(program);
			const String destination = cell.coordinates.get("destination", String());
			const String source_proof = cell.coordinates.get("source_proof", String());
			const String boundary = cell.coordinates.get("boundary", String());
			CHECK_EQ(fingerprint.destination, destination);
			CHECK_EQ(fingerprint.source_proof, source_proof);
			CHECK_EQ(fingerprint.boundary, boundary);
			const String coordinates_without_surface = destination + "|" + source_proof + "|" + boundary;
			const String semantic_fingerprint = fingerprint.destination + "|" + fingerprint.source_proof + "|" + fingerprint.boundary;
			if (const String *existing = semantic_fingerprints.getptr(coordinates_without_surface)) {
				CHECK_EQ(*existing, semantic_fingerprint);
			} else {
				semantic_fingerprints[coordinates_without_surface] = semantic_fingerprint;
			}
			int *surface_count = surface_counts.getptr(coordinates_without_surface);
			if (surface_count == nullptr) {
				surface_counts[coordinates_without_surface] = 1;
			} else {
				(*surface_count)++;
			}
		}
		CHECK_EQ(semantic_fingerprints.size(), 20);
		for (const KeyValue<String, int> &entry : surface_counts) {
			CHECK_EQ(entry.value, 2);
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

	TEST_CASE("TypeCompleteness UnionPilot analysis preserves traversal-like identities without writing files") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		REQUIRE_FALSE(resolution.cells.is_empty());
		FSCompletenessProgram program;
		REQUIRE_EQ(FSUnionCompletenessAdapter::render(resolution.cells[0], program), OK);
		program.case_id = "../../escape|x";
		const String escaped_path = "user://escape|x.fs";
		REQUIRE_FALSE(FileAccess::exists(escaped_path));

		const FSCompletenessObservation observation = FSUnionCompletenessAdapter::analyze(program, program.surface);
		CHECK_EQ(observation.case_id, program.case_id);
		CHECK_EQ(String(observation.dimensions.get("analysis", String())), "accept");
		CHECK(observation.diagnostics.is_empty());
		const bool escaped_file_created = FileAccess::exists(escaped_path);
		CHECK_FALSE(escaped_file_created);
		if (escaped_file_created) {
			DirAccess::remove_absolute(ProjectSettings::get_singleton()->globalize_path(escaped_path));
		}
	}

	TEST_CASE("TypeCompleteness UnionPilot repeated same-ID analysis remains independent") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		REQUIRE_FALSE(resolution.cells.is_empty());
		FSCompletenessProgram program;
		REQUIRE_EQ(FSUnionCompletenessAdapter::render(resolution.cells[0], program), OK);
		program.case_id = "repeated_identity";

		const FSCompletenessObservation first = FSUnionCompletenessAdapter::analyze(program, program.surface);
		const FSCompletenessObservation second = FSUnionCompletenessAdapter::analyze(program, program.surface);
		CHECK_EQ(first.case_id, program.case_id);
		CHECK_EQ(second.case_id, program.case_id);
		CHECK_EQ(String(first.dimensions.get("analysis", String())), "accept");
		CHECK_EQ(String(second.dimensions.get("analysis", String())), "accept");
		CHECK(first.diagnostics.is_empty());
		CHECK(second.diagnostics.is_empty());
	}

	TEST_CASE("TypeCompleteness UnionPilot diagnostics preserve exact owned source order") {
		FSCompletenessProgram malformed;
		malformed.case_id = "multiple_parser_diagnostics";
		malformed.surface = "text";
		malformed.source = "func first(:\nfunc second(:\n";

		const FSCompletenessObservation malformed_observation =
				FSUnionCompletenessAdapter::analyze(malformed, malformed.surface);
		CHECK_EQ(String(malformed_observation.dimensions.get("analysis", String())), "reject");
		REQUIRE_EQ(malformed_observation.diagnostics.size(), 6);
		CHECK_EQ(malformed_observation.diagnostics[0], "1:11: Expected parameter name.");
		CHECK_EQ(malformed_observation.diagnostics[1], String(R"diag(1:11: Expected closing ")" after function parameters.)diag"));
		CHECK_EQ(malformed_observation.diagnostics[2], "1:13: Expected indented block after function declaration.");
		CHECK_EQ(malformed_observation.diagnostics[3], "2:12: Expected parameter name.");
		CHECK_EQ(malformed_observation.diagnostics[4], String(R"diag(2:12: Expected closing ")" after function parameters.)diag"));
		CHECK_EQ(malformed_observation.diagnostics[5], "2:14: Expected indented block after function declaration.");

		FSCompletenessProgram invalid;
		invalid.case_id = "multiple_analyzer_diagnostics";
		invalid.surface = "text";
		invalid.source = "func test() -> void:\n\tvar first: int = \"bad\"\n\tvar second: String = 7\n";

		const FSCompletenessObservation observation = FSUnionCompletenessAdapter::analyze(invalid, invalid.surface);
		CHECK_EQ(String(observation.dimensions.get("analysis", String())), "reject");
		REQUIRE_EQ(observation.diagnostics.size(), 2);
		CHECK_EQ(observation.diagnostics[0],
				"2:22: Cannot assign a value of type String to variable \"first\" with specified type int.");
		CHECK_EQ(observation.diagnostics[1],
				"3:26: Cannot assign a value of type int to variable \"second\" with specified type String.");
	}
}

} // namespace FSTests
