/**************************************************************************/
/*  test_run_targets_panel.h                                              */
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

#include "editor/run/run_target_platform.h"
#include "editor/run/run_targets_panel.h"

#include "tests/test_macros.h"

namespace TestRunTargetsPanel {

static ReadinessStep make_step(const String &p_id, ReadinessStep::Status p_status) {
	ReadinessStep step;
	step.id = p_id;
	step.title = p_id;
	step.status = p_status;
	return step;
}

TEST_CASE("[RunTargetsPanel] Bundle identifier validity") {
	String error;

	CHECK(RunTargetsPanel::is_valid_bundle_id("com.example.game", &error));
	CHECK(error.is_empty());

	CHECK(RunTargetsPanel::is_valid_bundle_id("My-App.123"));

	// Empty is invalid and reports a reason.
	error = String();
	CHECK_FALSE(RunTargetsPanel::is_valid_bundle_id("", &error));
	CHECK_FALSE(error.is_empty());

	// Disallowed characters (space, underscore) are rejected.
	CHECK_FALSE(RunTargetsPanel::is_valid_bundle_id("com.example.my game"));
	CHECK_FALSE(RunTargetsPanel::is_valid_bundle_id("com.example.my_game"));
	CHECK_FALSE(RunTargetsPanel::is_valid_bundle_id("com.example.café"));
}

TEST_CASE("[RunTargetsPanel] Default bundle id derivation") {
	// Lowercased, non-alphanumerics stripped, prefixed with com.example.
	CHECK(RunTargetsPanel::default_bundle_id_for_project("My Project") == "com.example.myproject");
	CHECK(RunTargetsPanel::default_bundle_id_for_project("Awesome-Game 2!") == "com.example.awesomegame2");

	// A name with no usable characters falls back to a safe placeholder.
	CHECK(RunTargetsPanel::default_bundle_id_for_project("***") == "com.example.game");
	CHECK(RunTargetsPanel::default_bundle_id_for_project("") == "com.example.game");

	// The derived id is always a valid bundle identifier.
	CHECK(RunTargetsPanel::is_valid_bundle_id(RunTargetsPanel::default_bundle_id_for_project("My Project")));
}

TEST_CASE("[RunTargetsPanel] Unique target name generation") {
	Vector<RunTarget> existing;

	// No collision: the preferred name is used verbatim.
	CHECK(RunTargetsPanel::unique_target_name("My iPhone", existing) == "My iPhone");

	RunTarget a;
	a.name = "My iPhone";
	existing.push_back(a);

	// First collision appends " 2", and so on.
	CHECK(RunTargetsPanel::unique_target_name("My iPhone", existing) == "My iPhone 2");

	RunTarget b;
	b.name = "My iPhone 2";
	existing.push_back(b);
	CHECK(RunTargetsPanel::unique_target_name("My iPhone", existing) == "My iPhone 3");

	// A blank preferred name falls back to a generic label rather than an empty one.
	CHECK_FALSE(RunTargetsPanel::unique_target_name("   ", existing).strip_edges().is_empty());
}

TEST_CASE("[RunTargetsPanel] One-click device setup target") {
	Vector<RunTarget> existing;

	// A connected device becomes a target that preselects its id and platform,
	// with automatic signing and the device name as the (unique) target name.
	const RunTarget target = RunTargetsPanel::make_device_setup_target("ios", "00008-UDID", "Jane's iPhone", "iOS", existing);
	CHECK(target.name == "Jane's iPhone");
	CHECK(target.platform == "ios");
	CHECK(target.device_id == "00008-UDID");
	CHECK(target.export_preset == "iOS");
	CHECK(target.signing_mode == "automatic");

	// The generated name avoids colliding with an existing target.
	existing.push_back(target);
	const RunTarget second = RunTargetsPanel::make_device_setup_target("ios", "00008-UDID", "Jane's iPhone", "iOS", existing);
	CHECK(second.name == "Jane's iPhone 2");

	// With no device name, the device id is used as the name.
	const RunTarget no_name = RunTargetsPanel::make_device_setup_target("ios", "ABC123", "", "iOS", existing);
	CHECK(no_name.name == "ABC123");
	CHECK(no_name.device_id == "ABC123");

	// With neither name nor id, the device id falls back to "auto" and the name to
	// a non-empty generic label.
	const RunTarget blank = RunTargetsPanel::make_device_setup_target("ios", "", "", "iOS", existing);
	CHECK(blank.device_id == "auto");
	CHECK_FALSE(blank.name.strip_edges().is_empty());
}

TEST_CASE("[RunTargetsPanel] First actionable step selection") {
	Vector<ReadinessStep> steps;

	// All OK -> nothing to act on.
	steps.push_back(make_step("xcode", ReadinessStep::OK));
	steps.push_back(make_step("device", ReadinessStep::OK));
	CHECK(RunTargetsPanel::first_actionable_step_index(steps) == -1);

	// The first non-OK rung is the actionable one, regardless of later statuses.
	steps.clear();
	steps.push_back(make_step("xcode", ReadinessStep::OK));
	steps.push_back(make_step("device", ReadinessStep::ACTION_NEEDED));
	steps.push_back(make_step("trust", ReadinessStep::BLOCKED));
	CHECK(RunTargetsPanel::first_actionable_step_index(steps) == 1);

	// An empty ladder has no actionable step.
	steps.clear();
	CHECK(RunTargetsPanel::first_actionable_step_index(steps) == -1);
}

} // namespace TestRunTargetsPanel

#endif // TOOLS_ENABLED
