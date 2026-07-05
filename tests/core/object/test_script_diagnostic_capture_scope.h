/**************************************************************************/
/*  test_script_diagnostic_capture_scope.h                                */
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

#include "modules/modules_enabled.gen.h"

#include "core/core_globals.h"
#include "core/object/script_diagnostic_capture.h"
#include "core/object/script_diagnostic_capture_scope.h"

#include "tests/test_macros.h"

namespace TestScriptDiagnosticCaptureScope {

class SyncCaptureHelper : public Object {
	FOUNDRY_CLASS(SyncCaptureHelper, Object);

public:
	int run() {
		ERR_PRINT("sync capture helper error");
		return 99;
	}
};

TEST_CASE("[ScriptDiagnosticCaptureScope] Scope start/stop captures events and stop is idempotent") {
	Ref<ScriptDiagnosticCaptureScope> scope = ScriptDiagnosticCaptureScope::start();
	CHECK(scope->is_active());

	ERR_PRINT("scope captured error");
	WARN_PRINT("scope captured warning");

	CHECK(scope->get_events().size() == 2);
	CHECK(scope->get_capture()->get_events().size() == 2);

	scope->stop();
	CHECK_FALSE(scope->is_active());
	scope->stop();

	CHECK(scope->get_capture()->get_event_count() == 2);
	CHECK(scope->get_capture()->has_error("scope captured error"));
	CHECK(scope->get_capture()->has_warning("scope captured warning"));
}

TEST_CASE("[ScriptDiagnosticCaptureScope] Destructor restores quiet console printing") {
	const bool errors_enabled_before = CoreGlobals::print_error_enabled;
	CoreGlobals::print_error_enabled = true;

	{
		Ref<ScriptDiagnosticCaptureScope> scope = ScriptDiagnosticCaptureScope::start(true);
		CHECK_FALSE(CoreGlobals::print_error_enabled);
		ERR_PRINT("quiet destructor error");
	}

	CHECK(CoreGlobals::print_error_enabled);
	CoreGlobals::print_error_enabled = errors_enabled_before;
}

TEST_CASE("[ScriptDiagnosticCaptureScope] Nested quiet scopes broadcast events and restore out of order") {
	const bool errors_enabled_before = CoreGlobals::print_error_enabled;
	CoreGlobals::print_error_enabled = true;

	Ref<ScriptDiagnosticCaptureScope> outer = ScriptDiagnosticCaptureScope::start(true);
	Ref<ScriptDiagnosticCaptureScope> inner = ScriptDiagnosticCaptureScope::start(true);
	CHECK_FALSE(CoreGlobals::print_error_enabled);

	ERR_PRINT("nested broadcast error");

	CHECK(outer->get_capture()->has_error("nested broadcast error"));
	CHECK(inner->get_capture()->has_error("nested broadcast error"));

	outer->stop();
	CHECK_FALSE(CoreGlobals::print_error_enabled);

	inner->stop();
	CHECK(CoreGlobals::print_error_enabled);

	CoreGlobals::print_error_enabled = errors_enabled_before;
}

TEST_CASE("[ScriptDiagnosticCaptureScope] capture() returns sync callable value and events") {
	SyncCaptureHelper helper;
	const Callable callable = callable_mp(&helper, &SyncCaptureHelper::run);
	const Ref<ScriptDiagnosticCaptureResult> result = ScriptDiagnosticCaptureScope::capture(callable);

	CHECK(result.is_valid());
	CHECK_EQ(int(result->get_return_value()), 99);
	CHECK(result->get_capture().is_valid());
	CHECK_FALSE(result->get_capture()->is_active());
	CHECK(result->get_capture()->has_error("sync capture helper error"));
}

TEST_CASE("[ScriptDiagnosticCapture] has_diagnostic_containing matches substrings without changing exact match") {
	Ref<ScriptDiagnosticCapture> capture;
	capture.instantiate();
	capture->start();

	ERR_PRINT("prefix captured error suffix");
	capture->stop();

	CHECK(capture->has_error_containing("captured error"));
	CHECK_FALSE(capture->has_error_containing("missing text"));
	CHECK(capture->has_error("prefix captured error suffix"));
	CHECK_FALSE(capture->has_error("captured error"));
}

TEST_CASE("[ScriptDiagnosticCapture] is_supported returns true") {
	CHECK(ScriptDiagnosticCapture::is_supported());
}

TEST_CASE("[ScriptDiagnosticCapture] Event dictionaries expose the stable schema") {
	Ref<ScriptDiagnosticCapture> capture;
	capture.instantiate();
	capture->start();

	ERR_PRINT("schema error");
	WARN_PRINT("schema warning");
	capture->stop();

	const Dictionary event = capture->get_event(0);
	CHECK(event.has("severity"));
	CHECK(event.has("severity_name"));
	CHECK(event.has("message"));
	CHECK(event.has("code"));
	CHECK(event.has("rationale"));
	CHECK(event.has("function"));
	CHECK(event.has("file"));
	CHECK(event.has("line"));
	CHECK(event.has("editor_notify"));

	CHECK(String(event["severity_name"]) == "error");
	CHECK(String(event["message"]) == "schema error");
}

} // namespace TestScriptDiagnosticCaptureScope

#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED

#include "modules/foundry_script/foundry_script.h"
#include "modules/foundry_script/tests/fs_test_runner.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/object/script_function_state.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "tests/core/config/test_project_settings.h"

namespace TestScriptDiagnosticCaptureScopeAsync {

class CaptureAsyncListener : public Object {
	FOUNDRY_CLASS(CaptureAsyncListener, Object);

public:
	Ref<ScriptDiagnosticCaptureResult> result;
	bool done = false;

