/**************************************************************************/
/*  test_strict_activation.h                                              */
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

#include "tests/test_macros.h"

#include "../editor/fs_strict_activation.h"
#include "../editor/fs_verification_harness.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"

#include "test_refactor.h" // FSTests::TemporaryScriptFile, initialize, root, finish_language

#ifndef FOUNDRY_SCRIPT_NO_LSP

#include "editor/file_system/editor_file_system.h"

namespace FSTests {

// Restores the two strict project settings to their prior values when it goes out of scope, so a
// test that flips them does not leak state into later tests (the analyzer reads these globally). A
// confirmed activation also persists project.godot, so the guard snapshots the on-disk file and
// restores it byte-for-byte, keeping the curated test fixture (and its comments) intact rather than
// letting ProjectSettings::save() rewrite it with full defaults.
struct StrictSettingsGuard {
	Variant prior_null;
	Variant prior_dynamic;
	String project_path;
	bool had_project_file = false;
	PackedByteArray project_bytes;

	StrictSettingsGuard() {
		ProjectSettings *settings = ProjectSettings::get_singleton();
		prior_null = settings->get_setting("debug/foundry_script/analysis/strict_null_checks", false);
		prior_dynamic = settings->get_setting("debug/foundry_script/analysis/strict_dynamic_checks", false);

		project_path = settings->globalize_path("res://project.godot");
		if (FileAccess::exists(project_path)) {
			had_project_file = true;
			project_bytes = FileAccess::get_file_as_bytes(project_path);
		}
	}

