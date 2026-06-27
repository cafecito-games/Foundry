/**************************************************************************/
/*  test_ios_project_template.h                                           */
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

#include "editor/project_manager/ios_project_template.h"
#include "editor/run/run_target.h"

#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestIOSProjectTemplate {

TEST_CASE("[Editor][IOSProjectTemplate] Bundle id is derived from the project name") {
	CHECK_EQ(IOSProjectTemplate::derive_bundle_identifier("My Game"), "com.example.mygame");
	CHECK_EQ(IOSProjectTemplate::derive_bundle_identifier("New Game Project"), "com.example.newgameproject");
}

TEST_CASE("[Editor][IOSProjectTemplate] Bundle id strips punctuation and uppercase") {
	CHECK_EQ(IOSProjectTemplate::derive_bundle_identifier("Tower-Defense 2!"), "com.example.towerdefense2");
	CHECK_EQ(IOSProjectTemplate::derive_bundle_identifier("  Spaced  Out  "), "com.example.spacedout");
}

TEST_CASE("[Editor][IOSProjectTemplate] Bundle id falls back to 'game' for empty slugs") {
	CHECK_EQ(IOSProjectTemplate::derive_bundle_identifier(""), "com.example.game");
	CHECK_EQ(IOSProjectTemplate::derive_bundle_identifier("!@#$%"), "com.example.game");
}

TEST_CASE("[Editor][IOSProjectTemplate] Default target carries the documented mobile defaults") {
	const RunTarget target = IOSProjectTemplate::make_default_target();
	CHECK_EQ(target.name, "iOS Device");
	CHECK_EQ(target.platform, "ios");
	CHECK_EQ(target.export_preset, "iOS");
	CHECK_EQ(target.device_id, "auto");
	CHECK_EQ(target.signing_mode, "automatic");
	CHECK(target.team_id.is_empty());
}

TEST_CASE("[Editor][IOSProjectTemplate] Seeding writes a runnable iOS export preset") {
	const String dir = TestUtils::get_temp_path("ios_template_preset");
	DirAccess::make_dir_recursive_absolute(dir);

	CHECK_EQ(IOSProjectTemplate::seed(dir, "My Game"), OK);

	const String presets_path = dir.path_join("export_presets.cfg");
	REQUIRE(FileAccess::exists(presets_path));

	Ref<ConfigFile> presets;
	presets.instantiate();
	REQUIRE_EQ(presets->load(presets_path), OK);

	CHECK_EQ(String(presets->get_value("preset.0", "name")), "iOS");
	CHECK_EQ(String(presets->get_value("preset.0", "platform")), "iOS");
	CHECK(bool(presets->get_value("preset.0", "runnable")));
	CHECK_EQ(String(presets->get_value("preset.0", "export_filter")), "all_resources");
	CHECK_EQ(String(presets->get_value("preset.0.options", "application/bundle_identifier")), "com.example.mygame");
	// No signing is attempted at create time: the team id stays empty.
	CHECK_EQ(String(presets->get_value("preset.0.options", "application/app_store_team_id")), "");
}

TEST_CASE("[Editor][IOSProjectTemplate] Seeding writes a single default run target") {
	const String dir = TestUtils::get_temp_path("ios_template_targets");
	DirAccess::make_dir_recursive_absolute(dir);

	CHECK_EQ(IOSProjectTemplate::seed(dir, "My Game"), OK);

	const String targets_path = dir.path_join("run_targets.cfg");
	REQUIRE(FileAccess::exists(targets_path));

	const Vector<RunTarget> targets = RunTarget::load_all(targets_path);
	REQUIRE_EQ(targets.size(), 1);
	CHECK_EQ(targets[0], IOSProjectTemplate::make_default_target());
}

} // namespace TestIOSProjectTemplate

#endif // TOOLS_ENABLED
