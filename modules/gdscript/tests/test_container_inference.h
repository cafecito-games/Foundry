/**************************************************************************/
/*  test_container_inference.h                                            */
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

#ifdef TOOLS_ENABLED

#include "../editor/gdscript_container_inference.h"
#include "../editor/gdscript_refactoring.h"
#include "../gdscript_analyzer.h"
#include "../gdscript_parser.h"

#include "tests/test_macros.h"

#include "test_refactor.h" // GDScriptTests::TemporaryScriptFile, make_context.

#ifndef GDSCRIPT_NO_LSP
#include "editor/file_system/editor_file_system.h"
#endif // GDSCRIPT_NO_LSP

namespace GDScriptTests {

// Holds a parsed-and-analyzed script so inference can read resolved usage types.
class InferenceFixture {
public:
	GDScriptParser parser;
	GDScriptAnalyzer analyzer;

	explicit InferenceFixture(const String &p_source) :
			analyzer(&parser) {
		parse_error = parser.parse(p_source, "user://container_inference_test.gd", false);
		if (parse_error == OK) {
			analyze_error = analyzer.analyze();
		}
	}

	Error parse_error = ERR_PARSE_ERROR;
	Error analyze_error = ERR_PARSE_ERROR;

	const GDScriptParser::FunctionNode *function(const StringName &p_name) const {
		const GDScriptParser::ClassNode *tree = parser.get_tree();
		if (tree == nullptr || !tree->has_function(p_name)) {
			return nullptr;
		}
		return tree->get_member(p_name).function;
	}

