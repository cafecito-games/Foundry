/**************************************************************************/
/*  test_generic_tagged_union.h                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             FOUNDRY ENGINE                             */
/*          A fork of the Godot Engine (https://godotengine.org)          */
/*                       https://www.cafecito.games                       */
/**************************************************************************/
/* Copyright (c) 2026-present Cafecito Games LLC.                         */
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

#include "../fs_analyzer.h"
#include "../fs_parser.h"

#ifdef TOOLS_ENABLED
#include "../fs_format.h"
#endif // TOOLS_ENABLED

#include "tests/test_macros.h"

// Declaration-shape coverage for generic tagged unions, plus analyzer coverage for parameter scope,
// bounds, and the open self identity used by recursive declarations. The analyzer tests inspect
// resolved `DataType` structures so scope selection is proven, not merely implied by compilation.

namespace FSTests {
namespace GenericTaggedUnion {

static const FSParser::EnumNode *find_enum(const FSParser &p_parser, const StringName &p_name) {
	const FSParser::ClassNode *root_class = p_parser.get_tree();
	if (root_class == nullptr || !root_class->has_member(p_name)) {
		return nullptr;
	}
	const FSParser::ClassNode::Member &member = root_class->get_member(p_name);
	if (member.type != FSParser::ClassNode::Member::ENUM) {
		return nullptr;
	}
	return member.m_enum;
}

static bool has_error_containing(const FSParser &p_parser, const String &p_fragment) {
	for (const FSParser::ParserError &error : p_parser.get_errors()) {
		if (error.message.contains(p_fragment)) {
			return true;
		}
	}
	return false;
}

#ifdef TOOLS_ENABLED
// The formatter is an editor-only tool, so its coverage compiles only where it exists.
static String format_source(const String &p_source) {
	FSFormatter formatter;
	FSFormatter::Result result;
	const Error err = formatter.format(p_source, "test.fs", result);
	CHECK_MESSAGE(err == OK, "Source must format without parse errors.");
	return result.formatted;
}
#endif // TOOLS_ENABLED

TEST_CASE("[Modules][FoundryScript][GenericTaggedUnion] Parser stores enum type parameters") {
	FSParser parser;
	const Error err = parser.parse(
			"enum Result[T, E: Resource]:\n"
			"\tOk(value: T)\n"
			"\tErr(error: E)\n",
			"user://generic_tagged_union_parser.fs", false);
	REQUIRE_EQ(err, OK);

	const FSParser::EnumNode *result = find_enum(parser, SNAME("Result"));
	REQUIRE(result != nullptr);
	CHECK(result->is_tagged_union);
	REQUIRE_EQ(result->type_parameters.size(), 2);
	REQUIRE(result->type_parameters[0]->identifier != nullptr);
	CHECK_EQ(result->type_parameters[0]->identifier->name, SNAME("T"));
	CHECK(result->type_parameters[0]->bound == nullptr);
	REQUIRE(result->type_parameters[1]->identifier != nullptr);
	CHECK_EQ(result->type_parameters[1]->identifier->name, SNAME("E"));
	CHECK(result->type_parameters[1]->bound != nullptr);
}

TEST_CASE("[Modules][FoundryScript][GenericTaggedUnion] Parser stores enum_name type parameters") {
	FSParser parser;
	const Error err = parser.parse(
			"enum_name GlobalResult[T, E]:\n"
			"\tOk(value: T)\n"
			"\tErr(error: E)\n",
			"user://generic_tagged_union_enum_name.fs", false);
	REQUIRE_EQ(err, OK);

	const FSParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);
	REQUIRE(root_class->enum_file_decl != nullptr);
	REQUIRE_EQ(root_class->enum_file_decl->type_parameters.size(), 2);
	CHECK_EQ(root_class->enum_file_decl->type_parameters[0]->identifier->name, SNAME("T"));
	CHECK_EQ(root_class->enum_file_decl->type_parameters[1]->identifier->name, SNAME("E"));
}

TEST_CASE("[Modules][FoundryScript][GenericTaggedUnion] Non-generic enums keep an empty parameter list") {
	FSParser parser;
	const Error err = parser.parse(
			"enum Direction:\n"
			"\tNorth = 0\n"
			"\tSouth = 1\n",
			"user://generic_tagged_union_plain.fs", false);
	REQUIRE_EQ(err, OK);

	const FSParser::EnumNode *direction = find_enum(parser, SNAME("Direction"));
	REQUIRE(direction != nullptr);
	CHECK(direction->type_parameters.is_empty());
}

TEST_CASE("[Modules][FoundryScript][GenericTaggedUnion] Unnamed generic enums are rejected") {
	FSParser parser;
	const Error err = parser.parse(
			"enum[T]:\n"
			"\tValue(payload: T)\n",
			"user://generic_tagged_union_unnamed.fs", false);
	CHECK_NE(err, OK);
	CHECK(has_error_containing(parser, "Type parameters require a named tagged union."));
}

TEST_CASE("[Modules][FoundryScript][GenericTaggedUnion] Parameterized integer-backed enums are rejected") {
	FSParser parser;
	const Error err = parser.parse(
			"enum Direction[T]:\n"
			"\tNorth = 0\n"
			"\tSouth = 1\n",
			"user://generic_tagged_union_integer_backed.fs", false);
	CHECK_NE(err, OK);
	CHECK(has_error_containing(parser, R"(Generic enum "Direction" must contain at least one payload-bearing case.)"));
}

TEST_CASE("[Modules][FoundryScript][GenericTaggedUnion] Duplicate enum type parameter names are rejected") {
	FSParser parser;
	const Error err = parser.parse(
			"enum Pair[T, T]:\n"
			"\tBoth(first: T, second: T)\n",
			"user://generic_tagged_union_duplicate.fs", false);
	CHECK_NE(err, OK);
	CHECK(has_error_containing(parser, R"(Type parameter with name "T" was already declared.)"));
}

#ifdef TOOLS_ENABLED
TEST_CASE("[Modules][FoundryScript][GenericTaggedUnion] Formatter canonicalizes generic enum headers") {
	// A trailing comma in the parameter list is accepted and dropped, matching how the same
	// helper canonicalizes class and function parameter lists.
	const String formatted = format_source(
			"enum Result[T,E: Resource,]:\n"
			"\tOk(value:T)\n"
			"\tErr(error:E)\n");
	CHECK(formatted.contains("enum Result[T, E: Resource]:"));

	const String global_formatted = format_source(
			"enum_name GlobalResult[T,E]:\n"
			"\tOk(value:T)\n"
			"\tErr(error:E)\n");
	CHECK(global_formatted.contains("enum_name GlobalResult[T, E]:"));
}
#endif // TOOLS_ENABLED

// Analyzer-driven coverage. Every assertion reads an analyzed `DataType` or an emitted diagnostic.

static const FSParser::ClassNode *find_inner_class(const FSParser::ClassNode *p_class, const StringName &p_name) {
	if (p_class == nullptr || !p_class->has_member(p_name)) {
		return nullptr;
	}
	const FSParser::ClassNode::Member &member = p_class->get_member(p_name);
	return member.type == FSParser::ClassNode::Member::CLASS ? member.m_class : nullptr;
}

static const FSParser::EnumNode *find_enum_in(const FSParser::ClassNode *p_class, const StringName &p_name) {
	if (p_class == nullptr || !p_class->has_member(p_name)) {
		return nullptr;
	}
	const FSParser::ClassNode::Member &member = p_class->get_member(p_name);
	return member.type == FSParser::ClassNode::Member::ENUM ? member.m_enum : nullptr;
}

static const FSParser::FunctionNode *find_enum_function(const FSParser::EnumNode *p_enum, const StringName &p_name) {
	if (p_enum == nullptr) {
		return nullptr;
	}
	for (const FSParser::FunctionNode *function : p_enum->functions) {
		if (function != nullptr && function->identifier != nullptr && function->identifier->name == p_name) {
			return function;
		}
	}
	return nullptr;
}

// The payload field type recorded on the enum's analyzed datatype, or an unset type when the case
// or field does not exist.
static FSParser::DataType payload_field_type(const FSParser::EnumNode *p_enum, const StringName &p_case, int p_field) {
	if (p_enum == nullptr) {
		return FSParser::DataType();
	}
	const FSParser::DataType enum_type = p_enum->get_datatype();
	const FSParser::DataType::EnumCasePayload *payload = enum_type.enum_case_payloads.getptr(p_case);
	if (payload == nullptr || p_field < 0 || p_field >= payload->field_types.size()) {
		return FSParser::DataType();
	}
	return payload->field_types[p_field];
}

// Doctest runs without exceptions here, so a failed REQUIRE does not abort the case; index through
// this accessor rather than crashing a red run on an out-of-range read.
static FSParser::DataType type_at(const Vector<FSParser::DataType> &p_types, int p_index) {
	return p_index >= 0 && p_index < p_types.size() ? p_types[p_index] : FSParser::DataType();
}

static String first_error_message(const FSParser &p_parser) {
	return p_parser.get_errors().is_empty() ? String() : p_parser.get_errors().front()->get().message;
}

static bool is_type_parameter(const FSParser::DataType &p_type, const StringName &p_name,
		FSParser::DataType::TypeParameterScope p_scope, int p_index) {
	return p_type.kind == FSParser::DataType::TYPE_PARAMETER && p_type.type_parameter_name == p_name &&
			p_type.type_parameter_scope == p_scope && p_type.type_parameter_index == p_index;
}

TEST_CASE("[Modules][FoundryScript][GenericTaggedUnionScope] Enum parameters resolve between method and class scopes") {
	FSParser parser;
	const Error parse_error = parser.parse(
			"class Outer[ClassT]:\n"
			"\tenum Choice[EnumT: ClassT]:\n"
			"\t\tValue(value: EnumT)\n"
			"\t\tFallback(outer: ClassT)\n"
			"\n"
			"\t\tstatic func identity[MethodT](value: MethodT) -> MethodT:\n"
			"\t\t\tvar through_lambda := func(inner: MethodT) -> MethodT:\n"
			"\t\t\t\treturn inner\n"
			"\t\t\treturn value\n",
			"user://generic_tagged_union_scope.fs", false);
	REQUIRE_EQ(parse_error, OK);

	FSAnalyzer analyzer(&parser);
	REQUIRE_EQ(analyzer.analyze(), OK);

	const FSParser::ClassNode *outer = find_inner_class(parser.get_tree(), SNAME("Outer"));
	REQUIRE(outer != nullptr);
	const FSParser::EnumNode *choice = find_enum_in(outer, SNAME("Choice"));
	REQUIRE(choice != nullptr);

	// An enum parameter wins over the enclosing class, and an unmatched name still falls back to it.
	CHECK(is_type_parameter(payload_field_type(choice, SNAME("Value"), 0), SNAME("EnumT"),
			FSParser::DataType::TYPE_PARAMETER_ENUM, 0));
	CHECK(is_type_parameter(payload_field_type(choice, SNAME("Fallback"), 0), SNAME("ClassT"),
			FSParser::DataType::TYPE_PARAMETER_CLASS, 0));

	// The enum parameter's bound resolves in the declaration scope, so it sees the class parameter.
	REQUIRE_EQ(choice->type_parameters.size(), 1);
	const FSParser::DataType enum_bound = choice->type_parameters[0]->resolved_bound;
	CHECK(is_type_parameter(enum_bound, SNAME("ClassT"), FSParser::DataType::TYPE_PARAMETER_CLASS, 0));

	// A method parameter still shadows both, inside the signature and inside a nested lambda.
	const FSParser::FunctionNode *identity = find_enum_function(choice, SNAME("identity"));
	REQUIRE(identity != nullptr);
	REQUIRE_EQ(identity->parameters.size(), 1);
	CHECK(is_type_parameter(identity->parameters[0]->get_datatype(), SNAME("MethodT"),
			FSParser::DataType::TYPE_PARAMETER_METHOD, 0));

	REQUIRE(identity->body != nullptr);
	const FSParser::LambdaNode *lambda = nullptr;
	for (FSParser::Node *statement : identity->body->statements) {
		if (statement->type != FSParser::Node::VARIABLE) {
			continue;
		}
		const FSParser::VariableNode *variable = static_cast<const FSParser::VariableNode *>(statement);
		if (variable->initializer != nullptr && variable->initializer->type == FSParser::Node::LAMBDA) {
			lambda = static_cast<const FSParser::LambdaNode *>(variable->initializer);
		}
	}
	REQUIRE(lambda != nullptr);
	REQUIRE(lambda->function != nullptr);
	REQUIRE_EQ(lambda->function->parameters.size(), 1);
	CHECK(is_type_parameter(lambda->function->parameters[0]->get_datatype(), SNAME("MethodT"),
			FSParser::DataType::TYPE_PARAMETER_METHOD, 0));
}

TEST_CASE("[Modules][FoundryScript][GenericTaggedUnionScope] Same-spelled parameters keep distinct scopes") {
	FSParser parser;
	const Error parse_error = parser.parse(
			"class Shadow[T]:\n"
			"\tvar held: T\n"
			"\n"
			"\tenum Choice[T]:\n"
			"\t\tValue(value: T)\n"
			"\n"
			"\t\tstatic func identity[T](value: T) -> T:\n"
			"\t\t\treturn value\n",
			"user://generic_tagged_union_shadow.fs", false);
	REQUIRE_EQ(parse_error, OK);

	FSAnalyzer analyzer(&parser);
	REQUIRE_EQ(analyzer.analyze(), OK);

	const FSParser::ClassNode *shadow = find_inner_class(parser.get_tree(), SNAME("Shadow"));
	REQUIRE(shadow != nullptr);
	const FSParser::EnumNode *choice = find_enum_in(shadow, SNAME("Choice"));
	REQUIRE(choice != nullptr);

	const FSParser::DataType class_member_type = shadow->get_member(SNAME("held")).variable->get_datatype();
	const FSParser::DataType payload_type = payload_field_type(choice, SNAME("Value"), 0);
	const FSParser::FunctionNode *identity = find_enum_function(choice, SNAME("identity"));
	REQUIRE(identity != nullptr);
	REQUIRE_EQ(identity->parameters.size(), 1);
	const FSParser::DataType method_type = identity->parameters[0]->get_datatype();

	CHECK(is_type_parameter(class_member_type, SNAME("T"), FSParser::DataType::TYPE_PARAMETER_CLASS, 0));
	CHECK(is_type_parameter(payload_type, SNAME("T"), FSParser::DataType::TYPE_PARAMETER_ENUM, 0));
	CHECK(is_type_parameter(method_type, SNAME("T"), FSParser::DataType::TYPE_PARAMETER_METHOD, 0));

	// Identical spelling and ordinal, so only the scope separates the three handles.
	CHECK(class_member_type != payload_type);
	CHECK(payload_type != method_type);
	CHECK(class_member_type != method_type);
}

TEST_CASE("[Modules][FoundryScript][GenericTaggedUnionScope] Enum parameter bounds resolve eagerly") {
	FSParser parser;
	const Error parse_error = parser.parse(
			"enum Bounded[T: Resource, U: T]:\n"
			"\tPair(first: T, second: U)\n",
			"user://generic_tagged_union_bounds.fs", false);
	REQUIRE_EQ(parse_error, OK);

	FSAnalyzer analyzer(&parser);
	REQUIRE_EQ(analyzer.analyze(), OK);

	const FSParser::EnumNode *bounded = find_enum(parser, SNAME("Bounded"));
	REQUIRE(bounded != nullptr);
	REQUIRE_EQ(bounded->type_parameters.size(), 2);

	const FSParser::DataType first_bound = bounded->type_parameters[0]->resolved_bound;
	CHECK(first_bound.kind == FSParser::DataType::NATIVE);
	CHECK(first_bound.native_type == SNAME("Resource"));

	const FSParser::DataType second_bound = bounded->type_parameters[1]->resolved_bound;
	CHECK(is_type_parameter(second_bound, SNAME("T"), FSParser::DataType::TYPE_PARAMETER_ENUM, 0));

	// The bound travels with the handle used in payload positions.
	const FSParser::DataType second_field = payload_field_type(bounded, SNAME("Pair"), 1);
	REQUIRE(is_type_parameter(second_field, SNAME("U"), FSParser::DataType::TYPE_PARAMETER_ENUM, 1));
	REQUIRE_EQ(second_field.type_parameter_bound.size(), 1);
	CHECK(is_type_parameter(type_at(second_field.type_parameter_bound, 0), SNAME("T"),
			FSParser::DataType::TYPE_PARAMETER_ENUM, 0));
}

TEST_CASE("[Modules][FoundryScript][GenericTaggedUnionScope] Bare and explicit open self canonicalize alike") {
	FSParser parser;
	const Error parse_error = parser.parse(
			"enum TokenTree[T]:\n"
			"\tLeaf(value: T)\n"
			"\tLink(next: TokenTree)\n"
			"\tBranch(children: Array[TokenTree])\n"
			"\tExplicit(children: Array[TokenTree[T]])\n"
			"\n"
			"\tstatic func singleton(value: T) -> TokenTree:\n"
			"\t\treturn TokenTree.Leaf(value)\n",
			"user://generic_tagged_union_recursive.fs", false);
	REQUIRE_EQ(parse_error, OK);

	FSAnalyzer analyzer(&parser);
	REQUIRE_EQ(analyzer.analyze(), OK);
	CHECK(parser.get_errors().is_empty());

	const FSParser::EnumNode *tree = find_enum(parser, SNAME("TokenTree"));
	REQUIRE(tree != nullptr);

	// The declaration publishes its own parameter vector as the open identity.
	const FSParser::DataType tree_type = tree->get_datatype();
	REQUIRE_EQ(tree_type.type_arguments.size(), 1);
	CHECK(is_type_parameter(type_at(tree_type.type_arguments, 0), SNAME("T"),
			FSParser::DataType::TYPE_PARAMETER_ENUM, 0));

	const FSParser::DataType direct = payload_field_type(tree, SNAME("Link"), 0);
	REQUIRE(direct.kind == FSParser::DataType::ENUM);
	CHECK(direct.is_tagged_union);
	REQUIRE_EQ(direct.type_arguments.size(), 1);
	CHECK(is_type_parameter(type_at(direct.type_arguments, 0), SNAME("T"), FSParser::DataType::TYPE_PARAMETER_ENUM, 0));

	const FSParser::DataType bare_container = payload_field_type(tree, SNAME("Branch"), 0);
	REQUIRE_EQ(bare_container.container_element_types.size(), 1);
	const FSParser::DataType explicit_container = payload_field_type(tree, SNAME("Explicit"), 0);
	REQUIRE_EQ(explicit_container.container_element_types.size(), 1);

	// Bare `TokenTree` and the exact open spelling `TokenTree[T]` are the same open self type.
	CHECK(type_at(bare_container.container_element_types, 0) == direct);
	CHECK(type_at(explicit_container.container_element_types, 0) == direct);
}

TEST_CASE("[Modules][FoundryScript][GenericTaggedUnionScope] Recursive completion is finite and stable") {
	FSParser parser;
	const Error parse_error = parser.parse(
			"enum TokenTree[T]:\n"
			"\tLeaf(value: T)\n"
			"\tLink(next: TokenTree)\n"
			"\tBranch(children: Array[TokenTree])\n",
			"user://generic_tagged_union_completion.fs", false);
	REQUIRE_EQ(parse_error, OK);

	FSAnalyzer analyzer(&parser);
	REQUIRE_EQ(analyzer.analyze(), OK);

	const FSParser::EnumNode *tree = find_enum(parser, SNAME("TokenTree"));
	REQUIRE(tree != nullptr);

	const FSParser::DataType shell = payload_field_type(tree, SNAME("Link"), 0);
	REQUIRE(shell.kind == FSParser::DataType::ENUM);
	// The captured recursive edge starts as an identity shell carrying the enum-scoped parameter.
	CHECK(shell.enum_values.is_empty());
	REQUIRE_EQ(shell.type_arguments.size(), 1);
	CHECK(is_type_parameter(type_at(shell.type_arguments, 0), SNAME("T"), FSParser::DataType::TYPE_PARAMETER_ENUM, 0));

	const FSParser::DataType completed = FSAnalyzer::test_complete_self_referential_enum_type(shell);
	CHECK_EQ(completed.enum_values.size(), 3);
	CHECK(completed.type_arguments == shell.type_arguments);
	// Recursive edges inside the copied payload map stay shells, which is what keeps this finite.
	const FSParser::DataType::EnumCasePayload *link_payload = completed.enum_case_payloads.getptr(SNAME("Link"));
	REQUIRE(link_payload != nullptr);
	REQUIRE_EQ(link_payload->field_types.size(), 1);
	CHECK(type_at(link_payload->field_types, 0).enum_values.is_empty());

	const FSParser::DataType completed_twice = FSAnalyzer::test_complete_self_referential_enum_type(completed);
	CHECK_EQ(completed_twice.enum_values.size(), completed.enum_values.size());
	CHECK(completed_twice.type_arguments == completed.type_arguments);
	const FSParser::DataType::EnumCasePayload *twice_payload = completed_twice.enum_case_payloads.getptr(SNAME("Link"));
	REQUIRE(twice_payload != nullptr);
	REQUIRE_EQ(twice_payload->field_types.size(), 1);
	CHECK(type_at(twice_payload->field_types, 0).enum_values.is_empty());

	// A container-mediated edge completes through its element without growing either.
	const FSParser::DataType array_shell = payload_field_type(tree, SNAME("Branch"), 0);
	REQUIRE_EQ(array_shell.container_element_types.size(), 1);
	const FSParser::DataType completed_array = FSAnalyzer::test_complete_self_referential_enum_type(array_shell);
	REQUIRE_EQ(completed_array.container_element_types.size(), 1);
	CHECK_EQ(type_at(completed_array.container_element_types, 0).enum_values.size(), 3);
	CHECK(type_at(completed_array.container_element_types, 0).type_arguments == shell.type_arguments);
}

TEST_CASE("[Modules][FoundryScript][GenericTaggedUnionScope] Generic union misuse reports one pinned diagnostic") {
	SUBCASE("bare external use") {
		FSParser parser;
		REQUIRE_EQ(parser.parse(
						   "enum Holder[T]:\n"
						   "\tValue(value: T)\n"
						   "\n"
						   "func take(held: Holder) -> void:\n"
						   "\tprint(held)\n",
						   "user://generic_tagged_union_bare.fs", false),
				OK);
		FSAnalyzer analyzer(&parser);
		CHECK_NE(analyzer.analyze(), OK);
		REQUIRE_EQ(parser.get_errors().size(), 1);
		CHECK_EQ(first_error_message(parser),
				String(R"(Generic tagged union "Holder" expects 1 type argument(s), but 0 were given.)"));
	}

	SUBCASE("wrong recursive arity") {
		FSParser parser;
		REQUIRE_EQ(parser.parse(
						   "enum Pair[T, U]:\n"
						   "\tBoth(first: T, second: U)\n"
						   "\tNested(inner: Pair[T])\n",
						   "user://generic_tagged_union_arity.fs", false),
				OK);
		FSAnalyzer analyzer(&parser);
		CHECK_NE(analyzer.analyze(), OK);
		REQUIRE_EQ(parser.get_errors().size(), 1);
		CHECK_EQ(first_error_message(parser),
				String(R"(Generic tagged union "Pair" expects 2 type argument(s), but 1 were given.)"));
	}

	SUBCASE("exact-arity external application") {
		FSParser parser;
		REQUIRE_EQ(parser.parse(
						   "enum Outcome[T, E]:\n"
						   "\tOk(value: T)\n"
						   "\tErr(error: E)\n"
						   "\n"
						   "func take(outcome: Outcome[int, String]) -> void:\n"
						   "\tprint(outcome)\n",
						   "user://generic_tagged_union_application.fs", false),
				OK);
		FSAnalyzer analyzer(&parser);
		CHECK_NE(analyzer.analyze(), OK);
		REQUIRE_EQ(parser.get_errors().size(), 1);
		CHECK_EQ(first_error_message(parser),
				String(R"(Generic tagged union "Outcome" type application is not available yet.)"));
	}

	SUBCASE("external case reference") {
		FSParser parser;
		REQUIRE_EQ(parser.parse(
						   "enum Holder[T]:\n"
						   "\tValue(value: T)\n"
						   "\n"
						   "func check(value: Variant) -> bool:\n"
						   "\treturn value is Holder.Value\n",
						   "user://generic_tagged_union_case.fs", false),
				OK);
		FSAnalyzer analyzer(&parser);
		CHECK_NE(analyzer.analyze(), OK);
		REQUIRE_EQ(parser.get_errors().size(), 1);
		CHECK_EQ(first_error_message(parser),
				String(R"(Generic tagged union "Holder" expects 1 type argument(s), but 0 were given.)"));
	}

	SUBCASE("decorated self argument") {
		// `T?` names the same parameter but is a different type, so it is an application, not the
		// declaration's own open vector.
		FSParser parser;
		REQUIRE_EQ(parser.parse(
						   "enum Slot[T]:\n"
						   "\tValue(value: T)\n"
						   "\tNested(inner: Slot[T?])\n",
						   "user://generic_tagged_union_nullable_self.fs", false),
				OK);
		FSAnalyzer analyzer(&parser);
		CHECK_NE(analyzer.analyze(), OK);
		REQUIRE_EQ(parser.get_errors().size(), 1);
		CHECK_EQ(first_error_message(parser),
				String(R"(Generic tagged union "Slot" type application is not available yet.)"));
	}
}

TEST_CASE("[Modules][FoundryScript][GenericTaggedUnionScope] Bounded parameters still spell the open self type") {
	SUBCASE("explicit Variant bound") {
		// A use-site handle carries the explicit `Variant` bound that the published open handle drops,
		// so identity must not depend on the bound.
		FSParser parser;
		REQUIRE_EQ(parser.parse(
						   "enum Slot[T: Variant]:\n"
						   "\tValue(value: T)\n"
						   "\tNested(inner: Slot[T])\n",
						   "user://generic_tagged_union_variant_bound.fs", false),
				OK);
		FSAnalyzer analyzer(&parser);
		CHECK_EQ(analyzer.analyze(), OK);
		CHECK_EQ(first_error_message(parser), String());

		const FSParser::EnumNode *slot = find_enum(parser, SNAME("Slot"));
		REQUIRE(slot != nullptr);
		const FSParser::DataType nested = payload_field_type(slot, SNAME("Nested"), 0);
		CHECK(nested.kind == FSParser::DataType::ENUM);
		REQUIRE_EQ(nested.type_arguments.size(), 1);
		CHECK(is_type_parameter(type_at(nested.type_arguments, 0), SNAME("T"),
				FSParser::DataType::TYPE_PARAMETER_ENUM, 0));
	}

	SUBCASE("bound naming the union itself") {
		// The open identity is published before bounds are resolved, so a self-referential bound reads
		// it instead of re-entering resolution and reporting a cycle.
		FSParser parser;
		REQUIRE_EQ(parser.parse(
						   "enum Recursive[T: Recursive]:\n"
						   "\tValue(value: T)\n",
						   "user://generic_tagged_union_self_bound.fs", false),
				OK);
		FSAnalyzer analyzer(&parser);
		CHECK_EQ(analyzer.analyze(), OK);
		CHECK_EQ(first_error_message(parser), String());

		const FSParser::EnumNode *recursive = find_enum(parser, SNAME("Recursive"));
		REQUIRE(recursive != nullptr);
		REQUIRE_EQ(recursive->type_parameters.size(), 1);
		const FSParser::DataType bound = recursive->type_parameters[0]->resolved_bound;
		CHECK(bound.kind == FSParser::DataType::ENUM);
		CHECK(bound.is_tagged_union);
	}
}

TEST_CASE("[Modules][FoundryScript][GenericTaggedUnionScope] Enum scope does not reach a forward-resolved class") {
	// Resolving the payload pulls `Sibling` in while the union is the active enum. `Sibling`'s own `T`
	// is a class parameter and must stay one, so parameter visibility follows lexical ownership rather
	// than whichever declaration happened to trigger the resolution.
	FSParser parser;
	REQUIRE_EQ(parser.parse(
					   "enum Holder[T]:\n"
					   "\tValue(box: Sibling)\n"
					   "\n"
					   "class Sibling:\n"
					   "\tclass Inner[T]:\n"
					   "\t\tvar held: T\n",
					   "user://generic_tagged_union_sibling.fs", false),
			OK);
	FSAnalyzer analyzer(&parser);
	REQUIRE_EQ(analyzer.analyze(), OK);

	const FSParser::ClassNode *sibling = find_inner_class(parser.get_tree(), SNAME("Sibling"));
	REQUIRE(sibling != nullptr);
	const FSParser::ClassNode *inner = find_inner_class(sibling, SNAME("Inner"));
	REQUIRE(inner != nullptr);
	CHECK(is_type_parameter(inner->get_member(SNAME("held")).variable->get_datatype(), SNAME("T"),
			FSParser::DataType::TYPE_PARAMETER_CLASS, 0));
}

TEST_CASE("[Modules][FoundryScript][GenericTaggedUnionScope] A union parameter is invisible outside its owner") {
	// `Helper` declares no `T`, so it must report the unresolved name whether or not the union that
	// pulled it in is still the active enum.
	FSParser parser;
	REQUIRE_EQ(parser.parse(
					   "enum Holder[T]:\n"
					   "\tValue(box: Helper)\n"
					   "\n"
					   "class Helper:\n"
					   "\tvar held: T\n",
					   "user://generic_tagged_union_outside_owner.fs", false),
			OK);
	FSAnalyzer analyzer(&parser);
	CHECK_NE(analyzer.analyze(), OK);
	CHECK_EQ(first_error_message(parser), String(R"(Could not find type "T" in the current scope.)"));
}

TEST_CASE("[Modules][FoundryScript][GenericTaggedUnionScope] Enum arguments do not bind enclosing class parameters") {
	// A union's open arguments belong to the union, not to the class that owns it, so calling an enum
	// function must not substitute the enum's parameters for the enclosing class's.
	FSParser parser;
	REQUIRE_EQ(parser.parse(
					   "class Outer[ClassT]:\n"
					   "\tenum Choice[EnumT]:\n"
					   "\t\tValue(value: EnumT)\n"
					   "\n"
					   "\t\tstatic func identity(value: ClassT) -> ClassT:\n"
					   "\t\t\treturn value\n"
					   "\n"
					   "\tfunc call_it(value: ClassT) -> ClassT:\n"
					   "\t\treturn Choice.identity(value)\n",
					   "user://generic_tagged_union_enum_arguments.fs", false),
			OK);
	FSAnalyzer analyzer(&parser);
	CHECK_EQ(analyzer.analyze(), OK);
	CHECK_EQ(first_error_message(parser), String());
}

TEST_CASE("[Modules][FoundryScript][GenericTaggedUnionScope] An enum parameter cannot shadow a class bound") {
	// `Box`'s bound resolves in `Box`'s own scope, so the enum parameter active at the use site must
	// not stand in for the outer class's same-named parameter: the enum's `Bounded` is a different
	// parameter and does not satisfy the class-scoped bound.
	FSParser parser;
	REQUIRE_EQ(parser.parse(
					   "class Outer[Bounded: Resource]:\n"
					   "\tclass Box[X: Bounded]:\n"
					   "\t\tvar held: X\n"
					   "\n"
					   "\tenum Holder[Bounded]:\n"
					   "\t\tValue(box: Box[Bounded])\n",
					   "user://generic_tagged_union_class_bound.fs", false),
			OK);
	FSAnalyzer analyzer(&parser);
	CHECK_NE(analyzer.analyze(), OK);
	REQUIRE_EQ(parser.get_errors().size(), 1);
	CHECK_EQ(first_error_message(parser),
			String(R"(Type argument "Bounded" does not satisfy the bound "Bounded" of type parameter "X".)"));
}

} // namespace GenericTaggedUnion
} // namespace FSTests
