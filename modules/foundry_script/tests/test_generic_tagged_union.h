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

#include "../fs_format.h"
#include "../fs_parser.h"

#include "tests/test_macros.h"

// Declaration-shape coverage for generic tagged unions. Only named tagged unions may carry type
// parameters, so these tests assert on the parsed AST and on the canonical formatter output rather
// than on analysis or runtime behavior, which land in later work.

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

static String format_source(const String &p_source) {
	FSFormatter formatter;
	FSFormatter::Result result;
	const Error err = formatter.format(p_source, "test.fs", result);
	CHECK_MESSAGE(err == OK, "Source must format without parse errors.");
	return result.formatted;
}

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

} // namespace GenericTaggedUnion
} // namespace FSTests
