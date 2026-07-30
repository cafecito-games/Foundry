/**************************************************************************/
/*  test_run_target.h                                                     */
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

#ifdef TOOLS_ENABLED

#include "editor/run/run_target.h"
#include "editor/run/run_target_manager.h"
#include "editor/run/run_target_platform.h"

#include "editor/export/editor_export_platform.h"
#include "editor/export/editor_export_preset.h"
#include "editor/settings/editor_settings.h"

#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestRunTarget {

// A test double that proves the `RunTargetPlatform` abstraction is usable
// without any real iOS/Xcode tooling. It returns canned devices and a fixed
// readiness ladder, and records the last `run()` invocation so callers can
// assert dispatch without performing a deploy.
class FakeRunTargetPlatform : public RunTargetPlatform {
public:
	bool run_called = false;
	RunTarget last_run_target;
	int last_debug_flags = 0;
	Error run_result = OK;

	virtual Vector<ReadinessStep> probe_readiness(const RunTarget &p_target) override {
		Vector<ReadinessStep> steps;

		ReadinessStep xcode;
		xcode.id = "xcode";
		xcode.title = "Xcode toolchain";
		xcode.detail = "Xcode command-line tools are installed.";
		xcode.status = ReadinessStep::Status::OK;
		steps.push_back(xcode);

		ReadinessStep developer_mode;
		developer_mode.id = "developer_mode";
		developer_mode.title = "Developer Mode";
		developer_mode.detail = "Developer Mode is disabled on the device.";
		developer_mode.status = ReadinessStep::Status::ACTION_NEEDED;
		developer_mode.fix_hint = "Settings > Privacy & Security > Developer Mode > On, then reboot.";
		steps.push_back(developer_mode);

		return steps;
	}

	virtual Vector<RunTargetDevice> list_devices() override {
		Vector<RunTargetDevice> devices;

		RunTargetDevice phone;
		phone.id = "00008110-000000000000000E";
		phone.name = "My iPhone";
		phone.badge = ReadinessStep::Status::OK;
		devices.push_back(phone);

		RunTargetDevice pad;
		pad.id = "00008120-000000000000000F";
		pad.name = "My iPad";
		pad.badge = ReadinessStep::Status::ACTION_NEEDED;
		devices.push_back(pad);

		return devices;
	}

	virtual Error run(const RunTarget &p_target, int p_debug_flags) override {
		run_called = true;
		last_run_target = p_target;
		last_debug_flags = p_debug_flags;
		return run_result;
	}
};

static RunTarget make_target(const String &p_name) {
	RunTarget target;
	target.name = p_name;
	target.platform = "ios";
	target.export_preset = "iOS";
	target.device_id = "auto";
	target.signing_mode = "automatic";
	target.team_id = "ABCDE12345";
	return target;
}

TEST_CASE("[Editor][RunTarget] Missing file loads as an empty list without error") {
	const String path = TestUtils::get_temp_path("run_targets_missing.cfg");
	// Make sure no stale file is present from a previous run.
	if (FileAccess::exists(path)) {
		DirAccess::remove_absolute(path);
	}

	Error error = FAILED;
	const Vector<RunTarget> targets = RunTarget::load_all(path, &error);

	CHECK(targets.is_empty());
	CHECK_EQ(error, OK);
}

TEST_CASE("[Editor][RunTarget] Empty target list round-trips to an empty list") {
	const String path = TestUtils::get_temp_path("run_targets_empty.cfg");

	const Error save_error = RunTarget::save_all(path, Vector<RunTarget>());
	CHECK_EQ(save_error, OK);

	Error load_error = FAILED;
	const Vector<RunTarget> targets = RunTarget::load_all(path, &load_error);
	CHECK(targets.is_empty());
	CHECK_EQ(load_error, OK);
}

