/**************************************************************************/
/*  test_run_target.h                                                     */
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

#include "editor/run/run_target.h"

#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestRunTarget {

static RunTarget make_target(const String &p_name) {
	RunTarget target;
	target.name = p_name;
	target.platform = "ios";
	target.export_preset = "iOS";
	target.device_id = "auto";
	target.signing_mode = "automatic";
	target.team_id = "ABCDE12345";
	return target;
}

TEST_CASE("[Editor][RunTarget] Missing file loads as an empty list without error") {
	const String path = TestUtils::get_temp_path("run_targets_missing.cfg");
	// Make sure no stale file is present from a previous run.
	if (FileAccess::exists(path)) {
		DirAccess::remove_absolute(path);
	}

	Error error = FAILED;
	const Vector<RunTarget> targets = RunTarget::load_all(path, &error);

	CHECK(targets.is_empty());
	CHECK_EQ(error, OK);
}

TEST_CASE("[Editor][RunTarget] Empty target list round-trips to an empty list") {
	const String path = TestUtils::get_temp_path("run_targets_empty.cfg");

	const Error save_error = RunTarget::save_all(path, Vector<RunTarget>());
	CHECK_EQ(save_error, OK);

	Error load_error = FAILED;
	const Vector<RunTarget> targets = RunTarget::load_all(path, &load_error);
	CHECK(targets.is_empty());
	CHECK_EQ(load_error, OK);
}

TEST_CASE("[Editor][RunTarget] Two targets round-trip with no data loss") {
	const String path = TestUtils::get_temp_path("run_targets_two.cfg");

	Vector<RunTarget> originals;
	RunTarget first = make_target("My iPhone");
	first.device_id = "00008110-000000000000000E";
	RunTarget second = make_target("My iPad");
	second.export_preset = "iPad";
	second.team_id = "ZZZZZ99999";
	originals.push_back(first);
	originals.push_back(second);

	CHECK_EQ(RunTarget::save_all(path, originals), OK);

	const Vector<RunTarget> loaded = RunTarget::load_all(path);
	REQUIRE_EQ(loaded.size(), 2);

	CHECK_EQ(loaded[0], first);
	CHECK_EQ(loaded[1], second);

	// Spot-check individual fields so a broken equality operator can't mask a regression.
	CHECK_EQ(loaded[0].name, "My iPhone");
	CHECK_EQ(loaded[0].platform, "ios");
	CHECK_EQ(loaded[0].export_preset, "iOS");
	CHECK_EQ(loaded[0].device_id, "00008110-000000000000000E");
	CHECK_EQ(loaded[0].signing_mode, "automatic");
	CHECK_EQ(loaded[0].team_id, "ABCDE12345");
	CHECK_EQ(loaded[1].export_preset, "iPad");
	CHECK_EQ(loaded[1].team_id, "ZZZZZ99999");
}

TEST_CASE("[Editor][RunTarget] Unknown keys are preserved across a save") {
	const String path = TestUtils::get_temp_path("run_targets_unknown.cfg");

	// Author a config the way a newer editor (with extra fields) would.
	{
		Ref<ConfigFile> config;
		config.instantiate();
		config->set_value("target.0", "name", "Future iPhone");
		config->set_value("target.0", "platform", "ios");
		config->set_value("target.0", "export_preset", "iOS");
		config->set_value("target.0", "device_id", "auto");
		config->set_value("target.0", "signing_mode", "automatic");
		config->set_value("target.0", "team_id", "ABCDE12345");
		config->set_value("target.0", "future_string", "keep me");
		config->set_value("target.0", "future_number", 42);
		CHECK_EQ(config->save(path), OK);
	}

	const Vector<RunTarget> loaded = RunTarget::load_all(path);
	REQUIRE_EQ(loaded.size(), 1);
	CHECK_EQ(loaded[0].extra_keys.size(), 2);
	CHECK_EQ(String(loaded[0].extra_keys["future_string"]), "keep me");
	CHECK_EQ(int(loaded[0].extra_keys["future_number"]), 42);

	// Re-saving must not drop the unknown keys.
	CHECK_EQ(RunTarget::save_all(path, loaded), OK);

	Ref<ConfigFile> reread;
	reread.instantiate();
	REQUIRE_EQ(reread->load(path), OK);
	CHECK(reread->has_section_key("target.0", "future_string"));
	CHECK(reread->has_section_key("target.0", "future_number"));
	CHECK_EQ(String(reread->get_value("target.0", "future_string")), "keep me");
	CHECK_EQ(int(reread->get_value("target.0", "future_number")), 42);

	// And a full reload still equals what we loaded the first time.
	const Vector<RunTarget> reloaded = RunTarget::load_all(path);
	REQUIRE_EQ(reloaded.size(), 1);
	CHECK_EQ(reloaded[0], loaded[0]);
}

} // namespace TestRunTarget

#endif // TOOLS_ENABLED
