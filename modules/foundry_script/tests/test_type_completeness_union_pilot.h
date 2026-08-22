/**************************************************************************/
/*  test_type_completeness_union_pilot.h                                  */
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
#include "fs_type_completeness_cache.h"
#include "fs_type_completeness_census.h"
#include "fs_type_completeness_json.h"
#include "fs_type_completeness_runner.h"
#include "fs_type_completeness_union_adapter.h"

#include "../fs_analyzer.h"
#include "../fs_parser.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/file_access_pack.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "tests/test_macros.h"
#include "tests/test_tools.h"
#include "tests/test_utils.h"

namespace FSTests {

static const String type_completeness_union_pilot_root = "modules/foundry_script/tests/type_completeness";

// The report without the members whose values are properties of the run rather than of the evidence:
// artifact paths live in a scratch root that differs per run, and stage timings are wall-clock. What
// is left is comparable against a document captured from a different run in a different scratch root.
static Variant union_pilot_scratch_independent_evidence(const Variant &p_value) {
	if (p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary source = p_value;
		Dictionary evidence;
		for (const String &key : Completeness::sorted_dictionary_keys(source)) {
			if (key == "artifact_path" || key == "timings_ms") {
				continue;
			}
			evidence[key] = union_pilot_scratch_independent_evidence(source[key]);
		}
		return evidence;
	}
	if (p_value.get_type() == Variant::ARRAY) {
		const Array source = p_value;
		Array evidence;
		for (int index = 0; index < source.size(); index++) {
			evidence.push_back(union_pilot_scratch_independent_evidence(source[index]));
		}
		return evidence;
	}
	return p_value;
}

static FSCompletenessManifest load_union_pilot_completeness_manifest() {
	FSCompletenessManifest manifest;
	Vector<String> errors;
	REQUIRE_MESSAGE(
			FSCompletenessManifest::load(
					type_completeness_union_pilot_root.path_join("rules/union_destination_membership.json"),
					manifest, errors) == OK,
			String(" | ").join(errors));
	return manifest;
}

// Coordinates the tracked rule manifest declares for a witness. Tests bind witnesses exactly the way
// the runner does, so a test can never agree with a coordinate table the run no longer consults.
static Dictionary union_pilot_witness_coordinates(const String &p_witness_id) {
	return manifest_witness_coordinates(load_union_pilot_completeness_manifest(), p_witness_id);
}

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
		REQUIRE_EQ(FSUnionCompletenessAdapter::shared().render(cell, program), OK);
		programs.push_back(program);
	}
	return programs;
}

static void check_union_pilot_runtime_batch_cleared(const FSCompletenessRuntimeBatch &p_batch) {
	CHECK(p_batch.text.is_empty());
	CHECK(p_batch.bytecode.is_empty());
}

static FSCompletenessRuntimeBatch stale_union_pilot_runtime_batch() {
	FSCompletenessRuntimeBatch batch;
	FSCompletenessRuntimeResult result;
	result.case_id = "stale";
	result.surface = "text";
	result.passed = true;
	result.status = "stale";
	batch.text[result.case_id] = result;
	return batch;
}

static PackedStringArray union_pilot_directory_entries(const String &p_path) {
	PackedStringArray entries;
	Ref<DirAccess> directory = DirAccess::open(p_path);
	if (directory.is_null()) {
		return entries;
	}
	directory->set_include_hidden(true);
	directory->list_dir_begin();
	for (String entry = directory->get_next(); !entry.is_empty(); entry = directory->get_next()) {
		if (entry != "." && entry != "..") {
			entries.push_back(entry);
		}
	}
	directory->list_dir_end();
	entries.sort();
	return entries;
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

static String union_pilot_mutated_case_id;
static String union_pilot_mutated_pair_text_id;
static String union_pilot_mutated_pair_bytecode_id;
static String union_pilot_program_mutation_source;
static String union_pilot_program_traversal_case_id;
static String union_pilot_program_traversal_surface;
static int union_pilot_mutation_count = 0;
static int union_pilot_program_mutation_count = 0;
static int union_pilot_program_callback_count = 0;
static int union_pilot_identity_mutation_count = 0;
static int union_pilot_identity_callback_count = 0;
static int union_pilot_persisted_write_mutation_count = 0;
static int union_pilot_runner_artifact_mutation_count = 0;
static int union_pilot_runner_report_mutation_count = 0;

struct UnionPilotProgramMutationScope {
	~UnionPilotProgramMutationScope() {
		union_pilot_mutated_case_id.clear();
		union_pilot_program_mutation_source.clear();
		union_pilot_program_traversal_case_id.clear();
		union_pilot_program_traversal_surface.clear();
		union_pilot_program_mutation_count = 0;
		union_pilot_program_callback_count = 0;
		union_pilot_identity_mutation_count = 0;
		union_pilot_identity_callback_count = 0;
		union_pilot_persisted_write_mutation_count = 0;
		union_pilot_runner_artifact_mutation_count = 0;
		union_pilot_runner_report_mutation_count = 0;
		UnionCompletenessInternal::set_persisted_write_test_hook(nullptr);
	}
};

static void truncate_first_union_pilot_persisted_source(const String &p_path) {
	if (union_pilot_persisted_write_mutation_count != 0 || p_path.get_extension() != "fs") {
		return;
	}
	Error open_error = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &open_error);
	if (file.is_null() || open_error != OK) {
		return;
	}
	file->store_string("truncated\n");
	file->flush();
	file->close();
	file.unref();
	union_pilot_persisted_write_mutation_count++;
}

static void truncate_first_union_pilot_runner_artifact(const String &p_path) {
	if (union_pilot_runner_artifact_mutation_count != 0 || p_path.get_extension() != "fs") {
		return;
	}
	Error open_error = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &open_error);
	if (file.is_null() || open_error != OK) {
		return;
	}
	file->store_string("truncated\n");
	file->flush();
	file->close();
	file.unref();
	union_pilot_runner_artifact_mutation_count++;
}

static void truncate_union_pilot_runner_report_temp(const String &p_path) {
	if (union_pilot_runner_report_mutation_count != 0 || !p_path.contains(".tmp.")) {
		return;
	}
	Error open_error = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &open_error);
	if (file.is_null() || open_error != OK) {
		return;
	}
	file->store_string("truncated\n");
	file->flush();
	file->close();
	file.unref();
	union_pilot_runner_report_mutation_count++;
}

static void inject_union_pilot_analyzer_error(FSCompletenessProgram &r_program) {
	union_pilot_program_callback_count++;
	if (r_program.case_id == union_pilot_mutated_case_id) {
		r_program.source = union_pilot_program_mutation_source;
		union_pilot_program_mutation_count++;
	}
}

static void corrupt_union_pilot_program_identity(FSCompletenessProgram &r_program) {
	union_pilot_program_callback_count++;
	if (r_program.case_id == union_pilot_mutated_case_id) {
		r_program.case_id = union_pilot_program_traversal_case_id;
		r_program.surface = union_pilot_program_traversal_surface;
		r_program.coordinates["surface"] = union_pilot_program_traversal_surface;
		r_program.expected_output = "mutated expected output\n";
		union_pilot_program_mutation_count++;
	}
}

static void corrupt_union_pilot_program_coordinates(FSCompletenessProgram &r_program) {
	union_pilot_identity_callback_count++;
	if (r_program.case_id == union_pilot_mutated_case_id) {
		r_program.coordinates["surface"] = "mutated_surface";
		union_pilot_identity_mutation_count++;
	}
}

static void corrupt_union_pilot_observation_identity(FSCompletenessObservation &r_observation) {
	union_pilot_identity_callback_count++;
	if (r_observation.case_id == union_pilot_mutated_case_id) {
		r_observation.case_id = "forged_case_id";
		union_pilot_identity_mutation_count++;
	}
}

static void corrupt_union_pilot_runtime_identity(FSCompletenessRuntimeResult &r_result) {
	union_pilot_identity_callback_count++;
	if (r_result.case_id == union_pilot_mutated_case_id) {
		r_result.surface = "forged_surface";
		union_pilot_identity_mutation_count++;
	}
}

static void corrupt_union_pilot_stored_carrier(FSCompletenessObservation &r_observation) {
	if (r_observation.case_id == union_pilot_mutated_case_id && r_observation.surface == "text") {
		r_observation.dimensions["stored_carrier"] = "plain_destination";
		union_pilot_mutation_count++;
	}
}

static void corrupt_union_pilot_both_outputs(FSCompletenessObservation &r_observation) {
	if (r_observation.case_id == union_pilot_mutated_pair_text_id ||
			r_observation.case_id == union_pilot_mutated_pair_bytecode_id) {
		r_observation.produced_output = "wrong output\n";
	}
}

static void corrupt_union_pilot_text_output(FSCompletenessObservation &r_observation) {
	if (r_observation.case_id == union_pilot_mutated_pair_text_id) {
		r_observation.produced_output = "wrong output\n";
	}
}

static void add_union_pilot_diagnostic(FSCompletenessObservation &r_observation) {
	if (r_observation.case_id == union_pilot_mutated_case_id) {
		r_observation.diagnostics.push_back("injected diagnostic");
	}
}

static bool is_union_pilot_adapter_diagnostic_target(const FSCompletenessProgram &p_program) {
	return p_program.coordinates.get("surface", String()) == "text" &&
			p_program.coordinates.get("destination", String()) == "plain" &&
			p_program.coordinates.get("source_proof", String()) == "static_member" &&
			p_program.coordinates.get("boundary", String()) == "reflective_write";
}

static FSCompletenessProgram *find_union_pilot_contract_program(
		Vector<FSCompletenessProgram> &r_programs, const String &p_surface) {
	for (FSCompletenessProgram &program : r_programs) {
		if (program.coordinates.get("surface", String()) == p_surface &&
				program.coordinates.get("destination", String()) == "plain" &&
				program.coordinates.get("source_proof", String()) == "static_member" &&
				program.coordinates.get("boundary", String()) == "argument_binding") {
			return &program;
		}
	}
	return nullptr;
}

static String union_pilot_absolute_fixture_path(const String &p_relative_path) {
	return TestUtils::get_tests_dir().get_base_dir().path_join(p_relative_path).simplify_path();
}

static String union_pilot_preload_source(
		const String &p_source, const String &p_dependency_path, const String &p_constant_name) {
	return vformat("const %s = preload(\"%s\")\n", p_constant_name, p_dependency_path.c_escape()) + p_source;
}

static bool union_pilot_diagnostics_contain(
		const PackedStringArray &p_diagnostics, const String &p_fragment) {
	for (const String &diagnostic : p_diagnostics) {
		if (diagnostic.contains(p_fragment)) {
			return true;
		}
	}
	return false;
}

static void fail_union_pilot_runtime_result(FSCompletenessRuntimeResult &r_result) {
	if (r_result.case_id == union_pilot_mutated_case_id) {
		r_result.passed = false;
		r_result.status = "injected_failure";
	}
}

static void corrupt_union_pilot_witness_dimension(FSCompletenessObservation &r_observation) {
	if (r_observation.case_id == union_pilot_mutated_case_id) {
		r_observation.dimensions["runtime_obligation"] = "typed_destination_check";
	}
}

static void corrupt_union_pilot_multiple_observations(FSCompletenessObservation &r_observation) {
	if (r_observation.case_id == union_pilot_mutated_pair_text_id) {
		r_observation.produced_output = "wrong text output\n";
		r_observation.dimensions["analysis"] = "reject";
		r_observation.dimensions["stored_carrier"] = "plain_destination";
	} else if (r_observation.case_id == union_pilot_mutated_pair_bytecode_id) {
		r_observation.dimensions["stored_carrier"] = "wrong_bytecode_carrier";
	}
}

static void corrupt_union_pilot_both_stored_carriers(FSCompletenessObservation &r_observation) {
	if (r_observation.case_id == union_pilot_mutated_pair_text_id ||
			r_observation.case_id == union_pilot_mutated_pair_bytecode_id) {
		r_observation.dimensions["stored_carrier"] = "plain_destination";
	}
}

static void select_union_pilot_pair(const FSCompletenessResolution &p_resolution,
		const String &p_destination, const String &p_source_proof, const String &p_boundary) {
	union_pilot_mutated_pair_text_id.clear();
	union_pilot_mutated_pair_bytecode_id.clear();
	for (const FSCompletenessResolvedCell &cell : p_resolution.cells) {
		if (cell.coordinates.get("destination", String()) != p_destination ||
				cell.coordinates.get("source_proof", String()) != p_source_proof ||
				cell.coordinates.get("boundary", String()) != p_boundary) {
			continue;
		}
		if (cell.coordinates.get("surface", String()) == "text") {
			union_pilot_mutated_pair_text_id = cell.case_id;
		} else {
			union_pilot_mutated_pair_bytecode_id = cell.case_id;
		}
	}
	REQUIRE_FALSE(union_pilot_mutated_pair_text_id.is_empty());
	REQUIRE_FALSE(union_pilot_mutated_pair_bytecode_id.is_empty());
}

static String stage_union_pilot_completeness_catalog_at(
		TemporaryProjectTree &p_tree, const String &p_relative_root) {
	const String staged_root = p_tree.root.path_join(p_relative_root);
	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(filesystem.is_valid());
	REQUIRE_EQ(filesystem->copy_dir(type_completeness_union_pilot_root, staged_root), OK);
	return staged_root;
}

static String stage_union_pilot_completeness_catalog(TemporaryProjectTree &p_tree) {
	return stage_union_pilot_completeness_catalog_at(p_tree, "catalog");
}

static String union_pilot_finding_record(const String &p_finding_id, const String &p_case_id,
		const String &p_classification, const String &p_extra_field = String(),
		const String &p_permanent_test_paths =
				"\"modules/foundry_script/tests/test_type_completeness_union_pilot.h\"") {
	return vformat(R"JSON({
	"schema_version": 1,
	"finding_id": "%s",
	"case_id": "%s",
	"family": "union_destination_membership",
	"dimension": "stored_carrier",
	"classification": "%s",
	"issue_url": "https://example.invalid/issues/1",
	"closure_packet_url": "https://example.invalid/closure/1",
	"permanent_test_paths": [%s]%s
}
)JSON",
			p_finding_id, p_case_id, p_classification, p_permanent_test_paths, p_extra_field);
}

static Error union_pilot_tracked_file_probe_error = OK;
static int union_pilot_tracked_file_probe_exit_code = 0;
static String union_pilot_tracked_file_probe_repository_root;
static String union_pilot_tracked_file_probe_path;

