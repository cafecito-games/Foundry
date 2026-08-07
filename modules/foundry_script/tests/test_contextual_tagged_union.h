/**************************************************************************/
/*  test_contextual_tagged_union.h                                        */
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

#ifdef TOOLS_ENABLED

#include "../fs_analyzer.h"
#include "../fs_format.h"
#include "../fs_parser.h"

#include "tests/test_macros.h"

namespace FSTests {

// Parses `p_source` as a whole script and hands back the parser so the caller can walk the tree.
// The parse tree is owned by the parser, so it must outlive every node the caller inspects.
static Error parse_contextual_case_source(FSParser &p_parser, const String &p_source) {
	return p_parser.parse(p_source, "user://contextual_tagged_union.fs", false);
}

static const FSParser::ExpressionNode *find_local_initializer(const FSParser::SuiteNode *p_suite, const StringName &p_name) {
	if (p_suite == nullptr) {
		return nullptr;
	}
	for (int i = 0; i < p_suite->statements.size(); i++) {
		const FSParser::Node *statement = p_suite->statements[i];
		if (statement == nullptr || statement->type != FSParser::Node::VARIABLE) {
			continue;
		}
		const FSParser::VariableNode *variable = static_cast<const FSParser::VariableNode *>(statement);
		if (variable->identifier != nullptr && variable->identifier->name == p_name) {
			return variable->initializer;
		}
	}
	return nullptr;
}

static const FSParser::ExpressionNode *find_initializer(const FSParser &p_parser, const StringName &p_function_name, const StringName &p_variable_name) {
	const FSParser::ClassNode *tree = p_parser.get_tree();
	if (tree == nullptr || !tree->has_function(p_function_name)) {
		return nullptr;
	}
	const FSParser::FunctionNode *function = tree->get_member(p_function_name).function;
	return function != nullptr ? find_local_initializer(function->body, p_variable_name) : nullptr;
}

static const FSParser::SubscriptNode *as_subscript(const FSParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr || p_expression->type != FSParser::Node::SUBSCRIPT) {
		return nullptr;
	}
	return static_cast<const FSParser::SubscriptNode *>(p_expression);
}

static const FSParser::CallNode *as_call(const FSParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr || p_expression->type != FSParser::Node::CALL) {
		return nullptr;
	}
	return static_cast<const FSParser::CallNode *>(p_expression);
}

TEST_CASE("[Modules][FoundryScript][ContextualTaggedUnion] Payload shorthand parses as a contextual case call") {
	FSParser parser;
	REQUIRE_EQ(parse_contextual_case_source(parser,
					   "func run() -> void:\n"
					   "\tvar value = .Ok(1)\n"),
			OK);

	const FSParser::CallNode *call = as_call(find_initializer(parser, SNAME("run"), SNAME("value")));
	REQUIRE(call != nullptr);
	CHECK(call->is_contextual_enum_case);
	CHECK_EQ(call->function_name, StringName("Ok"));
	REQUIRE_EQ(call->arguments.size(), 1);

	// The callee is the case name with no union in front of it; the analyzer fills the union in
	// from the expected type at the consumer site.
	const FSParser::SubscriptNode *callee = as_subscript(call->callee);
	REQUIRE(callee != nullptr);
	CHECK(callee->base == nullptr);
	CHECK(callee->is_attribute);
	CHECK(callee->is_contextual_enum_case);
	REQUIRE(callee->attribute != nullptr);
	CHECK_EQ(callee->attribute->name, StringName("Ok"));
}

TEST_CASE("[Modules][FoundryScript][ContextualTaggedUnion] Payload-less shorthand parses as a contextual case reference") {
	FSParser parser;
	REQUIRE_EQ(parse_contextual_case_source(parser,
					   "func run() -> void:\n"
					   "\tvar value = .None\n"),
			OK);

	const FSParser::SubscriptNode *reference = as_subscript(find_initializer(parser, SNAME("run"), SNAME("value")));
	REQUIRE(reference != nullptr);
	CHECK(reference->base == nullptr);
	CHECK(reference->is_attribute);
	CHECK(reference->is_contextual_enum_case);
	CHECK_FALSE(reference->is_tuple_index);
	REQUIRE(reference->attribute != nullptr);
	CHECK_EQ(reference->attribute->name, StringName("None"));
}

