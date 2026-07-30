/**************************************************************************/
/*  fs_autoload_index.cpp                                                 */
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

#include "fs_autoload_index.h"

#include "foundry_script.h"
#ifndef FOUNDRY_SCRIPT_NO_FRONTEND
#include "fs_analyzer.h"
#endif // FOUNDRY_SCRIPT_NO_FRONTEND
#include "fs_cache.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"

namespace {

static constexpr const char *AUTOLOAD_INDEX_CACHE_FILE = "autoload_index_cache.cfg";
static constexpr const char *AUTOLOAD_INDEX_CACHE_SECTION = "";
static constexpr const char *AUTOLOAD_INDEX_CACHE_ENTRIES_KEY = "entries";

struct OrderedAutoloadEntry {
	FSAutoloadIndexEntry entry;
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

void sort_entries_by_order(Vector<FSAutoloadIndexEntry> &r_entries) {
	Vector<OrderedAutoloadEntry> ordered_entries;
	for (int i = 0; i < r_entries.size(); i++) {
		OrderedAutoloadEntry ordered;
		ordered.entry = r_entries[i];
		ordered.source_index = i;
		ordered_entries.push_back(ordered);
	}

	ordered_entries.sort_custom<OrderedAutoloadEntryComparator>();
	r_entries.clear();
	for (const OrderedAutoloadEntry &ordered : ordered_entries) {
		r_entries.push_back(ordered.entry);
	}
}

#ifdef TOOLS_ENABLED
Error collect_script_annotation_autoload_entries(const String &p_script_path, Vector<FSAutoloadIndexEntry> &r_entries) {
	FSParser parser;
	const String source = FSCache::get_source_code(ResourceLoader::path_remap(p_script_path));
	if (!source.contains("@autoload")) {
		return OK;
	}

	Error err = parser.parse(source, p_script_path, false);
	if (err != OK) {
		return err;
	}

	FSAnalyzer analyzer(&parser);
	err = analyzer.resolve_inheritance();
	if (err != OK) {
		return err;
	}
	err = analyzer.resolve_interface();
	if (err != OK) {
		return err;
	}

	const FSAutoloadIndex &script_index = analyzer.get_autoload_index();
	for (const FSAutoloadIndexEntry &entry : script_index.get_entries()) {
		if (entry.source == FSAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION) {
			r_entries.push_back(entry);
		}
	}

	return OK;
}
#endif // TOOLS_ENABLED

void add_diagnostic(FSAutoloadIndexEntry &r_entry, FSAutoloadIndexDiagnostic::Code p_code, const String &p_message) {
	FSAutoloadIndexDiagnostic diagnostic;
	diagnostic.code = p_code;
	diagnostic.message = p_message;
	r_entry.diagnostics.push_back(diagnostic);
}

String source_to_string(FSAutoloadIndexEntry::Source p_source) {
	switch (p_source) {
		case FSAutoloadIndexEntry::SOURCE_PROJECT_SETTINGS:
			return "project_settings";
		case FSAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION:
			return "script_annotation";
	}
	return "project_settings";
}

FSAutoloadIndexEntry::Source source_from_string(const String &p_source) {
	if (p_source == "script_annotation") {
		return FSAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION;
	}
	return FSAutoloadIndexEntry::SOURCE_PROJECT_SETTINGS;
}

Dictionary dependency_to_dictionary(const FSAutoloadIndexDependency &p_dependency) {
	Dictionary dictionary;
	dictionary["name"] = p_dependency.name;
	dictionary["is_autoload"] = p_dependency.is_autoload;
	return dictionary;
}

FSAutoloadIndexDependency dependency_from_dictionary(const Dictionary &p_dictionary) {
	FSAutoloadIndexDependency dependency;
	dependency.name = StringName(String(p_dictionary.get("name", String())));
	dependency.is_autoload = p_dictionary.get("is_autoload", true);
	return dependency;
}

Dictionary diagnostic_to_dictionary(const FSAutoloadIndexDiagnostic &p_diagnostic) {
	Dictionary dictionary;
	dictionary["code"] = p_diagnostic.code;
	dictionary["message"] = p_diagnostic.message;
	dictionary["is_error"] = p_diagnostic.is_error;
	return dictionary;
}

FSAutoloadIndexDiagnostic diagnostic_from_dictionary(const Dictionary &p_dictionary) {
	FSAutoloadIndexDiagnostic diagnostic;
	diagnostic.code = static_cast<FSAutoloadIndexDiagnostic::Code>(int(p_dictionary.get("code", 0)));
	diagnostic.message = p_dictionary.get("message", String());
	diagnostic.is_error = p_dictionary.get("is_error", true);
	return diagnostic;
}

Dictionary entry_to_dictionary(const FSAutoloadIndexEntry &p_entry) {
	Dictionary dictionary;
	dictionary["name"] = p_entry.name;
	dictionary["path"] = p_entry.path;
	dictionary["is_singleton"] = p_entry.is_singleton;
	dictionary["order"] = p_entry.order;
	dictionary["source"] = source_to_string(p_entry.source);
	dictionary["global_class_name"] = p_entry.global_class_name;
	dictionary["script_path"] = p_entry.script_path;
	dictionary["native_base"] = p_entry.native_base;
	dictionary["is_node"] = p_entry.is_node;
	dictionary["is_tool"] = p_entry.is_tool;
	dictionary["is_same_script_global_class"] = p_entry.is_same_script_global_class;

	Array dependencies;
	for (const FSAutoloadIndexDependency &dependency : p_entry.dependencies) {
		dependencies.push_back(dependency_to_dictionary(dependency));
	}
	dictionary["dependencies"] = dependencies;

	Array diagnostics;
	for (const FSAutoloadIndexDiagnostic &diagnostic : p_entry.diagnostics) {
		diagnostics.push_back(diagnostic_to_dictionary(diagnostic));
	}
	dictionary["diagnostics"] = diagnostics;

	return dictionary;
}

bool entry_from_dictionary(const Dictionary &p_dictionary, FSAutoloadIndexEntry &r_entry) {
	if (!p_dictionary.has("name") || !p_dictionary.has("path")) {
		return false;
	}

	r_entry.name = StringName(String(p_dictionary["name"]));
	r_entry.path = p_dictionary["path"];
	r_entry.is_singleton = p_dictionary.get("is_singleton", true);
	r_entry.order = p_dictionary.get("order", 0);
	r_entry.source = source_from_string(p_dictionary.get("source", "project_settings"));
	r_entry.global_class_name = StringName(String(p_dictionary.get("global_class_name", String())));
	r_entry.script_path = p_dictionary.get("script_path", String());
	r_entry.native_base = StringName(String(p_dictionary.get("native_base", String())));
	r_entry.is_node = p_dictionary.get("is_node", false);
	r_entry.is_tool = p_dictionary.get("is_tool", false);
	r_entry.is_same_script_global_class = p_dictionary.get("is_same_script_global_class", false);

	Array dependencies = p_dictionary.get("dependencies", Array());
	for (const Variant &dependency_variant : dependencies) {
		if (dependency_variant.get_type() != Variant::DICTIONARY) {
			continue;
		}
		r_entry.dependencies.push_back(dependency_from_dictionary(dependency_variant));
	}

	Array diagnostics = p_dictionary.get("diagnostics", Array());
	for (const Variant &diagnostic_variant : diagnostics) {
		if (diagnostic_variant.get_type() != Variant::DICTIONARY) {
			continue;
		}
		r_entry.diagnostics.push_back(diagnostic_from_dictionary(diagnostic_variant));
	}

	return r_entry.name != StringName();
}

bool is_dependency_diagnostic(FSAutoloadIndexDiagnostic::Code p_code) {
	return p_code == FSAutoloadIndexDiagnostic::MISSING_DEPENDENCY ||
			p_code == FSAutoloadIndexDiagnostic::NON_AUTOLOAD_DEPENDENCY ||
			p_code == FSAutoloadIndexDiagnostic::CYCLIC_DEPENDENCY;
}

bool is_name_diagnostic(FSAutoloadIndexDiagnostic::Code p_code) {
	return p_code == FSAutoloadIndexDiagnostic::RESERVED_GLOBAL_NAME_COLLISION ||
			p_code == FSAutoloadIndexDiagnostic::UNRELATED_GLOBAL_CLASS_COLLISION;
}

void clear_dependency_diagnostics(Vector<FSAutoloadIndexEntry> &r_entries) {
	for (int i = 0; i < r_entries.size(); i++) {
		FSAutoloadIndexEntry &entry = r_entries.write[i];
		Vector<FSAutoloadIndexDiagnostic> retained;
		for (const FSAutoloadIndexDiagnostic &diagnostic : entry.diagnostics) {
			if (!is_dependency_diagnostic(diagnostic.code)) {
				retained.push_back(diagnostic);
			}
		}
		entry.diagnostics = retained;
	}
}

void clear_name_diagnostics(Vector<FSAutoloadIndexEntry> &r_entries) {
	for (int i = 0; i < r_entries.size(); i++) {
		FSAutoloadIndexEntry &entry = r_entries.write[i];
		Vector<FSAutoloadIndexDiagnostic> retained;
		for (const FSAutoloadIndexDiagnostic &diagnostic : entry.diagnostics) {
			if (!is_name_diagnostic(diagnostic.code)) {
				retained.push_back(diagnostic);
			}
		}
		entry.diagnostics = retained;
	}
}

void merge_compatible_autoload_entry(
		FSAutoloadIndexEntry &r_existing,
		const FSAutoloadIndexEntry &p_incoming) {
	Vector<FSAutoloadIndexDiagnostic> diagnostics = r_existing.diagnostics;
	for (const FSAutoloadIndexDiagnostic &diagnostic : p_incoming.diagnostics) {
		diagnostics.push_back(diagnostic);
	}

	if (p_incoming.source == FSAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION) {
		r_existing = p_incoming;
	} else if (r_existing.source != FSAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION) {
		r_existing = p_incoming;
	}

	r_existing.diagnostics = diagnostics;
}

void normalize_duplicate_entries(Vector<FSAutoloadIndexEntry> &r_entries) {
	Vector<FSAutoloadIndexEntry> normalized;
	HashMap<StringName, int> name_indices;

	for (int i = 0; i < r_entries.size(); i++) {
		FSAutoloadIndexEntry entry = r_entries[i];
		if (!entry.path.is_empty()) {
			entry.path = ResourceUID::ensure_path(entry.path);
		}

		const int *existing_index = name_indices.getptr(entry.name);
		if (existing_index == nullptr) {
			name_indices[entry.name] = normalized.size();
			normalized.push_back(entry);
			continue;
		}

		FSAutoloadIndexEntry &existing = normalized.write[*existing_index];
		if (FoundryScript::is_canonically_equal_paths(existing.path, entry.path)) {
			merge_compatible_autoload_entry(existing, entry);
			continue;
		}

		Vector<FSAutoloadIndexDiagnostic> diagnostics = existing.diagnostics;
		for (const FSAutoloadIndexDiagnostic &diagnostic : entry.diagnostics) {
			diagnostics.push_back(diagnostic);
		}

		const String existing_path = existing.path;
		const String entry_path = entry.path;
		if (entry.source == FSAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION &&
				existing.source != FSAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION) {
			existing = entry;
		}
		existing.diagnostics = diagnostics;
		add_diagnostic(existing,
				FSAutoloadIndexDiagnostic::CONFLICTING_AUTOLOAD_PATH,
				vformat("Autoload \"%s\" is declared with conflicting paths \"%s\" and \"%s\" during migration.",
						String(existing.name),
						existing_path,
						entry_path));
	}

	r_entries = normalized;
}

String format_dependency_cycle_path(const Vector<int> &p_cycle, const Vector<FSAutoloadIndexEntry> &p_entries) {
	String path;
	for (int i = 0; i < p_cycle.size(); i++) {
		if (i > 0) {
			path += " -> ";
		}
		path += String(p_entries[p_cycle[i]].name);
	}
	return path;
}

bool find_dependency_cycle_from_start_dfs(int p_start,
		int p_index,
		const Vector<Vector<int>> &p_dependency_edges,
		const Vector<uint8_t> &p_consider,
		Vector<int> &r_state,
		Vector<int> &r_stack,
		Vector<int> &r_cycle) {
	r_state.write[p_index] = 1;
	r_stack.push_back(p_index);

	for (const int dependency_index : p_dependency_edges[p_index]) {
		if (!p_consider[dependency_index]) {
			continue;
		}

		if (dependency_index == p_start) {
			for (const int cycle_index : r_stack) {
				r_cycle.push_back(cycle_index);
			}
			r_cycle.push_back(p_start);
			return true;
		}

		if (r_state[dependency_index] == 0) {
			if (find_dependency_cycle_from_start_dfs(p_start, dependency_index, p_dependency_edges, p_consider, r_state, r_stack, r_cycle)) {
				return true;
			}
		}
	}

	r_stack.remove_at(r_stack.size() - 1);
	r_state.write[p_index] = 2;
	return false;
}

struct DependencyComponent {
	Vector<int> entries;
	Vector<int> outgoing_components;
	int min_index = 0;
	int indegree = 0;
};

void collect_dependency_finish_order_dfs(int p_index,
		const Vector<Vector<int>> &p_dependency_edges,
		const Vector<uint8_t> &p_consider,
		Vector<uint8_t> &r_visited,
		Vector<int> &r_finish_order) {
	r_visited.write[p_index] = 1;

	for (const int dependency_index : p_dependency_edges[p_index]) {
		if (p_consider[dependency_index] && !r_visited[dependency_index]) {
			collect_dependency_finish_order_dfs(dependency_index, p_dependency_edges, p_consider, r_visited, r_finish_order);
		}
	}

	r_finish_order.push_back(p_index);
}

void collect_dependency_component_dfs(int p_index,
		int p_component_index,
		const Vector<Vector<int>> &p_outgoing_edges,
		const Vector<uint8_t> &p_consider,
		Vector<int> &r_component_indices,
		Vector<int> &r_component_entries) {
	r_component_indices.write[p_index] = p_component_index;
	r_component_entries.push_back(p_index);

	for (const int dependent_index : p_outgoing_edges[p_index]) {
		if (p_consider[dependent_index] && r_component_indices[dependent_index] == -1) {
			collect_dependency_component_dfs(dependent_index, p_component_index, p_outgoing_edges, p_consider, r_component_indices, r_component_entries);
		}
	}
}

void append_unprocessed_dependency_order(const Vector<Vector<int>> &p_dependency_edges,
		const Vector<Vector<int>> &p_outgoing_edges,
		const Vector<uint8_t> &p_unprocessed,
		Vector<int> &r_ordered_indices) {
	const int entry_count = p_unprocessed.size();

	Vector<uint8_t> visited;
	visited.resize(entry_count);
	for (int i = 0; i < entry_count; i++) {
		visited.write[i] = 0;
	}

	Vector<int> finish_order;
	for (int i = 0; i < entry_count; i++) {
		if (p_unprocessed[i] && !visited[i]) {
			collect_dependency_finish_order_dfs(i, p_dependency_edges, p_unprocessed, visited, finish_order);
		}
	}

	Vector<int> component_indices;
	component_indices.resize(entry_count);
	for (int i = 0; i < entry_count; i++) {
		component_indices.write[i] = -1;
	}

	Vector<DependencyComponent> components;
	for (int i = finish_order.size() - 1; i >= 0; i--) {
		const int entry_index = finish_order[i];
		if (component_indices[entry_index] != -1) {
			continue;
		}

		DependencyComponent component;
		collect_dependency_component_dfs(entry_index, components.size(), p_outgoing_edges, p_unprocessed, component_indices, component.entries);
		component.entries.sort();
		component.min_index = component.entries[0];
		components.push_back(component);
	}

	for (int entry_index = 0; entry_index < entry_count; entry_index++) {
		if (!p_unprocessed[entry_index]) {
			continue;
		}

		const int entry_component = component_indices[entry_index];
		for (const int dependency_index : p_dependency_edges[entry_index]) {
			if (!p_unprocessed[dependency_index]) {
				continue;
			}

			const int dependency_component = component_indices[dependency_index];
			if (dependency_component == entry_component) {
				continue;
			}

			components.write[dependency_component].outgoing_components.push_back(entry_component);
			components.write[entry_component].indegree++;
		}
	}

	Vector<int> ready_components;
	for (int i = 0; i < components.size(); i++) {
		if (components[i].indegree == 0) {
			ready_components.push_back(i);
		}
	}

	while (!ready_components.is_empty()) {
		int ready_position = 0;
		for (int i = 1; i < ready_components.size(); i++) {
			if (components[ready_components[i]].min_index < components[ready_components[ready_position]].min_index) {
				ready_position = i;
			}
		}

		const int component_index = ready_components[ready_position];
		ready_components.remove_at(ready_position);

		for (const int entry_index : components[component_index].entries) {
			r_ordered_indices.push_back(entry_index);
		}

		for (const int dependent_component : components[component_index].outgoing_components) {
			components.write[dependent_component].indegree--;
			if (components[dependent_component].indegree == 0) {
				ready_components.push_back(dependent_component);
			}
		}
	}
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

bool is_foundry_script_path(const String &p_path, const String &p_resource_type) {
	FSLanguage *language = FSLanguage::get_singleton();
	return p_resource_type == "FoundryScript" || (language != nullptr && p_path.get_extension() == language->get_extension());
}

bool is_scene_path(const String &p_path, const String &p_resource_type) {
	return p_resource_type == "PackedScene" || p_path.get_extension() == "tscn" || p_path.get_extension() == "scn";
}

void populate_foundry_script_metadata(FSAutoloadIndexEntry &r_entry) {
	FSLanguage *language = FSLanguage::get_singleton();
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
	r_entry.is_same_script_global_class = r_entry.global_class_name == r_entry.name && FoundryScript::is_canonically_equal_paths(r_entry.script_path, r_entry.path);

	if (!r_entry.is_node) {
		add_diagnostic(r_entry,
				FSAutoloadIndexDiagnostic::NON_NODE_SCRIPT,
				vformat("Autoload \"%s\" points to script \"%s\", which does not inherit from Node.", String(r_entry.name), r_entry.path));
	}
}

void populate_resource_metadata(FSAutoloadIndexEntry &r_entry) {
	if (!FileAccess::exists(r_entry.path)) {
		add_diagnostic(r_entry,
				FSAutoloadIndexDiagnostic::MISSING_PATH,
				vformat("Autoload \"%s\" points to missing path \"%s\".", String(r_entry.name), r_entry.path));
		return;
	}

	const String resource_type = ResourceLoader::get_resource_type(r_entry.path);
	if (is_foundry_script_path(r_entry.path, resource_type)) {
		populate_foundry_script_metadata(r_entry);
		return;
	}

	if (is_scene_path(r_entry.path, resource_type)) {
		r_entry.is_node = true;
		return;
	}

	add_diagnostic(r_entry,
			FSAutoloadIndexDiagnostic::NON_SCRIPT_NON_SCENE_PATH,
			vformat("Autoload \"%s\" points to \"%s\", which is not a script or scene.", String(r_entry.name), r_entry.path));
}

void populate_name_diagnostics(FSAutoloadIndexEntry &r_entry) {
	if (r_entry.is_singleton && is_reserved_global_name(r_entry.name)) {
		add_diagnostic(r_entry,
				FSAutoloadIndexDiagnostic::RESERVED_GLOBAL_NAME_COLLISION,
				vformat("Autoload \"%s\" collides with a reserved language global.", String(r_entry.name)));
	}

	if (!ScriptServer::is_global_class(r_entry.name)) {
		return;
	}

	const String global_class_path = ScriptServer::get_global_class_path(r_entry.name);
	if (FoundryScript::is_canonically_equal_paths(global_class_path, r_entry.path)) {
		r_entry.is_same_script_global_class = true;
		return;
	}

	add_diagnostic(r_entry,
			FSAutoloadIndexDiagnostic::UNRELATED_GLOBAL_CLASS_COLLISION,
			vformat("Autoload \"%s\" collides with global script class from \"%s\".", String(r_entry.name), global_class_path));
}

} // namespace

void FSAutoloadIndex::clear() {
	entries.clear();
	name_lookup.clear();
	path_lookup.clear();
	global_class_lookup.clear();
}

void FSAutoloadIndex::sort_and_validate_dependencies() {
	clear_dependency_diagnostics(entries);
	sort_entries_by_order(entries);
	rebuild_lookups();

	const int entry_count = entries.size();
	Vector<Vector<int>> outgoing_edges;
	Vector<Vector<int>> dependency_edges;
	Vector<int> indegree;
	outgoing_edges.resize(entry_count);
	dependency_edges.resize(entry_count);
	indegree.resize(entry_count);
	for (int i = 0; i < entry_count; i++) {
		indegree.write[i] = 0;
	}

	for (int entry_index = 0; entry_index < entry_count; entry_index++) {
		FSAutoloadIndexEntry &entry = entries.write[entry_index];
		for (const FSAutoloadIndexDependency &dependency : entry.dependencies) {
			if (!dependency.is_autoload) {
				add_diagnostic(entry,
						FSAutoloadIndexDiagnostic::NON_AUTOLOAD_DEPENDENCY,
						vformat("Autoload \"%s\" depends on \"%s\", but \"%s\" is not an autoload.", String(entry.name), String(dependency.name), String(dependency.name)));
				continue;
			}

			const int *dependency_index = name_lookup.getptr(dependency.name);
			if (dependency_index == nullptr) {
				dependency_index = global_class_lookup.getptr(dependency.name);
			}
			if (dependency_index == nullptr) {
				add_diagnostic(entry,
						FSAutoloadIndexDiagnostic::MISSING_DEPENDENCY,
						vformat("Autoload \"%s\" depends on \"%s\", but \"%s\" is missing from the autoload index.", String(entry.name), String(dependency.name), String(dependency.name)));
				continue;
			}

			dependency_edges.write[entry_index].push_back(*dependency_index);
			outgoing_edges.write[*dependency_index].push_back(entry_index);
			indegree.write[entry_index]++;
		}
	}

	Vector<int> ready;
	for (int i = 0; i < entry_count; i++) {
		if (indegree[i] == 0) {
			ready.push_back(i);
		}
	}

	Vector<int> ordered_indices;
	Vector<uint8_t> processed;
	processed.resize(entry_count);
	for (int i = 0; i < entry_count; i++) {
		processed.write[i] = 0;
	}

	while (!ready.is_empty()) {
		int ready_position = 0;
		for (int i = 1; i < ready.size(); i++) {
			if (ready[i] < ready[ready_position]) {
				ready_position = i;
			}
		}

		const int entry_index = ready[ready_position];
		ready.remove_at(ready_position);
		processed.write[entry_index] = 1;
		ordered_indices.push_back(entry_index);

		for (const int dependent_index : outgoing_edges[entry_index]) {
			indegree.write[dependent_index]--;
			if (indegree[dependent_index] == 0) {
				ready.push_back(dependent_index);
			}
		}
	}

	if (ordered_indices.size() < entry_count) {
		Vector<uint8_t> unprocessed;
		Vector<uint8_t> diagnosed_cycle_entries;
		unprocessed.resize(entry_count);
		diagnosed_cycle_entries.resize(entry_count);
		for (int i = 0; i < entry_count; i++) {
			unprocessed.write[i] = processed[i] ? 0 : 1;
			diagnosed_cycle_entries.write[i] = 0;
		}

		for (int i = 0; i < entry_count; i++) {
			if (unprocessed[i] && !diagnosed_cycle_entries[i]) {
				Vector<int> cycle;
				Vector<int> stack;
				Vector<int> state;
				state.resize(entry_count);
				for (int state_index = 0; state_index < entry_count; state_index++) {
					state.write[state_index] = 0;
				}
				find_dependency_cycle_from_start_dfs(i, i, dependency_edges, unprocessed, state, stack, cycle);
				if (!cycle.is_empty()) {
					const String cycle_path = format_dependency_cycle_path(cycle, entries);
					const String message = vformat("Autoload dependency cycle: %s.", cycle_path);
					for (int cycle_index = 0; cycle_index < cycle.size() - 1; cycle_index++) {
						const int entry_index = cycle[cycle_index];
						if (!diagnosed_cycle_entries[entry_index]) {
							add_diagnostic(entries.write[entry_index], FSAutoloadIndexDiagnostic::CYCLIC_DEPENDENCY, message);
							diagnosed_cycle_entries.write[entry_index] = 1;
						}
					}
				}
			}
		}

		append_unprocessed_dependency_order(dependency_edges, outgoing_edges, unprocessed, ordered_indices);
	}

	Vector<FSAutoloadIndexEntry> ordered_entries;
	for (const int entry_index : ordered_indices) {
		ordered_entries.push_back(entries[entry_index]);
	}
	entries = ordered_entries;
}

void FSAutoloadIndex::rebuild_lookups() {
	name_lookup.clear();
	path_lookup.clear();
	global_class_lookup.clear();

	for (int i = 0; i < entries.size(); i++) {
		const FSAutoloadIndexEntry &entry = entries[i];
		name_lookup[entry.name] = i;
		if (!entry.path.is_empty()) {
			path_lookup[entry.path] = i;
		}
		if (entry.global_class_name != StringName()) {
			global_class_lookup[entry.global_class_name] = i;
		}
	}
}

void FSAutoloadIndex::rebuild_from_project_settings() {
	clear();

	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (project_settings == nullptr) {
		version++;
		return;
	}

	HashMap<StringName, int> settings_order;
	HashMap<StringName, bool> prepended_settings;
	List<PropertyInfo> properties;
	project_settings->get_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		if (!property.name.begins_with("autoload/") && !property.name.begins_with("autoload_prepend/")) {
			continue;
		}
		const StringName autoload_name = property.name.get_slicec('/', 1);
		settings_order[autoload_name] = project_settings->get_order(property.name);
		if (property.name.begins_with("autoload_prepend/")) {
			prepended_settings[autoload_name] = true;
		}
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
		ordered.entry.source = FSAutoloadIndexEntry::SOURCE_PROJECT_SETTINGS;

		const int *setting_order = settings_order.getptr(autoload.name);
		const int order = setting_order != nullptr ? *setting_order : source_index;
		ordered.entry.order = prepended_settings.has(autoload.name) ? -order - 1 : order;

		populate_resource_metadata(ordered.entry);
		populate_name_diagnostics(ordered.entry);

		ordered_entries.push_back(ordered);
		source_index++;
	}

