/**************************************************************************/
/*  fs_name_mangler_binding_safety.cpp                                    */
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

#include "fs_name_mangler_binding_safety.h"

#ifdef TOOLS_ENABLED

#include "core/object/class_db.h"
#include "scene/resources/animation.h"
#include "scene/resources/animation_library.h"
#include "scene/resources/packed_scene.h"

namespace {

String get_binding_kind_label(FSNameManglerBindingSafety::BindingKind p_kind) {
	switch (p_kind) {
		case FSNameManglerBindingSafety::BINDING_CONNECTION_SIGNAL:
			return "connection signal";
		case FSNameManglerBindingSafety::BINDING_CONNECTION_METHOD:
			return "connection method";
		case FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY:
			return "serialized property";
		case FSNameManglerBindingSafety::BINDING_ANIMATION_PROPERTY:
			return "animation property";
		case FSNameManglerBindingSafety::BINDING_ANIMATION_METHOD:
			return "animation method";
		case FSNameManglerBindingSafety::BINDING_RESOURCE_SCRIPT_CLASS:
			return "resource script class";
		case FSNameManglerBindingSafety::BINDING_TYPED_CONTAINER_SCRIPT_CLASS:
			return "typed container script class";
	}
	return "unknown binding";
}

struct EvidenceComparator {
	bool operator()(const FSNameManglerBindingSafety::Evidence &p_left,
			const FSNameManglerBindingSafety::Evidence &p_right) const {
		if (p_left.name != p_right.name) {
			return String(p_left.name) < String(p_right.name);
		}
		if (p_left.kind != p_right.kind) {
			return p_left.kind < p_right.kind;
		}
		if (p_left.source != p_right.source) {
			return p_left.source < p_right.source;
		}
		return p_left.owner < p_right.owner;
	}
};

bool evidence_matches(const FSNameManglerBindingSafety::Evidence &p_left,
		const FSNameManglerBindingSafety::Evidence &p_right) {
	return p_left.name == p_right.name && p_left.kind == p_right.kind &&
			p_left.source == p_right.source && p_left.owner == p_right.owner;
}

struct DiagnosticComparator {
	bool operator()(const FSNameManglerBindingSafety::Diagnostic &p_left,
			const FSNameManglerBindingSafety::Diagnostic &p_right) const {
		if (p_left.source != p_right.source) {
			return p_left.source < p_right.source;
		}
		if (p_left.context != p_right.context) {
			return p_left.context < p_right.context;
		}
		return p_left.message < p_right.message;
	}
};

bool diagnostic_matches(const FSNameManglerBindingSafety::Diagnostic &p_left,
		const FSNameManglerBindingSafety::Diagnostic &p_right) {
	return p_left.source == p_right.source && p_left.context == p_right.context &&
			p_left.message == p_right.message;
}

struct ResourceRootComparator {
	bool operator()(const FSNameManglerBindingSafety::ResourceRoot &p_left,
			const FSNameManglerBindingSafety::ResourceRoot &p_right) const {
		return p_left.source < p_right.source;
	}
};

enum DeclarationKind {
	DECLARATION_PROPERTY,
	DECLARATION_METHOD,
	DECLARATION_SIGNAL,
};

struct ScriptDomain {
	HashSet<const FoundryScript *> pointers;
	HashMap<String, const FoundryScript *> identities;
};

String get_script_identity(const FoundryScript *p_script) {
	if (p_script == nullptr) {
		return String();
	}
	return FoundryScript::canonicalize_path(p_script->get_script_path()) + "::" +
			p_script->get_fully_qualified_name();
}

String get_script_owner(const FoundryScript *p_script) {
	if (p_script == nullptr) {
		return String();
	}
	const String path = FoundryScript::canonicalize_path(p_script->get_script_path());
	const String qualified_name = p_script->get_fully_qualified_name();
	return qualified_name.is_empty() ? path : path + "::" + qualified_name;
}

void index_script(
		const FoundryScript *p_script,
		ScriptDomain &r_domain,
		HashSet<const FoundryScript *> &r_visited,
		FSNameManglerBindingSafety::Result &r_result) {
	if (p_script == nullptr || r_visited.has(p_script)) {
		return;
	}
	r_visited.insert(p_script);
	r_domain.pointers.insert(p_script);
	const String identity = get_script_identity(p_script);
	if (!identity.is_empty()) {
		const HashMap<String, const FoundryScript *>::ConstIterator existing =
				r_domain.identities.find(identity);
		if (existing && existing->value != p_script) {
			FSNameManglerBindingSafety::Diagnostic diagnostic;
			diagnostic.source = p_script->get_script_path();
			diagnostic.context = p_script->get_fully_qualified_name();
			diagnostic.message = "duplicate compiled script identity";
			r_result.diagnostics.push_back(diagnostic);
			r_result.error = ERR_INVALID_DATA;
			r_result.complete = false;
		} else {
			r_domain.identities.insert(identity, p_script);
		}
	}
	for (const KeyValue<StringName, Ref<FoundryScript>> &subclass :
			p_script->get_subclasses()) {
		index_script(subclass.value.ptr(), r_domain, r_visited, r_result);
	}
}

const FoundryScript *resolve_domain_script(
		const FoundryScript *p_script, const ScriptDomain &p_domain) {
	if (p_script == nullptr) {
		return nullptr;
	}
	if (p_domain.pointers.has(p_script)) {
		return p_script;
	}
	const HashMap<String, const FoundryScript *>::ConstIterator found =
			p_domain.identities.find(get_script_identity(p_script));
	return found ? found->value : nullptr;
}

bool script_is_in_domain(
		const Ref<Script> &p_script, const ScriptDomain &p_domain) {
	HashSet<const FoundryScript *> visited;
	for (const FoundryScript *current =
					Object::cast_to<FoundryScript>(p_script.ptr());
			current != nullptr && !visited.has(current);
			current = Object::cast_to<FoundryScript>(
					current->get_base_script().ptr())) {
		visited.insert(current);
		if (resolve_domain_script(current, p_domain) != nullptr) {
			return true;
		}
	}
	return false;
}

bool script_declares(
		const FoundryScript *p_script,
		const StringName &p_name,
		DeclarationKind p_kind) {
	switch (p_kind) {
		case DECLARATION_PROPERTY:
			return p_script->get_members().has(p_name);
		case DECLARATION_METHOD:
			return p_script->get_member_functions().has(p_name);
		case DECLARATION_SIGNAL:
			return p_script->get_signals().has(p_name);
	}
	return false;
}

const FoundryScript *find_foundry_declaration(
		const Ref<Script> &p_script,
		const StringName &p_name,
		DeclarationKind p_kind) {
	HashSet<const FoundryScript *> visited;
	for (const FoundryScript *current =
					Object::cast_to<FoundryScript>(p_script.ptr());
			current != nullptr && !visited.has(current);
			current = Object::cast_to<FoundryScript>(
					current->get_base_script().ptr())) {
		visited.insert(current);
		if (script_declares(current, p_name, p_kind)) {
			return current;
		}
	}
	return nullptr;
}

bool script_has_declaration(
		const Ref<Script> &p_script,
		const StringName &p_name,
		DeclarationKind p_kind) {
	switch (p_kind) {
		case DECLARATION_PROPERTY: {
			List<PropertyInfo> properties;
			p_script->get_script_property_list(&properties);
			for (const PropertyInfo &property : properties) {
				if (property.name == p_name) {
					return true;
				}
			}
			return false;
		}
		case DECLARATION_METHOD: {
			List<MethodInfo> methods;
			p_script->get_script_method_list(&methods);
			for (const MethodInfo &method : methods) {
				if (method.name == p_name) {
					return true;
				}
			}
			return false;
		}
		case DECLARATION_SIGNAL:
			return p_script->has_script_signal(p_name);
	}
	return false;
}

struct VirtualProperty {
	Variant value;
	bool deferred_node_path = false;
};

struct VirtualNode {
	String path;
	StringName native_type;
	Ref<Script> script;
	RBMap<StringName, VirtualProperty> properties;
};

struct VirtualConnection {
	String source_path;
	StringName signal;
	String target_path;
	StringName method;
	String source;
};

struct VirtualScene {
	RBMap<String, VirtualNode> nodes;
	Vector<VirtualConnection> connections;
	HashSet<const SceneState *> active_states;
};

void fail_collection(
		FSNameManglerBindingSafety::Result &r_result,
		const String &p_source,
		const String &p_context,
		const String &p_message) {
	FSNameManglerBindingSafety::Diagnostic diagnostic;
	diagnostic.source = p_source;
	diagnostic.context = p_context;
	diagnostic.message = p_message;
	r_result.diagnostics.push_back(diagnostic);
	r_result.error = ERR_INVALID_DATA;
	r_result.complete = false;
}

bool append_scene_path(
		const String &p_mount,
		const NodePath &p_relative,
		const String &p_source,
		const String &p_context,
		FSNameManglerBindingSafety::Result &r_result,
		String &r_path) {
	if (p_relative.is_absolute() || p_relative.get_subname_count() != 0) {
		fail_collection(r_result, p_source, p_context,
				"scene path must be relative and cannot contain subnames");
		return false;
	}

	Vector<StringName> components;
	if (!p_mount.is_empty()) {
		const PackedStringArray mount_components = p_mount.split("/");
		for (const String &component : mount_components) {
			if (!component.is_empty()) {
				components.push_back(component);
			}
		}
	}
	for (int i = 0; i < p_relative.get_name_count(); i++) {
		const StringName component = p_relative.get_name(i);
		if (component == SNAME(".")) {
			continue;
		}
		if (component == SNAME("..") || String(component).is_empty()) {
			fail_collection(r_result, p_source, p_context,
					"scene path cannot escape its mounted scene");
			return false;
		}
		components.push_back(component);
	}

	r_path.clear();
	for (const StringName &component : components) {
		if (!r_path.is_empty()) {
			r_path += "/";
		}
		r_path += String(component);
	}
	return true;
}

void expand_scene_state(
		const Ref<SceneState> &p_state,
		const String &p_mount,
		const String &p_source,
		VirtualScene &r_scene,
		FSNameManglerBindingSafety::Result &r_result) {
	if (p_state.is_null()) {
		fail_collection(r_result, p_source, p_mount, "PackedScene has no SceneState");
		return;
	}
	if (r_scene.active_states.has(p_state.ptr())) {
		fail_collection(r_result, p_source, p_mount,
				"scene inheritance or instance cycle");
		return;
	}
	r_scene.active_states.insert(p_state.ptr());

	const Ref<SceneState> base_state = p_state->get_base_scene_state();
	if (base_state.is_valid()) {
		expand_scene_state(base_state, p_mount, p_source, r_scene, r_result);
	}

	for (int node_index = 0; node_index < p_state->get_node_count(); node_index++) {
		String node_path;
		const String context = vformat("node row %d", node_index);
		if (!append_scene_path(p_mount, p_state->get_node_path(node_index),
					p_source, context, r_result, node_path)) {
			continue;
		}

		const Ref<PackedScene> instance = p_state->get_node_instance(node_index);
		if (node_index > 0 && instance.is_valid()) {
			expand_scene_state(
					instance->get_state(), node_path, p_source, r_scene, r_result);
		} else if (p_state->is_node_instance_placeholder(node_index)) {
			fail_collection(r_result, p_source, node_path,
					"instance placeholder cannot be expanded");
		}

		const StringName node_type = p_state->get_node_type(node_index);
		RBMap<String, VirtualNode>::Element *node_entry =
				r_scene.nodes.find(node_path);
		if (node_entry == nullptr) {
			if (node_type.is_empty()) {
				fail_collection(r_result, p_source, node_path,
						"instantiated node row has no base or instance node");
				continue;
			}
			VirtualNode new_node;
			new_node.path = node_path;
			new_node.native_type = node_type;
			r_scene.nodes.insert(node_path, new_node);
			node_entry = r_scene.nodes.find(node_path);
		} else if (!node_type.is_empty()) {
			fail_collection(r_result, p_source, node_path,
					"concrete node row collides with an existing virtual node");
			continue;
		}
		VirtualNode &node = node_entry->value();

		if (node_index > 0) {
			String parent_path;
			if (append_scene_path(p_mount,
						p_state->get_node_path(node_index, true),
						p_source, context + " parent", r_result,
						parent_path) &&
					!r_scene.nodes.has(parent_path)) {
				fail_collection(r_result, p_source, node_path,
						"node parent is missing from the composed scene");
			}
		}

		for (int property_index = 0;
				property_index < p_state->get_node_property_count(node_index);
				property_index++) {
			const StringName property_name =
					p_state->get_node_property_name(node_index, property_index);
			const Variant property_value =
					p_state->get_node_property_value(node_index, property_index);
			VirtualProperty property;
			property.value = property_value;
			const Vector<String> deferred_properties =
					p_state->get_node_deferred_nodepath_properties(node_index);
			property.deferred_node_path =
					deferred_properties.has(String(property_name));
			node.properties.insert(property_name, property);
			if (property_name == CoreStringName(script)) {
				node.script = property_value.get_type() == Variant::NIL
						? Ref<Script>()
						: Ref<Script>(property_value);
			}
		}
	}

	for (int connection_index = 0;
			connection_index < p_state->get_connection_count();
			connection_index++) {
		VirtualConnection connection;
		connection.source = p_source;
		const String context = vformat("connection %d", connection_index);
		if (!append_scene_path(p_mount,
					p_state->get_connection_source(connection_index),
					p_source, context + " source", r_result,
					connection.source_path) ||
				!append_scene_path(p_mount,
						p_state->get_connection_target(connection_index),
						p_source, context + " target", r_result,
						connection.target_path)) {
			continue;
		}
		connection.signal = p_state->get_connection_signal(connection_index);
		connection.method = p_state->get_connection_method(connection_index);
		r_scene.connections.push_back(connection);
	}

	r_scene.active_states.erase(p_state.ptr());
}

struct NativePropertySurface {
	bool has_property(
			const StringName &p_native_type,
			const StringName &p_property) const {
		if (ClassDB::has_property(p_native_type, p_property)) {
			return true;
		}

		const String property = p_property;
		return property.begins_with("metadata/") &&
				property.trim_prefix("metadata/").is_valid_ascii_identifier();
	}
};

void collect_scene_properties(
		const VirtualScene &p_scene,
		const String &p_source,
		const ScriptDomain &p_domain,
		NativePropertySurface &r_native_properties,
		FSNameManglerBindingSafety::Result &r_result) {
	for (const KeyValue<String, VirtualNode> &node_entry : p_scene.nodes) {
		const VirtualNode &node = node_entry.value;
		for (const KeyValue<StringName, VirtualProperty> &property_entry :
				node.properties) {
			if (property_entry.key == CoreStringName(script)) {
				continue;
			}
			if (ClassDB::is_parent_class(
						node.native_type, SNAME("AnimationMixer")) &&
					String(property_entry.key).begins_with("libraries/")) {
				continue;
			}
			const FoundryScript *declaration = find_foundry_declaration(
					node.script, property_entry.key, DECLARATION_PROPERTY);
			if (declaration != nullptr) {
				const FoundryScript *owner =
						resolve_domain_script(declaration, p_domain);
				if (owner != nullptr) {
					FSNameManglerBindingSafety::Evidence evidence;
					evidence.name = property_entry.key;
					evidence.kind =
							FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY;
					evidence.source = p_source;
					evidence.owner = get_script_owner(owner);
					r_result.evidence.push_back(evidence);
				}
				continue;
			}
			if ((node.script.is_valid() &&
						script_has_declaration(node.script, property_entry.key,
								DECLARATION_PROPERTY)) ||
					r_native_properties.has_property(
							node.native_type, property_entry.key) ||
					!script_is_in_domain(node.script, p_domain)) {
				continue;
			}
			fail_collection(r_result, p_source,
					node.path.is_empty() ? "." : node.path,
					vformat("serialized property \"%s\" has no script or native owner",
							property_entry.key));
		}
	}
}

bool native_has_declaration(
		const StringName &p_native_type,
		const StringName &p_name,
		DeclarationKind p_kind,
		NativePropertySurface &r_native_properties) {
	switch (p_kind) {
		case DECLARATION_PROPERTY:
			return r_native_properties.has_property(
					p_native_type, p_name);
		case DECLARATION_METHOD:
			return ClassDB::has_method(p_native_type, p_name);
		case DECLARATION_SIGNAL:
			return ClassDB::has_signal(p_native_type, p_name);
	}
	return false;
}

void collect_scene_declaration(
		const VirtualNode &p_node,
		const StringName &p_name,
		DeclarationKind p_declaration_kind,
		FSNameManglerBindingSafety::BindingKind p_binding_kind,
		const String &p_source,
		const String &p_context,
		const ScriptDomain &p_domain,
		NativePropertySurface &r_native_properties,
		FSNameManglerBindingSafety::Result &r_result) {
	const FoundryScript *declaration = find_foundry_declaration(
			p_node.script, p_name, p_declaration_kind);
	if (declaration != nullptr) {
		const FoundryScript *owner = resolve_domain_script(declaration, p_domain);
		if (owner != nullptr) {
			FSNameManglerBindingSafety::Evidence evidence;
			evidence.name = p_name;
			evidence.kind = p_binding_kind;
			evidence.source = p_source;
			evidence.owner = get_script_owner(owner);
			r_result.evidence.push_back(evidence);
		}
		return;
	}
	if ((p_node.script.is_valid() &&
				script_has_declaration(
						p_node.script, p_name, p_declaration_kind)) ||
			native_has_declaration(
					p_node.native_type, p_name, p_declaration_kind,
					r_native_properties)) {
		return;
	}
	if (p_declaration_kind == DECLARATION_PROPERTY &&
			!script_is_in_domain(p_node.script, p_domain)) {
		return;
	}
	fail_collection(r_result, p_source, p_context,
			vformat("\"%s\" has no script or native owner", p_name));
}

void collect_scene_connections(
		const VirtualScene &p_scene,
		const ScriptDomain &p_domain,
		NativePropertySurface &r_native_properties,
		FSNameManglerBindingSafety::Result &r_result) {
	for (const VirtualConnection &connection : p_scene.connections) {
		const RBMap<String, VirtualNode>::Element *source_node =
				p_scene.nodes.find(connection.source_path);
		const RBMap<String, VirtualNode>::Element *target_node =
				p_scene.nodes.find(connection.target_path);
		const String context = vformat(
				"connection %s.%s -> %s.%s",
				connection.source_path.is_empty() ? String(".")
												  : connection.source_path,
				connection.signal,
				connection.target_path.is_empty() ? String(".")
												  : connection.target_path,
				connection.method);
		if (source_node == nullptr || target_node == nullptr) {
			fail_collection(r_result, connection.source, context,
					"connection endpoint is missing from the composed scene");
			continue;
		}
		collect_scene_declaration(
				source_node->value(), connection.signal, DECLARATION_SIGNAL,
				FSNameManglerBindingSafety::BINDING_CONNECTION_SIGNAL,
				connection.source, context + " signal", p_domain,
				r_native_properties, r_result);
		collect_scene_declaration(
				target_node->value(), connection.method, DECLARATION_METHOD,
				FSNameManglerBindingSafety::BINDING_CONNECTION_METHOD,
				connection.source, context + " method", p_domain,
				r_native_properties, r_result);
	}
}

struct PropertyInfoComparator {
	bool operator()(const PropertyInfo &p_left, const PropertyInfo &p_right) const {
		return String(p_left.name) < String(p_right.name);
	}
};

struct VariantTraversalState {
	HashSet<const Resource *> resources;
	HashSet<const void *> arrays;
	HashSet<const void *> dictionaries;
	NativePropertySurface native_properties;
};

void collect_variant_bindings(
		const Variant &p_value,
		const String &p_source,
		const String &p_context,
		const ScriptDomain &p_domain,
		VariantTraversalState &r_traversal,
		FSNameManglerBindingSafety::Result &r_result,
		bool p_top_level_resource = false);

void collect_packed_scene_bindings(
		const Ref<PackedScene> &p_scene,
		const String &p_source,
		const String &p_context,
		const ScriptDomain &p_domain,
		VariantTraversalState &r_traversal,
		FSNameManglerBindingSafety::Result &r_result);

void collect_script_class(
		const Variant &p_script_value,
		FSNameManglerBindingSafety::BindingKind p_kind,
		const String &p_source,
		const ScriptDomain &p_domain,
		FSNameManglerBindingSafety::Result &r_result) {
	const Ref<Script> script = p_script_value;
	const FoundryScript *foundry_script =
			Object::cast_to<FoundryScript>(script.ptr());
	const FoundryScript *owner =
			resolve_domain_script(foundry_script, p_domain);
	if (owner == nullptr || owner->get_local_name().is_empty()) {
		return;
	}
	FSNameManglerBindingSafety::Evidence evidence;
	evidence.name = owner->get_local_name();
	evidence.kind = p_kind;
	evidence.source = p_source;
	evidence.owner = get_script_owner(owner);
	r_result.evidence.push_back(evidence);
}

void collect_array_bindings(
		const Array &p_array,
		const String &p_source,
		const String &p_context,
		const ScriptDomain &p_domain,
		VariantTraversalState &r_traversal,
		FSNameManglerBindingSafety::Result &r_result) {
	if (p_array.is_typed()) {
		collect_script_class(
				p_array.get_typed_script(),
				FSNameManglerBindingSafety::BINDING_TYPED_CONTAINER_SCRIPT_CLASS,
				p_source, p_domain, r_result);
	}
	if (r_traversal.arrays.has(p_array.id())) {
		return;
	}
	r_traversal.arrays.insert(p_array.id());
	for (int i = 0; i < p_array.size(); i++) {
		collect_variant_bindings(
				p_array[i], p_source, vformat("%s[%d]", p_context, i),
				p_domain, r_traversal, r_result);
	}
}

void collect_dictionary_bindings(
		const Dictionary &p_dictionary,
		const String &p_source,
		const String &p_context,
		const ScriptDomain &p_domain,
		VariantTraversalState &r_traversal,
		FSNameManglerBindingSafety::Result &r_result) {
	if (p_dictionary.is_typed_key()) {
		collect_script_class(
				p_dictionary.get_typed_key_script(),
				FSNameManglerBindingSafety::BINDING_TYPED_CONTAINER_SCRIPT_CLASS,
				p_source, p_domain, r_result);
	}
	if (p_dictionary.is_typed_value()) {
		collect_script_class(
				p_dictionary.get_typed_value_script(),
				FSNameManglerBindingSafety::BINDING_TYPED_CONTAINER_SCRIPT_CLASS,
				p_source, p_domain, r_result);
	}
	if (r_traversal.dictionaries.has(p_dictionary.id())) {
		return;
	}
	r_traversal.dictionaries.insert(p_dictionary.id());
	for (const KeyValue<Variant, Variant> &entry : p_dictionary) {
		collect_variant_bindings(
				entry.key, p_source, p_context + " key",
				p_domain, r_traversal, r_result);
		collect_variant_bindings(
				entry.value, p_source, p_context + " value",
				p_domain, r_traversal, r_result);
	}
}

void collect_resource_bindings(
		const Ref<Resource> &p_resource,
		const String &p_source,
		const String &p_context,
		const ScriptDomain &p_domain,
		VariantTraversalState &r_traversal,
		FSNameManglerBindingSafety::Result &r_result,
		bool p_top_level) {
	if (p_resource.is_null() ||
			r_traversal.resources.has(p_resource.ptr())) {
		return;
	}
	r_traversal.resources.insert(p_resource.ptr());

	const Ref<Script> script = p_resource->get_script();
	const FoundryScript *foundry_script =
			Object::cast_to<FoundryScript>(script.ptr());
	if (p_top_level && foundry_script != nullptr &&
			!foundry_script->get_global_name().is_empty()) {
		const FoundryScript *owner =
				resolve_domain_script(foundry_script, p_domain);
		if (owner != nullptr && !owner->get_local_name().is_empty()) {
			FSNameManglerBindingSafety::Evidence evidence;
			evidence.name = owner->get_local_name();
			evidence.kind =
					FSNameManglerBindingSafety::BINDING_RESOURCE_SCRIPT_CLASS;
			evidence.source = p_source;
			evidence.owner = get_script_owner(owner);
			r_result.evidence.push_back(evidence);
		}
	}

	List<PropertyInfo> property_list;
	p_resource->get_property_list(&property_list);
	Vector<PropertyInfo> properties;
	for (const PropertyInfo &property : property_list) {
		if (property.usage & PROPERTY_USAGE_STORAGE) {
			properties.push_back(property);
		}
	}
	properties.sort_custom<PropertyInfoComparator>();

	for (const PropertyInfo &property : properties) {
		if (property.name == CoreStringName(script)) {
			continue;
		}
		const FoundryScript *declaration = find_foundry_declaration(
				script, property.name, DECLARATION_PROPERTY);
		const FoundryScript *owner =
				resolve_domain_script(declaration, p_domain);
		if (owner != nullptr) {
			FSNameManglerBindingSafety::Evidence evidence;
			evidence.name = property.name;
			evidence.kind =
					FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY;
			evidence.source = p_source;
			evidence.owner = get_script_owner(owner);
			r_result.evidence.push_back(evidence);
		}

		bool valid = false;
		const Variant value = p_resource->get(property.name, &valid);
		if (!valid) {
			fail_collection(
					r_result, p_source,
					p_context + "." + String(property.name),
					"storage property could not be read");
			continue;
		}
		collect_variant_bindings(
				value, p_source,
				p_context + "." + String(property.name),
				p_domain, r_traversal, r_result);
	}
}

void collect_variant_bindings(
		const Variant &p_value,
		const String &p_source,
		const String &p_context,
		const ScriptDomain &p_domain,
		VariantTraversalState &r_traversal,
		FSNameManglerBindingSafety::Result &r_result,
		bool p_top_level_resource) {
	switch (p_value.get_type()) {
		case Variant::OBJECT: {
			const Ref<Resource> resource = p_value;
			const Ref<PackedScene> scene = resource;
			if (scene.is_valid()) {
				collect_packed_scene_bindings(
						scene, p_source, p_context, p_domain,
						r_traversal, r_result);
			} else if (resource.is_valid()) {
				collect_resource_bindings(
						resource, p_source, p_context, p_domain,
						r_traversal, r_result, p_top_level_resource);
			}
		} break;
		case Variant::ARRAY:
			collect_array_bindings(
					p_value, p_source, p_context, p_domain,
					r_traversal, r_result);
			break;
		case Variant::DICTIONARY:
			collect_dictionary_bindings(
					p_value, p_source, p_context, p_domain,
					r_traversal, r_result);
			break;
		default:
			break;
	}
}

void collect_scene_property_values(
		const VirtualScene &p_scene,
		const String &p_source,
		const ScriptDomain &p_domain,
		VariantTraversalState &r_traversal,
		FSNameManglerBindingSafety::Result &r_result) {
	for (const KeyValue<String, VirtualNode> &node_entry : p_scene.nodes) {
		for (const KeyValue<StringName, VirtualProperty> &property :
				node_entry.value.properties) {
			if (property.key == CoreStringName(script)) {
				continue;
			}
			collect_variant_bindings(
					property.value.value, p_source,
					vformat("node %s.%s",
							node_entry.key.is_empty() ? String(".")
													  : node_entry.key,
							property.key),
					p_domain, r_traversal, r_result);
		}
	}
}

struct StringNameComparator {
	bool operator()(const StringName &p_left, const StringName &p_right) const {
		return String(p_left) < String(p_right);
	}
};

bool resolve_relative_node_path(
		const String &p_base,
		const NodePath &p_relative,
		const String &p_source,
		const String &p_context,
		FSNameManglerBindingSafety::Result &r_result,
		String &r_path) {
	if (p_relative.is_absolute()) {
		fail_collection(r_result, p_source, p_context,
				"animation path must be scene-relative");
		return false;
	}
	Vector<String> components;
	if (!p_base.is_empty()) {
		const PackedStringArray base_components = p_base.split("/");
		for (const String &component : base_components) {
			if (!component.is_empty()) {
				components.push_back(component);
			}
		}
	}
	for (int i = 0; i < p_relative.get_name_count(); i++) {
		const String component = p_relative.get_name(i);
		if (component == ".") {
			continue;
		}
		if (component == "..") {
			if (components.is_empty()) {
				fail_collection(r_result, p_source, p_context,
						"animation path escapes the composed scene");
				return false;
			}
			components.remove_at(components.size() - 1);
			continue;
		}
		if (component.is_empty()) {
			fail_collection(r_result, p_source, p_context,
					"animation path contains an empty node component");
			return false;
		}
		components.push_back(component);
	}
	r_path.clear();
	for (const String &component : components) {
		if (!r_path.is_empty()) {
			r_path += "/";
		}
		r_path += component;
	}
	return true;
}

bool resource_has_property(
		const Ref<Resource> &p_resource, const StringName &p_name) {
	List<PropertyInfo> properties;
	p_resource->get_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		if (property.name == p_name) {
			return true;
		}
	}
	return false;
}

