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

#include "fs_temporary_project_tree.h"
#include "fs_type_completeness_union_adapter.h"

#include "../fs_analyzer.h"
#include "../fs_parser.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/file_access_pack.h"
#include "core/os/os.h"
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
	CHECK(p_program.coordinates.is_empty());
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

static Vector<FSCompletenessProgram> render_union_pilot_programs(const FSCompletenessResolution &p_resolution) {
	Vector<FSCompletenessProgram> programs;
	for (const FSCompletenessResolvedCell &cell : p_resolution.cells) {
		FSCompletenessProgram program;
		REQUIRE_EQ(FSUnionCompletenessAdapter::render(cell, program), OK);
		programs.push_back(program);
	}
	return programs;
}

static void check_union_pilot_runtime_batch_cleared(const FSCompletenessRuntimeBatch &p_batch) {
	CHECK(p_batch.text.is_empty());
	CHECK(p_batch.bytecode.is_empty());
	CHECK_EQ(p_batch.parity_failures, 0);
}

static FSCompletenessRuntimeBatch stale_union_pilot_runtime_batch() {
	FSCompletenessRuntimeBatch batch;
	FSCompletenessRuntimeResult result;
	result.case_id = "stale";
	result.surface = "text";
	result.passed = true;
	result.status = "stale";
	batch.text[result.case_id] = result;
	batch.parity_failures = 9;
	return batch;
}

struct UnionPilotSemanticFingerprint {
	String accept_destination;
	String holder_destination;
	String operative_destination;
	String source_proof;
	String resolved_source_proof;
	String boundary;
	String synthetic_path;
};

static void check_union_pilot_packed_paths_unchanged(const HashSet<String> &p_before) {
	const HashSet<String> after = PackedData::get_singleton()->get_file_paths();
	CHECK_EQ(after.size(), p_before.size());
	for (const String &path : p_before) {
		CHECK(after.has(path));
	}
}

static void check_union_pilot_synthetic_source_cleaned(const String &p_path) {
	CHECK_FALSE(FileAccess::exists(p_path));
	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(filesystem.is_valid());
	CHECK_FALSE(filesystem->dir_exists(p_path.get_base_dir()));
}

static const FSParser::FunctionNode *union_pilot_find_function(
		const FSParser::ClassNode *p_class, const StringName &p_name) {
	if (p_class == nullptr || !p_class->has_member(p_name)) {
		return nullptr;
	}
	const FSParser::ClassNode::Member &member = p_class->get_member(p_name);
	return member.type == FSParser::ClassNode::Member::FUNCTION ? member.function : nullptr;
}

static const FSParser::VariableNode *union_pilot_find_member_variable(
		const FSParser::ClassNode *p_class, const StringName &p_name) {
	if (p_class == nullptr || !p_class->has_member(p_name)) {
		return nullptr;
	}
	const FSParser::ClassNode::Member &member = p_class->get_member(p_name);
	return member.type == FSParser::ClassNode::Member::VARIABLE ? member.variable : nullptr;
}

static const FSParser::ClassNode *union_pilot_find_class(
		const FSParser::ClassNode *p_class, const StringName &p_name) {
	if (p_class == nullptr || !p_class->has_member(p_name)) {
		return nullptr;
	}
	const FSParser::ClassNode::Member &member = p_class->get_member(p_name);
	return member.type == FSParser::ClassNode::Member::CLASS ? member.m_class : nullptr;
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
			literal->numeric_type_is_explicit && literal->numeric_type == NumericType::UINT32 &&
			literal->get_datatype().kind == FSParser::DataType::BUILTIN &&
			literal->get_datatype().builtin_type == Variant::UINT &&
			literal->get_datatype().type_source == FSParser::DataType::ANNOTATED_EXPLICIT &&
			literal->get_datatype().numeric_type == NumericType::UINT32;
}

