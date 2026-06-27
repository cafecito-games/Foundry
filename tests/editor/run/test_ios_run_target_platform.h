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

#include "core/io/file_access.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestIOSRunTargetPlatform {

// Reads a captured decoded-provisioning-profile fixture (the XML plist
// `security cms -D` emits) from `tests/data/ios_run_targets/`.
static String load_profile_fixture(const String &p_fixture) {
	const String path = TestUtils::get_data_path(String("ios_run_targets/").path_join(p_fixture));
	Error error = OK;
	const String text = FileAccess::get_file_as_string(path, &error);
	REQUIRE_MESSAGE(error == OK, vformat("Could not read fixture: %s", path));
	return text;
}

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

TEST_CASE("[Editor][IOSRunTarget] build_probe_devices passes through devicectl devices unchanged") {
	const String json = make_devicectl_json("phone-A", "My iPhone", "paired", "disabled");

	const Array devices = IOSRunTargetPlatform::build_probe_devices(json, Vector<IOSDeployDevice>());
	REQUIRE_EQ(devices.size(), 1);

	const Dictionary device = devices[0];
	CHECK_EQ(String(device.get("identifier", String())), "phone-A");
	const Dictionary connection_properties = device.get("connectionProperties", Dictionary());
	CHECK_EQ(String(connection_properties.get("pairingState", String())), "paired");
	const Dictionary device_properties = device.get("deviceProperties", Dictionary());
	// A connected-but-not-runnable devicectl device is preserved so the ladder can
	// still diagnose it (Developer Mode off here).
	CHECK_EQ(String(device_properties.get("developerModeStatus", String())), "disabled");
}

TEST_CASE("[Editor][IOSRunTarget] build_probe_devices folds in ios_deploy devices as ready") {
	Vector<IOSDeployDevice> ios_deploy_devices;
	IOSDeployDevice legacy;
	legacy.id = "legacy-phone";
	legacy.name = "Legacy iPhone";
	ios_deploy_devices.push_back(legacy);

	// No modern devicectl devices: the only device comes from the legacy enumerator.
	const Array devices = IOSRunTargetPlatform::build_probe_devices(String(), ios_deploy_devices);
	REQUIRE_EQ(devices.size(), 1);

	// The legacy device is reported in devicectl shape as paired with Developer
	// Mode enabled, so the readiness ladder agrees with the device list.
	const RunTargetReadiness::ProbeResult probe = RunTargetReadiness::parse_ios_probe(
			Dictionary(), "legacy-phone");
	CHECK_FALSE(probe.device_connected); // Sanity: an empty snapshot sees nothing.

	Dictionary snapshot;
	snapshot["xcode_select_path"] = "/Applications/Xcode.app/Contents/Developer";
	snapshot["signing_team"] = "ABCDE12345";
	snapshot["provisioning_stderr"] = String();
	snapshot["devices"] = devices;

	const RunTargetReadiness::ProbeResult ready = RunTargetReadiness::parse_ios_probe(snapshot, "legacy-phone");
	CHECK(ready.device_connected);
	CHECK(ready.device_trusted);
	CHECK(ready.developer_mode_enabled);

	const Vector<ReadinessStep> steps = RunTargetReadiness::evaluate(ready);
	for (const ReadinessStep &step : steps) {
		CHECK_EQ(step.status, ReadinessStep::OK);
	}
}

