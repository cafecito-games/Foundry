/**************************************************************************/
/*  test_generic_analyzer.h                                               */
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

#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript_parser.h"

#include "tests/test_macros.h"

namespace GDScriptTests {

// These tests drive the analyzer end to end and inspect resolved DataTypes, so they
// verify that type parameters and specialized handles are interpreted (not just parsed).

static const GDScriptParser::ClassNode *generic_find_inner_class(const GDScriptParser::ClassNode *p_class, const StringName &p_name) {
	if (!p_class->has_member(p_name)) {
		return nullptr;
	}
	const GDScriptParser::ClassNode::Member member = p_class->get_member(p_name);
	if (member.type != GDScriptParser::ClassNode::Member::CLASS) {
		return nullptr;
	}
	return member.m_class;
}

static const GDScriptParser::VariableNode *generic_find_member_variable(const GDScriptParser::ClassNode *p_class, const StringName &p_name) {
	if (!p_class->has_member(p_name)) {
		return nullptr;
	}
	const GDScriptParser::ClassNode::Member member = p_class->get_member(p_name);
	if (member.type != GDScriptParser::ClassNode::Member::VARIABLE) {
		return nullptr;
	}
	return member.variable;
}

static const GDScriptParser::FunctionNode *generic_find_function(const GDScriptParser::ClassNode *p_class, const StringName &p_name) {
	if (!p_class->has_function(p_name)) {
		return nullptr;
	}
	return p_class->get_member(p_name).function;
}

static const GDScriptParser::VariableNode *generic_find_local_variable(const GDScriptParser::FunctionNode *p_function, const StringName &p_name) {
	if (p_function == nullptr || p_function->body == nullptr) {
		return nullptr;
	}
	for (GDScriptParser::Node *statement : p_function->body->statements) {
		if (statement->type != GDScriptParser::Node::VARIABLE) {
			continue;
		}
		const GDScriptParser::VariableNode *variable = static_cast<const GDScriptParser::VariableNode *>(statement);
		if (variable->identifier && variable->identifier->name == p_name) {
			return variable;
		}
	}
	return nullptr;
}

TEST_CASE("[Modules][GDScript] Analyzer resolves a class type parameter inside a generic class") {
	GDScriptParser parser;
	const Error parse_error = parser.parse("class Box[T]:\n\tvar value: T\n", "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::ClassNode *box = generic_find_inner_class(parser.get_tree(), "Box");
	REQUIRE(box != nullptr);
	const GDScriptParser::VariableNode *value = generic_find_member_variable(box, "value");
	REQUIRE(value != nullptr);

	const GDScriptParser::DataType type = value->get_datatype();
	CHECK(type.kind == GDScriptParser::DataType::TYPE_PARAMETER);
	CHECK(type.type_parameter_name == StringName("T"));
	CHECK(type.type_parameter_scope == GDScriptParser::DataType::TYPE_PARAMETER_CLASS);
	CHECK(type.type_parameter_index == 0);
}

TEST_CASE("[Modules][GDScript] Analyzer resolves a method type parameter in signatures") {
	GDScriptParser parser;
	const Error parse_error = parser.parse("func identity[T](v: T) -> T:\n\treturn v\n", "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::FunctionNode *identity = generic_find_function(parser.get_tree(), "identity");
	REQUIRE(identity != nullptr);
	REQUIRE(identity->parameters.size() == 1);

	const GDScriptParser::DataType parameter_type = identity->parameters[0]->get_datatype();
	CHECK(parameter_type.kind == GDScriptParser::DataType::TYPE_PARAMETER);
	CHECK(parameter_type.type_parameter_name == StringName("T"));
	CHECK(parameter_type.type_parameter_scope == GDScriptParser::DataType::TYPE_PARAMETER_METHOD);

	const GDScriptParser::DataType return_type = identity->get_datatype();
	CHECK(return_type.kind == GDScriptParser::DataType::TYPE_PARAMETER);
	CHECK(return_type.type_parameter_name == StringName("T"));
}

TEST_CASE("[Modules][GDScript] Analyzer records the bound of a constrained type parameter") {
	GDScriptParser parser;
	const Error parse_error = parser.parse("class Holder[T: RefCounted]:\n\tvar value: T\n", "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::ClassNode *holder = generic_find_inner_class(parser.get_tree(), "Holder");
	REQUIRE(holder != nullptr);
	const GDScriptParser::VariableNode *value = generic_find_member_variable(holder, "value");
	REQUIRE(value != nullptr);

	const GDScriptParser::DataType type = value->get_datatype();
	REQUIRE(type.kind == GDScriptParser::DataType::TYPE_PARAMETER);
	REQUIRE(type.type_parameter_bound.size() == 1);
	CHECK(type.type_parameter_bound[0].kind == GDScriptParser::DataType::NATIVE);
	CHECK(type.type_parameter_bound[0].native_type == StringName("RefCounted"));
}

TEST_CASE("[Modules][GDScript] Analyzer resolves a specialized class handle into type arguments") {
	GDScriptParser parser;
	const Error parse_error = parser.parse("class Box[T]:\n\tvar value: T\nvar boxed: Box[int]\n", "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::VariableNode *boxed = generic_find_member_variable(parser.get_tree(), "boxed");
	REQUIRE(boxed != nullptr);

	const GDScriptParser::DataType type = boxed->get_datatype();
	CHECK(type.kind == GDScriptParser::DataType::CLASS);
	REQUIRE(type.type_arguments.size() == 1);
	CHECK(type.type_arguments[0].kind == GDScriptParser::DataType::BUILTIN);
	CHECK(type.type_arguments[0].builtin_type == Variant::INT);
}

TEST_CASE("[Modules][GDScript] Analyzer substitutes type arguments on member access") {
	GDScriptParser parser;
	const String source =
			"class Box[T]:\n"
			"\tvar value: T\n"
			"func test() -> void:\n"
			"\tvar b: Box[int]\n"
			"\tvar got := b.value\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::FunctionNode *test = generic_find_function(parser.get_tree(), "test");
	REQUIRE(test != nullptr);
	const GDScriptParser::VariableNode *got = generic_find_local_variable(test, "got");
	REQUIRE(got != nullptr);

	const GDScriptParser::DataType type = got->get_datatype();
	CHECK(type.kind == GDScriptParser::DataType::BUILTIN);
	CHECK(type.builtin_type == Variant::INT);
}

TEST_CASE("[Modules][GDScript] Analyzer rejects a wrong type-argument arity") {
	GDScriptParser parser;
	const Error parse_error = parser.parse("class Box[T]:\n\tvar value: T\nvar bad: Box[int, String]\n", "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	const Error analyze_error = analyzer.analyze();
	CHECK(analyze_error != OK);

	bool found_arity_error = false;
	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message.contains("type argument")) {
			found_arity_error = true;
			break;
		}
	}
	CHECK(found_arity_error);
}

TEST_CASE("[Modules][GDScript] Analyzer rejects specializing a non-generic class") {
	GDScriptParser parser;
	const Error parse_error = parser.parse("class Plain:\n\tpass\nvar bad: Plain[int]\n", "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	const Error analyze_error = analyzer.analyze();
	CHECK(analyze_error != OK);
}

} // namespace GDScriptTests
