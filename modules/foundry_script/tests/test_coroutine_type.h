/**************************************************************************/
/*  test_coroutine_type.h                                                 */
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

#include "modules/foundry_script/fs_parser.h"

#include "tests/test_macros.h"

namespace FSTests {

// These tests inspect the parsed AST directly so they fail if the `Coroutine[T]`
// generic type syntax is consumed but its result-type metadata or coroutine
// marker is dropped, which a runtime script fixture would not catch (analyzer
// resolution of the type lands in a follow-up change).

static const FSParser::TypeNode *first_variable_type(const FSParser &p_parser, const StringName &p_function_name) {
	const FSParser::ClassNode *root_class = p_parser.get_tree();
	if (root_class == nullptr || !root_class->has_function(p_function_name)) {
		return nullptr;
	}
	const FSParser::FunctionNode *function = root_class->get_member(p_function_name).function;
	if (function == nullptr || function->body == nullptr || function->body->statements.is_empty()) {
		return nullptr;
	}
	const FSParser::Node *statement = function->body->statements[0];
	if (statement->type != FSParser::Node::VARIABLE) {
		return nullptr;
	}
	return static_cast<const FSParser::VariableNode *>(statement)->datatype_specifier;
}

TEST_CASE("[Modules][FoundryScript] Coroutine[T] records its result type and coroutine marker") {
	FSParser parser;
	const Error error = parser.parse("func test():\n\tvar job: Coroutine[int]\n", "user://test.fs", false);
	REQUIRE(error == OK);
	REQUIRE(parser.get_errors().is_empty());

	const FSParser::TypeNode *type = first_variable_type(parser, "test");
	REQUIRE(type != nullptr);
	CHECK(type->is_coroutine);
	CHECK_FALSE(type->is_nullable);
	REQUIRE(type->type_chain.size() == 1);
	CHECK(type->type_chain[0]->name == StringName("Coroutine"));
	REQUIRE(type->container_types.size() == 1);
	REQUIRE(type->container_types[0]->type_chain.size() == 1);
	CHECK(type->container_types[0]->type_chain[0]->name == StringName("int"));
}

TEST_CASE("[Modules][FoundryScript] A bare Coroutine is not marked as the Coroutine[T] form") {
	// The coroutine marker is reserved for the bracketed generic form so it always
	// implies a recorded result type; a bare Coroutine is just an ordinary type name.
	FSParser parser;
	const Error error = parser.parse("func test():\n\tvar job: Coroutine\n", "user://test.fs", false);
	REQUIRE(error == OK);
	REQUIRE(parser.get_errors().is_empty());

	const FSParser::TypeNode *type = first_variable_type(parser, "test");
	REQUIRE(type != nullptr);
	CHECK_FALSE(type->is_coroutine);
	CHECK(type->container_types.is_empty());
}

TEST_CASE("[Modules][FoundryScript] Coroutine[void] names a void-returning async result") {
	FSParser parser;
	const Error error = parser.parse("func test():\n\tvar job: Coroutine[void]\n", "user://test.fs", false);
	REQUIRE(error == OK);
	REQUIRE(parser.get_errors().is_empty());

	const FSParser::TypeNode *type = first_variable_type(parser, "test");
	REQUIRE(type != nullptr);
	CHECK(type->is_coroutine);
	REQUIRE(type->container_types.size() == 1);
	CHECK(type->container_types[0]->type_chain.is_empty()); // `void` parses as a TypeNode with no type chain.
}

TEST_CASE("[Modules][FoundryScript] Coroutine[T] accepts the nullable variant") {
	FSParser parser;
	const Error error = parser.parse("func test():\n\tvar job: Coroutine[String]?\n", "user://test.fs", false);
	REQUIRE(error == OK);
	REQUIRE(parser.get_errors().is_empty());

	const FSParser::TypeNode *type = first_variable_type(parser, "test");
	REQUIRE(type != nullptr);
	CHECK(type->is_coroutine);
	CHECK(type->is_nullable);
	REQUIRE(type->container_types.size() == 1);
	CHECK(type->container_types[0]->type_chain[0]->name == StringName("String"));
}

TEST_CASE("[Modules][FoundryScript] Coroutine nests as a container element type") {
	FSParser parser;
	const Error error = parser.parse("func test():\n\tvar jobs: Array[Coroutine[int]]\n", "user://test.fs", false);
	REQUIRE(error == OK);
	REQUIRE(parser.get_errors().is_empty());

	const FSParser::TypeNode *type = first_variable_type(parser, "test");
	REQUIRE(type != nullptr);
	CHECK_FALSE(type->is_coroutine); // The outer type is Array, not Coroutine.
	REQUIRE(type->type_chain.size() == 1);
	CHECK(type->type_chain[0]->name == StringName("Array"));
	REQUIRE(type->container_types.size() == 1);

	const FSParser::TypeNode *element = type->container_types[0];
	REQUIRE(element != nullptr);
	CHECK(element->is_coroutine);
	REQUIRE(element->type_chain.size() == 1);
	CHECK(element->type_chain[0]->name == StringName("Coroutine"));
	REQUIRE(element->container_types.size() == 1);
	CHECK(element->container_types[0]->type_chain[0]->name == StringName("int"));
}

TEST_CASE("[Modules][FoundryScript] Coroutine[T] is recorded in a function return type") {
	// The variable-position cases above never exercise the return-type slot, which is parsed
	// through a separate path, so a regression there would otherwise go unnoticed.
	FSParser parser;
	const Error error = parser.parse("func start() -> Coroutine[String]:\n\tpass\n", "user://test.fs", false);
	REQUIRE(error == OK);
	REQUIRE(parser.get_errors().is_empty());

	const FSParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);
	REQUIRE(root_class->has_function(StringName("start")));
	const FSParser::FunctionNode *function = root_class->get_member(StringName("start")).function;
	REQUIRE(function != nullptr);

