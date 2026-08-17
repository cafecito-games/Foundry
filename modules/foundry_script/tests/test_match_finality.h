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

static const char *OPEN_ENUM_ERROR = R"(Not all code paths return a value. The "match" over "Level" leaves the undeclared values of its integer carrier unhandled; add an unguarded "_" or bind branch.)";

TEST_CASE("[Modules][FoundryScript][MatchFinality] A full plain-enum cover does not terminate") {
	// A plain enum is carried by an integer that accepts undeclared values, so handling every declared
	// member leaves a live no-match path.
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

	CHECK(has_exact_error(parser, OPEN_ENUM_ERROR));

	const FSParser::FunctionNode *describe = find_function(parser, SNAME("describe"));
	REQUIRE(describe != nullptr);
	const FSParser::MatchNode *match_node = find_first_match(describe->body);
	REQUIRE(match_node != nullptr);
	CHECK_FALSE(match_node->covers_subject_domain);
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] Explicit undeclared integers do not close a plain-enum match") {
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
		99:
			return "ninety-nine"
)";
	REQUIRE(parser.parse(source, "res://match_finality_plain_enum_extra_integer.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK(has_exact_error(parser, OPEN_ENUM_ERROR));

	const FSParser::FunctionNode *describe = find_function(parser, SNAME("describe"));
	REQUIRE(describe != nullptr);
	const FSParser::MatchNode *match_node = find_first_match(describe->body);
	REQUIRE(match_node != nullptr);
	CHECK_FALSE(match_node->covers_subject_domain);
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] A guarded catch-all does not close a plain-enum match") {
	FSParser parser;
	const String source = R"(
enum Level:
	LOW = 0
	HIGH = 1

func describe(level: Level, allow: bool) -> String:
	match level:
		Level.LOW:
			return "low"
		Level.HIGH:
			return "high"
		_ when allow:
			return "other"
)";
	REQUIRE(parser.parse(source, "res://match_finality_plain_enum_guarded.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK(has_exact_error(parser, OPEN_ENUM_ERROR));

	const FSParser::FunctionNode *describe = find_function(parser, SNAME("describe"));
	REQUIRE(describe != nullptr);
	const FSParser::MatchNode *match_node = find_first_match(describe->body);
	REQUIRE(match_node != nullptr);
	CHECK_FALSE(match_node->covers_subject_domain);
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] An unguarded catch-all closes a plain-enum match") {
	FSParser wildcard_parser;
	const String wildcard_source = R"(
enum Level:
	LOW = 0
	HIGH = 1

func describe(level: Level) -> String:
	match level:
		Level.LOW:
			return "low"
		_:
			return "other"
)";
	REQUIRE(wildcard_parser.parse(wildcard_source, "res://match_finality_plain_enum_wildcard.fs", false) == OK);
	FSAnalyzer wildcard_analyzer(&wildcard_parser);
	wildcard_analyzer.analyze();
	CHECK(wildcard_parser.get_errors().is_empty());

	FSParser bind_parser;
	const String bind_source = R"(
enum Level:
	LOW = 0
	HIGH = 1

func describe(level: Level) -> String:
	match level:
		Level.LOW:
			return "low"
		var other:
			return str(other)
)";
	REQUIRE(bind_parser.parse(bind_source, "res://match_finality_plain_enum_bind.fs", false) == OK);
	FSAnalyzer bind_analyzer(&bind_parser);
	bind_analyzer.analyze();
	CHECK(bind_parser.get_errors().is_empty());

	const FSParser::FunctionNode *describe = find_function(bind_parser, SNAME("describe"));
	REQUIRE(describe != nullptr);
	const FSParser::MatchNode *match_node = find_first_match(describe->body);
	REQUIRE(match_node != nullptr);
	CHECK(match_node->covers_subject_domain);
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] A Variant test closes a plain-enum match") {
	// `is Variant` accepts every value, undeclared carrier integers included, so it is the one type
	// test that closes an open plain-enum domain.
	FSParser parser;
	const String source = R"(