void collect_animation_resource_declaration(
		const Ref<Resource> &p_resource,
		const StringName &p_name,
		const String &p_source,
		const String &p_context,
		const ScriptDomain &p_domain,
		FSNameManglerBindingSafety::Result &r_result) {
	const Ref<Script> script = p_resource->get_script();
	const FoundryScript *declaration = find_foundry_declaration(
			script, p_name, DECLARATION_PROPERTY);
	if (declaration != nullptr) {
		const FoundryScript *owner =
				resolve_domain_script(declaration, p_domain);
		if (owner != nullptr) {
			FSNameManglerBindingSafety::Evidence evidence;
			evidence.name = p_name;
			evidence.kind =
					FSNameManglerBindingSafety::BINDING_ANIMATION_PROPERTY;
			evidence.source = p_source;
			evidence.owner = get_script_owner(owner);
			r_result.evidence.push_back(evidence);
		}
		return;
	}
	if ((script.is_valid() &&
				script_has_declaration(
						script, p_name, DECLARATION_PROPERTY)) ||
			ClassDB::has_property(p_resource->get_class_name(), p_name) ||
			resource_has_property(p_resource, p_name)) {
		return;
	}
	fail_collection(r_result, p_source, p_context,
			vformat("\"%s\" has no script or native Resource owner", p_name));
}

