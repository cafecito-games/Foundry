/**************************************************************************/
/*  run_target_readiness.h                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                              GODOT ENGINE                              */
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

#include "editor/run/run_target_platform.h"

#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

// The Readiness Doctor: pure detection that turns the prerequisite state of a
// run target into the ordered, renderable `ReadinessStep` ladder. It never
// shells out, touches the UI, or performs any of Apple's steps itself; a
// platform adapter gathers the raw signals and hands them in as a `ProbeResult`,
// and the Doctor decides what the user must do next.
//
// The ladder is evaluated in a fixed order. The first unsatisfied rung is the
// one the user can act on (`ACTION_NEEDED`); every rung after it is reported as
// upcoming (`BLOCKED`) because its own state can't be trusted until the earlier
// blocker is cleared. Keeping this logic in one place is what lets the run-bar
// dropdown and the Targets panel render from a single source of truth, and lets
// the run path map its own provisioning failures into the exact same vocabulary.
class RunTargetReadiness {
public:
	// Stable step identifiers, shared across the ladder, the error mapping, and
	// the UI surfaces that render badges/affordances per step.
	static constexpr const char *STEP_XCODE = "xcode";
	static constexpr const char *STEP_DEVICE = "device";
	static constexpr const char *STEP_TRUST = "trust";
	static constexpr const char *STEP_DEVELOPER_MODE = "developer_mode";
	static constexpr const char *STEP_TEAM = "team";
	static constexpr const char *STEP_PROVISIONING = "provisioning";

	// The semantic signals the ladder is evaluated against. A platform adapter
	// fills this in from its own probes (tool output, device structs); the ladder
	// evaluation itself is platform-agnostic.
	struct ProbeResult {
		bool xcode_present = false;
		bool device_connected = false;
		bool device_trusted = false;
		bool developer_mode_enabled = false;
		bool team_available = false;
		bool provisioning_resolved = false;

		// Raw stderr captured from an automatic-signing resolve attempt, used to
		// refine the provisioning step's detail/fix hint. Empty when no attempt
		// was made or it succeeded.
		String provisioning_error;
	};

	// Evaluates the fixed-order readiness ladder for `p_result` and returns the
	// six steps (Xcode -> device -> trust -> Developer Mode -> team ->
	// provisioning). Satisfied leading steps are `OK`, the first unsatisfied step
	// is `ACTION_NEEDED`, and later steps are `BLOCKED` (upcoming).
	static Vector<ReadinessStep> evaluate(const ProbeResult &p_result);

	// Maps a captured `xcodebuild` automatic-signing failure into the same
	// provisioning `ReadinessStep` the cold ladder emits, so the run path and the
	// Doctor share one error vocabulary. The returned step is always
	// `ACTION_NEEDED` (this specific failure was observed and is user-fixable).
	static ReadinessStep map_provisioning_error(const String &p_stderr_text);

	// iOS adapter helper: turns a captured probe snapshot (the shape of the
	// `tests/data/ios_run_targets/*.json` fixtures, also assembled live from
	// `xcode-select`/`devicectl`/`xcodebuild` output) into a `ProbeResult`. Lives
	// here so the run path and the tests share one parser. `p_device_id` selects
	// which enumerated device to inspect; empty or "auto" picks the first.
	static ProbeResult parse_ios_probe(const Dictionary &p_snapshot, const String &p_device_id = String());

private:
	// Builds the provisioning step body (title/detail/fix hint) for a given
	// status and captured error, so `evaluate` and `map_provisioning_error`
	// produce byte-identical steps from the same error text.
	static ReadinessStep make_provisioning_step(const String &p_error, ReadinessStep::Status p_status);
};
