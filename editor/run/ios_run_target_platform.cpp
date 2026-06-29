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

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/plist.h"
#include "core/object/object.h"
#include "core/os/os.h"
#include "core/templates/hash_set.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

CommandResult OSCommandRunner::run(const String &p_program, const List<String> &p_arguments) {
	CommandResult result;
	// Capture stdout only. `devicectl --json-output -` writes its JSON to stdout while
	// progress and "Failed ..." diagnostics go to stderr; merging stderr in would
	// corrupt the JSON (and spam the log via the parser) even when the command itself
	// succeeds. A genuine failure is detected through the exit code instead.
	result.error = OS::get_singleton()->execute(p_program, p_arguments, &result.output, &result.exit_code, false);
	return result;
}

// Parses `p_json_text` as a JSON object, quietly. Unlike `JSON::parse_string`, this
// does not emit an error when the text is not valid JSON: `devicectl` failures yield
// a human-readable "Failed ..." string rather than JSON, and that is an expected,
// non-exceptional outcome the callers handle by reporting no devices (issue #702).
static bool parse_devicectl_object(const String &p_json_text, Dictionary &r_data) {
	if (p_json_text.strip_edges().is_empty()) {
		return false;
	}
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(p_json_text) != OK) {
		return false;
	}
	const Variant parsed = json->get_data();
	if (parsed.get_type() != Variant::DICTIONARY) {
		return false;
	}
	r_data = parsed;
	return true;
}