	~StrictSettingsGuard() {
		ProjectSettings *settings = ProjectSettings::get_singleton();
		settings->set_setting("debug/foundry_script/analysis/strict_null_checks", prior_null);
		settings->set_setting("debug/foundry_script/analysis/strict_dynamic_checks", prior_dynamic);

		// Restore the exact on-disk project.godot so a persisted flip never leaks onto disk.
		if (had_project_file) {
			Ref<FileAccess> file = FileAccess::open(project_path, FileAccess::WRITE);
			if (file.is_valid()) {
				file->store_buffer(project_bytes.ptr(), project_bytes.size());
			}
		}
	}
};

TEST_SUITE("[Modules][FoundryScript][StrictActivation]") {
	TEST_CASE("Evaluate gates on a clean project and reports activation is allowed") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		// A fully typed file has no strict-mode violations, so the gate is clean.
		const String path = "res://refactor/activation_clean.fs";
		const String source =
				"func add(a: int, b: int) -> int:\n"
				"\treturn a + b\n";
		TemporaryScriptFile file(path, source);

		StrictActivationRequest request;
		request.strict_null_checks = true;
		request.strict_dynamic_checks = true;
		StrictActivationPlan plan = FSStrictActivation::evaluate({ path }, request);
		REQUIRE(plan.ok);
		CHECK(plan.clean);
		CHECK_EQ(plan.violations.size(), 0);

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Evaluate reports the gate is not clean when violations remain") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		// Assigning a Variant to a typed local is a strict_dynamic_checks violation.
		const String path = "res://refactor/activation_dirty.fs";
		const String source =
				"func dyn():\n"
				"\treturn JSON.parse_string(\"1\")\n"
				"func use() -> void:\n"
				"\tvar x: int = dyn()\n";
		TemporaryScriptFile file(path, source);

		StrictActivationRequest request;
		request.strict_dynamic_checks = true;
		StrictActivationPlan plan = FSStrictActivation::evaluate({ path }, request);
		REQUIRE(plan.ok);
		CHECK_FALSE(plan.clean);
		CHECK_GT(plan.violations.size(), 0);

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Activation without explicit confirmation never flips the settings") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);
		StrictSettingsGuard guard;

		// Start from a known-off baseline.
		ProjectSettings *settings = ProjectSettings::get_singleton();
		settings->set_setting("debug/foundry_script/analysis/strict_null_checks", false);
		settings->set_setting("debug/foundry_script/analysis/strict_dynamic_checks", false);

		const String path = "res://refactor/activation_noconfirm.fs";
		const String source =
				"func add(a: int, b: int) -> int:\n"
				"\treturn a + b\n";
		TemporaryScriptFile file(path, source);

		StrictActivationRequest request;
		request.strict_null_checks = true;
		request.strict_dynamic_checks = true;
		request.confirmed = false; // No explicit confirmation.
		StrictActivationResult result = FSStrictActivation::activate({ path }, request);
		REQUIRE(result.ok);
		CHECK_FALSE(result.activated);
		CHECK_FALSE(result.blocked_reason.is_empty());

		// The settings must be untouched.
		CHECK_FALSE((bool)settings->get_setting("debug/foundry_script/analysis/strict_null_checks", false));
		CHECK_FALSE((bool)settings->get_setting("debug/foundry_script/analysis/strict_dynamic_checks", false));

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Confirmed activation on a clean project flips the settings and the post-flip report is clean") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);
		StrictSettingsGuard guard;

		ProjectSettings *settings = ProjectSettings::get_singleton();
		settings->set_setting("debug/foundry_script/analysis/strict_null_checks", false);
		settings->set_setting("debug/foundry_script/analysis/strict_dynamic_checks", false);

		const String path = "res://refactor/activation_confirm_clean.fs";
		const String source =
				"func add(a: int, b: int) -> int:\n"
				"\treturn a + b\n";
		TemporaryScriptFile file(path, source);

		StrictActivationRequest request;
		request.strict_null_checks = true;
		request.strict_dynamic_checks = true;
		request.confirmed = true;
		StrictActivationResult result = FSStrictActivation::activate({ path }, request);
		REQUIRE(result.ok);
		CHECK(result.activated);
		CHECK(result.strict_null_checks_set);
		CHECK(result.strict_dynamic_checks_set);

		// The settings were actually written (live for the session).
		CHECK((bool)settings->get_setting("debug/foundry_script/analysis/strict_null_checks", false));
		CHECK((bool)settings->get_setting("debug/foundry_script/analysis/strict_dynamic_checks", false));

		// Persistence is reported honestly: either it was saved to disk, or a save error explains
		// why the live flip is not yet durable. Never both empty (silent loss on restart).
		CHECK((result.persisted) == (result.persist_error.is_empty()));

		// The post-flip report is clean.
		CHECK(result.post_flip.ok);
		CHECK_EQ(result.post_flip.violations.size(), 0);

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Confirmed activation on a dirty project is blocked unless violations are explicitly allowed") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);
		StrictSettingsGuard guard;

		ProjectSettings *settings = ProjectSettings::get_singleton();
		settings->set_setting("debug/foundry_script/analysis/strict_dynamic_checks", false);

		const String path = "res://refactor/activation_dirty_confirm.fs";
		const String source =
				"func dyn():\n"
				"\treturn JSON.parse_string(\"1\")\n"
				"func use() -> void:\n"
				"\tvar x: int = dyn()\n";
		TemporaryScriptFile file(path, source);

		// Confirmed, but the gate is not clean and violations are not allowed: refuse to flip.
		StrictActivationRequest blocked;
		blocked.strict_dynamic_checks = true;
		blocked.confirmed = true;
		StrictActivationResult blocked_result = FSStrictActivation::activate({ path }, blocked);
		REQUIRE(blocked_result.ok);
		CHECK_FALSE(blocked_result.activated);
		CHECK_FALSE(blocked_result.blocked_reason.is_empty());
		CHECK_FALSE((bool)settings->get_setting("debug/foundry_script/analysis/strict_dynamic_checks", false));

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Gradual activation flips on a dirty project and the post-flip report matches the strict preview") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);
		StrictSettingsGuard guard;

		ProjectSettings *settings = ProjectSettings::get_singleton();
		settings->set_setting("debug/foundry_script/analysis/strict_dynamic_checks", false);

		const String path = "res://refactor/activation_gradual.fs";
		const String source =
				"func dyn():\n"
				"\treturn JSON.parse_string(\"1\")\n"
				"func use() -> void:\n"
				"\tvar x: int = dyn()\n";
		TemporaryScriptFile file(path, source);

		// The independent strict preview of the same flag is the source of truth the post-flip
		// report must match.
		VerificationOptions preview_options;
		preview_options.strict_dynamic_checks = true;
		StrictPreviewResult preview = FSVerificationHarness::preview_strict({ path }, preview_options);
		REQUIRE(preview.ok);
		REQUIRE_GT(preview.violations.size(), 0);

		// Confirmed and explicitly allowing violations: the gradual (warn-before-error) path flips
		// despite the remaining violations.
		StrictActivationRequest request;
		request.strict_dynamic_checks = true;
		request.confirmed = true;
		request.allow_with_violations = true;
		StrictActivationResult result = FSStrictActivation::activate({ path }, request);
		REQUIRE(result.ok);
		CHECK(result.activated);
		CHECK(result.strict_dynamic_checks_set);
		CHECK((bool)settings->get_setting("debug/foundry_script/analysis/strict_dynamic_checks", false));

		// The post-flip violation report matches the strict-mode preview.
		REQUIRE(result.post_flip.ok);
		REQUIRE_EQ(result.post_flip.violations.size(), preview.violations.size());
		for (int i = 0; i < preview.violations.size(); i++) {
			CHECK_EQ(result.post_flip.violations[i].path, preview.violations[i].path);
			CHECK_EQ(result.post_flip.violations[i].line, preview.violations[i].line);
			CHECK_EQ(result.post_flip.violations[i].column, preview.violations[i].column);
			CHECK_EQ(result.post_flip.violations[i].message, preview.violations[i].message);
			CHECK_EQ(result.post_flip.violations[i].category, preview.violations[i].category);
		}

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Activation only flips the flags the request asks for") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);
		StrictSettingsGuard guard;

		ProjectSettings *settings = ProjectSettings::get_singleton();
		settings->set_setting("debug/foundry_script/analysis/strict_null_checks", false);
		settings->set_setting("debug/foundry_script/analysis/strict_dynamic_checks", false);

		const String path = "res://refactor/activation_partial.fs";
		const String source =
				"func add(a: int, b: int) -> int:\n"
				"\treturn a + b\n";
		TemporaryScriptFile file(path, source);

		// Request only the dynamic flag.
		StrictActivationRequest request;
		request.strict_dynamic_checks = true;
		request.confirmed = true;
		StrictActivationResult result = FSStrictActivation::activate({ path }, request);
		REQUIRE(result.ok);
		CHECK(result.activated);
		CHECK(result.strict_dynamic_checks_set);
		CHECK_FALSE(result.strict_null_checks_set);

		// Only the dynamic flag was flipped; the null flag is untouched.
		CHECK((bool)settings->get_setting("debug/foundry_script/analysis/strict_dynamic_checks", false));
		CHECK_FALSE((bool)settings->get_setting("debug/foundry_script/analysis/strict_null_checks", false));

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("A feature override that masks the flipped setting is reported, not claimed live") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);
		StrictSettingsGuard guard;

