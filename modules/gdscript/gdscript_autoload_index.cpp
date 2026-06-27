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

void sort_entries_by_order(Vector<GDScriptAutoloadIndexEntry> &r_entries) {
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

void add_diagnostic(GDScriptAutoloadIndexEntry &r_entry, GDScriptAutoloadIndexDiagnostic::Code p_code, const String &p_message) {
	GDScriptAutoloadIndexDiagnostic diagnostic;
	diagnostic.code = p_code;
	diagnostic.message = p_message;
	r_entry.diagnostics.push_back(diagnostic);
}

String format_dependency_cycle_path(const Vector<int> &p_cycle, const Vector<GDScriptAutoloadIndexEntry> &p_entries) {
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

void GDScriptAutoloadIndex::sort_and_validate_dependencies() {
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
		GDScriptAutoloadIndexEntry &entry = entries.write[entry_index];
		for (const GDScriptAutoloadIndexDependency &dependency : entry.dependencies) {
			if (!dependency.is_autoload) {
				add_diagnostic(entry,
						GDScriptAutoloadIndexDiagnostic::NON_AUTOLOAD_DEPENDENCY,
						vformat("Autoload \"%s\" depends on \"%s\", but \"%s\" is not an autoload.", String(entry.name), String(dependency.name), String(dependency.name)));
				continue;
			}

			const int *dependency_index = name_lookup.getptr(dependency.name);
			if (dependency_index == nullptr) {
				add_diagnostic(entry,
						GDScriptAutoloadIndexDiagnostic::MISSING_DEPENDENCY,
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
							add_diagnostic(entries.write[entry_index], GDScriptAutoloadIndexDiagnostic::CYCLIC_DEPENDENCY, message);
							diagnosed_cycle_entries.write[entry_index] = 1;
						}
					}
				}
			}
		}

		append_unprocessed_dependency_order(dependency_edges, outgoing_edges, unprocessed, ordered_indices);
	}

	Vector<GDScriptAutoloadIndexEntry> ordered_entries;
	for (const int entry_index : ordered_indices) {
		ordered_entries.push_back(entries[entry_index]);
	}
	entries = ordered_entries;
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

	sort_and_validate_dependencies();
	rebuild_lookups();
	version++;
}

void GDScriptAutoloadIndex::rebuild_from_entries(const Vector<GDScriptAutoloadIndexEntry> &p_entries) {
	clear();
	entries = p_entries;
	sort_and_validate_dependencies();
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
