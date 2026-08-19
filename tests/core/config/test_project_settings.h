/**************************************************************************/
/*  test_project_settings.h                                               */
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

#include "project_settings_test_helpers.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_uid.h"
#include "core/os/os.h"
#include "core/variant/variant.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestProjectSettings {

TEST_CASE("[ProjectSettings] Get existing setting") {
	CHECK(ProjectSettings::get_singleton()->has_setting("application/run/main_scene"));

	Variant variant = ProjectSettings::get_singleton()->get_setting("application/run/main_scene");
	CHECK_EQ(variant.get_type(), Variant::STRING);

	String name = variant;
	CHECK_EQ(name, String());
}

TEST_CASE("[ProjectSettings] Default value is ignored if setting exists") {
	CHECK(ProjectSettings::get_singleton()->has_setting("application/run/main_scene"));

	Variant variant = ProjectSettings::get_singleton()->get_setting("application/run/main_scene", "SomeDefaultValue");
	CHECK_EQ(variant.get_type(), Variant::STRING);

	String name = variant;
	CHECK_EQ(name, String());
}

TEST_CASE("[ProjectSettings] Non existing setting is null") {
	CHECK_FALSE(ProjectSettings::get_singleton()->has_setting("not_existing_setting"));

	Variant variant = ProjectSettings::get_singleton()->get_setting("not_existing_setting");
	CHECK_EQ(variant.get_type(), Variant::NIL);
}

TEST_CASE("[ProjectSettings] Non existing setting should return default value") {
	CHECK_FALSE(ProjectSettings::get_singleton()->has_setting("not_existing_setting"));

	Variant variant = ProjectSettings::get_singleton()->get_setting("not_existing_setting");
	CHECK_EQ(variant.get_type(), Variant::NIL);

	variant = ProjectSettings::get_singleton()->get_setting("not_existing_setting", "my_nice_default_value");
	CHECK_EQ(variant.get_type(), Variant::STRING);

	String name = variant;
	CHECK_EQ(name, "my_nice_default_value");

	CHECK_FALSE(ProjectSettings::get_singleton()->has_setting("not_existing_setting"));
}

TEST_CASE("[ProjectSettings] Set value should be returned when retrieved") {
	CHECK_FALSE(ProjectSettings::get_singleton()->has_setting("my_custom_setting"));

	Variant variant = ProjectSettings::get_singleton()->get_setting("my_custom_setting");
	CHECK_EQ(variant.get_type(), Variant::NIL);

	ProjectSettings::get_singleton()->set_setting("my_custom_setting", true);
	CHECK(ProjectSettings::get_singleton()->has_setting("my_custom_setting"));

	variant = ProjectSettings::get_singleton()->get_setting("my_custom_setting");
	CHECK_EQ(variant.get_type(), Variant::BOOL);

	bool value = variant;
	CHECK_EQ(true, value);

	CHECK(ProjectSettings::get_singleton()->has_setting("my_custom_setting"));
}

TEST_CASE("[ProjectSettings] localize_path") {
	String old_resource_path = TestProjectSettingsInternalsAccessor::resource_path();
	TestProjectSettingsInternalsAccessor::resource_path() = DirAccess::create(DirAccess::ACCESS_FILESYSTEM)->get_current_dir();
	String root_path = ProjectSettings::get_singleton()->get_resource_path();
#ifdef WINDOWS_ENABLED
	String root_path_win = ProjectSettings::get_singleton()->get_resource_path().replace_char('/', '\\');
#endif

	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("filename"), "res://filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("path/filename"), "res://path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("path/something/../filename"), "res://path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("path/./filename"), "res://path/filename");
#ifdef WINDOWS_ENABLED
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("path\\filename"), "res://path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("path\\something\\..\\filename"), "res://path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("path\\.\\filename"), "res://path/filename");
#endif

	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("../filename"), "../filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("../path/filename"), "../path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("..\\path\\filename"), "../path/filename");

	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("/testroot/filename"), "/testroot/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("/testroot/path/filename"), "/testroot/path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("/testroot/path/something/../filename"), "/testroot/path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("/testroot/path/./filename"), "/testroot/path/filename");