void collect_animation_property_path(
		const VirtualNode &p_target_node,
		const NodePath &p_track_path,
		const String &p_source,
		const String &p_context,
		const ScriptDomain &p_domain,
		NativePropertySurface &r_native_properties,
		FSNameManglerBindingSafety::Result &r_result) {
	const int subname_count = p_track_path.get_subname_count();
	if (subname_count == 0) {
		fail_collection(r_result, p_source, p_context,
				"property track has no property subname");
		return;
	}
	const StringName node_property = p_track_path.get_subname(0);
	collect_scene_declaration(
			p_target_node, node_property, DECLARATION_PROPERTY,
			FSNameManglerBindingSafety::BINDING_ANIMATION_PROPERTY,
			p_source, p_context, p_domain, r_native_properties, r_result);
	if (subname_count == 1) {
		return;
	}

	const RBMap<StringName, VirtualProperty>::Element *stored_property =
			p_target_node.properties.find(node_property);
	if (stored_property == nullptr) {
		fail_collection(r_result, p_source, p_context,
				vformat("resource-valued property \"%s\" is not serialized",
						node_property));
		return;
	}
	Ref<Resource> current_resource = stored_property->value().value;
	if (current_resource.is_null()) {
		fail_collection(r_result, p_source, p_context,
				vformat("property \"%s\" is not a Resource", node_property));
		return;
	}
	for (int subname_index = 1;
			subname_index < subname_count; subname_index++) {
		const StringName property_name =
				p_track_path.get_subname(subname_index);
		collect_animation_resource_declaration(
				current_resource, property_name, p_source, p_context,
				p_domain, r_result);
		if (subname_index == subname_count - 1) {
			return;
		}
		bool valid = false;
		const Variant next_value =
				current_resource->get(property_name, &valid);
		if (!valid) {
			fail_collection(r_result, p_source, p_context,
					vformat("resource property \"%s\" could not be read",
							property_name));
			return;
		}
		current_resource = next_value;
		if (current_resource.is_null()) {
			fail_collection(r_result, p_source, p_context,
					vformat("resource property \"%s\" is not a Resource",
							property_name));
			return;
		}
	}
}

