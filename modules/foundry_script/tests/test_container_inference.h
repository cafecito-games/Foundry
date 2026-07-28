/**************************************************************************/
/*  test_container_inference.h                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                              GODOT ENGINE                              */
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

#include "../editor/fs_container_inference.h"
#include "../editor/fs_refactoring.h"
#include "../fs_analyzer.h"
#include "../fs_parser.h"

#include "tests/test_macros.h"

#include "test_refactor.h" // FSTests::TemporaryScriptFile, make_context.

#ifndef FOUNDRY_SCRIPT_NO_LSP
#include "editor/file_system/editor_file_system.h"
#endif // FOUNDRY_SCRIPT_NO_LSP

namespace FSTests {

// Holds a parsed-and-analyzed script so inference can read resolved usage types.
class InferenceFixture {
public:
	FSParser parser;
	FSAnalyzer analyzer;

	explicit InferenceFixture(const String &p_source) :
			analyzer(&parser) {
		parse_error = parser.parse(p_source, "user://container_inference_test.fs", false);
		if (parse_error == OK) {
			analyze_error = analyzer.analyze();
		}
	}

	Error parse_error = ERR_PARSE_ERROR;
	Error analyze_error = ERR_PARSE_ERROR;

	const FSParser::FunctionNode *function(const StringName &p_name) const {
		const FSParser::ClassNode *tree = parser.get_tree();
		if (tree == nullptr || !tree->has_function(p_name)) {
			return nullptr;
		}
		return tree->get_member(p_name).function;
	}

	const FSParser::VariableNode *local(const FSParser::FunctionNode *p_function, const StringName &p_name) const {
		return find_local_recursive(p_function != nullptr ? p_function->body : nullptr, p_name);
	}

	const FSParser::ClassNode *tree() const {
		return parser.get_tree();
	}

