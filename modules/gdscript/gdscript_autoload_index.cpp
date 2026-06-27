/**************************************************************************/
/*  gdscript_autoload_index.cpp                                           */
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

#include "gdscript_autoload_index.h"

#include "gdscript.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"

namespace {

struct OrderedAutoloadEntry {
	GDScriptAutoloadIndexEntry entry;
	int source_index = 0;
};

struct OrderedAutoloadEntryComparator {
	bool operator()(const OrderedAutoloadEntry &p_a, const OrderedAutoloadEntry &p_b) const {
		if (p_a.entry.order != p_b.entry.order) {
			return p_a.entry.order < p_b.entry.order;
		}
		return p_a.source_index < p_b.source_index;
	}
};

void add_diagnostic(GDScriptAutoloadIndexEntry &r_entry, GDScriptAutoloadIndexDiagnostic::Code p_code, const String &p_message) {
	GDScriptAutoloadIndexDiagnostic diagnostic;
	diagnostic.code = p_code;
	diagnostic.message = p_message;
	r_entry.diagnostics.push_back(diagnostic);
}

bool is_reserved_global_name(const StringName &p_name) {
	for (int i = 0; i < ScriptServer::get_language_count(); i++) {
		if (ScriptServer::get_language(i)->get_reserved_global_names().has(String(p_name))) {
			return true;
		}
	}
	return false;
}

StringName get_native_base(const StringName &p_base) {
	if (p_base == StringName()) {
		return StringName();
	}
	if (ScriptServer::is_global_class(p_base)) {
		return ScriptServer::get_global_class_native_base(p_base);
	}
	return p_base;
}

bool is_gdscript_path(const String &p_path, const String &p_resource_type) {
	GDScriptLanguage *language = GDScriptLanguage::get_singleton();
	return p_resource_type == "GDScript" || (language != nullptr && p_path.get_extension() == language->get_extension());
}

bool is_scene_path(const String &p_path, const String &p_resource_type) {
	return p_resource_type == "PackedScene" || p_path.get_extension() == "tscn" || p_path.get_extension() == "scn";
}

void populate_gdscript_metadata(GDScriptAutoloadIndexEntry &r_entry) {
	GDScriptLanguage *language = GDScriptLanguage::get_singleton();
	if (language == nullptr) {
		return;
	}

	String base_type;
	bool is_tool = false;
	const String global_class_name = language->get_global_class_name(r_entry.path, &base_type, nullptr, nullptr, &is_tool, nullptr);

	r_entry.script_path = r_entry.path;
	r_entry.global_class_name = global_class_name;
	r_entry.native_base = get_native_base(base_type);
	r_entry.is_tool = is_tool;
	r_entry.is_node = r_entry.native_base != StringName() && ClassDB::is_parent_class(r_entry.native_base, SNAME("Node"));
	r_entry.is_same_script_global_class = r_entry.global_class_name == r_entry.name && GDScript::is_canonically_equal_paths(r_entry.script_path, r_entry.path);

	if (!r_entry.is_node) {
		add_diagnostic(r_entry,
				GDScriptAutoloadIndexDiagnostic::NON_NODE_SCRIPT,
				vformat("Autoload \"%s\" points to script \"%s\", which does not inherit from Node.", String(r_entry.name), r_entry.path));
	}
}

void populate_resource_metadata(GDScriptAutoloadIndexEntry &r_entry) {
	if (!FileAccess::exists(r_entry.path)) {
		add_diagnostic(r_entry,
				GDScriptAutoloadIndexDiagnostic::MISSING_PATH,
				vformat("Autoload \"%s\" points to missing path \"%s\".", String(r_entry.name), r_entry.path));
		return;
	}

	const String resource_type = ResourceLoader::get_resource_type(r_entry.path);
	if (is_gdscript_path(r_entry.path, resource_type)) {
		populate_gdscript_metadata(r_entry);
		return;
	}

	if (is_scene_path(r_entry.path, resource_type)) {
		r_entry.is_node = true;
		return;
	}

	add_diagnostic(r_entry,
			GDScriptAutoloadIndexDiagnostic::NON_SCRIPT_NON_SCENE_PATH,
			vformat("Autoload \"%s\" points to \"%s\", which is not a script or scene.", String(r_entry.name), r_entry.path));
}

void populate_name_diagnostics(GDScriptAutoloadIndexEntry &r_entry) {
	if (r_entry.is_singleton && is_reserved_global_name(r_entry.name)) {
		add_diagnostic(r_entry,
				GDScriptAutoloadIndexDiagnostic::RESERVED_GLOBAL_NAME_COLLISION,
				vformat("Autoload \"%s\" collides with a reserved language global.", String(r_entry.name)));
	}

	if (!ScriptServer::is_global_class(r_entry.name)) {
		return;
	}

	const String global_class_path = ScriptServer::get_global_class_path(r_entry.name);
	if (GDScript::is_canonically_equal_paths(global_class_path, r_entry.path)) {
		r_entry.is_same_script_global_class = true;
		return;
	}

	add_diagnostic(r_entry,
			GDScriptAutoloadIndexDiagnostic::UNRELATED_GLOBAL_CLASS_COLLISION,
			vformat("Autoload \"%s\" collides with global script class from \"%s\".", String(r_entry.name), global_class_path));
}

} // namespace

