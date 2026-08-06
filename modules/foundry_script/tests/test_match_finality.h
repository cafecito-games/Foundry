/**************************************************************************/
/*  test_match_finality.h                                                 */
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

#include "modules/foundry_script/fs_analyzer.h"
#include "modules/foundry_script/fs_parser.h"

#include "tests/test_macros.h"

// A `match` that provably covers its subject's whole domain leaves no fallthrough, so a
// value-returning function needs no trailing unreachable `return`. These tests drive the analyzer end
// to end and inspect emitted diagnostics, resolved return types, and the coverage flag recorded on the
// `MatchNode`, so the finality decision is proven rather than implied.

namespace FSTests {
namespace MatchFinality {

static const char *FLOW_ERROR = "Not all code paths return a value";

static bool has_error_containing(const FSParser &p_parser, const String &p_fragment) {
	for (const FSParser::ParserError &parser_error : p_parser.get_errors()) {
		if (parser_error.message.contains(p_fragment)) {
			return true;
		}
	}
	return false;
}

static bool has_exact_error(const FSParser &p_parser, const String &p_message) {
	for (const FSParser::ParserError &parser_error : p_parser.get_errors()) {
		if (parser_error.message == p_message) {
			return true;
		}
	}
	return false;
}

static const FSParser::FunctionNode *find_function(const FSParser &p_parser, const StringName &p_name) {
	const FSParser::ClassNode *root_class = p_parser.get_tree();
	if (root_class == nullptr || !root_class->has_member(p_name)) {
		return nullptr;
	}
	const FSParser::ClassNode::Member &member = root_class->get_member(p_name);
	if (member.type != FSParser::ClassNode::Member::FUNCTION) {
		return nullptr;
	}
	return member.function;
}

static const FSParser::MatchNode *find_first_match(const FSParser::SuiteNode *p_suite) {
	if (p_suite == nullptr) {
		return nullptr;
	}
	for (const FSParser::Node *statement : p_suite->statements) {
		if (statement != nullptr && statement->type == FSParser::Node::MATCH) {
			return static_cast<const FSParser::MatchNode *>(statement);
		}
	}
	return nullptr;
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] A full unguarded case cover terminates") {
	FSParser parser;
	const String source = R"(
enum Plain:
	Ok(value: String)
	Err(error: int)

func describe(v: Plain) -> String:
	match v:
		Plain.Ok(value):
			return value
		Plain.Err(error):
			return str(error)
)";
	REQUIRE(parser.parse(source, "res://match_finality_full_cover.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK_FALSE(has_error_containing(parser, FLOW_ERROR));
	CHECK(parser.get_errors().is_empty());

	const FSParser::FunctionNode *describe = find_function(parser, SNAME("describe"));
	REQUIRE(describe != nullptr);
	const FSParser::DataType returned = describe->get_datatype();
	CHECK(returned.kind == FSParser::DataType::BUILTIN);
	CHECK(returned.builtin_type == Variant::STRING);
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] A generic union cover terminates") {
	FSParser parser;
	const String source = R"(
enum Res[T]:
	Ok(value: T)
	Err(error: int)

func describe(v: Res[int]) -> String:
	match v:
		Res[int].Ok(value):
			return str(value)
		Res[int].Err(error):
			return str(error)
)";
	REQUIRE(parser.parse(source, "res://match_finality_generic_cover.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK_FALSE(has_error_containing(parser, FLOW_ERROR));
	CHECK(parser.get_errors().is_empty());

	const FSParser::FunctionNode *describe = find_function(parser, SNAME("describe"));
	REQUIRE(describe != nullptr);
	CHECK(describe->get_datatype().builtin_type == Variant::STRING);
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] A missing case names the gap") {
	FSParser parser;
	const String source = R"(
enum Plain:
	Ok(value: String)
	Err(error: int)

func describe(v: Plain) -> String:
	match v:
		Plain.Ok(value):
			return value
)";
	REQUIRE(parser.parse(source, "res://match_finality_missing_case.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK(has_error_containing(parser, R"(does not cover: Err)"));
	CHECK(has_exact_error(parser, R"(Not all code paths return a value. The "match" over "Plain" does not cover: Err.)"));
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] A guarded-only cover does not terminate") {
	FSParser parser;
	const String source = R"(
enum Plain:
	Ok(value: String)
	Err(error: int)

func describe(v: Plain) -> String:
	match v:
		Plain.Ok(value) when value.is_empty():
			return "empty"
		Plain.Err(error) when error > 0:
			return "positive"
)";
	REQUIRE(parser.parse(source, "res://match_finality_guarded.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK(has_error_containing(parser, FLOW_ERROR));
	CHECK(has_exact_error(parser, R"(Not all code paths return a value. The "match" over "Plain" does not cover: Ok, Err.)"));

	const FSParser::FunctionNode *describe = find_function(parser, SNAME("describe"));
	REQUIRE(describe != nullptr);
	const FSParser::MatchNode *match_node = find_first_match(describe->body);
	REQUIRE(match_node != nullptr);
	CHECK_FALSE(match_node->covers_subject_domain);
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] A refutable payload pattern covers nothing") {
	FSParser parser;
	const String source = R"(
enum Plain:
	Ok(value: int)
	Err(error: int)

func describe(v: Plain) -> String:
	match v:
		Plain.Ok(0):
			return "zero"
		Plain.Err(error):
			return str(error)
)";
	REQUIRE(parser.parse(source, "res://match_finality_refutable.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK(has_exact_error(parser, R"(Not all code paths return a value. The "match" over "Plain" does not cover: Ok.)"));
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] A nullable subject requires a null cover") {
	const String uncovered_source = R"(
enum Plain:
	Ok(value: String)
	Err(error: int)

func describe(v: Plain?) -> String:
	match v:
		Plain.Ok(value):
			return value
		Plain.Err(error):
			return str(error)
)";
	FSParser uncovered_parser;
	REQUIRE(uncovered_parser.parse(uncovered_source, "res://match_finality_nullable_open.fs", false) == OK);
	FSAnalyzer uncovered_analyzer(&uncovered_parser);
	uncovered_analyzer.analyze();
	CHECK(has_exact_error(uncovered_parser, R"(Not all code paths return a value. The "match" over "Plain" does not cover: null.)"));