	ordered_entries.sort_custom<OrderedAutoloadEntryComparator>();
	for (const OrderedAutoloadEntry &ordered : ordered_entries) {
		entries.push_back(ordered.entry);
	}

	sort_and_validate_dependencies();
	rebuild_lookups();
	version++;
}

Error FSAutoloadIndex::rebuild_from_cache_and_project_settings(const String &p_cache_path) {
	Vector<FSAutoloadIndexEntry> merged_entries;

	const String cache_path = p_cache_path.is_empty() ? get_cache_path() : p_cache_path;
	if (!cache_path.is_empty() && FileAccess::exists(cache_path)) {
		FSAutoloadIndex cached_index;
		const Error cache_err = cached_index.load_from_cache(cache_path);
		if (cache_err != OK) {
			return cache_err;
		}
		for (const FSAutoloadIndexEntry &entry : cached_index.get_entries()) {
			if (entry.source == FSAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION) {
				merged_entries.push_back(entry);
			}
		}
	}

	FSAutoloadIndex project_settings_index;
	project_settings_index.rebuild_from_project_settings();
	for (const FSAutoloadIndexEntry &entry : project_settings_index.get_entries()) {
		merged_entries.push_back(entry);
	}

	rebuild_from_entries(merged_entries);
	return OK;
}

Error FSAutoloadIndex::rebuild_for_runtime_startup(const String &p_cache_path) {
#ifdef TOOLS_ENABLED
	const Error rebuild_err = rebuild_from_project_settings_and_script_annotations();
	if (rebuild_err != OK) {
		return rebuild_err;
	}

	const String cache_path = p_cache_path.is_empty() ? get_cache_path() : p_cache_path;
	if (!cache_path.is_empty()) {
		const Error cache_err = save_to_cache(cache_path);
		if (cache_err != OK) {
			WARN_PRINT(vformat("Failed to save FoundryScript autoload index cache to \"%s\".", cache_path));
		}
	}
	return OK;
#else
	return rebuild_from_cache_and_project_settings(p_cache_path);
#endif // TOOLS_ENABLED
}

void FSAutoloadIndex::rebuild_from_entries(const Vector<FSAutoloadIndexEntry> &p_entries) {
	clear();
	entries = p_entries;
	normalize_duplicate_entries(entries);
	clear_name_diagnostics(entries);
	for (int i = 0; i < entries.size(); i++) {
		populate_name_diagnostics(entries.write[i]);
	}
	sort_and_validate_dependencies();
	rebuild_lookups();
	version++;
}

String FSAutoloadIndex::get_cache_path() {
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (project_settings == nullptr) {
		return String();
	}
	return project_settings->get_project_data_path().path_join(AUTOLOAD_INDEX_CACHE_FILE);
}

Error FSAutoloadIndex::save_to_cache(const String &p_cache_path) const {
	const String cache_path = p_cache_path.is_empty() ? get_cache_path() : p_cache_path;
	ERR_FAIL_COND_V(cache_path.is_empty(), ERR_FILE_BAD_PATH);

	const Error dir_err = DirAccess::make_dir_recursive_absolute(cache_path.get_base_dir());
	ERR_FAIL_COND_V(dir_err != OK, dir_err);

	Array serialized_entries;
	for (const FSAutoloadIndexEntry &entry : entries) {
		serialized_entries.push_back(entry_to_dictionary(entry));
	}

	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value(AUTOLOAD_INDEX_CACHE_SECTION, AUTOLOAD_INDEX_CACHE_ENTRIES_KEY, serialized_entries);
	return config->save(cache_path);
}

Error FSAutoloadIndex::load_from_cache(const String &p_cache_path) {
	const String cache_path = p_cache_path.is_empty() ? get_cache_path() : p_cache_path;
	ERR_FAIL_COND_V(cache_path.is_empty(), ERR_FILE_BAD_PATH);

	Ref<ConfigFile> config;
	config.instantiate();
	const Error load_err = config->load(cache_path);
	if (load_err != OK) {
		return load_err;
	}

	Vector<FSAutoloadIndexEntry> loaded_entries;
	const Array serialized_entries = config->get_value(AUTOLOAD_INDEX_CACHE_SECTION, AUTOLOAD_INDEX_CACHE_ENTRIES_KEY, Array());
	for (const Variant &entry_variant : serialized_entries) {
		if (entry_variant.get_type() != Variant::DICTIONARY) {
			continue;
		}

		FSAutoloadIndexEntry entry;
		if (entry_from_dictionary(entry_variant, entry)) {
			loaded_entries.push_back(entry);
		}
	}

	rebuild_from_entries(loaded_entries);
	return OK;
}

#ifdef TOOLS_ENABLED
Error FSAutoloadIndex::rebuild_from_project_settings_and_script_annotations() {
	Vector<FSAutoloadIndexEntry> merged_entries;

	FSLanguage *foundry_script = FSLanguage::get_singleton();
	if (foundry_script != nullptr) {
		const StringName fs_language_name = foundry_script->get_name();
		LocalVector<StringName> global_classes;
		ScriptServer::get_global_class_list(global_classes);

		for (const StringName &global_class : global_classes) {
			if (ScriptServer::get_global_class_language(global_class) != fs_language_name) {
				continue;
			}

			const String script_path = ScriptServer::get_global_class_path(global_class);
			const Error collect_err = collect_script_annotation_autoload_entries(script_path, merged_entries);
			if (collect_err != OK) {
				return collect_err;
			}
		}
	}

	FSAutoloadIndex project_settings_index;
	project_settings_index.rebuild_from_project_settings();
	for (const FSAutoloadIndexEntry &entry : project_settings_index.get_entries()) {
		merged_entries.push_back(entry);
	}

	rebuild_from_entries(merged_entries);
	return OK;
}
#endif // TOOLS_ENABLED

bool FSAutoloadIndex::has_autoload(const StringName &p_name) const {
	return name_lookup.has(p_name);
}

const FSAutoloadIndexEntry *FSAutoloadIndex::get_by_name(const StringName &p_name) const {
	const int *index = name_lookup.getptr(p_name);
	return index != nullptr ? &entries[*index] : nullptr;
}

const FSAutoloadIndexEntry *FSAutoloadIndex::get_by_path(const String &p_path) const {
	const int *index = path_lookup.getptr(ResourceUID::ensure_path(p_path));
	return index != nullptr ? &entries[*index] : nullptr;
}

const FSAutoloadIndexEntry *FSAutoloadIndex::get_by_global_class(const StringName &p_global_class_name) const {
	const int *index = global_class_lookup.getptr(p_global_class_name);
	return index != nullptr ? &entries[*index] : nullptr;
}

Vector<ProjectSettings::AutoloadInfo> FSAutoloadIndex::get_startup_autoloads() const {
	Vector<ProjectSettings::AutoloadInfo> autoloads;
	for (const FSAutoloadIndexEntry &entry : entries) {
		if (entry.path.is_empty()) {
			continue;
		}

		ProjectSettings::AutoloadInfo info;
		info.name = entry.name;
		info.path = entry.path;
		info.is_singleton = entry.is_singleton;
		autoloads.push_back(info);
	}
	return autoloads;
}

void FSAutoloadIndex::register_startup_autoloads_in_project_settings() const {
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (project_settings == nullptr) {
		return;
	}

	for (const ProjectSettings::AutoloadInfo &info : get_startup_autoloads()) {
		project_settings->add_autoload(info);
	}
}