void collect_animation(
		const Ref<Animation> &p_animation,
		const String &p_mixer_root,
		const String &p_source,
		const String &p_context,
		const VirtualScene &p_scene,
		const ScriptDomain &p_domain,
		NativePropertySurface &r_native_properties,
		FSNameManglerBindingSafety::Result &r_result) {
	if (p_animation.is_null()) {
		fail_collection(r_result, p_source, p_context,
				"animation is null");
		return;
	}
	for (int track_index = 0;
			track_index < p_animation->get_track_count(); track_index++) {
		const Animation::TrackType track_type =
				p_animation->track_get_type(track_index);
		if (track_type != Animation::TYPE_VALUE &&
				track_type != Animation::TYPE_BEZIER &&
				track_type != Animation::TYPE_METHOD) {
			continue;
		}
		const NodePath track_path =
				p_animation->track_get_path(track_index);
		const String track_context =
				vformat("%s track %d", p_context, track_index);
		String target_path;
		if (!resolve_relative_node_path(
					p_mixer_root, track_path, p_source, track_context,
					r_result, target_path)) {
			continue;
		}
		const RBMap<String, VirtualNode>::Element *target_node =
				p_scene.nodes.find(target_path);
		if (target_node == nullptr) {
			fail_collection(r_result, p_source, track_context,
					vformat("animation target \"%s\" is missing",
							target_path.is_empty() ? String(".") : target_path));
			continue;
		}
		if (track_type == Animation::TYPE_VALUE ||
				track_type == Animation::TYPE_BEZIER) {
			collect_animation_property_path(
					target_node->value(), track_path, p_source,
					track_context, p_domain, r_native_properties, r_result);
			continue;
		}
		if (track_path.get_subname_count() != 0) {
			fail_collection(r_result, p_source, track_context,
					"method track target cannot contain property subnames");
			continue;
		}
		for (int key_index = 0;
				key_index < p_animation->track_get_key_count(track_index);
				key_index++) {
			collect_scene_declaration(
					target_node->value(),
					p_animation->method_track_get_name(
							track_index, key_index),
					DECLARATION_METHOD,
					FSNameManglerBindingSafety::BINDING_ANIMATION_METHOD,
					p_source, vformat("%s key %d", track_context, key_index),
					p_domain, r_native_properties, r_result);
		}
	}
}

