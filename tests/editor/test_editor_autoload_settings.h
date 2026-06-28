/**************************************************************************/
/*  test_editor_autoload_settings.h                                       */
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

#include "modules/modules_enabled.gen.h"

#ifdef MODULE_GDSCRIPT_ENABLED

#include "editor/settings/editor_autoload_settings.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "modules/gdscript/gdscript.h"
#include "modules/gdscript/gdscript_autoload_index.h"
#include "tests/test_macros.h"

namespace TestEditorAutoloadSettings {

struct TemporaryAutoloadProject {
	String root;

	explicit TemporaryAutoloadProject(const String &p_name) {
		root = OS::get_singleton()->get_temp_path().path_join(p_name);
		remove_recursive(root);
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE_EQ(dir->make_dir_recursive(root), OK);
	}

	~TemporaryAutoloadProject() {
		remove_recursive(root);
	}

	void write_file(const String &p_relative_path, const String &p_contents) const {
		const String absolute_path = root.path_join(p_relative_path);
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE_EQ(dir->make_dir_recursive(absolute_path.get_base_dir()), OK);

		Ref<FileAccess> file = FileAccess::open(absolute_path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string(p_contents);
	}

	static void remove_recursive(const String &p_path) {
		Ref<DirAccess> dir = DirAccess::open(p_path);
		if (dir.is_null()) {
			return;
		}

		dir->set_include_hidden(true);
		dir->list_dir_begin();
		for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
			if (entry == "." || entry == "..") {
				continue;
			}

			const String child = p_path.path_join(entry);
			if (dir->current_is_dir() && !dir->is_link(child)) {
				remove_recursive(child);
			} else {
				DirAccess::remove_absolute(child);
			}
		}
		dir->list_dir_end();
		DirAccess::remove_absolute(p_path);
	}
};

struct ScopedGDScriptLanguage {
	bool initialized = false;

	ScopedGDScriptLanguage() {
		if (!GDScriptLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
			GDScriptLanguage::get_singleton()->init();
			initialized = true;
		}
	}

	~ScopedGDScriptLanguage() {
		if (initialized) {
			GDScriptLanguage::get_singleton()->finish();
		}
	}
};

static GDScriptAutoloadIndexEntry make_autoload_entry(
		const StringName &p_name,
		const String &p_path,
		GDScriptAutoloadIndexEntry::Source p_source,
		int p_order = 0) {
	GDScriptAutoloadIndexEntry entry;
	entry.name = p_name;
	entry.path = p_path;
	entry.is_singleton = true;
	entry.source = p_source;
	entry.order = p_order;
	return entry;
}

static const EditorAutoloadSettings::AutoloadViewEntry *find_view_entry(
		const Vector<EditorAutoloadSettings::AutoloadViewEntry> &p_entries,
		const StringName &p_name) {
	for (const EditorAutoloadSettings::AutoloadViewEntry &entry : p_entries) {
		if (entry.name == p_name) {
			return &entry;
		}
	}
	return nullptr;
}

TEST_CASE("[Editor][AutoloadSettings] View model exposes source diagnostics and editability") {
	GDScriptAutoloadIndexEntry project_entry = make_autoload_entry(
			SNAME("EditorProjectAutoload"),
			"res://project_autoload.gd",
			GDScriptAutoloadIndexEntry::SOURCE_PROJECT_SETTINGS,
			10);

	GDScriptAutoloadIndexEntry script_entry = make_autoload_entry(
			SNAME("EditorScriptAutoload"),
			"res://script_autoload.gd",
			GDScriptAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION,
			20);
	GDScriptAutoloadIndexDiagnostic diagnostic;
	diagnostic.code = GDScriptAutoloadIndexDiagnostic::NON_NODE_SCRIPT;
	diagnostic.message = "Autoload \"EditorScriptAutoload\" does not inherit from Node.";
	script_entry.diagnostics.push_back(diagnostic);

	GDScriptAutoloadIndexEntry conflicting_project_entry = make_autoload_entry(
			SNAME("EditorConflictAutoload"),
			"res://project_conflict.gd",
			GDScriptAutoloadIndexEntry::SOURCE_PROJECT_SETTINGS,
			30);
	GDScriptAutoloadIndexEntry conflicting_script_entry = make_autoload_entry(
			SNAME("EditorConflictAutoload"),
			"res://script_conflict.gd",
			GDScriptAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION,
			40);

	Vector<GDScriptAutoloadIndexEntry> index_entries;
	index_entries.push_back(project_entry);
	index_entries.push_back(script_entry);
	index_entries.push_back(conflicting_project_entry);
	index_entries.push_back(conflicting_script_entry);

	GDScriptAutoloadIndex index;
	index.rebuild_from_entries(index_entries);

	Vector<EditorAutoloadSettings::AutoloadViewEntry> view_entries =
			EditorAutoloadSettings::build_autoload_view_entries(index);

	const EditorAutoloadSettings::AutoloadViewEntry *project_view =
			find_view_entry(view_entries, SNAME("EditorProjectAutoload"));
	REQUIRE(project_view != nullptr);
	CHECK_EQ(project_view->source_label, "Project Settings");
	CHECK(project_view->can_edit_project_settings);
	CHECK(project_view->supports_manual_ordering);
	CHECK_FALSE(project_view->has_diagnostics);
	CHECK_EQ(project_view->diagnostics_summary, "OK");

	const EditorAutoloadSettings::AutoloadViewEntry *script_view =
			find_view_entry(view_entries, SNAME("EditorScriptAutoload"));
	REQUIRE(script_view != nullptr);
	CHECK_EQ(script_view->source_label, "Script");
	CHECK_FALSE(script_view->can_edit_project_settings);
	CHECK_FALSE(script_view->supports_manual_ordering);
	CHECK(script_view->has_diagnostics);
	CHECK_EQ(script_view->diagnostics_summary, "1 Issue");
	CHECK(script_view->diagnostics_text.contains("does not inherit from Node"));

	const EditorAutoloadSettings::AutoloadViewEntry *conflict_view =
			find_view_entry(view_entries, SNAME("EditorConflictAutoload"));
	REQUIRE(conflict_view != nullptr);
	CHECK_EQ(conflict_view->source_label, "Conflict");
	CHECK(conflict_view->has_diagnostics);
	CHECK(conflict_view->has_conflict);
	CHECK_EQ(conflict_view->diagnostics_summary, "Conflict");
	CHECK(conflict_view->diagnostics_text.contains("project_conflict.gd"));
	CHECK(conflict_view->diagnostics_text.contains("script_conflict.gd"));
}

TEST_CASE("[Editor][AutoloadSettings] Project view index includes script-owned annotations") {
	ScopedGDScriptLanguage language;
	TemporaryAutoloadProject project("editor_autoload_settings_script_scan");
	project.write_file("autoloaded.gd",
			"@autoload\n"
			"class_name EditorViewScriptOwned extends Node\n");

	GDScriptAutoloadIndex index = EditorAutoloadSettings::build_autoload_index_for_project_view(project.root);

	const GDScriptAutoloadIndexEntry *entry = index.get_by_name(SNAME("EditorViewScriptOwned"));
	REQUIRE(entry != nullptr);
	CHECK_EQ(entry->source, GDScriptAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION);
	CHECK_EQ(entry->path, project.root.path_join("autoloaded.gd"));
}

} // namespace TestEditorAutoloadSettings

#endif // MODULE_GDSCRIPT_ENABLED