TEST_CASE("[Editor][RunTarget] Two targets round-trip with no data loss") {
	const String path = TestUtils::get_temp_path("run_targets_two.cfg");

	Vector<RunTarget> originals;
	RunTarget first = make_target("My iPhone");
	first.device_id = "00008110-000000000000000E";
	RunTarget second = make_target("My iPad");
	second.export_preset = "iPad";
	second.team_id = "ZZZZZ99999";
	originals.push_back(first);
	originals.push_back(second);

	CHECK_EQ(RunTarget::save_all(path, originals), OK);

	const Vector<RunTarget> loaded = RunTarget::load_all(path);
	REQUIRE_EQ(loaded.size(), 2);

	CHECK_EQ(loaded[0], first);
	CHECK_EQ(loaded[1], second);

	// Spot-check individual fields so a broken equality operator can't mask a regression.
	CHECK_EQ(loaded[0].name, "My iPhone");
	CHECK_EQ(loaded[0].platform, "ios");
	CHECK_EQ(loaded[0].export_preset, "iOS");
	CHECK_EQ(loaded[0].device_id, "00008110-000000000000000E");
	CHECK_EQ(loaded[0].signing_mode, "automatic");
	CHECK_EQ(loaded[0].team_id, "ABCDE12345");
	CHECK_EQ(loaded[1].export_preset, "iPad");
	CHECK_EQ(loaded[1].team_id, "ZZZZZ99999");
}

TEST_CASE("[Editor][RunTarget] Unknown keys are preserved across a save") {
	const String path = TestUtils::get_temp_path("run_targets_unknown.cfg");

	// Author a config the way a newer editor (with extra fields) would.
	{
		Ref<ConfigFile> config;
		config.instantiate();
		config->set_value("target.0", "name", "Future iPhone");
		config->set_value("target.0", "platform", "ios");
		config->set_value("target.0", "export_preset", "iOS");
		config->set_value("target.0", "device_id", "auto");
		config->set_value("target.0", "signing_mode", "automatic");
		config->set_value("target.0", "team_id", "ABCDE12345");
		config->set_value("target.0", "future_string", "keep me");
		config->set_value("target.0", "future_number", 42);
		CHECK_EQ(config->save(path), OK);
	}

	const Vector<RunTarget> loaded = RunTarget::load_all(path);
	REQUIRE_EQ(loaded.size(), 1);
	CHECK_EQ(loaded[0].extra_keys.size(), 2);
	CHECK_EQ(String(loaded[0].extra_keys["future_string"]), "keep me");
	CHECK_EQ(int(loaded[0].extra_keys["future_number"]), 42);

	// Re-saving must not drop the unknown keys.
	CHECK_EQ(RunTarget::save_all(path, loaded), OK);

	Ref<ConfigFile> reread;
	reread.instantiate();
	REQUIRE_EQ(reread->load(path), OK);
	CHECK(reread->has_section_key("target.0", "future_string"));
	CHECK(reread->has_section_key("target.0", "future_number"));
	CHECK_EQ(String(reread->get_value("target.0", "future_string")), "keep me");
	CHECK_EQ(int(reread->get_value("target.0", "future_number")), 42);

	// And a full reload still equals what we loaded the first time.
	const Vector<RunTarget> reloaded = RunTarget::load_all(path);
	REQUIRE_EQ(reloaded.size(), 1);
	CHECK_EQ(reloaded[0], loaded[0]);
}

TEST_CASE("[Editor][RunTarget] Fake adapter enumerates canned devices") {
	FakeRunTargetPlatform platform;
	RunTargetPlatform &adapter = platform; // Drive it through the abstract interface.

	const Vector<RunTargetDevice> devices = adapter.list_devices();
	REQUIRE_EQ(devices.size(), 2);

	CHECK_EQ(devices[0].id, "00008110-000000000000000E");
	CHECK_EQ(devices[0].name, "My iPhone");
	CHECK_EQ(devices[0].badge, ReadinessStep::Status::OK);

	CHECK_EQ(devices[1].name, "My iPad");
	CHECK_EQ(devices[1].badge, ReadinessStep::Status::ACTION_NEEDED);
}