void collect_scene_animations(
		const VirtualScene &p_scene,
		const String &p_source,
		const ScriptDomain &p_domain,
		NativePropertySurface &r_native_properties,
		FSNameManglerBindingSafety::Result &r_result) {
	for (const KeyValue<String, VirtualNode> &node_entry : p_scene.nodes) {
		const VirtualNode &node = node_entry.value;
		if (!ClassDB::is_parent_class(
					node.native_type, SNAME("AnimationMixer"))) {
			continue;
		}
		NodePath root_node_path("..");
		const RBMap<StringName, VirtualProperty>::Element *root_property =
				node.properties.find(SNAME("root_node"));
		if (root_property != nullptr) {
			if (root_property->value().value.get_type() !=
					Variant::NODE_PATH) {
				fail_collection(r_result, p_source, node.path,
						"AnimationMixer root_node is not a NodePath");
				continue;
			}
			root_node_path = root_property->value().value;
		}
		String mixer_root;
		if (!resolve_relative_node_path(
					node.path, root_node_path, p_source,
					node.path + ".root_node", r_result, mixer_root)) {
			continue;
		}
		if (!p_scene.nodes.has(mixer_root)) {
			fail_collection(r_result, p_source, node.path,
					"AnimationMixer root_node is missing from the composed scene");
			continue;
		}

		for (const KeyValue<StringName, VirtualProperty> &property :
				node.properties) {
			const String property_name = property.key;
			if (!property_name.begins_with("libraries/")) {
				continue;
			}
			const Ref<AnimationLibrary> library = property.value.value;
			if (library.is_null()) {
				fail_collection(r_result, p_source,
						node.path + "." + property_name,
						"AnimationMixer library property is not an AnimationLibrary");
				continue;
			}
			List<StringName> animation_list;
			library->get_animation_list(&animation_list);
			Vector<StringName> animation_names;
			for (const StringName &animation_name : animation_list) {
				animation_names.push_back(animation_name);
			}
			animation_names.sort_custom<StringNameComparator>();
			for (const StringName &animation_name : animation_names) {
				collect_animation(
						library->get_animation(animation_name), mixer_root,
						p_source,
						vformat("%s.%s animation %s", node.path,
								property_name, animation_name),
						p_scene, p_domain, r_native_properties, r_result);
			}
		}
	}
}