void GDScriptAutoloadIndex::clear() {
	entries.clear();
	name_lookup.clear();
	path_lookup.clear();
	global_class_lookup.clear();
}

void GDScriptAutoloadIndex::rebuild_lookups() {
	name_lookup.clear();
	path_lookup.clear();
	global_class_lookup.clear();

	for (int i = 0; i < entries.size(); i++) {
		const GDScriptAutoloadIndexEntry &entry = entries[i];
		name_lookup[entry.name] = i;
		if (!entry.path.is_empty()) {
			path_lookup[entry.path] = i;
		}
		if (entry.global_class_name != StringName()) {
			global_class_lookup[entry.global_class_name] = i;
		}
	}
}

void GDScriptAutoloadIndex::rebuild_from_project_settings() {
	clear();

	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (project_settings == nullptr) {
		version++;
		return;
	}

	HashMap<StringName, int> settings_order;
	List<PropertyInfo> properties;
	project_settings->get_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		if (!property.name.begins_with("autoload/") && !property.name.begins_with("autoload_prepend/")) {
			continue;
		}
		const StringName autoload_name = property.name.get_slicec('/', 1);
		settings_order[autoload_name] = project_settings->get_order(property.name);
	}

	Vector<OrderedAutoloadEntry> ordered_entries;
	int source_index = 0;
	for (const KeyValue<StringName, ProjectSettings::AutoloadInfo> &kv : project_settings->get_autoload_list()) {
		const ProjectSettings::AutoloadInfo &autoload = kv.value;

		OrderedAutoloadEntry ordered;
		ordered.source_index = source_index;
		ordered.entry.name = autoload.name;
		ordered.entry.path = ResourceUID::ensure_path(autoload.path);
		ordered.entry.is_singleton = autoload.is_singleton;
		ordered.entry.source = GDScriptAutoloadIndexEntry::SOURCE_PROJECT_SETTINGS;

		const int *setting_order = settings_order.getptr(autoload.name);
		ordered.entry.order = setting_order != nullptr ? *setting_order : source_index;

		populate_resource_metadata(ordered.entry);
		populate_name_diagnostics(ordered.entry);

		ordered_entries.push_back(ordered);
		source_index++;
	}

	ordered_entries.sort_custom<OrderedAutoloadEntryComparator>();
	for (const OrderedAutoloadEntry &ordered : ordered_entries) {
		entries.push_back(ordered.entry);
	}

	rebuild_lookups();
	version++;
}

bool GDScriptAutoloadIndex::has_autoload(const StringName &p_name) const {
	return name_lookup.has(p_name);
}

const GDScriptAutoloadIndexEntry *GDScriptAutoloadIndex::get_by_name(const StringName &p_name) const {
	const int *index = name_lookup.getptr(p_name);
	return index != nullptr ? &entries[*index] : nullptr;
}

const GDScriptAutoloadIndexEntry *GDScriptAutoloadIndex::get_by_path(const String &p_path) const {
	const int *index = path_lookup.getptr(ResourceUID::ensure_path(p_path));
	return index != nullptr ? &entries[*index] : nullptr;
}

const GDScriptAutoloadIndexEntry *GDScriptAutoloadIndex::get_by_global_class(const StringName &p_global_class_name) const {
	const int *index = global_class_lookup.getptr(p_global_class_name);
	return index != nullptr ? &entries[*index] : nullptr;
}