TEST_CASE("[Editor][IOSRunTarget] build_probe_devices orders ios_deploy devices before devicectl") {
	// A devicectl device that is connected but has Developer Mode off, plus a
	// legacy ios_deploy device. The ios_deploy device must come first so an "auto"
	// target's readiness resolves to the same device Run would deploy to (the
	// export poll lists ios_deploy devices first).
	const String json = make_devicectl_json("modern-phone", "Modern iPhone", "paired", "disabled");

	Vector<IOSDeployDevice> ios_deploy_devices;
	IOSDeployDevice legacy;
	legacy.id = "legacy-phone";
	legacy.name = "Legacy iPhone";
	ios_deploy_devices.push_back(legacy);

	const Array devices = IOSRunTargetPlatform::build_probe_devices(json, ios_deploy_devices);
	REQUIRE_EQ(devices.size(), 2);

	const Dictionary first = devices[0];
	CHECK_EQ(String(first.get("identifier", String())), "legacy-phone");
	const Dictionary second = devices[1];
	CHECK_EQ(String(second.get("identifier", String())), "modern-phone");

	// "auto" readiness must pick the runnable ios_deploy device, not the
	// Developer-Mode-off devicectl one, so the ladder reports ready.
	Dictionary snapshot;
	snapshot["xcode_select_path"] = "/Applications/Xcode.app/Contents/Developer";
	snapshot["signing_team"] = "ABCDE12345";
	snapshot["provisioning_stderr"] = String();
	snapshot["devices"] = devices;

	const RunTargetReadiness::ProbeResult probe = RunTargetReadiness::parse_ios_probe(snapshot, "auto");
	CHECK(probe.device_connected);
	CHECK(probe.developer_mode_enabled);
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

TEST_CASE("[Editor][IOSRunTarget] Provisioning profile parses into a signing team") {
	SigningTeam team;
	CHECK(IOSRunTargetPlatform::parse_provisioning_profile_team(load_profile_fixture("provisioning_personal.plist"), team));
	CHECK_EQ(team.id, "ABCDE12345");
	CHECK_EQ(team.name, "Jane Developer");

	SigningTeam company;
	CHECK(IOSRunTargetPlatform::parse_provisioning_profile_team(load_profile_fixture("provisioning_company.plist"), company));
	CHECK_EQ(company.id, "FGHIJ67890");
	CHECK_EQ(company.name, "Acme Incorporated");
}

TEST_CASE("[Editor][IOSRunTarget] Provisioning profile without a team name falls back to the id") {
	const String plist =
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
			"<plist version=\"1.0\"><dict>"
			"<key>TeamIdentifier</key><array><string>KLMNO13579</string></array>"
			"</dict></plist>";
	SigningTeam team;
	CHECK(IOSRunTargetPlatform::parse_provisioning_profile_team(plist, team));
	CHECK_EQ(team.id, "KLMNO13579");
	CHECK_EQ(team.name, "KLMNO13579");
}

TEST_CASE("[Editor][IOSRunTarget] Profiles without a team identifier are rejected") {
	SigningTeam team;

	// Empty input.
	CHECK_FALSE(IOSRunTargetPlatform::parse_provisioning_profile_team("", team));

	// Not a plist at all.
	CHECK_FALSE(IOSRunTargetPlatform::parse_provisioning_profile_team("not a plist", team));

	// A valid plist that carries no team identifier.
	const String no_team =
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
			"<plist version=\"1.0\"><dict>"
			"<key>AppIDName</key><string>Orphan</string>"
			"</dict></plist>";
	CHECK_FALSE(IOSRunTargetPlatform::parse_provisioning_profile_team(no_team, team));

	// A team identifier present but empty.
	const String empty_team =
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
			"<plist version=\"1.0\"><dict>"
			"<key>TeamIdentifier</key><array><string></string></array>"
			"</dict></plist>";
	CHECK_FALSE(IOSRunTargetPlatform::parse_provisioning_profile_team(empty_team, team));
}

TEST_CASE("[Editor][IOSRunTarget] Embedded plist is sliced out of a CMS-wrapped profile") {
	// A provisioning profile stores its plist as cleartext inside a binary CMS
	// wrapper (including NUL bytes). The extractor must recover exactly the
	// `<?xml ... </plist>` span and feed cleanly into the team parser.
	const String plist =
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
			"<plist version=\"1.0\"><dict>"
			"<key>TeamIdentifier</key><array><string>PQRST24680</string></array>"
			"<key>TeamName</key><string>Slice Team</string>"
			"</dict></plist>";

	Vector<uint8_t> bytes;
	// Binary DER prelude, including a NUL byte.
	bytes.push_back(0x30);
	bytes.push_back(0x82);
	bytes.push_back(0x00);
	bytes.push_back(0x2A);
	const CharString plist_utf8 = plist.utf8();
	for (int i = 0; i < plist_utf8.length(); i++) {
		bytes.push_back((uint8_t)plist_utf8[i]);
	}
	// Trailing signature bytes after </plist>.
	bytes.push_back(0x00);
	bytes.push_back(0xFF);

	const String extracted = IOSRunTargetPlatform::extract_plist_from_profile(bytes);
	CHECK_EQ(extracted, plist);

	SigningTeam team;
	CHECK(IOSRunTargetPlatform::parse_provisioning_profile_team(extracted, team));
	CHECK_EQ(team.id, "PQRST24680");
	CHECK_EQ(team.name, "Slice Team");

	// No plist payload yields an empty string.
	Vector<uint8_t> junk;
	junk.push_back(0x01);
	junk.push_back(0x02);
	CHECK(IOSRunTargetPlatform::extract_plist_from_profile(junk).is_empty());
	CHECK(IOSRunTargetPlatform::extract_plist_from_profile(Vector<uint8_t>()).is_empty());
}

TEST_CASE("[Editor][IOSRunTarget] Signing teams are deduplicated across profiles") {
	Vector<String> payloads;
	// Two profiles for the same personal team plus one company profile; a personal
	// developer commonly has many profiles minted for the same team.
	payloads.push_back(load_profile_fixture("provisioning_personal.plist"));
	payloads.push_back(load_profile_fixture("provisioning_company.plist"));
	payloads.push_back(load_profile_fixture("provisioning_personal.plist"));
	payloads.push_back("garbage that does not parse");

	const Vector<SigningTeam> teams = IOSRunTargetPlatform::parse_signing_teams(payloads);
	REQUIRE_EQ(teams.size(), 2);
	// First-seen order is preserved.
	CHECK_EQ(teams[0].id, "ABCDE12345");
	CHECK_EQ(teams[0].name, "Jane Developer");
	CHECK_EQ(teams[1].id, "FGHIJ67890");
	CHECK_EQ(teams[1].name, "Acme Incorporated");
}

TEST_CASE("[Editor][IOSRunTarget] Default platform adapter advertises no signing teams") {
	// The base interface returns nothing so platforms without a team concept fall
	// back to manual entry in the panel.
	struct StubPlatform : public RunTargetPlatform {
		virtual Vector<ReadinessStep> probe_readiness(const RunTarget &) override { return Vector<ReadinessStep>(); }
		virtual Vector<RunTargetDevice> list_devices() override { return Vector<RunTargetDevice>(); }
		virtual Error run(const RunTarget &, int) override { return OK; }
	};
	StubPlatform stub;
	CHECK(stub.list_signing_teams().is_empty());
}

} // namespace TestIOSRunTargetPlatform

#endif // TOOLS_ENABLED
