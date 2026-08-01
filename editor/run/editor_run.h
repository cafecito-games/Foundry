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

	List<OS::ProcessID> pids;

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
	Status status;
	String running_scene;

public:
	inline static EditorRunInstanceStarting instance_starting_callback = nullptr;
	inline static EditorRunInstanceRequestScreenshot instance_rq_screenshot_callback = nullptr;

	Status get_status() const;
	String get_running_scene() const;

	Error run(const String &p_scene, const String &p_write_movie = "", const Vector<String> &p_run_args = Vector<String>());
	Error run_project_test(const TestLaunch &p_launch);
	void run_native_notify() { status = STATUS_PLAY; }
	void stop();

	void stop_child_process(OS::ProcessID p_pid);
	bool has_child_process(OS::ProcessID p_pid) const;
	int get_child_process_count() const { return pids.size(); }
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
};