static Error union_pilot_tracked_file_probe(const String &p_repository_root, const String &p_path,
		String &r_output, int &r_exit_code) {
	union_pilot_tracked_file_probe_repository_root = p_repository_root;
	union_pilot_tracked_file_probe_path = p_path;
	r_output = String();
	r_exit_code = union_pilot_tracked_file_probe_exit_code;
	return union_pilot_tracked_file_probe_error;
}

struct UnionPilotWarningRecorder {
	UnionPilotWarningRecorder() {
		handler.errfunc = _record;
		handler.userdata = this;
		add_error_handler(&handler);
	}

	~UnionPilotWarningRecorder() {
		remove_error_handler(&handler);
	}

	static void _record(void *p_self, const char *p_function, const char *p_file, int p_line,
			const char *p_error, const char *p_explanation, bool p_editor_notify, ErrorHandlerType p_type) {
		UnionPilotWarningRecorder *self = static_cast<UnionPilotWarningRecorder *>(p_self);
		self->messages += String::utf8(
								  p_explanation != nullptr && p_explanation[0] != '\0' ? p_explanation : p_error) +
				"\n";
	}

	ErrorHandlerList handler;
	String messages;
};

// The evidence half of a report: everything a second run of the same inputs must reproduce exactly.
// Timings measure this machine on this run and are the one member that legitimately differs.
static Dictionary union_pilot_report_evidence(const Dictionary &p_report) {
	Dictionary evidence = p_report.duplicate(true);
	evidence.erase("timings_ms");
	return evidence;
}

// Every stage the runner times, in report order.
static PackedStringArray union_pilot_timing_stages() {
	return PackedStringArray({ "load", "resolve", "render", "analyze", "execute", "report", "total" });
}

// The census summary a published report carries. Every coverage entry is accounted for by exactly one
// status, and every entry that is not backed by a witness names the workstream issue that owns it, so
// an uncovered representation child is visible in the report rather than absent from it.
static void check_union_pilot_census(const Dictionary &p_report) {
	REQUIRE(p_report.has("census"));
	const Dictionary census = p_report["census"];
	for (const String &member : FSCompletenessCensus::summary_count_members()) {
		CAPTURE(member);
		REQUIRE(census.has(member));
		CHECK_EQ(Variant(census[member]).get_type(), Variant::FLOAT);
	}
	const Array entries = census["entries"];
	CHECK(entries.size() > 0);
	CHECK_EQ(int(double(census["covered"])) + int(double(census["uncovered"])) +
					int(double(census["unsupported"])) + int(double(census["quality_deferred"])),
			entries.size());
	for (int index = 0; index < entries.size(); index++) {
		CAPTURE(index);
		const Dictionary entry = entries[index];
		const String status = entry.get("status", String());
		CHECK_FALSE(String(entry.get("representation", String())).is_empty());
		CHECK_FALSE(String(entry.get("child_slot", String())).is_empty());
		CHECK_FALSE(String(entry.get("surface", String())).is_empty());
		if (status == "uncovered" || status == "quality_deferred") {
			CHECK_FALSE(String(entry.get("issue_url", String())).is_empty());
		}
	}
}