#ifdef WINDOWS_ENABLED
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("C:/testroot/filename"), "C:/testroot/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("C:/testroot/path/filename"), "C:/testroot/path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("C:/testroot/path/something/../filename"), "C:/testroot/path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("C:/testroot/path/./filename"), "C:/testroot/path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("C:\\testroot\\filename"), "C:/testroot/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("C:\\testroot\\path\\filename"), "C:/testroot/path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("C:\\testroot\\path\\something\\..\\filename"), "C:/testroot/path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("C:\\testroot\\path\\.\\filename"), "C:/testroot/path/filename");
#endif

	CHECK_EQ(ProjectSettings::get_singleton()->localize_path(root_path + "/filename"), "res://filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path(root_path + "/path/filename"), "res://path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path(root_path + "/path/something/../filename"), "res://path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path(root_path + "/path/./filename"), "res://path/filename");
#ifdef WINDOWS_ENABLED
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path(root_path_win + "\\filename"), "res://filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path(root_path_win + "\\path\\filename"), "res://path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path(root_path_win + "\\path\\something\\..\\filename"), "res://path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path(root_path_win + "\\path\\.\\filename"), "res://path/filename");
#endif

	TestProjectSettingsInternalsAccessor::resource_path() = old_resource_path;
}

TEST_CASE("[SceneTree][ProjectSettings] settings_changed signal") {
	SIGNAL_WATCH(ProjectSettings::get_singleton(), SNAME("settings_changed"));

	ProjectSettings::get_singleton()->set_setting("test_signal_setting", "test_value");
	MessageQueue::get_singleton()->flush();

	SIGNAL_CHECK("settings_changed", { {} });

	SIGNAL_UNWATCH(ProjectSettings::get_singleton(), SNAME("settings_changed"));
}

TEST_CASE("[ProjectSettings] get_changed_settings basic functionality") {
	String setting_name = "test_changed_setting";
	ProjectSettings::get_singleton()->set_setting(setting_name, "test_value");

	PackedStringArray changes = ProjectSettings::get_singleton()->get_changed_settings();
	CHECK(changes.has(setting_name));
}

TEST_CASE("[ProjectSettings] get_changed_settings multiple settings") {
	ProjectSettings::get_singleton()->set_setting("test_setting_1", "value1");
	ProjectSettings::get_singleton()->set_setting("test_setting_2", "value2");
	ProjectSettings::get_singleton()->set_setting("another_group/setting", "value3");

	PackedStringArray changes = ProjectSettings::get_singleton()->get_changed_settings();
	CHECK(changes.has("test_setting_1"));
	CHECK(changes.has("test_setting_2"));
	CHECK(changes.has("another_group/setting"));
}

TEST_CASE("[ProjectSettings] check_changed_settings_in_group") {
	ProjectSettings::get_singleton()->set_setting("group1/setting1", "value1");
	ProjectSettings::get_singleton()->set_setting("group1/setting2", "value2");
	ProjectSettings::get_singleton()->set_setting("group2/setting1", "value3");
	ProjectSettings::get_singleton()->set_setting("other_setting", "value4");

	CHECK(ProjectSettings::get_singleton()->check_changed_settings_in_group("group1/"));
	CHECK(ProjectSettings::get_singleton()->check_changed_settings_in_group("group2/"));
	CHECK_FALSE(ProjectSettings::get_singleton()->check_changed_settings_in_group("nonexistent/"));

	CHECK(ProjectSettings::get_singleton()->check_changed_settings_in_group("group1"));
	CHECK(ProjectSettings::get_singleton()->check_changed_settings_in_group("other_setting"));
}

TEST_CASE("[SceneTree][ProjectSettings] Changes cleared after settings_changed signal") {
	SIGNAL_WATCH(ProjectSettings::get_singleton(), SNAME("settings_changed"));

	ProjectSettings::get_singleton()->set_setting("signal_clear_test", "value");

	PackedStringArray changes_before = ProjectSettings::get_singleton()->get_changed_settings();
	CHECK(changes_before.has("signal_clear_test"));

	MessageQueue::get_singleton()->flush();

	SIGNAL_CHECK("settings_changed", { {} });

	PackedStringArray changes_after = ProjectSettings::get_singleton()->get_changed_settings();
	CHECK_FALSE(changes_after.has("signal_clear_test"));

	SIGNAL_UNWATCH(ProjectSettings::get_singleton(), SNAME("settings_changed"));
}

TEST_CASE("[ProjectSettings] No tracking when setting same value") {
	String setting_name = "same_value_test";
	String test_value = "same_value";

	ProjectSettings::get_singleton()->set_setting(setting_name, test_value);
	int count_before = ProjectSettings::get_singleton()->get_changed_settings().size();

	// Setting the same value should not be tracked due to early return.
	ProjectSettings::get_singleton()->set_setting(setting_name, test_value);
	int count_after = ProjectSettings::get_singleton()->get_changed_settings().size();

	CHECK_EQ(count_before, count_after);
}

