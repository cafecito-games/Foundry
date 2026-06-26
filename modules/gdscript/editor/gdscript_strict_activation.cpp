/**************************************************************************/
/*  gdscript_strict_activation.cpp                                        */
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

#include "gdscript_strict_activation.h"

#ifdef TOOLS_ENABLED

#include "core/config/project_settings.h"

namespace {

constexpr const char *STRICT_NULL_SETTING = "debug/gdscript/analysis/strict_null_checks";
constexpr const char *STRICT_DYNAMIC_SETTING = "debug/gdscript/analysis/strict_dynamic_checks";

// The strict preview the gate is built on. Shared by evaluate() and activate() so the plan a caller
// previews and the post-flip report a caller receives are produced by the same code path against the
// same flags, which is what keeps them in agreement.
VerificationOptions options_for(const StrictActivationRequest &p_request) {
	VerificationOptions options;
	options.strict_null_checks = p_request.strict_null_checks;
	options.strict_dynamic_checks = p_request.strict_dynamic_checks;
	return options;
}

} // namespace

StrictActivationPlan GDScriptStrictActivation::evaluate(
		const Vector<String> &p_paths,
		const StrictActivationRequest &p_request) {
	StrictActivationPlan plan;
	plan.strict_null_checks = p_request.strict_null_checks;
	plan.strict_dynamic_checks = p_request.strict_dynamic_checks;

	const StrictPreviewResult preview = GDScriptVerificationHarness::preview_strict(p_paths, options_for(p_request));
	if (!preview.ok) {
		plan.ok = false;
		plan.error_message = preview.error_message;
		return plan;
	}

	plan.ok = true;
	plan.violations = preview.violations;
	plan.clean = preview.violations.is_empty();
	return plan;
}

StrictActivationResult GDScriptStrictActivation::activate(
		const Vector<String> &p_paths,
		const StrictActivationRequest &p_request) {
	StrictActivationResult result;

	// Compute the gate first so a fatal preview error aborts before any decision or write.
	const StrictActivationPlan plan = evaluate(p_paths, p_request);
	if (!plan.ok) {
		result.ok = false;
		result.error_message = plan.error_message;
		return result;
	}
	result.ok = true;

	const bool any_requested = p_request.strict_null_checks || p_request.strict_dynamic_checks;
	if (!any_requested) {
		result.blocked_reason = "No strict flag was requested.";
	} else if (!p_request.confirmed) {
		// The explicit-confirmation gate: never flip a setting without it.
		result.blocked_reason = "Activation requires explicit confirmation.";
	} else if (!plan.clean && !p_request.allow_with_violations) {
		// The clean-report gate: refuse to flip while violations remain unless gradual mode is
		// explicitly requested.
		result.blocked_reason = vformat(
				"%d strict violation(s) remain; enable gradual activation to flip anyway.",
				plan.violations.size());
	} else {
		ProjectSettings *settings = ProjectSettings::get_singleton();
		if (p_request.strict_null_checks) {
			settings->set_setting(STRICT_NULL_SETTING, true);
			result.strict_null_checks_set = true;
		}
		if (p_request.strict_dynamic_checks) {
			settings->set_setting(STRICT_DYNAMIC_SETTING, true);
			result.strict_dynamic_checks_set = true;
		}
		result.activated = true;

		// set_setting() is an in-memory mutation only; persist it to project.godot so the
		// activation survives an editor restart. A save failure does not undo the live flip (the
		// setting is active for this session), but it is reported so the caller can surface that
		// the change is not yet durable rather than silently losing it on restart.
		const Error save_error = settings->save();
		if (save_error == OK) {
			result.persisted = true;
		} else {
			result.persist_error = vformat("Failed to persist project settings (error %d).", save_error);
		}

		// The analyzer reads these flags through get_setting_with_override(), so a per-feature
		// override can still resolve the effective value to false even after the base key is set
		// true. Read back through the same override-aware path and warn if the flip did not actually
		// take effect, rather than reporting strict mode as live when it is not.
		Vector<String> masked;
		if (result.strict_null_checks_set && !(bool)settings->get_setting_with_override(STRICT_NULL_SETTING)) {
			masked.push_back("strict_null_checks");
		}
		if (result.strict_dynamic_checks_set && !(bool)settings->get_setting_with_override(STRICT_DYNAMIC_SETTING)) {
			masked.push_back("strict_dynamic_checks");
		}
		if (!masked.is_empty()) {
			result.override_masked = true;
			result.effective_warning = vformat(
					"A per-feature project-setting override still disables %s; the base setting was written but strict mode is not effective.",
					String(", ").join(masked));
		}
	}

	// Always report the strict violations for the requested flags. preview_strict simulates the
	// requested flags directly (independent of the live ProjectSettings values), so the post-flip
	// report equals the plan whether or not the flip went through.
	result.post_flip = GDScriptVerificationHarness::preview_strict(p_paths, options_for(p_request));
	return result;
}

#endif // TOOLS_ENABLED
