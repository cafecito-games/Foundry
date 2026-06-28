/**************************************************************************/
/*  test_editor_export_platform_autoload.h                                */
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

#ifdef TOOLS_ENABLED

#include "modules/modules_enabled.gen.h"

#ifdef MODULE_GDSCRIPT_ENABLED

#include "editor/export/editor_export_platform.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/script_language.h"
#include "core/os/os.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestEditorExportPlatformAutoload {

class ScopedScriptServerClass {
	StringName class_name;

public:
	ScopedScriptServerClass(const StringName &p_class_name, const String &p_base, const String &p_path) {
		class_name = p_class_name;
		ScriptServer::remove_global_class(class_name);
		ScriptServer::add_global_class(class_name, p_base, SNAME("GDScript"), p_path, false, false, false);
	}

	~ScopedScriptServerClass() {
		ScriptServer::remove_global_class(class_name);
	}
};

class ScopedExportTempFiles {
	String root;
	Vector<String> files;
	Vector<String> directories;

public:
	ScopedExportTempFiles(const String &p_name) {
		root = TestUtils::get_temp_path(p_name + "_" + itos(OS::get_singleton()->get_ticks_usec()));
		REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(root), OK);
		directories.push_back(root);
	}

	~ScopedExportTempFiles() {
		for (const String &file : files) {
			DirAccess::remove_absolute(file);
		}
		for (int i = directories.size() - 1; i >= 0; i--) {
			DirAccess::remove_absolute(directories[i]);
		}
	}

	String write(const String &p_file_name, const String &p_contents) {
		const String path = root.path_join(p_file_name);
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string(p_contents);
		files.push_back(path);
		return path;
	}

	String make_directory(const String &p_dir_name) {
		const String path = root.path_join(p_dir_name);
		REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(path), OK);
		directories.push_back(path);
		return path;
	}
};

TEST_CASE("[Editor][Export] Script-owned autoload cache write failure is a hard forced-file error") {
	ScopedExportTempFiles files("editor_export_autoload_cache_failure");

	const String script_path = files.write("export_cache_required.gd",
			"@autoload\n"
			"class_name EditorExportAutoloadCacheRequired extends Node\n");
	ScopedScriptServerClass registered_autoload(
			SNAME("EditorExportAutoloadCacheRequired"),
			"Node",
			script_path);

	const String blocked_cache_path = files.make_directory("autoload_index_cache.cfg");

	Vector<String> forced_files;
	const Error err = EditorExportPlatform::collect_forced_export_files(
			Ref<EditorExportPreset>(),
			forced_files,
			true,
			blocked_cache_path);

	CHECK_NE(err, OK);
	CHECK_EQ(forced_files.find(blocked_cache_path), -1);
}

} // namespace TestEditorExportPlatformAutoload

#endif // MODULE_GDSCRIPT_ENABLED

#endif // TOOLS_ENABLED
