/**************************************************************************/
/*  test_analyzer_phases.h                                                */
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

#include "modules/foundry_script/fs_analyzer.h"
#include "modules/foundry_script/fs_cache.h"
#include "modules/foundry_script/fs_parser.h"

#include "core/io/file_access.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace FSTests {

static String write_temp_foundry_script(const String &p_file_name, const String &p_source) {
	const String path = TestUtils::get_temp_path(p_file_name);
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	CHECK(file.is_valid());
	if (file.is_valid()) {
		file->store_string(p_source);
	}
	return path;
}

TEST_CASE("[Modules][FoundryScript][Analyzer] incremental public phase sequence") {
	const char *source = R"(
class_name PhaseDemo
extends RefCounted

var value: int = 1

func get_value() -> int:
	return value
)";

	FSParser parser;
	Error err = parser.parse(source, "user://analyzer_phase_demo.fs", false);
	REQUIRE_EQ(err, OK);

	const FSParser::ClassNode *head = parser.get_tree();
	REQUIRE(head != nullptr);

	FSAnalyzer analyzer(&parser);

	err = analyzer.resolve_inheritance();
	CHECK_EQ(err, OK);
	CHECK_FALSE(head->base_type.has_no_type());
	CHECK_FALSE(head->resolved_interface);
	CHECK_FALSE(head->resolved_body);

	err = analyzer.resolve_interface();
	CHECK_EQ(err, OK);
	CHECK(head->resolved_interface);
	CHECK_FALSE(head->resolved_body);

	err = analyzer.resolve_body();
	CHECK_EQ(err, OK);
	CHECK(head->resolved_body);
}

TEST_CASE("[Modules][FoundryScript][Analyzer] FSParserRef status maps to analyzer public phases") {
	const String path = write_temp_foundry_script("analyzer_parser_ref_phases.fs", R"(
extends RefCounted

func run() -> int:
	return 1
)");

	FSCache::remove_parser(path);

	Error cache_err = OK;
	Ref<FSParserRef> parser_ref = FSCache::get_parser(path, FSParserRef::EMPTY, cache_err);
	REQUIRE(parser_ref.is_valid());
	CHECK_EQ(cache_err, OK);
	CHECK_EQ(parser_ref->get_status(), FSParserRef::EMPTY);

	CHECK_EQ(parser_ref->raise_status(FSParserRef::PARSED), OK);
	CHECK_EQ(parser_ref->get_status(), FSParserRef::PARSED);

	CHECK_EQ(parser_ref->raise_status(FSParserRef::INHERITANCE_SOLVED), OK);
	CHECK_EQ(parser_ref->get_status(), FSParserRef::INHERITANCE_SOLVED);
	const FSParser::ClassNode *head = parser_ref->get_parser()->get_tree();
	REQUIRE(head != nullptr);
	CHECK_FALSE(head->base_type.has_no_type());

	CHECK_EQ(parser_ref->raise_status(FSParserRef::INTERFACE_SOLVED), OK);
	CHECK_EQ(parser_ref->get_status(), FSParserRef::INTERFACE_SOLVED);
	CHECK(head->resolved_interface);

	CHECK_EQ(parser_ref->raise_status(FSParserRef::FULLY_SOLVED), OK);
	CHECK_EQ(parser_ref->get_status(), FSParserRef::FULLY_SOLVED);
	CHECK(head->resolved_body);

	FSCache::remove_parser(path);
}

TEST_CASE("[Modules][FoundryScript][Analyzer] phase order guard reports required violation details") {
	FSParser parser;
	REQUIRE_EQ(parser.parse("extends RefCounted\n", "user://analyzer_phase_order.fs", false), OK);

	FSAnalyzer analyzer(&parser);
	CHECK_EQ(analyzer.test_get_highest_completed_phase(), FSAnalyzer::AnalyzerPhase::NONE);

	const FSAnalyzer::AnalyzerPhase requested = FSAnalyzer::AnalyzerPhase::BODY_EXPRESSION_CALLABLE_SIGNAL;
	const FSAnalyzer::AnalyzerPhase required = FSAnalyzer::AnalyzerPhase::INTERFACE_AND_MEMBER_SURFACE;

	CHECK(analyzer.test_would_violate_phase_order(requested, required));

	const String message = analyzer.test_format_phase_order_violation(requested, required);
	CHECK(message.contains("FSAnalyzer phase order violation"));
	CHECK(message.contains("body_expression_callable_signal"));
	CHECK(message.contains("interface_and_member_surface"));
	CHECK(message.contains("none"));
	CHECK(message.contains("user://analyzer_phase_order.fs"));

	analyzer.test_mark_analyzer_phase_completed(FSAnalyzer::AnalyzerPhase::INTERFACE_AND_MEMBER_SURFACE);
	CHECK_FALSE(analyzer.test_would_violate_phase_order(requested, required));
}

} // namespace FSTests