		ProjectSettings *settings = ProjectSettings::get_singleton();
		settings->set_setting("debug/foundry_script/analysis/strict_dynamic_checks", false);

		// Install a per-feature override that resolves the effective value to false for one of the
		// project's active features, so writing the base key true does not make strict mode live.
		const Vector<String> features = ProjectSettings::get_singleton()->get_setting("application/config/features", PackedStringArray());
		String feature = "editor";
		if (!features.is_empty()) {
			feature = features[0];
		}
		const String override_key = "debug/foundry_script/analysis/strict_dynamic_checks." + feature;
		settings->set_setting(override_key, false);

		const String path = "res://refactor/activation_override.fs";
		const String source =
				"func add(a: int, b: int) -> int:\n"
				"\treturn a + b\n";
		TemporaryScriptFile file(path, source);

		StrictActivationRequest request;
		request.strict_dynamic_checks = true;
		request.confirmed = true;
		StrictActivationResult result = FSStrictActivation::activate({ path }, request);
		REQUIRE(result.ok);
		CHECK(result.activated); // The base key was written.

		// Only assert the masking report when the override actually resolves the effective value to
		// false; if this build's active features do not include the chosen one, the override is inert.
		if (!(bool)settings->get_setting_with_override("debug/foundry_script/analysis/strict_dynamic_checks")) {
			CHECK(result.override_masked);
			CHECK_FALSE(result.effective_warning.is_empty());
		}

		// Clean up the override so it does not leak into later tests.
		settings->set_setting(override_key, Variant());

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("An empty request flips nothing and is reported as a no-op") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);
		StrictSettingsGuard guard;

		StrictActivationRequest request;
		request.confirmed = true; // Confirmed, but no flag requested.
		StrictActivationResult result = FSStrictActivation::activate({}, request);
		REQUIRE(result.ok);
		CHECK_FALSE(result.activated);
		CHECK_FALSE(result.blocked_reason.is_empty());

		memdelete(protocol);
		memdelete(editor_file_system);
	}
}

} // namespace FSTests

#endif // FOUNDRY_SCRIPT_NO_LSP

#endif // TOOLS_ENABLED