TEST_CASE("[Editor][RunTarget] Fake adapter probes an ordered readiness ladder") {
	FakeRunTargetPlatform platform;
	RunTargetPlatform &adapter = platform;

	const Vector<ReadinessStep> steps = adapter.probe_readiness(make_target("My iPhone"));
	REQUIRE_EQ(steps.size(), 2);

	// Earlier satisfied step is OK; the first non-OK step carries the fix hint.
	CHECK_EQ(steps[0].id, StringName("xcode"));
	CHECK_EQ(steps[0].status, ReadinessStep::Status::OK);
	CHECK(steps[0].fix_hint.is_empty());

	CHECK_EQ(steps[1].id, StringName("developer_mode"));
	CHECK_EQ(steps[1].status, ReadinessStep::Status::ACTION_NEEDED);
	CHECK_FALSE(steps[1].fix_hint.is_empty());
}

TEST_CASE("[Editor][RunTarget] Fake adapter records run dispatch through the interface") {
	FakeRunTargetPlatform platform;
	RunTargetPlatform &adapter = platform;

	const RunTarget target = make_target("My iPhone");
	const Error result = adapter.run(target, EditorExportPlatform::DEBUG_FLAG_REMOTE_DEBUG);

	CHECK_EQ(result, OK);
	CHECK(platform.run_called);
	CHECK_EQ(platform.last_debug_flags, EditorExportPlatform::DEBUG_FLAG_REMOTE_DEBUG);
	CHECK_EQ(platform.last_run_target, target);
}

TEST_CASE("[Editor][RunTarget] ReadinessStep equality compares every field") {
	ReadinessStep base;
	base.id = "team";
	base.title = "Apple ID / team";
	base.detail = "No signing team selected.";
	base.status = ReadinessStep::Status::ACTION_NEEDED;
	base.fix_hint = "Sign in with your Apple ID in Xcode, then pick your team here.";

	ReadinessStep same = base;
	CHECK_EQ(base, same);

	ReadinessStep different_status = base;
	different_status.status = ReadinessStep::Status::BLOCKED;
	CHECK_NE(base, different_status);

	ReadinessStep different_hint = base;
	different_hint.fix_hint = "Something else.";
	CHECK_NE(base, different_hint);
}

// A preset lookup that resolves names from an in-memory table, standing in for
// the editor's `EditorExport` singleton (absent in headless tests).
class FakePresetProvider : public RunTargetManager::PresetProvider {
public:
	HashMap<String, Ref<EditorExportPreset>> presets;

	// Note: `EditorExportPreset::set_name` writes through `EditorExport::singleton`,
	// which is absent in headless tests, so the fake keys presets by name in its
	// own table instead of setting the preset's internal name.
	void add(const String &p_name) {
		Ref<EditorExportPreset> preset;
		preset.instantiate();
		presets[p_name] = preset;
	}

	virtual Ref<EditorExportPreset> find_preset_by_name(const String &p_name) const override {
		HashMap<String, Ref<EditorExportPreset>>::ConstIterator found = presets.find(p_name);
		if (found == presets.end()) {
			return Ref<EditorExportPreset>();
		}
		return found->value;
	}
};

TEST_CASE("[Editor][RunTarget] Manager dispatches to the adapter registered for a platform") {
	FakeRunTargetPlatform ios;
	FakeRunTargetPlatform android;

	RunTargetManager manager;
	manager.register_platform("ios", &ios);
	manager.register_platform("android", &android);

	CHECK_EQ(manager.get_platform("ios"), &ios);
	CHECK_EQ(manager.get_platform("android"), &android);
	CHECK_EQ(manager.get_platform("web"), nullptr);

	manager.unregister_platform("android");
	CHECK_EQ(manager.get_platform("android"), nullptr);
}

