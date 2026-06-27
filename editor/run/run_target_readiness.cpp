/**************************************************************************/
/*  run_target_readiness.cpp                                              */
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

#include "run_target_readiness.h"

#include "core/variant/array.h"
#include "core/variant/variant.h"

namespace {

// One rung of the ladder: its identity and the copy shown when satisfied versus
// when it is the blocking step. The provisioning rung is special-cased because
// its detail/fix hint depend on the captured error text.
struct LadderRung {
	const char *id;
	const char *title;
	const char *ok_detail;
	const char *fail_detail;
	const char *fix_hint;
};

const LadderRung LADDER[] = {
	{
			RunTargetReadiness::STEP_XCODE,
			"Xcode toolchain",
			"Xcode and its command-line tools are installed.",
			"Xcode or its command-line tools were not found.",
			"Install Xcode from the App Store, then run: xcodebuild -runFirstLaunch",
	},
	{
			RunTargetReadiness::STEP_DEVICE,
			"Device connected",
			"A device is connected.",
			"No device is connected.",
			"Plug in your iPhone over USB.",
	},
	{
			RunTargetReadiness::STEP_TRUST,
			"Device trusted",
			"The device is paired and trusts this computer.",
			"The device is connected but does not trust this computer yet.",
			"Unlock your iPhone and tap Trust This Computer.",
	},
	{
			RunTargetReadiness::STEP_DEVELOPER_MODE,
			"Developer Mode",
			"Developer Mode is enabled on the device.",
			"Developer Mode is turned off on the device.",
			"On the device: Settings > Privacy & Security > Developer Mode > On, then reboot.",
	},
	{
			RunTargetReadiness::STEP_TEAM,
			"Apple ID / team",
			"A signing team is selected.",
			"No Apple ID signing team is available.",
			"Sign in with your Apple ID in Xcode > Settings > Accounts, then pick your team here.",
	},
	// Provisioning intentionally has empty fail copy here: its failing detail and
	// fix hint come from `make_provisioning_step` so the cold ladder and the
	// error mapping stay in lockstep.
	{
			RunTargetReadiness::STEP_PROVISIONING,
			"Automatic signing",
			"Automatic signing resolved a provisioning profile.",
			"",
			"",
	},
};

constexpr int LADDER_SIZE = sizeof(LADDER) / sizeof(LADDER[0]);

// Shared copy for steps that sit after the blocking rung. Their own probe state
// can't be trusted until the earlier blocker is cleared, so they are reported as
// upcoming rather than passing or failing on their own.
constexpr const char *UPCOMING_DETAIL = "Pending — resolve the step(s) above first.";

} // namespace

ReadinessStep RunTargetReadiness::make_provisioning_step(const String &p_error, ReadinessStep::Status p_status) {
	ReadinessStep step;
	step.id = STEP_PROVISIONING;
	step.title = "Automatic signing";
	step.status = p_status;

	if (p_status == ReadinessStep::OK) {
		step.detail = "Automatic signing resolved a provisioning profile.";
		return step;
	}
	if (p_status == ReadinessStep::BLOCKED) {
		step.detail = UPCOMING_DETAIL;
		return step;
	}

	// ACTION_NEEDED: classify the captured failure into the shared vocabulary.
	const String lowered = p_error.to_lower();
	if (lowered.contains("register bundle identifier") || lowered.contains("bundle identifier") || lowered.contains("is not available")) {
		step.detail = "Xcode could not register the app's bundle identifier — it may already be taken.";
		step.fix_hint = "Change the bundle id (e.g. com.yourname.game) in the target's signing settings, then recheck.";
	} else if (lowered.contains("no profiles") || lowered.contains("provisioning profile") || lowered.contains("no signing certificate") || lowered.contains("development team")) {
		step.detail = "Xcode could not create a provisioning profile for this app.";
		step.fix_hint = "Open Xcode > Settings > Accounts, confirm your Apple ID and team, then recheck.";
	} else {
		step.detail = "Automatic signing failed.";
		step.fix_hint = "Review the signing settings (team and bundle id) in the target, then recheck.";
	}
	return step;
}

ReadinessStep RunTargetReadiness::map_provisioning_error(const String &p_stderr_text) {
	return make_provisioning_step(p_stderr_text, ReadinessStep::ACTION_NEEDED);
}

Vector<ReadinessStep> RunTargetReadiness::evaluate(const ProbeResult &p_result) {
	const bool satisfied[LADDER_SIZE] = {
		p_result.xcode_present,
		p_result.device_connected,
		p_result.device_trusted,
		p_result.developer_mode_enabled,
		p_result.team_available,
		p_result.provisioning_resolved,
	};

	int first_failure = -1;
	for (int i = 0; i < LADDER_SIZE; i++) {
		if (!satisfied[i]) {
			first_failure = i;
			break;
		}
	}

	Vector<ReadinessStep> steps;
	for (int i = 0; i < LADDER_SIZE; i++) {
		const LadderRung &rung = LADDER[i];
		const bool is_provisioning = String(rung.id) == STEP_PROVISIONING;

		ReadinessStep::Status status;
		if (first_failure == -1 || i < first_failure) {
			status = ReadinessStep::OK;
		} else if (i == first_failure) {
			status = ReadinessStep::ACTION_NEEDED;
		} else {
			status = ReadinessStep::BLOCKED;
		}

		if (is_provisioning) {
			// Route through the shared builder so a provisioning failure surfaced
			// here is byte-identical to one produced by `map_provisioning_error`.
			steps.push_back(make_provisioning_step(p_result.provisioning_error, status));
			continue;
		}

		ReadinessStep step;
		step.id = rung.id;
		step.title = rung.title;
		step.status = status;
		switch (status) {
			case ReadinessStep::OK:
				step.detail = rung.ok_detail;
				break;
			case ReadinessStep::ACTION_NEEDED:
				step.detail = rung.fail_detail;
				step.fix_hint = rung.fix_hint;
				break;
			case ReadinessStep::BLOCKED:
				step.detail = UPCOMING_DETAIL;
				break;
		}
		steps.push_back(step);
	}

	return steps;
}

RunTargetReadiness::ProbeResult RunTargetReadiness::parse_ios_probe(const Dictionary &p_snapshot, const String &p_device_id) {
	ProbeResult result;

	result.xcode_present = !String(p_snapshot.get("xcode_select_path", String())).strip_edges().is_empty();
	result.team_available = !String(p_snapshot.get("signing_team", String())).strip_edges().is_empty();

	const String provisioning_error = p_snapshot.get("provisioning_stderr", String());
	result.provisioning_error = provisioning_error;
	result.provisioning_resolved = provisioning_error.strip_edges().is_empty();

	const Array devices = p_snapshot.get("devices", Array());
	result.device_connected = !devices.is_empty();

	if (result.device_connected) {
		// Pick the requested device, falling back to the first enumerated one.
		Dictionary device = devices[0];
		const String wanted = p_device_id.strip_edges();
		if (!wanted.is_empty() && wanted != "auto") {
			for (int i = 0; i < devices.size(); i++) {
				const Dictionary candidate = devices[i];
				if (String(candidate.get("identifier", String())) == wanted) {
					device = candidate;
					break;
				}
			}
		}

		const Dictionary connection_properties = device.get("connectionProperties", Dictionary());
		result.device_trusted = String(connection_properties.get("pairingState", String())) == "paired";

		const Dictionary device_properties = device.get("deviceProperties", Dictionary());
		result.developer_mode_enabled = String(device_properties.get("developerModeStatus", String())) == "enabled";
	}

	return result;
}
