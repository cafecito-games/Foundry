/**************************************************************************/
/*  test_run_target_readiness.h                                           */
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

#include "editor/run/run_target_readiness.h"

#include "core/io/file_access.h"
#include "core/io/json.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestRunTargetReadiness {

// Loads a captured probe fixture and parses it into a `ProbeResult` the same way
// the iOS adapter will at runtime.
static RunTargetReadiness::ProbeResult load_probe(const String &p_fixture, const String &p_device_id = String()) {
	const String path = TestUtils::get_data_path(String("ios_run_targets/").path_join(p_fixture));
	Error error = FAILED;
	const String text = FileAccess::get_file_as_string(path, &error);
	REQUIRE_MESSAGE(error == OK, vformat("Could not read fixture: %s", path));

	const Variant parsed = JSON::parse_string(text);
	REQUIRE_MESSAGE(parsed.get_type() == Variant::DICTIONARY, vformat("Fixture is not a JSON object: %s", path));

	return RunTargetReadiness::parse_ios_probe(parsed, p_device_id);
}

// Returns the step with the given id from a ladder, or a default step if absent.
static ReadinessStep step_by_id(const Vector<ReadinessStep> &p_steps, const StringName &p_id) {
	for (const ReadinessStep &step : p_steps) {
		if (step.id == p_id) {
			return step;
		}
	}
	return ReadinessStep();
}

TEST_CASE("[Editor][RunTargetReadiness] Ladder is evaluated in fixed order") {
	RunTargetReadiness::ProbeResult result;
	result.xcode_present = true;
	result.device_connected = true;
	result.device_trusted = true;
	result.developer_mode_enabled = true;
	result.team_available = true;
	result.provisioning_resolved = true;

	const Vector<ReadinessStep> steps = RunTargetReadiness::evaluate(result);
	REQUIRE_EQ(steps.size(), 6);
	CHECK_EQ(steps[0].id, StringName(RunTargetReadiness::STEP_XCODE));
	CHECK_EQ(steps[1].id, StringName(RunTargetReadiness::STEP_DEVICE));
	CHECK_EQ(steps[2].id, StringName(RunTargetReadiness::STEP_TRUST));
	CHECK_EQ(steps[3].id, StringName(RunTargetReadiness::STEP_DEVELOPER_MODE));
	CHECK_EQ(steps[4].id, StringName(RunTargetReadiness::STEP_TEAM));
	CHECK_EQ(steps[5].id, StringName(RunTargetReadiness::STEP_PROVISIONING));
}

TEST_CASE("[Editor][RunTargetReadiness] All prerequisites satisfied reports every step OK") {
	const RunTargetReadiness::ProbeResult result = load_probe("paired_device.json");
	const Vector<ReadinessStep> steps = RunTargetReadiness::evaluate(result);

	REQUIRE_EQ(steps.size(), 6);
	for (const ReadinessStep &step : steps) {
		CHECK_EQ(step.status, ReadinessStep::Status::OK);
		CHECK(step.fix_hint.is_empty());
	}
}

TEST_CASE("[Editor][RunTargetReadiness] First unsatisfied rung blocks, earlier OK, later upcoming") {
	// Xcode present, device connected + trusted, but Developer Mode is off.
	const RunTargetReadiness::ProbeResult result = load_probe("devmode_off.json");
	const Vector<ReadinessStep> steps = RunTargetReadiness::evaluate(result);
	REQUIRE_EQ(steps.size(), 6);

	// Steps before the blocker are satisfied.
	CHECK_EQ(steps[0].status, ReadinessStep::Status::OK); // xcode
	CHECK_EQ(steps[1].status, ReadinessStep::Status::OK); // device
	CHECK_EQ(steps[2].status, ReadinessStep::Status::OK); // trust

	// The blocking rung is the actionable one, and it carries a fix hint.
	CHECK_EQ(steps[3].id, StringName(RunTargetReadiness::STEP_DEVELOPER_MODE));
	CHECK_EQ(steps[3].status, ReadinessStep::Status::ACTION_NEEDED);
	CHECK_FALSE(steps[3].fix_hint.is_empty());

	// Later rungs are upcoming (blocked) regardless of their own probe state,
	// and never present a fix hint of their own.
	CHECK_EQ(steps[4].status, ReadinessStep::Status::BLOCKED); // team
	CHECK(steps[4].fix_hint.is_empty());
	CHECK_EQ(steps[5].status, ReadinessStep::Status::BLOCKED); // provisioning
	CHECK(steps[5].fix_hint.is_empty());
}