	const GDScriptParser::VariableNode *local(const GDScriptParser::FunctionNode *p_function, const StringName &p_name) const {
		return find_local_recursive(p_function != nullptr ? p_function->body : nullptr, p_name);
	}

private:
	static const GDScriptParser::VariableNode *find_local_recursive(const GDScriptParser::SuiteNode *p_suite, const StringName &p_name) {
		if (p_suite == nullptr) {
			return nullptr;
		}
		for (const GDScriptParser::Node *statement : p_suite->statements) {
			if (statement == nullptr) {
				continue;
			}
			if (statement->type == GDScriptParser::Node::VARIABLE) {
				const GDScriptParser::VariableNode *variable = static_cast<const GDScriptParser::VariableNode *>(statement);
				if (variable->identifier != nullptr && variable->identifier->name == p_name) {
					return variable;
				}
			}
			const GDScriptParser::SuiteNode *nested = nullptr;
			switch (statement->type) {
				case GDScriptParser::Node::IF: {
					const GDScriptParser::IfNode *if_node = static_cast<const GDScriptParser::IfNode *>(statement);
					if (const GDScriptParser::VariableNode *found = find_local_recursive(if_node->true_block, p_name)) {
						return found;
					}
					nested = if_node->false_block;
				} break;
				case GDScriptParser::Node::FOR:
					nested = static_cast<const GDScriptParser::ForNode *>(statement)->loop;
					break;
				case GDScriptParser::Node::WHILE:
					nested = static_cast<const GDScriptParser::WhileNode *>(statement)->loop;
					break;
				default:
					break;
			}
			if (const GDScriptParser::VariableNode *found = find_local_recursive(nested, p_name)) {
				return found;
			}
		}
		return nullptr;
	}
};

// Runs element inference for local `p_var` declared in `p_func` of `p_source`.
static GDScriptContainerInference::Result infer_in(InferenceFixture &p_fixture, const char *p_func, const char *p_var) {
	REQUIRE_MESSAGE(p_fixture.parse_error == OK, "fixture source should parse");
	REQUIRE_MESSAGE(p_fixture.analyze_error == OK, "fixture source should analyze");
	const GDScriptParser::FunctionNode *function = p_fixture.function(p_func);
	REQUIRE(function != nullptr);
	const GDScriptParser::VariableNode *variable = p_fixture.local(function, p_var);
	REQUIRE(variable != nullptr);
	return GDScriptContainerInference::infer_local_array_element_type(variable, function->body);
}

TEST_SUITE("[Modules][GDScript][ContainerInference]") {
	TEST_CASE("A monomorphic literal infers its element type") {
		InferenceFixture fixture("func f():\n\tvar nums = [1, 2, 3]\n");
		GDScriptContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A string literal infers Array[String]") {
		InferenceFixture fixture("func f():\n\tvar names = [\"a\", \"b\"]\n");
		GDScriptContainerInference::Result result = infer_in(fixture, "f", "names");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[String]");
	}

	TEST_CASE("An empty literal grown only by append infers its element type") {
		InferenceFixture fixture("func f():\n\tvar nums = []\n\tnums.append(1)\n\tnums.push_back(2)\n");
		GDScriptContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("Usage inside a nested block is still considered") {
		InferenceFixture fixture("func f(flag):\n\tvar nums = []\n\tif flag:\n\t\tnums.append(7)\n");
		GDScriptContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("An indexed write contributes its element type") {
		InferenceFixture fixture("func f():\n\tvar slots = [0, 0]\n\tslots[0] = 5\n");
		GDScriptContainerInference::Result result = infer_in(fixture, "f", "slots");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("append_array merges the element type of the other array literal") {
		InferenceFixture fixture("func f():\n\tvar nums = []\n\tnums.append_array([1, 2])\n");
		GDScriptContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("Read-only methods and reads do not block inference") {
		InferenceFixture fixture("func f():\n\tvar nums = [1, 2]\n\tnums.sort()\n\tvar total = nums.size()\n\tfor value in nums:\n\t\tprint(value)\n");
		GDScriptContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("Mixed element types are skipped and reported") {
		InferenceFixture fixture("func f():\n\tvar items = [1, \"a\"]\n");
		GDScriptContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::MIXED);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("Mixing int and float via append is reported as mixed") {
		InferenceFixture fixture("func f():\n\tvar xs = [1]\n\txs.append(2.0)\n");
		GDScriptContainerInference::Result result = infer_in(fixture, "f", "xs");
		CHECK_EQ(result.outcome, GDScriptContainerInference::MIXED);
	}

	TEST_CASE("A returned variable escapes and is not inferred") {
		InferenceFixture fixture("func f():\n\tvar items = [1]\n\treturn items\n");
		GDScriptContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("Passing the variable to a call escapes it") {
		InferenceFixture fixture("func f():\n\tvar items = [1]\n\tprint(items)\n");
		GDScriptContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("Aliasing the variable to another local escapes it") {
		InferenceFixture fixture("func f():\n\tvar items = [1]\n\tvar alias = items\n\talias.append(\"s\")\n");
		GDScriptContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("Capturing the variable in a lambda escapes it") {
		InferenceFixture fixture("func f():\n\tvar items = [1]\n\tvar fn = func(): items.append(2)\n");
		GDScriptContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("An unmodelled mutating method forces a conservative skip") {
		InferenceFixture fixture("func f():\n\tvar items = [1]\n\titems.resize(4)\n");
		GDScriptContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::UNPROVABLE);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("Observing the array's typedness forces a conservative skip") {
		// `is_typed()` returns false on the bare array but would return true once
		// typed, so upgrading would change behavior; inference must not.
		InferenceFixture fixture("func f():\n\tvar items = [1]\n\tvar typed = items.is_typed()\n");
		GDScriptContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::UNPROVABLE);
	}

	TEST_CASE("An unused empty literal yields no evidence") {
		InferenceFixture fixture("func f():\n\tvar items = []\n");
		GDScriptContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::NO_EVIDENCE);
	}

	TEST_CASE("An already-annotated declaration is not applicable") {
		InferenceFixture fixture("func f():\n\tvar items: Array = [1]\n");
		GDScriptContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::NOT_APPLICABLE);
	}

	TEST_CASE("A non-literal initializer is not applicable") {
		InferenceFixture fixture("func make():\n\treturn [1]\nfunc f():\n\tvar items = make()\n\titems.append(2)\n");
		GDScriptContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::NOT_APPLICABLE);
	}
}

#ifndef GDSCRIPT_NO_LSP

// Locates the Add Type Annotation candidate anchored at p_line.
static const RefactorCandidate *inference_candidate_at_line(const RefactorCandidatesResult &p_result, int p_line) {
	for (const RefactorCandidate &candidate : p_result.candidates) {
		if (candidate.line == p_line) {
			return &candidate;
		}
	}
	return nullptr;
}

TEST_SUITE("[Modules][GDScript][ContainerInference]") {
	TEST_CASE("Candidate collection upgrades a provable local array to Array[int]") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/container_infer.gd";
		const String source =
				"func f():\n"
				"\tvar nums = []\n"
				"\tnums.append(1)\n";
		TemporaryScriptFile file(path, source);

		RefactorContext context;
		context.path = path;
		context.source = source;
		RefactorCandidatesResult result = GDScriptRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *nums = inference_candidate_at_line(result, 1);
		REQUIRE(nums != nullptr);
		CHECK(nums->enabled);
		REQUIRE_FALSE(nums->edits.is_empty());
		CHECK_EQ(nums->edits[0].new_text, ": Array[int] = ");

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Candidate collection keeps a mixed local array as bare Array") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/container_mixed.gd";
		const String source =
				"func f():\n"
				"\tvar items = [1, \"a\"]\n";
		TemporaryScriptFile file(path, source);

		RefactorContext context;
		context.path = path;
		context.source = source;
		RefactorCandidatesResult result = GDScriptRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *items = inference_candidate_at_line(result, 1);
		REQUIRE(items != nullptr);
		CHECK(items->enabled);
		REQUIRE_FALSE(items->edits.is_empty());
		CHECK_EQ(items->edits[0].new_text, ": Array = ");

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}
}

#endif // GDSCRIPT_NO_LSP

} // namespace GDScriptTests

#endif // TOOLS_ENABLED
