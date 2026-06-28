/**************************************************************************/
/*  test_annotation_declarations.h                                        */
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

// These tests inspect the parsed AST directly so they fail if an `annotation`
// declaration is consumed but its targets, parameters, variadic flag, or
// canonical identity are dropped, which a runtime script fixture cannot catch.

static const FSParser::AnnotationDeclarationNode *find_annotation_declaration(const FSParser::ClassNode *p_class, const StringName &p_name) {
	for (FSParser::AnnotationDeclarationNode *declaration : p_class->annotation_declarations) {
		if (declaration->identifier != nullptr && declaration->identifier->name == p_name) {
			return declaration;
		}
	}
	return nullptr;
}

TEST_CASE("[Modules][FoundryScript] Marker annotation declaration parses with a single target") {
	FSParser parser;
	const Error error = parser.parse("annotation test targets METHOD\n", "user://test.fs", false);
	REQUIRE(error == OK);

	const FSParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);
	REQUIRE(root_class->annotation_declarations.size() == 1);

	const FSParser::AnnotationDeclarationNode *declaration = root_class->annotation_declarations[0];
	CHECK(declaration->identifier->name == StringName("test"));
	CHECK(declaration->targets == uint32_t(FSParser::AnnotationDeclarationNode::TARGET_METHOD));
	CHECK(declaration->parameters.is_empty());
	CHECK_FALSE(declaration->is_variadic());
	CHECK(declaration->qualified_name == "test");
}

TEST_CASE("[Modules][FoundryScript] Annotation declaration parses typed parameters, defaults, and multiple targets") {
	FSParser parser;
	const Error error = parser.parse("annotation skip(reason: String = \"\") targets METHOD, CLASS\n", "user://test.fs", false);
	REQUIRE(error == OK);

	const FSParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);
	REQUIRE(root_class->annotation_declarations.size() == 1);

	const FSParser::AnnotationDeclarationNode *declaration = root_class->annotation_declarations[0];
	CHECK(declaration->identifier->name == StringName("skip"));
	REQUIRE(declaration->parameters.size() == 1);
	CHECK(declaration->parameters[0]->identifier->name == StringName("reason"));
	CHECK(declaration->parameters[0]->datatype_specifier != nullptr);
	CHECK(declaration->parameters[0]->initializer != nullptr);
	CHECK_FALSE(declaration->is_variadic());
	CHECK((declaration->targets & FSParser::AnnotationDeclarationNode::TARGET_METHOD) != 0);
	CHECK((declaration->targets & FSParser::AnnotationDeclarationNode::TARGET_CLASS) != 0);
	CHECK((declaration->targets & FSParser::AnnotationDeclarationNode::TARGET_VARIABLE) == 0);
}

TEST_CASE("[Modules][FoundryScript] Annotation declaration parses SIGNAL and CONSTANT targets") {
	FSParser parser;
	const Error error = parser.parse("annotation marker targets SIGNAL, CONSTANT\n", "user://test.fs", false);
	REQUIRE(error == OK);

	const FSParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);
	REQUIRE(root_class->annotation_declarations.size() == 1);

	const FSParser::AnnotationDeclarationNode *declaration = root_class->annotation_declarations[0];
	CHECK((declaration->targets & FSParser::AnnotationDeclarationNode::TARGET_SIGNAL) != 0);
	CHECK((declaration->targets & FSParser::AnnotationDeclarationNode::TARGET_CONSTANT) != 0);
	CHECK((declaration->targets & FSParser::AnnotationDeclarationNode::TARGET_METHOD) == 0);
}

TEST_CASE("[Modules][FoundryScript] Annotation declaration parses a final variadic parameter") {
	FSParser parser;
	const Error error = parser.parse("annotation tags(...names: String) targets METHOD, CLASS\n", "user://test.fs", false);
	REQUIRE(error == OK);

	const FSParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);
	REQUIRE(root_class->annotation_declarations.size() == 1);

	const FSParser::AnnotationDeclarationNode *declaration = root_class->annotation_declarations[0];
	CHECK(declaration->identifier->name == StringName("tags"));
	CHECK(declaration->parameters.is_empty());
	REQUIRE(declaration->is_variadic());
	CHECK(declaration->rest_parameter->identifier->name == StringName("names"));
}

TEST_CASE("[Modules][FoundryScript] Annotation declaration canonical identity uses the file namespace") {
	FSParser parser;
	const Error error = parser.parse("namespace cafecito.test\nannotation timeout(seconds: float) targets METHOD\n", "user://test.fs", false);
	REQUIRE(error == OK);

	const FSParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);

	const FSParser::AnnotationDeclarationNode *declaration = find_annotation_declaration(root_class, "timeout");
	REQUIRE(declaration != nullptr);
	CHECK(declaration->qualified_name == "cafecito.test.timeout");
}

TEST_CASE("[Modules][FoundryScript] Annotation declarations stay out of the runtime member list") {
	FSParser parser;
	const Error error = parser.parse("annotation test targets METHOD\nfunc test():\n\tpass\n", "user://test.fs", false);
	REQUIRE(error == OK);

	const FSParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);

	// The declaration and the function share the short name `test` but live in
	// separate symbol spaces. The member must remain the function.
	REQUIRE(root_class->has_member("test"));
	CHECK(root_class->get_member("test").type == FSParser::ClassNode::Member::FUNCTION);
	CHECK(find_annotation_declaration(root_class, "test") != nullptr);
}

TEST_CASE("[Modules][FoundryScript] Annotation declarations are rejected in inner classes") {
	FSParser parser;
	const Error error = parser.parse("class Inner:\n\tannotation test targets METHOD\n", "user://test.fs", false);
	CHECK(error != OK);

	const FSParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);
	CHECK(root_class->annotation_declarations.is_empty());
}

TEST_CASE("[Modules][FoundryScript] Annotation declarations are rejected in traits") {
	FSParser parser;
	const Error error = parser.parse("trait_name MyTrait\nannotation test targets METHOD\n", "user://test.fs", false);
	CHECK(error != OK);
}

TEST_CASE("[Modules][FoundryScript] Unknown annotation targets are parse errors") {
	FSParser parser;
	const Error error = parser.parse("annotation test targets WIDGET\n", "user://test.fs", false);
	CHECK(error != OK);
}

TEST_CASE("[Modules][FoundryScript] Duplicate annotation targets are parse errors") {
	FSParser parser;
	const Error error = parser.parse("annotation test targets METHOD, METHOD\n", "user://test.fs", false);
	CHECK(error != OK);
}

} // namespace FSTests