void collect_packed_scene_bindings(
		const Ref<PackedScene> &p_scene,
		const String &p_source,
		const String &p_context,
		const ScriptDomain &p_domain,
		VariantTraversalState &r_traversal,
		FSNameManglerBindingSafety::Result &r_result) {
	if (p_scene.is_null() ||
			r_traversal.resources.has(p_scene.ptr())) {
		return;
	}
	r_traversal.resources.insert(p_scene.ptr());

	const Ref<SceneState> state = p_scene->get_state();
	if (state.is_null()) {
		fail_collection(
				r_result, p_source, p_context,
				"PackedScene has no SceneState");
		return;
	}
	VirtualScene virtual_scene;
	expand_scene_state(
			state, String(), p_source, virtual_scene, r_result);
	collect_scene_properties(
			virtual_scene, p_source, p_domain,
			r_traversal.native_properties, r_result);
	collect_scene_connections(
			virtual_scene, p_domain,
			r_traversal.native_properties, r_result);
	collect_scene_animations(
			virtual_scene, p_source, p_domain,
			r_traversal.native_properties, r_result);
	collect_scene_property_values(
			virtual_scene, p_source, p_domain, r_traversal, r_result);
}

void sort_and_deduplicate(
		FSNameManglerBindingSafety::Result &r_result) {
	r_result.evidence.sort_custom<EvidenceComparator>();
	for (int i = r_result.evidence.size() - 1; i > 0; i--) {
		if (evidence_matches(r_result.evidence[i - 1],
					r_result.evidence[i])) {
			r_result.evidence.remove_at(i);
		}
	}
	r_result.diagnostics.sort_custom<DiagnosticComparator>();
	for (int i = r_result.diagnostics.size() - 1; i > 0; i--) {
		if (diagnostic_matches(r_result.diagnostics[i - 1],
					r_result.diagnostics[i])) {
			r_result.diagnostics.remove_at(i);
		}
	}
}

} // namespace