TEST_CASE("[Editor][RunTarget] Manager resolves a target to its preset, device, and debug flags") {
	FakeRunTargetPlatform ios;
	FakePresetProvider provider;
	provider.add("iOS");

	RunTargetManager manager;
	manager.register_platform("ios", &ios);
	manager.set_preset_provider(&provider);

	RunTarget target = make_target("My iPhone");
	target.device_id = "00008110-000000000000000E";

	// `[Editor]` tests create EditorSettings, and `compute_debug_flags` reads the
	// shared "Deploy Remote Debug" project setting. Pin it so the assertion below
	// does not depend on a stray on-disk project_metadata.cfg, and restore it.
	EditorSettings *settings = EditorSettings::get_singleton();
	REQUIRE(settings != nullptr);
	const Variant previous_remote_debug = settings->get_project_metadata("debug_options", "run_deploy_remote_debug", true);
	settings->set_project_metadata("debug_options", "run_deploy_remote_debug", true);

	RunTargetManager::ResolvedTarget resolved;
	const Error error = manager.resolve(target, resolved);

	CHECK_EQ(error, OK);
	REQUIRE(resolved.preset.is_valid());
	// The manager returns the exact preset the provider linked by name.
	CHECK_EQ(resolved.preset, provider.presets["iOS"]);
	CHECK_EQ(resolved.device_id, "00008110-000000000000000E");
	CHECK_EQ(resolved.platform_adapter, &ios);
	// A run-target deploy wires the running app back to the editor debugger when
	// the user has not opted out.
	CHECK((resolved.debug_flags & EditorExportPlatform::DEBUG_FLAG_REMOTE_DEBUG) != 0);

	// Opting out of remote debug clears the flag, matching the native deploy path.
	settings->set_project_metadata("debug_options", "run_deploy_remote_debug", false);
	RunTargetManager::ResolvedTarget opted_out;
	CHECK_EQ(manager.resolve(target, opted_out), OK);
	CHECK((opted_out.debug_flags & EditorExportPlatform::DEBUG_FLAG_REMOTE_DEBUG) == 0);

	settings->set_project_metadata("debug_options", "run_deploy_remote_debug", previous_remote_debug);
}

TEST_CASE("[Editor][RunTarget] Resolving a target whose export preset is gone fails gracefully") {
	FakeRunTargetPlatform ios;
	FakePresetProvider provider; // No presets registered: the linked one is "deleted".

	RunTargetManager manager;
	manager.register_platform("ios", &ios);
	manager.set_preset_provider(&provider);

	RunTarget target = make_target("My iPhone");

	RunTargetManager::ResolvedTarget resolved;
	ERR_PRINT_OFF;
	const Error error = manager.resolve(target, resolved);
	ERR_PRINT_ON;

	CHECK_EQ(error, ERR_DOES_NOT_EXIST);
	CHECK(resolved.preset.is_null());
}

TEST_CASE("[Editor][RunTarget] Resolving a target with an unregistered platform fails gracefully") {
	FakePresetProvider provider;
	provider.add("iOS");

	RunTargetManager manager;
	// No adapter registered for "ios".
	manager.set_preset_provider(&provider);

	RunTarget target = make_target("My iPhone");

	RunTargetManager::ResolvedTarget resolved;
	ERR_PRINT_OFF;
	const Error error = manager.resolve(target, resolved);
	ERR_PRINT_ON;

	CHECK_EQ(error, ERR_UNAVAILABLE);
	CHECK_EQ(resolved.platform_adapter, nullptr);
}

TEST_CASE("[Editor][RunTarget] Active-target selection rejects unknown names") {
	const String path = TestUtils::get_temp_path("run_targets_active_reject.cfg");

	Vector<RunTarget> initial;
	initial.push_back(make_target("My iPhone"));
	CHECK_EQ(RunTarget::save_all(path, initial), OK);

	RunTargetManager manager;
	CHECK_EQ(manager.load(path), OK);

	CHECK_FALSE(manager.has_active_target());
	CHECK_FALSE(manager.set_active_target("Nonexistent"));
	CHECK_FALSE(manager.has_active_target());

	CHECK(manager.set_active_target("My iPhone"));
	CHECK(manager.has_active_target());
	CHECK_EQ(manager.get_active_target().name, "My iPhone");

	// An empty name clears the selection.
	CHECK(manager.set_active_target(String()));
	CHECK_FALSE(manager.has_active_target());
}