	void on_completed(const Variant &p_result) {
		result = p_result;
		done = true;
	}
};

struct DiagnosticCaptureFixture {
	Node *suite = nullptr;
	TestProjectSettingsRestoreScope project_settings;

	explicit DiagnosticCaptureFixture() {
		const String scripts_path = String("modules/foundry_script/tests/scripts");
		const Error err = ProjectSettings::get_singleton()->setup(scripts_path, String(), true);
		REQUIRE_MESSAGE(err == OK, "Failed to set up diagnostic capture project.");
		if (!FSTests::is_fs_language_active()) {
			FSTests::init_language(scripts_path);
		}

		Ref<Script> script = ResourceLoader::load("res://script_test_execution/fixture.notest.fs");
		REQUIRE(script.is_valid());
		REQUIRE(script->can_instantiate());
		suite = memnew(Node);
		suite->set_script(script);
		SceneTree::get_singleton()->get_root()->add_child(suite);
	}

	~DiagnosticCaptureFixture() {
		if (suite) {
			suite->queue_free();
			suite = nullptr;
		}
	}
};

static Ref<ScriptDiagnosticCaptureResult> run_capture_async(Node *p_suite, const StringName &p_method, bool p_quiet = false, int p_max_frames = 240) {
	const Callable callable(p_suite, p_method);
	const Variant pending = ScriptDiagnosticCaptureScope::capture_async(callable, p_quiet);

	Ref<ScriptDiagnosticCaptureResult> immediate = pending;
	if (immediate.is_valid()) {
		return immediate;
	}

	ScriptDiagnosticCapturePendingState *pending_state = Object::cast_to<ScriptDiagnosticCapturePendingState>(pending);
	REQUIRE(pending_state != nullptr);
	if (pending_state == nullptr) {
		// Exceptions are disabled, so a failed REQUIRE does not abort the test case. Bail out
		// instead of dereferencing null and killing the whole suite run with a SIGSEGV.
		return Ref<ScriptDiagnosticCaptureResult>();
	}

	CaptureAsyncListener listener;
	pending_state->connect(SNAME("completed"), callable_mp(&listener, &CaptureAsyncListener::on_completed), Object::CONNECT_ONE_SHOT);

	SceneTree *tree = SceneTree::get_singleton();
	REQUIRE(tree != nullptr);
	if (tree == nullptr) {
		return Ref<ScriptDiagnosticCaptureResult>();
	}
	for (int frame = 0; frame < p_max_frames && !listener.done; frame++) {
		tree->process(1.0 / 60.0);
		if (FSLanguage::get_singleton()) {
			FSLanguage::get_singleton()->frame();
		}
	}

	REQUIRE(listener.done);
	REQUIRE(listener.result.is_valid());
	return listener.result;
}

TEST_CASE("[ScriptDiagnosticCaptureScope][SceneTree] capture_async records async diagnostics and stops capture") {
	const bool errors_enabled_before = CoreGlobals::print_error_enabled;
	CoreGlobals::print_error_enabled = true;

	DiagnosticCaptureFixture fixture;
	const Ref<ScriptDiagnosticCaptureResult> result = run_capture_async(fixture.suite, SNAME("async_capture_diagnostic"), true);
	REQUIRE(result.is_valid());
	if (result.is_null()) {
		CoreGlobals::print_error_enabled = errors_enabled_before;
		return;
	}

	CHECK_EQ(int(result->get_return_value()), 13);
	CHECK(result->get_capture().is_valid());
	CHECK_FALSE(result->get_capture()->is_active());
	CHECK(result->get_capture()->has_error("async capture error"));
	CHECK(CoreGlobals::print_error_enabled);

	CoreGlobals::print_error_enabled = errors_enabled_before;
}

TEST_CASE("[ScriptDiagnosticCaptureScope][SceneTree] capture_async runtime error stops capture and records diagnostic") {
	const bool errors_enabled_before = CoreGlobals::print_error_enabled;
	CoreGlobals::print_error_enabled = true;

	DiagnosticCaptureFixture fixture;
	const Ref<ScriptDiagnosticCaptureResult> result = run_capture_async(fixture.suite, SNAME("runtime_error"), true);
	REQUIRE(result.is_valid());
	if (result.is_null()) {
		CoreGlobals::print_error_enabled = errors_enabled_before;
		return;
	}

	CHECK(result->get_return_value().get_type() == Variant::NIL);
	CHECK(result->get_capture().is_valid());
	CHECK_FALSE(result->get_capture()->is_active());
	CHECK(result->get_capture()->get_event_count() > 0);
	CHECK(result->get_capture()->has_diagnostic_containing(ScriptDiagnosticCapture::SEVERITY_SCRIPT_ERROR, "index"));
	CHECK(CoreGlobals::print_error_enabled);

	CoreGlobals::print_error_enabled = errors_enabled_before;
}

TEST_CASE("[ScriptDiagnosticCaptureScope][SceneTree] Project settings restore keeps user:// writable") {
	{
		DiagnosticCaptureFixture fixture;
		(void)fixture;
	}

	Ref<FileAccess> file = FileAccess::open("user://diagnostic_capture_restore_probe.fs", FileAccess::WRITE);
	CHECK(file.is_valid());
	if (file.is_valid()) {
		DirAccess::remove_absolute("user://diagnostic_capture_restore_probe.fs");
	}
}

} // namespace TestScriptDiagnosticCaptureScopeAsync

#endif // MODULE_FOUNDRY_SCRIPT_ENABLED
