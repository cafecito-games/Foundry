/**************************************************************************/
/*  test_warning_settings_scope.h                                         */
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

#ifdef DEBUG_ENABLED

#include "../fs_analyzer.h"
#include "../fs_parser.h"
#include "../fs_warning.h"
#include "fs_test_warning_settings.h"

#include "core/config/project_settings.h"
#include "tests/test_macros.h"

namespace FSTests {
namespace WarningSettingsScopeTests {

// `var unused = 1` is an unused local, and the enclosing `match` has no wildcard branch over an
// open domain, so one source exercises a `WARN`-default code and an `IGNORE`-default code at once.
static const char *WARNING_SOURCE = R"(
func run(value: int) -> void:
	var unused = 1
	match value:
		1:
			print("one")
)";

static void analyze_source(FSParser &r_parser, const String &p_path) {
	REQUIRE_EQ(r_parser.parse(WARNING_SOURCE, p_path, false), OK);
	FSAnalyzer analyzer(&r_parser);
	analyzer.analyze();
}

TEST_CASE("[Modules][FoundryScript][TestSupport] WarningSettingsScope pins codes to their shipped defaults") {
	const WarningSettingsScope warning_settings;

	FSParser parser;
	analyze_source(parser, "res://warning_settings_scope_defaults.fs");

	CHECK_EQ(count_warnings_with_code(parser, FSWarning::UNUSED_VARIABLE), 1);
	CHECK_EQ(count_warnings_with_code(parser, FSWarning::MATCH_WITHOUT_DEFAULT), 0);
}

TEST_CASE("[Modules][FoundryScript][TestSupport] WarningSettingsScope applies explicit overrides") {
	const WarningSettingsScope warning_settings({ { FSWarning::MATCH_WITHOUT_DEFAULT, FSWarning::WARN } });

	FSParser parser;
	analyze_source(parser, "res://warning_settings_scope_override.fs");

	CHECK_EQ(count_warnings_with_code(parser, FSWarning::MATCH_WITHOUT_DEFAULT), 1);
}

// Snapshots and restores everything `WarningSettingsScope` touches, so a case can install a hostile
// ambient configuration to test the scope against without leaking it into the rest of the process.
class AmbientWarningStateGuard {
	Variant enable;
	Variant directory_rules;
	Variant unused_variable_level;
	bool ignoring_warnings = false;

public:
	AmbientWarningStateGuard() {
		ProjectSettings *settings = ProjectSettings::get_singleton();
		enable = settings->get_setting("debug/foundry_script/warnings/enable", true);
		directory_rules = settings->get_setting("debug/foundry_script/warnings/directory_rules", Dictionary());
		unused_variable_level = settings->get_setting(
				FSWarning::get_setting_path_from_code(FSWarning::UNUSED_VARIABLE),
				FSWarning::get_default_value(FSWarning::UNUSED_VARIABLE));
		ignoring_warnings = FSParser::is_ignoring_warnings();
	}

	~AmbientWarningStateGuard() {
		ProjectSettings *settings = ProjectSettings::get_singleton();
		settings->set_setting("debug/foundry_script/warnings/enable", enable);
		settings->set_setting("debug/foundry_script/warnings/directory_rules", directory_rules);
		settings->set_setting(FSWarning::get_setting_path_from_code(FSWarning::UNUSED_VARIABLE), unused_variable_level);
		FSParser::update_project_settings();
		FSParser::set_ignoring_warnings(ignoring_warnings);
	}
};

TEST_CASE("[Modules][FoundryScript][TestSupport] WarningSettingsScope restores the previous configuration") {
	const AmbientWarningStateGuard ambient;

	ProjectSettings *settings = ProjectSettings::get_singleton();
	const String unused_variable_setting = FSWarning::get_setting_path_from_code(FSWarning::UNUSED_VARIABLE);
	const Dictionary addons_rule({ { "res://addons", FSParser::WarningDirectoryRule::DECISION_EXCLUDE } });

	settings->set_setting("debug/foundry_script/warnings/enable", false);
	settings->set_setting("debug/foundry_script/warnings/directory_rules", addons_rule);
	settings->set_setting(unused_variable_setting, (int)FSWarning::IGNORE);
	FSParser::update_project_settings();
	REQUIRE(FSParser::is_ignoring_warnings());

	{
		const WarningSettingsScope warning_settings;
		CHECK_FALSE(FSParser::is_ignoring_warnings());
		CHECK_EQ((int)GLOBAL_GET(unused_variable_setting), (int)FSWarning::WARN);
		CHECK(Dictionary(GLOBAL_GET("debug/foundry_script/warnings/directory_rules")).is_empty());
	}

	CHECK_FALSE(settings->get_setting("debug/foundry_script/warnings/enable").booleanize());
	CHECK_EQ(Dictionary(settings->get_setting("debug/foundry_script/warnings/directory_rules")).size(), 1);
	CHECK_EQ((int)settings->get_setting(unused_variable_setting), (int)FSWarning::IGNORE);
	// The restore that three of the retired per-file scopes got wrong: it has to happen after
	// `update_project_settings()`, which recomputes the flag from the `enable` setting.
	CHECK(FSParser::is_ignoring_warnings());
}

TEST_CASE("[Modules][FoundryScript][TestSupport] WarningSettingsScope set_level takes effect and is still undone") {
	const AmbientWarningStateGuard ambient;

	const String unused_variable_setting = FSWarning::get_setting_path_from_code(FSWarning::UNUSED_VARIABLE);
	const Variant original_level = ProjectSettings::get_singleton()->get_setting(
			unused_variable_setting, FSWarning::get_default_value(FSWarning::UNUSED_VARIABLE));

	{
		WarningSettingsScope warning_settings;

		FSParser warned_parser;
		analyze_source(warned_parser, "res://warning_settings_scope_set_level_on.fs");
		CHECK_EQ(count_warnings_with_code(warned_parser, FSWarning::UNUSED_VARIABLE), 1);

		warning_settings.set_level(FSWarning::UNUSED_VARIABLE, FSWarning::IGNORE);

		FSParser ignored_parser;
		analyze_source(ignored_parser, "res://warning_settings_scope_set_level_off.fs");
		CHECK_EQ(count_warnings_with_code(ignored_parser, FSWarning::UNUSED_VARIABLE), 0);
	}

	CHECK_EQ((int)ProjectSettings::get_singleton()->get_setting(unused_variable_setting), (int)original_level);
}

TEST_CASE("[Modules][FoundryScript][TestSupport] WarningSettingsScope neutralizes the addons directory rule") {
	const AmbientWarningStateGuard ambient;

	ProjectSettings::get_singleton()->set_setting("debug/foundry_script/warnings/directory_rules",
			Dictionary({ { "res://addons", FSParser::WarningDirectoryRule::DECISION_EXCLUDE } }));
	FSParser::update_project_settings();

	const WarningSettingsScope warning_settings;

	FSParser parser;
	analyze_source(parser, "res://addons/warning_settings_scope_addon.fs");
	CHECK_EQ(count_warnings_with_code(parser, FSWarning::UNUSED_VARIABLE), 1);
}

} // namespace WarningSettingsScopeTests
} // namespace FSTests

#endif // DEBUG_ENABLED
