/**************************************************************************/
/*  editor_run.h                                                          */
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

#include "core/os/os.h"
#include "core/templates/local_vector.h"

typedef void (*EditorRunInstanceStarting)(int p_index, List<String> &r_arguments);
typedef bool (*EditorRunInstanceRequestScreenshot)(const Callable &p_callback);

class EditorRun {
public:
	enum Status {
		STATUS_PLAY,
		STATUS_PAUSED,
		STATUS_STOP
	};

	// One child of a launch that finished on its own, as recorded by the OS.
	struct ProcessCompletion {
		uint64_t launch_id = 0;
		OS::ProcessID pid = 0;
		// Taken straight from the OS record. `-1` means the platform could not supply
		// a trustworthy result and must never be reported as a successful exit.
		int exit_code = -1;
	};

	// The only runner adapter protocol revision this editor knows how to drive.
	static const int TEST_ADAPTER_PROTOCOL_VERSION = 1;

	// A structured `project test` launch, as requested through the debug adapter's
	// `foundry/launch` object. The adapter fields are passed to the runner script
	// after `--`; everything else is an engine option and stays before it.
	struct TestLaunch {
		String runner;
		int adapter_protocol_version = 0;
		String report_path;
		Vector<String> test_ids;
	};

	// Everything the argument builder needs from editor singletons, resolved by the
	// caller so the builder itself stays a pure function that tests can exercise
	// without an editor instance.
	struct LaunchContext {
		Vector<String> forwardable_arguments;
		String resource_path;
		String debug_uri;
		OS::ProcessID editor_pid = 0;
	};

	struct WindowPlacement {
		int screen = 0;
		Point2i position = Point2i(INT_MAX, INT_MAX);
		Size2i size;
		bool force_maximized = false;
		bool force_fullscreen = false;
	};

private:
	// A child of this run, tagged with the launch that created it so a replaced
	// launch's completion can never be attributed to its replacement.
	struct OwnedChild {
		OS::ProcessID pid = 0;
		uint64_t launch_id = 0;
	};

	Status status;
	String running_scene;
	List<OwnedChild> children;
	uint64_t launch_id = 0;

public:
	inline static EditorRunInstanceStarting instance_starting_callback = nullptr;
	inline static EditorRunInstanceRequestScreenshot instance_rq_screenshot_callback = nullptr;

	Status get_status() const;
	String get_running_scene() const;

	Error run(const String &p_scene, const String &p_write_movie = "", const Vector<String> &p_run_args = Vector<String>());
	Error run_project_test(const TestLaunch &p_launch);
	// A native run owns no local child, so it clears the launch identity: nothing it
	// starts can supply a process result.
	void run_native_notify() {
		status = STATUS_PLAY;
		launch_id = 0;
	}
	void stop();

	// Opens a new launch identity. Identities are unique and increasing across every
	// live run, so completions can be matched to the launch that produced them.
	uint64_t begin_launch();
	uint64_t get_launch_id() const { return launch_id; }

	// Registers an already created process as a child of the current launch.
	void adopt_child_process(OS::ProcessID p_pid);
	// Nonblocking. Reports one child that finished on its own since the last call and
	// drops it from ownership, so a naturally exited process is neither reported twice
	// nor killed afterwards. Explicit stop/kill cleanup never produces a completion.
	bool poll_child_completion(ProcessCompletion &r_completion);

	void stop_child_process(OS::ProcessID p_pid);
	bool has_child_process(OS::ProcessID p_pid) const;
	int get_child_process_count() const { return children.size(); }
	OS::ProcessID get_current_process() const;

	static bool request_screenshot(const Callable &p_callback);

	static LaunchContext build_launch_context();
	static List<String> build_project_test_arguments(const LaunchContext &p_context, const TestLaunch &p_launch);

	// Terminates every process launched by any live `EditorRun`, leaving debuggees
	// that were merely attached to untouched. Used by the tooling host's orderly
	// shutdown, which has no access to the run bar that owns the instance.
	static void stop_all_launched_children();

	static WindowPlacement get_window_placement();

	EditorRun();
	~EditorRun();

private:
	inline static LocalVector<EditorRun *> live_instances;
	inline static uint64_t next_launch_id = 1;
};
