/**************************************************************************/
/*  fs_test_warning_settings.h                                            */
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

#include "../fs_parser.h"
#include "../fs_warning.h"

#include "core/config/project_settings.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"
#include "tests/test_macros.h"

// Warning configuration is process-global and is never initialized by a test run: `Main::test_setup()`
// does not call `ScriptServer::init_languages()`, so `FSParser::warning_levels` is statically
// zero-initialized to all-`IGNORE` until something populates it, and the `.fs` corpus runner leaves it
// globally forced to `WARN` once it has run. Any test that asserts on `FSParser::get_warnings()` is
// therefore order-dependent unless it pins that state itself.
//
// Three independent gates silence a warning, and a test scope has to own all three:
//   1. `debug/foundry_script/warnings/enable` (drives `FSParser::is_ignoring_warnings()`),
//   2. `debug/foundry_script/warnings/directory_rules` (drives the per-script `res://addons`
//      exclusion applied when the parsed script path matches a rule),
//   3. `debug/foundry_script/warnings/<code>` (the per-code level; an `ERROR` level turns the
//      diagnostic into a parser error that never reaches `get_warnings()`).
//
// Rules for writing a warning-asserting test:
//   - Install a `WarningSettingsScope` in the case. Never poke the settings inline: the ordering
//     between `update_project_settings()` and `set_ignoring_warnings()` is easy to get wrong, and a
//     leaked setting silently changes every later case in the process.
//   - Pair every assertion that a warning is *absent* with a positive control in the same case under
//     the same scope: a source that does produce the code, asserted to produce it. Without the
//     positive control an "is absent" assertion cannot distinguish a correct negative from a process
//     in which warnings were simply off, and it passes vacuously.

namespace FSTests {

struct WarningLevelOverride {
	FSWarning::Code code = FSWarning::WARNING_MAX;
	FSWarning::WarnLevel level = FSWarning::IGNORE;
};

// Pins the process-global Foundry Script warning configuration for the lifetime of the scope and
// restores the previous configuration on destruction.
//
// Must be constructed inside a doctest `TEST_CASE`: the constructor self-checks with `CHECK` that the
// configuration actually took effect, which is only meaningful inside a running test case.
class WarningSettingsScope {
	static constexpr const char *ENABLE_SETTING = "debug/foundry_script/warnings/enable";
	static constexpr const char *DIRECTORY_RULES_SETTING = "debug/foundry_script/warnings/directory_rules";

	Variant previous_enable;
	Variant previous_directory_rules;
	Variant previous_levels[FSWarning::WARNING_MAX];
	bool previous_ignoring_warnings = false;

	static ProjectSettings *settings() { return ProjectSettings::get_singleton(); }

	void install(const Vector<WarningLevelOverride> &p_overrides) {
		previous_ignoring_warnings = FSParser::is_ignoring_warnings();
		previous_enable = settings()->get_setting(ENABLE_SETTING, true);
		previous_directory_rules = settings()->get_setting(DIRECTORY_RULES_SETTING, Dictionary());
		for (int i = 0; i < (int)FSWarning::WARNING_MAX; i++) {
			const String setting_path = FSWarning::get_setting_path_from_code((FSWarning::Code)i);
			previous_levels[i] = settings()->get_setting(setting_path, FSWarning::get_default_value((FSWarning::Code)i));
		}

		settings()->set_setting(ENABLE_SETTING, true);
		// The shipped default excludes `res://addons`, which would silently suppress every warning for
		// a fixture parsed under that prefix. A test states the levels it wants; no path-based rule.
		settings()->set_setting(DIRECTORY_RULES_SETTING, Dictionary());
		for (int i = 0; i < (int)FSWarning::WARNING_MAX; i++) {
			const String setting_path = FSWarning::get_setting_path_from_code((FSWarning::Code)i);
			settings()->set_setting(setting_path, FSWarning::get_default_value((FSWarning::Code)i));
		}
		for (const WarningLevelOverride &level_override : p_overrides) {
			ERR_CONTINUE((int)level_override.code >= (int)FSWarning::WARNING_MAX);
			settings()->set_setting(FSWarning::get_setting_path_from_code(level_override.code), (int)level_override.level);
		}

		FSParser::update_project_settings();
		// `update_project_settings()` derives `is_project_ignoring_warnings` from the `enable` setting,
		// so it must run before this: an explicit set that precedes it is discarded.
		FSParser::set_ignoring_warnings(false);

		CHECK_FALSE(FSParser::is_ignoring_warnings());
		for (const WarningLevelOverride &level_override : p_overrides) {
			CHECK_EQ((int)GLOBAL_GET(FSWarning::get_setting_path_from_code(level_override.code)), (int)level_override.level);
		}
	}

public:
	// Every code at its shipped default (`FSWarning::default_warning_levels`).
	WarningSettingsScope() {
		install(Vector<WarningLevelOverride>());
	}

	// The listed codes at the given levels; every other code at its shipped default.
	explicit WarningSettingsScope(const Vector<WarningLevelOverride> &p_overrides) {
		install(p_overrides);
	}

	// Changes one code mid-case. The destructor still restores the configuration snapshotted at
	// construction, however many times this was called.
	void set_level(FSWarning::Code p_code, FSWarning::WarnLevel p_level) {
		ERR_FAIL_INDEX((int)p_code, (int)FSWarning::WARNING_MAX);
		settings()->set_setting(FSWarning::get_setting_path_from_code(p_code), (int)p_level);
		FSParser::update_project_settings();
		FSParser::set_ignoring_warnings(false);
	}

	~WarningSettingsScope() {
		settings()->set_setting(ENABLE_SETTING, previous_enable);
		settings()->set_setting(DIRECTORY_RULES_SETTING, previous_directory_rules);
		for (int i = 0; i < (int)FSWarning::WARNING_MAX; i++) {
			settings()->set_setting(FSWarning::get_setting_path_from_code((FSWarning::Code)i), previous_levels[i]);
		}
		FSParser::update_project_settings();
		FSParser::set_ignoring_warnings(previous_ignoring_warnings);
	}

	WarningSettingsScope(const WarningSettingsScope &) = delete;
	WarningSettingsScope &operator=(const WarningSettingsScope &) = delete;
};

inline int count_warnings_with_code(const FSParser &p_parser, FSWarning::Code p_code) {
	int count = 0;
	for (const FSWarning &warning : p_parser.get_warnings()) {
		if (warning.code == p_code) {
			count++;
		}
	}
	return count;
}

} // namespace FSTests

#endif // DEBUG_ENABLED