void FSNameManglerBindingSafety::Input::add_resource(
		const Ref<Resource> &p_resource, const String &p_source) {
	ResourceRoot root;
	root.resource = p_resource;
	root.source = p_source.is_empty() && p_resource.is_valid() ? p_resource->get_path() : p_source;
	resources.push_back(root);
}

String FSNameManglerBindingSafety::Evidence::detail() const {
	String result = get_binding_kind_label(kind) + " in " + source;
	if (!owner.is_empty()) {
		result += " owned by " + owner;
	}
	return result;
}

String FSNameManglerBindingSafety::Diagnostic::format() const {
	String result = source;
	if (!context.is_empty()) {
		result += ": " + context;
	}
	if (!message.is_empty()) {
		result += ": " + message;
	}
	return result;
}

Error FSNameManglerBindingSafety::Result::apply_to_input(
		FSNameManglerAnalysis::Input &r_input) const {
	if (error != OK) {
		return error;
	}
	if (!complete) {
		return ERR_INVALID_DATA;
	}

	Vector<Evidence> sorted_evidence = evidence;
	sorted_evidence.sort_custom<EvidenceComparator>();
	for (int i = sorted_evidence.size() - 1; i > 0; i--) {
		if (evidence_matches(sorted_evidence[i - 1], sorted_evidence[i])) {
			sorted_evidence.remove_at(i);
		}
	}
	for (const Evidence &item : sorted_evidence) {
		if (item.name.is_empty() || item.source.is_empty()) {
			return ERR_INVALID_DATA;
		}
	}
	for (const Evidence &item : sorted_evidence) {
		r_input.add_keep(
				item.name, FSNameManglerAnalysis::KEEP_SCENE_OR_RESOURCE, item.detail());
	}
	return OK;
}

