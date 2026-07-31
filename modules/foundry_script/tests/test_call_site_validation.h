/**************************************************************************/
/*  test_call_site_validation.h                                           */
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

#include "../fs_analyzer.h"
#include "../fs_parser.h"

#include "tests/test_macros.h"

namespace FSTests {

static const FSParser::CallNode *call_site_validation_first_call(const FSParser::ClassNode *p_class, const StringName &p_function) {
	if (p_class == nullptr || !p_class->has_function(p_function)) {
		return nullptr;
	}
	const FSParser::FunctionNode *function = p_class->get_member(p_function).function;
	if (function == nullptr || function->body == nullptr || function->body->statements.is_empty()) {
		return nullptr;
	}
	const FSParser::Node *statement = function->body->statements[0];
	if (statement->type != FSParser::Node::CALL) {
		return nullptr;
	}
	return static_cast<const FSParser::CallNode *>(statement);
}

static bool analyzer_reports_substring(FSParser &p_parser, const String &p_substring) {
	for (const FSParser::ParserError &parser_error : p_parser.get_errors()) {
		if (parser_error.message.contains(p_substring)) {
			return true;
		}
	}
	return false;
}

TEST_CASE("[Modules][FoundryScript][CallSiteValidation] Named duplicate arguments are rejected during analysis") {
	FSParser parser;
	const Error error = parser.parse(
			"func target(a: int, b: int) -> void:\n"
			"\tpass\n"
			"func test() -> void:\n"
			"\ttarget(a = 1, a = 2)\n",
			"user://named_duplicate.fs",
			false);
	CHECK(error == OK);

	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK(analyzer_reports_substring(parser, "specified more than once"));
}

TEST_CASE("[Modules][FoundryScript][CallSiteValidation] Reordered named arguments canonicalize to positional order") {
	FSParser parser;
	const Error error = parser.parse(
			"func target(a: int, b: int, c: int) -> void:\n"
			"\tpass\n"
			"func test() -> void:\n"
			"\ttarget(c = 3, a = 1, b = 2)\n",
			"user://named_reordered.fs",
			false);
	CHECK(error == OK);

	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	const FSParser::CallNode *call = call_site_validation_first_call(parser.get_tree(), "test");
	CHECK(call != nullptr);
	if (call == nullptr) {
		return;
	}
	CHECK(call->arguments.size() == 3);
	CHECK(call->argument_names.is_empty());
	for (int i = 0; i < call->arguments.size(); i++) {
		CHECK(call->arguments[i]->type == FSParser::Node::LITERAL);
		if (call->arguments[i]->type == FSParser::Node::LITERAL) {
			CHECK_EQ(int(static_cast<const FSParser::LiteralNode *>(call->arguments[i])->reduced_value), i + 1);
		}
	}
}

TEST_CASE("[Modules][FoundryScript][CallSiteValidation] Signal connect arity mismatch is diagnosed") {
	FSParser parser;
	const Error error = parser.parse(
			"signal event(value: int)\n"
			"func accept_none() -> void:\n"
			"\tpass\n"
			"func test() -> void:\n"
			"\tvar typed_event: Signal[[int]] = event\n"
			"\ttyped_event.connect(accept_none)\n",
			"user://signal_connect.fs",
			false);
	CHECK(error == OK);

	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK(analyzer_reports_substring(parser, "signal emits"));
}

TEST_CASE("[Modules][FoundryScript][CallSiteValidation] Strict dynamic mode rejects unresolved Callable method names") {
	FSParser parser;
	const Error error = parser.parse(
			"class Worker:\n"
			"\tpass\n"
			"func test() -> void:\n"
			"\tvar worker := Worker()\n"
			"\tCallable(worker, \"missing\")\n",
			"user://strict_callable.fs",
			false);
	CHECK(error == OK);

	FSAnalyzer analyzer(&parser);
	analyzer.set_strict_dynamic_checks(true);
	analyzer.analyze();

	CHECK(analyzer_reports_substring(parser, "Callable construction in strict dynamic mode"));
}

TEST_CASE("[Modules][FoundryScript][CallSiteValidation] A final receiver's unresolved call keeps its own message in strict dynamic mode") {
	// The closed-class rejection is unconditional, so strict mode has nothing to add here and must not
	// shadow the specific diagnosis with its generic one.
	FSParser parser;
	const Error error = parser.parse(
			"final class Worker:\n"
			"\tfunc work() -> void:\n"
			"\t\tpass\n"
			"func test() -> void:\n"
			"\tvar worker := Worker.new()\n"
			"\tworker.perform()\n",
			"user://strict_final_receiver.fs",
			false);
	CHECK(error == OK);

	FSAnalyzer analyzer(&parser);
	analyzer.set_strict_dynamic_checks(true);
	analyzer.analyze();

	CHECK(analyzer_reports_substring(parser, "so no subtype can supply it"));
	CHECK_FALSE(analyzer_reports_substring(parser, "in strict dynamic mode"));
}

TEST_CASE("[Modules][FoundryScript][CallSiteValidation] Strict dynamic mode still rejects an open receiver's unresolved call") {
	// An open class keeps the strict-mode diagnosis: a subtype really could declare the method, so the
	// rejection is a policy choice rather than a fact about the type.
	FSParser parser;
	const Error error = parser.parse(
			"class Worker:\n"
			"\tfunc work() -> void:\n"
			"\t\tpass\n"
			"func test() -> void:\n"
			"\tvar worker := Worker.new()\n"
			"\tworker.perform()\n",
			"user://strict_open_receiver.fs",
			false);
	CHECK(error == OK);

	FSAnalyzer analyzer(&parser);
	analyzer.set_strict_dynamic_checks(true);
	analyzer.analyze();

	CHECK(analyzer_reports_substring(parser, "in strict dynamic mode"));
	CHECK_FALSE(analyzer_reports_substring(parser, "so no subtype can supply it"));
}

} // namespace FSTests