TEST_CASE("[Editor][RunTargetReadiness] No-team fixture blocks on the team rung") {
	const RunTargetReadiness::ProbeResult result = load_probe("no_team.json");
	const Vector<ReadinessStep> steps = RunTargetReadiness::evaluate(result);
	REQUIRE_EQ(steps.size(), 6);

	CHECK_EQ(steps[3].status, ReadinessStep::Status::OK); // developer_mode satisfied
	CHECK_EQ(steps[4].id, StringName(RunTargetReadiness::STEP_TEAM));
	CHECK_EQ(steps[4].status, ReadinessStep::Status::ACTION_NEEDED);
	CHECK_FALSE(steps[4].fix_hint.is_empty());
	CHECK_EQ(steps[5].status, ReadinessStep::Status::BLOCKED); // provisioning upcoming
}

TEST_CASE("[Editor][RunTargetReadiness] Disconnected device blocks on the device rung") {
	RunTargetReadiness::ProbeResult result;
	result.xcode_present = true;
	// Nothing else satisfied.

	const Vector<ReadinessStep> steps = RunTargetReadiness::evaluate(result);
	REQUIRE_EQ(steps.size(), 6);

	CHECK_EQ(steps[0].status, ReadinessStep::Status::OK); // xcode
	CHECK_EQ(steps[1].id, StringName(RunTargetReadiness::STEP_DEVICE));
	CHECK_EQ(steps[1].status, ReadinessStep::Status::ACTION_NEEDED);
	CHECK_EQ(steps[2].status, ReadinessStep::Status::BLOCKED); // trust upcoming
}

TEST_CASE("[Editor][RunTargetReadiness] Missing Xcode blocks on the first rung") {
	RunTargetReadiness::ProbeResult result;
	// Everything else would pass, but Xcode is absent.
	result.device_connected = true;
	result.device_trusted = true;
	result.developer_mode_enabled = true;
	result.team_available = true;
	result.provisioning_resolved = true;

	const Vector<ReadinessStep> steps = RunTargetReadiness::evaluate(result);
	REQUIRE_EQ(steps.size(), 6);
	CHECK_EQ(steps[0].id, StringName(RunTargetReadiness::STEP_XCODE));
	CHECK_EQ(steps[0].status, ReadinessStep::Status::ACTION_NEEDED);
	for (int i = 1; i < steps.size(); i++) {
		CHECK_EQ(steps[i].status, ReadinessStep::Status::BLOCKED);
	}
}

TEST_CASE("[Editor][RunTargetReadiness] Provisioning error maps to the same step both directions") {
	// Read the captured stderr straight from the fixture for the error-mapping
	// direction.
	const String path = TestUtils::get_data_path("ios_run_targets/bundle_id_taken.json");
	const Variant parsed = JSON::parse_string(FileAccess::get_file_as_string(path));
	REQUIRE_EQ(parsed.get_type(), Variant::DICTIONARY);
	const Dictionary snapshot = parsed;
	const String stderr_text = snapshot.get("provisioning_stderr", String());
	REQUIRE_FALSE(stderr_text.is_empty());

	// Cold-probe direction: the ladder surfaces provisioning as the blocking step.
	const RunTargetReadiness::ProbeResult result = RunTargetReadiness::parse_ios_probe(snapshot);
	const Vector<ReadinessStep> steps = RunTargetReadiness::evaluate(result);
	const ReadinessStep cold_step = step_by_id(steps, RunTargetReadiness::STEP_PROVISIONING);
	CHECK_EQ(cold_step.status, ReadinessStep::Status::ACTION_NEEDED);

	// Error-mapping direction: the run path maps the same stderr.
	const ReadinessStep mapped_step = RunTargetReadiness::map_provisioning_error(stderr_text);

	// Both directions must produce a byte-identical step (id, title, detail,
	// status, fix hint) so the run path and the Doctor share one vocabulary.
	CHECK_EQ(cold_step, mapped_step);
	CHECK_EQ(mapped_step.id, StringName(RunTargetReadiness::STEP_PROVISIONING));
	CHECK_FALSE(mapped_step.fix_hint.is_empty());
	// The bundle-id-taken signature should steer the hint toward changing the id.
	CHECK(mapped_step.fix_hint.to_lower().contains("bundle id"));
}

