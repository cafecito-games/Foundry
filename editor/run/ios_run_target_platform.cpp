/**************************************************************************/
/*  ios_run_target_platform.cpp                                           */
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

#include "ios_run_target_platform.h"

#include "editor/run/run_target_readiness.h"

#include "editor/export/editor_export.h"
#include "editor/export/editor_export_platform_apple_embedded.h"
#include "editor/export/editor_export_preset.h"

#include "core/io/json.h"
#include "core/object/object.h"
#include "core/os/os.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

CommandResult OSCommandRunner::run(const String &p_program, const List<String> &p_arguments) {
	CommandResult result;
	result.error = OS::get_singleton()->execute(p_program, p_arguments, &result.output, &result.exit_code, true);
	return result;
}

Vector<IOSRunTargetPlatform::DeviceInfo> IOSRunTargetPlatform::parse_devicectl_devices(const String &p_json_text) {
	Vector<DeviceInfo> result;
	if (p_json_text.strip_edges().is_empty()) {
		return result;
	}

	const Variant parsed = JSON::parse_string(p_json_text);
	if (parsed.get_type() != Variant::DICTIONARY) {
		return result;
	}

	const Dictionary data = parsed;
	const Dictionary outcome = data.get("result", Dictionary());
	const Array devices = outcome.get("devices", Array());

	for (int i = 0; i < devices.size(); i++) {
		const Dictionary device = devices[i];
		const Dictionary connection_properties = device.get("connectionProperties", Dictionary());
		const Dictionary device_properties = device.get("deviceProperties", Dictionary());

		DeviceInfo info;
		info.id = device.get("identifier", String());
		info.name = device_properties.get("name", String());
		info.wifi = String(connection_properties.get("transportType", String())) == "localNetwork";
		info.paired = String(connection_properties.get("pairingState", String())) == "paired";
		info.developer_mode = String(device_properties.get("developerModeStatus", String())) == "enabled";
		result.push_back(info);
	}

	return result;
}

IOSRunTargetPlatform::IOSRunTargetPlatform() {
	command_runner = memnew(OSCommandRunner);
	owns_command_runner = true;
}

IOSRunTargetPlatform::IOSRunTargetPlatform(CommandRunner *p_command_runner) {
	command_runner = p_command_runner;
	owns_command_runner = false;
}

IOSRunTargetPlatform::~IOSRunTargetPlatform() {
	if (owns_command_runner && command_runner != nullptr) {
		memdelete(command_runner);
	}
}

String IOSRunTargetPlatform::_query_devicectl() {
	ERR_FAIL_NULL_V(command_runner, String());

	List<String> args;
	args.push_back("devicectl");
	args.push_back("list");
	args.push_back("devices");
	args.push_back("-j");
	args.push_back("-");
	args.push_back("-q");
	// Bound the call so a stuck tool cannot hang the editor.
	args.push_back("--timeout");
	args.push_back("5");
	// Mirror the export poll's device-type filter so the run-target list and
	// readiness only consider devices the iOS export platform can deploy to
	// (matching EditorExportPlatformIOS::device_types).
	args.push_back("--filter");
	args.push_back("hardwareProperties.deviceType MATCHES 'iPhone|iPad'");

	const CommandResult outcome = command_runner->run("xcrun", args);
	if (outcome.error != OK || outcome.exit_code != 0) {
		return String();
	}
	return outcome.output;
}

String IOSRunTargetPlatform::_resolve_auto_device_id(const Array &p_devices, const String &p_requested) {
	if (!p_requested.is_empty() && p_requested != "auto") {
		return p_requested;
	}

	String first_connected;
	for (int i = 0; i < p_devices.size(); i++) {
		const Dictionary device = p_devices[i];
		const String identifier = device.get("identifier", String());
		if (first_connected.is_empty()) {
			first_connected = identifier;
		}
		const Dictionary connection_properties = device.get("connectionProperties", Dictionary());
		const Dictionary device_properties = device.get("deviceProperties", Dictionary());
		const bool runnable = String(connection_properties.get("pairingState", String())) == "paired" &&
				String(device_properties.get("developerModeStatus", String())) == "enabled";
		if (runnable) {
			return identifier;
		}
	}

	return first_connected;
}

Vector<ReadinessStep> IOSRunTargetPlatform::probe_readiness(const RunTarget &p_target) {
	ERR_FAIL_NULL_V(command_runner, Vector<ReadinessStep>());

	// Assemble the raw probe snapshot the Doctor consumes. The snapshot matches the
	// shape `RunTargetReadiness::parse_ios_probe` expects, which is also the shape
	// of the captured `tests/data/ios_run_targets/*.json` fixtures.
	Dictionary snapshot;

	// Xcode toolchain: `xcode-select -p` prints the developer dir when installed.
	{
		List<String> args;
		args.push_back("-p");
		const CommandResult outcome = command_runner->run("xcode-select", args);
		if (outcome.error == OK && outcome.exit_code == 0) {
			snapshot["xcode_select_path"] = outcome.output.strip_edges();
		} else {
			snapshot["xcode_select_path"] = String();
		}
	}

	// Device state: the raw `devicectl` device array maps directly onto the probe's
	// expected `devices` shape (identifier / connectionProperties / deviceProperties).
	{
		const String devices_json = _query_devicectl();
		const Variant parsed = JSON::parse_string(devices_json);
		Array devices;
		if (parsed.get_type() == Variant::DICTIONARY) {
			const Dictionary data = parsed;
			const Dictionary outcome = data.get("result", Dictionary());
			devices = outcome.get("devices", Array());
		}
		snapshot["devices"] = devices;
	}

	// Signing team: resolved from the target's remembered selection. A cold probe
	// performs no provisioning resolve, so the provisioning rung is reported as
	// upcoming until a real deploy (or the panel's resolve) maps a failure in.
	snapshot["signing_team"] = p_target.team_id;
	snapshot["provisioning_stderr"] = String();

	// Diagnose the same device a Run would target. For an "auto" target that means
	// the first runnable device, so readiness and Run agree; when none is runnable,
	// fall back to the first connected device so the ladder can still explain what
	// to fix (e.g. Developer Mode off) instead of silently inspecting the wrong one.
	const String device_id = _resolve_auto_device_id(snapshot["devices"], p_target.device_id);

	const RunTargetReadiness::ProbeResult probe = RunTargetReadiness::parse_ios_probe(snapshot, device_id);
	return RunTargetReadiness::evaluate(probe);
}

