/**************************************************************************/
/*  project_settings_test_helpers.h                                       */
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

#include "core/config/project_settings.h"

class TestProjectSettingsInternalsAccessor {
public:
	static String &resource_path() {
		return ProjectSettings::get_singleton()->resource_path;
	}

	static String &project_data_dir_name() {
		return ProjectSettings::get_singleton()->project_data_dir_name;
	}

	static bool &project_loaded() {
		return ProjectSettings::get_singleton()->project_loaded;
	}

	static uint64_t &last_save_time() {
		return ProjectSettings::get_singleton()->last_save_time;
	}

	static Error load_settings_text(const String &p_path) {
		return ProjectSettings::get_singleton()->_load_settings_text(p_path);
	}

	static Error load_settings_binary(const String &p_path) {
		return ProjectSettings::get_singleton()->_load_settings_binary(p_path);
	}
};

// RAII guard for tests that call ProjectSettings::setup() against a temporary
// project root. Restores the fields that affect res:// and user:// resolution
// so later cases keep a writable user:// mapping under arbitrary --case filters.
class TestProjectSettingsRestoreScope {
	String saved_resource_path;
	bool saved_project_loaded = false;
	String saved_app_name;

public:
	TestProjectSettingsRestoreScope() {
		saved_resource_path = ProjectSettings::get_singleton()->get_resource_path();
		saved_project_loaded = ProjectSettings::get_singleton()->is_project_loaded();
		saved_app_name = GLOBAL_GET("application/config/name");
	}

	~TestProjectSettingsRestoreScope() {
		TestProjectSettingsInternalsAccessor::resource_path() = saved_resource_path;
		TestProjectSettingsInternalsAccessor::project_loaded() = saved_project_loaded;
		ProjectSettings::get_singleton()->set_setting("application/config/name", saved_app_name);
	}
};
