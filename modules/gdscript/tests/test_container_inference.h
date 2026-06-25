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

	const GDScriptParser::ClassNode *tree() const {
		return parser.get_tree();
	}

	const GDScriptParser::VariableNode *member(const StringName &p_name) const {
		const GDScriptParser::ClassNode *root = parser.get_tree();
		if (root == nullptr || !root->has_member(p_name)) {
			return nullptr;
		}
		const GDScriptParser::ClassNode::Member &found = root->get_member(p_name);
		return found.type == GDScriptParser::ClassNode::Member::VARIABLE ? found.variable : nullptr;
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

// Runs dictionary element inference for local `p_var` declared in `p_func` of `p_source`.
static GDScriptContainerInference::Result infer_dict_in(InferenceFixture &p_fixture, const char *p_func, const char *p_var) {
	REQUIRE_MESSAGE(p_fixture.parse_error == OK, "fixture source should parse");
	REQUIRE_MESSAGE(p_fixture.analyze_error == OK, "fixture source should analyze");
	const GDScriptParser::FunctionNode *function = p_fixture.function(p_func);
	REQUIRE(function != nullptr);
	const GDScriptParser::VariableNode *variable = p_fixture.local(function, p_var);
	REQUIRE(variable != nullptr);
	return GDScriptContainerInference::infer_local_dictionary_element_type(variable, function->body);
}

// Runs class-wide array element inference for member `p_var` of `p_source`.
static GDScriptContainerInference::Result infer_member_in(InferenceFixture &p_fixture, const char *p_var) {
	REQUIRE_MESSAGE(p_fixture.parse_error == OK, "fixture source should parse");
	REQUIRE_MESSAGE(p_fixture.analyze_error == OK, "fixture source should analyze");
	const GDScriptParser::VariableNode *variable = p_fixture.member(p_var);
	REQUIRE(variable != nullptr);
	return GDScriptContainerInference::infer_member_array_element_type(variable, p_fixture.tree());
}

// Runs class-wide dictionary element inference for member `p_var` of `p_source`.
static GDScriptContainerInference::Result infer_member_dict_in(InferenceFixture &p_fixture, const char *p_var) {
	REQUIRE_MESSAGE(p_fixture.parse_error == OK, "fixture source should parse");
	REQUIRE_MESSAGE(p_fixture.analyze_error == OK, "fixture source should analyze");
	const GDScriptParser::VariableNode *variable = p_fixture.member(p_var);
	REQUIRE(variable != nullptr);
	return GDScriptContainerInference::infer_member_dictionary_element_type(variable, p_fixture.tree());
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

	TEST_CASE("A compound indexed write forces a conservative skip") {
		// `slots[0] **= -1` stores typeof(int ** int), which can be a float, not
		// the typeof(-1) the value alone suggests, so the element type is unknown.
		InferenceFixture fixture("func f():\n\tvar slots = [2, 3]\n\tslots[0] **= -1\n");
		GDScriptContainerInference::Result result = infer_in(fixture, "f", "slots");
		CHECK_EQ(result.outcome, GDScriptContainerInference::UNPROVABLE);
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

	TEST_CASE("A value-validating read forces a conservative skip") {
		// `[1].has(1.2)` is false, but `Array[int].has(1.2)` coerces 1.2 to 1 and
		// returns true, so typing would change behavior; inference must not.
		InferenceFixture fixture("func f():\n\tvar items = [1]\n\tvar found = items.has(1.2)\n");
		GDScriptContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::UNPROVABLE);
	}

	TEST_CASE("A copy-returning method forces a conservative skip") {
		// `duplicate()` carries the typed flag onto the copy, so an alias of it
		// would reject a later mismatched element after typing.
		InferenceFixture fixture("func f():\n\tvar items = [1]\n\tvar copy = items.duplicate()\n");
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

TEST_SUITE("[Modules][GDScript][ContainerInference][Dictionary]") {
	TEST_CASE("A monomorphic dictionary literal infers its key and value types") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1, \"b\": 2}\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("An empty dictionary grown only by indexed writes infers its types") {
		InferenceFixture fixture("func f():\n\tvar d = {}\n\td[\"a\"] = 1\n\td[\"b\"] = 2\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("set contributes key and value types") {
		InferenceFixture fixture("func f():\n\tvar d = {}\n\td.set(\"a\", 1)\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("get_or_add contributes key and value types") {
		InferenceFixture fixture("func f():\n\tvar d = {}\n\td.get_or_add(\"a\", 1)\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("merge with a typed dictionary literal contributes both element types") {
		InferenceFixture fixture("func f():\n\tvar d = {}\n\td.merge({\"a\": 1})\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("A mixed key type is skipped and reported") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\td[2] = 3\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::MIXED);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("A mixed value type is skipped and reported") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\td[\"b\"] = \"x\"\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::MIXED);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("A compound indexed write forces a conservative skip") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 2}\n\td[\"a\"] **= -1\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::UNPROVABLE);
	}

	TEST_CASE("A value-validating read forces a conservative skip") {
		// `{1: 0}.has(1.2)` is false, but `Dictionary[int, int].has(1.2)` coerces
		// 1.2 to 1 and returns true, so typing would change behavior.
		InferenceFixture fixture("func f():\n\tvar d = {1: 0}\n\tvar found = d.has(1.2)\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::UNPROVABLE);
	}

	TEST_CASE("Observing the dictionary's typedness forces a conservative skip") {
		InferenceFixture fixture("func f():\n\tvar d = {1: 0}\n\tvar typed = d.is_typed()\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::UNPROVABLE);
	}

	TEST_CASE("A copy-returning method forces a conservative skip") {
		InferenceFixture fixture("func f():\n\tvar d = {1: 0}\n\tvar copy = d.duplicate()\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::UNPROVABLE);
	}

	TEST_CASE("An unmodelled value-validating read forces a conservative skip") {
		// `has_all` validates each key against the typed key, so it can change
		// behavior once the dictionary is typed; it is not modeled.
		InferenceFixture fixture("func f():\n\tvar d = {1: 0}\n\tvar all = d.has_all([1.2])\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::UNPROVABLE);
	}

	TEST_CASE("In-place sort and reads do not block inference") {
		// `sort()` reorders entries in place without introducing or validating types.
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\td.sort()\n\tvar n = d.size()\n\tvar v = d[\"a\"]\n\tfor key in d:\n\t\tprint(key)\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("A subscript read with a coercible mismatched key forces a conservative skip") {
		// `{1: "x"}[1.2]` misses on the untyped dictionary but would coerce 1.2 to 1
		// and hit after typing as `Dictionary[int, String]`, so typing changes behavior.
		InferenceFixture fixture("func f():\n\tvar d = {1: \"x\"}\n\tvar v = d[1.2]\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::MIXED);
	}

	TEST_CASE("A subscript read with a matching key type does not block inference") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\tvar v = d[\"a\"]\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("A returned dictionary escapes and is not inferred") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\treturn d\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("Passing the dictionary to a call escapes it") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\tprint(d)\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("Capturing the dictionary in a lambda escapes it") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\tvar fn = func(): d[\"b\"] = 2\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("An unused empty dictionary literal yields no evidence") {
		InferenceFixture fixture("func f():\n\tvar d = {}\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::NO_EVIDENCE);
	}

	TEST_CASE("An empty dictionary with only key evidence yields no evidence") {
		// A bare `erase` would validate the key but contributes nothing storable;
		// without a value type the dictionary cannot be fully typed.
		InferenceFixture fixture("func f():\n\tvar d = {}\n\td[\"a\"] = 1\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("An already-annotated declaration is not applicable") {
		InferenceFixture fixture("func f():\n\tvar d: Dictionary = {\"a\": 1}\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::NOT_APPLICABLE);
	}

	TEST_CASE("A non-literal initializer is not applicable") {
		InferenceFixture fixture("func make():\n\treturn {1: 0}\nfunc f():\n\tvar d = make()\n\td[\"a\"] = 1\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, GDScriptContainerInference::NOT_APPLICABLE);
	}

	TEST_CASE("An array literal is not applicable to dictionary inference") {
		InferenceFixture fixture("func f():\n\tvar items = [1, 2]\n");
		GDScriptContainerInference::Result result = infer_dict_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::NOT_APPLICABLE);
	}
}

TEST_SUITE("[Modules][GDScript][ContainerInference][Member]") {
	TEST_CASE("A class-private member array mutated monomorphically is inferred") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\t_items.append(n)\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("Mutations across several methods are unioned") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func a() -> void:\n"
				"\t_items.append(1)\n"
				"func b() -> void:\n"
				"\t_items.push_back(2)\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("Heterogeneous mutations across methods are reported as mixed") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func a() -> void:\n"
				"\t_items.append(1)\n"
				"func b() -> void:\n"
				"\t_items.append(\"x\")\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::MIXED);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("A self-qualified mutation is inferred") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\tself._items.append(n)\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A public (non-underscore) member is skipped and reported") {
		// Without a leading underscore the member is part of the public API and can
		// be mutated by another script, which the single-file analysis cannot see.
		InferenceFixture fixture(
				"var items = []\n"
				"func add(n: int) -> void:\n"
				"\titems.append(n)\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("A public member dictionary is skipped") {
		InferenceFixture fixture(
				"var by_name = {}\n"
				"func put(key: String, value: int) -> void:\n"
				"\tby_name[key] = value\n");
		GDScriptContainerInference::Result result = infer_member_dict_in(fixture, "by_name");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("An exported member is skipped and reported") {
		InferenceFixture fixture(
				"@export var items = []\n"
				"func add() -> void:\n"
				"\titems.append(1)\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("A member with a custom setter is skipped") {
		InferenceFixture fixture(
				"var items = []:\n"
				"\tset(value):\n"
				"\t\titems = value\n"
				"func add() -> void:\n"
				"\titems.append(1)\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("A static member is skipped") {
		InferenceFixture fixture(
				"static var items = []\n"
				"func add() -> void:\n"
				"\titems.append(1)\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("Passing the member to a call escapes it") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func go() -> void:\n"
				"\t_items.append(1)\n"
				"\tprint(_items)\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("Returning the member escapes it") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func get_items() -> Array:\n"
				"\treturn _items\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("Leaking self escapes the member") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func register(bus: Object) -> void:\n"
				"\tbus.add(self)\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("Accessing the member through another reference escapes it") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func copy_from(other) -> void:\n"
				"\tother._items.append(1)\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("Calling an overridable script method on self escapes the member") {
		// A subclass outside this file can override `notify()` and mutate the
		// inherited `_items` with a different type, so the call is not bounded.
		InferenceFixture fixture(
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\t_items.append(n)\n"
				"\tafter_add()\n"
				"func after_add() -> void:\n"
				"\tpass\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("Calling a native method via self does not block inference") {
		// A native method cannot reach a script-defined member, so it is safe.
		InferenceFixture fixture(
				"extends Node\n"
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\t_items.append(n)\n"
				"\tself.notify_property_list_changed()\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A global utility call does not block inference") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\t_items.append(n)\n"
				"\tprint(n)\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A subclass mutation contributes to the union") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"class Inner:\n"
				"\tfunc touch(host) -> void:\n"
				"\t\thost.add()\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A member dictionary mutated monomorphically is inferred") {
		InferenceFixture fixture(
				"var _by_name = {}\n"
				"func put(key: String, value: int) -> void:\n"
				"\t_by_name[key] = value\n");
		GDScriptContainerInference::Result result = infer_member_dict_in(fixture, "_by_name");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("A self-qualified dictionary set is inferred") {
		InferenceFixture fixture(
				"var _by_name = {}\n"
				"func put() -> void:\n"
				"\tself._by_name.set(\"a\", 1)\n");
		GDScriptContainerInference::Result result = infer_member_dict_in(fixture, "_by_name");
		CHECK_EQ(result.outcome, GDScriptContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("An exported dictionary member is skipped") {
		InferenceFixture fixture(
				"@export var by_name = {}\n"
				"func put() -> void:\n"
				"\tby_name[\"a\"] = 1\n");
		GDScriptContainerInference::Result result = infer_member_dict_in(fixture, "by_name");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("A lambda that uses self escapes the member") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func make_writer() -> Callable:\n"
				"\treturn func(): _items.append(\"x\")\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("Aliasing the member in another member initializer escapes it") {
		InferenceFixture fixture(
				"var _items = []\n"
				"var _alias = _items\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("A dynamic self.set defeats the scan and escapes the member") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak() -> void:\n"
				"\tself.set(\"_items\", [\"x\"])\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("A thread-safe dynamic set escapes the member") {
		InferenceFixture fixture(
				"extends Node\n"
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak() -> void:\n"
				"\tset_thread_safe(\"_items\", [\"x\"])\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("An implicit-self dynamic set escapes the member") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak() -> void:\n"
				"\tset(\"_items\", [\"x\"])\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("A dynamic get that returns the member escapes it") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak() -> void:\n"
				"\tget(\"_items\").append(\"x\")\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("Another member's inline setter mutating the member escapes it") {
		InferenceFixture fixture(
				"var _items = []\n"
				"var trigger = 0:\n"
				"\tset(value):\n"
				"\t\ttrigger = value\n"
				"\t\t_items.append(\"x\")\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::MIXED);
	}

	TEST_CASE("A nested class member with the same name is not counted as evidence") {
		InferenceFixture fixture(
				"var _items = []\n"
				"class Inner:\n"
				"\tvar _items = []\n"
				"\tfunc fill() -> void:\n"
				"\t\t_items.append(1)\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		// The outer member has no writes of its own; the inner same-named member's
		// writes must not be attributed to it.
		CHECK_EQ(result.outcome, GDScriptContainerInference::NO_EVIDENCE);
	}

	TEST_CASE("A lambda body that mutates the member through another reference escapes it") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func make(other) -> Callable:\n"
				"\treturn func(): other._items.append(\"x\")\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("The member's own initializer leaking self escapes it") {
		InferenceFixture fixture(
				"func register(o) -> int:\n"
				"\treturn 0\n"
				"var _items = [register(self)]\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("Aliasing the member through a parameter default escapes it") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func f(alias = _items) -> void:\n"
				"\t_items.append(1)\n"
				"\talias.append(\"x\")\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("Referencing a reflection method as a callable escapes the member") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak() -> void:\n"
				"\tvar setter := set\n"
				"\tsetter.call(\"_items\", [\"x\"])\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("Referencing self.set as a callable escapes the member") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak() -> void:\n"
				"\tvar setter := self.set\n"
				"\tsetter.call(\"_items\", [\"x\"])\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("A member dictionary with an overridable self call escapes") {
		InferenceFixture fixture(
				"var _by_name = {}\n"
				"func put(key: String, value: int) -> void:\n"
				"\t_by_name[key] = value\n"
				"\tafter_put()\n"
				"func after_put() -> void:\n"
				"\tpass\n");
		GDScriptContainerInference::Result result = infer_member_dict_in(fixture, "_by_name");
		CHECK_EQ(result.outcome, GDScriptContainerInference::ESCAPES);
	}

	TEST_CASE("An unused member array yields no evidence") {
		InferenceFixture fixture("var _items = []\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, GDScriptContainerInference::NO_EVIDENCE);
	}

	TEST_CASE("An already-annotated member is not applicable") {
		InferenceFixture fixture("var items: Array = []\n");
		GDScriptContainerInference::Result result = infer_member_in(fixture, "items");
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

	TEST_CASE("Candidate collection upgrades a provable local dictionary") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/container_infer_dict.gd";
		const String source =
				"func f():\n"
				"\tvar d = {}\n"
				"\td[\"a\"] = 1\n";
		TemporaryScriptFile file(path, source);

		RefactorContext context;
		context.path = path;
		context.source = source;
		RefactorCandidatesResult result = GDScriptRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *d = inference_candidate_at_line(result, 1);
		REQUIRE(d != nullptr);
		CHECK(d->enabled);
		REQUIRE_FALSE(d->edits.is_empty());
		CHECK_EQ(d->edits[0].new_text, ": Dictionary[String, int] = ");

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Candidate collection keeps a mixed local dictionary as bare Dictionary") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/container_mixed_dict.gd";
		const String source =
				"func f():\n"
				"\tvar d = {\"a\": 1, 2: \"b\"}\n";
		TemporaryScriptFile file(path, source);

		RefactorContext context;
		context.path = path;
		context.source = source;
		RefactorCandidatesResult result = GDScriptRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *d = inference_candidate_at_line(result, 1);
		REQUIRE(d != nullptr);
		CHECK(d->enabled);
		REQUIRE_FALSE(d->edits.is_empty());
		CHECK_EQ(d->edits[0].new_text, ": Dictionary = ");

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Candidate collection upgrades a provable member array to Array[int]") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/container_member_array.gd";
		const String source =
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\t_items.append(n)\n";
		TemporaryScriptFile file(path, source);

		RefactorContext context;
		context.path = path;
		context.source = source;
		RefactorCandidatesResult result = GDScriptRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *items = inference_candidate_at_line(result, 0);
		REQUIRE(items != nullptr);
		CHECK(items->enabled);
		REQUIRE_FALSE(items->edits.is_empty());
		CHECK_EQ(items->edits[0].new_text, ": Array[int] = ");

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Candidate collection keeps an escaping member array as bare Array") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/container_member_escape.gd";
		const String source =
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\t_items.append(n)\n"
				"func get_items() -> Array:\n"
				"\treturn _items\n";
		TemporaryScriptFile file(path, source);

		RefactorContext context;
		context.path = path;
		context.source = source;
		RefactorCandidatesResult result = GDScriptRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *items = inference_candidate_at_line(result, 0);
		REQUIRE(items != nullptr);
		CHECK(items->enabled);
		REQUIRE_FALSE(items->edits.is_empty());
		CHECK_EQ(items->edits[0].new_text, ": Array = ");

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Candidate collection upgrades a provable member dictionary") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/container_member_dict.gd";
		const String source =
				"var _by_name = {}\n"
				"func put(key: String, value: int) -> void:\n"
				"\t_by_name[key] = value\n";
		TemporaryScriptFile file(path, source);

		RefactorContext context;
		context.path = path;
		context.source = source;
		RefactorCandidatesResult result = GDScriptRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *d = inference_candidate_at_line(result, 0);
		REQUIRE(d != nullptr);
		CHECK(d->enabled);
		REQUIRE_FALSE(d->edits.is_empty());
		CHECK_EQ(d->edits[0].new_text, ": Dictionary[String, int] = ");

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}
}

#endif // GDSCRIPT_NO_LSP

} // namespace GDScriptTests

#endif // TOOLS_ENABLED