static void check_union_pilot_timings(const Dictionary &p_report) {
	REQUIRE(p_report.has("timings_ms"));
	const Dictionary timings = p_report["timings_ms"];
	CHECK_EQ(timings.size(), union_pilot_timing_stages().size());
	for (const String &stage : union_pilot_timing_stages()) {
		CAPTURE(stage);
		REQUIRE(timings.has(stage));
		CHECK_EQ(Variant(timings[stage]).get_type(), Variant::FLOAT);
		CHECK(double(timings[stage]) >= 0.0);
	}
}

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness][UnionPilot]") {
	TEST_CASE("TypeCompleteness UnionPilot runner publishes complete deterministic coverage") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		TemporaryProjectTree tree(vformat("type_completeness_union_report_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		tree.write_file("report.json", "previous regular report\n");

		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		CHECK(options.program_mutator == nullptr);
		FSCompletenessRunResult result;
		REQUIRE_EQ(FSCompletenessRunner::run(options, result), OK);
		CHECK(result.success);
		CHECK_EQ(result.executed_cells, 40);
		CHECK(result.findings.is_empty());

		Error read_error = OK;
		const String report_source = FileAccess::get_file_as_string(options.report_path, &read_error);
		REQUIRE_EQ(read_error, OK);
		JSON json;
		REQUIRE_EQ(json.parse(report_source), OK);
		REQUIRE_EQ(json.get_data().get_type(), Variant::DICTIONARY);
		const Dictionary report = json.get_data();
		CHECK_EQ(int(report.get("schema_version", 0)), 1);
		CHECK_EQ(String(report.get("family", String())), options.family);
		CHECK_EQ(bool(report.get("success", false)), true);
		CHECK_EQ(int(report.get("cell_count", 0)), 40);
		CHECK_EQ(int(report.get("uncovered_required_dimensions", -1)), 0);
		CHECK_EQ(int(report.get("text_bytecode_parity_failures", -1)), 0);

		const Dictionary executed_by_surface = report.get("executed_by_surface", Dictionary());
		CHECK_EQ(int(executed_by_surface.get("text", 0)), 20);
		CHECK_EQ(int(executed_by_surface.get("bytecode", 0)), 20);
		const Dictionary chain_coverage = report.get("coverage_by_chain_length", Dictionary());
		CHECK_EQ(int(chain_coverage.get("0", 0)), 10);
		CHECK_EQ(int(chain_coverage.get("1", 0)), 28);
		CHECK_EQ(int(chain_coverage.get("2", 0)), 26);
		CHECK_EQ(int(chain_coverage.get("3", 0)), 8);
		CHECK_EQ(int(chain_coverage.get("0", 0)) + int(chain_coverage.get("1", 0)) +
						int(chain_coverage.get("2", 0)) + int(chain_coverage.get("3", 0)),
				72);
		CHECK(chain_coverage.has("3"));

		const Dictionary coverage_by_dimension = report.get("coverage_by_dimension", Dictionary());
		CHECK_EQ(int(coverage_by_dimension.get("analysis", 0)), 40);
		CHECK_EQ(int(coverage_by_dimension.get("runtime_obligation", 0)), 24);
		CHECK_EQ(int(coverage_by_dimension.get("stored_carrier", 0)), 8);
		const Array cases = report.get("cases", Array());
		CHECK_EQ(cases.size(), 40);
		const Array findings = report.get("findings", Array());
		CHECK(findings.is_empty());
		CHECK_EQ(result.report, report);
		int durable_artifacts = 0;
		for (int i = 0; i < cases.size(); i++) {
			CAPTURE(i);
			REQUIRE_EQ(cases[i].get_type(), Variant::DICTIONARY);
			const Dictionary case_report = cases[i];
			CHECK_FALSE(String(case_report.get("case_id", String())).is_empty());
			CHECK_EQ(Dictionary(case_report.get("coordinates", Dictionary())).size(), 4);
			CHECK_EQ(String(case_report.get("status", String())), "passed");
			CHECK_EQ(bool(case_report.get("passed", false)), true);
			CHECK_EQ(bool(case_report.get("runtime_passed", false)), true);
			const String case_id = case_report.get("case_id", String());
			const String surface = Dictionary(case_report.get("coordinates", Dictionary())).get("surface", String());
			const String artifact_path = case_report.get("artifact_path", String());
			CHECK_EQ(artifact_path,
					tree.root.path_join("report-artifacts").path_join(surface).path_join(case_id + ".fs"));
			CHECK(FileAccess::exists(artifact_path));
			for (const FSCompletenessResolvedCell &cell : resolution.cells) {
				if (cell.case_id != case_id) {
					continue;
				}
				FSCompletenessProgram program;
				REQUIRE_EQ(FSUnionCompletenessAdapter::shared().render(cell, program), OK);
				CHECK_EQ(FileAccess::get_file_as_string(artifact_path), program.source);
				durable_artifacts++;
				break;
			}
			CHECK_FALSE(Dictionary(case_report.get("expected", Dictionary())).is_empty());
			CHECK_FALSE(Dictionary(case_report.get("actual", Dictionary())).is_empty());
			CHECK_FALSE(Dictionary(case_report.get("canonical_provenance", Dictionary())).is_empty());
			CHECK_FALSE(Dictionary(case_report.get("agreeing_provenance", Dictionary())).is_empty());
			const Dictionary coordinates = case_report.get("coordinates", Dictionary());
			if (coordinates.get("destination", String()) == "plain" &&
					coordinates.get("source_proof", String()) == "static_member" &&
					coordinates.get("boundary", String()) == "argument_binding" &&
					coordinates.get("surface", String()) == "text") {
				const Array canonical_analysis =
						Dictionary(case_report.get("canonical_provenance", Dictionary())).get("analysis", Array());
				const Array agreeing_analysis =
						Dictionary(case_report.get("agreeing_provenance", Dictionary())).get("analysis", Array());
				CHECK(canonical_analysis.is_empty());
				REQUIRE_EQ(agreeing_analysis.size(), 1);
				CHECK(Array(agreeing_analysis[0]).is_empty());
			}
		}
		CHECK_EQ(durable_artifacts, 40);

		check_union_pilot_timings(report);
		check_union_pilot_timings(result.report);
		check_union_pilot_census(report);
		check_union_pilot_census(result.report);

		FSCompletenessRunResult repeated_result;
		REQUIRE_EQ(FSCompletenessRunner::run(options, repeated_result), OK);
		Error repeated_read_error = OK;
		const String repeated_source = FileAccess::get_file_as_string(options.report_path, &repeated_read_error);
		REQUIRE_EQ(repeated_read_error, OK);
		JSON repeated_json;
		REQUIRE_EQ(repeated_json.parse(repeated_source), OK);
		REQUIRE_EQ(repeated_json.get_data().get_type(), Variant::DICTIONARY);
		// Two runs of the same inputs publish the same evidence; only the timings differ, and they are
		// excluded from every digest and comparison downstream.
		CHECK_EQ(union_pilot_report_evidence(repeated_json.get_data()), union_pilot_report_evidence(report));
		CHECK_EQ(union_pilot_report_evidence(repeated_result.report), union_pilot_report_evidence(report));
		check_union_pilot_timings(repeated_result.report);
	}

	TEST_CASE("TypeCompleteness UnionPilot runner creates an owned scratch root before execution") {
		TemporaryProjectTree tree(vformat("type_completeness_union_scratch_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root.path_join("runner_scratch");
		options.report_path = options.scratch_root.path_join("report.json");
		CHECK_FALSE(DirAccess::dir_exists_absolute(options.scratch_root));

		FSCompletenessRunResult result;
		REQUIRE_EQ(FSCompletenessRunner::run(options, result), OK);
		CHECK(result.success);
		CHECK_EQ(result.executed_cells, 40);
		CHECK(DirAccess::dir_exists_absolute(options.scratch_root));
		CHECK(FileAccess::exists(options.report_path));
	}

	TEST_CASE("TypeCompleteness UnionPilot runner reports adapter analyzer failures") {
		UnionPilotProgramMutationScope mutation_scope;
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		union_pilot_mutated_case_id.clear();
		for (const FSCompletenessResolvedCell &cell : resolution.cells) {
			FSCompletenessProgram program;
			REQUIRE_EQ(FSUnionCompletenessAdapter::shared().render(cell, program), OK);
			if (is_union_pilot_adapter_diagnostic_target(program)) {
				union_pilot_mutated_case_id = cell.case_id;
				break;
			}
		}
		REQUIRE_FALSE(union_pilot_mutated_case_id.is_empty());
		Error source_error = OK;
		union_pilot_program_mutation_source = FileAccess::get_file_as_string(
				"modules/foundry_script/tests/scripts/analyzer/errors/number_not_an_expression.fs", &source_error);
		REQUIRE_EQ(source_error, OK);
		REQUIRE_FALSE(union_pilot_program_mutation_source.is_empty());
		union_pilot_program_mutation_count = 0;
		union_pilot_program_callback_count = 0;

		TemporaryProjectTree tree(
				vformat("type_completeness_union_runner_analyzer_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.program_mutator = inject_union_pilot_analyzer_error;

		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), FAILED);
		CHECK_FALSE(result.success);
		CHECK_EQ(result.executed_cells, 40);
		CHECK_EQ(union_pilot_program_callback_count, 40);
		CHECK_EQ(union_pilot_program_mutation_count, 1);
		CHECK_EQ(int(result.report.get("cell_count", 0)), 40);
		CHECK_EQ(bool(result.report.get("success", true)), false);
		CHECK_EQ(int(result.report.get("text_bytecode_parity_failures", 0)), 1);
		CHECK(FileAccess::exists(options.report_path));
		CHECK_EQ(result.findings.size(), 5);

		HashSet<String> target_dimensions;
		String target_artifact_path;
		for (const FSCompletenessFinding &finding : result.findings) {
			if (finding.case_id != union_pilot_mutated_case_id) {
				continue;
			}
			target_dimensions.insert(finding.dimension);
			CHECK_EQ(finding.classification, "unclassified");
			CHECK_FALSE(finding.parity_evidence.is_empty());
			target_artifact_path = finding.artifact_path;
			if (finding.dimension == "analysis") {
				CHECK_EQ(String(finding.expected), "accept");
				CHECK_EQ(String(finding.actual), "reject");
			} else if (finding.dimension == "diagnostics") {
				const PackedStringArray diagnostics = finding.actual;
				CHECK_FALSE(diagnostics.is_empty());
			} else if (finding.dimension == "diagnostic_severity") {
				CHECK_EQ(String(finding.expected), "at_most_warning");
				CHECK_EQ(String(finding.actual), "error");
			}
		}
		CHECK(target_dimensions.has("analysis"));
		CHECK(target_dimensions.has("diagnostics"));
		CHECK(target_dimensions.has("diagnostic_severity"));
		CHECK(target_dimensions.has("output"));
		CHECK(target_dimensions.has("runtime_status"));
		CHECK_EQ(target_dimensions.size(), 5);
		CHECK(FileAccess::exists(target_artifact_path));
		CHECK_EQ(FileAccess::get_file_as_string(target_artifact_path), union_pilot_program_mutation_source);

		const Array cases = result.report.get("cases", Array());
		REQUIRE_EQ(cases.size(), 40);
		int target_case_count = 0;
		for (int i = 0; i < cases.size(); i++) {
			const Dictionary case_report = cases[i];
			if (case_report.get("case_id", String()) != union_pilot_mutated_case_id) {
				continue;
			}
			target_case_count++;
			CHECK_EQ(String(case_report.get("status", String())), "failed");
			CHECK_FALSE(bool(case_report.get("passed", true)));
			CHECK_FALSE(bool(case_report.get("runtime_passed", true)));
			CHECK_EQ(String(case_report.get("runtime_status", String())), "analyzer_error");
			CHECK_FALSE(Array(case_report.get("diagnostics", Array())).is_empty());
			CHECK(String(case_report.get("produced_output", String())).begins_with("FS_TEST_ANALYZER_ERROR\n"));
			CHECK_EQ(String(case_report.get("expected_output", String())), "uint 5\n");
			CHECK_EQ(String(Dictionary(case_report.get("actual", Dictionary())).get("analysis", String())), "reject");
		}
		CHECK_EQ(target_case_count, 1);

		Error report_error = OK;
		const String report_source = FileAccess::get_file_as_string(options.report_path, &report_error);
		REQUIRE_EQ(report_error, OK);
		JSON json;
		REQUIRE_EQ(json.parse(report_source), OK);
		const Dictionary parsed_report = json.get_data();
		CHECK_EQ(bool(parsed_report.get("success", true)), false);
		CHECK_EQ(int(parsed_report.get("cell_count", 0)), 40);
		CHECK_EQ(Array(parsed_report.get("findings", Array())).size(), 5);
		CHECK_EQ(Array(parsed_report.get("cases", Array())).size(), 40);
	}

	TEST_CASE("TypeCompleteness UnionPilot runner reports dependency compiler failures") {
		UnionPilotProgramMutationScope mutation_scope;
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		union_pilot_mutated_case_id.clear();
		for (const FSCompletenessResolvedCell &cell : resolution.cells) {
			FSCompletenessProgram program;
			REQUIRE_EQ(FSUnionCompletenessAdapter::shared().render(cell, program), OK);
			if (program.coordinates.get("surface", String()) != "text" ||
					program.coordinates.get("destination", String()) != "plain" ||
					program.coordinates.get("source_proof", String()) != "static_member" ||
					program.coordinates.get("boundary", String()) != "argument_binding") {
				continue;
			}
			union_pilot_mutated_case_id = program.case_id;
			const String dependency_path = union_pilot_absolute_fixture_path(
					"modules/foundry_script/tests/scripts/analyzer/errors/"
					"enum_name_bare_payload_case_in_own_body.notest.fs");
			REQUIRE(FileAccess::exists(dependency_path));
			union_pilot_program_mutation_source = union_pilot_preload_source(
					program.source, dependency_path, "RunnerCompilerFailureDependency");
			break;
		}
		REQUIRE_FALSE(union_pilot_mutated_case_id.is_empty());
		REQUIRE_FALSE(union_pilot_program_mutation_source.is_empty());
		FSCompletenessProgram analysis_program;
		for (const FSCompletenessResolvedCell &cell : resolution.cells) {
			if (cell.case_id == union_pilot_mutated_case_id) {
				REQUIRE_EQ(FSUnionCompletenessAdapter::shared().render(cell, analysis_program), OK);
				analysis_program.source = union_pilot_program_mutation_source;
				break;
			}
		}
		const FSCompletenessObservation analysis =
				FSUnionCompletenessAdapter::shared().analyze(analysis_program, analysis_program.surface);
		CHECK_EQ(String(analysis.dimensions.get("analysis", String())), "accept");
		CHECK(analysis.diagnostics.is_empty());

		TemporaryProjectTree tree(
				vformat("type_completeness_union_runner_compiler_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.program_mutator = inject_union_pilot_analyzer_error;

		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), FAILED);
		CHECK_FALSE(result.success);
		CHECK_EQ(result.executed_cells, 40);
		CHECK_EQ(union_pilot_program_callback_count, 40);
		CHECK_EQ(union_pilot_program_mutation_count, 1);
		CHECK(FileAccess::exists(options.report_path));
		CHECK_EQ(int(result.report.get("cell_count", 0)), 40);
		CHECK_EQ(int(result.report.get("text_bytecode_parity_failures", 0)), 1);
		CHECK_EQ(result.findings.size(), 4);

		HashSet<String> target_dimensions;
		String target_artifact_path;
		for (const FSCompletenessFinding &finding : result.findings) {
			CHECK_EQ(finding.case_id, union_pilot_mutated_case_id);
			target_dimensions.insert(finding.dimension);
			CHECK_FALSE(finding.parity_evidence.is_empty());
			target_artifact_path = finding.artifact_path;
		}
		CHECK(target_dimensions.has("diagnostics"));
		CHECK(target_dimensions.has("diagnostic_severity"));
		CHECK(target_dimensions.has("output"));
		CHECK(target_dimensions.has("runtime_status"));
		CHECK_EQ(target_dimensions.size(), 4);
		CHECK(FileAccess::exists(target_artifact_path));
		CHECK_EQ(FileAccess::get_file_as_string(target_artifact_path), union_pilot_program_mutation_source);

		const Array cases = result.report.get("cases", Array());
		REQUIRE_EQ(cases.size(), 40);
		int target_case_count = 0;
		for (int i = 0; i < cases.size(); i++) {
			const Dictionary case_report = cases[i];
			if (case_report.get("case_id", String()) != union_pilot_mutated_case_id) {
				continue;
			}
			target_case_count++;
			CHECK_EQ(String(case_report.get("status", String())), "failed");
			CHECK_FALSE(bool(case_report.get("passed", true)));
			CHECK_FALSE(bool(case_report.get("runtime_passed", true)));
			CHECK_EQ(String(case_report.get("runtime_status", String())), "compiler_error");
			CHECK_FALSE(Array(case_report.get("diagnostics", Array())).is_empty());
			CHECK(String(case_report.get("produced_output", String())).begins_with("FS_TEST_COMPILER_ERROR\n"));
			CHECK_EQ(String(Dictionary(case_report.get("actual", Dictionary())).get("analysis", String())),
					"accept");
		}
		CHECK_EQ(target_case_count, 1);
	}

	TEST_CASE("TypeCompleteness UnionPilot runner rejects mutated program identity before staging") {
		UnionPilotProgramMutationScope mutation_scope;
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		REQUIRE_FALSE(resolution.cells.is_empty());
		union_pilot_mutated_case_id = resolution.cells[0].case_id;

		TemporaryProjectTree tree(
				vformat("type_completeness_union_identity_%d", OS::get_singleton()->get_process_id()));
		TemporaryProjectTree neighbor(
				vformat("type_completeness_union_identity_neighbor_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		REQUIRE(neighbor.is_valid());
		neighbor.write_file("sentinel.txt", "outside sentinel\n");
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(filesystem.is_valid());
		REQUIRE_EQ(DirAccess::make_dir_absolute(neighbor.root.path_join("nested")), OK);
		const PackedStringArray neighbor_entries_before = union_pilot_directory_entries(neighbor.root);
		union_pilot_program_traversal_case_id = "../outside";
		union_pilot_program_traversal_surface = "../../" + neighbor.root.get_file() + "/nested";
		union_pilot_program_mutation_count = 0;
		union_pilot_program_callback_count = 0;
		tree.write_file("report.json", "prior report sentinel\n");

		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.program_mutator = corrupt_union_pilot_program_identity;
		FSCompletenessRunResult result;
		result.success = true;
		result.executed_cells = 99;
		result.findings.push_back(FSCompletenessFinding());
		result.report["stale"] = true;

		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_INVALID_DATA);
		CHECK_FALSE(result.success);
		CHECK_EQ(result.executed_cells, 0);
		CHECK(result.findings.is_empty());
		CHECK(result.report.is_empty());
		CHECK_EQ(union_pilot_program_callback_count, 1);
		CHECK_EQ(union_pilot_program_mutation_count, 1);
		CHECK_EQ(FileAccess::get_file_as_string(options.report_path), "prior report sentinel\n");
		CHECK_FALSE(DirAccess::dir_exists_absolute(tree.root.path_join("report-artifacts")));
		CHECK_FALSE(FileAccess::exists(neighbor.root.path_join("outside.fs")));
		CHECK_EQ(FileAccess::get_file_as_string(neighbor.root.path_join("sentinel.txt")), "outside sentinel\n");
		CHECK_EQ(union_pilot_directory_entries(neighbor.root), neighbor_entries_before);
	}

	TEST_CASE("TypeCompleteness UnionPilot runner snapshots program coordinates deeply") {
		UnionPilotProgramMutationScope mutation_scope;
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		REQUIRE_FALSE(resolution.cells.is_empty());
		union_pilot_mutated_case_id = resolution.cells[0].case_id;
		union_pilot_identity_mutation_count = 0;
		union_pilot_identity_callback_count = 0;

		TemporaryProjectTree tree(
				vformat("type_completeness_union_coordinate_identity_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		tree.write_file("report.json", "prior report sentinel\n");
		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.program_mutator = corrupt_union_pilot_program_coordinates;
		FSCompletenessRunResult result;
		result.success = true;
		result.executed_cells = 99;
		result.report["stale"] = true;

		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_INVALID_DATA);
		CHECK_FALSE(result.success);
		CHECK_EQ(result.executed_cells, 0);
		CHECK(result.findings.is_empty());
		CHECK(result.report.is_empty());
		CHECK_EQ(union_pilot_identity_callback_count, 1);
		CHECK_EQ(union_pilot_identity_mutation_count, 1);
		CHECK_EQ(FileAccess::get_file_as_string(options.report_path), "prior report sentinel\n");
		CHECK_FALSE(DirAccess::dir_exists_absolute(tree.root.path_join("report-artifacts")));
	}

	TEST_CASE("TypeCompleteness UnionPilot runner rejects observation and runtime result identity mutation") {
		UnionPilotProgramMutationScope mutation_scope;
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		REQUIRE_FALSE(resolution.cells.is_empty());
		union_pilot_mutated_case_id = resolution.cells[0].case_id;

		for (int mutation_kind = 0; mutation_kind < 2; mutation_kind++) {
			CAPTURE(mutation_kind);
			union_pilot_identity_mutation_count = 0;
			union_pilot_identity_callback_count = 0;
			TemporaryProjectTree tree(vformat("type_completeness_union_runtime_identity_%d_%d",
					OS::get_singleton()->get_process_id(), mutation_kind));
			REQUIRE(tree.is_valid());
			tree.write_file("report.json", "prior report sentinel\n");
			FSCompletenessRunOptions options;
			options.catalog_root = type_completeness_union_pilot_root;
			options.family = "union_destination_membership";
			options.scratch_root = tree.root;
			options.report_path = tree.root.path_join("report.json");
			if (mutation_kind == 0) {
				options.observation_mutator = corrupt_union_pilot_observation_identity;
			} else {
				options.runtime_result_mutator = corrupt_union_pilot_runtime_identity;
			}
			FSCompletenessRunResult result;
			result.success = true;
			result.executed_cells = 99;
			result.report["stale"] = true;

			CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_INVALID_DATA);
			CHECK_FALSE(result.success);
			CHECK_EQ(result.executed_cells, 0);
			CHECK(result.findings.is_empty());
			CHECK(result.report.is_empty());
			CHECK_EQ(union_pilot_identity_callback_count, 1);
			CHECK_EQ(union_pilot_identity_mutation_count, 1);
			CHECK_EQ(FileAccess::get_file_as_string(options.report_path), "prior report sentinel\n");
			CHECK_FALSE(DirAccess::dir_exists_absolute(tree.root.path_join("report-artifacts")));
		}
	}

	TEST_CASE("TypeCompleteness UnionPilot runner rejects report collisions with generated artifacts") {
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
		REQUIRE_EQ(FSUnionCompletenessAdapter::shared().render(*text_cell, program), OK);

		TemporaryProjectTree tree(
				vformat("type_completeness_union_artifact_report_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String artifact_path = tree.root.path_join("report-artifacts/text/" + text_cell->case_id + ".fs");
		tree.write_file("report-artifacts/text/" + text_cell->case_id + ".fs", program.source);
		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = artifact_path;
		FSCompletenessRunResult result;
		result.success = true;
		result.executed_cells = 99;
		result.report["stale"] = true;

		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_UNAUTHORIZED);
		CHECK_FALSE(result.success);
		CHECK_EQ(result.executed_cells, 0);
		CHECK(result.findings.is_empty());
		CHECK(result.report.is_empty());
		CHECK_EQ(FileAccess::get_file_as_string(artifact_path), program.source);
		CHECK_FALSE(DirAccess::dir_exists_absolute(tree.root.path_join("report-artifacts/bytecode")));

		const String reserved_report_path = tree.root.path_join("report-artifacts/report.json");
		tree.write_file("report-artifacts/report.json", "reserved report sentinel\n");
		options.report_path = reserved_report_path;
		result.success = true;
		result.executed_cells = 99;
		result.report["stale"] = true;
		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_UNAUTHORIZED);
		CHECK_FALSE(result.success);
		CHECK_EQ(result.executed_cells, 0);
		CHECK(result.findings.is_empty());
		CHECK(result.report.is_empty());
		CHECK_EQ(FileAccess::get_file_as_string(reserved_report_path), "reserved report sentinel\n");
		CHECK_EQ(FileAccess::get_file_as_string(artifact_path), program.source);
		CHECK_FALSE(DirAccess::dir_exists_absolute(tree.root.path_join("report-artifacts/bytecode")));
	}

	TEST_CASE("TypeCompleteness UnionPilot runner rejects report collisions with its catalog") {
		TemporaryProjectTree tree(
				vformat("type_completeness_union_catalog_report_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String catalog_root = stage_union_pilot_completeness_catalog(tree);
		const String manifest_path = catalog_root.path_join("rules/union_destination_membership.json");
		const String manifest_before = FileAccess::get_file_as_string(manifest_path);
		const String report_path = catalog_root.path_join("report.json");
		tree.write_file("catalog/report.json", "catalog sentinel\n");
		FSCompletenessRunOptions options;
		options.catalog_root = catalog_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = report_path;
		FSCompletenessRunResult result;
		result.success = true;
		result.executed_cells = 99;
		result.report["stale"] = true;

		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_UNAUTHORIZED);
		CHECK_FALSE(result.success);
		CHECK_EQ(result.executed_cells, 0);
		CHECK(result.findings.is_empty());
		CHECK(result.report.is_empty());
		CHECK_EQ(FileAccess::get_file_as_string(report_path), "catalog sentinel\n");
		CHECK_EQ(FileAccess::get_file_as_string(manifest_path), manifest_before);
		CHECK_FALSE(DirAccess::dir_exists_absolute(tree.root.path_join("report-artifacts")));
	}

	TEST_CASE("TypeCompleteness UnionPilot runner rejects a catalog at the artifact root") {
		TemporaryProjectTree tree(
				vformat("type_completeness_union_catalog_equals_artifacts_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String catalog_root = stage_union_pilot_completeness_catalog_at(tree, "report-artifacts");
		const String catalog_path = catalog_root.path_join("dimensions/core.json");
		const String catalog_before = FileAccess::get_file_as_string(catalog_path);
		const PackedStringArray entries_before = union_pilot_directory_entries(catalog_root);
		const String report_path = tree.root.path_join("report.json");
		tree.write_file("report.json", "prior report sentinel\n");
		FSCompletenessRunOptions options;
		options.catalog_root = catalog_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = report_path;
		FSCompletenessRunResult result;
		result.success = true;
		result.executed_cells = 99;
		result.report["stale"] = true;

		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_UNAUTHORIZED);
		CHECK_FALSE(result.success);
		CHECK_EQ(result.executed_cells, 0);
		CHECK(result.findings.is_empty());
		CHECK(result.report.is_empty());
		CHECK_EQ(FileAccess::get_file_as_string(report_path), "prior report sentinel\n");
		CHECK_EQ(FileAccess::get_file_as_string(catalog_path), catalog_before);
		CHECK_EQ(union_pilot_directory_entries(catalog_root), entries_before);
	}

	TEST_CASE("TypeCompleteness UnionPilot runner rejects a catalog inside the artifact root") {
		TemporaryProjectTree tree(
				vformat("type_completeness_union_catalog_inside_artifacts_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String artifact_root = tree.root.path_join("report-artifacts");
		const String catalog_root =
				stage_union_pilot_completeness_catalog_at(tree, "report-artifacts/catalog");
		const String catalog_path = catalog_root.path_join("dimensions/core.json");
		const String catalog_before = FileAccess::get_file_as_string(catalog_path);
		const PackedStringArray artifact_entries_before = union_pilot_directory_entries(artifact_root);
		const PackedStringArray catalog_entries_before = union_pilot_directory_entries(catalog_root);
		const String report_path = tree.root.path_join("report.json");
		tree.write_file("report.json", "prior report sentinel\n");
		FSCompletenessRunOptions options;
		options.catalog_root = catalog_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = report_path;
		FSCompletenessRunResult result;
		result.success = true;
		result.executed_cells = 99;
		result.report["stale"] = true;

		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_UNAUTHORIZED);
		CHECK_FALSE(result.success);
		CHECK_EQ(result.executed_cells, 0);
		CHECK(result.findings.is_empty());
		CHECK(result.report.is_empty());
		CHECK_EQ(FileAccess::get_file_as_string(report_path), "prior report sentinel\n");
		CHECK_EQ(FileAccess::get_file_as_string(catalog_path), catalog_before);
		CHECK_EQ(union_pilot_directory_entries(artifact_root), artifact_entries_before);
		CHECK_EQ(union_pilot_directory_entries(catalog_root), catalog_entries_before);
	}

	TEST_CASE("TypeCompleteness UnionPilot runner rejects an artifact root inside its catalog") {
		TemporaryProjectTree tree(
				vformat("type_completeness_union_artifacts_inside_catalog_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String catalog_root = stage_union_pilot_completeness_catalog(tree);
		const String catalog_path = catalog_root.path_join("dimensions/core.json");
		const String catalog_before = FileAccess::get_file_as_string(catalog_path);
		const String report_path = catalog_root.path_join("report.json");
		tree.write_file("catalog/report.json", "prior report sentinel\n");
		const PackedStringArray entries_before = union_pilot_directory_entries(catalog_root);
		FSCompletenessRunOptions options;
		options.catalog_root = catalog_root;
		options.family = "union_destination_membership";
		options.scratch_root = catalog_root;
		options.report_path = report_path;
		FSCompletenessRunResult result;
		result.success = true;
		result.executed_cells = 99;
		result.report["stale"] = true;

		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_UNAUTHORIZED);
		CHECK_FALSE(result.success);
		CHECK_EQ(result.executed_cells, 0);
		CHECK(result.findings.is_empty());
		CHECK(result.report.is_empty());
		CHECK_EQ(FileAccess::get_file_as_string(report_path), "prior report sentinel\n");
		CHECK_EQ(FileAccess::get_file_as_string(catalog_path), catalog_before);
		CHECK_EQ(union_pilot_directory_entries(catalog_root), entries_before);
		CHECK_FALSE(DirAccess::dir_exists_absolute(catalog_root.path_join("report-artifacts")));
	}

	TEST_CASE("TypeCompleteness UnionPilot runner rejects a report path through an outside symlink") {
		TemporaryProjectTree tree(vformat("type_completeness_union_report_link_%d", OS::get_singleton()->get_process_id()));
		TemporaryProjectTree neighbor(vformat("type_completeness_union_report_neighbor_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		REQUIRE(neighbor.is_valid());
		neighbor.write_file("report.json", "caller report sentinel\n");
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(filesystem.is_valid());
		const String report_link = tree.root.path_join("report-link");
		REQUIRE_EQ(filesystem->create_link(neighbor.root, report_link), OK);
		const PackedStringArray neighbor_entries_before = union_pilot_directory_entries(neighbor.root);

		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = report_link.path_join("report.json");
		FSCompletenessRunResult result;
		result.success = true;
		result.executed_cells = 99;
		result.findings.push_back(FSCompletenessFinding());
		result.report["stale"] = true;
		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_UNAUTHORIZED);
		CHECK_FALSE(result.success);
		CHECK_EQ(result.executed_cells, 0);
		CHECK(result.findings.is_empty());
		CHECK(result.report.is_empty());
		CHECK_EQ(FileAccess::get_file_as_string(neighbor.root.path_join("report.json")),
				"caller report sentinel\n");
		CHECK_EQ(union_pilot_directory_entries(neighbor.root), neighbor_entries_before);
	}

	TEST_CASE("TypeCompleteness UnionPilot runner rejects a symlinked report file") {
		TemporaryProjectTree tree(vformat("type_completeness_union_report_file_link_%d", OS::get_singleton()->get_process_id()));
		TemporaryProjectTree neighbor(vformat("type_completeness_union_report_file_neighbor_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		REQUIRE(neighbor.is_valid());
		neighbor.write_file("caller-report.json", "caller report sentinel\n");
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(filesystem.is_valid());
		const String report_path = tree.root.path_join("report.json");
		REQUIRE_EQ(filesystem->create_link(neighbor.root.path_join("caller-report.json"), report_path), OK);
		const PackedStringArray neighbor_entries_before = union_pilot_directory_entries(neighbor.root);

		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = report_path;
		FSCompletenessRunResult result;
		result.success = true;
		result.executed_cells = 99;
		result.findings.push_back(FSCompletenessFinding());
		result.report["stale"] = true;
		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_UNAUTHORIZED);
		CHECK_FALSE(result.success);
		CHECK_EQ(result.executed_cells, 0);
		CHECK(result.findings.is_empty());
		CHECK(result.report.is_empty());
		CHECK(filesystem->is_link(report_path));
		CHECK_EQ(FileAccess::get_file_as_string(neighbor.root.path_join("caller-report.json")),
				"caller report sentinel\n");
		CHECK_EQ(union_pilot_directory_entries(neighbor.root), neighbor_entries_before);
	}

	TEST_CASE("TypeCompleteness UnionPilot runner preserves reports when temp names are exhausted") {
		TemporaryProjectTree tree(vformat("type_completeness_union_report_collision_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		tree.write_file("report.json", "caller report sentinel\n");
		const String report_path = tree.root.path_join("report.json");
		for (int attempt = 0; attempt < 128; attempt++) {
			tree.write_file(vformat("report.json.tmp.%d.%d", OS::get_singleton()->get_process_id(), attempt),
					"occupied temp\n");
		}
		const PackedStringArray entries_before = union_pilot_directory_entries(tree.root);

		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = report_path;
		FSCompletenessRunResult result;
		result.success = true;
		result.executed_cells = 99;
		result.findings.push_back(FSCompletenessFinding());
		result.report["stale"] = true;
		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_ALREADY_EXISTS);
		CHECK_FALSE(result.success);
		CHECK_EQ(result.executed_cells, 0);
		CHECK(result.findings.is_empty());
		CHECK(result.report.is_empty());
		CHECK_EQ(FileAccess::get_file_as_string(report_path), "caller report sentinel\n");
		CHECK_EQ(union_pilot_directory_entries(tree.root), entries_before);
	}

	TEST_CASE("TypeCompleteness UnionPilot runner rejects persisted artifact truncation before execution") {
		UnionPilotProgramMutationScope mutation_scope;
		TemporaryProjectTree tree(
				vformat("type_completeness_union_truncated_artifact_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const PackedStringArray entries_before = union_pilot_directory_entries(tree.root);

		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.persisted_write_hook = truncate_first_union_pilot_runner_artifact;
		FSCompletenessRunResult result;
		result.success = true;
		result.executed_cells = 99;
		result.findings.push_back(FSCompletenessFinding());
		result.report["stale"] = true;

		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_FILE_CORRUPT);
		CHECK_EQ(union_pilot_runner_artifact_mutation_count, 1);
		CHECK_FALSE(result.success);
		CHECK_EQ(result.executed_cells, 0);
		CHECK(result.findings.is_empty());
		CHECK(result.report.is_empty());
		CHECK_EQ(union_pilot_directory_entries(tree.root), entries_before);
	}

	TEST_CASE("TypeCompleteness UnionPilot runner preserves reports after persisted temp truncation") {
		UnionPilotProgramMutationScope mutation_scope;
		TemporaryProjectTree tree(
				vformat("type_completeness_union_truncated_report_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		tree.write_file("report.json", "caller report sentinel\n");
		const String report_path = tree.root.path_join("report.json");
		const PackedStringArray entries_before = union_pilot_directory_entries(tree.root);

		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = report_path;
		options.persisted_write_hook = truncate_union_pilot_runner_report_temp;
		FSCompletenessRunResult result;
		result.success = true;
		result.executed_cells = 99;
		result.findings.push_back(FSCompletenessFinding());
		result.report["stale"] = true;

		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_FILE_CORRUPT);
		CHECK_EQ(union_pilot_runner_report_mutation_count, 1);
		CHECK_FALSE(result.success);
		CHECK_EQ(result.executed_cells, 0);
		CHECK(result.findings.is_empty());
		CHECK(result.report.is_empty());
		CHECK_EQ(FileAccess::get_file_as_string(report_path), "caller report sentinel\n");
		CHECK_EQ(union_pilot_directory_entries(tree.root), entries_before);
	}

	TEST_CASE("TypeCompleteness UnionPilot runner reconciles a direct mismatch with parity evidence") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		union_pilot_mutated_case_id.clear();
		for (const FSCompletenessResolvedCell &cell : resolution.cells) {
			if (cell.coordinates.get("surface", String()) == "text" &&
					cell.coordinates.get("destination", String()) == "union" &&
					cell.coordinates.get("source_proof", String()) == "numeric_constant" &&
					cell.coordinates.get("boundary", String()) == "argument_binding") {
				union_pilot_mutated_case_id = cell.case_id;
				break;
			}
		}
		REQUIRE_FALSE(union_pilot_mutated_case_id.is_empty());
		union_pilot_mutation_count = 0;

		TemporaryProjectTree tree(vformat("type_completeness_union_mismatch_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.observation_mutator = corrupt_union_pilot_stored_carrier;
		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), FAILED);
		CHECK_FALSE(result.success);
		CHECK_EQ(result.executed_cells, 40);
		CHECK_EQ(union_pilot_mutation_count, 1);
		REQUIRE_EQ(result.findings.size(), 1);
		if (result.findings.size() != 1) {
			return;
		}
		const FSCompletenessFinding &finding = result.findings[0];
		CHECK_FALSE(finding.finding_id.is_empty());
		CHECK_EQ(finding.case_id, union_pilot_mutated_case_id);
		CHECK_EQ(finding.family, options.family);
		CHECK_EQ(finding.dimension, "stored_carrier");
		CHECK_EQ(String(finding.expected), "admitting_alternative");
		CHECK_EQ(String(finding.actual), "plain_destination");
		CHECK_EQ(finding.classification, "unclassified");
		CHECK_FALSE(finding.artifact_path.is_empty());
		CHECK(FileAccess::exists(finding.artifact_path));
		CHECK_EQ(String(finding.parity_evidence.get("text", String())), "plain_destination");
		CHECK_EQ(String(finding.parity_evidence.get("bytecode", String())), "admitting_alternative");

		const Dictionary report = result.report;
		CHECK_EQ(bool(report.get("success", true)), false);
		CHECK_EQ(int(report.get("text_bytecode_parity_failures", 0)), 1);
		const Array report_findings = report.get("findings", Array());
		REQUIRE_EQ(report_findings.size(), 1);
		const Dictionary report_finding = report_findings[0];
		CHECK_EQ(String(report_finding.get("case_id", String())), union_pilot_mutated_case_id);
		CHECK_EQ(String(report_finding.get("dimension", String())), "stored_carrier");
		CHECK_FALSE(Dictionary(report_finding.get("parity_evidence", Dictionary())).is_empty());
		CHECK_EQ(Array(report.get("cases", Array())).size(), 40);

		Error read_error = OK;
		const String report_source = FileAccess::get_file_as_string(options.report_path, &read_error);
		REQUIRE_EQ(read_error, OK);
		JSON json;
		REQUIRE_EQ(json.parse(report_source), OK);
		CHECK_EQ(Dictionary(json.get_data()), report);
	}

	TEST_CASE("TypeCompleteness UnionPilot runner rejects matching wrong output without parity") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		select_union_pilot_pair(resolution, "plain", "static_member", "reflective_write");
		TemporaryProjectTree tree(vformat("type_completeness_union_wrong_output_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());

		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.observation_mutator = corrupt_union_pilot_both_outputs;
		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), FAILED);
		CHECK_FALSE(result.success);
		REQUIRE_EQ(result.findings.size(), 2);
		for (const FSCompletenessFinding &finding : result.findings) {
			CHECK_EQ(finding.dimension, "output");
			CHECK_EQ(String(finding.expected), "uint 5\n");
			CHECK_EQ(String(finding.actual), "wrong output\n");
			CHECK(finding.parity_evidence.is_empty());
		}
		CHECK_EQ(int(result.report.get("text_bytecode_parity_failures", -1)), 0);
		const Array cases = result.report.get("cases", Array());
		int observed_cases = 0;
		for (int i = 0; i < cases.size(); i++) {
			const Dictionary case_report = cases[i];
			const String case_id = case_report.get("case_id", String());
			if (case_id != union_pilot_mutated_pair_text_id &&
					case_id != union_pilot_mutated_pair_bytecode_id) {
				continue;
			}
			observed_cases++;
			CHECK_EQ(String(case_report.get("status", String())), "failed");
			CHECK_FALSE(bool(case_report.get("passed", true)));
			CHECK_EQ(bool(case_report.get("runtime_passed", false)), true);
			CHECK_EQ(String(case_report.get("runtime_status", String())), "ok");
			CHECK(Array(case_report.get("diagnostics", Array())).is_empty());
			CHECK_EQ(String(case_report.get("produced_output", String())), "wrong output\n");
			CHECK_EQ(String(case_report.get("expected_output", String())), "uint 5\n");
		}
		CHECK_EQ(observed_cases, 2);
	}

	TEST_CASE("TypeCompleteness UnionPilot runner attaches output parity to a direct finding") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		select_union_pilot_pair(resolution, "plain", "static_member", "reflective_write");
		TemporaryProjectTree tree(vformat("type_completeness_union_one_output_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());

		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.observation_mutator = corrupt_union_pilot_text_output;
		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), FAILED);
		REQUIRE_EQ(result.findings.size(), 1);
		if (result.findings.size() != 1) {
			return;
		}
		CHECK_EQ(result.findings[0].case_id, union_pilot_mutated_pair_text_id);
		CHECK_EQ(result.findings[0].dimension, "output");
		const Dictionary output_evidence = result.findings[0].parity_evidence.get("output", Dictionary());
		CHECK_EQ(String(output_evidence.get("text", String())), "wrong output\n");
		CHECK_EQ(String(output_evidence.get("bytecode", String())), "uint 5\n");
		CHECK_EQ(int(result.report.get("text_bytecode_parity_failures", 0)), 1);
		int failed_pair_cases = 0;
		const Array cases = result.report.get("cases", Array());
		for (int i = 0; i < cases.size(); i++) {
			const Dictionary case_report = cases[i];
			const String case_id = case_report.get("case_id", String());
			if (case_id == union_pilot_mutated_pair_text_id ||
					case_id == union_pilot_mutated_pair_bytecode_id) {
				CHECK_EQ(String(case_report.get("status", String())), "failed");
				CHECK_FALSE(bool(case_report.get("passed", true)));
				CHECK_EQ(bool(case_report.get("runtime_passed", false)), true);
				failed_pair_cases++;
			}
		}
		CHECK_EQ(failed_pair_cases, 2);
	}

	TEST_CASE("TypeCompleteness UnionPilot runner reports diagnostics on a non witness") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		union_pilot_mutated_case_id.clear();
		for (const FSCompletenessResolvedCell &cell : resolution.cells) {
			if (cell.coordinates.get("surface", String()) == "text" &&
					cell.coordinates.get("destination", String()) == "plain" &&
					cell.coordinates.get("source_proof", String()) == "static_member" &&
					cell.coordinates.get("boundary", String()) == "reflective_write") {
				union_pilot_mutated_case_id = cell.case_id;
				break;
			}
		}
		REQUIRE_FALSE(union_pilot_mutated_case_id.is_empty());
		TemporaryProjectTree tree(vformat("type_completeness_union_diagnostic_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());

		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.observation_mutator = add_union_pilot_diagnostic;
		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), FAILED);
		REQUIRE_EQ(result.findings.size(), 1);
		if (result.findings.size() != 1) {
			return;
		}
		CHECK_EQ(result.findings[0].case_id, union_pilot_mutated_case_id);
		CHECK_EQ(result.findings[0].dimension, "diagnostics");
		const PackedStringArray actual = result.findings[0].actual;
		REQUIRE_EQ(actual.size(), 1);
		CHECK_EQ(actual[0], "injected diagnostic");
		CHECK(FileAccess::exists(options.report_path));
	}

	TEST_CASE("TypeCompleteness UnionPilot runner reports failed runtime status") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		select_union_pilot_pair(resolution, "plain", "static_member", "reflective_write");
		union_pilot_mutated_case_id = union_pilot_mutated_pair_text_id;
		TemporaryProjectTree tree(vformat("type_completeness_union_runtime_status_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());

		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.runtime_result_mutator = fail_union_pilot_runtime_result;
		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), FAILED);
		REQUIRE_EQ(result.findings.size(), 1);
		if (result.findings.size() != 1) {
			return;
		}
		CHECK_EQ(result.findings[0].case_id, union_pilot_mutated_case_id);
		CHECK_EQ(result.findings[0].dimension, "runtime_status");
		const Dictionary actual = result.findings[0].actual;
		CHECK_EQ(bool(actual.get("passed", true)), false);
		CHECK_EQ(String(actual.get("status", String())), "injected_failure");
		CHECK_FALSE(result.findings[0].parity_evidence.is_empty());
		CHECK_EQ(int(result.report.get("text_bytecode_parity_failures", 0)), 1);
	}

	TEST_CASE("TypeCompleteness UnionPilot runner reports witness observation regressions") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		const Dictionary coordinates = union_pilot_witness_coordinates("text_gradual_argument_binding");
		REQUIRE_FALSE(coordinates.is_empty());
		union_pilot_mutated_case_id.clear();
		for (const FSCompletenessResolvedCell &cell : resolution.cells) {
			if (cell.coordinates == coordinates) {
				union_pilot_mutated_case_id = cell.case_id;
				break;
			}
		}
		REQUIRE_FALSE(union_pilot_mutated_case_id.is_empty());
		TemporaryProjectTree tree(vformat("type_completeness_union_witness_regression_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());

		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.observation_mutator = corrupt_union_pilot_witness_dimension;
		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), FAILED);
		REQUIRE_EQ(result.findings.size(), 1);
		if (result.findings.size() != 1) {
			return;
		}
		CHECK_EQ(result.findings[0].case_id, union_pilot_mutated_case_id);
		CHECK_EQ(result.findings[0].dimension, "runtime_obligation");
		CHECK_EQ(String(result.findings[0].expected), "union_membership_check");
		CHECK_EQ(String(result.findings[0].actual), "typed_destination_check");
		CHECK(FileAccess::exists(options.report_path));
	}

	TEST_CASE("TypeCompleteness UnionPilot runner preserves all pair disagreement evidence") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		select_union_pilot_pair(resolution, "union", "numeric_constant", "argument_binding");
		TemporaryProjectTree tree(vformat("type_completeness_union_multiple_mismatch_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());

		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.observation_mutator = corrupt_union_pilot_multiple_observations;
		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), FAILED);
		REQUIRE_EQ(result.findings.size(), 4);
		int bytecode_direct_findings = 0;
		for (const FSCompletenessFinding &finding : result.findings) {
			const Dictionary evidence = finding.parity_evidence;
			CHECK_FALSE(evidence.is_empty());
			CHECK_FALSE(Dictionary(evidence.get("output", Dictionary())).is_empty());
			const Dictionary dimensions = evidence.get("dimensions", Dictionary());
			CHECK(dimensions.has("analysis"));
			CHECK(dimensions.has("stored_carrier"));
			if (finding.case_id == union_pilot_mutated_pair_bytecode_id) {
				bytecode_direct_findings++;
			}
		}
		CHECK_EQ(bytecode_direct_findings, 1);
		CHECK_EQ(int(result.report.get("text_bytecode_parity_failures", 0)), 1);
	}

	TEST_CASE("TypeCompleteness UnionPilot runner reconciles a terminal ledger entry") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		union_pilot_mutated_case_id.clear();
		for (const FSCompletenessResolvedCell &cell : resolution.cells) {
			if (cell.coordinates.get("surface", String()) == "text" &&
					cell.coordinates.get("destination", String()) == "union" &&
					cell.coordinates.get("source_proof", String()) == "numeric_constant" &&
					cell.coordinates.get("boundary", String()) == "argument_binding") {
				union_pilot_mutated_case_id = cell.case_id;
				break;
			}
		}
		REQUIRE_FALSE(union_pilot_mutated_case_id.is_empty());
		const String finding_id = "fstcf-v1-" +
				(union_pilot_mutated_case_id + "|stored_carrier").sha256_text().substr(0, 20);

		TemporaryProjectTree tree(vformat("type_completeness_union_known_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String catalog_root = stage_union_pilot_completeness_catalog(tree);
		const String fixture_path = "modules/foundry_script/tests/scripts/analyzer/features/cast_non_null.fs";
		tree.write_file("catalog/findings/" + finding_id + ".json",
				union_pilot_finding_record(finding_id, union_pilot_mutated_case_id, "product_defect", String(),
						vformat("\"modules/foundry_script/tests/test_type_completeness_union_pilot.h\", \"%s\"",
								fixture_path)));

		FSCompletenessRunOptions options;
		options.catalog_root = catalog_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.observation_mutator = corrupt_union_pilot_stored_carrier;
		FSCompletenessRunResult result;
		REQUIRE_EQ(FSCompletenessRunner::run(options, result), FAILED);
		CHECK_FALSE(result.success);
		CHECK_EQ(result.executed_cells, 40);
		REQUIRE_EQ(result.findings.size(), 1);
		if (result.findings.size() != 1) {
			return;
		}
		CHECK_EQ(result.findings[0].finding_id, finding_id);
		CHECK_EQ(result.findings[0].classification, "product_defect");
		CHECK_EQ(result.findings[0].issue_url, "https://example.invalid/issues/1");
		CHECK_EQ(result.findings[0].closure_packet_url, "https://example.invalid/closure/1");
		REQUIRE_EQ(result.findings[0].permanent_test_paths.size(), 2);
		CHECK_EQ(result.findings[0].permanent_test_paths[0],
				"modules/foundry_script/tests/test_type_completeness_union_pilot.h");
		CHECK_EQ(result.findings[0].permanent_test_paths[1], fixture_path);
		CHECK_FALSE(result.findings[0].parity_evidence.is_empty());
		CHECK_EQ(Array(result.report.get("findings", Array())).size(), 1);
		CHECK_EQ(bool(result.report.get("success", true)), false);
		CHECK_EQ(int(result.report.get("text_bytecode_parity_failures", 0)), 1);
		CHECK(FileAccess::exists(options.report_path));
		Error read_error = OK;
		const String report_source = FileAccess::get_file_as_string(options.report_path, &read_error);
		REQUIRE_EQ(read_error, OK);
		JSON json;
		REQUIRE_EQ(json.parse(report_source), OK);
		const Dictionary report = json.get_data();
		CHECK_EQ(bool(report.get("success", true)), false);
		const Array report_findings = report.get("findings", Array());
		REQUIRE_EQ(report_findings.size(), 1);
		CHECK_EQ(String(Dictionary(report_findings[0]).get("classification", String())), "product_defect");
	}

	TEST_CASE("TypeCompleteness UnionPilot runner ignores dot-prefixed findings metadata") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		union_pilot_mutated_case_id.clear();
		for (const FSCompletenessResolvedCell &cell : resolution.cells) {
			if (cell.coordinates.get("surface", String()) == "text" &&
					cell.coordinates.get("destination", String()) == "union" &&
					cell.coordinates.get("source_proof", String()) == "numeric_constant" &&
					cell.coordinates.get("boundary", String()) == "argument_binding") {
				union_pilot_mutated_case_id = cell.case_id;
				break;
			}
		}
		REQUIRE_FALSE(union_pilot_mutated_case_id.is_empty());
		const String finding_id = "fstcf-v1-" +
				(union_pilot_mutated_case_id + "|stored_carrier").sha256_text().substr(0, 20);

		TemporaryProjectTree tree(
				vformat("type_completeness_union_findings_dot_entry_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String catalog_root = stage_union_pilot_completeness_catalog(tree);
		tree.write_file("catalog/findings/" + finding_id + ".json",
				union_pilot_finding_record(finding_id, union_pilot_mutated_case_id, "product_defect"));
		tree.write_file("catalog/findings/.DS_Store", "filesystem metadata");

		FSCompletenessRunOptions options;
		options.catalog_root = catalog_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.observation_mutator = corrupt_union_pilot_stored_carrier;
		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), FAILED);
		REQUIRE_EQ(result.findings.size(), 1);
		if (result.findings.size() != 1) {
			return;
		}
		CHECK_EQ(result.findings[0].finding_id, finding_id);
		CHECK_EQ(result.findings[0].classification, "product_defect");
	}

	TEST_CASE("TypeCompleteness UnionPilot runner rejects visible unexpected findings entries with a diagnostic") {
		TemporaryProjectTree tree(
				vformat("type_completeness_union_findings_visible_entry_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String catalog_root = stage_union_pilot_completeness_catalog(tree);
		tree.write_file("catalog/findings/notes.txt", "unexpected");

		FSCompletenessRunOptions options;
		options.catalog_root = catalog_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		FSCompletenessRunResult result;
		UnionPilotWarningRecorder diagnostics;
		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_INVALID_DATA);
		CHECK(result.findings.is_empty());
		CHECK(diagnostics.messages.contains("unexpected non-JSON findings entry 'notes.txt'"));
	}

	TEST_CASE("TypeCompleteness UnionPilot runner falls back when Git tracked-file verification is unavailable") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		union_pilot_mutated_case_id.clear();
		for (const FSCompletenessResolvedCell &cell : resolution.cells) {
			if (cell.coordinates.get("surface", String()) == "text" &&
					cell.coordinates.get("destination", String()) == "union" &&
					cell.coordinates.get("source_proof", String()) == "numeric_constant" &&
					cell.coordinates.get("boundary", String()) == "argument_binding") {
				union_pilot_mutated_case_id = cell.case_id;
				break;
			}
		}
		REQUIRE_FALSE(union_pilot_mutated_case_id.is_empty());
		const String finding_id = "fstcf-v1-" +
				(union_pilot_mutated_case_id + "|stored_carrier").sha256_text().substr(0, 20);

		TemporaryProjectTree tree(
				vformat("type_completeness_union_git_unavailable_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String catalog_root = stage_union_pilot_completeness_catalog(tree);
		tree.write_file("catalog/findings/" + finding_id + ".json",
				union_pilot_finding_record(finding_id, union_pilot_mutated_case_id, "product_defect"));

		union_pilot_tracked_file_probe_error = ERR_CANT_FORK;
		union_pilot_tracked_file_probe_exit_code = -1;
		union_pilot_tracked_file_probe_repository_root.clear();
		union_pilot_tracked_file_probe_path.clear();
		FSCompletenessRunOptions options;
		options.catalog_root = catalog_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.observation_mutator = corrupt_union_pilot_stored_carrier;
		options.tracked_file_probe = union_pilot_tracked_file_probe;
		FSCompletenessRunResult result;
		UnionPilotWarningRecorder warnings;
		CHECK_EQ(FSCompletenessRunner::run(options, result), FAILED);
		REQUIRE_EQ(result.findings.size(), 1);
		if (result.findings.size() != 1) {
			return;
		}
		CHECK_EQ(result.findings[0].classification, "product_defect");
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(filesystem.is_valid());
		CHECK_EQ(union_pilot_tracked_file_probe_repository_root,
				filesystem->get_current_dir().replace("\\", "/").simplify_path());
		CHECK_EQ(union_pilot_tracked_file_probe_path,
				"modules/foundry_script/tests/test_type_completeness_union_pilot.h");
		CHECK(warnings.messages.contains("Git tracked-file verification is unavailable"));
		CHECK(warnings.messages.contains("accepting canonical existing test path"));
	}

	TEST_CASE("TypeCompleteness UnionPilot runner rejects an untracked permanent test path") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		String current_case_id;
		for (const FSCompletenessResolvedCell &cell : resolution.cells) {
			if (cell.dimensions.has("stored_carrier")) {
				current_case_id = cell.case_id;
				break;
			}
		}
		REQUIRE_FALSE(current_case_id.is_empty());
		const String finding_id = "fstcf-v1-untracked-permanent-test";

		TemporaryProjectTree tree(
				vformat("type_completeness_union_git_untracked_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String catalog_root = stage_union_pilot_completeness_catalog(tree);
		tree.write_file("catalog/findings/" + finding_id + ".json",
				union_pilot_finding_record(finding_id, current_case_id, "product_defect"));

		union_pilot_tracked_file_probe_error = OK;
		union_pilot_tracked_file_probe_exit_code = 1;
		union_pilot_tracked_file_probe_repository_root.clear();
		union_pilot_tracked_file_probe_path.clear();
		FSCompletenessRunOptions options;
		options.catalog_root = catalog_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.tracked_file_probe = union_pilot_tracked_file_probe;
		FSCompletenessRunResult result;
		UnionPilotWarningRecorder warnings;
		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_INVALID_DATA);
		CHECK(result.findings.is_empty());
		CHECK_EQ(union_pilot_tracked_file_probe_path,
				"modules/foundry_script/tests/test_type_completeness_union_pilot.h");
		CHECK_FALSE(warnings.messages.contains("Git tracked-file verification is unavailable"));
	}

	TEST_CASE("TypeCompleteness UnionPilot runner rejects a linked findings entry atomically") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		union_pilot_mutated_case_id.clear();
		for (const FSCompletenessResolvedCell &cell : resolution.cells) {
			if (cell.coordinates.get("surface", String()) == "text" &&
					cell.coordinates.get("destination", String()) == "union" &&
					cell.coordinates.get("source_proof", String()) == "numeric_constant" &&
					cell.coordinates.get("boundary", String()) == "argument_binding") {
				union_pilot_mutated_case_id = cell.case_id;
				break;
			}
		}
		REQUIRE_FALSE(union_pilot_mutated_case_id.is_empty());
		const String finding_id = "fstcf-v1-" +
				(union_pilot_mutated_case_id + "|stored_carrier").sha256_text().substr(0, 20);

		TemporaryProjectTree tree(
				vformat("type_completeness_union_linked_finding_%d", OS::get_singleton()->get_process_id()));
		TemporaryProjectTree neighbor(
				vformat("type_completeness_union_linked_finding_neighbor_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		REQUIRE(neighbor.is_valid());
		const String catalog_root = stage_union_pilot_completeness_catalog(tree);
		const String neighbor_record = union_pilot_finding_record(
				finding_id, union_pilot_mutated_case_id, "product_defect");
		neighbor.write_file("outside-finding.json", neighbor_record);
		const PackedStringArray neighbor_entries_before = union_pilot_directory_entries(neighbor.root);
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(filesystem.is_valid());
		const String linked_finding_path = catalog_root.path_join("findings/" + finding_id + ".json");
		REQUIRE_EQ(filesystem->create_link(
						   neighbor.root.path_join("outside-finding.json"), linked_finding_path),
				OK);
		tree.write_file("report.json", "prior report sentinel\n");

		FSCompletenessRunOptions options;
		options.catalog_root = catalog_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.observation_mutator = corrupt_union_pilot_stored_carrier;
		FSCompletenessRunResult result;
		result.success = true;
		result.executed_cells = 99;
		result.findings.push_back(FSCompletenessFinding());
		result.report["stale"] = true;

		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_UNAUTHORIZED);
		CHECK_FALSE(result.success);
		CHECK_EQ(result.executed_cells, 0);
		CHECK(result.findings.is_empty());
		CHECK(result.report.is_empty());
		CHECK_EQ(FileAccess::get_file_as_string(options.report_path), "prior report sentinel\n");
		CHECK(filesystem->is_link(linked_finding_path));
		CHECK_EQ(FileAccess::get_file_as_string(neighbor.root.path_join("outside-finding.json")), neighbor_record);
		CHECK_EQ(union_pilot_directory_entries(neighbor.root), neighbor_entries_before);
	}

	TEST_CASE("TypeCompleteness UnionPilot runner expands a split migration deterministically") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		select_union_pilot_pair(resolution, "union", "numeric_constant", "argument_binding");
		const String old_case_id = "fstc-v1-split-union-numeric";
		const String finding_id = "fstcf-v1-split-union-numeric";
		TemporaryProjectTree tree(vformat("type_completeness_union_split_migration_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String catalog_root = stage_union_pilot_completeness_catalog(tree);
		tree.write_file("catalog/migrations/v1.json", vformat(R"JSON({
	"schema_version": 1,
	"migrations": [{
		"old_id": "%s",
		"new_ids": ["%s", "%s"],
		"reason": "surface split"
	}]
}
)JSON",
															  old_case_id, union_pilot_mutated_pair_text_id, union_pilot_mutated_pair_bytecode_id));
		tree.write_file("catalog/findings/" + finding_id + ".json",
				union_pilot_finding_record(finding_id, old_case_id, "product_defect"));

		FSCompletenessRunOptions options;
		options.catalog_root = catalog_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.observation_mutator = corrupt_union_pilot_both_stored_carriers;
		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), FAILED);
		CHECK_FALSE(result.success);
		REQUIRE_EQ(result.findings.size(), 2);
		const Array report_findings = result.report.get("findings", Array());
		REQUIRE_EQ(report_findings.size(), 2);
		for (int i = 0; i < report_findings.size(); i++) {
			const Dictionary finding = report_findings[i];
			CHECK_EQ(String(finding.get("finding_id", String())), finding_id);
			CHECK_EQ(String(finding.get("classification", String())), "product_defect");
			CHECK_EQ(String(finding.get("migrated_from", String())), old_case_id);
			const Array resolved_ids = finding.get("resolved_case_ids", Array());
			REQUIRE_EQ(resolved_ids.size(), 2);
			CHECK_EQ(String(resolved_ids[0]), union_pilot_mutated_pair_text_id < union_pilot_mutated_pair_bytecode_id ? union_pilot_mutated_pair_text_id : union_pilot_mutated_pair_bytecode_id);
			CHECK_EQ(String(resolved_ids[1]), union_pilot_mutated_pair_text_id < union_pilot_mutated_pair_bytecode_id ? union_pilot_mutated_pair_bytecode_id : union_pilot_mutated_pair_text_id);
		}
	}

	TEST_CASE("TypeCompleteness UnionPilot runner rejects malformed unresolved and duplicate ledger records atomically") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		String current_case_id;
		for (const FSCompletenessResolvedCell &cell : resolution.cells) {
			if (cell.dimensions.has("stored_carrier")) {
				current_case_id = cell.case_id;
				break;
			}
		}
		REQUIRE_FALSE(current_case_id.is_empty());
		const String finding_id = "fstcf-v1-ledger-validation";
		const String other_finding_id = "fstcf-v1-ledger-validation-other";
		struct InvalidLedgerCase {
			const char *name;
			String first;
			String second;
			String first_filename;
			String second_filename;
			Error expected_error;
		};
		const InvalidLedgerCase invalid_cases[] = {
			{ "unknown_field",
					union_pilot_finding_record(finding_id, current_case_id, "product_defect",
							",\n\t\"unexpected\": true"),
					String(), finding_id + ".json", String(), ERR_INVALID_DATA },
			{ "unresolved_case", union_pilot_finding_record(finding_id, "fstc-v1-does-not-exist", "product_defect"),
					String(), finding_id + ".json", String(), ERR_INVALID_DATA },
			{ "unknown_classification",
					union_pilot_finding_record(finding_id, current_case_id, "not_a_classification"),
					String(), finding_id + ".json", String(), ERR_INVALID_DATA },
			{ "filename_mismatch", union_pilot_finding_record(finding_id, current_case_id, "product_defect"),
					String(), "wrong-name.json", String(), ERR_INVALID_DATA },
			{ "duplicate_raw_member",
					union_pilot_finding_record(finding_id, current_case_id, "product_defect",
							",\n\t\"family\": \"union_destination_membership\""),
					String(), finding_id + ".json", String(), ERR_INVALID_DATA },
			{ "unsafe_permanent_path",
					union_pilot_finding_record(finding_id, current_case_id, "product_defect")
							.replace("modules/foundry_script/tests/test_type_completeness_union_pilot.h", "../outside.h"),
					String(), finding_id + ".json", String(), ERR_INVALID_DATA },
			{ "tracked_readme_permanent_path",
					union_pilot_finding_record(finding_id, current_case_id, "product_defect")
							.replace("modules/foundry_script/tests/test_type_completeness_union_pilot.h", "README.md"),
					String(), finding_id + ".json", String(), ERR_INVALID_DATA },
			{ "tracked_source_permanent_path",
					union_pilot_finding_record(finding_id, current_case_id, "product_defect")
							.replace("modules/foundry_script/tests/test_type_completeness_union_pilot.h",
									"modules/foundry_script/fs_parser.cpp"),
					String(), finding_id + ".json", String(), ERR_INVALID_DATA },
			{ "reconciliation_collision", union_pilot_finding_record(finding_id, current_case_id, "product_defect"),
					union_pilot_finding_record(other_finding_id, current_case_id, "duplicate"),
					finding_id + ".json", other_finding_id + ".json", ERR_INVALID_DATA },
		};

		for (const InvalidLedgerCase &invalid : invalid_cases) {
			CAPTURE(invalid.name);
			TemporaryProjectTree tree(vformat("type_completeness_union_ledger_%s_%d",
					invalid.name, OS::get_singleton()->get_process_id()));
			REQUIRE(tree.is_valid());
			const String catalog_root = stage_union_pilot_completeness_catalog(tree);
			tree.write_file("catalog/findings/" + invalid.first_filename, invalid.first);
			if (!invalid.second.is_empty()) {
				tree.write_file("catalog/findings/" + invalid.second_filename, invalid.second);
			}
			tree.write_file("report.json", "caller report sentinel\n");

			FSCompletenessRunOptions options;
			options.catalog_root = catalog_root;
			options.family = "union_destination_membership";
			options.scratch_root = tree.root;
			options.report_path = tree.root.path_join("report.json");
			FSCompletenessRunResult result;
			result.success = true;
			result.executed_cells = 99;
			result.findings.push_back(FSCompletenessFinding());
			result.report["stale"] = true;
			CHECK_EQ(FSCompletenessRunner::run(options, result), invalid.expected_error);
			CHECK_FALSE(result.success);
			CHECK_EQ(result.executed_cells, 0);
			CHECK(result.findings.is_empty());
			CHECK(result.report.is_empty());
			CHECK_EQ(FileAccess::get_file_as_string(options.report_path), "caller report sentinel\n");
		}
	}

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

			const FSCompletenessProgram *text_program = nullptr;
			for (const FSCompletenessProgram &program : programs) {
				if (program.surface == "text") {
					text_program = &program;
					break;
				}
			}
			REQUIRE(text_program != nullptr);
			tree.write_file("sentinel.txt", "caller sentinel\n");
			tree.write_file("text/project.foundry", "caller project sentinel\n");
			tree.write_file("text/" + text_program->case_id + ".fs", "caller source sentinel\n");
			tree.write_file("text/stray.fs", text_program->source);
			tree.write_file("text/stray.out", "FS_TEST_OK\n" + text_program->expected_output);
			const PackedStringArray caller_entries_before = union_pilot_directory_entries(tree.root);

			FSCompletenessRuntimeBatch batch;
			const Error execution_error = FSUnionCompletenessAdapter::shared().execute(tree.root, programs, batch);
			REQUIRE_EQ(execution_error, OK);
			if (execution_error != OK) {
				return;
			}
			CHECK_EQ(batch.text.size(), 20);
			CHECK_EQ(batch.bytecode.size(), 20);

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
				const int64_t original_instance_id = result->dimensions.get("original_instance_id", int64_t(0));
				const int64_t inspected_instance_id = result->dimensions.get("inspected_instance_id", int64_t(0));
				CHECK_NE(original_instance_id, 0);
				CHECK_NE(inspected_instance_id, 0);
				if (surface == "bytecode") {
					CHECK_NE(original_instance_id, inspected_instance_id);
					CHECK_EQ(bool(result->dimensions.get("inspected_compiled_binary", false)), true);
				} else {
					CHECK_EQ(original_instance_id, inspected_instance_id);
					CHECK_EQ(bool(result->dimensions.get("inspected_compiled_binary", true)), false);
				}
				if (cell.coordinates.get("boundary", String()) == "reflective_write") {
					const int64_t owner_instance_id =
							result->dimensions.get("inspected_owner_instance_id", int64_t(0));
					CHECK_NE(owner_instance_id, 0);
					CHECK_NE(owner_instance_id, inspected_instance_id);
				} else {
					CHECK_FALSE(result->dimensions.has("inspected_owner_instance_id"));
				}

				const String pair_key = String(cell.coordinates.get("destination", String())) + "|" +
						String(cell.coordinates.get("source_proof", String())) + "|" +
						String(cell.coordinates.get("boundary", String()));
				semantic_pairs.insert(pair_key);
			}
			CHECK_EQ(semantic_pairs.size(), 20);

			CHECK_EQ(union_pilot_directory_entries(tree.root), caller_entries_before);
			CHECK_EQ(FileAccess::get_file_as_string(tree.root.path_join("sentinel.txt")), "caller sentinel\n");
			CHECK_EQ(FileAccess::get_file_as_string(tree.root.path_join("text/project.foundry")),
					"caller project sentinel\n");
			CHECK_EQ(FileAccess::get_file_as_string(
							 tree.root.path_join("text/" + text_program->case_id + ".fs")),
					"caller source sentinel\n");
			CHECK(FileAccess::exists(tree.root.path_join("text/stray.fs")));
			CHECK(FileAccess::exists(tree.root.path_join("text/stray.out")));
			CHECK_FALSE(FileAccess::exists(tree.root.get_base_dir().path_join(programs[0].case_id + ".fs")));
		}
		CHECK_FALSE(FileAccess::exists(owned_root));
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(filesystem.is_valid());
		CHECK_FALSE(filesystem->dir_exists(owned_root));
	}

	TEST_CASE("TypeCompleteness UnionPilot publishes analyzer rejected runtime observations") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		Vector<FSCompletenessProgram> programs = render_union_pilot_programs(resolution);
		String target_case_id;
		for (FSCompletenessProgram &program : programs) {
			if (is_union_pilot_adapter_diagnostic_target(program)) {
				target_case_id = program.case_id;
				Error source_error = OK;
				program.source = FileAccess::get_file_as_string(
						"modules/foundry_script/tests/scripts/analyzer/errors/number_not_an_expression.fs",
						&source_error);
				REQUIRE_EQ(source_error, OK);
				break;
			}
		}
		REQUIRE_FALSE(target_case_id.is_empty());
		TemporaryProjectTree tree(
				vformat("type_completeness_union_adapter_observation_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const PackedStringArray caller_entries_before = union_pilot_directory_entries(tree.root);

		FSCompletenessRuntimeBatch batch = stale_union_pilot_runtime_batch();
		CHECK_EQ(FSUnionCompletenessAdapter::shared().execute(tree.root, programs, batch), OK);
		CHECK_EQ(batch.text.size(), 20);
		CHECK_EQ(batch.bytecode.size(), 20);
		const FSCompletenessRuntimeResult *target = batch.text.getptr(target_case_id);
		REQUIRE(target != nullptr);
		if (target == nullptr) {
			return;
		}
		CHECK_FALSE(target->passed);
		CHECK_EQ(target->status, "analyzer_error");
		CHECK_EQ(String(target->dimensions.get("analysis", String())), "reject");
		CHECK_FALSE(target->diagnostics.is_empty());
		CHECK(target->produced_output.begins_with("FS_TEST_ANALYZER_ERROR\n"));
		CHECK_EQ(union_pilot_directory_entries(tree.root), caller_entries_before);
	}

	TEST_CASE("TypeCompleteness UnionPilot publishes dependency compiler failures") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		Vector<FSCompletenessProgram> programs = render_union_pilot_programs(resolution);
		FSCompletenessProgram *target_program = find_union_pilot_contract_program(programs, "text");
		REQUIRE(target_program != nullptr);
		if (target_program == nullptr) {
			return;
		}
		const String dependency_path = union_pilot_absolute_fixture_path(
				"modules/foundry_script/tests/scripts/analyzer/errors/"
				"enum_name_bare_payload_case_in_own_body.notest.fs");
		REQUIRE(FileAccess::exists(dependency_path));
		target_program->source = union_pilot_preload_source(
				target_program->source, dependency_path, "CompilerFailureDependency");
		const FSCompletenessObservation analysis =
				FSUnionCompletenessAdapter::shared().analyze(*target_program, target_program->surface);
		CHECK_EQ(String(analysis.dimensions.get("analysis", String())), "accept");
		CHECK(analysis.diagnostics.is_empty());

		TemporaryProjectTree tree(
				vformat("type_completeness_union_compiler_observation_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const PackedStringArray caller_entries_before = union_pilot_directory_entries(tree.root);
		FSCompletenessRuntimeBatch batch = stale_union_pilot_runtime_batch();
		const Error execution_error = FSUnionCompletenessAdapter::shared().execute(tree.root, programs, batch);
		REQUIRE_EQ(execution_error, OK);
		if (execution_error != OK) {
			return;
		}
		CHECK_EQ(batch.text.size(), 20);
		CHECK_EQ(batch.bytecode.size(), 20);
		const FSCompletenessRuntimeResult *target = batch.text.getptr(target_program->case_id);
		REQUIRE(target != nullptr);
		if (target == nullptr) {
			return;
		}
		CHECK_FALSE(target->passed);
		CHECK_EQ(target->status, "compiler_error");
		CHECK_EQ(String(target->dimensions.get("analysis", String())), "accept");
		CHECK_FALSE(target->diagnostics.is_empty());
		CHECK(target->produced_output.begins_with("FS_TEST_COMPILER_ERROR\n"));
		CHECK_EQ(union_pilot_directory_entries(tree.root), caller_entries_before);
	}

	TEST_CASE("TypeCompleteness UnionPilot publishes bytecode dependency reload failures") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		Vector<FSCompletenessProgram> programs = render_union_pilot_programs(resolution);
		FSCompletenessProgram *target_program = find_union_pilot_contract_program(programs, "bytecode");
		REQUIRE(target_program != nullptr);
		if (target_program == nullptr) {
			return;
		}
		const String dependency_path = union_pilot_absolute_fixture_path(
				"modules/foundry_script/tests/scripts/runtime/features/metatypes.notest.fs");
		REQUIRE(FileAccess::exists(dependency_path));
		target_program->source = union_pilot_preload_source(
				target_program->source, dependency_path, "BytecodeReloadDependency");
		const FSCompletenessObservation analysis =
				FSUnionCompletenessAdapter::shared().analyze(*target_program, target_program->surface);
		CHECK_EQ(String(analysis.dimensions.get("analysis", String())), "accept");
		CHECK(analysis.diagnostics.is_empty());

		TemporaryProjectTree tree(
				vformat("type_completeness_union_bytecode_reload_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const PackedStringArray caller_entries_before = union_pilot_directory_entries(tree.root);
		FSCompletenessRuntimeBatch batch = stale_union_pilot_runtime_batch();
		ErrorDetector reload_error_detector;
		ERR_PRINT_OFF;
		const Error execution_error = FSUnionCompletenessAdapter::shared().execute(tree.root, programs, batch);
		ERR_PRINT_ON;
		REQUIRE_EQ(execution_error, OK);
		if (execution_error != OK) {
			return;
		}
		CHECK_EQ(batch.text.size(), 20);
		CHECK_EQ(batch.bytecode.size(), 20);
		const FSCompletenessRuntimeResult *target = batch.bytecode.getptr(target_program->case_id);
		REQUIRE(target != nullptr);
		if (target == nullptr) {
			return;
		}
		CHECK_FALSE(target->passed);
		CHECK_EQ(target->status, "ok");
		CHECK_EQ(String(target->dimensions.get("analysis", String())), "accept");
		CHECK(union_pilot_diagnostics_contain(target->diagnostics, "Runtime contract bytecode reload failed"));
		CHECK_EQ(target->produced_output, "uint 5\n");
		CHECK_NE(int64_t(target->dimensions.get("original_instance_id", int64_t(0))), 0);
		CHECK_FALSE(target->dimensions.has("inspected_instance_id"));
		CHECK(reload_error_detector.has_error);
		CHECK_EQ(union_pilot_directory_entries(tree.root), caller_entries_before);
	}

	TEST_CASE("TypeCompleteness UnionPilot publishes missing destination descriptors") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		Vector<FSCompletenessProgram> programs = render_union_pilot_programs(resolution);
		FSCompletenessProgram *target_program = find_union_pilot_contract_program(programs, "text");
		REQUIRE(target_program != nullptr);
		if (target_program == nullptr) {
			return;
		}
		target_program->source = target_program->source.replace("accept(", "accept_probe(");
		const FSCompletenessObservation analysis =
				FSUnionCompletenessAdapter::shared().analyze(*target_program, target_program->surface);
		CHECK_EQ(String(analysis.dimensions.get("analysis", String())), "accept");
		CHECK(analysis.diagnostics.is_empty());

		TemporaryProjectTree tree(
				vformat("type_completeness_union_descriptor_observation_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const PackedStringArray caller_entries_before = union_pilot_directory_entries(tree.root);
		FSCompletenessRuntimeBatch batch = stale_union_pilot_runtime_batch();
		const Error execution_error = FSUnionCompletenessAdapter::shared().execute(tree.root, programs, batch);
		REQUIRE_EQ(execution_error, OK);
		if (execution_error != OK) {
			return;
		}
		CHECK_EQ(batch.text.size(), 20);
		CHECK_EQ(batch.bytecode.size(), 20);
		const FSCompletenessRuntimeResult *target = batch.text.getptr(target_program->case_id);
		REQUIRE(target != nullptr);
		if (target == nullptr) {
			return;
		}
		CHECK_FALSE(target->passed);
		CHECK_EQ(target->status, "ok");
		CHECK_EQ(String(target->dimensions.get("analysis", String())), "accept");
		CHECK(target->diagnostics.has("Compiled accept function does not expose one destination argument."));
		CHECK_EQ(target->produced_output, "uint 5\n");
		const int64_t original_id = target->dimensions.get("original_instance_id", int64_t(0));
		const int64_t inspected_id = target->dimensions.get("inspected_instance_id", int64_t(0));
		CHECK_NE(original_id, 0);
		CHECK_EQ(inspected_id, original_id);
		CHECK_EQ(bool(target->dimensions.get("inspected_compiled_binary", true)), false);
		CHECK_EQ(union_pilot_directory_entries(tree.root), caller_entries_before);
	}

	// How many surfaces a semantic pair carries depends on the surfaces the run selected, so the
	// adapter no longer decides it; two programs for the same pair on the same surface stay a defect
	// no cardinality could tell apart from a correct matrix.
	TEST_CASE("TypeCompleteness UnionPilot rejects duplicate surface pairs atomically") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		const Vector<FSCompletenessProgram> valid = render_union_pilot_programs(resolution);
		TemporaryProjectTree tree(vformat("type_completeness_union_pair_errors_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());

		Vector<FSCompletenessProgram> duplicate = valid;
		duplicate.push_back(valid[0]);
		FSCompletenessRuntimeBatch batch = stale_union_pilot_runtime_batch();
		CHECK_EQ(FSUnionCompletenessAdapter::shared().execute(tree.root, duplicate, batch), ERR_ALREADY_IN_USE);
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
		CHECK_NE(FSUnionCompletenessAdapter::shared().execute(tree.root, unknown, batch), OK);
		check_union_pilot_runtime_batch_cleared(batch);

		Vector<FSCompletenessProgram> duplicate = valid;
		duplicate.write[1].case_id = duplicate[0].case_id;
		batch = stale_union_pilot_runtime_batch();
		CHECK_EQ(FSUnionCompletenessAdapter::shared().execute(tree.root, duplicate, batch), ERR_ALREADY_EXISTS);
		check_union_pilot_runtime_batch_cleared(batch);
	}

	TEST_CASE("TypeCompleteness UnionPilot publishes output mismatches and invalid carriers") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		const Vector<FSCompletenessProgram> valid = render_union_pilot_programs(resolution);
		TemporaryProjectTree mismatch_tree(vformat("type_completeness_union_output_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(mismatch_tree.is_valid());

		Vector<FSCompletenessProgram> mismatch = valid;
		mismatch.write[0].expected_output = "wrong output\n";
		FSCompletenessRuntimeBatch batch;
		const Error mismatch_error = FSUnionCompletenessAdapter::shared().execute(mismatch_tree.root, mismatch, batch);
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
		REQUIRE_EQ(FSUnionCompletenessAdapter::shared().execute(carrier_tree.root, bad_carrier, batch), OK);
		CHECK_EQ(batch.text.size(), 20);
		CHECK_EQ(batch.bytecode.size(), 20);
		const HashMap<String, FSCompletenessRuntimeResult> &carrier_results =
				bad_carrier[0].surface == "text" ? batch.text : batch.bytecode;
		const FSCompletenessRuntimeResult *carrier_result = carrier_results.getptr(bad_carrier[0].case_id);
		REQUIRE(carrier_result != nullptr);
		if (carrier_result == nullptr) {
			return;
		}
		CHECK_FALSE(carrier_result->passed);
		CHECK_EQ(carrier_result->status, "ok");
		CHECK_EQ(carrier_result->produced_output, "bad 5\n");
		CHECK_FALSE(carrier_result->diagnostics.is_empty());
	}

	TEST_CASE("TypeCompleteness UnionPilot rejects unsafe scratch roots and duplicate global setup") {
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
			CHECK_NE(FSUnionCompletenessAdapter::shared().execute(unsafe_root, programs, batch), OK);
			check_union_pilot_runtime_batch_cleared(batch);
		}

		TemporaryProjectTree invalid_tree(vformat("type_completeness_union_invalid_root_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(invalid_tree.is_valid());
		invalid_tree.write_file("occupied", "not a directory");
		FSCompletenessRuntimeBatch batch = stale_union_pilot_runtime_batch();
		CHECK_NE(FSUnionCompletenessAdapter::shared().execute(
						 invalid_tree.root.path_join("occupied"), programs, batch),
				OK);
		check_union_pilot_runtime_batch_cleared(batch);

		TemporaryProjectTree tree(vformat("type_completeness_union_setup_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String duplicate_class_fixture = TestUtils::get_tests_dir().get_base_dir().path_join(
				"modules/foundry_script/tests/scripts/utils.notest.fs");
		const String duplicate_class_source = FileAccess::get_file_as_string(duplicate_class_fixture);
		REQUIRE_FALSE(duplicate_class_source.is_empty());
		Vector<FSCompletenessProgram> duplicate_class_programs = programs;
		int duplicate_count = 0;
		for (FSCompletenessProgram &program : duplicate_class_programs) {
			if (program.surface == "text" && duplicate_count < 2) {
				program.source = duplicate_class_source;
				duplicate_count++;
			}
		}
		REQUIRE_EQ(duplicate_count, 2);
		const PackedStringArray caller_entries_before = union_pilot_directory_entries(tree.root);
		batch = stale_union_pilot_runtime_batch();
		ErrorDetector setup_error_detector;
		ERR_PRINT_OFF;
		const Error setup_error =
				FSUnionCompletenessAdapter::shared().execute(tree.root, duplicate_class_programs, batch);
		ERR_PRINT_ON;
		CHECK_EQ(setup_error, ERR_CANT_OPEN);
		CHECK(setup_error_detector.has_error);
		check_union_pilot_runtime_batch_cleared(batch);
		CHECK_EQ(union_pilot_directory_entries(tree.root), caller_entries_before);
	}

	TEST_CASE("TypeCompleteness UnionPilot rejects persisted runtime fixture truncation before execution") {
		UnionPilotProgramMutationScope mutation_scope;
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		const Vector<FSCompletenessProgram> programs = render_union_pilot_programs(resolution);
		TemporaryProjectTree tree(
				vformat("type_completeness_union_truncated_fixture_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const PackedStringArray entries_before = union_pilot_directory_entries(tree.root);

		UnionCompletenessInternal::set_persisted_write_test_hook(
				truncate_first_union_pilot_persisted_source);
		FSCompletenessRuntimeBatch batch = stale_union_pilot_runtime_batch();
		CHECK_EQ(FSUnionCompletenessAdapter::shared().execute(tree.root, programs, batch), ERR_FILE_CORRUPT);
		CHECK_EQ(union_pilot_persisted_write_mutation_count, 1);
		check_union_pilot_runtime_batch_cleared(batch);
		CHECK_EQ(union_pilot_directory_entries(tree.root), entries_before);
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
		const String outside_root_link = tree.root.path_join("outside_root");
		REQUIRE_EQ(filesystem->create_link(neighbor.root, outside_root_link), OK);

		FSCompletenessRuntimeBatch batch = stale_union_pilot_runtime_batch();
		CHECK_EQ(FSUnionCompletenessAdapter::shared().execute(outside_root_link, programs, batch), ERR_UNAUTHORIZED);
		check_union_pilot_runtime_batch_cleared(batch);
		CHECK_FALSE(FileAccess::exists(neighbor.root.path_join("text/project.foundry")));

		REQUIRE_EQ(filesystem->create_link(neighbor.root, tree.root.path_join("ancestor_link")), OK);
		REQUIRE_EQ(filesystem->make_dir_recursive(neighbor.root.path_join("existing_root")), OK);
		batch = stale_union_pilot_runtime_batch();
		CHECK_EQ(FSUnionCompletenessAdapter::shared().execute(
						 tree.root.path_join("ancestor_link/existing_root"), programs, batch),
				ERR_UNAUTHORIZED);
		check_union_pilot_runtime_batch_cleared(batch);
		CHECK_FALSE(FileAccess::exists(neighbor.root.path_join("existing_root/text/project.foundry")));
	}

	TEST_CASE("TypeCompleteness UnionPilot leaves caller surface aliases untouched") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		const Vector<FSCompletenessProgram> programs = render_union_pilot_programs(resolution);
		TemporaryProjectTree tree(vformat("type_completeness_union_surface_plan_%d", OS::get_singleton()->get_process_id()));
		TemporaryProjectTree neighbor(vformat("type_completeness_union_surface_neighbor_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		REQUIRE(neighbor.is_valid());
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(filesystem.is_valid());
		neighbor.write_file("sentinel.txt", "neighbor sentinel\n");
		REQUIRE_EQ(filesystem->create_link(neighbor.root, tree.root.path_join("text")), OK);
		const PackedStringArray caller_entries_before = union_pilot_directory_entries(tree.root);

		FSCompletenessRuntimeBatch batch;
		CHECK_EQ(FSUnionCompletenessAdapter::shared().execute(tree.root, programs, batch), OK);
		CHECK_EQ(batch.text.size(), 20);
		CHECK_EQ(batch.bytecode.size(), 20);
		CHECK_EQ(union_pilot_directory_entries(tree.root), caller_entries_before);
		CHECK_EQ(FileAccess::get_file_as_string(neighbor.root.path_join("sentinel.txt")),
				"neighbor sentinel\n");
		CHECK_FALSE(FileAccess::exists(neighbor.root.path_join("project.foundry")));
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
		REQUIRE_EQ(FSUnionCompletenessAdapter::shared().render(*union_gradual, program), OK);
		program.source = program.source.replace("uint | String", "uint | String | RefCounted");
		Dictionary runtime_context = union_gradual->coordinates.duplicate();
		runtime_context["produced_output"] = "uint 5\n";

		const FSCompletenessObservation observation =
				FSUnionCompletenessAdapter::shared().inspect_runtime_contract(program, runtime_context);
		CHECK(observation.diagnostics.has("Runtime destination descriptor admits RefCounted.new()."));
		CHECK_FALSE(observation.dimensions.has("runtime_obligation"));
	}

	TEST_CASE("TypeCompleteness UnionPilot runtime contract rejects coordinate context disagreement") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		REQUIRE_FALSE(resolution.cells.is_empty());
		FSCompletenessProgram program;
		REQUIRE_EQ(FSUnionCompletenessAdapter::shared().render(resolution.cells[0], program), OK);
		Dictionary runtime_context;
		runtime_context["produced_output"] = "uint 5\n";
		runtime_context["destination"] =
				program.coordinates.get("destination", String()) == "plain" ? "union" : "plain";

		const FSCompletenessObservation observation =
				FSUnionCompletenessAdapter::shared().inspect_runtime_contract(program, runtime_context);
		CHECK(observation.diagnostics.has(
				"Runtime context coordinate 'destination' disagrees with the program."));
		CHECK_FALSE(observation.dimensions.has("runtime_obligation"));
	}

	TEST_CASE("TypeCompleteness UnionPilot scratch source identity resolves Holder and cleans its tree") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		REQUIRE_FALSE(resolution.cells.is_empty());
		FSCompletenessProgram program;
		REQUIRE_EQ(FSUnionCompletenessAdapter::shared().render(resolution.cells[0], program), OK);
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
			REQUIRE_EQ(FSUnionCompletenessAdapter::shared().render(cell, program), OK);
			const String surface = cell.coordinates.get("surface", String());
			CHECK_EQ(program.case_id, cell.case_id);
			CHECK_EQ(program.surface, surface);
			CHECK_EQ(program.expected_output, "uint 5\n");

			const FSCompletenessObservation observation = FSUnionCompletenessAdapter::shared().analyze(program, surface);
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
			CHECK_EQ(FSUnionCompletenessAdapter::shared().render(missing, program), ERR_INVALID_DATA);
			check_union_pilot_program_cleared(program);

			FSCompletenessResolvedCell unknown = valid;
			unknown.coordinates = valid.coordinates.duplicate();
			unknown.coordinates[axis] = "unknown";
			program = stale_union_pilot_program();
			CHECK_EQ(FSUnionCompletenessAdapter::shared().render(unknown, program), ERR_INVALID_DATA);
			check_union_pilot_program_cleared(program);

			FSCompletenessResolvedCell wrong_type = valid;
			wrong_type.coordinates = valid.coordinates.duplicate();
			wrong_type.coordinates[axis] = 7;
			program = stale_union_pilot_program();
			CHECK_EQ(FSUnionCompletenessAdapter::shared().render(wrong_type, program), ERR_INVALID_DATA);
			check_union_pilot_program_cleared(program);
		}

		FSCompletenessResolvedCell empty_case = valid;
		empty_case.case_id.clear();
		FSCompletenessProgram program = stale_union_pilot_program();
		CHECK_EQ(FSUnionCompletenessAdapter::shared().render(empty_case, program), ERR_INVALID_DATA);
		check_union_pilot_program_cleared(program);
	}

	TEST_CASE("TypeCompleteness UnionPilot executes one surface when the run selects it") {
		TemporaryProjectTree tree(
				vformat("type_completeness_union_text_only_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.surfaces.insert("text");

		FSCompletenessRunResult result;
		REQUIRE_EQ(FSCompletenessRunner::run(options, result), OK);
		CHECK(result.success);
		CHECK_EQ(result.outcome, "passed");
		CHECK_EQ(result.executed_cells, 20);
		CHECK(result.structural_failures.is_empty());
		CHECK_EQ(int(double(result.report["cell_count"])), 20);
		const Dictionary executed_by_surface = result.report["executed_by_surface"];
		CHECK_EQ(executed_by_surface.size(), 1);
		CHECK_EQ(int(double(executed_by_surface["text"])), 20);
		// Parity needs two observations of one semantic case; this run made one of each.
		CHECK_EQ(int(double(result.report["text_bytecode_parity_failures"])), 0);
		const Array cases = result.report["cases"];
		REQUIRE_EQ(cases.size(), 20);
		for (int index = 0; index < cases.size(); index++) {
			const Dictionary case_report = cases[index];
			const Dictionary coordinates = case_report["coordinates"];
			CHECK_EQ(String(coordinates["surface"]), "text");
			CHECK_EQ(String(case_report["status"]), "passed");
		}

		// The exception declares a witness on the surface this run did not execute, so it is not
		// witnessed by this run whatever the observed witnesses did.
		const Array exceptions = result.report["exceptions"];
		REQUIRE_EQ(exceptions.size(), 1);
		const Dictionary exception_report = exceptions[0];
		CHECK_EQ(bool(exception_report["witnessed"]), false);
		CHECK_EQ(Array(exception_report["positive_witnesses"]).size(), 1);
		CHECK_EQ(Array(exception_report["boundary_witnesses"]).size(), 1);

		FSCompletenessRunOptions refused = options;
		refused.surfaces.insert("assembly");
		refused.scratch_root = tree.root.path_join("refused");
		refused.report_path = refused.scratch_root.path_join("report.json");
		FSCompletenessRunResult refused_result;
		CHECK_EQ(FSCompletenessRunner::run(refused, refused_result), ERR_INVALID_PARAMETER);
		CHECK_FALSE(refused_result.success);
		CHECK_EQ(refused_result.outcome, "structural_failure");
		CHECK_FALSE(FileAccess::exists(refused.report_path));
	}

	TEST_CASE("TypeCompleteness UnionPilot report matches the captured evidence document") {
		const FSCompletenessRunResult *baseline =
				FSCompletenessBaseline::shared_or_skip("union_destination_membership");
		if (baseline == nullptr) {
			return;
		}
		Error read_error = OK;
		const String source = FileAccess::get_file_as_string(
				type_completeness_union_pilot_root.path_join(
						"expected_reports/union_destination_membership.json"),
				&read_error);
		REQUIRE_EQ(read_error, OK);
		Variant expected;
		Vector<String> errors;
		REQUIRE_MESSAGE(parse_type_completeness_json(source, String(), expected, errors) == OK,
				String(" | ").join(errors));

		const String produced_document =
				JSON::stringify(union_pilot_scratch_independent_evidence(baseline->report), "  ");
		const String expected_document =
				JSON::stringify(union_pilot_scratch_independent_evidence(expected), "  ");
		CHECK_EQ(produced_document, expected_document);
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
			const Dictionary coordinates = union_pilot_witness_coordinates(expectation.id);
			CHECK_EQ(coordinates.size(), 4);
			CHECK_EQ(String(coordinates.get("destination", String())), expectation.destination);
			CHECK_EQ(String(coordinates.get("source_proof", String())), expectation.source_proof);
			CHECK_EQ(String(coordinates.get("boundary", String())), expectation.boundary);
			CHECK_EQ(String(coordinates.get("surface", String())), expectation.surface);
		}

		CHECK(union_pilot_witness_coordinates("unknown").is_empty());
	}

	TEST_CASE("TypeCompleteness UnionPilot analysis rejects malformed parser input with diagnostics") {
		FSCompletenessProgram malformed;
		malformed.case_id = "malformed_parser_input";
		malformed.surface = "text";
		malformed.source = "func test(:\n";

		const FSCompletenessObservation observation = FSUnionCompletenessAdapter::shared().analyze(malformed, "text");
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

		const FSCompletenessObservation observation = FSUnionCompletenessAdapter::shared().analyze(invalid, "bytecode");
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
		REQUIRE_EQ(FSUnionCompletenessAdapter::shared().render(*text_cell, program), OK);

		const FSCompletenessObservation mismatch = FSUnionCompletenessAdapter::shared().analyze(program, "bytecode");
		CHECK_EQ(mismatch.case_id, program.case_id);
		CHECK_EQ(mismatch.surface, "bytecode");
		CHECK_EQ(String(mismatch.dimensions.get("analysis", String())), "reject");
		CHECK_FALSE(mismatch.diagnostics.is_empty());

		const FSCompletenessObservation unknown = FSUnionCompletenessAdapter::shared().analyze(program, "unknown");
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
		REQUIRE_EQ(FSUnionCompletenessAdapter::shared().render(resolution.cells[0], program), OK);
		program.case_id = "../../escape|x";

		const FSCompletenessObservation observation = FSUnionCompletenessAdapter::shared().analyze(program, program.surface);
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
		REQUIRE_EQ(FSUnionCompletenessAdapter::shared().render(resolution.cells[0], program), OK);
		program.case_id = "repeated_identity";

		const FSCompletenessObservation first = FSUnionCompletenessAdapter::shared().analyze(program, program.surface);
		const FSCompletenessObservation second = FSUnionCompletenessAdapter::shared().analyze(program, program.surface);
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
				FSUnionCompletenessAdapter::shared().analyze(malformed, malformed.surface);
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

		const FSCompletenessObservation observation = FSUnionCompletenessAdapter::shared().analyze(invalid, invalid.surface);
		CHECK_EQ(String(observation.dimensions.get("analysis", String())), "reject");
		REQUIRE_EQ(observation.diagnostics.size(), 2);
		CHECK_EQ(observation.diagnostics[0],
				"2:22: Cannot assign a value of type String to variable \"first\" with specified type int.");
		CHECK_EQ(observation.diagnostics[1],
				"3:26: Cannot assign a value of type int to variable \"second\" with specified type String.");
	}
}

} // namespace FSTests