enum Level:
	LOW = 0
	HIGH = 1

func describe(level: Level) -> String:
	match level:
		level is Variant:
			return str(level)
)";
	REQUIRE(parser.parse(source, "res://match_finality_plain_enum_variant.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK(parser.get_errors().is_empty());

	const FSParser::FunctionNode *describe = find_function(parser, SNAME("describe"));
	REQUIRE(describe != nullptr);
	const FSParser::MatchNode *match_node = find_first_match(describe->body);
	REQUIRE(match_node != nullptr);
	CHECK(match_node->covers_subject_domain);
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] A plain-enum match keeps the no-match path for definite assignment") {
	// Definite assignment must see the same open domain flow finality does: without an unguarded
	// catch-all, a blank final assigned in every member branch is still not definitely assigned.
	FSParser parser;
	const String source = R"(
enum Level:
	LOW = 0
	HIGH = 1

final var label: String

func _init(level: Level) -> void:
	match level:
		level is Level:
			label = "declared"
)";
	REQUIRE(parser.parse(source, "res://match_finality_plain_enum_definite_assignment.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK(has_error_containing(parser, R"(Final variable "label" must be definitely assigned)"));

	FSParser covered_parser;
	const String covered_source = R"(
enum Level:
	LOW = 0
	HIGH = 1

final var label: String

func _init(level: Level) -> void:
	match level:
		level is Level:
			label = "declared"
		_:
			label = "undeclared"
)";
	REQUIRE(covered_parser.parse(covered_source, "res://match_finality_plain_enum_definite_assignment_covered.fs", false) == OK);
	FSAnalyzer covered_analyzer(&covered_parser);
	covered_analyzer.analyze();
	CHECK(covered_parser.get_errors().is_empty());
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] A full-carrier test closes a plain-enum match") {
	// The carrier is what makes a plain enum open, so a test that admits the whole carrier admits the
	// undeclared values too and leaves no fallthrough, in a value-returning and in a void function.
	FSParser parser;
	const String source = R"(
enum Level:
	LOW = 0
	HIGH = 1

func describe(level: Level) -> String:
	match level:
		level is long:
			return str(level)

