/**************************************************************************/
/*  gdscript_autoload_index.h                                             */
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

#include "core/config/project_settings.h"
#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

struct GDScriptAutoloadIndexDiagnostic {
	enum Code {
		MISSING_PATH,
		NON_SCRIPT_NON_SCENE_PATH,
		NON_NODE_SCRIPT,
		RESERVED_GLOBAL_NAME_COLLISION,
		UNRELATED_GLOBAL_CLASS_COLLISION,
		CONFLICTING_AUTOLOAD_PATH,
		MISSING_DEPENDENCY,
		NON_AUTOLOAD_DEPENDENCY,
		CYCLIC_DEPENDENCY,
	};

	Code code = MISSING_PATH;
	String message;
	bool is_error = true;
};

struct GDScriptAutoloadIndexDependency {
	StringName name;
	bool is_autoload = true;
};

struct GDScriptAutoloadIndexEntry {
	enum Source {
		SOURCE_PROJECT_SETTINGS,
		SOURCE_SCRIPT_ANNOTATION,
	};

	StringName name;
	String path;
	bool is_singleton = false;
	int order = 0;
	Source source = SOURCE_PROJECT_SETTINGS;

	StringName global_class_name;
	String script_path;
	StringName native_base;
	bool is_node = false;
	bool is_tool = false;
	bool is_same_script_global_class = false;

	Vector<GDScriptAutoloadIndexDependency> dependencies;
	Vector<GDScriptAutoloadIndexDiagnostic> diagnostics;
};

class GDScriptAutoloadIndex {
	Vector<GDScriptAutoloadIndexEntry> entries;
	HashMap<StringName, int> name_lookup;
	HashMap<String, int> path_lookup;
	HashMap<StringName, int> global_class_lookup;
	uint64_t version = 0;

	void clear();
	void sort_and_validate_dependencies();
	void rebuild_lookups();

public:
	void rebuild_from_project_settings();
	void rebuild_from_entries(const Vector<GDScriptAutoloadIndexEntry> &p_entries);
	Error rebuild_from_cache_and_project_settings(const String &p_cache_path = String());
	Error rebuild_for_runtime_startup(const String &p_cache_path = String());

	static String get_cache_path();
	Error save_to_cache(const String &p_cache_path = String()) const;
	Error load_from_cache(const String &p_cache_path = String());
#ifdef TOOLS_ENABLED
	Error rebuild_from_project_settings_and_script_annotations();
#endif // TOOLS_ENABLED

	bool has_autoload(const StringName &p_name) const;
	const GDScriptAutoloadIndexEntry *get_by_name(const StringName &p_name) const;
	const GDScriptAutoloadIndexEntry *get_by_path(const String &p_path) const;
	const GDScriptAutoloadIndexEntry *get_by_global_class(const StringName &p_global_class_name) const;
	Vector<ProjectSettings::AutoloadInfo> get_startup_autoloads() const;
	void register_startup_autoloads_in_project_settings() const;

	const Vector<GDScriptAutoloadIndexEntry> &get_entries() const { return entries; }
	uint64_t get_version() const { return version; }
};