Vector<RunTargetDevice> IOSRunTargetPlatform::list_devices() {
	Vector<RunTargetDevice> result;

	// In the live editor, surface the same device cache the deploy path matches
	// against, so every device shown here is one `run()` can actually deploy to (no
	// drift between what the adapter lists and what `run_on_device` will accept).
	EditorExportPlatformAppleEmbedded *platform = _find_export_platform();
	if (platform != nullptr) {
		for (const EditorExportPlatformAppleEmbedded::RunnableDeviceInfo &info : platform->get_runnable_devices()) {
			RunTargetDevice device;
			device.id = info.id;
			device.name = info.name;
			device.badge = ReadinessStep::OK;
			result.push_back(device);
		}
		return result;
	}

	// Headless tooling and tests have no export platform; enumerate directly through
	// the command seam instead, applying the same runnable filter as the poll.
	for (const DeviceInfo &info : parse_devicectl_devices(_query_devicectl())) {
		if (!info.paired || !info.developer_mode) {
			continue;
		}
		RunTargetDevice device;
		device.id = info.id;
		device.name = info.name;
		device.badge = ReadinessStep::OK;
		result.push_back(device);
	}

	return result;
}

EditorExportPlatformAppleEmbedded *IOSRunTargetPlatform::_find_export_platform() const {
	EditorExport *editor_export = EditorExport::get_singleton();
	if (editor_export == nullptr) {
		return nullptr;
	}
	for (int i = 0; i < editor_export->get_export_platform_count(); i++) {
		Ref<EditorExportPlatform> platform = editor_export->get_export_platform(i);
		EditorExportPlatformAppleEmbedded *apple = Object::cast_to<EditorExportPlatformAppleEmbedded>(platform.ptr());
		if (apple != nullptr) {
			return apple;
		}
	}
	return nullptr;
}

Ref<EditorExportPreset> IOSRunTargetPlatform::_find_preset(const String &p_name) const {
	EditorExport *editor_export = EditorExport::get_singleton();
	if (editor_export == nullptr) {
		return Ref<EditorExportPreset>();
	}
	for (int i = 0; i < editor_export->get_export_preset_count(); i++) {
		Ref<EditorExportPreset> preset = editor_export->get_export_preset(i);
		if (preset.is_valid() && preset->get_name() == p_name) {
			return preset;
		}
	}
	return Ref<EditorExportPreset>();
}

Error IOSRunTargetPlatform::run(const RunTarget &p_target, int p_debug_flags) {
	EditorExportPlatformAppleEmbedded *platform = _find_export_platform();
	if (platform == nullptr) {
		ERR_PRINT("Cannot run iOS target: no Apple-embedded export platform is registered (macOS with Xcode is required).");
		return ERR_UNAVAILABLE;
	}

	Ref<EditorExportPreset> preset = _find_preset(p_target.export_preset);
	if (preset.is_null()) {
		ERR_PRINT(vformat("Cannot run iOS target \"%s\": export preset \"%s\" no longer exists.", p_target.name, p_target.export_preset));
		return ERR_DOES_NOT_EXIST;
	}

	// Resolve the device to deploy to. A target may remember "auto" (or nothing),
	// meaning "the connected device"; `run_on_device` only matches concrete UUIDs.
	// Resolve "auto" against the export platform's own poll cache (the exact list
	// `run_on_device` searches) so the chosen device is guaranteed deployable and
	// can't drift from a separately enumerated, possibly newer, devicectl query.
	String device_id = p_target.device_id;
	if (device_id.is_empty() || device_id == "auto") {
		const Vector<EditorExportPlatformAppleEmbedded::RunnableDeviceInfo> runnable = platform->get_runnable_devices();
		if (runnable.is_empty()) {
			ERR_PRINT(vformat("Cannot run iOS target \"%s\": no runnable device is connected.", p_target.name));
			return ERR_UNAVAILABLE;
		}
		device_id = runnable[0].id;
	}

	// The target remembers the signing team; the iOS export signs with the preset's
	// `application/app_store_team_id`, and readiness reads the team from the target,
	// so apply the target's team for the duration of this deploy and restore it
	// afterward. This keeps readiness and the deploy in agreement without permanently
	// rewriting the shared preset's signing team for normal exports or other targets.
	// The deploy is synchronous, so no save scheduled by the override can observe the
	// transient value before it is restored.
	const Variant previous_team = preset->get("application/app_store_team_id");
	const bool override_team = !p_target.team_id.is_empty() && String(previous_team) != p_target.team_id;
	if (override_team) {
		preset->set("application/app_store_team_id", p_target.team_id);
	}

	// Hand off to the existing export-to-`.xcarchive` + `devicectl` deploy path,
	// which already passes `-allowProvisioningUpdates` so automatic signing resolves
	// certificates, profiles, and device registration.
	const Error run_error = platform->run_on_device(preset, device_id, p_debug_flags);

	if (override_team) {
		preset->set("application/app_store_team_id", previous_team);
	}
	return run_error;
}