TEST_CASE("[Modules][FoundryScript][ContextualTaggedUnion] Shorthand parses in nested expression positions") {
	FSParser parser;
	REQUIRE_EQ(parse_contextual_case_source(parser,
					   "func run(condition: bool) -> void:\n"
					   "\tvar elements = [.Ok(1), .Err(\"x\")]\n"
					   "\tvar chosen = .Ok(1) if condition else .Err(\"no\")\n"),
			OK);

	const FSParser::ExpressionNode *elements = find_initializer(parser, SNAME("run"), SNAME("elements"));
	REQUIRE(elements != nullptr);
	REQUIRE_EQ(elements->type, FSParser::Node::ARRAY);
	const FSParser::ArrayNode *array = static_cast<const FSParser::ArrayNode *>(elements);
	REQUIRE_EQ(array->elements.size(), 2);
	for (int i = 0; i < array->elements.size(); i++) {
		const FSParser::CallNode *element = as_call(array->elements[i]);
		REQUIRE(element != nullptr);
		CHECK(element->is_contextual_enum_case);
	}

	const FSParser::ExpressionNode *chosen = find_initializer(parser, SNAME("run"), SNAME("chosen"));
	REQUIRE(chosen != nullptr);
	REQUIRE_EQ(chosen->type, FSParser::Node::TERNARY_OPERATOR);
	const FSParser::TernaryOpNode *ternary = static_cast<const FSParser::TernaryOpNode *>(chosen);
	const FSParser::CallNode *true_branch = as_call(ternary->true_expr);
	const FSParser::CallNode *false_branch = as_call(ternary->false_expr);
	REQUIRE(true_branch != nullptr);
	REQUIRE(false_branch != nullptr);
	CHECK(true_branch->is_contextual_enum_case);
	CHECK(false_branch->is_contextual_enum_case);
}

TEST_CASE("[Modules][FoundryScript][ContextualTaggedUnion] Leading-dot numbers and tuple indices are unchanged") {
	FSParser parser;
	REQUIRE_EQ(parse_contextual_case_source(parser,
					   "func run(pair: (int, int)) -> void:\n"
					   "\tvar fraction = .5\n"
					   "\tvar first = pair.0\n"),
			OK);

	const FSParser::ExpressionNode *fraction = find_initializer(parser, SNAME("run"), SNAME("fraction"));
	REQUIRE(fraction != nullptr);
	REQUIRE_EQ(fraction->type, FSParser::Node::LITERAL);
	const FSParser::LiteralNode *literal = static_cast<const FSParser::LiteralNode *>(fraction);
	CHECK_EQ(literal->value.get_type(), Variant::FLOAT);
	CHECK_EQ(double(literal->value), doctest::Approx(0.5));

	const FSParser::SubscriptNode *tuple_index = as_subscript(find_initializer(parser, SNAME("run"), SNAME("first")));
	REQUIRE(tuple_index != nullptr);
	CHECK(tuple_index->is_tuple_index);
	CHECK_FALSE(tuple_index->is_contextual_enum_case);
	REQUIRE(tuple_index->base != nullptr);
}

TEST_CASE("[Modules][FoundryScript][ContextualTaggedUnion] A dot without a case name is a parse error") {
	FSParser parser;
	CHECK_NE(parse_contextual_case_source(parser,
					 "func run() -> void:\n"
					 "\tvar value = .\n"),
			OK);
	CHECK_FALSE(parser.get_errors().is_empty());
}