TEST_CASE("[ProjectSettings][Autoload] UID-only values resolve after a cold cache is populated") {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	const StringName setting = "autoload/ProjectSettingsColdAutoload";
	const ResourceUID::ID uid = ResourceUID::get_singleton()->create_id();
	CHECK_NE(uid, ResourceUID::INVALID_ID);
	if (uid == ResourceUID::INVALID_ID) {
		return;
	}
	const String uid_text = ResourceUID::get_singleton()->id_to_text(uid);

	settings->set_setting(setting, "*" + uid_text);
	CHECK_EQ(settings->get_autoload(StringName("ProjectSettingsColdAutoload")).name, StringName("ProjectSettingsColdAutoload"));

	ResourceUID::get_singleton()->add_id(uid, "res://cold_autoload.fs");
	CHECK_EQ(settings->get_autoload(StringName("ProjectSettingsColdAutoload")).path, "res://cold_autoload.fs");

	settings->set_setting(setting, Variant());
	ResourceUID::get_singleton()->remove_id(uid);
}

TEST_CASE("[ProjectSettings][Autoload] path-plus-UID values use a path fallback until the UID resolves") {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	const StringName setting = "autoload/ProjectSettingsFallbackAutoload";
	const ResourceUID::ID uid = ResourceUID::get_singleton()->create_id();
	CHECK_NE(uid, ResourceUID::INVALID_ID);
	if (uid == ResourceUID::INVALID_ID) {
		return;
	}
	const String uid_text = ResourceUID::get_singleton()->id_to_text(uid);

	settings->set_setting(setting, "*res://fallback_autoload.fs::" + uid_text);
	CHECK_EQ(settings->get_autoload(StringName("ProjectSettingsFallbackAutoload")).path, "res://fallback_autoload.fs");

	ResourceUID::get_singleton()->add_id(uid, "res://resolved_autoload.fs");
	CHECK_EQ(settings->get_autoload(StringName("ProjectSettingsFallbackAutoload")).path, "res://resolved_autoload.fs");

	settings->set_setting(setting, Variant());
	ResourceUID::get_singleton()->remove_id(uid);
}

// Saving and reloading project settings mutates global state: `save_custom()` refreshes the stored
// feature list, and the loaders install every parsed key and refresh the last-save timestamp. This
// guard removes the keys the round-trip introduces and restores the rest on every exit path,
// including a partial parse that only installed some of them.
class TestProjectSettingsRoundTripScope {
	Vector<String> introduced_keys;
	Variant saved_features;
	uint64_t saved_last_save_time = 0;

public:
	explicit TestProjectSettingsRoundTripScope(const Vector<String> &p_introduced_keys) :
			introduced_keys(p_introduced_keys) {
		saved_features = ProjectSettings::get_singleton()->get_setting("application/config/features");
		saved_last_save_time = TestProjectSettingsInternalsAccessor::last_save_time();
	}

	~TestProjectSettingsRoundTripScope() {
		for (const String &key : introduced_keys) {
			ProjectSettings::get_singleton()->set_setting(key, Variant());
		}
		ProjectSettings::get_singleton()->set_setting("application/config/features", saved_features);
		TestProjectSettingsInternalsAccessor::last_save_time() = saved_last_save_time;
	}
};

// Writes unsigned and signed settings to a project file in the requested format, reloads them
// through the loader the engine uses at startup, and checks carrier and exact value.
static void check_project_settings_round_trip(const String &p_file_name, bool p_binary) {
	ProjectSettings *settings = ProjectSettings::get_singleton();

	struct ExpectedUnsigned {
		const char *key;
		uint64_t value;
	};
	const ExpectedUnsigned expectations[] = {
		{ "fixed_width_integers/unsigned_zero", 0 },
		{ "fixed_width_integers/unsigned_above_signed_max", uint64_t(INT64_MAX) + 1 },
		{ "fixed_width_integers/unsigned_maximum", UINT64_MAX },
	};
	const char *signed_key = "fixed_width_integers/signed_minimum";

	ProjectSettings::CustomMap custom;
	Vector<String> introduced_keys;
	for (const ExpectedUnsigned &expectation : expectations) {
		custom[expectation.key] = Variant(expectation.value);
		introduced_keys.push_back(expectation.key);
	}
	custom[signed_key] = Variant(int64_t(INT64_MIN));
	introduced_keys.push_back(signed_key);

	const TestProjectSettingsRoundTripScope restore_scope(introduced_keys);

	const String save_path = TestUtils::get_temp_path(p_file_name);
	const Error save_error = settings->save_custom(save_path, custom, Vector<String>(), false);
	CHECK_EQ(save_error, OK);
	if (save_error != OK) {
		return;
	}

	const Error load_error = p_binary
			? TestProjectSettingsInternalsAccessor::load_settings_binary(save_path)
			: TestProjectSettingsInternalsAccessor::load_settings_text(save_path);
	CHECK_EQ(load_error, OK);
	if (load_error != OK) {
		return;
	}

	for (const ExpectedUnsigned &expectation : expectations) {
		const Variant loaded = settings->get_setting(expectation.key);
		CHECK_MESSAGE(loaded.get_type() == Variant::UINT, expectation.key);
		CHECK_MESSAGE(loaded.operator uint64_t() == expectation.value, expectation.key);
	}

	const Variant loaded_signed = settings->get_setting(signed_key);
	CHECK_EQ(loaded_signed.get_type(), Variant::INT);
	CHECK_EQ(loaded_signed.operator int64_t(), INT64_MIN);
}