	const FSParser::VariableNode *member(const StringName &p_name) const {
		const FSParser::ClassNode *class_root = parser.get_tree();
		if (class_root == nullptr || !class_root->has_member(p_name)) {
			return nullptr;
		}
		const FSParser::ClassNode::Member &found = class_root->get_member(p_name);
		return found.type == FSParser::ClassNode::Member::VARIABLE ? found.variable : nullptr;
	}

private:
	static const FSParser::VariableNode *find_local_recursive(const FSParser::SuiteNode *p_suite, const StringName &p_name) {
		if (p_suite == nullptr) {
			return nullptr;
		}
		for (const FSParser::Node *statement : p_suite->statements) {
			if (statement == nullptr) {
				continue;
			}
			if (statement->type == FSParser::Node::VARIABLE) {
				const FSParser::VariableNode *variable = static_cast<const FSParser::VariableNode *>(statement);
				if (variable->identifier != nullptr && variable->identifier->name == p_name) {
					return variable;
				}
			}
			const FSParser::SuiteNode *nested = nullptr;
			switch (statement->type) {
				case FSParser::Node::IF: {
					const FSParser::IfNode *if_node = static_cast<const FSParser::IfNode *>(statement);
					if (const FSParser::VariableNode *found = find_local_recursive(if_node->true_block, p_name)) {
						return found;
					}
					nested = if_node->false_block;
				} break;
				case FSParser::Node::FOR:
					nested = static_cast<const FSParser::ForNode *>(statement)->loop;
					break;
				case FSParser::Node::WHILE:
					nested = static_cast<const FSParser::WhileNode *>(statement)->loop;
					break;
				default:
					break;
			}
			if (const FSParser::VariableNode *found = find_local_recursive(nested, p_name)) {
				return found;
			}
		}
		return nullptr;
	}
};

// Runs element inference for local `p_var` declared in `p_func` of `p_source`.
static FSContainerInference::Result infer_in(InferenceFixture &p_fixture, const char *p_func, const char *p_var) {
	REQUIRE_MESSAGE(p_fixture.parse_error == OK, "fixture source should parse");
	REQUIRE_MESSAGE(p_fixture.analyze_error == OK, "fixture source should analyze");
	const FSParser::FunctionNode *function = p_fixture.function(p_func);
	REQUIRE(function != nullptr);
	const FSParser::VariableNode *variable = p_fixture.local(function, p_var);
	REQUIRE(variable != nullptr);
	return FSContainerInference::infer_local_array_element_type(variable, function->body);
}

// Runs dictionary element inference for local `p_var` declared in `p_func` of `p_source`.
static FSContainerInference::Result infer_dict_in(InferenceFixture &p_fixture, const char *p_func, const char *p_var) {
	REQUIRE_MESSAGE(p_fixture.parse_error == OK, "fixture source should parse");
	REQUIRE_MESSAGE(p_fixture.analyze_error == OK, "fixture source should analyze");
	const FSParser::FunctionNode *function = p_fixture.function(p_func);
	REQUIRE(function != nullptr);
	const FSParser::VariableNode *variable = p_fixture.local(function, p_var);
	REQUIRE(variable != nullptr);
	return FSContainerInference::infer_local_dictionary_element_type(variable, function->body);
}

// Runs class-wide array element inference for member `p_var` of `p_source`.
static FSContainerInference::Result infer_member_in(InferenceFixture &p_fixture, const char *p_var) {
	REQUIRE_MESSAGE(p_fixture.parse_error == OK, "fixture source should parse");
	REQUIRE_MESSAGE(p_fixture.analyze_error == OK, "fixture source should analyze");
	const FSParser::VariableNode *variable = p_fixture.member(p_var);
	REQUIRE(variable != nullptr);
	return FSContainerInference::infer_member_array_element_type(variable, p_fixture.tree());
}

// Runs class-wide dictionary element inference for member `p_var` of `p_source`.
static FSContainerInference::Result infer_member_dict_in(InferenceFixture &p_fixture, const char *p_var) {
	REQUIRE_MESSAGE(p_fixture.parse_error == OK, "fixture source should parse");
	REQUIRE_MESSAGE(p_fixture.analyze_error == OK, "fixture source should analyze");
	const FSParser::VariableNode *variable = p_fixture.member(p_var);
	REQUIRE(variable != nullptr);
	return FSContainerInference::infer_member_dictionary_element_type(variable, p_fixture.tree());
}

TEST_SUITE("[Modules][FoundryScript][ContainerInference]") {
	TEST_CASE("A monomorphic literal infers its element type") {
		InferenceFixture fixture("func f():\n\tvar nums = [1, 2, 3]\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A string literal infers Array[String]") {
		InferenceFixture fixture("func f():\n\tvar names = [\"a\", \"b\"]\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "names");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[String]");
	}

	TEST_CASE("An empty literal grown only by append infers its element type") {
		InferenceFixture fixture("func f():\n\tvar nums = []\n\tnums.append(1)\n\tnums.push_back(2)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("Usage inside a nested block is still considered") {
		InferenceFixture fixture("func f(flag):\n\tvar nums = []\n\tif flag:\n\t\tnums.append(7)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("An indexed write contributes its element type") {
		InferenceFixture fixture("func f():\n\tvar slots = [0, 0]\n\tslots[0] = 5\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "slots");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A type-preserving compound indexed write contributes its element type") {
		// `total[0] += 5` stores typeof(int + int) == int, the same as the existing
		// element type, so the operation is behavior-preserving once typed.
		InferenceFixture fixture("func f():\n\tvar total = [2, 3]\n\ttotal[0] += 5\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "total");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A widening compound indexed write forces a conservative skip") {
		// `slots[0] *= 1.5` stores typeof(int * float) == float, which is not the
		// int element type, so typing as Array[int] would change the stored type.
		InferenceFixture fixture("func f():\n\tvar slots = [2, 3]\n\tslots[0] *= 1.5\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "slots");
		CHECK_EQ(result.outcome, FSContainerInference::MIXED);
	}

	TEST_CASE("A compound indexed write on an empty array yields no evidence") {
		// With no stored element the array is never typed, so the compound write runs
		// on a Variant element exactly as before; there is nothing to validate.
		InferenceFixture fixture("func f():\n\tvar slots = []\n\tslots[0] += 1\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "slots");
		CHECK_EQ(result.outcome, FSContainerInference::NO_EVIDENCE);
	}

	TEST_CASE("append_array merges the element type of the other array literal") {
		InferenceFixture fixture("func f():\n\tvar nums = []\n\tnums.append_array([1, 2])\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("Read-only methods and reads do not block inference") {
		InferenceFixture fixture("func f():\n\tvar nums = [1, 2]\n\tnums.sort()\n\tvar total = nums.size()\n\tfor value in nums:\n\t\tprint(value)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("Mixed element types are skipped and reported") {
		InferenceFixture fixture("func f():\n\tvar items = [1, \"a\"]\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, FSContainerInference::MIXED);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("Mixing int and float via append is reported as mixed") {
		InferenceFixture fixture("func f():\n\tvar xs = [1]\n\txs.append(2.0)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "xs");
		CHECK_EQ(result.outcome, FSContainerInference::MIXED);
	}

	TEST_CASE("A returned variable escapes and is not inferred") {
		InferenceFixture fixture("func f():\n\tvar items = [1]\n\treturn items\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("Passing the variable to a call escapes it") {
		InferenceFixture fixture("func f():\n\tvar items = [1]\n\tprint(items)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Aliasing the variable to another local escapes it") {
		InferenceFixture fixture("func f():\n\tvar items = [1]\n\tvar alias = items\n\talias.append(\"s\")\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Capturing the variable in a lambda escapes it") {
		InferenceFixture fixture("func f():\n\tvar items = [1]\n\tvar fn = func(): items.append(2)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Referencing the array inside a destructuring initializer escapes it") {
		InferenceFixture fixture("func f():\n\tvar items = [1]\n\tvar (a, b) = (items, 2)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("A bare array reference in a discarded destructured slot does not escape it") {
		// The value is evaluated and immediately dropped via `_`; no binding
		// retains a reference to it, so this is as safe as a bare read.
		InferenceFixture fixture("func f():\n\tvar items = [1]\n\titems.append(2)\n\tvar (_, b) = (items, 2)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A mutating call in a discarded destructured slot still forces a conservative skip") {
		// Unlike a bare reference, this element performs an unmodelled mutation
		// that still runs even though its result is discarded via `_`.
		InferenceFixture fixture("func f():\n\tvar items = [1]\n\tvar (_, b) = (items.resize(4), 2)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, FSContainerInference::UNPROVABLE);
	}

	TEST_CASE("A non-literal destructuring initializer still escapes the array") {
		InferenceFixture fixture("func f():\n\tvar items = [1]\n\tvar (a, b) = (items, 2) if true else (0, 3)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("An unmodelled mutating method forces a conservative skip") {
		InferenceFixture fixture("func f():\n\tvar items = [1]\n\titems.resize(4)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, FSContainerInference::UNPROVABLE);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("Observing the array's typedness forces a conservative skip") {
		// `is_typed()` returns false on the bare array but would return true once
		// typed, so upgrading would change behavior; inference must not.
		InferenceFixture fixture("func f():\n\tvar items = [1]\n\tvar typed = items.is_typed()\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, FSContainerInference::UNPROVABLE);
	}

	TEST_CASE("A value-validating read with a coercible argument forces a conservative skip") {
		// `[1].has(1.2)` is false, but `Array[int].has(1.2)` coerces 1.2 to 1 and
		// returns true, so typing would change behavior; inference must not.
		InferenceFixture fixture("func f():\n\tvar items = [1]\n\tvar found = items.has(1.2)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, FSContainerInference::UNPROVABLE);
	}

	TEST_CASE("A value-validating read whose argument matches the element type does not block inference") {
		// `Array[int].has(2)` coerces 2 to 2 (identity), so the read behaves
		// identically before and after typing.
		InferenceFixture fixture("func f():\n\tvar items = [1]\n\tvar found = items.has(2)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("find and erase with a matching argument do not block inference") {
		InferenceFixture fixture("func f():\n\tvar items = [1, 2]\n\tvar i = items.find(2)\n\titems.erase(1)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A value-validating read on an empty array yields no evidence") {
		// With no stored element the array is never typed, so the read coerces against
		// no element type; there is nothing to validate and nothing to infer.
		InferenceFixture fixture("func f():\n\tvar items = []\n\tvar found = items.has(1)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, FSContainerInference::NO_EVIDENCE);
	}

	TEST_CASE("A value-validating read validated before the element is stored still skips on mismatch") {
		// Flow-insensitive: the float argument must be validated against the int
		// element even though the read textually precedes the append.
		InferenceFixture fixture("func f():\n\tvar items = []\n\tvar found = items.has(1.2)\n\titems.append(1)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, FSContainerInference::UNPROVABLE);
	}

	TEST_CASE("A copy-returning method forces a conservative skip") {
		// `duplicate()` carries the typed flag onto the copy, so an alias of it
		// would reject a later mismatched element after typing.
		InferenceFixture fixture("func f():\n\tvar items = [1]\n\tvar copy = items.duplicate()\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, FSContainerInference::UNPROVABLE);
	}

	TEST_CASE("An unused empty literal yields no evidence") {
		InferenceFixture fixture("func f():\n\tvar items = []\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, FSContainerInference::NO_EVIDENCE);
	}

	TEST_CASE("An already-annotated declaration is not applicable") {
		InferenceFixture fixture("func f():\n\tvar items: Array = [1]\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, FSContainerInference::NOT_APPLICABLE);
	}

	TEST_CASE("A non-literal initializer is not applicable") {
		InferenceFixture fixture("func make():\n\treturn [1]\nfunc f():\n\tvar items = make()\n\titems.append(2)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, FSContainerInference::NOT_APPLICABLE);
	}

	TEST_CASE("A loop variable reassigned to an incompatible type forces a conservative skip") {
		// `for x in nums:` binds `x` as `Variant` while `nums` is bare, so `x = "y"` is
		// valid. Typing `nums` as `Array[int]` narrows `x` to a hard `int`, which would
		// reject the later `String` reassignment. Bail rather than break it.
		InferenceFixture fixture("func f():\n\tvar nums = [1, 2]\n\tfor x in nums:\n\t\tx = \"y\"\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("A compound reassignment of a loop variable forces a conservative skip") {
		// `x += "y"` is valid while `x` is `Variant`; after `nums` becomes `Array[int]`,
		// `x` is `int` and the compound operand is rejected.
		InferenceFixture fixture("func f():\n\tvar nums = [1, 2]\n\tfor x in nums:\n\t\tx += \"y\"\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
	}

	TEST_CASE("A loop variable reassigned inside a captured lambda forces a conservative skip") {
		InferenceFixture fixture("func f():\n\tvar nums = [1, 2]\n\tfor x in nums:\n\t\tvar cb = func(): x = \"y\"\n\t\tcb.call()\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
	}

	TEST_CASE("A loop variable reassigned to the element type still infers") {
		// The reassignment stores another `int`, so narrowing the loop variable to
		// `int` is behavior-preserving.
		InferenceFixture fixture("func f():\n\tvar nums = [1, 2]\n\tfor x in nums:\n\t\tx = 9\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("An unreassigned loop variable does not block inference") {
		InferenceFixture fixture("func f():\n\tvar nums = [1, 2]\n\tfor x in nums:\n\t\tprint(x)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A plain (soft) read binding reassigned incompatibly does not block inference") {
		// A plain `var v =` local is soft-typed: an incompatible reassignment downgrades
		// it to `Variant` rather than erroring, so narrowing the read never breaks it.
		InferenceFixture fixture("func f():\n\tvar nums = [1, 2]\n\tvar v = nums[0]\n\tv = \"x\"\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("An explicitly typed loop variable is not narrowed and does not block inference") {
		InferenceFixture fixture("func f():\n\tvar nums = [1, 2]\n\tfor x: Variant in nums:\n\t\tx = \"y\"\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A read binding passed to an incompatible typed parameter forces a conservative skip") {
		// `var v = nums[0]` is `Variant` while `nums` is bare, so `take_string(v)` coerces.
		// Typing `nums` as `Array[int]` narrows `v` to `int`, which the `String` parameter rejects.
		InferenceFixture fixture("func take_string(s: String) -> void:\n\tpass\nfunc f():\n\tvar nums = [1]\n\tvar v = nums[0]\n\ttake_string(v)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("A loop variable passed to an incompatible typed parameter forces a conservative skip") {
		InferenceFixture fixture("func take_string(s: String) -> void:\n\tpass\nfunc f():\n\tvar nums = [1]\n\tfor x in nums:\n\t\ttake_string(x)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
	}

	TEST_CASE("A read binding passed to a matching typed parameter still infers") {
		InferenceFixture fixture("func take_int(n: int) -> void:\n\tpass\nfunc f():\n\tvar nums = [1]\n\tvar v = nums[0]\n\ttake_int(v)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A read binding passed to an untyped parameter does not block inference") {
		InferenceFixture fixture("func take_any(a):\n\tpass\nfunc f():\n\tvar nums = [1]\n\tvar v = nums[0]\n\ttake_any(v)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A read binding returned from an incompatible typed function forces a conservative skip") {
		InferenceFixture fixture("func f() -> String:\n\tvar nums = [1]\n\tvar v = nums[0]\n\treturn v\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
	}

	TEST_CASE("A loop variable returned from an incompatible typed function forces a conservative skip") {
		InferenceFixture fixture("func f() -> String:\n\tvar nums = [1]\n\tfor x in nums:\n\t\treturn x\n\treturn \"\"\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
	}

	TEST_CASE("A read binding returned from a matching typed function still infers") {
		InferenceFixture fixture("func f() -> int:\n\tvar nums = [1]\n\tvar v = nums[0]\n\treturn v\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A read binding returned from an untyped function does not block inference") {
		InferenceFixture fixture("func f():\n\tvar nums = [1]\n\tvar v = nums[0]\n\treturn v\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A read binding assigned to an incompatible typed target forces a conservative skip") {
		InferenceFixture fixture("func f():\n\tvar nums = [1]\n\tvar v = nums[0]\n\tvar s: String = \"\"\n\ts = v\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
	}

	TEST_CASE("A read binding used to initialize an incompatible typed local forces a conservative skip") {
		InferenceFixture fixture("func f():\n\tvar nums = [1]\n\tvar v = nums[0]\n\tvar s: String = v\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
	}

	TEST_CASE("A read binding assigned to a matching typed target still infers") {
		InferenceFixture fixture("func f():\n\tvar nums = [1]\n\tvar v = nums[0]\n\tvar n: int = 0\n\tn = v\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("An element-accessor read binding passed to an incompatible typed parameter forces a conservative skip") {
		// `nums.get(0)` returns the element type once typed, just like `nums[0]`.
		InferenceFixture fixture("func take_string(s: String) -> void:\n\tpass\nfunc f():\n\tvar nums = [1]\n\tvar v = nums.get(0)\n\ttake_string(v)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
	}

	TEST_CASE("A hard `:=` accessor binding reassigned incompatibly forces a conservative skip") {
		// `var v := nums.get(0)` is a hard `Variant` over a bare array, so `v = "x"` is
		// valid; typing `nums` as `Array[int]` specializes `get` to `int`, which the
		// reassignment then rejects. Unlike a plain `=`, a `:=` local does not downgrade.
		InferenceFixture fixture("func f():\n\tvar nums = [1]\n\tvar v := nums.get(0)\n\tv = \"x\"\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
	}

	TEST_CASE("A read binding assigned to a weak (unannotated) local does not block inference") {
		// `var s = ""` is a weak local: an incompatible store downgrades it to `Variant`
		// rather than erroring, so narrowing the read never breaks `s = v`.
		InferenceFixture fixture("func f():\n\tvar nums = [1]\n\tvar v = nums[0]\n\tvar s = \"\"\n\ts = v\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A `max()` read binding used in a typed position does not block inference") {
		// `max()` keeps a `Variant` return even on a typed array, so binding it never
		// narrows; passing it to a `String` parameter stays valid after typing.
		InferenceFixture fixture("func take_string(s: String) -> void:\n\tpass\nfunc f():\n\tvar nums = [1]\n\tvar v = nums.max()\n\ttake_string(v)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A direct element read passed to an incompatible typed parameter forces a conservative skip") {
		// `take_string(nums[0])` passes the read directly without an intermediate local;
		// it narrows from `Variant` to `int` once `nums` is typed, breaking the `String`
		// parameter.
		InferenceFixture fixture("func take_string(s: String) -> void:\n\tpass\nfunc f():\n\tvar nums = [1]\n\ttake_string(nums[0])\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
	}

	TEST_CASE("A direct accessor read returned from an incompatible typed function forces a conservative skip") {
		InferenceFixture fixture("func f() -> String:\n\tvar nums = [1]\n\treturn nums.pop_back()\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
	}

	TEST_CASE("A direct element read initializing an incompatible typed local forces a conservative skip") {
		InferenceFixture fixture("func f():\n\tvar nums = [1]\n\tvar s: String = nums[0]\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
	}

	TEST_CASE("A direct element read used in a matching typed position still infers") {
		InferenceFixture fixture("func take_int(n: int) -> void:\n\tpass\nfunc f():\n\tvar nums = [1]\n\ttake_int(nums[0])\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A read used in an explicit `Variant` position does not block inference") {
		// An explicit `Variant` parameter is a hard type but still accepts every value,
		// so narrowing the read from `Variant` to `int` cannot break it.
		InferenceFixture fixture("func take_variant(v: Variant) -> void:\n\tpass\nfunc f():\n\tvar nums = [1]\n\tvar v = nums[0]\n\ttake_variant(v)\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A direct read returned from a `Variant` function does not block inference") {
		InferenceFixture fixture("func f() -> Variant:\n\tvar nums = [1]\n\treturn nums[0]\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A read initializing an explicit `Variant` local does not block inference") {
		InferenceFixture fixture("func f():\n\tvar nums = [1]\n\tvar v: Variant = nums[0]\n");
		FSContainerInference::Result result = infer_in(fixture, "f", "nums");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}
}

TEST_SUITE("[Modules][FoundryScript][ContainerInference][Dictionary]") {
	TEST_CASE("A monomorphic dictionary literal infers its key and value types") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1, \"b\": 2}\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("An empty dictionary grown only by indexed writes infers its types") {
		InferenceFixture fixture("func f():\n\tvar d = {}\n\td[\"a\"] = 1\n\td[\"b\"] = 2\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("set contributes key and value types") {
		InferenceFixture fixture("func f():\n\tvar d = {}\n\td.set(\"a\", 1)\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("get_or_add contributes key and value types") {
		InferenceFixture fixture("func f():\n\tvar d = {}\n\td.get_or_add(\"a\", 1)\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("merge with a typed dictionary literal contributes both element types") {
		InferenceFixture fixture("func f():\n\tvar d = {}\n\td.merge({\"a\": 1})\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("A mixed key type is skipped and reported") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\td[2] = 3\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::MIXED);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("A mixed value type is skipped and reported") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\td[\"b\"] = \"x\"\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::MIXED);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("A type-preserving compound indexed write contributes its value type") {
		// `d["a"] += 5` stores typeof(int + int) == int, matching the existing value
		// type, so the operation is behavior-preserving once typed.
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 2}\n\td[\"a\"] += 5\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("A widening compound indexed write forces a conservative skip") {
		// `d["a"] *= 1.5` stores typeof(int * float) == float, not the int value
		// type, so typing as Dictionary[String, int] would change the stored type.
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 2}\n\td[\"a\"] *= 1.5\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::MIXED);
	}

	TEST_CASE("A value-validating read with a coercible argument forces a conservative skip") {
		// `{1: 0}.has(1.2)` is false, but `Dictionary[int, int].has(1.2)` coerces
		// 1.2 to 1 and returns true, so typing would change behavior.
		InferenceFixture fixture("func f():\n\tvar d = {1: 0}\n\tvar found = d.has(1.2)\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::UNPROVABLE);
	}

	TEST_CASE("has and erase with a matching key do not block inference") {
		// The key arguments coerce to themselves on a Dictionary[int, int], so these
		// validating reads behave identically before and after typing.
		InferenceFixture fixture("func f():\n\tvar d = {1: 0}\n\tvar found = d.has(2)\n\td.erase(3)\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[int, int]");
	}

	TEST_CASE("Observing the dictionary's typedness forces a conservative skip") {
		InferenceFixture fixture("func f():\n\tvar d = {1: 0}\n\tvar typed = d.is_typed()\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::UNPROVABLE);
	}

	TEST_CASE("A copy-returning method forces a conservative skip") {
		InferenceFixture fixture("func f():\n\tvar d = {1: 0}\n\tvar copy = d.duplicate()\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::UNPROVABLE);
	}

	TEST_CASE("An unmodelled value-validating read forces a conservative skip") {
		// `has_all` validates each key against the typed key, so it can change
		// behavior once the dictionary is typed; it is not modeled.
		InferenceFixture fixture("func f():\n\tvar d = {1: 0}\n\tvar all = d.has_all([1.2])\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::UNPROVABLE);
	}

	TEST_CASE("In-place sort and reads do not block inference") {
		// `sort()` reorders entries in place without introducing or validating types.
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\td.sort()\n\tvar n = d.size()\n\tvar v = d[\"a\"]\n\tfor key in d:\n\t\tprint(key)\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("A subscript read with a coercible mismatched key forces a conservative skip") {
		// `{1: "x"}[1.2]` misses on the untyped dictionary but would coerce 1.2 to 1
		// and hit after typing as `Dictionary[int, String]`, so typing changes behavior.
		InferenceFixture fixture("func f():\n\tvar d = {1: \"x\"}\n\tvar v = d[1.2]\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::MIXED);
	}

	TEST_CASE("A subscript read with a matching key type does not block inference") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\tvar v = d[\"a\"]\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("A returned dictionary escapes and is not inferred") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\treturn d\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("Passing the dictionary to a call escapes it") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\tprint(d)\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Capturing the dictionary in a lambda escapes it") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\tvar fn = func(): d[\"b\"] = 2\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Referencing the dictionary inside a destructuring initializer escapes it") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\tvar (a, b) = (d, 2)\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("A bare dictionary reference in a discarded destructured slot does not escape it") {
		// The value is evaluated and immediately dropped via `_`; no binding
		// retains a reference to it, so this is as safe as a bare read.
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\td[\"b\"] = 2\n\tvar (_, b) = (d, 2)\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("An unmodelled call in a discarded destructured slot still forces a conservative skip") {
		// Unlike a bare reference, this element performs an unmodelled call
		// that still runs even though its result is discarded via `_`.
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\tvar (_, b) = (d.duplicate(), 2)\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::UNPROVABLE);
	}

	TEST_CASE("A non-literal destructuring initializer still escapes the dictionary") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\tvar (a, b) = (d, 2) if true else (0, 3)\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("An unused empty dictionary literal yields no evidence") {
		InferenceFixture fixture("func f():\n\tvar d = {}\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::NO_EVIDENCE);
	}

	TEST_CASE("An empty dictionary with only key evidence yields no evidence") {
		// A bare `erase` would validate the key but contributes nothing storable;
		// without a value type the dictionary cannot be fully typed.
		InferenceFixture fixture("func f():\n\tvar d = {}\n\td[\"a\"] = 1\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("An already-annotated declaration is not applicable") {
		InferenceFixture fixture("func f():\n\tvar d: Dictionary = {\"a\": 1}\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::NOT_APPLICABLE);
	}

	TEST_CASE("A non-literal initializer is not applicable") {
		InferenceFixture fixture("func make():\n\treturn {1: 0}\nfunc f():\n\tvar d = make()\n\td[\"a\"] = 1\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::NOT_APPLICABLE);
	}

	TEST_CASE("An array literal is not applicable to dictionary inference") {
		InferenceFixture fixture("func f():\n\tvar items = [1, 2]\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "items");
		CHECK_EQ(result.outcome, FSContainerInference::NOT_APPLICABLE);
	}

	TEST_CASE("A loop key variable reassigned to an incompatible type forces a conservative skip") {
		// `for k in d:` binds `k` as `Variant` while `d` is bare, so `k = 5` is valid.
		// Typing `d` as `Dictionary[String, int]` narrows `k` to a hard `String`.
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\tfor k in d:\n\t\tk = 5\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("A compound reassignment of a loop key variable forces a conservative skip") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\tfor k in d:\n\t\tk += 5\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
	}

	TEST_CASE("A loop key variable reassigned inside a captured lambda forces a conservative skip") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\tfor k in d:\n\t\tvar cb = func(): k = 5\n\t\tcb.call()\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
	}

	TEST_CASE("A loop key variable reassigned to the key type still infers") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\tfor k in d:\n\t\tk = \"b\"\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("An unreassigned loop key variable does not block dictionary inference") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\tfor k in d:\n\t\tprint(k)\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("A plain (soft) dictionary read binding reassigned incompatibly does not block inference") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\tvar v = d[\"a\"]\n\tv = \"x\"\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("An explicitly typed loop key variable is not narrowed and does not block inference") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\tfor k: Variant in d:\n\t\tk = 5\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("A loop key variable passed to an incompatible typed parameter forces a conservative skip") {
		// `for k in d:` binds `k` as `Variant`; typing `d` narrows it to the `String` key
		// type, which the `int` parameter rejects.
		InferenceFixture fixture("func take_int(n: int) -> void:\n\tpass\nfunc f():\n\tvar d = {\"a\": 1}\n\tfor k in d:\n\t\ttake_int(k)\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("A value read binding passed to an incompatible typed parameter forces a conservative skip") {
		// `var v = d["a"]` narrows from `Variant` to the `int` value type once `d` is typed,
		// which the `String` parameter rejects.
		InferenceFixture fixture("func take_string(s: String) -> void:\n\tpass\nfunc f():\n\tvar d = {\"a\": 1}\n\tvar v = d[\"a\"]\n\ttake_string(v)\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
	}

	TEST_CASE("A loop key variable passed to a matching typed parameter still infers") {
		InferenceFixture fixture("func take_string(s: String) -> void:\n\tpass\nfunc f():\n\tvar d = {\"a\": 1}\n\tfor k in d:\n\t\ttake_string(k)\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("A value read binding returned from an incompatible typed function forces a conservative skip") {
		InferenceFixture fixture("func f() -> String:\n\tvar d = {\"a\": 1}\n\tvar v = d[\"a\"]\n\treturn v\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
	}

	TEST_CASE("A value read binding assigned to an incompatible typed target forces a conservative skip") {
		InferenceFixture fixture("func f():\n\tvar d = {\"a\": 1}\n\tvar v = d[\"a\"]\n\tvar s: String = v\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
	}

	TEST_CASE("A value read binding used in matching typed positions still infers") {
		InferenceFixture fixture("func take_int(n: int) -> void:\n\tpass\nfunc f():\n\tvar d = {\"a\": 1}\n\tvar v = d[\"a\"]\n\ttake_int(v)\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("A direct value read passed to an incompatible typed parameter forces a conservative skip") {
		// `take_string(d["a"])` passes the value read directly; it narrows from `Variant`
		// to the `int` value type once `d` is typed, breaking the `String` parameter.
		InferenceFixture fixture("func take_string(s: String) -> void:\n\tpass\nfunc f():\n\tvar d = {\"a\": 1}\n\ttake_string(d[\"a\"])\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::READ_NARROWS);
	}

	TEST_CASE("A direct value read used in a matching typed position still infers") {
		InferenceFixture fixture("func take_int(n: int) -> void:\n\tpass\nfunc f():\n\tvar d = {\"a\": 1}\n\ttake_int(d[\"a\"])\n");
		FSContainerInference::Result result = infer_dict_in(fixture, "f", "d");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}
}

TEST_SUITE("[Modules][FoundryScript][ContainerInference][Member]") {
	TEST_CASE("A class-private member array mutated monomorphically is inferred") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\t_items.append(n)\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("Mutations across several methods are unioned") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func a() -> void:\n"
				"\t_items.append(1)\n"
				"func b() -> void:\n"
				"\t_items.push_back(2)\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("Heterogeneous mutations across methods are reported as mixed") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func a() -> void:\n"
				"\t_items.append(1)\n"
				"func b() -> void:\n"
				"\t_items.append(\"x\")\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::MIXED);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("A self-qualified mutation is inferred") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\tself._items.append(n)\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A public (non-underscore) member is skipped and reported") {
		// Without a leading underscore the member is part of the public API and can
		// be mutated by another script, which the single-file analysis cannot see.
		InferenceFixture fixture(
				"var items = []\n"
				"func add(n: int) -> void:\n"
				"\titems.append(n)\n");
		FSContainerInference::Result result = infer_member_in(fixture, "items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("A public member dictionary is skipped") {
		InferenceFixture fixture(
				"var by_name = {}\n"
				"func put(key: String, value: int) -> void:\n"
				"\tby_name[key] = value\n");
		FSContainerInference::Result result = infer_member_dict_in(fixture, "by_name");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("An exported member is skipped and reported") {
		InferenceFixture fixture(
				"@export var items = []\n"
				"func add() -> void:\n"
				"\titems.append(1)\n");
		FSContainerInference::Result result = infer_member_in(fixture, "items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("A member with a custom setter is skipped") {
		InferenceFixture fixture(
				"var items = []:\n"
				"\tset(value):\n"
				"\t\titems = value\n"
				"func add() -> void:\n"
				"\titems.append(1)\n");
		FSContainerInference::Result result = infer_member_in(fixture, "items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
		CHECK_FALSE(result.detail.is_empty());
	}

	TEST_CASE("A static member is skipped") {
		InferenceFixture fixture(
				"static var items = []\n"
				"func add() -> void:\n"
				"\titems.append(1)\n");
		FSContainerInference::Result result = infer_member_in(fixture, "items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Passing the member to a call escapes it") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func go() -> void:\n"
				"\t_items.append(1)\n"
				"\tprint(_items)\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Returning the member escapes it") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func get_items() -> Array:\n"
				"\treturn _items\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Leaking self escapes the member") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func register(bus: Object) -> void:\n"
				"\tbus.add(self)\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Accessing the member through another reference escapes it") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func copy_from(other) -> void:\n"
				"\tother._items.append(1)\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Calling an overridable script method on self resolves through the complete closure") {
		// With the subclass closure proven complete, every override of `after_add`
		// lives in this class or a scanned subclass, all folded into the same union.
		// The override body here does not touch `_items`, so the inference resolves the
		// dispatch instead of bailing.
		InferenceFixture fixture(
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\t_items.append(n)\n"
				"\tafter_add()\n"
				"func after_add() -> void:\n"
				"\tpass\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("Calling an overridable script method on self escapes when the closure is incomplete") {
		// When the caller cannot prove it enumerated every subclass, an unseen override
		// of `after_add` could mutate the inherited member, so the call is not bounded.
		InferenceFixture fixture(
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\t_items.append(n)\n"
				"\tafter_add()\n"
				"func after_add() -> void:\n"
				"\tpass\n");
		const FSParser::VariableNode *variable = fixture.member("_items");
		REQUIRE(variable != nullptr);
		FSContainerInference::Result result = FSContainerInference::infer_member_array_element_type(
				variable, fixture.tree(), Vector<const FSParser::ClassNode *>(), /* subclasses_complete */ false);
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Calling a native method via self does not block inference") {
		// A native method cannot reach a script-defined member, so it is safe.
		InferenceFixture fixture(
				"extends Node\n"
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\t_items.append(n)\n"
				"\tself.notify_property_list_changed()\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A consumed self native-call result that may be self escapes") {
		InferenceFixture fixture(
				"extends Node\n"
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak(registry: Object) -> void:\n"
				"\tregistry.call(\"store\", self.get_node(\".\"))\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Emitting a signal on self may run external callbacks and escapes") {
		InferenceFixture fixture(
				"extends Node\n"
				"signal changed\n"
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"\tself.emit_signal(\"changed\")\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("First-class signal emit on self escapes the member") {
		InferenceFixture fixture(
				"signal changed\n"
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"\tchanged.emit()\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Self-qualified first-class signal emit escapes the member") {
		InferenceFixture fixture(
				"signal changed\n"
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"\tself.changed.emit()\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("A statement-level native self call does not block inference") {
		// The call result is discarded, so a returned `self` cannot escape.
		InferenceFixture fixture(
				"extends Node\n"
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"\tself.add_to_group(\"g\")\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A global utility call does not block inference") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\t_items.append(n)\n"
				"\tprint(n)\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
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
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("A member dictionary mutated monomorphically is inferred") {
		InferenceFixture fixture(
				"var _by_name = {}\n"
				"func put(key: String, value: int) -> void:\n"
				"\t_by_name[key] = value\n");
		FSContainerInference::Result result = infer_member_dict_in(fixture, "_by_name");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("A self-qualified dictionary set is inferred") {
		InferenceFixture fixture(
				"var _by_name = {}\n"
				"func put() -> void:\n"
				"\tself._by_name.set(\"a\", 1)\n");
		FSContainerInference::Result result = infer_member_dict_in(fixture, "_by_name");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("An exported dictionary member is skipped") {
		InferenceFixture fixture(
				"@export var by_name = {}\n"
				"func put() -> void:\n"
				"\tby_name[\"a\"] = 1\n");
		FSContainerInference::Result result = infer_member_dict_in(fixture, "by_name");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("A lambda that uses self escapes the member") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func make_writer() -> Callable:\n"
				"\treturn func(): _items.append(\"x\")\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Aliasing the member in another member initializer escapes it") {
		InferenceFixture fixture(
				"var _items = []\n"
				"var _alias = _items\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("A dynamic self.set defeats the scan and escapes the member") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak() -> void:\n"
				"\tself.set(\"_items\", [\"x\"])\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("A thread-safe dynamic set escapes the member") {
		InferenceFixture fixture(
				"extends Node\n"
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak() -> void:\n"
				"\tset_thread_safe(\"_items\", [\"x\"])\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("An implicit-self dynamic set escapes the member") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak() -> void:\n"
				"\tset(\"_items\", [\"x\"])\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("A dynamic get that returns the member escapes it") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak() -> void:\n"
				"\tget(\"_items\").append(\"x\")\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
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
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::MIXED);
	}

	TEST_CASE("A nested class member with the same name is not counted as evidence") {
		InferenceFixture fixture(
				"var _items = []\n"
				"class Inner:\n"
				"\tvar _items = []\n"
				"\tfunc fill() -> void:\n"
				"\t\t_items.append(1)\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		// The outer member has no writes of its own; the inner same-named member's
		// writes must not be attributed to it.
		CHECK_EQ(result.outcome, FSContainerInference::NO_EVIDENCE);
	}

	TEST_CASE("A lambda body that mutates the member through another reference escapes it") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func make(other) -> Callable:\n"
				"\treturn func(): other._items.append(\"x\")\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("The member's own initializer leaking self escapes it") {
		InferenceFixture fixture(
				"func register(o) -> int:\n"
				"\treturn 0\n"
				"var _items = [register(self)]\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Aliasing the member through a parameter default escapes it") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func f(alias = _items) -> void:\n"
				"\t_items.append(1)\n"
				"\talias.append(\"x\")\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Referencing a reflection method as a callable escapes the member") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak() -> void:\n"
				"\tvar setter := set\n"
				"\tsetter.call(\"_items\", [\"x\"])\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Referencing self.set as a callable escapes the member") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak() -> void:\n"
				"\tvar setter := self.set\n"
				"\tsetter.call(\"_items\", [\"x\"])\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("A member dictionary with an overridable self call resolves through the complete closure") {
		// As in the array case, a complete closure means every override of `after_put`
		// is folded into this union, so the dispatch resolves instead of bailing.
		InferenceFixture fixture(
				"var _by_name = {}\n"
				"func put(key: String, value: int) -> void:\n"
				"\t_by_name[key] = value\n"
				"\tafter_put()\n"
				"func after_put() -> void:\n"
				"\tpass\n");
		FSContainerInference::Result result = infer_member_dict_in(fixture, "_by_name");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Dictionary[String, int]");
	}

	TEST_CASE("A super call may mutate the inherited member and escapes it") {
		InferenceFixture fixture(
				"extends Node\n"
				"var _items = []\n"
				"func setup() -> void:\n"
				"\t_items.append(1)\n"
				"\tsuper.add_to_group(\"g\")\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Referencing an overridable method as a callable escapes the member") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak() -> void:\n"
				"\tvar cb = after_add\n"
				"\tcb.call()\n"
				"func after_add() -> void:\n"
				"\tpass\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("A reflection write through a foreign reference naming the member escapes it") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak(other: Object) -> void:\n"
				"\tother.set(\"_items\", [\"x\"])\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Dynamic property indexing of the member through self escapes it") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak() -> void:\n"
				"\tself[\"_items\"] = [\"x\"]\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Dynamic property indexing of the member through a reference escapes it") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak(other) -> void:\n"
				"\tother[\"_items\"].append(\"x\")\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Nested callv reflection naming the member escapes it") {
		InferenceFixture fixture(
				"extends Node\n"
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak(other: Object) -> void:\n"
				"\tother.callv(\"set\", [\"_items\", [\"x\"]])\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("A constant member name in a reflection call escapes the member") {
		InferenceFixture fixture(
				"const P = \"_items\"\n"
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak() -> void:\n"
				"\tset(P, [\"x\"])\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("A constant member name in dynamic indexing escapes the member") {
		InferenceFixture fixture(
				"const P = \"_items\"\n"
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak() -> void:\n"
				"\tself[P] = [\"x\"]\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("A NodePath member name in set_indexed escapes the member") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak() -> void:\n"
				"\tset_indexed(^\"_items\", [\"x\"])\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("A reflection get with a dynamic name on a possibly-self receiver escapes") {
		InferenceFixture fixture(
				"extends Node\n"
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak(other: Object, prop: String) -> void:\n"
				"\tother.get(prop).append(\"x\")\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Dynamic indexing with a runtime key on a possibly-self receiver escapes") {
		InferenceFixture fixture(
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func sneak(other, prop) -> void:\n"
				"\tother[prop].append(\"x\")\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("Leaking self through a $\".\" node reference escapes the member") {
		InferenceFixture fixture(
				"extends Node\n"
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"func expose() -> Node:\n"
				"\treturn $\".\"\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::ESCAPES);
	}

	TEST_CASE("A $child node reference for a different node does not block inference") {
		InferenceFixture fixture(
				"extends Node\n"
				"var _items = []\n"
				"func add() -> void:\n"
				"\t_items.append(1)\n"
				"\tvar n = $Child\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::INFERRED);
		CHECK_EQ(result.element_type.to_string(), "Array[int]");
	}

	TEST_CASE("An unused member array yields no evidence") {
		InferenceFixture fixture("var _items = []\n");
		FSContainerInference::Result result = infer_member_in(fixture, "_items");
		CHECK_EQ(result.outcome, FSContainerInference::NO_EVIDENCE);
	}

	TEST_CASE("An already-annotated member is not applicable") {
		InferenceFixture fixture("var items: Array = []\n");
		FSContainerInference::Result result = infer_member_in(fixture, "items");
		CHECK_EQ(result.outcome, FSContainerInference::NOT_APPLICABLE);
	}
}

#ifndef FOUNDRY_SCRIPT_NO_LSP

// Locates the Add Type Annotation candidate anchored at p_line.
static const RefactorCandidate *inference_candidate_at_line(const RefactorCandidatesResult &p_result, int p_line) {
	for (const RefactorCandidate &candidate : p_result.candidates) {
		if (candidate.line == p_line) {
			return &candidate;
		}
	}
	return nullptr;
}

TEST_SUITE("[Modules][FoundryScript][ContainerInference]") {
	TEST_CASE("Candidate collection upgrades a provable local array to Array[int]") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/container_infer.fs";
		const String source =
				"func f():\n"
				"\tvar nums = []\n"
				"\tnums.append(1)\n";
		TemporaryScriptFile file(path, source);

		RefactorContext context;
		context.path = path;
		context.source = source;
		RefactorCandidatesResult result = FSRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *nums = inference_candidate_at_line(result, 1);
		REQUIRE(nums != nullptr);
		CHECK(nums->enabled);
		REQUIRE_FALSE(nums->edits.is_empty());
		CHECK_EQ(nums->edits[0].new_text, ": Array[int] = ");

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Candidate collection keeps a mixed local array as bare Array") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/container_mixed.fs";
		const String source =
				"func f():\n"
				"\tvar items = [1, \"a\"]\n";
		TemporaryScriptFile file(path, source);

		RefactorContext context;
		context.path = path;
		context.source = source;
		RefactorCandidatesResult result = FSRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *items = inference_candidate_at_line(result, 1);
		REQUIRE(items != nullptr);
		CHECK(items->enabled);
		REQUIRE_FALSE(items->edits.is_empty());
		CHECK_EQ(items->edits[0].new_text, ": Array = ");

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Candidate collection upgrades a provable local dictionary") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/container_infer_dict.fs";
		const String source =
				"func f():\n"
				"\tvar d = {}\n"
				"\td[\"a\"] = 1\n";
		TemporaryScriptFile file(path, source);

		RefactorContext context;
		context.path = path;
		context.source = source;
		RefactorCandidatesResult result = FSRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *d = inference_candidate_at_line(result, 1);
		REQUIRE(d != nullptr);
		CHECK(d->enabled);
		REQUIRE_FALSE(d->edits.is_empty());
		CHECK_EQ(d->edits[0].new_text, ": Dictionary[String, int] = ");

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Candidate collection keeps a mixed local dictionary as bare Dictionary") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/container_mixed_dict.fs";
		const String source =
				"func f():\n"
				"\tvar d = {\"a\": 1, 2: \"b\"}\n";
		TemporaryScriptFile file(path, source);

		RefactorContext context;
		context.path = path;
		context.source = source;
		RefactorCandidatesResult result = FSRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *d = inference_candidate_at_line(result, 1);
		REQUIRE(d != nullptr);
		CHECK(d->enabled);
		REQUIRE_FALSE(d->edits.is_empty());
		CHECK_EQ(d->edits[0].new_text, ": Dictionary = ");

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Candidate collection upgrades a provable member array to Array[int]") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/container_member_array.fs";
		const String source =
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\t_items.append(n)\n";
		TemporaryScriptFile file(path, source);

		RefactorContext context;
		context.path = path;
		context.source = source;
		context.allow_member_container_inference = true; // Verified migration path.
		RefactorCandidatesResult result = FSRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *items = inference_candidate_at_line(result, 0);
		REQUIRE(items != nullptr);
		CHECK(items->enabled);
		REQUIRE_FALSE(items->edits.is_empty());
		CHECK_EQ(items->edits[0].new_text, ": Array[int] = ");

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Candidate collection keeps an escaping member array as bare Array") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/container_member_escape.fs";
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
		context.allow_member_container_inference = true; // Verified migration path.
		RefactorCandidatesResult result = FSRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *items = inference_candidate_at_line(result, 0);
		REQUIRE(items != nullptr);
		CHECK(items->enabled);
		REQUIRE_FALSE(items->edits.is_empty());
		CHECK_EQ(items->edits[0].new_text, ": Array = ");

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Candidate collection upgrades a provable member dictionary") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/container_member_dict.fs";
		const String source =
				"var _by_name = {}\n"
				"func put(key: String, value: int) -> void:\n"
				"\t_by_name[key] = value\n";
		TemporaryScriptFile file(path, source);

		RefactorContext context;
		context.path = path;
		context.source = source;
		context.allow_member_container_inference = true; // Verified migration path.
		RefactorCandidatesResult result = FSRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *d = inference_candidate_at_line(result, 0);
		REQUIRE(d != nullptr);
		CHECK(d->enabled);
		REQUIRE_FALSE(d->edits.is_empty());
		CHECK_EQ(d->edits[0].new_text, ": Dictionary[String, int] = ");

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("A subclass writing a matching element type keeps the member inferred") {
		// An external subclass appends the same element type to the inherited member,
		// so the project-wide union is still monomorphic and the upgrade is sound.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		const String base_path = "res://refactor/container_member_subclass_base_ok.fs";
		const String base_source =
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\t_items.append(n)\n";
		TemporaryScriptFile base_file(base_path, base_source);

		const String subclass_path = "res://refactor/container_member_subclass_child_ok.fs";
		TemporaryScriptFile subclass_file(subclass_path,
				"extends \"res://refactor/container_member_subclass_base_ok.fs\"\n"
				"func add_more(n: int) -> void:\n"
				"\t_items.append(n)\n");

		RefactorContext context;
		context.path = base_path;
		context.source = base_source;
		context.allow_member_container_inference = true; // Verified migration path.
		RefactorCandidatesResult result = FSRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *items = inference_candidate_at_line(result, 0);
		REQUIRE(items != nullptr);
		CHECK(items->enabled);
		REQUIRE_FALSE(items->edits.is_empty());
		CHECK_EQ(items->edits[0].new_text, ": Array[int] = ");

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("A subclass writing a conflicting element type keeps the member bare") {
		// The subclass mutates the inherited member with a different element type, so
		// the project-wide union is no longer monomorphic. The inference itself must
		// withhold the upgrade rather than leaving it to the verifier.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		const String base_path = "res://refactor/container_member_subclass_base_conflict.fs";
		const String base_source =
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\t_items.append(n)\n";
		TemporaryScriptFile base_file(base_path, base_source);

		const String subclass_path = "res://refactor/container_member_subclass_child_conflict.fs";
		TemporaryScriptFile subclass_file(subclass_path,
				"extends \"res://refactor/container_member_subclass_base_conflict.fs\"\n"
				"func add_text(s: String) -> void:\n"
				"\t_items.append(s)\n");

		RefactorContext context;
		context.path = base_path;
		context.source = base_source;
		context.allow_member_container_inference = true; // Verified migration path.
		RefactorCandidatesResult result = FSRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *items = inference_candidate_at_line(result, 0);
		REQUIRE(items != nullptr);
		CHECK(items->enabled);
		REQUIRE_FALSE(items->edits.is_empty());
		CHECK_EQ(items->edits[0].new_text, ": Array = ");

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("An overridable self call whose override matches keeps the member inferred") {
		// The base calls an overridable `self` hook. A subclass overrides it and mutates
		// the inherited member with the same element type. Because the subclass closure
		// is complete, the override body is folded into the union: the call resolves
		// through it instead of bailing, and the union stays monomorphic.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		const String base_path = "res://refactor/container_member_override_base_ok.fs";
		const String base_source =
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\t_items.append(n)\n"
				"\ton_added()\n"
				"func on_added() -> void:\n"
				"\tpass\n";
		TemporaryScriptFile base_file(base_path, base_source);

		const String subclass_path = "res://refactor/container_member_override_child_ok.fs";
		TemporaryScriptFile subclass_file(subclass_path,
				"extends \"res://refactor/container_member_override_base_ok.fs\"\n"
				"func on_added() -> void:\n"
				"\t_items.append(7)\n");

		RefactorContext context;
		context.path = base_path;
		context.source = base_source;
		context.allow_member_container_inference = true; // Verified migration path.
		RefactorCandidatesResult result = FSRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *items = inference_candidate_at_line(result, 0);
		REQUIRE(items != nullptr);
		CHECK(items->enabled);
		REQUIRE_FALSE(items->edits.is_empty());
		CHECK_EQ(items->edits[0].new_text, ": Array[int] = ");

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("An overridable self call whose override conflicts keeps the member bare") {
		// The subclass overrides the hook called on `self` and mutates the inherited
		// member with a different element type. The folded override pushes a second
		// type into the union, so the inference withholds the upgrade.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		const String base_path = "res://refactor/container_member_override_base_conflict.fs";
		const String base_source =
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\t_items.append(n)\n"
				"\ton_added()\n"
				"func on_added() -> void:\n"
				"\tpass\n";
		TemporaryScriptFile base_file(base_path, base_source);

		const String subclass_path = "res://refactor/container_member_override_child_conflict.fs";
		TemporaryScriptFile subclass_file(subclass_path,
				"extends \"res://refactor/container_member_override_base_conflict.fs\"\n"
				"func on_added() -> void:\n"
				"\t_items.append(\"x\")\n");

		RefactorContext context;
		context.path = base_path;
		context.source = base_source;
		context.allow_member_container_inference = true; // Verified migration path.
		RefactorCandidatesResult result = FSRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *items = inference_candidate_at_line(result, 0);
		REQUIRE(items != nullptr);
		CHECK(items->enabled);
		REQUIRE_FALSE(items->edits.is_empty());
		CHECK_EQ(items->edits[0].new_text, ": Array = ");

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("An inherited base method called on self keeps the member bare") {
		// The declaring class calls a method inherited from an unscanned base. The
		// subclass closure spans only descendants, so the base body is not folded into
		// the union: it could mutate the member dynamically (`self.set(...)`) with
		// another type unseen, and the inference must withhold the upgrade rather than
		// resolve the call.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		const String base_path = "res://refactor/container_member_inherited_base.fs";
		TemporaryScriptFile base_file(base_path,
				"extends RefCounted\n"
				"func base_hook() -> void:\n"
				"\tset(\"_items\", [\"x\"])\n");

		const String derived_path = "res://refactor/container_member_inherited_derived.fs";
		const String derived_source =
				"extends \"res://refactor/container_member_inherited_base.fs\"\n"
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\t_items.append(n)\n"
				"\tbase_hook()\n";
		TemporaryScriptFile derived_file(derived_path, derived_source);

		RefactorContext context;
		context.path = derived_path;
		context.source = derived_source;
		context.allow_member_container_inference = true; // Verified migration path.
		RefactorCandidatesResult result = FSRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *items = inference_candidate_at_line(result, 1);
		REQUIRE(items != nullptr);
		CHECK(items->enabled);
		REQUIRE_FALSE(items->edits.is_empty());
		CHECK_EQ(items->edits[0].new_text, ": Array = ");

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("A subclass escaping the inherited member keeps it bare") {
		// The subclass returns the inherited member, escaping it; even though every
		// observed element is an int, an external holder could mutate it, so the
		// inference must withhold the upgrade.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		const String base_path = "res://refactor/container_member_subclass_base_escape.fs";
		const String base_source =
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\t_items.append(n)\n";
		TemporaryScriptFile base_file(base_path, base_source);

		const String subclass_path = "res://refactor/container_member_subclass_child_escape.fs";
		TemporaryScriptFile subclass_file(subclass_path,
				"extends \"res://refactor/container_member_subclass_base_escape.fs\"\n"
				"func leak() -> Array:\n"
				"\treturn _items\n");

		RefactorContext context;
		context.path = base_path;
		context.source = base_source;
		context.allow_member_container_inference = true; // Verified migration path.
		RefactorCandidatesResult result = FSRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *items = inference_candidate_at_line(result, 0);
		REQUIRE(items != nullptr);
		CHECK(items->enabled);
		REQUIRE_FALSE(items->edits.is_empty());
		CHECK_EQ(items->edits[0].new_text, ": Array = ");

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("An unparsable plausible subclass keeps the member bare") {
		// A project script that fails to parse but textually `extends` the base could
		// hide a subclass that mutates the inherited member. The open world cannot be
		// proven bounded, so the inference must withhold the upgrade conservatively.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		const String base_path = "res://refactor/container_member_subclass_base_broken.fs";
		const String base_source =
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\t_items.append(n)\n";
		TemporaryScriptFile base_file(base_path, base_source);

		// A deliberately broken subclass that still names the base file in its
		// `extends`. Matching by path avoids registering a global class name that
		// could leak into unrelated tests.
		const String broken_path = "res://refactor/container_member_subclass_child_broken.fs";
		TemporaryScriptFile broken_file(broken_path,
				"extends \"res://refactor/container_member_subclass_base_broken.fs\"\n"
				"func add_text(s: String) -> void:\n"
				"\tvar broken: = =\n");

		RefactorContext context;
		context.path = base_path;
		context.source = base_source;
		context.allow_member_container_inference = true; // Verified migration path.
		RefactorCandidatesResult result = FSRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *items = inference_candidate_at_line(result, 0);
		REQUIRE(items != nullptr);
		CHECK(items->enabled);
		REQUIRE_FALSE(items->edits.is_empty());
		CHECK_EQ(items->edits[0].new_text, ": Array = ");

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Interactive collection keeps a member array bare without verification") {
		// The default (interactive) path applies edits directly with no verifier, so
		// the open-world member element upgrade is withheld and the analyzer's bare
		// container type is offered instead.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/container_member_interactive.fs";
		const String source =
				"var _items = []\n"
				"func add(n: int) -> void:\n"
				"\t_items.append(n)\n";
		TemporaryScriptFile file(path, source);

		RefactorContext context;
		context.path = path;
		context.source = source;
		// allow_member_container_inference defaults to false (interactive path).
		RefactorCandidatesResult result = FSRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *items = inference_candidate_at_line(result, 0);
		REQUIRE(items != nullptr);
		CHECK(items->enabled);
		REQUIRE_FALSE(items->edits.is_empty());
		CHECK_EQ(items->edits[0].new_text, ": Array = ");

		memdelete(protocol);
		memdelete(editor_file_system);
	}
}

#endif // FOUNDRY_SCRIPT_NO_LSP

} // namespace FSTests

#endif // TOOLS_ENABLED