TEST_CASE("[Modules][FoundryScript][ContextualTaggedUnion] Case patterns accept the shorthand head") {
	FSParser parser;
	REQUIRE_EQ(parse_contextual_case_source(parser,
					   "func run(subject: int) -> void:\n"
					   "\tmatch subject:\n"
					   "\t\t.Ok(value):\n"
					   "\t\t\tprint(value)\n"
					   "\t\t_:\n"
					   "\t\t\tpass\n"),
			OK);

	const FSParser::ClassNode *tree = parser.get_tree();
	REQUIRE(tree != nullptr);
	REQUIRE(tree->has_function(SNAME("run")));
	const FSParser::FunctionNode *function = tree->get_member(SNAME("run")).function;
	REQUIRE(function != nullptr);
	REQUIRE(function->body != nullptr);
	REQUIRE_EQ(function->body->statements.size(), 1);
	REQUIRE_EQ(function->body->statements[0]->type, FSParser::Node::MATCH);
	const FSParser::MatchNode *match = static_cast<const FSParser::MatchNode *>(function->body->statements[0]);
	REQUIRE_EQ(match->branches.size(), 2);
	REQUIRE_EQ(match->branches[0]->patterns.size(), 1);

	const FSParser::PatternNode *pattern = match->branches[0]->patterns[0];
	REQUIRE(pattern != nullptr);
	REQUIRE_EQ(pattern->pattern_type, FSParser::PatternNode::PT_ENUM_CASE);
	CHECK(pattern->is_contextual_enum_case);
	REQUIRE(pattern->case_type != nullptr);
	REQUIRE_EQ(pattern->case_type->type_chain.size(), 1);
	CHECK_EQ(pattern->case_type->type_chain[0]->name, StringName("Ok"));
	REQUIRE_EQ(pattern->array.size(), 1);
}

TEST_CASE("[Modules][FoundryScript][ContextualTaggedUnion] The formatter round-trips the shorthand") {
	const String source =
			"func run(condition: bool, subject: int) -> void:\n"
			"\tvar value = .Ok(1)\n"
			"\tvar empty = .None\n"
			"\tvar elements = [.Ok(1), .Err(\"x\")]\n"
			"\tvar chosen = .Ok(1) if condition else .Err(\"no\")\n"
			"\tmatch subject:\n"
			"\t\t.Ok(payload):\n"
			"\t\t\tprint(payload)\n"
			"\t\t_:\n"
			"\t\t\tpass\n";

	FSFormatter formatter;
	FSFormatter::Result result;
	REQUIRE_EQ(formatter.format(source, "contextual_tagged_union.fs", result), OK);
	CHECK_EQ(result.formatted, source);

	// Canonical output must be a fixed point of the formatter.
	FSFormatter::Result reformatted;
	REQUIRE_EQ(formatter.format(result.formatted, "contextual_tagged_union.fs", reformatted), OK);
	CHECK_EQ(reformatted.formatted, result.formatted);
}

// Parses and analyzes a whole script so resolved types and folded constants can be read back.
class ContextualCaseFixture {
public:
	FSParser parser;
	FSAnalyzer analyzer;
	Error parse_error = ERR_PARSE_ERROR;
	Error analyze_error = ERR_PARSE_ERROR;

	explicit ContextualCaseFixture(const String &p_source) :
			analyzer(&parser) {
		parse_error = parse_contextual_case_source(parser, p_source);
		if (parse_error == OK) {
			analyze_error = analyzer.analyze();
		}
	}

	const FSParser::FunctionNode *function(const StringName &p_name) const {
		const FSParser::ClassNode *tree = parser.get_tree();
		if (tree == nullptr || !tree->has_function(p_name)) {
			return nullptr;
		}
		return tree->get_member(p_name).function;
	}

	const FSParser::ExpressionNode *initializer(const StringName &p_function_name, const StringName &p_variable_name) const {
		return find_initializer(parser, p_function_name, p_variable_name);
	}
};

static const String RESULT_DECLARATION =
		"enum Result[T, E]:\n"
		"\tOk(value: T)\n"
		"\tErr(error: E)\n"
		"\n"
		"\n";

