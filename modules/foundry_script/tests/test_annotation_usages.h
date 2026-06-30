/**************************************************************************/
/*  test_annotation_usages.h                                              */
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

// These tests inspect the parsed AST directly so they fail if a custom annotation
// usage is rejected at parse time, or if its positional/named arguments or their
// source order are dropped, which a runtime script fixture cannot observe yet
// (analyzer resolution lands in a later change).

static const List<FSParser::AnnotationNode *> *find_function_annotations(const FSParser::ClassNode *p_class, const StringName &p_name) {
	if (!p_class->has_member(p_name)) {
		return nullptr;
	}
	const FSParser::ClassNode::Member &member = p_class->get_member(p_name);
	if (member.type != FSParser::ClassNode::Member::FUNCTION) {
		return nullptr;
	}
	return &member.function->annotations;
}

TEST_CASE("[Modules][FoundryScript] Unknown annotation usage is preserved as a custom node") {
	FSParser parser;
	const Error error = parser.parse("@my_marker\nfunc test():\n\tpass\n", "user://test.fs", false);
	REQUIRE(error == OK);

	const FSParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);

	const List<FSParser::AnnotationNode *> *annotations = find_function_annotations(root_class, "test");
	REQUIRE(annotations != nullptr);
	REQUIRE(annotations->size() == 1);

	const FSParser::AnnotationNode *annotation = annotations->front()->get();
	CHECK(annotation->name == StringName("@my_marker"));
	CHECK(annotation->is_custom);
	CHECK(annotation->info == nullptr);
	CHECK(annotation->arguments.is_empty());
	CHECK(annotation->argument_names.is_empty());
}

TEST_CASE("[Modules][FoundryScript] Custom annotation usage records positional arguments") {
	FSParser parser;
	const Error error = parser.parse("@tags(\"gameplay\", \"slow\")\nfunc test():\n\tpass\n", "user://test.fs", false);
	REQUIRE(error == OK);

	const FSParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);

	const List<FSParser::AnnotationNode *> *annotations = find_function_annotations(root_class, "test");
	REQUIRE(annotations != nullptr);
	REQUIRE(annotations->size() == 1);

	const FSParser::AnnotationNode *annotation = annotations->front()->get();
	CHECK(annotation->is_custom);
	REQUIRE(annotation->arguments.size() == 2);
	REQUIRE(annotation->argument_names.size() == 2);
	// Positional arguments carry an empty name.
	CHECK(annotation->argument_names[0] == StringName());
	CHECK(annotation->argument_names[1] == StringName());
}

TEST_CASE("[Modules][FoundryScript] Custom annotation usage records named arguments") {
	FSParser parser;
	const Error error = parser.parse("@cases(provider = \"crit_rows\", count = 3)\nfunc test():\n\tpass\n", "user://test.fs", false);
	REQUIRE(error == OK);

	const FSParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);

	const List<FSParser::AnnotationNode *> *annotations = find_function_annotations(root_class, "test");
	REQUIRE(annotations != nullptr);
	REQUIRE(annotations->size() == 1);

	const FSParser::AnnotationNode *annotation = annotations->front()->get();
	CHECK(annotation->is_custom);
	REQUIRE(annotation->arguments.size() == 2);
	REQUIRE(annotation->argument_names.size() == 2);
	CHECK(annotation->argument_names[0] == StringName("provider"));
	CHECK(annotation->argument_names[1] == StringName("count"));
}

TEST_CASE("[Modules][FoundryScript] Custom annotation usage preserves positional-then-named order") {
	FSParser parser;
	const Error error = parser.parse("@config(1, 2, label = \"x\")\nfunc test():\n\tpass\n", "user://test.fs", false);
	REQUIRE(error == OK);

	const FSParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);

	const List<FSParser::AnnotationNode *> *annotations = find_function_annotations(root_class, "test");
	REQUIRE(annotations != nullptr);
	REQUIRE(annotations->size() == 1);

	const FSParser::AnnotationNode *annotation = annotations->front()->get();
	REQUIRE(annotation->argument_names.size() == 3);
	CHECK(annotation->argument_names[0] == StringName());
	CHECK(annotation->argument_names[1] == StringName());
	CHECK(annotation->argument_names[2] == StringName("label"));
}

TEST_CASE("[Modules][FoundryScript] Repeated custom annotations are preserved in source order") {
	FSParser parser;
	const Error error = parser.parse("@tag(\"a\")\n@tag(\"b\")\nfunc test():\n\tpass\n", "user://test.fs", false);
	REQUIRE(error == OK);

	const FSParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);

	const List<FSParser::AnnotationNode *> *annotations = find_function_annotations(root_class, "test");
	REQUIRE(annotations != nullptr);
	REQUIRE(annotations->size() == 2);
	CHECK(annotations->front()->get()->name == StringName("@tag"));
	CHECK(annotations->back()->get()->name == StringName("@tag"));
}

TEST_CASE("[Modules][FoundryScript] Custom annotations apply to class, method, variable, signal, constant, and parameter targets") {
	FSParser parser;
	const Error error = parser.parse("annotation marker targets CLASS, VARIABLE, CONSTANT, SIGNAL, METHOD, PARAMETER\n@marker\nclass Inner:\n\t@marker\n\tvar value: int = 0\n\t@marker\n\tconst LIMIT = 1\n\t@marker\n\tsignal changed(@marker amount: float)\n\t@marker\n\tfunc method(@marker arg: int):\n\t\tpass\n", "user://test.fs", false);
	REQUIRE(error == OK);
}

TEST_CASE("[Modules][FoundryScript] Doc-comment annotation names remain parse errors") {
	FSParser parser;
	const Error error = parser.parse("@deprecated\nvar value = 1\n", "user://test.fs", false);
	CHECK(error != OK);
}

TEST_CASE("[Modules][FoundryScript] Custom annotations are rejected on unsupported targets") {
	FSParser enum_parser;
	CHECK(enum_parser.parse("@marker\nenum Direction { UP, DOWN }\n", "user://test.fs", false) != OK);

	FSParser declaration_parser;
	CHECK(declaration_parser.parse("@marker\nannotation test targets METHOD\n", "user://test.fs", false) != OK);
}

TEST_CASE("[Modules][FoundryScript] Built-in annotations still reject named arguments") {
	FSParser parser;
	const Error error = parser.parse("@export_enum(names = \"A,B\")\nvar value: int\n", "user://test.fs", false);
	CHECK(error != OK);
}

} // namespace FSTests
