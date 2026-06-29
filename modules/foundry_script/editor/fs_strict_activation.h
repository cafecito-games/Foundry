/**************************************************************************/
/*  fs_strict_activation.h                                                */
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

#include "fs_verification_harness.h" // StrictViolation, StrictPreviewResult

#include "core/string/ustring.h"
#include "core/templates/vector.h"

// The final, opt-in step of the migration: turning on the two strict project settings
// (`debug/foundry_script/analysis/strict_null_checks`, `debug/foundry_script/analysis/strict_dynamic_checks`,
// registered in `modules/foundry_script/foundry_script.cpp`). It is gated behind a clean verification report
// and never flips a setting without explicit confirmation, matching the wizard's safe-by-default,
// honest-reporting stance: the user always sees what enabling the flag would cost before it is
// enabled, and a flip is always a deliberate, confirmed act.

// Which strict flags an activation request targets, and the confirmations gating the flip.
struct StrictActivationRequest {
	bool strict_null_checks = false; // Request to turn on debug/foundry_script/analysis/strict_null_checks.
	bool strict_dynamic_checks = false; // Request to turn on debug/foundry_script/analysis/strict_dynamic_checks.

	// The explicit-confirmation gate: a flip is performed only when this is true. An unconfirmed
	// activate() is a safe no-op that reports back why nothing changed, so a caller can confirm
	// only after the user has seen the plan.
	bool confirmed = false;

	// The gradual (warn-before-error) escape hatch. When false (the default), activate() refuses
	// to flip any flag whose strict preview still has violations, so the settings only turn on
	// after a clean report. When true, a confirmed request flips even with remaining violations,
	// which the post-flip report surfaces so they are addressed incrementally rather than hidden.
	bool allow_with_violations = false;
};

// The read-only assessment that gates activation: the strict preview for the requested flags plus
// whether the project is clean enough to flip them safely. evaluate() writes nothing.
struct StrictActivationPlan {
	bool ok = false; // false only on a fatal precondition (e.g. an unreadable file); see error_message.
	String error_message;
	bool strict_null_checks = false; // Echoes the request: the flag this plan was evaluated for.
	bool strict_dynamic_checks = false;
	Vector<StrictViolation> violations; // Strict-mode violations across the requested flags.
	bool clean = false; // True iff no violations remain, i.e. activation passes the gate cleanly.
};

// The result of an activate() call: whether the settings were flipped and the violation report as
// it stands after the (possible) flip.
struct StrictActivationResult {
	bool ok = false; // false only on a fatal precondition; see error_message.
	String error_message;

	bool activated = false; // True iff at least one setting was actually written.
	// Set when activated is false to explain why nothing was flipped (no flag requested, not
	// confirmed, or gate not clean and violations not allowed). Empty when activated is true.
	String blocked_reason;

	// True iff a flip went through and ProjectSettings was successfully persisted to disk, so the
	// activation survives an editor restart. False with activated=true means the settings are live
	// for this session but the on-disk project.foundry could not be written; see persist_error.
	bool persisted = false;
	String persist_error; // The save failure message when activated && !persisted; empty otherwise.

	// True when a flip went through but a per-feature project-setting override still masks the
	// effective value the analyzer reads (e.g. `...strict_dynamic_checks.<feature>=false`), so the
	// base key was written yet strict mode is not actually live. effective_warning describes which
	// flag is masked; both stay empty/false when the effective value matches the request.
	bool override_masked = false;
	String effective_warning;

	// The values the settings hold after the call. Each is true only if the request asked for it
	// and the flip went through; an unrequested flag stays false here regardless of its prior value.
	bool strict_null_checks_set = false;
	bool strict_dynamic_checks_set = false;

	// The strict violation report for the requested flags after the call. When the flip went
	// through it reflects the now-active settings; either way it equals the strict preview of the
	// same flags, so the wizard's post-flip report matches what it previewed before flipping.
	StrictPreviewResult post_flip;
};

// The gated strict-settings activator. evaluate() produces the read-only plan a caller shows the
// user; activate() performs the confirmed, gated flip and returns the post-flip report. Activation
// mutates global ProjectSettings (and, like the rest of the harness, shared FoundryScript cache state),
// so it is NOT safe to call concurrently with a live editing session or another migration run.
class FSStrictActivation {
public:
	// Builds the read-only activation plan for p_paths under the flags in p_request. The
	// confirmation fields of p_request are ignored here; evaluate() never writes anything.
	static StrictActivationPlan evaluate(
			const Vector<String> &p_paths,
			const StrictActivationRequest &p_request);

	// Performs the gated flip for p_paths under p_request. Writes the requested project settings
	// only when p_request.confirmed is true and either the gate is clean or
	// p_request.allow_with_violations is true; otherwise it is a no-op that reports blocked_reason.
	// Always returns a post-flip report matching the strict preview of the requested flags.
	static StrictActivationResult activate(
			const Vector<String> &p_paths,
			const StrictActivationRequest &p_request);
};

#endif // TOOLS_ENABLED