TEST_CASE("[ProjectSettings][UInt] Text project settings round-trip unsigned values") {
	check_project_settings_round_trip("uint_project_settings.foundry", false);
}

TEST_CASE("[ProjectSettings][UInt] Binary project settings round-trip unsigned values") {
	check_project_settings_round_trip("uint_project_settings.binary", true);
}

TEST_CASE("[ProjectSettings] Restore scope leaves user:// writable after a project rename") {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	const String real_app_name = GLOBAL_GET("application/config/name");
	const Variant real_use_custom_user_dir = GLOBAL_GET("application/config/use_custom_user_dir");

	// Retarget to a mapping whose app-userdata leaf does not exist yet, then take the scope:
	// its restore target is that never-created leaf, so the destructor's leaf re-creation is
	// the only thing that can make the mapping writable again (`FileAccess::open(..., WRITE)`
	// does not create parents). `use_custom_user_dir` is pinned so the leaf is deterministic.
	settings->set_setting("application/config/use_custom_user_dir", false);
	settings->set_setting("application/config/name", "restore_scope_missing_leaf_probe");
	CHECK_MESSAGE(!DirAccess::exists(OS::get_singleton()->get_user_data_dir()),
			"The restore target's app-userdata leaf should not exist yet: ",
			OS::get_singleton()->get_user_data_dir());
	{
		TestProjectSettingsRestoreScope restore_scope;
		// A rename whose leaf is never created either.
		settings->set_setting("application/config/name", "restore_scope_rename_probe");
		CHECK_MESSAGE(!DirAccess::exists(OS::get_singleton()->get_user_data_dir()),
				"The renamed mapping's app-userdata leaf should not exist while the scope is live: ",
				OS::get_singleton()->get_user_data_dir());
	}

	// The scope restored the never-created mapping *and* re-created its leaf.
	CHECK_MESSAGE(DirAccess::exists(OS::get_singleton()->get_user_data_dir()),
			"The restored mapping's leaf should exist again after the scope dies: ",
			OS::get_singleton()->get_user_data_dir());
	{
		Ref<FileAccess> probe = FileAccess::open("user://restore_scope_writable_probe.txt", FileAccess::WRITE);
		CHECK_MESSAGE(probe.is_valid(), "user:// should be writable again after the scope dies");
	}
	Ref<DirAccess> user_dir = DirAccess::open("user://");
	REQUIRE(user_dir.is_valid());
	CHECK_EQ(user_dir->remove("restore_scope_writable_probe.txt"), OK);

	// Leave the case-entry mapping (and its leaf) as this case found them.
	settings->set_setting("application/config/name", real_app_name);
	settings->set_setting("application/config/use_custom_user_dir", real_use_custom_user_dir);
	OS::get_singleton()->ensure_user_data_dir();
}

TEST_CASE("[ProjectSettings] Restore scope restores custom user dir settings") {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	const bool saved_use_custom = settings->get_setting("application/config/use_custom_user_dir");
	const String saved_custom_dir = settings->get_setting("application/config/custom_user_dir_name");
	const String saved_user_data_dir = OS::get_singleton()->get_user_data_dir();
	{
		TestProjectSettingsRestoreScope restore_scope;
		settings->set_setting("application/config/use_custom_user_dir", true);
		settings->set_setting("application/config/custom_user_dir_name", "restore_scope_custom_probe");
	}
	CHECK_EQ(bool(settings->get_setting("application/config/use_custom_user_dir")), saved_use_custom);
	CHECK_EQ(String(settings->get_setting("application/config/custom_user_dir_name")), saved_custom_dir);
	CHECK_EQ(OS::get_singleton()->get_user_data_dir(), saved_user_data_dir);
}

} // namespace TestProjectSettings
