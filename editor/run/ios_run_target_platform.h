/**************************************************************************/
/*  ios_run_target_platform.h                                             */
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

#include "editor/run/run_target_platform.h"

#include "core/error/error_list.h"
#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/templates/list.h"
#include "core/templates/vector.h"

// Outcome of running a single command through a `CommandRunner`.
struct CommandResult {
	Error error = FAILED; // OK when the process was launched (regardless of exit code).
	int exit_code = -1; // The process exit code; meaningful only when `error == OK`.
	String output; // Combined process output (stdout, and stderr where captured).
};

// The seam the iOS adapter shells out through. The production implementation runs
// real tools (`xcode-select`, `xcrun devicectl`); tests inject a fake that returns
// captured fixtures, so readiness probing and device enumeration are unit-testable
// without Xcode or a connected device.
class CommandRunner {
public:
	virtual CommandResult run(const String &p_program, const List<String> &p_arguments) = 0;
	virtual ~CommandRunner() {}
};

// Default `CommandRunner` that shells out via `OS::execute`. macOS only in
// practice (the iOS tools exist nowhere else), but it compiles everywhere.
class OSCommandRunner : public CommandRunner {
public:
	virtual CommandResult run(const String &p_program, const List<String> &p_arguments) override;
};

class EditorExportPlatformAppleEmbedded;
class EditorExportPreset;

// iOS adapter for the run-target layer. It does not reimplement device
// enumeration or deploy: `list_devices()` parses `devicectl` output with the same
// routine the export platform's poll thread now uses, and `run()` hands off to the
// export platform's extracted `run_on_device` path (which already exports to a
// `.xcarchive` with `-allowProvisioningUpdates` for automatic signing).
// `probe_readiness()` gathers raw signals through the injectable `CommandRunner`
// and lets `RunTargetReadiness` decide the ladder, keeping detection pure.
class IOSRunTargetPlatform : public RunTargetPlatform {
public:
	// A device parsed from `xcrun devicectl list devices -j`. Carries the fields
	// both the export poll thread (icon, deploy transport) and the run-target layer
	// need, so the JSON shape is parsed in exactly one place.
	struct DeviceInfo {
		String id;
		String name; // Raw device name, without any UI suffix.
		bool wifi = false;
		bool paired = false;
		bool developer_mode = false;
	};

	// Parses the JSON emitted by `xcrun devicectl list devices -j -` into the
	// device list. Shared by the export platform's poll thread and this adapter so
	// enumeration is never duplicated. Returns an empty list for malformed input.
	static Vector<DeviceInfo> parse_devicectl_devices(const String &p_json_text);

	// Constructs the adapter with the production `OSCommandRunner`.
	IOSRunTargetPlatform();
	// Constructs the adapter with an injected runner (not owned by the adapter).
	explicit IOSRunTargetPlatform(CommandRunner *p_command_runner);
	~IOSRunTargetPlatform();

	virtual Vector<ReadinessStep> probe_readiness(const RunTarget &p_target) override;
	virtual Vector<RunTargetDevice> list_devices() override;
	virtual Error run(const RunTarget &p_target, int p_debug_flags) override;

private:
	CommandRunner *command_runner = nullptr;
	bool owns_command_runner = false;

	// Runs `xcrun devicectl list devices` and returns its raw output, or an empty
	// string when the tool is unavailable or fails.
	String _query_devicectl();

	// Locates the registered iOS export platform (the deploy/enumeration owner), or
	// nullptr when running headless or off macOS.
	EditorExportPlatformAppleEmbedded *_find_export_platform() const;
	Ref<EditorExportPreset> _find_preset(const String &p_name) const;
};
