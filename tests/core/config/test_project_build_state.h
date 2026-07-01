/**************************************************************************/
/*  test_project_build_state.h                                            */
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

#include "core/config/project_build_state.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "tests/core/config/test_project_settings.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestProjectBuildState {

struct ScopedBuildStateProject {
	String old_resource_path;
	String old_project_data_dir_name;
	String root_path;

	explicit ScopedBuildStateProject(const String &p_name) {
		old_resource_path = TestProjectSettingsInternalsAccessor::resource_path();
		old_project_data_dir_name = TestProjectSettingsInternalsAccessor::project_data_dir_name();
		root_path = TestUtils::get_temp_path(p_name + "_" + itos(OS::get_singleton()->get_ticks_usec()));
		const Error err = DirAccess::make_dir_recursive_absolute(root_path);
		CHECK_EQ(err, OK);
		if (err == OK) {
			TestProjectSettingsInternalsAccessor::resource_path() = root_path;
			TestProjectSettingsInternalsAccessor::project_data_dir_name() = "." + ProjectSettings::PROJECT_DATA_DIR_NAME_SUFFIX;
		}
	}

	~ScopedBuildStateProject() {
		remove_recursive(root_path);
		TestProjectSettingsInternalsAccessor::project_data_dir_name() = old_project_data_dir_name;
		TestProjectSettingsInternalsAccessor::resource_path() = old_resource_path;
	}

	String globalize(const String &p_path) const {
		return ProjectSettings::get_singleton()->globalize_path(p_path);
	}

	void write_file(const String &p_path, const String &p_text) const {
		const String absolute_path = globalize(p_path);
		const Error mkdir_err = DirAccess::make_dir_recursive_absolute(absolute_path.get_base_dir());
		CHECK_EQ(mkdir_err, OK);
		if (mkdir_err != OK) {
			return;
		}

		Ref<FileAccess> file = FileAccess::open(absolute_path, FileAccess::WRITE);
		CHECK_MESSAGE(file.is_valid(), vformat("Cannot write '%s'.", absolute_path));
		if (file.is_valid()) {
			file->store_string(p_text);
		}
	}

	static void remove_recursive(const String &p_absolute_path) {
		Ref<DirAccess> dir = DirAccess::open(p_absolute_path);
		if (dir.is_null()) {
			return;
		}

		dir->set_include_hidden(true);
		dir->list_dir_begin();
		for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
			if (entry == "." || entry == "..") {
				continue;
			}

			const String child = p_absolute_path.path_join(entry);
			if (dir->current_is_dir() && !dir->is_link(child)) {
				remove_recursive(child);
			} else {
				DirAccess::remove_absolute(child);
			}
		}
		dir->list_dir_end();
		DirAccess::remove_absolute(p_absolute_path);
	}
};

static PackedStringArray make_paths(const String &p_a, const String &p_b = String()) {
	PackedStringArray paths;
	paths.push_back(p_a);
	if (!p_b.is_empty()) {
		paths.push_back(p_b);
	}
	return paths;
}

TEST_CASE("[ProjectBuildState] successful task state persists under project data path and reloads clean") {
	ScopedBuildStateProject project("project_build_state_persist");
	project.write_file("res://generated/out.txt", "output-v1\n");

	const PackedStringArray outputs = make_paths("res://generated/out.txt");

	ProjectBuildState state;
	CHECK_EQ(state.get_state_path(), "res://.foundry/build_state.cfg");
	state.record_task_result("generate_proto", "fingerprint-v1", outputs, true);
	CHECK_EQ(state.save(), OK);
	CHECK(FileAccess::exists(project.globalize("res://.foundry/build_state.cfg")));

	ProjectBuildState reloaded;
	CHECK_EQ(reloaded.load(), OK);

	const ProjectBuildState::TaskRecord *record = reloaded.get_task_record("generate_proto");
	REQUIRE(record != nullptr);
	CHECK(record->success);
	CHECK_EQ(record->fingerprint, "fingerprint-v1");
	CHECK_FALSE(record->output_manifest.is_empty());

	const ProjectBuildState::DirtyStatus first_status =
			reloaded.get_task_dirty_status("generate_proto", "fingerprint-v1", outputs);
	CHECK_FALSE(first_status.dirty);
	CHECK_EQ(first_status.reason, ProjectBuildState::DIRTY_NONE);

	const ProjectBuildState::DirtyStatus second_status =
			reloaded.get_task_dirty_status("generate_proto", "fingerprint-v1", outputs);
	CHECK_FALSE(second_status.dirty);
	CHECK_EQ(second_status.reason, ProjectBuildState::DIRTY_NONE);
}

