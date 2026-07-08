/**************************************************************************/
/*  test_analyzer_finalization.h                                          */
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
#include "modules/foundry_script/fs_parser.h"
#include "modules/foundry_script/fs_warning.h"

#include "core/config/project_settings.h"
#include "tests/test_macros.h"

namespace FSTests {

#ifdef DEBUG_ENABLED
class AnalyzerWarningSettingsScope {
	Variant previous_enable;
	HashMap<String, Variant> previous_levels;

public:
	AnalyzerWarningSettingsScope() {
		previous_enable = ProjectSettings::get_singleton()->get_setting("debug/foundry_script/warnings/enable", true);
		ProjectSettings::get_singleton()->set_setting("debug/foundry_script/warnings/enable", true);
		for (int i = 0; i < (int)FSWarning::WARNING_MAX; i++) {
			if (i == FSWarning::UNTYPED_DECLARATION || i == FSWarning::INFERRED_DECLARATION) {
				continue;
			}
			const String setting_path = FSWarning::get_setting_path_from_code((FSWarning::Code)i);
			previous_levels[setting_path] = ProjectSettings::get_singleton()->get_setting(setting_path, (int)FSWarning::WARN);
			ProjectSettings::get_singleton()->set_setting(setting_path, (int)FSWarning::WARN);
		}
		FSParser::update_project_settings();
	}

	~AnalyzerWarningSettingsScope() {
		ProjectSettings::get_singleton()->set_setting("debug/foundry_script/warnings/enable", previous_enable);
		for (const KeyValue<String, Variant> &entry : previous_levels) {
			ProjectSettings::get_singleton()->set_setting(entry.key, entry.value);
		}
		FSParser::update_project_settings();
	}
};
#endif // DEBUG_ENABLED

#ifdef DEBUG_ENABLED
static int count_warnings_with_code(const FSParser &p_parser, FSWarning::Code p_code) {
	int count = 0;
	for (const FSWarning &warning : p_parser.get_warnings()) {
		if (warning.code == p_code) {
			count++;
		}
	}
	return count;
}

TEST_CASE("[Modules][FoundryScript][Analyzer] @warning_ignore suppresses pending warnings after body analysis") {
	AnalyzerWarningSettingsScope warning_settings;

	const char *source = R"(
extends RefCounted

var _a
@warning_ignore("unused_private_class_variable")
var _b
var _d

func test() -> void:
	pass
)";

	FSParser parser;
	Error err = parser.parse(source, "user://analyzer_finalization_warning_ignore.fs", false);
	REQUIRE_EQ(err, OK);

	FSAnalyzer analyzer(&parser);
	REQUIRE_EQ(analyzer.analyze(), OK);

	CHECK_EQ(count_warnings_with_code(parser, FSWarning::UNUSED_PRIVATE_CLASS_VARIABLE), 2);
}

TEST_CASE("[Modules][FoundryScript][Analyzer] non-ignored pending warnings are applied after body analysis") {
	AnalyzerWarningSettingsScope warning_settings;

	const char *source = R"(
extends RefCounted

var _a
var _b

func test() -> void:
	pass
)";

	FSParser parser;
	Error err = parser.parse(source, "user://analyzer_finalization_pending_warnings.fs", false);
	REQUIRE_EQ(err, OK);

	FSAnalyzer analyzer(&parser);
	REQUIRE_EQ(analyzer.analyze(), OK);

	CHECK_EQ(count_warnings_with_code(parser, FSWarning::UNUSED_PRIVATE_CLASS_VARIABLE), 2);
}
#endif // DEBUG_ENABLED

TEST_CASE("[Modules][FoundryScript][Analyzer] resolve_body return status follows parser errors after warning finalization") {
	const char *source = R"(
extends RefCounted

func broken() -> int:
	return missing_symbol
)";

	FSParser parser;
	Error err = parser.parse(source, "user://analyzer_finalization_resolve_body_status.fs", false);
	REQUIRE_EQ(err, OK);

	FSAnalyzer analyzer(&parser);
	REQUIRE_EQ(analyzer.resolve_inheritance(), OK);
	REQUIRE_EQ(analyzer.resolve_interface(), OK);

	err = analyzer.resolve_body();
	CHECK_EQ(err, ERR_PARSE_ERROR);
	CHECK_FALSE(parser.get_errors().is_empty());
}

TEST_CASE("[Modules][FoundryScript][Analyzer] analyze return status follows parser errors after finalization") {
	const char *source = R"(
extends RefCounted

func broken() -> int:
	return missing_symbol
)";

	FSParser parser;
	Error err = parser.parse(source, "user://analyzer_finalization_analyze_status.fs", false);
	REQUIRE_EQ(err, OK);

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	CHECK_EQ(err, ERR_PARSE_ERROR);
	CHECK_FALSE(parser.get_errors().is_empty());
}

} // namespace FSTests
