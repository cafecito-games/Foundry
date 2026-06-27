/**************************************************************************/
/*  run_target_platform.h                                                 */
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

#include "editor/run/run_target.h"

#include "core/error/error_list.h"
#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"

// One entry in the readiness ladder the Doctor produces for a run target. The
// run-bar dropdown and the Targets panel both render from this single contract,
// so detection logic is never duplicated across UI surfaces.
//
// `status` reports where the step sits relative to the user:
//   - OK: the prerequisite is satisfied, nothing to do.
//   - ACTION_NEEDED: the user can resolve this themselves; `fix_hint` says how.
//   - BLOCKED: a hard stop that cannot proceed until an earlier step is fixed.
struct ReadinessStep {
	enum Status {
		OK,
		ACTION_NEEDED,
		BLOCKED,
	};

	StringName id; // Stable identifier, e.g. "xcode", "device", "developer_mode".
	String title; // Short label shown in the ladder.
	String detail; // Longer human-readable explanation of the current state.
	Status status = OK;
	String fix_hint; // Plain-language next action when status is not OK.

	bool operator==(const ReadinessStep &p_other) const {
		return id == p_other.id &&
				title == p_other.title &&
				detail == p_other.detail &&
				status == p_other.status &&
				fix_hint == p_other.fix_hint;
	}
	bool operator!=(const ReadinessStep &p_other) const {
		return !(*this == p_other);
	}
};

// A device discovered by a platform adapter (e.g. a connected iPhone). `badge`
// is a one-glance readiness summary suitable for the run-bar dropdown; the full
// per-target ladder still comes from `probe_readiness`.
struct RunTargetDevice {
	String id; // Stable device identifier (e.g. the iOS UDID), or "auto".
	String name; // Human-readable device name, e.g. "My iPhone".
	ReadinessStep::Status badge = ReadinessStep::OK;

	bool operator==(const RunTargetDevice &p_other) const {
		return id == p_other.id &&
				name == p_other.name &&
				badge == p_other.badge;
	}
	bool operator!=(const RunTargetDevice &p_other) const {
		return !(*this == p_other);
	}
};

// Platform-agnostic adapter the run-target layer drives. The iOS adapter wraps
// the existing export enumeration + `run()` machinery; Android (and desktop)
// drop in later by supplying their own implementation. The manager, run-bar
// dropdown, and Targets panel only ever talk to this interface, never to a
// concrete platform, which is what keeps the core reusable and testable.
class RunTargetPlatform {
public:
	// Detect the prerequisite state for `p_target` and return the ordered
	// readiness ladder (see `ReadinessStep`). Pure detection: no UI, no run
	// side-effects.
	virtual Vector<ReadinessStep> probe_readiness(const RunTarget &p_target) = 0;

	// Enumerate the devices this platform can currently deploy to.
	virtual Vector<RunTargetDevice> list_devices() = 0;

	// Deploy and launch `p_target`. `p_debug_flags` is a bitmask of
	// `EditorExportPlatform::DebugFlags` (e.g. DEBUG_FLAG_REMOTE_DEBUG).
	virtual Error run(const RunTarget &p_target, int p_debug_flags) = 0;

	virtual ~RunTargetPlatform() {}
};