TEST_CASE("[ProjectBuildState] config input and tool fingerprint changes mark tasks dirty") {
	ScopedBuildStateProject project("project_build_state_fingerprint");
	project.write_file("res://generated/out.txt", "output-v1\n");

	const PackedStringArray outputs = make_paths("res://generated/out.txt");
	ProjectBuildState state;
	state.record_task_result("generate_proto", "fingerprint-v1", outputs, true);

	const ProjectBuildState::DirtyStatus status =
			state.get_task_dirty_status("generate_proto", "fingerprint-v2", outputs);
	CHECK(status.dirty);
	CHECK_EQ(status.reason, ProjectBuildState::DIRTY_FINGERPRINT_CHANGED);
}

TEST_CASE("[ProjectBuildState] deleted or edited outputs mark tasks dirty") {
	ScopedBuildStateProject project("project_build_state_outputs");
	project.write_file("res://generated/out.txt", "output-v1\n");

	const PackedStringArray outputs = make_paths("res://generated/out.txt");
	ProjectBuildState state;
	state.record_task_result("generate_proto", "fingerprint-v1", outputs, true);

	project.write_file("res://generated/out.txt", "output-v2\n");
	ProjectBuildState::DirtyStatus status =
			state.get_task_dirty_status("generate_proto", "fingerprint-v1", outputs);
	CHECK(status.dirty);
	CHECK_EQ(status.reason, ProjectBuildState::DIRTY_OUTPUT_CHANGED);

	DirAccess::remove_absolute(project.globalize("res://generated/out.txt"));
	status = state.get_task_dirty_status("generate_proto", "fingerprint-v1", outputs);
	CHECK(status.dirty);
	CHECK_EQ(status.reason, ProjectBuildState::DIRTY_OUTPUT_MISSING);
}

TEST_CASE("[ProjectBuildState] failed previous runs and provider-forced reruns stay dirty") {
	ScopedBuildStateProject project("project_build_state_failures");
	project.write_file("res://generated/out.txt", "output-v1\n");

	const PackedStringArray outputs = make_paths("res://generated/out.txt");

	ProjectBuildState missing_state;
	ProjectBuildState::DirtyStatus status =
			missing_state.get_task_dirty_status("generate_proto", "fingerprint-v1", outputs);
	CHECK(status.dirty);
	CHECK_EQ(status.reason, ProjectBuildState::DIRTY_MISSING_STATE);

	ProjectBuildState failed_state;
	failed_state.record_task_result("generate_proto", "fingerprint-v1", outputs, false);
	CHECK_EQ(failed_state.save(), OK);
	status = failed_state.get_task_dirty_status("generate_proto", "fingerprint-v1", outputs);
	CHECK(status.dirty);
	CHECK_EQ(status.reason, ProjectBuildState::DIRTY_PREVIOUS_FAILURE);

	ProjectBuildState reloaded_failed_state;
	CHECK_EQ(reloaded_failed_state.load(), OK);
	status = reloaded_failed_state.get_task_dirty_status("generate_proto", "fingerprint-v1", outputs);
	CHECK(status.dirty);
	CHECK_EQ(status.reason, ProjectBuildState::DIRTY_PREVIOUS_FAILURE);

	ProjectBuildState forced_state;
	forced_state.record_task_result("generate_proto", "fingerprint-v1", outputs, true);
	status = forced_state.get_task_dirty_status("generate_proto", "fingerprint-v1", outputs, true);
	CHECK(status.dirty);
	CHECK_EQ(status.reason, ProjectBuildState::DIRTY_PROVIDER_FORCED);
}

TEST_CASE("[ProjectBuildState] unreadable state files keep tasks dirty") {
	ScopedBuildStateProject project("project_build_state_unreadable");
	project.write_file("res://.foundry/build_state.cfg", "[build_state\nnot valid\n");

	ProjectBuildState state;
	ERR_PRINT_OFF;
	CHECK_NE(state.load(), OK);
	ERR_PRINT_ON;

	const ProjectBuildState::DirtyStatus status =
			state.get_task_dirty_status("generate_proto", "fingerprint-v1", make_paths("res://generated/out.txt"));
	CHECK(status.dirty);
	CHECK_EQ(status.reason, ProjectBuildState::DIRTY_UNREADABLE_STATE);

	project.write_file("res://generated/recovered.txt", "ok\n");
	const PackedStringArray outputs = make_paths("res://generated/recovered.txt");
	state.record_task_result("generate_proto", "fingerprint-v1", outputs, true);
	CHECK_FALSE(state.get_task_dirty_status("generate_proto", "fingerprint-v1", outputs).dirty);
}