static bool union_pilot_is_explicit_builtin(const FSParser::DataType &p_type, Variant::Type p_builtin) {
	return p_type.kind == FSParser::DataType::BUILTIN && p_type.builtin_type == p_builtin &&
			p_type.type_source == FSParser::DataType::ANNOTATED_EXPLICIT;
}

static bool union_pilot_is_explicit_variant(const FSParser::DataType &p_type) {
	return p_type.kind == FSParser::DataType::VARIANT &&
			p_type.type_source == FSParser::DataType::ANNOTATED_EXPLICIT;
}

static String union_pilot_destination_fingerprint(const FSParser::DataType &p_type) {
	if (union_pilot_is_explicit_builtin(p_type, Variant::UINT)) {
		return "plain";
	}
	if (p_type.kind != FSParser::DataType::UNION || p_type.type_source != FSParser::DataType::ANNOTATED_EXPLICIT ||
			p_type.union_members.size() != 2) {
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

static String union_pilot_resolved_source_fingerprint(
		const FSParser::ExpressionNode *p_expression, const FSParser::ClassNode *p_root_class,
		const FSParser::FunctionNode *p_test) {
	const FSParser::VariableNode *typed_source = union_pilot_find_local(p_test, SNAME("typed_source"));
	const FSParser::VariableNode *variant_source = union_pilot_find_local(p_test, SNAME("variant_source"));
	const FSParser::FunctionNode *supply = union_pilot_find_function(p_root_class, SNAME("supply"));
	const FSParser::FunctionNode *erase = union_pilot_find_function(p_root_class, SNAME("erase"));
	if (p_expression == nullptr || typed_source == nullptr || variant_source == nullptr || supply == nullptr ||
			erase == nullptr) {
		return "invalid";
	}

	if (p_expression->type == FSParser::Node::IDENTIFIER) {
		const FSParser::IdentifierNode *identifier = static_cast<const FSParser::IdentifierNode *>(p_expression);
		if (identifier->name == SNAME("typed_source")) {
			return identifier->source == FSParser::IdentifierNode::LOCAL_VARIABLE &&
							identifier->variable_source == typed_source && typed_source->datatype_specifier != nullptr &&
							union_pilot_is_unsigned_five(typed_source->initializer) &&
							union_pilot_is_explicit_builtin(typed_source->get_datatype(), Variant::UINT) &&
							union_pilot_is_explicit_builtin(identifier->get_datatype(), Variant::UINT)
					? "static_member"
					: "invalid";
		}
		if (identifier->name == SNAME("variant_source")) {
			return identifier->source == FSParser::IdentifierNode::LOCAL_VARIABLE &&
							identifier->variable_source == variant_source && variant_source->datatype_specifier != nullptr &&
							union_pilot_is_unsigned_five(variant_source->initializer) &&
							union_pilot_is_explicit_variant(variant_source->get_datatype()) &&
							union_pilot_is_explicit_variant(identifier->get_datatype())
					? "variant"
					: "invalid";
		}
	}

	if (p_expression->type == FSParser::Node::LITERAL) {
		const FSParser::LiteralNode *literal = static_cast<const FSParser::LiteralNode *>(p_expression);
		const FSParser::DataType datatype = literal->get_datatype();
		return literal->value.get_type() == Variant::INT && int64_t(literal->value) == 5 &&
						!literal->numeric_type_is_explicit && literal->numeric_type == NumericType::INT32 &&
						literal->is_constant && union_pilot_is_explicit_builtin(datatype, Variant::UINT) &&
						datatype.numeric_type == NumericType::UINT32
				? "numeric_constant"
				: "invalid";
	}

	if (p_expression->type != FSParser::Node::CALL) {
		return "invalid";
	}
	const FSParser::CallNode *call = static_cast<const FSParser::CallNode *>(p_expression);
	if (call->function_name == SNAME("supply")) {
		if (call->arguments.size() != 1 || !union_pilot_is_unsigned_five(call->arguments[0]) ||
				!union_pilot_is_identifier(call->callee, SNAME("supply"))) {
			return "invalid";
		}
		return supply->parameters.size() == 1 && supply->parameters[0]->datatype_specifier == nullptr &&
						supply->return_type == nullptr && supply->parameters[0]->get_datatype().kind == FSParser::DataType::VARIANT &&
						supply->parameters[0]->get_datatype().type_source == FSParser::DataType::UNDETECTED &&
						supply->get_datatype().kind == FSParser::DataType::VARIANT &&
						supply->get_datatype().type_source == FSParser::DataType::INFERRED &&
						call->get_datatype().kind == FSParser::DataType::VARIANT &&
						call->get_datatype().type_source == FSParser::DataType::INFERRED
				? "gradual"
				: "invalid";
	}

	if (call->function_name != SNAME("erase") || call->arguments.size() != 1 ||
			!union_pilot_is_unsigned_five(call->arguments[0]) || call->callee == nullptr ||
			call->callee->type != FSParser::Node::SUBSCRIPT || erase->type_parameters.size() != 1 ||
			erase->type_parameters[0]->identifier == nullptr ||
			erase->type_parameters[0]->identifier->name != SNAME("T") || erase->type_parameters[0]->bound != nullptr ||
			erase->parameters.size() != 1 || erase->parameters[0]->datatype_specifier == nullptr ||
			erase->return_type == nullptr || !union_pilot_is_explicit_variant(erase->get_datatype()) ||
			!union_pilot_is_explicit_variant(call->get_datatype())) {
		return "invalid";
	}
	const FSParser::DataType parameter_type = erase->parameters[0]->get_datatype();
	if (parameter_type.kind != FSParser::DataType::TYPE_PARAMETER ||
			parameter_type.type_parameter_name != SNAME("T") || parameter_type.type_parameter_index != 0 ||
			parameter_type.type_parameter_scope != FSParser::DataType::TYPE_PARAMETER_METHOD ||
			!parameter_type.type_parameter_bound.is_empty()) {
		return "invalid";
	}
	const FSParser::SubscriptNode *application = static_cast<const FSParser::SubscriptNode *>(call->callee);
	if (application->is_attribute || !union_pilot_is_identifier(application->base, SNAME("erase")) ||
			!union_pilot_is_identifier(application->index, SNAME("uint"))) {
		return "invalid";
	}
	return call->resolved_parameter_types.size() == 1 &&
					union_pilot_is_explicit_builtin(call->resolved_parameter_types[0], Variant::UINT)
			? "erased"
			: "invalid";
}

static UnionPilotSemanticFingerprint union_pilot_semantic_fingerprint(const FSCompletenessProgram &p_program) {
	UnionPilotSemanticFingerprint fingerprint;
	UnionCompletenessInternal::SyntheticSourceScope synthetic_source(p_program.case_id, p_program.source);
	REQUIRE(synthetic_source.is_available());
	const String &path = synthetic_source.get_path();

	FSParser parser;
	REQUIRE_EQ(parser.parse(p_program.source, path, false), OK);
	FSAnalyzer analyzer(&parser);
	REQUIRE_EQ(analyzer.analyze(), OK);
	REQUIRE(parser.get_errors().is_empty());

	const FSParser::ClassNode *root_class = parser.get_tree();
	const FSParser::FunctionNode *accept = union_pilot_find_function(root_class, SNAME("accept"));
	REQUIRE(accept != nullptr);
	REQUIRE_EQ(accept->parameters.size(), 1);
	fingerprint.accept_destination = union_pilot_destination_fingerprint(accept->parameters[0]->get_datatype());
	const FSParser::ClassNode *holder_class = union_pilot_find_class(root_class, SNAME("Holder"));
	REQUIRE(holder_class != nullptr);
	const FSParser::VariableNode *holder_value = union_pilot_find_member_variable(holder_class, SNAME("value"));
	REQUIRE(holder_value != nullptr);
	fingerprint.holder_destination = union_pilot_destination_fingerprint(holder_value->get_datatype());
	fingerprint.synthetic_path = path;

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
	fingerprint.resolved_source_proof =
			union_pilot_resolved_source_fingerprint(source_expression, root_class, test);
	fingerprint.operative_destination = fingerprint.boundary == "argument_binding" ? fingerprint.accept_destination : fingerprint.holder_destination;
	return fingerprint;
}

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness][UnionPilot]") {
	TEST_CASE("TypeCompleteness UnionPilot executes text and serialized bytecode with semantic parity") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		REQUIRE_EQ(resolution.cells.size(), 40);
		const Vector<FSCompletenessProgram> programs = render_union_pilot_programs(resolution);
		const String tree_name = vformat("type_completeness_union_runtime_%d", OS::get_singleton()->get_process_id());
		String owned_root;
		{
			TemporaryProjectTree tree(tree_name);
			REQUIRE(tree.is_valid());
			owned_root = tree.root;
			const String scratch_root = TemporaryProjectTree::get_test_scratch_root();
			REQUIRE_FALSE(scratch_root.is_empty());
			CHECK(TemporaryProjectTree::is_strict_descendant(scratch_root, tree.root));

			FSCompletenessRuntimeBatch batch;
			const Error execution_error = FSUnionCompletenessAdapter::execute(tree.root, programs, batch);
			REQUIRE_EQ(execution_error, OK);
			if (execution_error != OK) {
				return;
			}
			CHECK_EQ(batch.text.size(), 20);
			CHECK_EQ(batch.bytecode.size(), 20);
			CHECK_EQ(batch.parity_failures, 0);

			HashSet<String> semantic_pairs;
			for (const FSCompletenessResolvedCell &cell : resolution.cells) {
				CAPTURE(cell.case_id);
				const String surface = cell.coordinates.get("surface", String());
				const HashMap<String, FSCompletenessRuntimeResult> &surface_results =
						surface == "text" ? batch.text : batch.bytecode;
				const FSCompletenessRuntimeResult *result = surface_results.getptr(cell.case_id);
				REQUIRE(result != nullptr);
				CHECK(result->passed);
				CHECK_EQ(result->case_id, cell.case_id);
				CHECK_EQ(result->surface, surface);
				CHECK_EQ(result->produced_output, "uint 5\n");
				CHECK_EQ(result->status, "ok");
				CHECK(result->diagnostics.is_empty());
				for (const KeyValue<String, FSCompletenessResolvedDimension> &dimension : cell.dimensions) {
					CAPTURE(dimension.key);
					CHECK_EQ(result->dimensions.get(dimension.key, Variant()), dimension.value.expected);
				}
				if (surface == "bytecode") {
					CHECK_EQ(String(result->dimensions.get("descriptor_surface", String())), "serialized_reload");
				}

				const String pair_key = String(cell.coordinates.get("destination", String())) + "|" +
						String(cell.coordinates.get("source_proof", String())) + "|" +
						String(cell.coordinates.get("boundary", String()));
				semantic_pairs.insert(pair_key);
			}
			CHECK_EQ(semantic_pairs.size(), 20);

			CHECK(FileAccess::exists(tree.root.path_join("text/project.foundry")));
			CHECK(FileAccess::exists(tree.root.path_join("bytecode/project.foundry")));
			CHECK_EQ(DirAccess::get_files_at(tree.root.path_join("text")).size(), 41);
			CHECK_EQ(DirAccess::get_files_at(tree.root.path_join("bytecode")).size(), 41);
			CHECK_FALSE(FileAccess::exists(tree.root.get_base_dir().path_join(programs[0].case_id + ".fs")));
		}
		CHECK_FALSE(FileAccess::exists(owned_root));
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(filesystem.is_valid());
		CHECK_FALSE(filesystem->dir_exists(owned_root));
	}

	TEST_CASE("TypeCompleteness UnionPilot rejects missing and duplicate surface pairs atomically") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		const Vector<FSCompletenessProgram> valid = render_union_pilot_programs(resolution);
		TemporaryProjectTree tree(vformat("type_completeness_union_pair_errors_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());

		Vector<FSCompletenessProgram> missing = valid;
		missing.remove_at(missing.size() - 1);
		FSCompletenessRuntimeBatch batch = stale_union_pilot_runtime_batch();
		CHECK_EQ(FSUnionCompletenessAdapter::execute(tree.root, missing, batch), ERR_INVALID_DATA);
		check_union_pilot_runtime_batch_cleared(batch);

		Vector<FSCompletenessProgram> duplicate = valid;
		duplicate.push_back(valid[0]);
		batch = stale_union_pilot_runtime_batch();
		CHECK_EQ(FSUnionCompletenessAdapter::execute(tree.root, duplicate, batch), ERR_ALREADY_IN_USE);
		check_union_pilot_runtime_batch_cleared(batch);
	}

	TEST_CASE("TypeCompleteness UnionPilot rejects unknown and duplicate case IDs atomically") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		const Vector<FSCompletenessProgram> valid = render_union_pilot_programs(resolution);
		TemporaryProjectTree tree(vformat("type_completeness_union_id_errors_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());

		Vector<FSCompletenessProgram> unknown = valid;
		unknown.write[0].case_id = "unknown_case";
		FSCompletenessRuntimeBatch batch = stale_union_pilot_runtime_batch();
		CHECK_NE(FSUnionCompletenessAdapter::execute(tree.root, unknown, batch), OK);
		check_union_pilot_runtime_batch_cleared(batch);

		Vector<FSCompletenessProgram> duplicate = valid;
		duplicate.write[1].case_id = duplicate[0].case_id;
		batch = stale_union_pilot_runtime_batch();
		CHECK_EQ(FSUnionCompletenessAdapter::execute(tree.root, duplicate, batch), ERR_ALREADY_EXISTS);
		check_union_pilot_runtime_batch_cleared(batch);
	}

	TEST_CASE("TypeCompleteness UnionPilot publishes output mismatches but rejects an invalid carrier") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		const Vector<FSCompletenessProgram> valid = render_union_pilot_programs(resolution);
		TemporaryProjectTree mismatch_tree(vformat("type_completeness_union_output_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(mismatch_tree.is_valid());

		Vector<FSCompletenessProgram> mismatch = valid;
		mismatch.write[0].expected_output = "wrong output\n";
		FSCompletenessRuntimeBatch batch;
		const Error mismatch_error = FSUnionCompletenessAdapter::execute(mismatch_tree.root, mismatch, batch);
		REQUIRE_EQ(mismatch_error, OK);
		if (mismatch_error != OK) {
			return;
		}
		const HashMap<String, FSCompletenessRuntimeResult> &surface_results =
				mismatch[0].surface == "text" ? batch.text : batch.bytecode;
		const FSCompletenessRuntimeResult *mismatch_result = surface_results.getptr(mismatch[0].case_id);
		REQUIRE(mismatch_result != nullptr);
		CHECK_FALSE(mismatch_result->passed);
		CHECK_EQ(mismatch_result->produced_output, "uint 5\n");

		TemporaryProjectTree carrier_tree(vformat("type_completeness_union_carrier_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(carrier_tree.is_valid());
		Vector<FSCompletenessProgram> bad_carrier = valid;
		bad_carrier.write[0].source = bad_carrier[0].source.replace("return \"uint \" + str(value)", "return \"bad \" + str(value)");
		batch = stale_union_pilot_runtime_batch();
		CHECK_NE(FSUnionCompletenessAdapter::execute(carrier_tree.root, bad_carrier, batch), OK);
		check_union_pilot_runtime_batch_cleared(batch);
	}

	TEST_CASE("TypeCompleteness UnionPilot rejects unsafe scratch roots and runner setup failure") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		const Vector<FSCompletenessProgram> programs = render_union_pilot_programs(resolution);
		const Vector<String> unsafe_roots = {
			"relative/runtime",
			"user://runtime",
			OS::get_singleton()->get_executable_path().get_base_dir(),
		};
		for (const String &unsafe_root : unsafe_roots) {
			CAPTURE(unsafe_root);
			FSCompletenessRuntimeBatch batch = stale_union_pilot_runtime_batch();
			CHECK_NE(FSUnionCompletenessAdapter::execute(unsafe_root, programs, batch), OK);
			check_union_pilot_runtime_batch_cleared(batch);
		}

		TemporaryProjectTree invalid_tree(vformat("type_completeness_union_invalid_root_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(invalid_tree.is_valid());
		invalid_tree.write_file("occupied", "not a directory");
		FSCompletenessRuntimeBatch batch = stale_union_pilot_runtime_batch();
		CHECK_NE(FSUnionCompletenessAdapter::execute(
						 invalid_tree.root.path_join("occupied"), programs, batch),
				OK);
		check_union_pilot_runtime_batch_cleared(batch);

		TemporaryProjectTree tree(vformat("type_completeness_union_setup_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		tree.write_file("text/stray.fs", "func test():\n\tpass\n");
		batch = stale_union_pilot_runtime_batch();
		ERR_PRINT_OFF;
		const Error setup_error = FSUnionCompletenessAdapter::execute(tree.root, programs, batch);
		ERR_PRINT_ON;
		CHECK_EQ(setup_error, ERR_CANT_OPEN);
		check_union_pilot_runtime_batch_cleared(batch);
		CHECK(FileAccess::exists(tree.root.path_join("text/project.foundry")));
		CHECK(FileAccess::exists(tree.root.path_join("bytecode/project.foundry")));
		CHECK(FileAccess::exists(tree.root.path_join("text/stray.fs")));
	}

	TEST_CASE("TypeCompleteness UnionPilot refuses symlinked runtime roots and ancestors") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		const Vector<FSCompletenessProgram> programs = render_union_pilot_programs(resolution);
		TemporaryProjectTree tree(vformat("type_completeness_union_symlink_%d", OS::get_singleton()->get_process_id()));
		TemporaryProjectTree neighbor(vformat("type_completeness_union_neighbor_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		REQUIRE(neighbor.is_valid());
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(filesystem.is_valid());
		if (filesystem->create_link(neighbor.root, tree.root.path_join("text")) != OK) {
			return;
		}

		FSCompletenessRuntimeBatch batch = stale_union_pilot_runtime_batch();
		CHECK_EQ(FSUnionCompletenessAdapter::execute(tree.root, programs, batch), ERR_UNAUTHORIZED);
		check_union_pilot_runtime_batch_cleared(batch);
		CHECK_FALSE(FileAccess::exists(neighbor.root.path_join("project.foundry")));
		CHECK_FALSE(FileAccess::exists(tree.root.path_join("bytecode/project.foundry")));
		CHECK_EQ(DirAccess::remove_absolute(tree.root.path_join("text")), OK);

		const String outside_root_link = tree.root.path_join("outside_root");
		if (filesystem->create_link(neighbor.root, outside_root_link) != OK) {
			return;
		}

		batch = stale_union_pilot_runtime_batch();
		CHECK_EQ(FSUnionCompletenessAdapter::execute(outside_root_link, programs, batch), ERR_UNAUTHORIZED);
		check_union_pilot_runtime_batch_cleared(batch);
		CHECK_FALSE(FileAccess::exists(neighbor.root.path_join("text/project.foundry")));

		if (filesystem->create_link(neighbor.root, tree.root.path_join("ancestor_link")) != OK) {
			return;
		}
		REQUIRE_EQ(filesystem->make_dir_recursive(neighbor.root.path_join("existing_root")), OK);
		batch = stale_union_pilot_runtime_batch();
		CHECK_EQ(FSUnionCompletenessAdapter::execute(
						 tree.root.path_join("ancestor_link/existing_root"), programs, batch),
				ERR_UNAUTHORIZED);
		check_union_pilot_runtime_batch_cleared(batch);
		CHECK_FALSE(FileAccess::exists(neighbor.root.path_join("existing_root/text/project.foundry")));
	}

	TEST_CASE("TypeCompleteness UnionPilot preflights both surfaces before writing any fixture") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		const Vector<FSCompletenessProgram> programs = render_union_pilot_programs(resolution);
		TemporaryProjectTree tree(vformat("type_completeness_union_surface_plan_%d", OS::get_singleton()->get_process_id()));
		TemporaryProjectTree neighbor(vformat("type_completeness_union_surface_neighbor_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		REQUIRE(neighbor.is_valid());
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(filesystem.is_valid());
		if (filesystem->create_link(neighbor.root, tree.root.path_join("bytecode")) != OK) {
			return;
		}

		FSCompletenessRuntimeBatch batch = stale_union_pilot_runtime_batch();
		CHECK_EQ(FSUnionCompletenessAdapter::execute(tree.root, programs, batch), ERR_UNAUTHORIZED);
		check_union_pilot_runtime_batch_cleared(batch);
		CHECK_FALSE(FileAccess::exists(tree.root.path_join("text/project.foundry")));
		CHECK_FALSE(FileAccess::exists(neighbor.root.path_join("project.foundry")));

		CHECK_EQ(DirAccess::remove_absolute(tree.root.path_join("bytecode")), OK);
		neighbor.write_file("existing_source.fs", "keep\n");
		String text_case_id;
		for (const FSCompletenessProgram &program : programs) {
			if (program.surface == "text") {
				text_case_id = program.case_id;
				break;
			}
		}
		REQUIRE_FALSE(text_case_id.is_empty());
		REQUIRE_EQ(filesystem->make_dir_recursive(tree.root.path_join("text")), OK);
		if (filesystem->create_link(neighbor.root.path_join("existing_source.fs"),
					tree.root.path_join("text").path_join(text_case_id + ".fs")) != OK) {
			return;
		}

		batch = stale_union_pilot_runtime_batch();
		CHECK_EQ(FSUnionCompletenessAdapter::execute(tree.root, programs, batch), ERR_UNAUTHORIZED);
		check_union_pilot_runtime_batch_cleared(batch);
		CHECK_FALSE(FileAccess::exists(tree.root.path_join("text/project.foundry")));
		CHECK_EQ(FileAccess::get_file_as_string(neighbor.root.path_join("existing_source.fs")), "keep\n");
	}

	TEST_CASE("TypeCompleteness UnionPilot runtime descriptor rejects RefCounted") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		const FSCompletenessResolvedCell *union_gradual = nullptr;
		for (const FSCompletenessResolvedCell &cell : resolution.cells) {
			if (cell.coordinates.get("destination", String()) == "union" &&
					cell.coordinates.get("source_proof", String()) == "gradual" &&
					cell.coordinates.get("boundary", String()) == "argument_binding" &&
					cell.coordinates.get("surface", String()) == "text") {
				union_gradual = &cell;
				break;
			}
		}
		REQUIRE(union_gradual != nullptr);
		FSCompletenessProgram program;
		REQUIRE_EQ(FSUnionCompletenessAdapter::render(*union_gradual, program), OK);
		program.source = program.source.replace("uint | String", "uint | String | RefCounted");
		Dictionary runtime_context = union_gradual->coordinates.duplicate();
		runtime_context["produced_output"] = "uint 5\n";

		const FSCompletenessObservation observation =
				FSUnionCompletenessAdapter::inspect_runtime_contract(program, runtime_context);
		CHECK(observation.diagnostics.has("Runtime destination descriptor admits RefCounted.new()."));
		CHECK_FALSE(observation.dimensions.has("runtime_obligation"));
	}

	TEST_CASE("TypeCompleteness UnionPilot scratch source identity resolves Holder and cleans its tree") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		REQUIRE_FALSE(resolution.cells.is_empty());
		FSCompletenessProgram program;
		REQUIRE_EQ(FSUnionCompletenessAdapter::render(resolution.cells[0], program), OK);
		const HashSet<String> packed_paths_before = PackedData::get_singleton()->get_file_paths();
		const String scratch_root = TemporaryProjectTree::get_test_scratch_root();
		REQUIRE_FALSE(scratch_root.is_empty());

		String owned_path;
		String owned_root;
		{
			UnionCompletenessInternal::SyntheticSourceScope source_scope(program.case_id, program.source);
			REQUIRE(source_scope.is_available());
			owned_path = source_scope.get_path();
			owned_root = owned_path.get_base_dir();
			CHECK(TemporaryProjectTree::is_strict_descendant(scratch_root, owned_path));
			CHECK(FileAccess::exists(owned_path));

			FSParser parser;
			REQUIRE_EQ(parser.parse(program.source, owned_path, false), OK);
			FSAnalyzer analyzer(&parser);
			CHECK_EQ(analyzer.analyze(), OK);
			CHECK(parser.get_errors().is_empty());
		}

		CHECK_FALSE(FileAccess::exists(owned_path));
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(filesystem.is_valid());
		CHECK_FALSE(filesystem->dir_exists(owned_root));
		check_union_pilot_packed_paths_unchanged(packed_paths_before);
	}

	TEST_CASE("TypeCompleteness UnionPilot renders and observes all resolved static semantics") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		REQUIRE_EQ(resolution.cells.size(), 40);
		const HashSet<String> packed_paths_before = PackedData::get_singleton()->get_file_paths();
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
			CHECK_EQ(fingerprint.accept_destination, destination);
			CHECK_EQ(fingerprint.holder_destination, destination);
			CHECK_EQ(fingerprint.operative_destination, destination);
			CHECK_EQ(fingerprint.source_proof, source_proof);
			CHECK_EQ(fingerprint.resolved_source_proof, source_proof);
			CHECK_EQ(fingerprint.boundary, boundary);
			check_union_pilot_synthetic_source_cleaned(fingerprint.synthetic_path);
			const String coordinates_without_surface = destination + "|" + source_proof + "|" + boundary;
			const String semantic_fingerprint = fingerprint.accept_destination + "|" + fingerprint.holder_destination + "|" +
					fingerprint.operative_destination + "|" + fingerprint.source_proof + "|" +
					fingerprint.resolved_source_proof + "|" + fingerprint.boundary;
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
		check_union_pilot_packed_paths_unchanged(packed_paths_before);
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

	TEST_CASE("TypeCompleteness UnionPilot analysis confines traversal-like identities to scratch") {
		const HashSet<String> packed_paths_before = PackedData::get_singleton()->get_file_paths();
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		REQUIRE_FALSE(resolution.cells.is_empty());
		FSCompletenessProgram program;
		REQUIRE_EQ(FSUnionCompletenessAdapter::render(resolution.cells[0], program), OK);
		program.case_id = "../../escape|x";

		const FSCompletenessObservation observation = FSUnionCompletenessAdapter::analyze(program, program.surface);
		CHECK_EQ(observation.case_id, program.case_id);
		CHECK_EQ(String(observation.dimensions.get("analysis", String())), "accept");
		CHECK(observation.diagnostics.is_empty());
		const UnionPilotSemanticFingerprint fingerprint = union_pilot_semantic_fingerprint(program);
		CHECK_FALSE(fingerprint.synthetic_path.contains("escape"));
		check_union_pilot_synthetic_source_cleaned(fingerprint.synthetic_path);
		check_union_pilot_packed_paths_unchanged(packed_paths_before);
	}

	TEST_CASE("TypeCompleteness UnionPilot repeated same-ID analysis remains independent") {
		const HashSet<String> packed_paths_before = PackedData::get_singleton()->get_file_paths();
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
		check_union_pilot_packed_paths_unchanged(packed_paths_before);
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