func record(level: Level) -> void:
	match level:
		level is long:
			print(level)
)";
	REQUIRE(parser.parse(source, "res://match_finality_plain_enum_carrier.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK(parser.get_errors().is_empty());

	const FSParser::FunctionNode *describe = find_function(parser, SNAME("describe"));
	REQUIRE(describe != nullptr);
	const FSParser::MatchNode *match_node = find_first_match(describe->body);
	REQUIRE(match_node != nullptr);
	CHECK(match_node->covers_subject_domain);

	const FSParser::FunctionNode *record = find_function(parser, SNAME("record"));
	REQUIRE(record != nullptr);
	const FSParser::MatchNode *void_match = find_first_match(record->body);
	REQUIRE(void_match != nullptr);
	CHECK(void_match->covers_subject_domain);
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] An enum-type test does not close a plain-enum match") {
	// `value is Level` is a membership test over the declared values, so the undeclared carrier values
	// still fall through even though the carrier test on the same subject does not.
	FSParser parser;
	const String source = R"(
enum Level:
	LOW = 0
	HIGH = 1

func describe(level: Level) -> String:
	match level:
		level is Level:
			return str(level)
)";
	REQUIRE(parser.parse(source, "res://match_finality_plain_enum_own_type.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK(has_exact_error(parser, OPEN_ENUM_ERROR));

	const FSParser::FunctionNode *describe = find_function(parser, SNAME("describe"));
	REQUIRE(describe != nullptr);
	const FSParser::MatchNode *match_node = find_first_match(describe->body);
	REQUIRE(match_node != nullptr);
	CHECK_FALSE(match_node->covers_subject_domain);
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] A narrower carrier test does not close a plain-enum match") {
	// `int` is a 32-bit range test over the 64-bit carrier, so values the slot can hold still escape.
	FSParser parser;
	const String source = R"(
enum Level:
	LOW = 0
	HIGH = 1

func describe(level: Level) -> String:
	match level:
		level is int:
			return str(level)
)";
	REQUIRE(parser.parse(source, "res://match_finality_plain_enum_narrow_carrier.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK(has_exact_error(parser, OPEN_ENUM_ERROR));

	const FSParser::FunctionNode *describe = find_function(parser, SNAME("describe"));
	REQUIRE(describe != nullptr);
	const FSParser::MatchNode *match_node = find_first_match(describe->body);
	REQUIRE(match_node != nullptr);
	CHECK_FALSE(match_node->covers_subject_domain);
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] A full-carrier test satisfies definite assignment") {
	// Definite assignment reads the same coverage decision flow finality does, so both answers agree.
	FSParser parser;
	const String source = R"(
enum Level:
	LOW = 0
	HIGH = 1

final var label: String

func _init(level: Level) -> void:
	match level:
		level is long:
			label = str(level)
)";
	REQUIRE(parser.parse(source, "res://match_finality_plain_enum_carrier_definite_assignment.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK(parser.get_errors().is_empty());

	FSParser narrow_parser;
	const String narrow_source = R"(
enum Level:
	LOW = 0
	HIGH = 1

final var label: String

func _init(level: Level) -> void:
	match level:
		level is int:
			label = str(level)
)";
	REQUIRE(narrow_parser.parse(narrow_source, "res://match_finality_plain_enum_narrow_carrier_definite_assignment.fs", false) == OK);
	FSAnalyzer narrow_analyzer(&narrow_parser);
	narrow_analyzer.analyze();
	CHECK(has_error_containing(narrow_parser, R"(Final variable "label" must be definitely assigned)"));
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

TEST_CASE("[Modules][FoundryScript][MatchFinality] A same-subject type test over the whole domain terminates") {
	FSParser parser;
	const String source = R"(
enum Plain:
	Ok(value: String)
	Err(error: int)

func from_bool(value: bool) -> String:
	match value:
		value is bool:
			return str(value)

func from_union(value: Plain) -> String:
	match value:
		value is Plain:
			return "plain"

func from_variant(value: int) -> String:
	match value:
		value is Variant:
			return str(value)
)";
	REQUIRE(parser.parse(source, "res://match_finality_is_domain.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK_FALSE(has_error_containing(parser, FLOW_ERROR));
	CHECK(parser.get_errors().is_empty());

	const StringName function_names[] = { SNAME("from_bool"), SNAME("from_union"), SNAME("from_variant") };
	for (const StringName &function_name : function_names) {
		const FSParser::FunctionNode *function = find_function(parser, function_name);
		REQUIRE(function != nullptr);
		const FSParser::MatchNode *match_node = find_first_match(function->body);
		REQUIRE(match_node != nullptr);
		CHECK(match_node->covers_subject_domain);
	}
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] Both spellings of a full enum cover agree") {
	// An enum-typed slot can hold an integer outside the declared set, which no branch of either match
	// selects at run time; both then fall through and return null. Testing the enum type and listing
	// every declared member must therefore reach the same coverage decision -- both open -- or the same
	// program would compile under one spelling and not the other.
	FSParser parser;
	const String source = R"(
enum Level:
	LOW = 1
	HIGH = 2

func by_values(value: Level) -> String:
	match value:
		Level.LOW:
			return "low"
		Level.HIGH:
			return "high"

func by_type(value: Level) -> String:
	match value:
		value is Level:
			return "type"
)";
	REQUIRE(parser.parse(source, "res://match_finality_enum_spellings.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK(has_exact_error(parser, OPEN_ENUM_ERROR));

	const FSParser::FunctionNode *by_values = find_function(parser, SNAME("by_values"));
	const FSParser::FunctionNode *by_type = find_function(parser, SNAME("by_type"));
	REQUIRE(by_values != nullptr);
	REQUIRE(by_type != nullptr);
	const FSParser::MatchNode *values_match = find_first_match(by_values->body);
	const FSParser::MatchNode *type_match = find_first_match(by_type->body);
	REQUIRE(values_match != nullptr);
	REQUIRE(type_match != nullptr);
	CHECK_FALSE(values_match->covers_subject_domain);
	CHECK_FALSE(type_match->covers_subject_domain);
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] A partial same-subject type test does not terminate") {
	FSParser parser;
	const String source = R"(
type IntOrString = int | String

enum Plain:
	Ok(value: String)
	Err(error: int)

func from_union(value: IntOrString) -> String:
	match value:
		value is int:
			return "int"

func from_case(value: Plain) -> String:
	match value:
		value is Plain.Ok:
			return "ok"

func from_nullable(value: bool?) -> String:
	match value:
		value is bool:
			return str(value)

func from_guard(value: bool) -> String:
	match value:
		value is bool when value:
			return "true"
)";
	REQUIRE(parser.parse(source, "res://match_finality_is_partial.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK(has_error_containing(parser, FLOW_ERROR));

	const StringName function_names[] = { SNAME("from_union"), SNAME("from_case"), SNAME("from_nullable"), SNAME("from_guard") };
	for (const StringName &function_name : function_names) {
		const FSParser::FunctionNode *function = find_function(parser, function_name);
		REQUIRE(function != nullptr);
		const FSParser::MatchNode *match_node = find_first_match(function->body);
		REQUIRE(match_node != nullptr);
		CHECK_FALSE(match_node->covers_subject_domain);
	}
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] A same-named unrelated enum does not cover") {
	// Two enums can share a simple name, and a soft subject type is downgraded rather than reported
	// when a test is incompatible with it, so neither the name nor the recorded subject type on its
	// own proves the test always passes.
	FSParser parser;
	const String source = R"(
class Inner:
	enum Level:
		LOW = 1
		HIGH = 2

enum Level:
	LOW = 1
	HIGH = 2

func describe() -> String:
	var value = Level.LOW
	match value:
		value is Inner.Level:
			return "inner"
)";
	REQUIRE(parser.parse(source, "res://match_finality_same_named_enum.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK(has_error_containing(parser, FLOW_ERROR));

	const FSParser::FunctionNode *describe = find_function(parser, SNAME("describe"));
	REQUIRE(describe != nullptr);
	const FSParser::MatchNode *match_node = find_first_match(describe->body);
	REQUIRE(match_node != nullptr);
	CHECK_FALSE(match_node->covers_subject_domain);
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] An open-domain subject is not covered by its own type") {
	// Coverage is only decided where the subject's domain is statically enumerable. Proving that a
	// test on an open domain accepts every value it can hold is the residual-set modeling match-arm
	// analysis still defers, so such a test contributes no coverage.
	FSParser parser;
	const String source = R"(
func describe(value: String) -> String:
	match value:
		value is String:
			return value
)";
	REQUIRE(parser.parse(source, "res://match_finality_is_open_domain.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK(has_error_containing(parser, FLOW_ERROR));

	const FSParser::FunctionNode *describe = find_function(parser, SNAME("describe"));
	REQUIRE(describe != nullptr);
	const FSParser::MatchNode *match_node = find_first_match(describe->body);
	REQUIRE(match_node != nullptr);
	CHECK_FALSE(match_node->covers_subject_domain);
}

TEST_CASE("[Modules][FoundryScript][MatchFinality] A covering type test leaves a later wildcard reachable") {
	// The unreachable-pattern warning is syntactic and keyed on a wildcard branch, so making a type
	// test count as coverage must not turn a trailing `_` arm into a diagnostic.
	FSParser parser;
	const String source = R"(
func describe(value: bool) -> String:
	match value:
		value is bool:
			return str(value)
		_:
			return "other"
)";
	REQUIRE(parser.parse(source, "res://match_finality_is_then_wildcard.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	CHECK(parser.get_errors().is_empty());
#ifdef DEBUG_ENABLED
	for (const FSWarning &warning : parser.get_warnings()) {
		CHECK(warning.code != FSWarning::UNREACHABLE_PATTERN);
	}
#endif // DEBUG_ENABLED
}

} // namespace MatchFinality
} // namespace FSTests
