/**************************************************************************/
/*  test_editor_run_native.h                                              */
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

#include "editor/run/editor_run_native.h"
#include "editor/run/run_target.h"
#include "editor/run/run_target_platform.h"

#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

#include "tests/test_macros.h"

namespace TestEditorRunNative {

static RunTarget make_ios_target(const String &p_name, const String &p_device_id) {
	RunTarget target;
	target.name = p_name;
	target.platform = "ios";
	target.export_preset = "iOS";
	target.device_id = p_device_id;
	target.signing_mode = "automatic";
	target.team_id = "ABCDE12345";
	return target;
}

static RunTargetDevice make_device(const String &p_id, const String &p_name, ReadinessStep::Status p_badge) {
	RunTargetDevice device;
	device.id = p_id;
	device.name = p_name;
	device.badge = p_badge;
	return device;
}

TEST_CASE("[Editor][RunTarget] Menu model is empty with no targets and no devices") {
	const Vector<RunTargetMenuEntry> entries = EditorRunNative::build_menu_model(Vector<RunTarget>(), HashMap<String, Vector<RunTargetDevice>>());
	CHECK(entries.is_empty());
}

TEST_CASE("[Editor][RunTarget] Menu model pairs a configured target with its connected device") {
	Vector<RunTarget> targets;
	targets.push_back(make_ios_target("My iPhone", "00008110-000000000000000E"));

	HashMap<String, Vector<RunTargetDevice>> devices;
	Vector<RunTargetDevice> ios_devices;
	ios_devices.push_back(make_device("00008110-000000000000000E", "My iPhone", ReadinessStep::Status::OK));
	devices["ios"] = ios_devices;

	const Vector<RunTargetMenuEntry> entries = EditorRunNative::build_menu_model(targets, devices);

	// Only the configured target shows; its device is claimed, so no "set up" row.
	REQUIRE_EQ(entries.size(), 1);
	CHECK_EQ(entries[0].kind, RunTargetMenuEntry::TARGET);
	CHECK_EQ(entries[0].target_name, "My iPhone");
	CHECK_EQ(entries[0].device_id, "00008110-000000000000000E");
	CHECK_EQ(entries[0].badge, ReadinessStep::Status::OK);
	CHECK(entries[0].runnable);
}

TEST_CASE("[Editor][RunTarget] Menu model offers an unconfigured connected device for setup") {
	HashMap<String, Vector<RunTargetDevice>> devices;
	Vector<RunTargetDevice> ios_devices;
	ios_devices.push_back(make_device("00008110-000000000000000E", "My iPhone", ReadinessStep::Status::ACTION_NEEDED));
	devices["ios"] = ios_devices;

	const Vector<RunTargetMenuEntry> entries = EditorRunNative::build_menu_model(Vector<RunTarget>(), devices);

	REQUIRE_EQ(entries.size(), 1);
	CHECK_EQ(entries[0].kind, RunTargetMenuEntry::SETUP_DEVICE);
	CHECK_EQ(entries[0].device_id, "00008110-000000000000000E");
	CHECK_EQ(entries[0].label, "My iPhone");
	CHECK_EQ(entries[0].badge, ReadinessStep::Status::ACTION_NEEDED);
}

TEST_CASE("[Editor][RunTarget] A configured target whose device is gone is shown but not runnable") {
	Vector<RunTarget> targets;
	targets.push_back(make_ios_target("My iPhone", "00008110-000000000000000E"));

	// No devices connected for the platform.
	const Vector<RunTargetMenuEntry> entries = EditorRunNative::build_menu_model(targets, HashMap<String, Vector<RunTargetDevice>>());

	REQUIRE_EQ(entries.size(), 1);
	CHECK_EQ(entries[0].kind, RunTargetMenuEntry::TARGET);
	CHECK_FALSE(entries[0].runnable);
	CHECK_EQ(entries[0].badge, ReadinessStep::Status::BLOCKED);
}

TEST_CASE("[Editor][RunTarget] An auto target binds to the first connected device") {
	Vector<RunTarget> targets;
	targets.push_back(make_ios_target("Auto", "auto"));

	HashMap<String, Vector<RunTargetDevice>> devices;
	Vector<RunTargetDevice> ios_devices;
	ios_devices.push_back(make_device("00008110-000000000000000E", "First iPhone", ReadinessStep::Status::OK));
	ios_devices.push_back(make_device("00008120-000000000000000F", "Second iPad", ReadinessStep::Status::OK));
	devices["ios"] = ios_devices;

	const Vector<RunTargetMenuEntry> entries = EditorRunNative::build_menu_model(targets, devices);

	// The auto target claims the first device; the second is offered for setup.
	REQUIRE_EQ(entries.size(), 2);
	CHECK_EQ(entries[0].kind, RunTargetMenuEntry::TARGET);
	CHECK_EQ(entries[0].device_id, "00008110-000000000000000E");
	CHECK(entries[0].runnable);
	CHECK_EQ(entries[1].kind, RunTargetMenuEntry::SETUP_DEVICE);
	CHECK_EQ(entries[1].device_id, "00008120-000000000000000F");
}

} // namespace TestEditorRunNative

#endif // TOOLS_ENABLED