TEST_CASE("[Editor][RunTarget] Active-target selection persists across a reload") {
	const String path = TestUtils::get_temp_path("run_targets_active_persist.cfg");

	Vector<RunTarget> initial;
	initial.push_back(make_target("My iPhone"));
	RunTarget second = make_target("My iPad");
	second.export_preset = "iPad";
	initial.push_back(second);
	CHECK_EQ(RunTarget::save_all(path, initial), OK);

	{
		RunTargetManager manager;
		REQUIRE_EQ(manager.load(path), OK);
		REQUIRE(manager.set_active_target("My iPad"));
		CHECK_EQ(manager.save(), OK);
	}

	RunTargetManager reloaded;
	REQUIRE_EQ(reloaded.load(path), OK);
	CHECK(reloaded.has_active_target());
	CHECK_EQ(reloaded.get_active_target_name(), "My iPad");
	CHECK_EQ(reloaded.get_active_target().export_preset, "iPad");

	// The targets themselves survive the manager's save path unchanged.
	REQUIRE_EQ(reloaded.get_targets().size(), 2);
	CHECK_EQ(reloaded.get_targets()[0].name, "My iPhone");
	CHECK_EQ(reloaded.get_targets()[1].name, "My iPad");
}

TEST_CASE("[Editor][RunTarget] A dangling active selection is dropped on load") {
	const String path = TestUtils::get_temp_path("run_targets_active_dangling.cfg");

	// Author a config whose persisted active target no longer exists.
	{
		Ref<ConfigFile> config;
		config.instantiate();
		config->set_value("target.0", "name", "My iPhone");
		config->set_value("target.0", "platform", "ios");
		config->set_value("meta", "active_target", "Deleted Target");
		CHECK_EQ(config->save(path), OK);
	}

	RunTargetManager manager;
	REQUIRE_EQ(manager.load(path), OK);
	CHECK_FALSE(manager.has_active_target());
	CHECK(manager.get_active_target_name().is_empty());
}