TEST_CASE("[Modules][FoundryScript][ContextualTaggedUnion] A var initializer resolves the shorthand like the explicit form") {
	ContextualCaseFixture fixture(RESULT_DECLARATION +
			"func run() -> void:\n"
			"\tvar shorthand: Result[int, String] = .Ok(1)\n"
			"\tvar explicit: Result[int, String] = Result[int, String].Ok(1)\n"
			"\tprint(shorthand, explicit)\n");
	REQUIRE_EQ(fixture.parse_error, OK);
	REQUIRE_EQ(fixture.analyze_error, OK);

	const FSParser::ExpressionNode *shorthand = fixture.initializer(SNAME("run"), SNAME("shorthand"));
	const FSParser::ExpressionNode *explicit_form = fixture.initializer(SNAME("run"), SNAME("explicit"));
	REQUIRE(shorthand != nullptr);
	REQUIRE(explicit_form != nullptr);
	CHECK_EQ(shorthand->get_datatype(), explicit_form->get_datatype());
	CHECK(shorthand->get_datatype().is_tagged_union_type());
	CHECK_FALSE(shorthand->get_datatype().is_meta_type);
	CHECK(shorthand->get_datatype().to_string().ends_with("Result[int, String]"));

	// Both spellings fold to the same read-only `[tag, payload...]` Array.
	REQUIRE(shorthand->is_constant);
	REQUIRE(explicit_form->is_constant);
	CHECK_EQ(shorthand->reduced_value, explicit_form->reduced_value);
	const Array folded = shorthand->reduced_value;
	CHECK(folded.is_read_only());
	REQUIRE_EQ(folded.size(), 2);
	CHECK_EQ(folded[0], Variant(0));
	CHECK_EQ(folded[1], Variant(1));
}

TEST_CASE("[Modules][FoundryScript][ContextualTaggedUnion] Shorthand parameter defaults constant-fold like the explicit form") {
	ContextualCaseFixture fixture(RESULT_DECLARATION +
			"enum Option[T]:\n"
			"\tNone\n"
			"\tSome(value: T)\n"
			"\n"
			"\n"
			"func shorthand_default(value: Result[int, String] = .Ok(1)) -> void:\n"
			"\tprint(value)\n"
			"\n"
			"\n"
			"func explicit_default(value: Result[int, String] = Result[int, String].Ok(1)) -> void:\n"
			"\tprint(value)\n"
			"\n"
			"\n"
			"func payloadless_default(value: Option[int] = .None) -> void:\n"
			"\tprint(value)\n");
	REQUIRE_EQ(fixture.parse_error, OK);
	REQUIRE_EQ(fixture.analyze_error, OK);

	const FSParser::FunctionNode *shorthand = fixture.function(SNAME("shorthand_default"));
	const FSParser::FunctionNode *explicit_form = fixture.function(SNAME("explicit_default"));
	const FSParser::FunctionNode *payloadless = fixture.function(SNAME("payloadless_default"));
	REQUIRE(shorthand != nullptr);
	REQUIRE(explicit_form != nullptr);
	REQUIRE(payloadless != nullptr);
	REQUIRE_EQ(shorthand->default_arg_values.size(), 1);
	REQUIRE_EQ(explicit_form->default_arg_values.size(), 1);
	REQUIRE_EQ(payloadless->default_arg_values.size(), 1);

	Array expected_payload;
	expected_payload.push_back(0);
	expected_payload.push_back(1);
	CHECK_EQ(shorthand->default_arg_values[0], Variant(expected_payload));
	CHECK_EQ(shorthand->default_arg_values[0], explicit_form->default_arg_values[0]);

	Array expected_payloadless;
	expected_payloadless.push_back(0);
	CHECK_EQ(payloadless->default_arg_values[0], Variant(expected_payloadless));
}

TEST_CASE("[Modules][FoundryScript][ContextualTaggedUnion] A target that names no union rejects the shorthand") {
	ContextualCaseFixture fixture(RESULT_DECLARATION +
			"func run() -> void:\n"
			"\tvar untyped = .Ok(1)\n"
			"\tprint(untyped)\n");
	REQUIRE_EQ(fixture.parse_error, OK);
	CHECK_NE(fixture.analyze_error, OK);

	REQUIRE_FALSE(fixture.parser.get_errors().is_empty());
	CHECK_EQ(fixture.parser.get_errors().front()->get().message,
			String(R"*(Contextual shorthand ".Ok" needs an expected tagged-union type; annotate the target, e.g. "var x: Result[int, String] = .Ok(...)".)*"));
}

} // namespace FSTests

#endif // TOOLS_ENABLED