Vector<IOSRunTargetPlatform::DeviceInfo> IOSRunTargetPlatform::parse_devicectl_devices(const String &p_json_text) {
	Vector<DeviceInfo> result;

	Dictionary data;
	if (!parse_devicectl_object(p_json_text, data)) {
		return result;
	}

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

Array IOSRunTargetPlatform::build_probe_devices(const String &p_devicectl_json, const Vector<IOSDeployDevice> &p_ios_deploy_devices) {
	Array devices;

	// Legacy ios_deploy devices first, matching the export poll's ordering so an
	// "auto" target's readiness and Run resolve to the same device. The legacy
	// enumerator only reports paired, deployable devices and exposes no trust or
	// Developer-Mode detail, so each is emitted in devicectl shape as paired with
	// Developer Mode enabled (i.e. fully ready), agreeing with what the device
	// list and run() already treat as runnable.
	for (const IOSDeployDevice &ios_deploy_device : p_ios_deploy_devices) {
		Dictionary connection_properties;
		connection_properties["pairingState"] = "paired";

		Dictionary device_properties;
		device_properties["name"] = ios_deploy_device.name;
		device_properties["developerModeStatus"] = "enabled";

		Dictionary entry;
		entry["identifier"] = ios_deploy_device.id;
		entry["connectionProperties"] = connection_properties;
		entry["deviceProperties"] = device_properties;
		devices.push_back(entry);
	}

	// Then the devicectl devices in their native shape, preserving connected but
	// not-yet-runnable devices so the ladder can still diagnose them. A failed probe
	// yields non-JSON "Failed ..." text, which is ignored quietly so the legacy
	// devices above are still surfaced.
	Dictionary data;
	if (parse_devicectl_object(p_devicectl_json, data)) {
		const Dictionary outcome = data.get("result", Dictionary());
		const Array devicectl_devices = outcome.get("devices", Array());
		for (int i = 0; i < devicectl_devices.size(); i++) {
			devices.push_back(devicectl_devices[i]);
		}
	}

	return devices;
}

bool IOSRunTargetPlatform::parse_provisioning_profile_team(const String &p_decoded_plist, SigningTeam &r_team) {
	if (p_decoded_plist.strip_edges().is_empty()) {
		return false;
	}

	PList plist;
	String parse_error;
	if (!plist.load_string(p_decoded_plist, parse_error)) {
		return false;
	}

	Ref<PListNode> root = plist.get_root();
	if (root.is_null() || root->get_type() != PList::PL_NODE_TYPE_DICT) {
		return false;
	}
	const HashMap<String, Ref<PListNode>> &fields = root->data_dict;

	// The team id is the first entry of the `TeamIdentifier` array.
	String team_id;
	HashMap<String, Ref<PListNode>>::ConstIterator identifiers = fields.find("TeamIdentifier");
	if (identifiers != fields.end() && identifiers->value.is_valid() && identifiers->value->get_type() == PList::PL_NODE_TYPE_ARRAY) {
		const Vector<Ref<PListNode>> &entries = identifiers->value->data_array;
		if (!entries.is_empty() && entries[0].is_valid() && entries[0]->get_type() == PList::PL_NODE_TYPE_STRING) {
			team_id = String(entries[0]->get_value()).strip_edges();
		}
	}
	if (team_id.is_empty()) {
		return false;
	}

	String team_name;
	HashMap<String, Ref<PListNode>>::ConstIterator name = fields.find("TeamName");
	if (name != fields.end() && name->value.is_valid() && name->value->get_type() == PList::PL_NODE_TYPE_STRING) {
		team_name = String(name->value->get_value()).strip_edges();
	}

	r_team.id = team_id;
	r_team.name = team_name.is_empty() ? team_id : team_name;
	return true;
}

Vector<SigningTeam> IOSRunTargetPlatform::parse_signing_teams(const Vector<String> &p_decoded_plists) {
	Vector<SigningTeam> result;
	HashSet<String> seen;
	for (const String &payload : p_decoded_plists) {
		SigningTeam team;
		if (!parse_provisioning_profile_team(payload, team)) {
			continue;
		}
		if (seen.has(team.id)) {
			continue;
		}
		seen.insert(team.id);
		result.push_back(team);
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

	// Device state: combine the same two enumeration sources the export poll uses
	// so readiness sees every device the list and Run can reach. The raw `devicectl`
	// array already matches the probe's expected `devices` shape (identifier /
	// connectionProperties / deviceProperties); the legacy `ios_deploy` devices are
	// pulled from the export poll cache and folded in so a pre-Xcode-15 device is
	// not reported as "no device connected" while it is listed and runnable.
	{
		Vector<IOSDeployDevice> ios_deploy_devices;
		const EditorExportPlatformAppleEmbedded *export_platform = _find_export_platform();
		if (export_platform != nullptr) {
			for (const EditorExportPlatformAppleEmbedded::RunnableDeviceInfo &info : export_platform->get_runnable_devices()) {
				if (!info.use_ios_deploy) {
					continue;
				}
				IOSDeployDevice ios_deploy_device;
				ios_deploy_device.id = info.id;
				ios_deploy_device.name = info.name;
				ios_deploy_devices.push_back(ios_deploy_device);
			}
		}
		snapshot["devices"] = build_probe_devices(_query_devicectl(), ios_deploy_devices);
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

Vector<String> IOSRunTargetPlatform::_list_provisioning_profile_paths() const {
	Vector<String> paths;

	const String home = OS::get_singleton()->get_environment("HOME");
	if (home.is_empty()) {
		return paths;
	}
	const String directory = home.path_join("Library/MobileDevice/Provisioning Profiles");

	Ref<DirAccess> dir = DirAccess::open(directory);
	if (dir.is_null()) {
		return paths;
	}

	dir->list_dir_begin();
	for (String file = dir->get_next(); !file.is_empty(); file = dir->get_next()) {
		if (dir->current_is_dir()) {
			continue;
		}
		const String extension = file.get_extension().to_lower();
		if (extension == "mobileprovision" || extension == "provisionprofile") {
			paths.push_back(directory.path_join(file));
		}
	}
	dir->list_dir_end();

	return paths;
}

String IOSRunTargetPlatform::extract_plist_from_profile(const Vector<uint8_t> &p_bytes) {
	if (p_bytes.is_empty()) {
		return String();
	}

	// A `.mobileprovision`/`.provisionprofile` is a CMS (PKCS#7) wrapper whose
	// signed content is the profile's plist stored as cleartext. Slice that plist
	// out of the raw bytes rather than running `security cms -D`, so the filename
	// never reaches a shell.
	const uint8_t *data = p_bytes.ptr();
	const int size = p_bytes.size();

	const auto find_bytes = [data, size](const char *p_needle, int p_from) -> int {
		const int needle_length = strlen(p_needle);
		if (needle_length == 0 || size < needle_length) {
			return -1;
		}
		for (int i = MAX(p_from, 0); i + needle_length <= size; i++) {
			if (memcmp(data + i, p_needle, needle_length) == 0) {
				return i;
			}
		}
		return -1;
	};

	int start = find_bytes("<?xml", 0);
	if (start < 0) {
		start = find_bytes("<plist", 0);
	}
	static const char *CLOSE_TAG = "</plist>";
	const int close = find_bytes(CLOSE_TAG, MAX(start, 0));
	if (start < 0 || close < 0) {
		return String();
	}
	const int end = close + (int)strlen(CLOSE_TAG);

	// The sliced range is XML text (ASCII, including any base64 `<data>` blobs),
	// so it is valid UTF-8.
	String result;
	if (result.append_utf8((const char *)(data + start), end - start) != OK) {
		return String();
	}
	return result;
}

String IOSRunTargetPlatform::_read_provisioning_profile_plist(const String &p_path) {
	Error error = OK;
	const Vector<uint8_t> bytes = FileAccess::get_file_as_bytes(p_path, &error);
	if (error != OK) {
		return String();
	}
	return extract_plist_from_profile(bytes);
}

Vector<SigningTeam> IOSRunTargetPlatform::list_signing_teams() {
	// Read every installed provisioning profile and hand the batch to the pure
	// parser. Each profile embeds the (team id, team name) pair Xcode minted for
	// it, so the union across profiles is the set of teams the user can sign with.
	Vector<String> profiles;
	for (const String &path : _list_provisioning_profile_paths()) {
		const String plist = _read_provisioning_profile_plist(path);
		if (!plist.strip_edges().is_empty()) {
			profiles.push_back(plist);
		}
	}
	return parse_signing_teams(profiles);
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

	// Hand off to the existing export-to-`.xcarchive` + `devicectl` deploy path,
	// which already passes `-allowProvisioningUpdates` so automatic signing resolves
	// certificates, profiles, and device registration. The preset's
	// `application/app_store_team_id` is the source of truth for the signing team;
	// the run-target layer keeps readiness aligned with it rather than mutating it.
	return platform->run_on_device(preset, device_id, p_debug_flags);
}