TEST_CASE("[Editor][RunTargetReadiness] Provisioning error classification is stable") {
	const ReadinessStep generic = RunTargetReadiness::map_provisioning_error("error: unknown signing failure");
	CHECK_EQ(generic.id, StringName(RunTargetReadiness::STEP_PROVISIONING));
	CHECK_EQ(generic.status, ReadinessStep::Status::ACTION_NEEDED);
	CHECK_FALSE(generic.detail.is_empty());
	CHECK_FALSE(generic.fix_hint.is_empty());

	const ReadinessStep no_profile = RunTargetReadiness::map_provisioning_error("error: No profiles for 'com.example.app' were found");
	CHECK(no_profile.detail.to_lower().contains("provisioning profile"));
}

TEST_CASE("[Editor][RunTargetReadiness] Device selection picks the requested device") {
	Dictionary snapshot;
	snapshot["xcode_select_path"] = "/Applications/Xcode.app/Contents/Developer";
	snapshot["signing_team"] = "ABCDE12345";
	snapshot["provisioning_stderr"] = "";

	Dictionary first_connection;
	first_connection["pairingState"] = "paired";
	Dictionary first_properties;
	first_properties["developerModeStatus"] = "enabled";
	Dictionary first;
	first["identifier"] = "phone-A";
	first["connectionProperties"] = first_connection;
	first["deviceProperties"] = first_properties;

	Dictionary second_connection;
	second_connection["pairingState"] = "paired";
	Dictionary second_properties;
	second_properties["developerModeStatus"] = "disabled";
	Dictionary second;
	second["identifier"] = "phone-B";
	second["connectionProperties"] = second_connection;
	second["deviceProperties"] = second_properties;

	Array devices;
	devices.push_back(first);
	devices.push_back(second);
	snapshot["devices"] = devices;

	// Selecting phone-B (Developer Mode off) must block on Developer Mode.
	const RunTargetReadiness::ProbeResult b = RunTargetReadiness::parse_ios_probe(snapshot, "phone-B");
	CHECK_FALSE(b.developer_mode_enabled);

	// Default selection picks the first device (Developer Mode on).
	const RunTargetReadiness::ProbeResult a = RunTargetReadiness::parse_ios_probe(snapshot, "auto");
	CHECK(a.developer_mode_enabled);
}

TEST_CASE("[Editor][RunTargetReadiness] A missing requested device is reported as disconnected") {
	Dictionary snapshot;
	snapshot["xcode_select_path"] = "/Applications/Xcode.app/Contents/Developer";
	snapshot["signing_team"] = "ABCDE12345";
	snapshot["provisioning_stderr"] = "";

	// A different phone than the one the target remembers is plugged in, fully set up.
	Dictionary connection;
	connection["pairingState"] = "paired";
	Dictionary properties;
	properties["developerModeStatus"] = "enabled";
	Dictionary other;
	other["identifier"] = "phone-other";
	other["connectionProperties"] = connection;
	other["deviceProperties"] = properties;

	Array devices;
	devices.push_back(other);
	snapshot["devices"] = devices;

	// The saved target points at a device that is not currently connected. We must
	// not borrow the other phone's trust/Developer-Mode state.
	const RunTargetReadiness::ProbeResult result = RunTargetReadiness::parse_ios_probe(snapshot, "phone-missing");
	CHECK_FALSE(result.device_connected);
	CHECK_FALSE(result.device_trusted);
	CHECK_FALSE(result.developer_mode_enabled);

	const Vector<ReadinessStep> steps = RunTargetReadiness::evaluate(result);
	REQUIRE_EQ(steps.size(), 6);
	CHECK_EQ(steps[0].status, ReadinessStep::Status::OK); // xcode
	CHECK_EQ(steps[1].id, StringName(RunTargetReadiness::STEP_DEVICE));
	CHECK_EQ(steps[1].status, ReadinessStep::Status::ACTION_NEEDED); // blocks on the missing device
}

} // namespace TestRunTargetReadiness

#endif // TOOLS_ENABLED