TEST_CASE("[Editor][RunTarget] A failed load leaves prior state untouched") {
	const String good_path = TestUtils::get_temp_path("run_targets_good.cfg");
	const String bad_path = TestUtils::get_temp_path("run_targets_malformed.cfg");

	Vector<RunTarget> initial;
	initial.push_back(make_target("My iPhone"));
	CHECK_EQ(RunTarget::save_all(good_path, initial), OK);

	// Author a file ConfigFile cannot parse so load() reports a hard error.
	{
		Ref<FileAccess> file = FileAccess::open(bad_path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string("this is not a valid config file = = =\n[unterminated");
		file->close();
	}

	RunTargetManager manager;
	REQUIRE_EQ(manager.load(good_path), OK);
	REQUIRE(manager.set_active_target("My iPhone"));

	// Loading a malformed file must fail without mutating the good state.
	ERR_PRINT_OFF;
	const Error error = manager.load(bad_path);
	ERR_PRINT_ON;
	CHECK_NE(error, OK);

	CHECK_EQ(manager.get_targets().size(), 1);
	CHECK(manager.has_active_target());
	CHECK_EQ(manager.get_active_target_name(), "My iPhone");

	// A subsequent save must still target the good path and preserve its targets.
	CHECK_EQ(manager.save(), OK);
	const Vector<RunTarget> reloaded = RunTarget::load_all(good_path);
	REQUIRE_EQ(reloaded.size(), 1);
	CHECK_EQ(reloaded[0].name, "My iPhone");
}

TEST_CASE("[Editor][RunTarget] Manager exposes failed load state") {
	const String path = TestUtils::get_temp_path("run_targets_load_state_bad.cfg");
	{
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string("[target.0\nname=bad\n");
	}

	RunTargetManager manager;
	ERR_PRINT_OFF;
	const Error load_error = manager.load(path);
	ERR_PRINT_ON;

	CHECK_NE(load_error, OK);
	CHECK_EQ(manager.get_last_load_error(), load_error);
	CHECK_FALSE(manager.has_loaded_config_path());
}

TEST_CASE("[Editor][RunTarget] Manager reports loaded config path after successful load") {
	const String path = TestUtils::get_temp_path("run_targets_load_state_good.cfg");

	Vector<RunTarget> initial;
	initial.push_back(make_target("My iPhone"));
	REQUIRE_EQ(RunTarget::save_all(path, initial), OK);

	RunTargetManager manager;
	CHECK_EQ(manager.load(path), OK);
	CHECK_EQ(manager.get_last_load_error(), OK);
	CHECK(manager.has_loaded_config_path());
}

TEST_CASE("[Editor][RunTarget] First-open configuration marker round-trips and is consumed once") {
	const String path = TestUtils::get_temp_path("run_targets_first_open.cfg");
	if (FileAccess::exists(path)) {
		DirAccess::remove_absolute(path);
	}

	Vector<RunTarget> initial;
	initial.push_back(make_target("My iPhone"));
	REQUIRE_EQ(RunTarget::save_all(path, initial), OK);
	REQUIRE_EQ(RunTargetManager::request_show_configuration_on_first_open(path), OK);

	const Vector<RunTarget> after_request = RunTarget::load_all(path);
	REQUIRE_EQ(after_request.size(), 1);
	CHECK_EQ(after_request[0], initial[0]);

	CHECK(RunTargetManager::consume_show_configuration_on_first_open(path));
	CHECK_FALSE(RunTargetManager::consume_show_configuration_on_first_open(path));

	const Vector<RunTarget> after_consume = RunTarget::load_all(path);
	REQUIRE_EQ(after_consume.size(), 1);
	CHECK_EQ(after_consume[0], initial[0]);
}

TEST_CASE("[Editor][RunTarget] First-open configuration consumes the legacy dock key") {
	const String path = TestUtils::get_temp_path("run_targets_first_open_legacy_key.cfg");
	if (FileAccess::exists(path)) {
		DirAccess::remove_absolute(path);
	}

	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("meta", "show_dock_on_first_open", true);
	REQUIRE_EQ(config->save(path), OK);

	CHECK(RunTargetManager::consume_show_configuration_on_first_open(path));
	CHECK_FALSE(RunTargetManager::consume_show_configuration_on_first_open(path));
}

TEST_CASE("[Editor][RunTarget] Consuming a first-open marker preserves the active selection") {
	const String path = TestUtils::get_temp_path("run_targets_first_open_active.cfg");
	if (FileAccess::exists(path)) {
		DirAccess::remove_absolute(path);
	}

	Vector<RunTarget> initial;
	initial.push_back(make_target("My iPhone"));
	{
		RunTargetManager manager;
		REQUIRE_EQ(RunTarget::save_all(path, initial), OK);
		REQUIRE_EQ(manager.load(path), OK);
		REQUIRE(manager.set_active_target("My iPhone"));
		REQUIRE_EQ(manager.save(), OK);
	}
	REQUIRE_EQ(RunTargetManager::request_show_configuration_on_first_open(path), OK);

	CHECK(RunTargetManager::consume_show_configuration_on_first_open(path));

	// The active selection persisted alongside the marker survives consuming it.
	RunTargetManager reloaded;
	REQUIRE_EQ(reloaded.load(path), OK);
	CHECK(reloaded.has_active_target());
	CHECK_EQ(reloaded.get_active_target_name(), "My iPhone");
}

TEST_CASE("[Editor][RunTarget] Consuming a marker on a config without one returns false") {
	const String path = TestUtils::get_temp_path("run_targets_no_marker.cfg");
	if (FileAccess::exists(path)) {
		DirAccess::remove_absolute(path);
	}

	Vector<RunTarget> initial;
	initial.push_back(make_target("My iPhone"));
	REQUIRE_EQ(RunTarget::save_all(path, initial), OK);

	CHECK_FALSE(RunTargetManager::consume_show_configuration_on_first_open(path));
}

TEST_CASE("[Editor][RunTarget] Consuming a marker on a missing config returns false") {
	const String path = TestUtils::get_temp_path("run_targets_marker_missing.cfg");
	if (FileAccess::exists(path)) {
		DirAccess::remove_absolute(path);
	}

	CHECK_FALSE(RunTargetManager::consume_show_configuration_on_first_open(path));
}

} // namespace TestRunTarget

#endif // TOOLS_ENABLED
