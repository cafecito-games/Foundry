/**************************************************************************/
/*  test_type_parameters.h                                                */
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

// These tests inspect the parsed AST directly so they fail if a generic
// declaration is consumed but its type parameters are dropped, which a
// runtime script fixture would not catch.

static const GDScriptParser::FunctionNode *find_function(const GDScriptParser::ClassNode *p_class, const StringName &p_name) {
	if (!p_class->has_function(p_name)) {
		return nullptr;
	}
	return p_class->get_member(p_name).function;
}

static const GDScriptParser::ClassNode *find_inner_class(const GDScriptParser::ClassNode *p_class, const StringName &p_name) {
	if (!p_class->has_member(p_name)) {
		return nullptr;
	}
	const GDScriptParser::ClassNode::Member member = p_class->get_member(p_name);
	if (member.type != GDScriptParser::ClassNode::Member::CLASS) {
		return nullptr;
	}
	return member.m_class;
}

static StringName bound_name(const GDScriptParser::TypeParameterNode *p_type_parameter) {
	if (p_type_parameter->bound == nullptr || p_type_parameter->bound->type_chain.is_empty()) {
		return StringName();
	}
	return p_type_parameter->bound->type_chain[0]->name;
}

TEST_CASE("[Modules][GDScript] Type parameters parse on class_name declarations") {
	GDScriptParser parser;
	const Error error = parser.parse("class_name Pair[K, V]\n", "user://test.gd", false);
	REQUIRE(error == OK);

	const GDScriptParser::ClassNode *root = parser.get_tree();
	REQUIRE(root != nullptr);
	REQUIRE(root->type_parameters.size() == 2);
	CHECK(root->type_parameters[0]->identifier->name == StringName("K"));
	CHECK(root->type_parameters[0]->bound == nullptr);
	CHECK(root->type_parameters[1]->identifier->name == StringName("V"));
	CHECK(root->type_parameters[1]->bound == nullptr);
}

TEST_CASE("[Modules][GDScript] Type parameters parse on inner classes with bounds") {
	GDScriptParser parser;
	const Error error = parser.parse("class Box[T]:\n\tpass\nclass Bounded[K, V: RefCounted]:\n\tpass\n", "user://test.gd", false);
	REQUIRE(error == OK);

	const GDScriptParser::ClassNode *root = parser.get_tree();
	REQUIRE(root != nullptr);

	const GDScriptParser::ClassNode *box = find_inner_class(root, "Box");
	REQUIRE(box != nullptr);
	REQUIRE(box->type_parameters.size() == 1);
	CHECK(box->type_parameters[0]->identifier->name == StringName("T"));
	CHECK(box->type_parameters[0]->bound == nullptr);

	const GDScriptParser::ClassNode *bounded = find_inner_class(root, "Bounded");
	REQUIRE(bounded != nullptr);
	REQUIRE(bounded->type_parameters.size() == 2);
	CHECK(bounded->type_parameters[0]->identifier->name == StringName("K"));
	CHECK(bounded->type_parameters[0]->bound == nullptr);
	CHECK(bounded->type_parameters[1]->identifier->name == StringName("V"));
	CHECK(bound_name(bounded->type_parameters[1]) == StringName("RefCounted"));
}

TEST_CASE("[Modules][GDScript] Type parameters parse on generic methods") {
	GDScriptParser parser;
	const Error error = parser.parse("func swap[T]():\n\tpass\nfunc constrained[T: RefCounted]():\n\tpass\n", "user://test.gd", false);
	REQUIRE(error == OK);

	const GDScriptParser::ClassNode *root = parser.get_tree();
	REQUIRE(root != nullptr);

	const GDScriptParser::FunctionNode *swap = find_function(root, "swap");
	REQUIRE(swap != nullptr);
	REQUIRE(swap->type_parameters.size() == 1);
	CHECK(swap->type_parameters[0]->identifier->name == StringName("T"));
	CHECK(swap->type_parameters[0]->bound == nullptr);

	const GDScriptParser::FunctionNode *constrained = find_function(root, "constrained");
	REQUIRE(constrained != nullptr);
	REQUIRE(constrained->type_parameters.size() == 1);
	CHECK(constrained->type_parameters[0]->identifier->name == StringName("T"));
	CHECK(bound_name(constrained->type_parameters[0]) == StringName("RefCounted"));
}

TEST_CASE("[Modules][GDScript] A non-generic declaration has no type parameters") {
	GDScriptParser parser;
	const Error error = parser.parse("func plain():\n\tpass\n", "user://test.gd", false);
	REQUIRE(error == OK);

	const GDScriptParser::FunctionNode *plain = find_function(parser.get_tree(), "plain");
	REQUIRE(plain != nullptr);
	CHECK(plain->type_parameters.is_empty());
}

} // namespace GDScriptTests