TEST_CASE("[ProjectBuildState] directory output manifests survive editor restart") {
	ScopedBuildStateProject project("project_build_state_directory_manifest");
	project.write_file("res://generated/a.fs", "class_name A\n");
	project.write_file("res://generated/nested/b.fs", "class_name B\n");

	const PackedStringArray outputs = make_paths("res://generated/");
	ProjectBuildState state;
	state.record_task_result("generate_sources", "fingerprint-v1", outputs, true);
	CHECK_EQ(state.save(), OK);

	ProjectBuildState reloaded;
	CHECK_EQ(reloaded.load(), OK);
	CHECK_FALSE(reloaded.get_task_dirty_status("generate_sources", "fingerprint-v1", outputs).dirty);

	DirAccess::remove_absolute(project.globalize("res://generated/nested/b.fs"));
	const ProjectBuildState::DirtyStatus status =
			reloaded.get_task_dirty_status("generate_sources", "fingerprint-v1", outputs);
	CHECK(status.dirty);
	CHECK_EQ(status.reason, ProjectBuildState::DIRTY_OUTPUT_CHANGED);
}

TEST_CASE("[ProjectBuildState] symlinked output roots stay dirty") {
	ScopedBuildStateProject project("project_build_state_symlink_root");
	project.write_file("res://outside/secret.txt", "outside\n");

	const String link_path = project.globalize("res://generated");
	REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(link_path.get_base_dir()), OK);

	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(dir.is_valid());
	if (dir->create_link(project.globalize("res://outside"), link_path) != OK) {
		return;
	}

	const PackedStringArray outputs = make_paths("res://generated/");
	ProjectBuildState state;
	state.record_task_result("generate_sources", "fingerprint-v1", outputs, true);

	const ProjectBuildState::DirtyStatus status =
			state.get_task_dirty_status("generate_sources", "fingerprint-v1", outputs);
	CHECK(status.dirty);
	CHECK_EQ(status.reason, ProjectBuildState::DIRTY_OUTPUT_MISSING);
}

TEST_CASE("[ProjectBuildState] outputs under symlinked parents stay dirty") {
	ScopedBuildStateProject project("project_build_state_symlink_parent");
	project.write_file("res://outside/out.txt", "outside\n");

	const String link_path = project.globalize("res://generated/link");
	REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(link_path.get_base_dir()), OK);

	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(dir.is_valid());
	if (dir->create_link(project.globalize("res://outside"), link_path) != OK) {
		return;
	}

	const PackedStringArray outputs = make_paths("res://generated/link/out.txt");
	ProjectBuildState state;
	state.record_task_result("generate_sources", "fingerprint-v1", outputs, true);

	const ProjectBuildState::DirtyStatus status =
			state.get_task_dirty_status("generate_sources", "fingerprint-v1", outputs);
	CHECK(status.dirty);
	CHECK_EQ(status.reason, ProjectBuildState::DIRTY_OUTPUT_MISSING);
}

TEST_CASE("[ProjectBuildState] unreadable output files stay dirty") {
	ScopedBuildStateProject project("project_build_state_unreadable_output");
	project.write_file("res://generated/unreadable.txt", "secret\n");

	const String output_path = project.globalize("res://generated/unreadable.txt");
	if (FileAccess::set_unix_permissions(output_path, 0000) != OK) {
		return;
	}

	const PackedStringArray outputs = make_paths("res://generated/unreadable.txt");
	ProjectBuildState state;
	ERR_PRINT_OFF;
	state.record_task_result("generate_unreadable", "fingerprint-v1", outputs, true);
	const ProjectBuildState::DirtyStatus status =
			state.get_task_dirty_status("generate_unreadable", "fingerprint-v1", outputs);
	ERR_PRINT_ON;

	FileAccess::set_unix_permissions(output_path, 0644);

	CHECK(status.dirty);
	CHECK_EQ(status.reason, ProjectBuildState::DIRTY_OUTPUT_MISSING);
}

} // namespace TestProjectBuildState