FSNameManglerBindingSafety::Result FSNameManglerBindingSafety::collect(
		const Input &p_input, const FSNameManglerAnalysis::Input &p_analysis_input) {
	Result result;
	ScriptDomain domain;
	HashSet<const FoundryScript *> visited_scripts;
	for (const Ref<FoundryScript> &script : p_analysis_input.scripts) {
		if (script.is_null()) {
			fail_collection(result, String(), "analysis input",
					"compiled script root is null");
			continue;
		}
		index_script(script.ptr(), domain, visited_scripts, result);
	}

	Vector<ResourceRoot> roots = p_input.resources;
	for (ResourceRoot &root : roots) {
		if (root.source.is_empty() && root.resource.is_valid()) {
			root.source = root.resource->get_path();
		}
	}
	roots.sort_custom<ResourceRootComparator>();
	String previous_source;
	for (const ResourceRoot &root : roots) {
		if (root.resource.is_null()) {
			fail_collection(result, root.source, "resource root",
					"resource is null");
			continue;
		}
		if (root.source.is_empty()) {
			fail_collection(result, String(), "resource root",
					"in-memory resource requires a stable source");
			continue;
		}
		if (root.source == previous_source) {
			fail_collection(result, root.source, "resource root",
					"duplicate resource source");
			continue;
		}
		previous_source = root.source;

		VariantTraversalState traversal;
		collect_variant_bindings(
				root.resource, root.source, "resource root", domain,
				traversal, result, true);
	}
	sort_and_deduplicate(result);
	return result;
}

#endif // TOOLS_ENABLED
