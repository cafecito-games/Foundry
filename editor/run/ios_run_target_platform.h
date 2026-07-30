/**************************************************************************/
/*  ios_run_target_platform.h                                             */
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

#include "core/error/error_list.h"
#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/templates/list.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"

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

// A legacy `ios_deploy` (pre-Xcode 15) device carried from the export platform's
// poll cache into the readiness probe. The legacy enumerator surfaces a device
// only when it is paired and deployable and exposes no trust/Developer-Mode
// detail, so readiness treats every such device as connected and ready.
struct IOSDeployDevice {
	String id;
	String name;
};

// iOS adapter for the run-target layer. It does not reimplement device
// enumeration or deploy: `list_devices()` parses `devicectl` output with the same
// routine the export platform's poll thread now uses, and `run()` hands off to the
// export platform's extracted `run_on_device` path (which already exports to a
// `.xcarchive` with `-allowProvisioningUpdates` for automatic signing).
// `probe_readiness()` gathers raw signals through the injectable `CommandRunner`,
// merges in the legacy `ios_deploy` devices from the export poll cache so the
// ladder agrees with the device list, and lets `RunTargetReadiness` decide the
// ladder, keeping detection pure.
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

	// Builds the readiness probe's `devices` array from the two enumeration
	// sources the export poll also combines: the modern `devicectl` JSON and the
	// legacy `ios_deploy` devices. The `ios_deploy` devices come first so they
	// match the export poll's ordering (keeping "auto" readiness and Run on the
	// same device) and are emitted in `devicectl` shape, reported as paired with
	// Developer Mode enabled so the ladder agrees with the device list. Devices
	// from `p_devicectl_json` follow in their native shape (including any that are
	// connected but not yet runnable, so the ladder can still explain the blocker).
	static Array build_probe_devices(const String &p_devicectl_json, const Vector<IOSDeployDevice> &p_ios_deploy_devices);

	// Extracts the signing team from one decoded provisioning profile (the XML
	// plist embedded in its CMS wrapper). Reads the first `TeamIdentifier` entry as
	// the id and `TeamName` as the label, falling back to the id when no name is
	// present. Returns false when the payload carries no team identifier. Pure so
	// the team parser is unit-testable from captured fixtures.
	static bool parse_provisioning_profile_team(const String &p_decoded_plist, SigningTeam &r_team);

	// Parses a batch of decoded provisioning profiles into the distinct signing
	// teams they reference, preserving first-seen order and deduplicating by team
	// id. Profiles without a team are skipped. Pure; the live `list_signing_teams`
	// is thin glue over this.
	static Vector<SigningTeam> parse_signing_teams(const Vector<String> &p_decoded_plists);

	// Slices the cleartext `<?xml ... </plist>` payload out of a provisioning
	// profile's raw CMS (PKCS#7) bytes, returning an empty string when no plist is
	// present. Pure, so the extraction the live path relies on (instead of shelling
	// out `security cms -D` with an attacker-influenced filename) is unit-testable.
	static String extract_plist_from_profile(const Vector<uint8_t> &p_bytes);

	// Constructs the adapter with the production `OSCommandRunner`.
	IOSRunTargetPlatform();
	// Constructs the adapter with an injected runner (not owned by the adapter).
	explicit IOSRunTargetPlatform(CommandRunner *p_command_runner);
	~IOSRunTargetPlatform();

	virtual Vector<ReadinessStep> probe_readiness(const RunTarget &p_target) override;
	virtual Vector<RunTargetDevice> list_devices() override;
	virtual Vector<SigningTeam> list_signing_teams() override;
	virtual Error run(const RunTarget &p_target, int p_debug_flags) override;

private:
	CommandRunner *command_runner = nullptr;
	bool owns_command_runner = false;

	// Lists the absolute paths of the installed provisioning profiles
	// (`~/Library/MobileDevice/Provisioning Profiles/*.mobileprovision` and
	// `*.provisionprofile`). Empty when the directory is absent (no profiles, or
	// off macOS). Live filesystem glue, mirrored by the readiness probe seams.
	Vector<String> _list_provisioning_profile_paths() const;

	// Reads one provisioning profile from disk and returns its embedded plist via
	// `extract_plist_from_profile`. Empty when the file is unreadable or has no
	// plist payload.
	static String _read_provisioning_profile_plist(const String &p_path);

	// Runs `xcrun devicectl list devices` and returns its raw output, or an empty
	// string when the tool is unavailable or fails.
	String _query_devicectl();

	// Resolves the device an "auto"/empty `p_requested` selection refers to, given a
	// `devicectl`-shaped `p_devices` array: the first runnable (paired + Developer
	// Mode) device, or the first connected device when none is runnable. A concrete
	// `p_requested` is returned unchanged.
	static String _resolve_auto_device_id(const Array &p_devices, const String &p_requested);

	// Locates the registered iOS export platform (the deploy/enumeration owner), or
	// nullptr when running headless or off macOS.
	EditorExportPlatformAppleEmbedded *_find_export_platform() const;
	Ref<EditorExportPreset> _find_preset(const String &p_name) const;
};
