/**************************************************************************/
/*  test_named_arguments.h                                                */
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

#include "modules/gdscript/gdscript_parser.h"

#include "tests/test_macros.h"

namespace GDScriptTests {

// These tests inspect the parsed AST directly so they fail if the parser drops
// the surface syntax of named call arguments (`name = value`). The analyzer that
// maps names to parameter positions is a separate stage; here we only assert that
// the parser records each argument's name parallel to its value.

static const GDScriptParser::CallNode *named_args_first_call(const GDScriptParser::ClassNode *p_class, const StringName &p_function) {
	if (p_class == nullptr || !p_class->has_function(p_function)) {
		return nullptr;
	}
	const GDScriptParser::FunctionNode *function = p_class->get_member(p_function).function;
	if (function == nullptr || function->body == nullptr || function->body->statements.is_empty()) {
		return nullptr;
	}
	const GDScriptParser::Node *statement = function->body->statements[0];
	if (statement->type != GDScriptParser::Node::CALL) {
		return nullptr;
	}
	return static_cast<const GDScriptParser::CallNode *>(statement);
}

TEST_CASE("[Modules][GDScript] Positional call arguments record empty names") {
	GDScriptParser parser;
	const Error error = parser.parse("func caller():\n\tf(1, 2, 3)\n", "user://test.gd", false);
	REQUIRE(error == OK);
	REQUIRE(parser.get_errors().is_empty());

	const GDScriptParser::CallNode *call = named_args_first_call(parser.get_tree(), "caller");
	REQUIRE(call != nullptr);
	REQUIRE(call->arguments.size() == 3);
	REQUIRE(call->argument_names.size() == call->arguments.size());
	for (int i = 0; i < call->argument_names.size(); i++) {
		CHECK(call->argument_names[i] == StringName());
	}
}

TEST_CASE("[Modules][GDScript] An all-named call records each parameter name") {
	GDScriptParser parser;
	const Error error = parser.parse("func caller():\n\tf(greeting = \"hi\", name = \"Bob\")\n", "user://test.gd", false);
	REQUIRE(error == OK);
	REQUIRE(parser.get_errors().is_empty());

	const GDScriptParser::CallNode *call = named_args_first_call(parser.get_tree(), "caller");
	REQUIRE(call != nullptr);
	REQUIRE(call->arguments.size() == 2);
	REQUIRE(call->argument_names.size() == 2);
	CHECK(call->argument_names[0] == StringName("greeting"));
	CHECK(call->argument_names[1] == StringName("name"));
	// The recorded values are the right-hand sides, not the names.
	REQUIRE(call->arguments[0]->type == GDScriptParser::Node::LITERAL);
	REQUIRE(call->arguments[1]->type == GDScriptParser::Node::LITERAL);
}

TEST_CASE("[Modules][GDScript] Positional-then-named mixing keeps the vectors aligned") {
	GDScriptParser parser;
	const Error error = parser.parse("func caller():\n\tf(1, b = 2, c = 3)\n", "user://test.gd", false);
	REQUIRE(error == OK);
	REQUIRE(parser.get_errors().is_empty());

	const GDScriptParser::CallNode *call = named_args_first_call(parser.get_tree(), "caller");
	REQUIRE(call != nullptr);
	REQUIRE(call->arguments.size() == 3);
	REQUIRE(call->argument_names.size() == 3);
	CHECK(call->argument_names[0] == StringName());
	CHECK(call->argument_names[1] == StringName("b"));
	CHECK(call->argument_names[2] == StringName("c"));
}

TEST_CASE("[Modules][GDScript] A named argument value can be an arbitrary expression") {
	GDScriptParser parser;
	const Error error = parser.parse("func caller():\n\tf(value = 1 + 2 * 3)\n", "user://test.gd", false);
	REQUIRE(error == OK);
	REQUIRE(parser.get_errors().is_empty());

	const GDScriptParser::CallNode *call = named_args_first_call(parser.get_tree(), "caller");
	REQUIRE(call != nullptr);
	REQUIRE(call->arguments.size() == 1);
	REQUIRE(call->argument_names.size() == 1);
	CHECK(call->argument_names[0] == StringName("value"));
	// `1 + 2 * 3` parses as a binary-operator expression, not a bare literal.
	CHECK(call->arguments[0]->type == GDScriptParser::Node::BINARY_OPERATOR);
}

TEST_CASE("[Modules][GDScript] Equality in an argument is not a named argument") {
	GDScriptParser parser;
	const Error error = parser.parse("func caller():\n\tf(a == b)\n", "user://test.gd", false);
	REQUIRE(error == OK);
	REQUIRE(parser.get_errors().is_empty());

	const GDScriptParser::CallNode *call = named_args_first_call(parser.get_tree(), "caller");
	REQUIRE(call != nullptr);
	REQUIRE(call->arguments.size() == 1);
	REQUIRE(call->argument_names.size() == 1);
	CHECK(call->argument_names[0] == StringName());
	CHECK(call->arguments[0]->type == GDScriptParser::Node::BINARY_OPERATOR);
}

TEST_CASE("[Modules][GDScript] A bare identifier argument is positional") {
	GDScriptParser parser;
	const Error error = parser.parse("func caller():\n\tf(a)\n", "user://test.gd", false);
	REQUIRE(error == OK);
	REQUIRE(parser.get_errors().is_empty());

	const GDScriptParser::CallNode *call = named_args_first_call(parser.get_tree(), "caller");
	REQUIRE(call != nullptr);
	REQUIRE(call->arguments.size() == 1);
	REQUIRE(call->argument_names.size() == 1);
	CHECK(call->argument_names[0] == StringName());
	CHECK(call->arguments[0]->type == GDScriptParser::Node::IDENTIFIER);
}

} // namespace GDScriptTests