	const FSParser::TypeNode *type = function->return_type;
	REQUIRE(type != nullptr);
	CHECK(type->is_coroutine);
	REQUIRE(type->type_chain.size() == 1);
	CHECK(type->type_chain[0]->name == StringName("Coroutine"));
	REQUIRE(type->container_types.size() == 1);
	CHECK(type->container_types[0]->type_chain[0]->name == StringName("String"));
}

TEST_CASE("[Modules][FoundryScript] Coroutine[T] is recorded in a parameter type") {
	FSParser parser;
	const Error error = parser.parse("func consume(job: Coroutine[int]):\n\tpass\n", "user://test.fs", false);
	REQUIRE(error == OK);
	REQUIRE(parser.get_errors().is_empty());

	const FSParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);
	REQUIRE(root_class->has_function(StringName("consume")));
	const FSParser::FunctionNode *function = root_class->get_member(StringName("consume")).function;
	REQUIRE(function != nullptr);
	REQUIRE(function->parameters.size() == 1);

	const FSParser::TypeNode *type = function->parameters[0]->datatype_specifier;
	REQUIRE(type != nullptr);
	CHECK(type->is_coroutine);
	REQUIRE(type->type_chain.size() == 1);
	CHECK(type->type_chain[0]->name == StringName("Coroutine"));
	REQUIRE(type->container_types.size() == 1);
	CHECK(type->container_types[0]->type_chain[0]->name == StringName("int"));
}

TEST_CASE("[Modules][FoundryScript] Coroutine[] rejects a missing result type") {
	FSParser parser;
	const Error error = parser.parse("func test():\n\tvar job: Coroutine[]\n", "user://test.fs", false);
	CHECK(error != OK);
	REQUIRE_FALSE(parser.get_errors().is_empty());
	CHECK(parser.get_errors().front()->get().message == String("Coroutine[T] expects a single result type parameter."));
}

TEST_CASE("[Modules][FoundryScript] Coroutine[T, U] rejects extra result types") {
	FSParser parser;
	const Error error = parser.parse("func test():\n\tvar job: Coroutine[int, String]\n", "user://test.fs", false);
	CHECK(error != OK);
	REQUIRE_FALSE(parser.get_errors().is_empty());
	CHECK(parser.get_errors().front()->get().message == String("Coroutine[T] expects a single result type parameter, but more were given."));
}

} // namespace FSTests
