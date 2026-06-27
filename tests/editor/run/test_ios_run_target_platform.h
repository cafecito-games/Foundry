/**************************************************************************/
/*  test_ios_run_target_platform.h                                        */
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

#include "editor/run/ios_run_target_platform.h"
#include "editor/run/run_target.h"
#include "editor/run/run_target_readiness.h"

#include "editor/export/editor_export_platform.h"

#include "tests/test_macros.h"

namespace TestIOSRunTargetPlatform {

// A `CommandRunner` test double that returns canned `xcode-select` and `xcrun
// devicectl` output, so the iOS adapter's readiness probing and device
// enumeration are exercised without Xcode or a connected device.
class FakeCommandRunner : public CommandRunner {
public:
	Error xcode_select_error = OK;
	int xcode_select_exit = 0;
	String xcode_select_output = "/Applications/Xcode.app/Contents/Developer";

	Error devicectl_error = OK;
	int devicectl_exit = 0;
	String devicectl_output;

	int xcode_select_calls = 0;
	int devicectl_calls = 0;

	virtual CommandResult run(const String &p_program, const List<String> &p_arguments) override {
		CommandResult result;
		if (p_program == "xcode-select") {
			xcode_select_calls++;
			result.error = xcode_select_error;
			result.exit_code = xcode_select_exit;
			result.output = xcode_select_output;
		} else if (p_program == "xcrun") {
			devicectl_calls++;
			result.error = devicectl_error;
			result.exit_code = devicectl_exit;
			result.output = devicectl_output;
		}
		return result;
	}
};

// Builds a raw `xcrun devicectl list devices -j` payload wrapping one device.
static String make_devicectl_json(const String &p_id, const String &p_name, const String &p_pairing, const String &p_developer_mode, const String &p_transport = "wired") {
	return vformat(
			"{\"result\":{\"devices\":[{\"identifier\":\"%s\",\"connectionProperties\":{\"pairingState\":\"%s\",\"transportType\":\"%s\"},\"deviceProperties\":{\"name\":\"%s\",\"developerModeStatus\":\"%s\"}}]}}",
			p_id, p_pairing, p_transport, p_name, p_developer_mode);
}

static RunTarget make_ios_target() {
	RunTarget target;
	target.name = "My iPhone";
	target.platform = "ios";
	target.export_preset = "iOS";
	target.device_id = "auto";
	target.signing_mode = "automatic";
	target.team_id = "ABCDE12345";
	return target;
}

static ReadinessStep step_by_id(const Vector<ReadinessStep> &p_steps, const char *p_id) {
	for (const ReadinessStep &step : p_steps) {
		if (step.id == StringName(p_id)) {
			return step;
		}
	}
	return ReadinessStep();
}

TEST_CASE("[Editor][IOSRunTarget] Devicectl JSON parses into the shared device list") {
	const String json =
			"{\"result\":{\"devices\":["
			"{\"identifier\":\"phone-A\",\"connectionProperties\":{\"pairingState\":\"paired\",\"transportType\":\"localNetwork\"},\"deviceProperties\":{\"name\":\"My iPhone\",\"developerModeStatus\":\"enabled\"}},"
			"{\"identifier\":\"pad-B\",\"connectionProperties\":{\"pairingState\":\"paired\",\"transportType\":\"wired\"},\"deviceProperties\":{\"name\":\"My iPad\",\"developerModeStatus\":\"disabled\"}}"
			"]}}";

	const Vector<IOSRunTargetPlatform::DeviceInfo> devices = IOSRunTargetPlatform::parse_devicectl_devices(json);
	REQUIRE_EQ(devices.size(), 2);

	CHECK_EQ(devices[0].id, "phone-A");
	CHECK_EQ(devices[0].name, "My iPhone");
	CHECK(devices[0].wifi);
	CHECK(devices[0].paired);
	CHECK(devices[0].developer_mode);

	CHECK_EQ(devices[1].id, "pad-B");
	CHECK_FALSE(devices[1].wifi);
	CHECK(devices[1].paired);
	CHECK_FALSE(devices[1].developer_mode);
}

TEST_CASE("[Editor][IOSRunTarget] Malformed devicectl output yields no devices") {
	CHECK(IOSRunTargetPlatform::parse_devicectl_devices("").is_empty());
	CHECK(IOSRunTargetPlatform::parse_devicectl_devices("not json").is_empty());
	CHECK(IOSRunTargetPlatform::parse_devicectl_devices("{\"result\":{}}").is_empty());
}

TEST_CASE("[Editor][IOSRunTarget] list_devices surfaces only runnable devices") {
	FakeCommandRunner runner;
	runner.devicectl_output =
			"{\"result\":{\"devices\":["
			"{\"identifier\":\"phone-A\",\"connectionProperties\":{\"pairingState\":\"paired\",\"transportType\":\"wired\"},\"deviceProperties\":{\"name\":\"Ready iPhone\",\"developerModeStatus\":\"enabled\"}},"
			"{\"identifier\":\"pad-B\",\"connectionProperties\":{\"pairingState\":\"paired\",\"transportType\":\"wired\"},\"deviceProperties\":{\"name\":\"DevMode-off iPad\",\"developerModeStatus\":\"disabled\"}},"
			"{\"identifier\":\"phone-C\",\"connectionProperties\":{\"pairingState\":\"unpaired\",\"transportType\":\"wired\"},\"deviceProperties\":{\"name\":\"Untrusted iPhone\",\"developerModeStatus\":\"enabled\"}}"
			"]}}";

	IOSRunTargetPlatform adapter(&runner);
	const Vector<RunTargetDevice> devices = adapter.list_devices();

	REQUIRE_EQ(devices.size(), 1);
	CHECK_EQ(devices[0].id, "phone-A");
	CHECK_EQ(devices[0].name, "Ready iPhone");
	CHECK_EQ(devices[0].badge, ReadinessStep::OK);
}

TEST_CASE("[Editor][IOSRunTarget] list_devices returns empty when devicectl fails") {
	FakeCommandRunner runner;
	runner.devicectl_exit = 1; // Tool ran but reported failure.
	runner.devicectl_output = "";

	IOSRunTargetPlatform adapter(&runner);
	CHECK(adapter.list_devices().is_empty());
}

TEST_CASE("[Editor][IOSRunTarget] probe_readiness reports all steps OK for a ready device") {
	FakeCommandRunner runner;
	runner.devicectl_output = make_devicectl_json("phone-A", "My iPhone", "paired", "enabled");

	IOSRunTargetPlatform adapter(&runner);
	RunTarget target = make_ios_target();
	target.device_id = "phone-A";

	const Vector<ReadinessStep> steps = adapter.probe_readiness(target);
	REQUIRE_EQ(steps.size(), 6);
	for (const ReadinessStep &step : steps) {
		CHECK_EQ(step.status, ReadinessStep::OK);
	}
	// The probe actually consulted both shell seams.
	CHECK(runner.xcode_select_calls > 0);
	CHECK(runner.devicectl_calls > 0);
}

TEST_CASE("[Editor][IOSRunTarget] probe_readiness blocks on Developer Mode when it is off") {
	FakeCommandRunner runner;
	runner.devicectl_output = make_devicectl_json("phone-A", "My iPhone", "paired", "disabled");

	IOSRunTargetPlatform adapter(&runner);
	RunTarget target = make_ios_target();
	target.device_id = "phone-A";

	const Vector<ReadinessStep> steps = adapter.probe_readiness(target);
	const ReadinessStep developer_mode = step_by_id(steps, RunTargetReadiness::STEP_DEVELOPER_MODE);
	CHECK_EQ(developer_mode.status, ReadinessStep::ACTION_NEEDED);
	CHECK_FALSE(developer_mode.fix_hint.is_empty());
}

TEST_CASE("[Editor][IOSRunTarget] probe_readiness blocks on Xcode when the toolchain is absent") {
	FakeCommandRunner runner;
	runner.xcode_select_exit = 2; // `xcode-select -p` fails when Xcode is not configured.
	runner.xcode_select_output = "";
	runner.devicectl_output = make_devicectl_json("phone-A", "My iPhone", "paired", "enabled");

	IOSRunTargetPlatform adapter(&runner);
	RunTarget target = make_ios_target();
	target.device_id = "phone-A";

	const Vector<ReadinessStep> steps = adapter.probe_readiness(target);
	REQUIRE_EQ(steps.size(), 6);
	CHECK_EQ(steps[0].id, StringName(RunTargetReadiness::STEP_XCODE));
	CHECK_EQ(steps[0].status, ReadinessStep::ACTION_NEEDED);
	for (int i = 1; i < steps.size(); i++) {
		CHECK_EQ(steps[i].status, ReadinessStep::BLOCKED);
	}
}

TEST_CASE("[Editor][IOSRunTarget] probe_readiness blocks on team when none is selected") {
	FakeCommandRunner runner;
	runner.devicectl_output = make_devicectl_json("phone-A", "My iPhone", "paired", "enabled");

	IOSRunTargetPlatform adapter(&runner);
	RunTarget target = make_ios_target();
	target.device_id = "phone-A";
	target.team_id = String(); // No signing team remembered yet.

	const Vector<ReadinessStep> steps = adapter.probe_readiness(target);
	const ReadinessStep team = step_by_id(steps, RunTargetReadiness::STEP_TEAM);
	CHECK_EQ(team.status, ReadinessStep::ACTION_NEEDED);
	const ReadinessStep provisioning = step_by_id(steps, RunTargetReadiness::STEP_PROVISIONING);
	CHECK_EQ(provisioning.status, ReadinessStep::BLOCKED);
}

TEST_CASE("[Editor][IOSRunTarget] auto target diagnoses the first runnable device") {
	// An unready device is listed before a ready one. "auto" readiness must follow
	// the same rule as Run (first runnable device), so it reports the ready device.
	FakeCommandRunner runner;
	runner.devicectl_output =
			"{\"result\":{\"devices\":["
			"{\"identifier\":\"phone-off\",\"connectionProperties\":{\"pairingState\":\"paired\",\"transportType\":\"wired\"},\"deviceProperties\":{\"name\":\"DevMode-off iPhone\",\"developerModeStatus\":\"disabled\"}},"
			"{\"identifier\":\"phone-ready\",\"connectionProperties\":{\"pairingState\":\"paired\",\"transportType\":\"wired\"},\"deviceProperties\":{\"name\":\"Ready iPhone\",\"developerModeStatus\":\"enabled\"}}"
			"]}}";

	IOSRunTargetPlatform adapter(&runner);
	RunTarget target = make_ios_target();
	target.device_id = "auto";

	const Vector<ReadinessStep> steps = adapter.probe_readiness(target);
	REQUIRE_EQ(steps.size(), 6);
	for (const ReadinessStep &step : steps) {
		CHECK_EQ(step.status, ReadinessStep::OK);
	}
}

TEST_CASE("[Editor][IOSRunTarget] auto target with no runnable device diagnoses the first connected one") {
	// Only an unready device is connected. "auto" readiness must fall back to it so
	// the ladder explains the blocker rather than claiming nothing is connected.
	FakeCommandRunner runner;
	runner.devicectl_output = make_devicectl_json("phone-off", "DevMode-off iPhone", "paired", "disabled");

	IOSRunTargetPlatform adapter(&runner);
	RunTarget target = make_ios_target();
	target.device_id = "auto";

	const Vector<ReadinessStep> steps = adapter.probe_readiness(target);
	const ReadinessStep device = step_by_id(steps, RunTargetReadiness::STEP_DEVICE);
	CHECK_EQ(device.status, ReadinessStep::OK); // The device is connected and trusted.
	const ReadinessStep developer_mode = step_by_id(steps, RunTargetReadiness::STEP_DEVELOPER_MODE);
	CHECK_EQ(developer_mode.status, ReadinessStep::ACTION_NEEDED); // ...but Developer Mode is off.
}

TEST_CASE("[Editor][IOSRunTarget] run fails gracefully without a registered export platform") {
	// Headless tests have no `EditorExport` singleton, so the adapter cannot find
	// the iOS export platform to deploy through; it must report that cleanly.
	FakeCommandRunner runner;
	IOSRunTargetPlatform adapter(&runner);

	ERR_PRINT_OFF;
	const Error error = adapter.run(make_ios_target(), EditorExportPlatform::DEBUG_FLAG_REMOTE_DEBUG);
	ERR_PRINT_ON;

	CHECK_NE(error, OK);
}

} // namespace TestIOSRunTargetPlatform

#endif // TOOLS_ENABLED