	const String covered_source = R"(
enum Plain:
	Ok(value: String)
	Err(error: int)

func describe(v: Plain?) -> String:
	match v:
		Plain.Ok(value):
			return value
		Plain.Err(error):
			return str(error)
		null:
			return "none"
)";
	FSParser covered_parser;
	REQUIRE(covered_parser.parse(covered_source, "res://match_finality_nullable_covered.fs", false) == OK);
	FSAnalyzer covered_analyzer(&covered_parser);
	covered_analyzer.analyze();
	CHECK_FALSE(has_error_containing(covered_parser, FLOW_ERROR));
	CHECK(covered_parser.get_errors().is_empty());
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] A full bool cover terminates") {
	FSParser parser;
	const String source = R"(
func describe(flag: bool) -> String:
	match flag:
		true:
			return "yes"
		false:
			return "no"
)";
	REQUIRE(parser.parse(source, "res://match_finality_bool.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK_FALSE(has_error_containing(parser, FLOW_ERROR));
	CHECK(parser.get_errors().is_empty());
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] A full plain-enum cover terminates") {
	FSParser parser;
	const String source = R"(
enum Level:
	LOW = 0
	HIGH = 1

func describe(level: Level) -> String:
	match level:
		Level.LOW:
			return "low"
		Level.HIGH:
			return "high"
)";
	REQUIRE(parser.parse(source, "res://match_finality_plain_enum.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK_FALSE(has_error_containing(parser, FLOW_ERROR));
	CHECK(parser.get_errors().is_empty());
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] A wildcard branch still terminates") {
	FSParser parser;
	const String source = R"(
enum Plain:
	Ok(value: String)
	Err(error: int)

func describe(v: Plain) -> String:
	match v:
		Plain.Ok(value):
			return value
		_:
			return "other"
)";
	REQUIRE(parser.parse(source, "res://match_finality_wildcard.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK_FALSE(has_error_containing(parser, FLOW_ERROR));
	CHECK(parser.get_errors().is_empty());
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] Coverage is computed in release builds") {
	// `covers_subject_domain` is written by code compiled outside `#ifdef DEBUG_ENABLED`, so a release
	// build reaches the same flow-finality decision a debug build does.
	FSParser parser;
	const String source = R"(
enum Plain:
	Ok(value: String)
	Err(error: int)

func describe(v: Plain) -> String:
	match v:
		Plain.Ok(value):
			return value
		Plain.Err(error):
			return str(error)
)";
	REQUIRE(parser.parse(source, "res://match_finality_release_flag.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	const FSParser::FunctionNode *describe = find_function(parser, SNAME("describe"));
	REQUIRE(describe != nullptr);
	const FSParser::MatchNode *match_node = find_first_match(describe->body);
	REQUIRE(match_node != nullptr);
	CHECK(match_node->covers_subject_domain);
	CHECK(match_node->uncovered_domain_values.is_empty());
	CHECK(match_node->subject_domain_name == "Plain");
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] A non-match cause keeps the original message") {
	FSParser parser;
	const String source = R"(
func describe(flag: bool) -> String:
	if flag:
		return "yes"
)";
	REQUIRE(parser.parse(source, "res://match_finality_plain_cause.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK(has_exact_error(parser, "Not all code paths return a value."));
}

} // namespace MatchFinality
} // namespace FSTests
