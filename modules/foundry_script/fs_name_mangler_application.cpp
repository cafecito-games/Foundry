/**************************************************************************/
/*  fs_name_mangler_application.cpp                                       */
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

#include "fs_name_mangler_application.h"

#include "fs_conformance_registry.h"
#include "fs_utility_functions.h"

#include "core/config/engine.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/string/char_utils.h"

#ifdef TOOLS_ENABLED

namespace {

struct IdentityReplacement {
	String source;
	String replacement;
};

struct IdentityReplacementComparator {
	bool operator()(const IdentityReplacement &p_left, const IdentityReplacement &p_right) const {
		if (p_left.source.length() != p_right.source.length()) {
			return p_left.source.length() > p_right.source.length();
		}
		return p_left.source < p_right.source;
	}
};

struct ScriptRefComparator {
	bool operator()(const Ref<FoundryScript> &p_left, const Ref<FoundryScript> &p_right) const {
		const String left_key = p_left->get_script_path() + "::" + p_left->get_fully_qualified_name();
		const String right_key = p_right->get_script_path() + "::" + p_right->get_fully_qualified_name();
		return left_key < right_key;
	}
};

} // namespace

struct FSNameManglerApplication::Transaction::Data {
	struct ClassSnapshot {
		Ref<FoundryScript> script;
		FoundryScript *parent = nullptr;

		StringName trait_type_name;
		StringName local_name;
		StringName global_name;
		String fully_qualified_name;

		HashMap<StringName, FoundryScript::MemberInfo> member_indices;
		HashSet<StringName> members;
		Vector<FoundryScript::TypeArgumentBinding> member_type_argument_bindings;
		HashMap<FoundryScript *, Vector<FoundryScript::TypeArgumentBinding>> type_parameter_bindings_by_ancestor;
		HashMap<StringName, FoundryScript::MemberInfo> static_variables_indices;
		HashMap<StringName, Variant> constants;
		HashMap<StringName, FSFunction *> member_functions;
		HashMap<StringName, FoundryScript::EnumFunctionSet> enum_functions;
		HashMap<StringName, Ref<FoundryScript>> subclasses;
		HashMap<StringName, MethodInfo> signals;
		Vector<StringName> script_trait_list;
		HashMap<StringName, FoundryScript::AbstractTraitRequirement> abstract_trait_requirements;
		Vector<FoundryScript::TypeParameter> type_parameters;
		HashMap<StringName, Vector<FoundryScript::AnnotationUsage>> method_annotations;
		HashMap<StringName, Vector<FoundryScript::AnnotationUsage>> variable_annotations;
		HashMap<StringName, Vector<FoundryScript::AnnotationUsage>> signal_annotations;
		HashMap<StringName, Vector<FoundryScript::AnnotationUsage>> constant_annotations;
		HashMap<StringName, HashMap<StringName, Vector<FoundryScript::AnnotationUsage>>> method_parameter_annotations;
		HashMap<StringName, HashMap<StringName, Vector<FoundryScript::AnnotationUsage>>> signal_parameter_annotations;

		HashMap<StringName, FoundryScript::MemberInfo> old_static_variables_indices;
		HashMap<StringName, int> member_lines;
		HashMap<StringName, Variant> member_default_values;
		List<PropertyInfo> members_cache;
		HashMap<StringName, Variant> member_default_values_cache;

		StringName transformed_trait_type_name;
		StringName transformed_local_name;
		StringName transformed_global_name;
		String transformed_fully_qualified_name;
	};

	struct FunctionSnapshot {
		FSFunction *function = nullptr;
		StringName owner_name;
		bool lambda = false;
		bool safe_arguments = false;

		StringName name;
		Vector<FSDataType> argument_types;
		FSDataType return_type;
		MethodInfo method_info;
		Vector<StringName> global_names;
		HashMap<StringName, StringName> parameter_names;
	};

	struct RegistrySnapshot {
		String source;
		Vector<FSConformanceRegistry::Conformance> conformances;
		Vector<FSConformanceRegistry::Conformance> transformed_conformances;
		Vector<FSConformanceRegistry::RuntimeConformance> entries;
		Vector<FSConformanceRegistry::RuntimeConformance> transformed_entries;
	};

	RBMap<StringName, StringName> rename_map;
	Vector<ClassSnapshot> class_snapshots;
	Vector<FunctionSnapshot> function_snapshots;
	Vector<RegistrySnapshot> registry_snapshots;
	HashMap<const FoundryScript *, int> class_snapshot_indices;
	HashSet<const FoundryScript *> indexed_classes;
	HashSet<FSFunction *> indexed_functions;
	HashSet<StringName> project_sources;
	HashSet<StringName> observed_names;
	HashMap<StringName, String> global_protected_names;
	HashMap<String, String> global_protected_paths;
	bool unscannable_protected_surface = false;
	HashSet<const void *> observed_variant_containers;
	HashSet<const void *> validated_variant_containers;
	Vector<IdentityReplacement> identity_replacements;
	HashSet<String> included_identity_sources;
	Vector<String> script_paths;

	void clear() {
		rename_map.clear();
		class_snapshots.clear();
		function_snapshots.clear();
		registry_snapshots.clear();
		class_snapshot_indices.clear();
		indexed_classes.clear();
		indexed_functions.clear();
		project_sources.clear();
		observed_names.clear();
		global_protected_names.clear();
		global_protected_paths.clear();
		unscannable_protected_surface = false;
		observed_variant_containers.clear();
		validated_variant_containers.clear();
		identity_replacements.clear();
		included_identity_sources.clear();
		script_paths.clear();
	}

	StringName rename_atomic(const StringName &p_name) const {
		if (p_name == StringName() || String(p_name).begins_with("@")) {
			return p_name;
		}
		const RBMap<StringName, StringName>::Element *entry = rename_map.find(p_name);
		return entry != nullptr ? entry->value() : p_name;
	}

	String rename_atomic_string(const String &p_name) const {
		return String(rename_atomic(StringName(p_name)));
	}

	static String replace_terminal_component(const String &p_identity,
			const StringName &p_original, const StringName &p_transformed) {
		if (p_original == StringName() || p_original == p_transformed) {
			return p_identity;
		}
		const String original = p_original;
		if (!p_identity.ends_with(original)) {
			return p_identity;
		}
		const int start = p_identity.length() - original.length();
		if (start > 0 && is_unicode_identifier_continue(p_identity[start - 1])) {
			return p_identity;
		}
		return p_identity.substr(0, start) + String(p_transformed);
	}

	bool build_identity_plan(Vector<Diagnostic> &r_diagnostics) {
		identity_replacements.clear();
		included_identity_sources.clear();
		HashMap<String, String> unique_replacements;

		for (ClassSnapshot &snapshot : class_snapshots) {
			snapshot.transformed_local_name = rename_atomic(snapshot.local_name);
			snapshot.transformed_global_name = StringName(replace_terminal_component(
					String(snapshot.global_name), snapshot.local_name, snapshot.transformed_local_name));

			if (snapshot.parent != nullptr) {
				const int *parent_index = class_snapshot_indices.getptr(snapshot.parent);
				CRASH_COND(parent_index == nullptr);
				snapshot.transformed_fully_qualified_name =
						class_snapshots[*parent_index].transformed_fully_qualified_name +
						"::" + String(snapshot.transformed_local_name);
			} else if (snapshot.global_name != StringName() &&
					snapshot.fully_qualified_name == String(snapshot.global_name)) {
				snapshot.transformed_fully_qualified_name = snapshot.transformed_global_name;
			} else {
				snapshot.transformed_fully_qualified_name = replace_terminal_component(
						snapshot.fully_qualified_name, snapshot.local_name, snapshot.transformed_local_name);
			}

			if (snapshot.trait_type_name == snapshot.local_name) {
				snapshot.transformed_trait_type_name = snapshot.transformed_local_name;
			} else if (snapshot.trait_type_name == snapshot.global_name) {
				snapshot.transformed_trait_type_name = snapshot.transformed_global_name;
			} else if (String(snapshot.trait_type_name) == snapshot.fully_qualified_name) {
				snapshot.transformed_trait_type_name =
						StringName(snapshot.transformed_fully_qualified_name);
			} else {
				snapshot.transformed_trait_type_name = StringName(replace_terminal_component(
						String(snapshot.trait_type_name), snapshot.local_name, snapshot.transformed_local_name));
			}

			const auto add_replacement = [&](const String &p_source, const String &p_replacement) {
				if (p_source.is_empty()) {
					return;
				}
				included_identity_sources.insert(p_source);
				const String *existing = unique_replacements.getptr(p_source);
				if (existing == nullptr) {
					unique_replacements.insert(p_source, p_replacement);
				} else if (*existing != p_replacement) {
					add_map_diagnostic(r_diagnostics,
							"class identity collision", snapshot.local_name,
							vformat(
									"Identity `%s` would have conflicting replacements `%s` and `%s`.",
									p_source, *existing, p_replacement));
				}
			};
			add_replacement(String(snapshot.local_name), String(snapshot.transformed_local_name));
			add_replacement(String(snapshot.global_name), String(snapshot.transformed_global_name));
			add_replacement(snapshot.fully_qualified_name, snapshot.transformed_fully_qualified_name);
			add_replacement(String(snapshot.trait_type_name), String(snapshot.transformed_trait_type_name));
			const String script_prefix =
					snapshot.script->get_script_path() + "::";
			String relative_identity;
			String transformed_relative_identity;
			if (!snapshot.script->get_script_path().is_empty() &&
					snapshot.fully_qualified_name.begins_with(script_prefix) &&
					snapshot.transformed_fully_qualified_name.begins_with(
							script_prefix)) {
				relative_identity = snapshot.fully_qualified_name.substr(
						script_prefix.length());
				transformed_relative_identity =
						snapshot.transformed_fully_qualified_name.substr(
								script_prefix.length());
				add_replacement(
						relative_identity,
						transformed_relative_identity);
			}

			// Enum value dictionaries are stored as constants, while enum type metadata uses a
			// structural `Owner.Enum` identity. Atomic replacements deliberately do not fire next
			// to `.`/`::`, because doing so would confuse namespace components with declarations.
			// Register the exact owner/enum aliases the compiled class graph proves instead.
			for (const KeyValue<StringName, Variant> &constant :
					snapshot.constants) {
				if (constant.value.get_type() != Variant::DICTIONARY) {
					continue;
				}
				const String enum_name = constant.key;
				const String transformed_enum_name =
						rename_atomic(constant.key);
				add_replacement(enum_name, transformed_enum_name);
				const auto add_owned_enum =
						[&](const String &p_owner,
								const String &p_transformed_owner) {
							if (p_owner.is_empty()) {
								return;
							}
							add_replacement(
									p_owner + "." + enum_name,
									p_transformed_owner + "." +
											transformed_enum_name);
							add_replacement(
									p_owner + "::" + enum_name,
									p_transformed_owner + "::" +
											transformed_enum_name);
						};
				add_owned_enum(
						String(snapshot.local_name),
						String(snapshot.transformed_local_name));
				add_owned_enum(
						String(snapshot.global_name),
						String(snapshot.transformed_global_name));
				add_owned_enum(
						snapshot.fully_qualified_name,
						snapshot.transformed_fully_qualified_name);
				add_owned_enum(
						snapshot.fully_qualified_name.replace("::", "."),
						snapshot.transformed_fully_qualified_name.replace(
								"::", "."));
				add_owned_enum(
						relative_identity,
						transformed_relative_identity);
				add_owned_enum(
						relative_identity.replace("::", "."),
						transformed_relative_identity.replace(
								"::", "."));
			}
			if (!r_diagnostics.is_empty()) {
				return false;
			}
		}

		for (const KeyValue<String, String> &entry : unique_replacements) {
			IdentityReplacement replacement;
			replacement.source = entry.key;
			replacement.replacement = entry.value;
			identity_replacements.push_back(replacement);
		}
		identity_replacements.sort_custom<IdentityReplacementComparator>();
		return true;
	}

	static bool has_identity_boundaries(const String &p_text, int p_start, int p_length) {
		const bool left_boundary =
				p_start == 0 || !is_unicode_identifier_continue(p_text[p_start - 1]);
		const int end = p_start + p_length;
		const bool right_boundary =
				end == p_text.length() || !is_unicode_identifier_continue(p_text[end]);
		return left_boundary && right_boundary;
	}

	String rewrite_identity(const String &p_identity) const {
		if (p_identity.is_empty()) {
			return p_identity;
		}
		for (const IdentityReplacement &replacement :
				identity_replacements) {
			if (p_identity == replacement.source) {
				return replacement.replacement;
			}
		}

		String prefix;
		String body = p_identity;
		for (const String &script_path : script_paths) {
			if (body == script_path) {
				return body;
			}
			const String nested_prefix = script_path + "::";
			if (body.begins_with(nested_prefix)) {
				prefix = nested_prefix;
				body = body.substr(nested_prefix.length());
				break;
			}
		}
		if (prefix.is_empty() && body.find("://") >= 0) {
			const int nested_separator = body.find("::", body.find("://") + 3);
			if (nested_separator < 0) {
				return body;
			}
			prefix = body.substr(0, nested_separator + 2);
			body = body.substr(nested_separator + 2);
		}

		String rewritten;
		int offset = 0;
		while (offset < body.length()) {
			bool matched = false;
			for (const IdentityReplacement &replacement : identity_replacements) {
				const int source_length = replacement.source.length();
				if (source_length == 0 || offset + source_length > body.length() ||
						body.substr(offset, source_length) != replacement.source ||
						!has_identity_boundaries(body, offset, source_length)) {
					continue;
				}
				if (replacement.source.is_valid_unicode_identifier()) {
					const bool follows_identity_separator =
							offset > 0 &&
							(body[offset - 1] == '.' || body[offset - 1] == ':');
					const int end = offset + source_length;
					const bool precedes_identity_separator =
							end < body.length() &&
							(body[end] == '.' || body[end] == ':');
					if (follows_identity_separator ||
							precedes_identity_separator) {
						continue;
					}
				}
				rewritten += replacement.replacement;
				offset += source_length;
				matched = true;
				break;
			}
			if (!matched) {
				rewritten += body.substr(offset, 1);
				offset++;
			}
		}
		return prefix + rewritten;
	}

	StringName rewrite_dispatch_name(const StringName &p_name) const {
		const StringName atom = rename_atomic(p_name);
		if (atom != p_name) {
			return atom;
		}
		return StringName(rewrite_identity(String(p_name)));
	}

	void rewrite_data_type(FSDataType &r_type) const {
		if (r_type.is_script_trait) {
			r_type.script_trait = StringName(rewrite_identity(String(r_type.script_trait)));
		}
		for (FSDataType &element_type : r_type.container_element_types) {
			rewrite_data_type(element_type);
		}
		for (FSDataType &type_argument : r_type.type_arguments) {
			rewrite_data_type(type_argument);
		}
	}

	void rewrite_type_argument_binding(FoundryScript::TypeArgumentBinding &r_binding) const {
		rewrite_data_type(r_binding.fixed);
	}

	static bool is_type_bearing_hint(PropertyHint p_hint) {
		switch (p_hint) {
			case PROPERTY_HINT_RESOURCE_TYPE:
			case PROPERTY_HINT_TYPE_STRING:
			case PROPERTY_HINT_NODE_PATH_VALID_TYPES:
			case PROPERTY_HINT_ARRAY_TYPE:
			case PROPERTY_HINT_NODE_TYPE:
			case PROPERTY_HINT_DICTIONARY_TYPE:
			case PROPERTY_HINT_CALLABLE_TYPE:
			case PROPERTY_HINT_COROUTINE_TYPE:
				return true;
			default:
				return false;
		}
	}

	void rewrite_property_info(PropertyInfo &r_info, bool p_rename_name = false) const {
		if (p_rename_name) {
			r_info.name = rename_atomic_string(r_info.name);
		}
		r_info.class_name = StringName(rewrite_identity(String(r_info.class_name)));
		if (is_type_bearing_hint(r_info.hint)) {
			r_info.hint_string = rewrite_identity(r_info.hint_string);
		}
	}

	HashMap<StringName, StringName> allocate_parameter_names(
			const MethodInfo &p_info,
			const HashMap<StringName, Vector<FoundryScript::AnnotationUsage>>
					*p_annotation_parameters = nullptr) const {
		HashSet<StringName> reserved;
		for (const PropertyInfo &argument : p_info.arguments) {
			reserved.insert(argument.name);
		}
		Vector<StringName> annotation_only_parameters;
		if (p_annotation_parameters != nullptr) {
			for (const KeyValue<StringName,
						 Vector<FoundryScript::AnnotationUsage>> &parameter :
					*p_annotation_parameters) {
				if (!reserved.has(parameter.key)) {
					annotation_only_parameters.push_back(parameter.key);
				}
				reserved.insert(parameter.key);
			}
			annotation_only_parameters.sort();
		}

		HashMap<StringName, StringName> result;
		uint64_t ordinal = 0;
		const auto allocate = [&](const StringName &p_original_name) {
			StringName candidate;
			do {
				candidate = StringName("_fsb_arg_" + String::num_int64(ordinal++, 36));
			} while (reserved.has(candidate));
			reserved.insert(candidate);
			result.insert(p_original_name, candidate);
		};
		for (const PropertyInfo &argument : p_info.arguments) {
			allocate(argument.name);
		}
		for (const StringName &parameter : annotation_only_parameters) {
			allocate(parameter);
		}
		return result;
	}

	void rewrite_method_info(MethodInfo &r_info, const StringName &p_owner_name,
			const HashMap<StringName, StringName> *p_parameter_names = nullptr) const {
		r_info.name = String(rename_atomic(p_owner_name));
		rewrite_property_info(r_info.return_val);
		for (PropertyInfo &argument : r_info.arguments) {
			rewrite_property_info(argument);
			if (p_parameter_names != nullptr) {
				const StringName *renamed = p_parameter_names->getptr(argument.name);
				if (renamed != nullptr) {
					argument.name = *renamed;
				}
			}
		}
	}

	void rewrite_member_info(FoundryScript::MemberInfo &r_info, const StringName &p_key) const {
		r_info.setter = rename_atomic(r_info.setter);
		r_info.getter = rename_atomic(r_info.getter);
		rewrite_data_type(r_info.data_type);
		rewrite_property_info(r_info.property_info);
		r_info.property_info.name = String(p_key);
		rewrite_type_argument_binding(r_info.type_argument_binding);
	}

	template <typename T>
	HashMap<StringName, T> rewrite_atomic_key_map(const HashMap<StringName, T> &p_source) const {
		HashMap<StringName, T> result;
		for (const KeyValue<StringName, T> &entry : p_source) {
			result.insert(rename_atomic(entry.key), entry.value);
		}
		return result;
	}

	bool snapshot_class(const Ref<FoundryScript> &p_script,
			FoundryScript *p_parent,
			Vector<Diagnostic> &r_diagnostics) {
		if (p_script.is_null()) {
			add_map_diagnostic(r_diagnostics, "root graph", StringName(),
					"Found a null nested Foundry Script.");
			return false;
		}
		if (indexed_classes.has(p_script.ptr())) {
			add_map_diagnostic(r_diagnostics, "root graph",
					p_script->local_name,
					vformat(
							"Found class `%s` more than once in the supplied graph.",
							p_script->fully_qualified_name));
			return false;
		}
		indexed_classes.insert(p_script.ptr());

		ClassSnapshot snapshot;
		snapshot.script = p_script;
		snapshot.parent = p_parent;
		snapshot.trait_type_name = p_script->trait_type_name;
		snapshot.local_name = p_script->local_name;
		snapshot.global_name = p_script->global_name;
		snapshot.fully_qualified_name = p_script->fully_qualified_name;
		snapshot.member_indices = p_script->member_indices;
		snapshot.members = p_script->members;
		snapshot.member_type_argument_bindings = p_script->member_type_argument_bindings;
		snapshot.type_parameter_bindings_by_ancestor = p_script->type_parameter_bindings_by_ancestor;
		snapshot.static_variables_indices = p_script->static_variables_indices;
		snapshot.constants = p_script->constants;
		snapshot.member_functions = p_script->member_functions;
		snapshot.enum_functions = p_script->enum_functions;
		snapshot.subclasses = p_script->subclasses;
		snapshot.signals = p_script->_signals;
		snapshot.script_trait_list = p_script->script_trait_list;
		snapshot.abstract_trait_requirements = p_script->abstract_trait_requirements;
		snapshot.type_parameters = p_script->type_parameters;
		snapshot.method_annotations = p_script->method_annotations;
		snapshot.variable_annotations = p_script->variable_annotations;
		snapshot.signal_annotations = p_script->signal_annotations;
		snapshot.constant_annotations = p_script->constant_annotations;
		snapshot.method_parameter_annotations = p_script->method_parameter_annotations;
		snapshot.signal_parameter_annotations = p_script->signal_parameter_annotations;
		snapshot.old_static_variables_indices = p_script->old_static_variables_indices;
		snapshot.member_lines = p_script->member_lines;
		snapshot.member_default_values = p_script->member_default_values;
		snapshot.members_cache = p_script->members_cache;
		snapshot.member_default_values_cache = p_script->member_default_values_cache;

		const int snapshot_index = class_snapshots.size();
		class_snapshot_indices.insert(p_script.ptr(), snapshot_index);
		class_snapshots.push_back(snapshot);

		Vector<Ref<FoundryScript>> subclasses;
		for (const KeyValue<StringName, Ref<FoundryScript>> &subclass : p_script->subclasses) {
			subclasses.push_back(subclass.value);
		}
		subclasses.sort_custom<ScriptRefComparator>();
		for (const Ref<FoundryScript> &subclass : subclasses) {
			if (!snapshot_class(
						subclass, p_script.ptr(), r_diagnostics)) {
				return false;
			}
		}
		return true;
	}

	void snapshot_function(FSFunction *p_function, const StringName &p_owner_name, bool p_lambda = false) {
		if (p_function == nullptr || indexed_functions.has(p_function)) {
			return;
		}
		indexed_functions.insert(p_function);

		FunctionSnapshot snapshot;
		snapshot.function = p_function;
		snapshot.owner_name = p_owner_name;
		snapshot.lambda = p_lambda;
		snapshot.safe_arguments = p_lambda ||
				(p_owner_name != StringName() && rename_map.has(p_owner_name));
		snapshot.name = p_function->name;
		snapshot.argument_types = p_function->argument_types;
		snapshot.return_type = p_function->return_type;
		snapshot.method_info = p_function->method_info;
		snapshot.global_names = p_function->global_names;
		if (snapshot.safe_arguments) {
			const HashMap<StringName,
					Vector<FoundryScript::AnnotationUsage>>
					*annotation_parameters = nullptr;
			if (p_function->_script != nullptr &&
					p_owner_name != StringName()) {
				annotation_parameters =
						p_function->_script->method_parameter_annotations.getptr(
								p_owner_name);
			}
			snapshot.parameter_names = allocate_parameter_names(
					snapshot.method_info, annotation_parameters);
		}
		function_snapshots.push_back(snapshot);

		for (FSFunction *lambda : p_function->lambdas) {
			snapshot_function(lambda, StringName(), true);
		}
	}

	static bool identity_contains_name(
			const String &p_identity, const StringName &p_name) {
		if (p_identity == String(p_name)) {
			return true;
		}
		const PackedStringArray components =
				p_identity.replace("::", ".").split(".", false);
		for (const String &component : components) {
			if (component == String(p_name)) {
				return true;
			}
		}
		return false;
	}

	static Vector<String> get_encoded_type_identities(const String &p_text) {
		Vector<String> identities;
		int offset = 0;
		while (offset < p_text.length()) {
			while (offset < p_text.length() &&
					!is_unicode_identifier_start(p_text[offset])) {
				offset++;
			}
			if (offset >= p_text.length()) {
				break;
			}
			const int start = offset;
			while (offset < p_text.length()) {
				const char32_t character = p_text[offset];
				if (is_unicode_identifier_continue(character) ||
						character == '.' || character == ':') {
					offset++;
					continue;
				}
				break;
			}
			String identity = p_text.substr(start, offset - start);
			while (identity.ends_with(".") || identity.ends_with(":")) {
				identity = identity.left(identity.length() - 1);
			}
			if (!identity.is_empty()) {
				identities.push_back(identity);
			}
		}
		return identities;
	}

	static bool raw_property_info_contains_name(
			const PropertyInfo &p_info, const StringName &p_name) {
		if (identity_contains_name(String(p_info.class_name), p_name)) {
			return true;
		}
		if (!is_type_bearing_hint(p_info.hint)) {
			return false;
		}
		for (const String &identity :
				get_encoded_type_identities(p_info.hint_string)) {
			if (identity_contains_name(identity, p_name)) {
				return true;
			}
		}
		return false;
	}

	static bool metadata_variant_contains_name_internal(
			const Variant &p_value, const StringName &p_name,
			HashSet<const void *> &r_visited_containers, int p_depth = 0) {
		const bool is_array = p_value.get_type() == Variant::ARRAY;
		const bool is_dictionary =
				p_value.get_type() == Variant::DICTIONARY;
		const void *container_id = nullptr;
		if (is_array) {
			container_id = Array(p_value).id();
		} else if (is_dictionary) {
			container_id = Dictionary(p_value).id();
		}
		if (container_id != nullptr &&
				r_visited_containers.has(container_id)) {
			return false;
		}
		if (p_depth > Variant::MAX_RECURSION_DEPTH) {
			return true;
		}
		if (container_id != nullptr) {
			r_visited_containers.insert(container_id);
		}
		switch (p_value.get_type()) {
			case Variant::STRING:
				return String(p_value) == String(p_name);
			case Variant::STRING_NAME:
				return StringName(p_value) == p_name;
			case Variant::NODE_PATH: {
				const NodePath path = p_value;
				for (int i = 0; i < path.get_subname_count(); i++) {
					if (path.get_subname(i) == p_name) {
						return true;
					}
				}
			} break;
			case Variant::CALLABLE: {
				const Callable callable = p_value;
				if (callable.get_method() == p_name) {
					return true;
				}
				for (const Variant &bound : callable.get_bound_arguments()) {
					if (metadata_variant_contains_name_internal(
								bound, p_name, r_visited_containers,
								p_depth + 1)) {
						return true;
					}
				}
			} break;
			case Variant::SIGNAL:
				return Signal(p_value).get_name() == p_name;
			case Variant::ARRAY:
				for (const Variant &value : Array(p_value)) {
					if (metadata_variant_contains_name_internal(
								value, p_name, r_visited_containers,
								p_depth + 1)) {
						return true;
					}
				}
				break;
			case Variant::DICTIONARY: {
				const Dictionary dictionary = p_value;
				for (const Variant &key : dictionary.keys()) {
					if (metadata_variant_contains_name_internal(
								key, p_name, r_visited_containers,
								p_depth + 1) ||
							metadata_variant_contains_name_internal(
									dictionary[key], p_name,
									r_visited_containers, p_depth + 1)) {
						return true;
					}
				}
			} break;
			case Variant::PACKED_STRING_ARRAY:
				for (const String &value : PackedStringArray(p_value)) {
					if (value == String(p_name)) {
						return true;
					}
				}
				break;
			case Variant::OBJECT: {
				Object *object = p_value;
				Resource *resource = Object::cast_to<Resource>(object);
				return resource != nullptr &&
						resource->get_path().contains(String(p_name));
			}
			default:
				break;
		}
		return false;
	}

	static bool metadata_variant_contains_name(
			const Variant &p_value, const StringName &p_name) {
		HashSet<const void *> visited_containers;
		return metadata_variant_contains_name_internal(
				p_value, p_name, visited_containers);
	}

	static bool external_script_contains_name(
			Script *p_script, const StringName &p_name,
			HashSet<const Script *> &r_visited, int p_depth) {
		if (p_script == nullptr) {
			return false;
		}
		if (p_depth > Variant::MAX_RECURSION_DEPTH) {
			return true;
		}
		if (Object::cast_to<FoundryScript>(p_script) != nullptr) {
			return false;
		}
		if (r_visited.has(p_script)) {
			return false;
		}
		r_visited.insert(p_script);
		if (p_script->get_path().contains(String(p_name)) ||
				p_script->get_global_name() == p_name) {
			return true;
		}
		List<MethodInfo> methods;
		p_script->get_script_method_list(&methods);
		for (const MethodInfo &method : methods) {
			if (method.name == p_name ||
					raw_property_info_contains_name(
							method.return_val, p_name)) {
				return true;
			}
			for (const PropertyInfo &argument : method.arguments) {
				if (raw_property_info_contains_name(argument, p_name)) {
					return true;
				}
			}
			for (const Variant &default_argument :
					method.default_arguments) {
				if (metadata_variant_contains_name(
							default_argument, p_name)) {
					return true;
				}
			}
		}
		List<PropertyInfo> properties;
		p_script->get_script_property_list(&properties);
		for (const PropertyInfo &property : properties) {
			if (StringName(property.name) == p_name ||
					raw_property_info_contains_name(property, p_name)) {
				return true;
			}
		}
		List<MethodInfo> signals;
		p_script->get_script_signal_list(&signals);
		for (const MethodInfo &signal : signals) {
			if (signal.name == p_name ||
					raw_property_info_contains_name(
							signal.return_val, p_name)) {
				return true;
			}
			for (const PropertyInfo &argument : signal.arguments) {
				if (raw_property_info_contains_name(argument, p_name)) {
					return true;
				}
			}
			for (const Variant &default_argument :
					signal.default_arguments) {
				if (metadata_variant_contains_name(
							default_argument, p_name)) {
					return true;
				}
			}
		}
		HashMap<StringName, Variant> constants;
		p_script->get_constants(&constants);
		if (constants.has(p_name)) {
			return true;
		}
		HashSet<StringName> members;
		p_script->get_members(&members);
		if (members.has(p_name)) {
			return true;
		}
		const Ref<Script> base = p_script->get_base_script();
		return external_script_contains_name(
				base.ptr(), p_name, r_visited, p_depth + 1);
	}

	bool container_type_contains_name(
			const ContainerType &p_type, const StringName &p_name,
			HashSet<const Script *> &r_visited_scripts, int p_depth) const {
		if (p_depth > Variant::MAX_RECURSION_DEPTH) {
			return true;
		}
		const FoundryScript *foundry_script =
				Object::cast_to<FoundryScript>(p_type.script.ptr());
		const bool included_foundry_script =
				foundry_script != nullptr &&
				class_snapshot_indices.has(foundry_script);
		if ((!included_foundry_script &&
					identity_contains_name(
							String(p_type.class_name), p_name)) ||
				external_script_contains_name(
						p_type.script.ptr(), p_name, r_visited_scripts,
						p_depth + 1)) {
			return true;
		}
		for (const ContainerType &element_type : p_type.element_types) {
			if (container_type_contains_name(
						element_type, p_name, r_visited_scripts,
						p_depth + 1)) {
				return true;
			}
		}
		for (const ContainerType &type_argument : p_type.type_arguments) {
			if (container_type_contains_name(
						type_argument, p_name, r_visited_scripts,
						p_depth + 1)) {
				return true;
			}
		}
		return false;
	}

	bool variant_contains_name_internal(
			const Variant &p_value, const StringName &p_name,
			HashSet<const void *> &r_visited_containers,
			HashSet<const Script *> &r_visited_scripts, int p_depth) const {
		const bool is_array = p_value.get_type() == Variant::ARRAY;
		const bool is_dictionary =
				p_value.get_type() == Variant::DICTIONARY;
		const void *container_id = nullptr;
		if (is_array) {
			container_id = Array(p_value).id();
		} else if (is_dictionary) {
			container_id = Dictionary(p_value).id();
		}
		if (container_id != nullptr &&
				r_visited_containers.has(container_id)) {
			return false;
		}
		if (p_depth > Variant::MAX_RECURSION_DEPTH) {
			return true;
		}
		if (container_id != nullptr) {
			r_visited_containers.insert(container_id);
		}
		switch (p_value.get_type()) {
			case Variant::STRING:
				return String(p_value) == String(p_name);
			case Variant::STRING_NAME:
				return StringName(p_value) == p_name;
			case Variant::NODE_PATH: {
				const NodePath path = p_value;
				for (int i = 0; i < path.get_subname_count(); i++) {
					if (path.get_subname(i) == p_name) {
						return true;
					}
				}
			} break;
			case Variant::CALLABLE: {
				const Callable callable = p_value;
				if (callable.get_method() == p_name) {
					return true;
				}
				for (const Variant &bound :
						callable.get_bound_arguments()) {
					if (variant_contains_name_internal(
								bound, p_name, r_visited_containers,
								r_visited_scripts, p_depth + 1)) {
						return true;
					}
				}
			} break;
			case Variant::SIGNAL:
				return Signal(p_value).get_name() == p_name;
			case Variant::ARRAY: {
				const Array array = p_value;
				if (container_type_contains_name(
							array.get_element_type(), p_name,
							r_visited_scripts, 0)) {
					return true;
				}
				for (const Variant &value : array) {
					if (variant_contains_name_internal(
								value, p_name, r_visited_containers,
								r_visited_scripts, p_depth + 1)) {
						return true;
					}
				}
			} break;
			case Variant::DICTIONARY: {
				const Dictionary dictionary = p_value;
				if (container_type_contains_name(
							dictionary.get_key_type(), p_name,
							r_visited_scripts, 0) ||
						container_type_contains_name(
								dictionary.get_value_type(), p_name,
								r_visited_scripts, 0)) {
					return true;
				}
				const Array keys = dictionary.keys();
				for (const Variant &key : keys) {
					if (variant_contains_name_internal(
								key, p_name, r_visited_containers,
								r_visited_scripts, p_depth + 1) ||
							variant_contains_name_internal(
									dictionary[key], p_name,
									r_visited_containers,
									r_visited_scripts, p_depth + 1)) {
						return true;
					}
				}
			} break;
			case Variant::PACKED_STRING_ARRAY: {
				for (const String &value : PackedStringArray(p_value)) {
					if (value == String(p_name)) {
						return true;
					}
				}
			} break;
			case Variant::OBJECT: {
				Object *object = p_value;
				Resource *resource = Object::cast_to<Resource>(object);
				if (resource != nullptr &&
						resource->get_path().contains(String(p_name))) {
					return true;
				}
				if (external_script_contains_name(
							Object::cast_to<Script>(object), p_name,
							r_visited_scripts, p_depth + 1)) {
					return true;
				}
				if (FSSpecializedClassHandle *specialized =
								Object::cast_to<FSSpecializedClassHandle>(
										object)) {
					if (external_script_contains_name(
								specialized->get_specialized_script().ptr(),
								p_name, r_visited_scripts, p_depth + 1)) {
						return true;
					}
					for (const ContainerType &type_argument :
							specialized->get_type_arguments()) {
						if (container_type_contains_name(
									type_argument, p_name,
									r_visited_scripts, 0)) {
							return true;
						}
					}
				}
			} break;
			case Variant::PACKED_BYTE_ARRAY:
			case Variant::PACKED_INT32_ARRAY:
			case Variant::PACKED_INT64_ARRAY:
			case Variant::PACKED_FLOAT32_ARRAY:
			case Variant::PACKED_FLOAT64_ARRAY:
			case Variant::PACKED_VECTOR2_ARRAY:
			case Variant::PACKED_VECTOR3_ARRAY:
			case Variant::PACKED_COLOR_ARRAY:
			case Variant::PACKED_VECTOR4_ARRAY:
				// These packed forms carry no textual identities.
				break;
			default:
				break;
		}
		return false;
	}

	bool variant_contains_name(
			const Variant &p_value, const StringName &p_name) const {
		HashSet<const void *> visited_containers;
		HashSet<const Script *> visited_scripts;
		return variant_contains_name_internal(
				p_value, p_name, visited_containers, visited_scripts, 0);
	}

	bool annotation_usages_contain_name(
			const Vector<FoundryScript::AnnotationUsage> &p_usages,
			const StringName &p_name) const {
		for (const FoundryScript::AnnotationUsage &usage : p_usages) {
			if (usage.name == p_name ||
					identity_contains_name(
							String(usage.qualified_name), p_name) ||
					variant_contains_name(usage.args, p_name) ||
					variant_contains_name(usage.kwargs, p_name)) {
				return true;
			}
		}
		return false;
	}

	static bool has_builtin_annotation(
			const Vector<FoundryScript::AnnotationUsage> &p_usages,
			const StringName &p_name, bool p_prefix = false) {
		for (const FoundryScript::AnnotationUsage &usage : p_usages) {
			if (usage.is_builtin &&
					((p_prefix &&
							 String(usage.name).begins_with(String(p_name))) ||
							(!p_prefix && usage.name == p_name))) {
				return true;
			}
		}
		return false;
	}

	bool property_info_contains_name(
			const PropertyInfo &p_info, const StringName &p_name) const {
		const String class_identity = p_info.class_name;
		if (!class_identity.is_empty() &&
				!identity_is_included(class_identity) &&
				identity_contains_name(class_identity, p_name)) {
			return true;
		}
		if (is_type_bearing_hint(p_info.hint)) {
			for (const String &identity :
					get_encoded_type_identities(p_info.hint_string)) {
				if (!identity_is_included(identity) &&
						identity_contains_name(identity, p_name)) {
					return true;
				}
			}
		}
		return false;
	}

	bool data_type_contains_name(
			const FSDataType &p_type, const StringName &p_name,
			HashSet<const Script *> &r_visited_scripts,
			int p_depth = 0) const {
		if (p_depth > Variant::MAX_RECURSION_DEPTH) {
			return true;
		}
		if (p_type.native_type != StringName() &&
				identity_contains_name(String(p_type.native_type), p_name)) {
			return true;
		}
		if (p_type.is_script_trait &&
				!identity_is_included(String(p_type.script_trait)) &&
				identity_contains_name(String(p_type.script_trait), p_name)) {
			return true;
		}
		if (external_script_contains_name(
					p_type.script_type, p_name, r_visited_scripts,
					p_depth + 1) ||
				external_script_contains_name(
						p_type.script_type_ref.ptr(), p_name,
						r_visited_scripts, p_depth + 1)) {
			return true;
		}
		for (const FSDataType &element_type :
				p_type.container_element_types) {
			if (data_type_contains_name(
						element_type, p_name, r_visited_scripts,
						p_depth + 1)) {
				return true;
			}
		}
		for (const FSDataType &type_argument : p_type.type_arguments) {
			if (data_type_contains_name(
						type_argument, p_name, r_visited_scripts,
						p_depth + 1)) {
				return true;
			}
		}
		return false;
	}

	void add_global_protected_name(
			const StringName &p_name, const String &p_surface) {
		if (p_name != StringName() &&
				!global_protected_names.has(p_name)) {
			global_protected_names.insert(p_name, p_surface);
		}
	}

	void add_global_protected_identity(
			const String &p_identity, const String &p_surface) {
		if (p_identity.is_valid_unicode_identifier()) {
			add_global_protected_name(
					StringName(p_identity), p_surface);
			return;
		}
		for (const String &component :
				p_identity.replace("::", ".").split(".", false)) {
			if (component.is_valid_unicode_identifier()) {
				add_global_protected_name(
						StringName(component), p_surface);
			}
		}
	}

	void add_global_protected_path(
			const String &p_path, const String &p_surface) {
		if (!p_path.is_empty() &&
				!global_protected_paths.has(p_path)) {
			global_protected_paths.insert(p_path, p_surface);
		}
	}

	void add_global_protected_property_info(
			const PropertyInfo &p_info, const String &p_surface) {
		add_global_protected_identity(
				String(p_info.class_name), p_surface + " class identity");
		if (!is_type_bearing_hint(p_info.hint)) {
			return;
		}
		for (const String &identity :
				get_encoded_type_identities(p_info.hint_string)) {
			add_global_protected_identity(
					identity, p_surface + " encoded type identity");
		}
	}

	void add_global_protected_variant(
			const Variant &p_value, const String &p_surface,
			HashSet<const void *> &r_visited_containers, int p_depth = 0) {
		const bool is_array = p_value.get_type() == Variant::ARRAY;
		const bool is_dictionary =
				p_value.get_type() == Variant::DICTIONARY;
		const void *container_id = nullptr;
		if (is_array) {
			container_id = Array(p_value).id();
		} else if (is_dictionary) {
			container_id = Dictionary(p_value).id();
		}
		if (container_id != nullptr &&
				r_visited_containers.has(container_id)) {
			return;
		}
		if (p_depth > Variant::MAX_RECURSION_DEPTH) {
			unscannable_protected_surface = true;
			return;
		}
		if (container_id != nullptr) {
			r_visited_containers.insert(container_id);
		}
		switch (p_value.get_type()) {
			case Variant::STRING:
				add_global_protected_name(
						StringName(String(p_value)), p_surface);
				break;
			case Variant::STRING_NAME:
				add_global_protected_name(StringName(p_value), p_surface);
				break;
			case Variant::NODE_PATH: {
				const NodePath path = p_value;
				for (int i = 0; i < path.get_subname_count(); i++) {
					add_global_protected_name(path.get_subname(i), p_surface);
				}
			} break;
			case Variant::CALLABLE: {
				const Callable callable = p_value;
				add_global_protected_name(callable.get_method(), p_surface);
				for (const Variant &bound : callable.get_bound_arguments()) {
					add_global_protected_variant(
							bound, p_surface, r_visited_containers,
							p_depth + 1);
				}
			} break;
			case Variant::SIGNAL:
				add_global_protected_name(
						Signal(p_value).get_name(), p_surface);
				break;
			case Variant::ARRAY:
				for (const Variant &value : Array(p_value)) {
					add_global_protected_variant(
							value, p_surface, r_visited_containers,
							p_depth + 1);
				}
				break;
			case Variant::DICTIONARY: {
				const Dictionary dictionary = p_value;
				for (const Variant &key : dictionary.keys()) {
					add_global_protected_variant(
							key, p_surface, r_visited_containers,
							p_depth + 1);
					add_global_protected_variant(
							dictionary[key], p_surface,
							r_visited_containers, p_depth + 1);
				}
			} break;
			case Variant::PACKED_STRING_ARRAY:
				for (const String &value : PackedStringArray(p_value)) {
					add_global_protected_name(StringName(value), p_surface);
				}
				break;
			case Variant::OBJECT: {
				Object *object = p_value;
				if (Resource *resource =
								Object::cast_to<Resource>(object)) {
					add_global_protected_path(
							resource->get_path(), p_surface + " resource path");
				}
			} break;
			default:
				break;
		}
	}

	void index_external_script_names(
			Script *p_script, const String &p_surface,
			HashSet<const Script *> &r_visited, int p_depth = 0) {
		if (p_script == nullptr) {
			return;
		}
		if (p_depth > Variant::MAX_RECURSION_DEPTH) {
			unscannable_protected_surface = true;
			return;
		}
		if (Object::cast_to<FoundryScript>(p_script) != nullptr ||
				r_visited.has(p_script)) {
			return;
		}
		r_visited.insert(p_script);
		add_global_protected_path(
				p_script->get_path(), p_surface + " path");
		add_global_protected_name(
				p_script->get_global_name(), p_surface + " global name");
		HashSet<const void *> visited_default_containers;
		List<MethodInfo> methods;
		p_script->get_script_method_list(&methods);
		for (const MethodInfo &method : methods) {
			add_global_protected_name(
					method.name, p_surface + " method");
			add_global_protected_property_info(
					method.return_val, p_surface + " return type");
			for (const PropertyInfo &argument : method.arguments) {
				add_global_protected_property_info(
						argument, p_surface + " argument type");
			}
			for (const Variant &default_argument :
					method.default_arguments) {
				add_global_protected_variant(
						default_argument,
						p_surface + " default argument",
						visited_default_containers);
			}
		}
		List<PropertyInfo> properties;
		p_script->get_script_property_list(&properties);
		for (const PropertyInfo &property : properties) {
			add_global_protected_name(
					StringName(property.name), p_surface + " property");
			add_global_protected_property_info(
					property, p_surface + " property type");
		}
		List<MethodInfo> signals;
		p_script->get_script_signal_list(&signals);
		for (const MethodInfo &signal : signals) {
			add_global_protected_name(
					signal.name, p_surface + " signal");
			add_global_protected_property_info(
					signal.return_val, p_surface + " signal return type");
			for (const PropertyInfo &argument : signal.arguments) {
				add_global_protected_property_info(
						argument, p_surface + " signal argument type");
			}
			for (const Variant &default_argument :
					signal.default_arguments) {
				add_global_protected_variant(
						default_argument,
						p_surface + " signal default argument",
						visited_default_containers);
			}
		}
		HashMap<StringName, Variant> constants;
		p_script->get_constants(&constants);
		for (const KeyValue<StringName, Variant> &constant : constants) {
			add_global_protected_name(
					constant.key, p_surface + " constant");
		}
		HashSet<StringName> members;
		p_script->get_members(&members);
		for (const StringName &member : members) {
			add_global_protected_name(
					member, p_surface + " member");
		}
		const Ref<Script> base = p_script->get_base_script();
		index_external_script_names(
				base.ptr(), p_surface + " base", r_visited, p_depth + 1);
	}

	void index_global_protected_names() {
		LocalVector<StringName> classes;
		ClassDB::get_class_list(classes); // Deterministic lexical order.
		for (const StringName &class_name : classes) {
			const String surface = "ClassDB `" + String(class_name) + "`";
			add_global_protected_name(class_name, surface + " class");
			List<MethodInfo> methods;
			ClassDB::get_method_list(class_name, &methods, true);
			for (const MethodInfo &method : methods) {
				add_global_protected_name(
						method.name, surface + " method");
			}
			List<MethodInfo> virtual_methods;
			ClassDB::get_virtual_methods(
					class_name, &virtual_methods, true);
			for (const MethodInfo &method : virtual_methods) {
				add_global_protected_name(
						method.name, surface + " virtual method");
			}
			List<PropertyInfo> properties;
			ClassDB::get_property_list(class_name, &properties, true);
			for (const PropertyInfo &property : properties) {
				const StringName property_name = StringName(property.name);
				add_global_protected_name(
						property_name, surface + " property");
				add_global_protected_name(
						ClassDB::get_property_setter(
								class_name, property_name),
						surface + " property setter");
				add_global_protected_name(
						ClassDB::get_property_getter(
								class_name, property_name),
						surface + " property getter");
			}
			List<MethodInfo> signals;
			ClassDB::get_signal_list(class_name, &signals, true);
			for (const MethodInfo &signal : signals) {
				add_global_protected_name(
						signal.name, surface + " signal");
			}
			List<String> constants;
			ClassDB::get_integer_constant_list(
					class_name, &constants, true);
			for (const String &constant : constants) {
				add_global_protected_name(
						StringName(constant), surface + " constant");
			}
		}
		for (int type = 0; type < Variant::VARIANT_MAX; type++) {
			const Variant::Type variant_type = (Variant::Type)type;
			add_global_protected_name(
					StringName(Variant::get_type_name(variant_type)),
					"Variant type");
			List<StringName> methods;
			Variant::get_builtin_method_list(variant_type, &methods);
			for (const StringName &method : methods) {
				add_global_protected_name(
						method, "Variant builtin method");
			}
			List<StringName> members;
			Variant::get_member_list(variant_type, &members);
			for (const StringName &member : members) {
				add_global_protected_name(member, "Variant member");
			}
		}
		List<StringName> utilities;
		Variant::get_utility_function_list(&utilities);
		for (const StringName &utility : utilities) {
			add_global_protected_name(utility, "Variant utility");
		}
		List<StringName> foundry_utilities;
		FSUtilityFunctions::get_function_list(&foundry_utilities);
		for (const StringName &utility : foundry_utilities) {
			add_global_protected_name(utility, "Foundry utility");
		}
		if (Engine::get_singleton() != nullptr) {
			List<Engine::Singleton> singletons;
			Engine::get_singleton()->get_singletons(&singletons);
			for (const Engine::Singleton &singleton : singletons) {
				add_global_protected_name(
						singleton.name, "engine singleton");
			}
		}
		if (FSLanguage::get_singleton() != nullptr) {
			for (const String &name :
					FSLanguage::get_singleton()
							->get_reserved_global_names()) {
				add_global_protected_name(
						StringName(name), "language global");
			}
		}
		LocalVector<StringName> global_classes;
		ScriptServer::get_global_class_list(global_classes);
		HashSet<const Script *> visited_scripts;
		for (const StringName &global_class : global_classes) {
			if (FSLanguage::get_singleton() != nullptr &&
					ScriptServer::get_global_class_language(global_class) ==
							FSLanguage::get_singleton()->get_name()) {
				continue;
			}
			add_global_protected_name(
					global_class, "external global script class");
			const String path =
					ScriptServer::get_global_class_path(global_class);
			add_global_protected_path(
					path, "external global script path");
			Ref<Resource> resource = ResourceCache::get_ref(path);
			if (resource.is_null() && ResourceLoader::exists(path)) {
				resource = ResourceLoader::load(path);
			}
			index_external_script_names(
					Object::cast_to<Script>(resource.ptr()),
					"external global script `" + path + "`",
					visited_scripts);
		}
	}

	bool find_protected_surface(const StringName &p_name, String &r_surface) const {
		if (unscannable_protected_surface) {
			r_surface = "unscannable serialized surface";
			return true;
		}
		const String *global_surface =
				global_protected_names.getptr(p_name);
		if (global_surface != nullptr) {
			r_surface = *global_surface;
			return true;
		}
		for (const KeyValue<String, String> &path :
				global_protected_paths) {
			if (path.key.contains(String(p_name))) {
				r_surface = path.value;
				return true;
			}
		}
		if (ClassDB::class_exists(p_name)) {
			r_surface = "native class identity";
			return true;
		}
		if (Variant::get_type_by_name(String(p_name)) < Variant::VARIANT_MAX) {
			r_surface = "Variant type identity";
			return true;
		}
		if (Engine::get_singleton() != nullptr &&
				Engine::get_singleton()->has_singleton(p_name)) {
			r_surface = "engine singleton";
			return true;
		}
		if (FSLanguage::get_singleton() != nullptr &&
				FSLanguage::get_singleton()->is_reserved_global_name(p_name)) {
			r_surface = "language global";
			return true;
		}
		if (Variant::has_utility_function(p_name) ||
				FSUtilityFunctions::function_exists(p_name)) {
			r_surface = "utility function";
			return true;
		}

		for (int type = 0; type < Variant::VARIANT_MAX; type++) {
			if (Variant::has_builtin_method((Variant::Type)type, p_name)) {
				r_surface = "Variant builtin method";
				return true;
			}
			if (Variant::has_member((Variant::Type)type, p_name)) {
				r_surface = "Variant member";
				return true;
			}
		}

		bool reflects_methods = false;
		bool reflects_properties = false;
		bool reflects_signals = false;
		const auto record_reflection =
				[&](const StringName &p_method,
						const StringName &p_class) {
					if (p_method == SNAME("get_method_list") ||
							(p_class == SNAME("FSReflection") &&
									(p_method == SNAME("get_methods") ||
											p_method ==
													SNAME("get_method_descriptors")))) {
						reflects_methods = true;
					} else if (p_method == SNAME("get_property_list") ||
							(p_class == SNAME("FSReflection") &&
									(p_method == SNAME("get_properties") ||
											p_method ==
													SNAME("get_property_descriptors")))) {
						reflects_properties = true;
					} else if (p_method == SNAME("get_signal_list")) {
						reflects_signals = true;
					}
				};
		for (const FunctionSnapshot &snapshot : function_snapshots) {
			for (const StringName &global_name : snapshot.global_names) {
				record_reflection(global_name, StringName());
			}
			for (const FSFunction::ExportFixups::MethodBindKey &key :
					snapshot.function->export_fixups.method_binds) {
				record_reflection(key.method_name, key.class_name);
			}
		}

		HashSet<const Script *> visited_external_scripts;
		for (const ClassSnapshot &snapshot : class_snapshots) {
			const FoundryScript *script = snapshot.script.ptr();
			bool is_method_declaration =
					snapshot.member_functions.has(p_name) ||
					snapshot.abstract_trait_requirements.has(p_name);
			for (const KeyValue<StringName,
						 FoundryScript::EnumFunctionSet> &enum_entry :
					snapshot.enum_functions) {
				is_method_declaration =
						is_method_declaration ||
						enum_entry.value.instance_functions.has(p_name) ||
						enum_entry.value.static_functions.has(p_name);
			}
			if ((reflects_methods && is_method_declaration) ||
					(reflects_properties &&
							(snapshot.members.has(p_name) ||
									snapshot.member_indices.has(p_name) ||
									snapshot.static_variables_indices.has(
											p_name))) ||
					(reflects_signals && snapshot.signals.has(p_name))) {
				r_surface = "reflection enumeration";
				return true;
			}
			const String script_path = script->get_script_path();
			if (script_path.contains(String(p_name)) ||
					script->simplified_icon_path.contains(String(p_name))) {
				r_surface = "resource path";
				return true;
			}
			if (!script->registered_conformance_source.is_empty() &&
					script->registered_conformance_source.contains(String(p_name))) {
				r_surface = "conformance source path";
				return true;
			}
			if (snapshot.local_name == p_name &&
					has_builtin_annotation(
							script->class_annotations,
							SNAME("keep_name"))) {
				r_surface = "@keep_name class declaration";
				return true;
			}
			const auto annotation_owner_is_kept =
					[&](const HashMap<StringName,
								Vector<FoundryScript::AnnotationUsage>>
									&p_annotations,
							bool p_exported_property) {
						const Vector<FoundryScript::AnnotationUsage> *usages =
								p_annotations.getptr(p_name);
						return usages != nullptr &&
								(has_builtin_annotation(
										 *usages, SNAME("keep_name")) ||
										(p_exported_property &&
												has_builtin_annotation(
														*usages,
														SNAME("export"),
														true)));
					};
			if (annotation_owner_is_kept(
						snapshot.method_annotations, false) ||
					annotation_owner_is_kept(
							snapshot.variable_annotations, true) ||
					annotation_owner_is_kept(
							snapshot.signal_annotations, false) ||
					annotation_owner_is_kept(
							snapshot.constant_annotations, false)) {
				r_surface = "annotation-protected declaration";
				return true;
			}
			if (script->native.is_valid()) {
				const StringName native_name = script->native->get_name();
				if (native_name == p_name ||
						ClassDB::has_method(native_name, p_name) ||
						ClassDB::has_property(native_name, p_name) ||
						ClassDB::has_signal(native_name, p_name)) {
					r_surface = vformat("native API `%s`", native_name);
					return true;
				}
			}
			for (const KeyValue<StringName, FoundryScript::MemberInfo> &member :
					snapshot.member_indices) {
				if (property_info_contains_name(
							member.value.property_info, p_name) ||
						data_type_contains_name(
								member.value.data_type, p_name,
								visited_external_scripts) ||
						data_type_contains_name(
								member.value.type_argument_binding.fixed,
								p_name,
								visited_external_scripts)) {
					r_surface = "member type metadata";
					return true;
				}
			}
			for (const KeyValue<StringName, FoundryScript::MemberInfo>
							&member : snapshot.static_variables_indices) {
				if (property_info_contains_name(
							member.value.property_info, p_name) ||
						data_type_contains_name(
								member.value.data_type, p_name,
								visited_external_scripts) ||
						data_type_contains_name(
								member.value.type_argument_binding.fixed,
								p_name,
								visited_external_scripts)) {
					r_surface = "static member type metadata";
					return true;
				}
			}
			for (const FoundryScript::TypeArgumentBinding &binding :
					snapshot.member_type_argument_bindings) {
				if (data_type_contains_name(
							binding.fixed, p_name,
							visited_external_scripts)) {
					r_surface = "member type-argument binding";
					return true;
				}
			}
			for (const KeyValue<FoundryScript *,
						 Vector<FoundryScript::TypeArgumentBinding>>
							&ancestor :
					snapshot.type_parameter_bindings_by_ancestor) {
				if (external_script_contains_name(
							ancestor.key, p_name,
							visited_external_scripts, 0)) {
					r_surface = "ancestor type-argument binding";
					return true;
				}
				for (const FoundryScript::TypeArgumentBinding &binding :
						ancestor.value) {
					if (data_type_contains_name(
								binding.fixed, p_name,
								visited_external_scripts)) {
						r_surface = "ancestor type-argument binding";
						return true;
					}
				}
			}
			for (const KeyValue<StringName, FoundryScript::MemberInfo>
							&member :
					snapshot.old_static_variables_indices) {
				if (property_info_contains_name(
							member.value.property_info, p_name) ||
						data_type_contains_name(
								member.value.data_type, p_name,
								visited_external_scripts) ||
						data_type_contains_name(
								member.value.type_argument_binding.fixed,
								p_name, visited_external_scripts)) {
					r_surface = "old static member type metadata";
					return true;
				}
			}
			if (variant_contains_name(script->rpc_config, p_name)) {
				r_surface = "RPC configuration";
				return true;
			}
			for (const Variant &value : script->static_variables) {
				if (variant_contains_name(value, p_name)) {
					r_surface = "static value";
					return true;
				}
			}
			for (const KeyValue<StringName, Variant> &entry : snapshot.constants) {
				if (variant_contains_name(entry.value, p_name)) {
					r_surface = "constant value";
					return true;
				}
			}
			for (const KeyValue<StringName, Variant> &entry :
					snapshot.member_default_values) {
				if (variant_contains_name(entry.value, p_name)) {
					r_surface = "member default value";
					return true;
				}
			}
			for (const KeyValue<StringName, Variant> &entry :
					snapshot.member_default_values_cache) {
				if (variant_contains_name(entry.value, p_name)) {
					r_surface = "cached member default value";
					return true;
				}
			}
			if (annotation_usages_contain_name(script->class_annotations, p_name)) {
				r_surface = "annotation argument";
				return true;
			}
			const auto annotation_map_contains =
					[&](const HashMap<StringName,
							Vector<FoundryScript::AnnotationUsage>>
									&p_annotations) {
						for (const KeyValue<StringName,
									 Vector<FoundryScript::AnnotationUsage>>
										&annotations : p_annotations) {
							if (annotation_usages_contain_name(
										annotations.value, p_name)) {
								return true;
							}
						}
						return false;
					};
			if (annotation_map_contains(snapshot.method_annotations) ||
					annotation_map_contains(snapshot.variable_annotations) ||
					annotation_map_contains(snapshot.signal_annotations) ||
					annotation_map_contains(snapshot.constant_annotations)) {
				r_surface = "annotation argument";
				return true;
			}
			const auto parameter_annotation_map_contains =
					[&](const HashMap<StringName,
							HashMap<StringName,
									Vector<FoundryScript::AnnotationUsage>>>
									&p_annotations) {
						for (const KeyValue<StringName,
									 HashMap<StringName,
											 Vector<FoundryScript::AnnotationUsage>>>
										&owner : p_annotations) {
							if (annotation_map_contains(owner.value)) {
								return true;
							}
						}
						return false;
					};
			if (parameter_annotation_map_contains(
						snapshot.method_parameter_annotations) ||
					parameter_annotation_map_contains(
							snapshot.signal_parameter_annotations)) {
				r_surface = "annotation argument";
				return true;
			}
			for (const KeyValue<StringName, MethodInfo> &signal :
					snapshot.signals) {
				if (property_info_contains_name(
							signal.value.return_val, p_name)) {
					r_surface = "signal type metadata";
					return true;
				}
				for (const PropertyInfo &argument :
						signal.value.arguments) {
					if (property_info_contains_name(argument, p_name)) {
						r_surface = "signal type metadata";
						return true;
					}
				}
				for (const Variant &default_argument :
						signal.value.default_arguments) {
					if (variant_contains_name(default_argument, p_name)) {
						r_surface = "signal default argument";
						return true;
					}
				}
			}
			for (const KeyValue<StringName,
						 FoundryScript::AbstractTraitRequirement> &requirement :
					snapshot.abstract_trait_requirements) {
				if (data_type_contains_name(
							requirement.value.return_type, p_name,
							visited_external_scripts) ||
						property_info_contains_name(
								requirement.value.method_info.return_val,
								p_name)) {
					r_surface = "abstract requirement type metadata";
					return true;
				}
				for (const PropertyInfo &argument :
						requirement.value.method_info.arguments) {
					if (property_info_contains_name(argument, p_name)) {
						r_surface =
								"abstract requirement type metadata";
						return true;
					}
				}
				for (const Variant &default_argument :
						requirement.value.method_info.default_arguments) {
					if (variant_contains_name(default_argument, p_name)) {
						r_surface = "abstract requirement default argument";
						return true;
					}
				}
			}
			for (const FoundryScript::TypeParameter &type_parameter :
					snapshot.type_parameters) {
				if (property_info_contains_name(
							type_parameter.bound, p_name)) {
					r_surface = "type-parameter bound";
					return true;
				}
			}
			for (const PropertyInfo &property : snapshot.members_cache) {
				if (property_info_contains_name(property, p_name)) {
					r_surface = "cached property type metadata";
					return true;
				}
			}
		}

		for (const FunctionSnapshot &snapshot : function_snapshots) {
			const FSFunction *function = snapshot.function;
			if (String(function->source).contains(String(p_name))) {
				r_surface = "script path";
				return true;
			}
			if (variant_contains_name(function->rpc_config, p_name)) {
				r_surface = "RPC configuration";
				return true;
			}
			for (const Variant &value : function->constants) {
				if (variant_contains_name(value, p_name)) {
					r_surface = "function constant";
					return true;
				}
			}
			for (const Variant &value : function->method_info.default_arguments) {
				if (variant_contains_name(value, p_name)) {
					r_surface = "default argument";
					return true;
				}
			}
			for (const FSDataType &argument_type :
					snapshot.argument_types) {
				if (data_type_contains_name(
							argument_type, p_name,
							visited_external_scripts)) {
					r_surface = "function argument type";
					return true;
				}
			}
			if (data_type_contains_name(
						snapshot.return_type, p_name,
						visited_external_scripts) ||
					property_info_contains_name(
							snapshot.method_info.return_val, p_name)) {
				r_surface = "function return type";
				return true;
			}
			for (const PropertyInfo &argument :
					snapshot.method_info.arguments) {
				if (property_info_contains_name(argument, p_name)) {
					r_surface = "function argument type";
					return true;
				}
			}
			for (const StringName &name : function->builtin_method_names) {
				if (name == p_name) {
					r_surface = "builtin method table";
					return true;
				}
			}
			for (const FSFunction::ExportFixups::TypedNameKey &key :
					function->export_fixups.setters) {
				if (key.name == p_name) {
					r_surface = "Variant setter fixup";
					return true;
				}
			}
			for (const FSFunction::ExportFixups::TypedNameKey &key :
					function->export_fixups.getters) {
				if (key.name == p_name) {
					r_surface = "Variant getter fixup";
					return true;
				}
			}
			for (const FSFunction::ExportFixups::TypedNameKey &key :
					function->export_fixups.builtin_methods) {
				if (key.name == p_name) {
					r_surface = "Variant builtin-method fixup";
					return true;
				}
			}
			for (const FSFunction::ExportFixups::MethodBindKey &key :
					function->export_fixups.method_binds) {
				if (key.class_name == p_name || key.method_name == p_name) {
					r_surface = "ClassDB MethodBind fixup";
					return true;
				}
			}
			for (const StringName &name : function->export_fixups.utilities) {
				if (name == p_name) {
					r_surface = "Variant utility fixup";
					return true;
				}
			}
			for (const StringName &name : function->export_fixups.gds_utilities) {
				if (name == p_name) {
					r_surface = "Foundry utility fixup";
					return true;
				}
			}
			for (const FSFunction::ExportFixups::GlobalStore &global_store :
					function->export_fixups.global_stores) {
				if (global_store.global_name == p_name) {
					r_surface = "global-store/autoload fixup";
					return true;
				}
			}
			for (const StringName &name : function->export_fixups.named_globals) {
				if (name == p_name) {
					r_surface = "named global";
					return true;
				}
			}
		}
		for (const RegistrySnapshot &snapshot : registry_snapshots) {
			for (const FSConformanceRegistry::RuntimeConformance &conformance :
					snapshot.entries) {
				if (external_script_contains_name(
							conformance.target_script, p_name,
							visited_external_scripts, 0)) {
					r_surface = "conformance target script";
					return true;
				}
				if (reflects_methods &&
						conformance.functions.has(p_name)) {
					r_surface = "reflection enumeration";
					return true;
				}
			}
		}
		return false;
	}

	bool validate_protected_names(Vector<Diagnostic> &r_diagnostics) const {
		for (const KeyValue<StringName, StringName> &entry : rename_map) {
			String surface;
			if (!find_protected_surface(entry.key, surface)) {
				continue;
			}
			Diagnostic diagnostic;
			diagnostic.surface = surface;
			diagnostic.source_name = entry.key;
			diagnostic.message =
					"Mapped source occurs on a protected native or dynamic surface.";
			r_diagnostics.push_back(diagnostic);
			return false;
		}
		return true;
	}

	void add_project_source(const StringName &p_name) {
		if (p_name == StringName() || String(p_name).begins_with("@")) {
			return;
		}
		project_sources.insert(p_name);
		observed_names.insert(p_name);
	}

	void collect_observed_variant(const Variant &p_value, int p_depth = 0) {
		const bool is_array = p_value.get_type() == Variant::ARRAY;
		const bool is_dictionary =
				p_value.get_type() == Variant::DICTIONARY;
		const void *container_id = nullptr;
		if (is_array) {
			container_id = Array(p_value).id();
		} else if (is_dictionary) {
			container_id = Dictionary(p_value).id();
		}
		if (container_id != nullptr &&
				observed_variant_containers.has(container_id)) {
			return;
		}
		if (p_depth > Variant::MAX_RECURSION_DEPTH) {
			return;
		}
		if (container_id != nullptr) {
			observed_variant_containers.insert(container_id);
		}
		switch (p_value.get_type()) {
			case Variant::STRING:
				if (!String(p_value).is_empty()) {
					observed_names.insert(StringName(String(p_value)));
				}
				break;
			case Variant::STRING_NAME:
				observed_names.insert(StringName(p_value));
				break;
			case Variant::NODE_PATH: {
				const NodePath path = p_value;
				for (int i = 0; i < path.get_subname_count(); i++) {
					observed_names.insert(path.get_subname(i));
				}
			} break;
			case Variant::CALLABLE: {
				const Callable callable = p_value;
				observed_names.insert(callable.get_method());
				for (const Variant &bound : callable.get_bound_arguments()) {
					collect_observed_variant(bound, p_depth + 1);
				}
			} break;
			case Variant::SIGNAL:
				observed_names.insert(Signal(p_value).get_name());
				break;
			case Variant::ARRAY:
				for (const Variant &value : Array(p_value)) {
					collect_observed_variant(value, p_depth + 1);
				}
				break;
			case Variant::DICTIONARY: {
				const Dictionary dictionary = p_value;
				const Array keys = dictionary.keys();
				for (const Variant &key : keys) {
					collect_observed_variant(key, p_depth + 1);
					collect_observed_variant(dictionary[key], p_depth + 1);
				}
			} break;
			case Variant::PACKED_STRING_ARRAY:
				for (const String &value : PackedStringArray(p_value)) {
					if (!value.is_empty()) {
						observed_names.insert(StringName(value));
					}
				}
				break;
			case Variant::OBJECT: {
				Object *object = p_value;
				FoundryScript *script = Object::cast_to<FoundryScript>(object);
				if (script != nullptr) {
					observed_names.insert(script->local_name);
					observed_names.insert(script->global_name);
					observed_names.insert(
							StringName(script->fully_qualified_name));
				}
			} break;
			default:
				break;
		}
	}

	void collect_observed_method_info(const MethodInfo &p_info) {
		if (!p_info.name.is_empty()) {
			observed_names.insert(StringName(p_info.name));
		}
		for (const PropertyInfo &argument : p_info.arguments) {
			observed_names.insert(StringName(argument.name));
		}
		for (const Variant &default_argument : p_info.default_arguments) {
			collect_observed_variant(default_argument);
		}
	}

	void collect_observed_property_info(const PropertyInfo &p_info) {
		if (!p_info.name.is_empty()) {
			observed_names.insert(StringName(p_info.name));
		}
		if (p_info.class_name != StringName()) {
			observed_names.insert(p_info.class_name);
		}
		if (is_type_bearing_hint(p_info.hint) &&
				!p_info.hint_string.is_empty()) {
			observed_names.insert(StringName(p_info.hint_string));
		}
	}

	void collect_observed_annotations(
			const Vector<FoundryScript::AnnotationUsage> &p_usages) {
		for (const FoundryScript::AnnotationUsage &usage : p_usages) {
			observed_names.insert(usage.name);
			observed_names.insert(usage.qualified_name);
			collect_observed_variant(usage.args);
			collect_observed_variant(usage.kwargs);
		}
	}

	void collect_map_surfaces() {
		project_sources.clear();
		observed_names.clear();
		observed_variant_containers.clear();

		for (const ClassSnapshot &snapshot : class_snapshots) {
			add_project_source(snapshot.local_name);
			observed_names.insert(snapshot.global_name);
			observed_names.insert(StringName(snapshot.fully_qualified_name));
			observed_names.insert(snapshot.trait_type_name);
			for (const StringName &member : snapshot.members) {
				add_project_source(member);
			}
			for (const KeyValue<StringName, FoundryScript::MemberInfo> &member :
					snapshot.member_indices) {
				add_project_source(member.key);
				observed_names.insert(member.value.setter);
				observed_names.insert(member.value.getter);
				collect_observed_property_info(member.value.property_info);
			}
			for (const KeyValue<StringName, FoundryScript::MemberInfo> &member :
					snapshot.static_variables_indices) {
				add_project_source(member.key);
				collect_observed_property_info(member.value.property_info);
			}
			for (const KeyValue<StringName, Variant> &constant :
					snapshot.constants) {
				add_project_source(constant.key);
				collect_observed_variant(constant.value);
			}
			for (const KeyValue<StringName, FSFunction *> &function :
					snapshot.member_functions) {
				add_project_source(function.key);
			}
			for (const KeyValue<StringName, FoundryScript::EnumFunctionSet>
							&enum_entry :
					snapshot.enum_functions) {
				add_project_source(enum_entry.key);
				for (const KeyValue<StringName, FSFunction *> &function :
						enum_entry.value.instance_functions) {
					add_project_source(function.key);
				}
				for (const KeyValue<StringName, FSFunction *> &function :
						enum_entry.value.static_functions) {
					add_project_source(function.key);
				}
			}
			for (const KeyValue<StringName, Ref<FoundryScript>> &subclass :
					snapshot.subclasses) {
				add_project_source(subclass.key);
			}
			for (const KeyValue<StringName, MethodInfo> &signal :
					snapshot.signals) {
				add_project_source(signal.key);
				collect_observed_method_info(signal.value);
			}
			for (const StringName &trait_name : snapshot.script_trait_list) {
				observed_names.insert(trait_name);
			}
			for (const KeyValue<StringName,
						 FoundryScript::AbstractTraitRequirement> &requirement :
					snapshot.abstract_trait_requirements) {
				add_project_source(requirement.key);
				collect_observed_method_info(requirement.value.method_info);
			}
			for (const FoundryScript::TypeParameter &type_parameter :
					snapshot.type_parameters) {
				observed_names.insert(type_parameter.name);
			}
			for (const KeyValue<StringName,
						 Vector<FoundryScript::AnnotationUsage>> &annotations :
					snapshot.method_annotations) {
				observed_names.insert(annotations.key);
				collect_observed_annotations(annotations.value);
			}
			for (const KeyValue<StringName,
						 Vector<FoundryScript::AnnotationUsage>> &annotations :
					snapshot.variable_annotations) {
				observed_names.insert(annotations.key);
				collect_observed_annotations(annotations.value);
			}
			for (const KeyValue<StringName,
						 Vector<FoundryScript::AnnotationUsage>> &annotations :
					snapshot.signal_annotations) {
				observed_names.insert(annotations.key);
				collect_observed_annotations(annotations.value);
			}
			for (const KeyValue<StringName,
						 Vector<FoundryScript::AnnotationUsage>> &annotations :
					snapshot.constant_annotations) {
				observed_names.insert(annotations.key);
				collect_observed_annotations(annotations.value);
			}
			const auto collect_parameter_annotations =
					[&](const HashMap<StringName,
							HashMap<StringName,
									Vector<FoundryScript::AnnotationUsage>>>
									&p_annotations) {
						for (const KeyValue<StringName,
									 HashMap<StringName,
											 Vector<FoundryScript::AnnotationUsage>>>
										&owner : p_annotations) {
							observed_names.insert(owner.key);
							for (const KeyValue<StringName,
										 Vector<FoundryScript::AnnotationUsage>>
											&parameter : owner.value) {
								observed_names.insert(parameter.key);
								collect_observed_annotations(parameter.value);
							}
						}
					};
			collect_parameter_annotations(
					snapshot.method_parameter_annotations);
			collect_parameter_annotations(
					snapshot.signal_parameter_annotations);
			collect_observed_annotations(snapshot.script->class_annotations);
			for (const Variant &value : snapshot.script->static_variables) {
				collect_observed_variant(value);
			}
			for (const KeyValue<StringName, Variant> &value :
					snapshot.member_default_values) {
				observed_names.insert(value.key);
				collect_observed_variant(value.value);
			}
			for (const KeyValue<StringName, FoundryScript::MemberInfo> &member :
					snapshot.old_static_variables_indices) {
				observed_names.insert(member.key);
				collect_observed_property_info(member.value.property_info);
			}
			for (const KeyValue<StringName, int> &member :
					snapshot.member_lines) {
				observed_names.insert(member.key);
			}
			for (const PropertyInfo &property : snapshot.members_cache) {
				collect_observed_property_info(property);
			}
			for (const KeyValue<StringName, Variant> &value :
					snapshot.member_default_values_cache) {
				observed_names.insert(value.key);
				collect_observed_variant(value.value);
			}
		}

		for (const FunctionSnapshot &snapshot : function_snapshots) {
			observed_names.insert(snapshot.name);
			collect_observed_method_info(snapshot.method_info);
			for (const StringName &global_name : snapshot.global_names) {
				observed_names.insert(global_name);
			}
			for (const Variant &constant : snapshot.function->constants) {
				collect_observed_variant(constant);
			}
			for (const FSFunction::ExportFixups::TypedNameKey &key :
					snapshot.function->export_fixups.setters) {
				observed_names.insert(key.name);
			}
			for (const FSFunction::ExportFixups::TypedNameKey &key :
					snapshot.function->export_fixups.getters) {
				observed_names.insert(key.name);
			}
			for (const FSFunction::ExportFixups::TypedNameKey &key :
					snapshot.function->export_fixups.builtin_methods) {
				observed_names.insert(key.name);
			}
			for (const FSFunction::ExportFixups::MethodBindKey &key :
					snapshot.function->export_fixups.method_binds) {
				observed_names.insert(key.class_name);
				observed_names.insert(key.method_name);
			}
			for (const FSFunction::ExportFixups::GlobalStore &store :
					snapshot.function->export_fixups.global_stores) {
				observed_names.insert(store.global_name);
			}
			for (const StringName &name :
					snapshot.function->export_fixups.named_globals) {
				observed_names.insert(name);
			}
		}

		for (const RegistrySnapshot &snapshot : registry_snapshots) {
			for (const FSConformanceRegistry::Conformance &conformance :
					snapshot.conformances) {
				observed_names.insert(conformance.trait_name);
				for (const String &target_key : conformance.target_keys) {
					observed_names.insert(StringName(target_key));
				}
				for (const KeyValue<StringName, FSParser::FunctionNode *> &witness :
						conformance.witnesses) {
					add_project_source(witness.key);
				}
			}
			for (const FSConformanceRegistry::RuntimeConformance &conformance :
					snapshot.entries) {
				observed_names.insert(conformance.trait_name);
				for (const String &target_key : conformance.target_keys) {
					observed_names.insert(StringName(target_key));
				}
				for (const KeyValue<StringName, FSFunction *> &witness :
						conformance.functions) {
					add_project_source(witness.key);
				}
			}
		}
	}

	static void add_map_diagnostic(
			Vector<Diagnostic> &r_diagnostics,
			const String &p_surface,
			const StringName &p_source_name,
			const String &p_message) {
		Diagnostic diagnostic;
		diagnostic.surface = p_surface;
		diagnostic.source_name = p_source_name;
		diagnostic.message = p_message;
		r_diagnostics.push_back(diagnostic);
	}

	bool validate_map_shape(Vector<Diagnostic> &r_diagnostics) const {
		HashMap<StringName, StringName> target_sources;
		for (const KeyValue<StringName, StringName> &entry : rename_map) {
			const String source = entry.key;
			const String replacement = entry.value;
			if (source.is_empty() || !source.is_valid_unicode_identifier()) {
				add_map_diagnostic(r_diagnostics, "rename map", entry.key,
						"Source must be a non-empty Unicode identifier, not a composite identity.");
				return false;
			}
			if (replacement.is_empty() ||
					!replacement.is_valid_unicode_identifier()) {
				add_map_diagnostic(r_diagnostics, "rename map replacement",
						entry.key,
						vformat(
								"`%s` must be a non-empty Unicode identifier, not a composite identity.",
								replacement));
				return false;
			}
			if (entry.key == entry.value) {
				add_map_diagnostic(r_diagnostics, "rename map", entry.key,
						"Identity mappings are not allowed.");
				return false;
			}
			const StringName *existing_source =
					target_sources.getptr(entry.value);
			if (existing_source != nullptr) {
				add_map_diagnostic(r_diagnostics, "rename map collision",
						entry.key,
						vformat(
								"Replacement `%s` is already assigned to source `%s`.",
								entry.value, *existing_source));
				return false;
			}
			target_sources.insert(entry.value, entry.key);
		}
		return true;
	}

	bool validate_map_surfaces(Vector<Diagnostic> &r_diagnostics) {
		collect_map_surfaces();
		for (const KeyValue<StringName, StringName> &entry : rename_map) {
			if (!project_sources.has(entry.key)) {
				add_map_diagnostic(r_diagnostics, "rename map", entry.key,
						"Source does not occur on a project declaration or dispatch surface in the closed graph.");
				return false;
			}
			if (observed_names.has(entry.value)) {
				add_map_diagnostic(r_diagnostics, "rename collision", entry.key,
						vformat(
								"Replacement `%s` collides with an observed serialized identifier.",
								entry.value));
				return false;
			}
			String protected_surface;
			if (find_protected_surface(entry.value, protected_surface)) {
				add_map_diagnostic(r_diagnostics, "rename collision", entry.key,
						vformat(
								"Replacement `%s` collides with protected %s.",
								entry.value, protected_surface));
				return false;
			}
		}

		HashSet<String> transformed_fully_qualified_names;
		HashSet<StringName> transformed_global_names;
		for (const ClassSnapshot &snapshot : class_snapshots) {
			if (transformed_fully_qualified_names.has(
						snapshot.transformed_fully_qualified_name)) {
				add_map_diagnostic(r_diagnostics, "class identity collision",
						snapshot.local_name,
						vformat(
								"More than one class would use fully-qualified identity `%s`.",
								snapshot.transformed_fully_qualified_name));
				return false;
			}
			transformed_fully_qualified_names.insert(
					snapshot.transformed_fully_qualified_name);
			if (snapshot.transformed_global_name != StringName()) {
				if (transformed_global_names.has(
							snapshot.transformed_global_name)) {
					add_map_diagnostic(r_diagnostics, "class identity collision",
							snapshot.local_name,
							vformat(
									"More than one class would use global identity `%s`.",
									snapshot.transformed_global_name));
					return false;
				}
				transformed_global_names.insert(snapshot.transformed_global_name);
			}
		}
		return true;
	}

	void snapshot_functions() {
		for (const ClassSnapshot &class_snapshot : class_snapshots) {
			for (const KeyValue<StringName, FSFunction *> &function : class_snapshot.member_functions) {
				snapshot_function(function.value, function.key);
			}
			for (const KeyValue<StringName, FoundryScript::EnumFunctionSet> &enum_entry :
					class_snapshot.enum_functions) {
				for (const KeyValue<StringName, FSFunction *> &function :
						enum_entry.value.instance_functions) {
					snapshot_function(function.value, function.key);
				}
				for (const KeyValue<StringName, FSFunction *> &function :
						enum_entry.value.static_functions) {
					snapshot_function(function.value, function.key);
				}
			}
			snapshot_function(class_snapshot.script->implicit_initializer, StringName());
			snapshot_function(class_snapshot.script->implicit_ready, StringName());
			snapshot_function(class_snapshot.script->static_initializer, StringName());
			for (FSFunction *witness : class_snapshot.script->witness_functions) {
				snapshot_function(witness, witness != nullptr ? witness->name : StringName());
			}
		}
	}

	bool snapshot_registries(Vector<Diagnostic> &r_diagnostics) {
		RBMap<String, bool> sources;
		for (const ClassSnapshot &snapshot : class_snapshots) {
			if (!snapshot.script->registered_conformance_source.is_empty()) {
				sources.insert(snapshot.script->registered_conformance_source, true);
			}
		}

		FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
		for (const KeyValue<String, bool> &source : sources) {
			RegistrySnapshot snapshot;
			snapshot.source = source.key;
			snapshot.conformances =
					registry->get_file_conformances(snapshot.source);
			snapshot.entries = registry->get_runtime_witnesses(snapshot.source);
			for (const FSConformanceRegistry::RuntimeConformance &conformance :
					snapshot.entries) {
				if (conformance.target_script == nullptr) {
					Diagnostic diagnostic;
					diagnostic.surface = "conformance registry";
					diagnostic.source_name = conformance.trait_name;
					diagnostic.message = vformat(
							"Runtime conformance source `%s` has no target script.",
							snapshot.source);
					r_diagnostics.push_back(diagnostic);
					return false;
				}
				for (const KeyValue<StringName, FSFunction *> &witness :
						conformance.functions) {
					if (witness.value == nullptr) {
						Diagnostic diagnostic;
						diagnostic.surface = "conformance registry";
						diagnostic.source_name = witness.key;
						diagnostic.message = vformat(
								"Runtime conformance source `%s` contains a null witness.",
								snapshot.source);
						r_diagnostics.push_back(diagnostic);
						return false;
					}
					snapshot_function(witness.value, witness.key);
				}
			}
			registry_snapshots.push_back(snapshot);
		}
		return true;
	}

	bool build_registry_plan(Vector<Diagnostic> &r_diagnostics) {
		const auto same_target_keys =
				[](const Vector<String> &p_left,
						const Vector<String> &p_right) {
					if (p_left.size() != p_right.size()) {
						return false;
					}
					Vector<String> left = p_left;
					Vector<String> right = p_right;
					left.sort();
					right.sort();
					return left == right;
				};
		const auto same_witness_names =
				[](const FSConformanceRegistry::WitnessFunctionMap
								&p_runtime,
						const FSConformanceRegistry::WitnessMap &p_parse) {
					if (p_runtime.size() != p_parse.size()) {
						return false;
					}
					for (const KeyValue<StringName, FSFunction *> &witness :
							p_runtime) {
						if (!p_parse.has(witness.key)) {
							return false;
						}
					}
					return true;
				};
		const auto add_correlation_diagnostic =
				[&](const RegistrySnapshot &p_snapshot,
						const StringName &p_trait,
						const String &p_message) {
					Diagnostic diagnostic;
					diagnostic.surface = "conformance registry";
					diagnostic.source_name = p_trait;
					diagnostic.message = vformat(
							"Runtime/parse conformance correlation for source "
							"`%s` failed: %s",
							p_snapshot.source, p_message);
					r_diagnostics.push_back(diagnostic);
				};

		for (RegistrySnapshot &snapshot : registry_snapshots) {
			snapshot.transformed_conformances = snapshot.conformances;
			for (FSConformanceRegistry::Conformance &conformance :
					snapshot.transformed_conformances) {
				for (String &target_key : conformance.target_keys) {
					target_key = rewrite_identity(target_key);
				}
				conformance.trait_name =
						StringName(rewrite_identity(String(conformance.trait_name)));
				FSConformanceRegistry::WitnessMap transformed_witnesses;
				for (const KeyValue<StringName, FSParser::FunctionNode *> &witness :
						conformance.witnesses) {
					transformed_witnesses.insert(
							rename_atomic(witness.key), witness.value);
				}
				conformance.witnesses = transformed_witnesses;
			}

			snapshot.transformed_entries.clear();
			HashSet<int> matched_parse_entries;
			for (const FSConformanceRegistry::RuntimeConformance
							&runtime_conformance :
					snapshot.entries) {
				Vector<int> candidates;
				HashSet<StringName> candidate_traits;
				for (int parse_index = 0;
						parse_index < snapshot.conformances.size();
						parse_index++) {
					const FSConformanceRegistry::Conformance
							&parse_conformance =
									snapshot.conformances[parse_index];
					if (runtime_conformance.trait_name != StringName() &&
							runtime_conformance.trait_name !=
									parse_conformance.trait_name) {
						continue;
					}
					if (!same_target_keys(
								runtime_conformance.target_keys,
								parse_conformance.target_keys) ||
							!same_witness_names(
									runtime_conformance.functions,
									parse_conformance.witnesses)) {
						continue;
					}
					if (candidate_traits.has(
								parse_conformance.trait_name)) {
						add_correlation_diagnostic(
								snapshot,
								parse_conformance.trait_name,
								"more than one parse entry has the same "
								"matching trait identity.");
						return false;
					}
					candidate_traits.insert(
							parse_conformance.trait_name);
					candidates.push_back(parse_index);
				}

				if (candidates.is_empty()) {
					add_correlation_diagnostic(
							snapshot,
							runtime_conformance.trait_name,
							"no parse entry has the same target aliases and "
							"witness names.");
					return false;
				}
				if (runtime_conformance.trait_name != StringName() &&
						candidates.size() != 1) {
					add_correlation_diagnostic(
							snapshot,
							runtime_conformance.trait_name,
							"an explicit runtime trait identity did not "
							"select exactly one parse entry.");
					return false;
				}

				for (const int parse_index : candidates) {
					const FSConformanceRegistry::Conformance
							&parse_conformance =
									snapshot.conformances[parse_index];
					if (matched_parse_entries.has(parse_index)) {
						add_correlation_diagnostic(
								snapshot,
								parse_conformance.trait_name,
								"more than one runtime entry matches the same "
								"parse entry.");
						return false;
					}
					matched_parse_entries.insert(parse_index);

					FSConformanceRegistry::RuntimeConformance transformed =
							runtime_conformance;
					for (String &target_key : transformed.target_keys) {
						target_key = rewrite_identity(target_key);
					}
					transformed.trait_name =
							snapshot.transformed_conformances[parse_index]
									.trait_name;
					HashMap<StringName, FSFunction *>
							transformed_functions;
					for (const KeyValue<StringName, FSFunction *> &witness :
							runtime_conformance.functions) {
						transformed_functions.insert(
								rename_atomic(witness.key),
								witness.value);
					}
					transformed.functions = transformed_functions;
					snapshot.transformed_entries.push_back(transformed);
				}
			}

			for (int parse_index = 0;
					parse_index < snapshot.conformances.size();
					parse_index++) {
				const FSConformanceRegistry::Conformance &conformance =
						snapshot.conformances[parse_index];
				if (!matched_parse_entries.has(parse_index)) {
					add_correlation_diagnostic(
							snapshot, conformance.trait_name,
							"a parse entry has no matching runtime entry.");
					return false;
				}
			}
		}
		return true;
	}

	FoundryScript *get_root_script(FoundryScript *p_script) const {
		if (p_script == nullptr) {
			return nullptr;
		}
		while (p_script->_owner != nullptr) {
			p_script = p_script->_owner;
		}
		return p_script;
	}

	bool validate_script_dependency(
			Script *p_dependency,
			const FoundryScript *p_referring,
			const String &p_surface,
			const HashSet<const FoundryScript *> &p_roots,
			Vector<Diagnostic> &r_diagnostics) const {
		FoundryScript *dependency = Object::cast_to<FoundryScript>(p_dependency);
		if (dependency == nullptr) {
			return true;
		}
		FoundryScript *dependency_root = get_root_script(dependency);
		if (dependency_root != nullptr && p_roots.has(dependency_root)) {
			return true;
		}

		Diagnostic diagnostic;
		diagnostic.surface = "closed graph";
		diagnostic.source_name =
				p_referring != nullptr ? p_referring->local_name : StringName();
		const String dependency_identity =
				dependency->get_script_path() + "::" +
				dependency->get_fully_qualified_name();
		diagnostic.message = vformat(
				"The closed graph omits Foundry Script dependency `%s` referenced by `%s` on %s.",
				dependency_identity,
				p_referring != nullptr ? p_referring->get_fully_qualified_name()
									   : String("<registry>"),
				p_surface);
		r_diagnostics.push_back(diagnostic);
		return false;
	}

	bool validate_container_type_closure(
			const ContainerType &p_type,
			const FoundryScript *p_referring,
			const String &p_surface,
			const HashSet<const FoundryScript *> &p_roots,
			Vector<Diagnostic> &r_diagnostics,
			int p_depth = 0) const {
		if (p_depth > Variant::MAX_RECURSION_DEPTH) {
			add_map_diagnostic(
					r_diagnostics, "closed graph",
					p_referring != nullptr ? p_referring->local_name
										   : StringName(),
					vformat(
							"%s exceeds the supported container-type "
							"recursion depth.",
							p_surface));
			return false;
		}
		if (p_type.script.is_valid() &&
				!validate_script_dependency(p_type.script.ptr(), p_referring,
						p_surface, p_roots, r_diagnostics)) {
			return false;
		}
		for (const ContainerType &element_type : p_type.element_types) {
			if (!validate_container_type_closure(element_type, p_referring,
						p_surface, p_roots, r_diagnostics, p_depth + 1)) {
				return false;
			}
		}
		for (const ContainerType &type_argument : p_type.type_arguments) {
			if (!validate_container_type_closure(type_argument, p_referring,
						p_surface, p_roots, r_diagnostics, p_depth + 1)) {
				return false;
			}
		}
		return true;
	}

	bool validate_data_type_closure(
			const FSDataType &p_type,
			const FoundryScript *p_referring,
			const String &p_surface,
			const HashSet<const FoundryScript *> &p_roots,
			Vector<Diagnostic> &r_diagnostics,
			int p_depth = 0) const {
		if (p_depth > Variant::MAX_RECURSION_DEPTH) {
			add_map_diagnostic(
					r_diagnostics, "closed graph",
					p_referring != nullptr ? p_referring->local_name
										   : StringName(),
					vformat(
							"%s exceeds the supported data-type recursion "
							"depth.",
							p_surface));
			return false;
		}
		if (p_type.script_type != nullptr &&
				!validate_script_dependency(p_type.script_type, p_referring,
						p_surface, p_roots, r_diagnostics)) {
			return false;
		}
		if (p_type.script_type_ref.is_valid() &&
				!validate_script_dependency(p_type.script_type_ref.ptr(),
						p_referring, p_surface, p_roots, r_diagnostics)) {
			return false;
		}
		for (const FSDataType &element_type : p_type.container_element_types) {
			if (!validate_data_type_closure(element_type, p_referring, p_surface,
						p_roots, r_diagnostics, p_depth + 1)) {
				return false;
			}
		}
		for (const FSDataType &type_argument : p_type.type_arguments) {
			if (!validate_data_type_closure(type_argument, p_referring, p_surface,
						p_roots, r_diagnostics, p_depth + 1)) {
				return false;
			}
		}
		return true;
	}

	bool validate_variant_closure(
			const Variant &p_value,
			const FoundryScript *p_referring,
			const String &p_surface,
			const HashSet<const FoundryScript *> &p_roots,
			Vector<Diagnostic> &r_diagnostics,
			int p_depth = 0) {
		const bool is_array = p_value.get_type() == Variant::ARRAY;
		const bool is_dictionary =
				p_value.get_type() == Variant::DICTIONARY;
		const void *container_id = nullptr;
		if (is_array) {
			container_id = Array(p_value).id();
		} else if (is_dictionary) {
			container_id = Dictionary(p_value).id();
		}
		if (container_id != nullptr &&
				validated_variant_containers.has(container_id)) {
			return true;
		}
		if (p_depth > Variant::MAX_RECURSION_DEPTH) {
			unscannable_protected_surface = true;
			return true;
		}
		if (container_id != nullptr) {
			validated_variant_containers.insert(container_id);
		}
		switch (p_value.get_type()) {
			case Variant::ARRAY: {
				const Array array = p_value;
				if (!validate_container_type_closure(array.get_element_type(),
							p_referring, p_surface, p_roots, r_diagnostics)) {
					return false;
				}
				for (const Variant &value : array) {
					if (!validate_variant_closure(value, p_referring, p_surface,
								p_roots, r_diagnostics, p_depth + 1)) {
						return false;
					}
				}
			} break;
			case Variant::DICTIONARY: {
				const Dictionary dictionary = p_value;
				if (!validate_container_type_closure(dictionary.get_key_type(),
							p_referring, p_surface, p_roots, r_diagnostics) ||
						!validate_container_type_closure(
								dictionary.get_value_type(), p_referring,
								p_surface, p_roots, r_diagnostics)) {
					return false;
				}
				const Array keys = dictionary.keys();
				for (const Variant &key : keys) {
					if (!validate_variant_closure(key, p_referring, p_surface,
								p_roots, r_diagnostics, p_depth + 1) ||
							!validate_variant_closure(dictionary[key], p_referring,
									p_surface, p_roots, r_diagnostics,
									p_depth + 1)) {
						return false;
					}
				}
			} break;
			case Variant::OBJECT: {
				Object *object = p_value;
				if (Script *script = Object::cast_to<Script>(object)) {
					return validate_script_dependency(script, p_referring,
							p_surface, p_roots, r_diagnostics);
				}
				if (FSSpecializedClassHandle *specialized =
								Object::cast_to<FSSpecializedClassHandle>(object)) {
					if (!validate_script_dependency(
								specialized->get_specialized_script().ptr(),
								p_referring, p_surface, p_roots, r_diagnostics)) {
						return false;
					}
					for (const ContainerType &type_argument :
							specialized->get_type_arguments()) {
						if (!validate_container_type_closure(type_argument,
									p_referring, p_surface, p_roots,
									r_diagnostics)) {
							return false;
						}
					}
				}
			} break;
			default:
				break;
		}
		return true;
	}

	bool validate_annotation_usages_closure(
			const Vector<FoundryScript::AnnotationUsage> &p_usages,
			const FoundryScript *p_referring,
			const String &p_surface,
			const HashSet<const FoundryScript *> &p_roots,
			Vector<Diagnostic> &r_diagnostics) {
		for (const FoundryScript::AnnotationUsage &usage : p_usages) {
			if (!validate_variant_closure(usage.args, p_referring, p_surface,
						p_roots, r_diagnostics) ||
					!validate_variant_closure(usage.kwargs, p_referring,
							p_surface, p_roots, r_diagnostics)) {
				return false;
			}
		}
		return true;
	}

	bool identity_is_included(const String &p_identity) const {
		if (p_identity.is_empty()) {
			return false;
		}
		if (included_identity_sources.has(p_identity)) {
			return true;
		}
		for (const ClassSnapshot &snapshot : class_snapshots) {
			if (p_identity == snapshot.script->get_script_path() ||
					p_identity == String(snapshot.local_name) ||
					p_identity == String(snapshot.global_name) ||
					p_identity == snapshot.fully_qualified_name ||
					p_identity == String(snapshot.trait_type_name)) {
				return true;
			}
			const String script_prefix =
					snapshot.script->get_script_path() + "::";
			if (!snapshot.script->get_script_path().is_empty() &&
					snapshot.fully_qualified_name.begins_with(
							script_prefix) &&
					p_identity ==
							snapshot.fully_qualified_name.substr(
									script_prefix.length())) {
				return true;
			}
		}
		const StringName identity_name = p_identity;
		if (!ScriptServer::is_global_class(identity_name)) {
			return false;
		}
		const String global_path =
				ScriptServer::get_global_class_path(identity_name);
		if (global_path.is_empty()) {
			return false;
		}
		for (const ClassSnapshot &snapshot : class_snapshots) {
			if (FoundryScript::is_canonically_equal_paths(
						global_path, snapshot.script->get_script_path())) {
				return true;
			}
		}
		return false;
	}

	static bool identity_is_native_or_builtin(const String &p_identity) {
		return ClassDB::class_exists(StringName(p_identity)) ||
				Variant::get_type_by_name(p_identity) < Variant::VARIANT_MAX;
	}

	bool validate_identity_dependency(
			const String &p_identity,
			const FoundryScript *p_referring,
			const String &p_surface,
			Vector<Diagnostic> &r_diagnostics,
			bool p_allow_native_or_builtin = false) const {
		if (p_identity.is_empty() || identity_is_included(p_identity) ||
				(p_allow_native_or_builtin &&
						identity_is_native_or_builtin(p_identity))) {
			return true;
		}
		Diagnostic diagnostic;
		diagnostic.surface = "closed graph";
		diagnostic.source_name =
				p_referring != nullptr ? p_referring->local_name : StringName();
		diagnostic.message = vformat(
				"The closed graph cannot resolve %s identity `%s` referenced by `%s`.",
				p_surface, p_identity,
				p_referring != nullptr ? p_referring->get_fully_qualified_name()
									   : String("<registry>"));
		r_diagnostics.push_back(diagnostic);
		return false;
	}

	bool validate_textual_type_identity(
			const String &p_identity,
			const FoundryScript *p_referring,
			const String &p_surface,
			Vector<Diagnostic> &r_diagnostics) const {
		String identity = p_identity;
		if (identity.ends_with("?")) {
			identity = identity.left(identity.length() - 1);
		}
		if (identity.is_empty() || identity_is_included(identity) ||
				identity_is_native_or_builtin(identity) ||
				!ScriptServer::is_global_class(StringName(identity))) {
			return true;
		}
		if (FSLanguage::get_singleton() == nullptr ||
				ScriptServer::get_global_class_language(StringName(identity)) !=
						FSLanguage::get_singleton()->get_name()) {
			return true;
		}
		return validate_identity_dependency(identity, p_referring, p_surface,
				r_diagnostics);
	}

	bool validate_property_info_closure(
			const PropertyInfo &p_info,
			const FoundryScript *p_referring,
			const String &p_surface,
			Vector<Diagnostic> &r_diagnostics) const {
		if (!validate_textual_type_identity(String(p_info.class_name),
					p_referring, p_surface, r_diagnostics)) {
			return false;
		}
		if ((p_info.usage &
					(PROPERTY_USAGE_CLASS_IS_ENUM |
							PROPERTY_USAGE_CLASS_IS_BITFIELD)) &&
				p_info.class_name != StringName()) {
			const String enum_identity = p_info.class_name;
			LocalVector<StringName> global_classes;
			ScriptServer::get_global_class_list(global_classes);
			String longest_owner;
			for (const StringName &global_class : global_classes) {
				const String candidate = global_class;
				if ((enum_identity == candidate ||
							enum_identity.begins_with(candidate + ".")) &&
						candidate.length() > longest_owner.length()) {
					longest_owner = candidate;
				}
			}
			if (!longest_owner.is_empty() &&
					!validate_textual_type_identity(longest_owner,
							p_referring, p_surface, r_diagnostics)) {
				return false;
			}
		}
		if (!is_type_bearing_hint(p_info.hint) || p_info.hint_string.is_empty()) {
			return true;
		}

		LocalVector<StringName> global_classes;
		ScriptServer::get_global_class_list(global_classes);
		for (const StringName &global_class : global_classes) {
			const String identity = global_class;
			int offset = 0;
			while (offset <=
					p_info.hint_string.length() - identity.length()) {
				const int found = p_info.hint_string.find(identity, offset);
				if (found < 0) {
					break;
				}
				if (has_identity_boundaries(p_info.hint_string, found,
							identity.length()) &&
						!validate_textual_type_identity(identity, p_referring,
								p_surface, r_diagnostics)) {
					return false;
				}
				offset = found + identity.length();
			}
		}
		return true;
	}

	bool validate_method_info_closure(
			const MethodInfo &p_info,
			const FoundryScript *p_referring,
			const String &p_surface,
			Vector<Diagnostic> &r_diagnostics) const {
		if (!validate_property_info_closure(p_info.return_val, p_referring,
					p_surface, r_diagnostics)) {
			return false;
		}
		for (const PropertyInfo &argument : p_info.arguments) {
			if (!validate_property_info_closure(argument, p_referring, p_surface,
						r_diagnostics)) {
				return false;
			}
		}
		return true;
	}

	bool validate_closed_graph(
			const HashSet<const FoundryScript *> &p_roots,
			Vector<Diagnostic> &r_diagnostics) {
		validated_variant_containers.clear();
		for (const ClassSnapshot &snapshot : class_snapshots) {
			const FoundryScript *script = snapshot.script.ptr();
			if (!validate_variant_closure(script->rpc_config, script,
						"RPC configuration", p_roots, r_diagnostics)) {
				return false;
			}
			if (script->base.is_valid() &&
					!validate_script_dependency(script->base.ptr(), script, "base",
							p_roots, r_diagnostics)) {
				return false;
			}
			for (const KeyValue<FoundryScript *,
						 Vector<FoundryScript::TypeArgumentBinding>> &ancestor :
					snapshot.type_parameter_bindings_by_ancestor) {
				if (!validate_script_dependency(ancestor.key, script,
							"ancestor type binding", p_roots, r_diagnostics)) {
					return false;
				}
				for (const FoundryScript::TypeArgumentBinding &binding :
						ancestor.value) {
					if (!validate_data_type_closure(binding.fixed, script,
								"ancestor type binding", p_roots,
								r_diagnostics)) {
						return false;
					}
				}
			}
			for (const FoundryScript::TypeArgumentBinding &binding :
					snapshot.member_type_argument_bindings) {
				if (!validate_data_type_closure(binding.fixed, script,
							"member type binding", p_roots, r_diagnostics)) {
					return false;
				}
			}
			for (const KeyValue<StringName, FoundryScript::MemberInfo> &member :
					snapshot.member_indices) {
				if (!validate_data_type_closure(member.value.data_type, script,
							"member type", p_roots, r_diagnostics) ||
						!validate_data_type_closure(
								member.value.type_argument_binding.fixed, script,
								"member type binding", p_roots,
								r_diagnostics) ||
						!validate_property_info_closure(
								member.value.property_info, script,
								"member property type", r_diagnostics)) {
					return false;
				}
			}
			for (const KeyValue<StringName, FoundryScript::MemberInfo> &member :
					snapshot.static_variables_indices) {
				if (!validate_data_type_closure(member.value.data_type, script,
							"static member type", p_roots, r_diagnostics) ||
						!validate_data_type_closure(
								member.value.type_argument_binding.fixed, script,
								"static member type binding", p_roots,
								r_diagnostics) ||
						!validate_property_info_closure(
								member.value.property_info, script,
								"static member property type", r_diagnostics)) {
					return false;
				}
			}
			for (const KeyValue<StringName, FoundryScript::MemberInfo> &member :
					snapshot.old_static_variables_indices) {
				if (!validate_data_type_closure(member.value.data_type, script,
							"old static member type", p_roots, r_diagnostics) ||
						!validate_data_type_closure(
								member.value.type_argument_binding.fixed, script,
								"old static member type binding", p_roots,
								r_diagnostics) ||
						!validate_property_info_closure(
								member.value.property_info, script,
								"old static member property type", r_diagnostics)) {
					return false;
				}
			}
			for (const PropertyInfo &property : snapshot.members_cache) {
				if (!validate_property_info_closure(property, script,
							"cached member property type", r_diagnostics)) {
					return false;
				}
			}
			for (const KeyValue<StringName, MethodInfo> &signal :
					snapshot.signals) {
				if (!validate_method_info_closure(signal.value, script,
							"signal type", r_diagnostics)) {
					return false;
				}
				for (const Variant &default_argument :
						signal.value.default_arguments) {
					if (!validate_variant_closure(default_argument, script,
								"signal default argument", p_roots,
								r_diagnostics)) {
						return false;
					}
				}
			}
			for (const StringName &trait_name : snapshot.script_trait_list) {
				if (!validate_identity_dependency(String(trait_name), script,
							"trait", r_diagnostics)) {
					return false;
				}
			}
			for (const KeyValue<StringName,
						 FoundryScript::AbstractTraitRequirement> &requirement :
					snapshot.abstract_trait_requirements) {
				if (!validate_data_type_closure(requirement.value.return_type,
							script, "abstract requirement type", p_roots,
							r_diagnostics) ||
						!validate_method_info_closure(
								requirement.value.method_info, script,
								"abstract requirement type", r_diagnostics)) {
					return false;
				}
				for (const Variant &default_argument :
						requirement.value.method_info.default_arguments) {
					if (!validate_variant_closure(default_argument, script,
								"abstract requirement default argument", p_roots,
								r_diagnostics)) {
						return false;
					}
				}
			}
			for (const FoundryScript::TypeParameter &type_parameter :
					snapshot.type_parameters) {
				if (!validate_property_info_closure(type_parameter.bound, script,
							"type-parameter bound", r_diagnostics)) {
					return false;
				}
			}
			for (const Variant &value : script->static_variables) {
				if (!validate_variant_closure(value, script, "static value",
							p_roots, r_diagnostics)) {
					return false;
				}
			}
			for (const KeyValue<StringName, Variant> &constant :
					snapshot.constants) {
				if (!validate_variant_closure(constant.value, script,
							"constant value", p_roots, r_diagnostics)) {
					return false;
				}
			}
			for (const KeyValue<StringName, Variant> &default_value :
					snapshot.member_default_values) {
				if (!validate_variant_closure(default_value.value, script,
							"member default value", p_roots, r_diagnostics)) {
					return false;
				}
			}
			for (const KeyValue<StringName, Variant> &default_value :
					snapshot.member_default_values_cache) {
				if (!validate_variant_closure(default_value.value, script,
							"cached member default value", p_roots,
							r_diagnostics)) {
					return false;
				}
			}
			for (const Ref<Script> &target : script->witness_target_scripts) {
				if (!validate_script_dependency(target.ptr(), script,
							"conformance target", p_roots, r_diagnostics)) {
					return false;
				}
			}

			if (!validate_annotation_usages_closure(script->class_annotations,
						script, "class annotation", p_roots, r_diagnostics)) {
				return false;
			}
			const auto validate_annotation_map =
					[&](const HashMap<StringName,
								Vector<FoundryScript::AnnotationUsage>>
									&p_annotations,
							const String &p_surface) {
						for (const KeyValue<StringName,
									 Vector<FoundryScript::AnnotationUsage>>
										&annotations : p_annotations) {
							if (!validate_annotation_usages_closure(
										annotations.value, script, p_surface,
										p_roots, r_diagnostics)) {
								return false;
							}
						}
						return true;
					};
			if (!validate_annotation_map(snapshot.method_annotations,
						"method annotation") ||
					!validate_annotation_map(snapshot.variable_annotations,
							"variable annotation") ||
					!validate_annotation_map(snapshot.signal_annotations,
							"signal annotation") ||
					!validate_annotation_map(snapshot.constant_annotations,
							"constant annotation")) {
				return false;
			}
			const auto validate_parameter_annotation_map =
					[&](const HashMap<StringName,
								HashMap<StringName,
										Vector<FoundryScript::AnnotationUsage>>>
									&p_annotations,
							const String &p_surface) {
						for (const KeyValue<StringName,
									 HashMap<StringName,
											 Vector<FoundryScript::AnnotationUsage>>>
										&owner : p_annotations) {
							if (!validate_annotation_map(owner.value, p_surface)) {
								return false;
							}
						}
						return true;
					};
			if (!validate_parameter_annotation_map(
						snapshot.method_parameter_annotations,
						"method parameter annotation") ||
					!validate_parameter_annotation_map(
							snapshot.signal_parameter_annotations,
							"signal parameter annotation")) {
				return false;
			}
		}

		for (const FunctionSnapshot &snapshot : function_snapshots) {
			const FoundryScript *referring = snapshot.function->_script;
			if (!validate_variant_closure(snapshot.function->rpc_config, referring,
						"function RPC configuration", p_roots, r_diagnostics)) {
				return false;
			}
			if (!validate_script_dependency(snapshot.function->_script, referring,
						"function owner", p_roots, r_diagnostics)) {
				return false;
			}
			for (const FSDataType &argument_type : snapshot.argument_types) {
				if (!validate_data_type_closure(argument_type, referring,
							"function argument type", p_roots, r_diagnostics)) {
					return false;
				}
			}
			if (!validate_data_type_closure(snapshot.return_type, referring,
						"function return type", p_roots, r_diagnostics)) {
				return false;
			}
			if (!validate_method_info_closure(snapshot.method_info, referring,
						"function metadata type", r_diagnostics)) {
				return false;
			}
			for (const Variant &constant : snapshot.function->constants) {
				if (!validate_variant_closure(constant, referring,
							"function constant", p_roots, r_diagnostics)) {
					return false;
				}
			}
			for (const Variant &default_argument :
					snapshot.method_info.default_arguments) {
				if (!validate_variant_closure(default_argument, referring,
							"default argument", p_roots, r_diagnostics)) {
					return false;
				}
			}
		}

		for (const RegistrySnapshot &snapshot : registry_snapshots) {
			for (const FSConformanceRegistry::Conformance &conformance :
					snapshot.conformances) {
				if (!validate_identity_dependency(String(conformance.trait_name),
							nullptr, "conformance trait", r_diagnostics)) {
					return false;
				}
				for (const String &target_key : conformance.target_keys) {
					if (!validate_identity_dependency(target_key, nullptr,
								"conformance target", r_diagnostics, true)) {
						return false;
					}
				}
			}
			for (const FSConformanceRegistry::RuntimeConformance &conformance :
					snapshot.entries) {
				if (!validate_script_dependency(
							conformance.target_script, nullptr,
							"conformance target", p_roots,
							r_diagnostics)) {
					return false;
				}
				if (!validate_identity_dependency(String(conformance.trait_name),
							nullptr, "conformance trait", r_diagnostics)) {
					return false;
				}
				for (const String &target_key : conformance.target_keys) {
					if (!validate_identity_dependency(target_key, nullptr,
								"conformance target", r_diagnostics, true)) {
						return false;
					}
				}
			}
		}
		return true;
	}

	bool prepare(const Vector<Ref<FoundryScript>> &p_scripts,
			const RBMap<StringName, StringName> &p_rename_map,
			Vector<Diagnostic> &r_diagnostics) {
		clear();
		rename_map = p_rename_map;

		if (p_scripts.is_empty()) {
			Diagnostic diagnostic;
			diagnostic.surface = "root graph";
			diagnostic.message = "At least one root script is required.";
			r_diagnostics.push_back(diagnostic);
			return false;
		}

		Vector<Ref<FoundryScript>> roots = p_scripts;
		HashSet<const FoundryScript *> root_set;
		for (const Ref<FoundryScript> &script : roots) {
			if (script.is_null()) {
				Diagnostic diagnostic;
				diagnostic.surface = "root graph";
				diagnostic.message = "Found a null Foundry Script.";
				r_diagnostics.push_back(diagnostic);
				return false;
			}
			if (script->_owner != nullptr) {
				Diagnostic diagnostic;
				diagnostic.surface = "root graph";
				diagnostic.source_name = script->local_name;
				diagnostic.message = "Nested classes cannot be supplied as roots.";
				r_diagnostics.push_back(diagnostic);
				return false;
			}
			if (root_set.has(script.ptr())) {
				Diagnostic diagnostic;
				diagnostic.surface = "root graph";
				diagnostic.source_name = script->local_name;
				diagnostic.message = "Found the same root script more than once.";
				r_diagnostics.push_back(diagnostic);
				return false;
			}
			root_set.insert(script.ptr());
		}
		if (!validate_map_shape(r_diagnostics)) {
			return false;
		}

		roots.sort_custom<ScriptRefComparator>();
		for (const Ref<FoundryScript> &root : roots) {
			if (!snapshot_class(root, nullptr, r_diagnostics)) {
				return false;
			}
		}
		index_global_protected_names();
		for (const ClassSnapshot &snapshot : class_snapshots) {
			const String path = snapshot.script->get_script_path();
			if (!path.is_empty() && !script_paths.has(path)) {
				script_paths.push_back(path);
			}
		}
		script_paths.sort();
		if (!build_identity_plan(r_diagnostics)) {
			return false;
		}
		if (!snapshot_registries(r_diagnostics)) {
			return false;
		}
		snapshot_functions();
		if (!validate_closed_graph(root_set, r_diagnostics)) {
			return false;
		}
		if (!validate_protected_names(r_diagnostics)) {
			return false;
		}
		if (!validate_map_surfaces(r_diagnostics)) {
			return false;
		}
		return build_registry_plan(r_diagnostics);
	}

	void stage_class(ClassSnapshot &snapshot) {
		FoundryScript *script = snapshot.script.ptr();
		script->local_name = snapshot.transformed_local_name;
		script->global_name = snapshot.transformed_global_name;
		script->fully_qualified_name = snapshot.transformed_fully_qualified_name;
		script->trait_type_name = snapshot.transformed_trait_type_name;

		script->member_indices.clear();
		for (const KeyValue<StringName, FoundryScript::MemberInfo> &entry : snapshot.member_indices) {
			const StringName key = rename_atomic(entry.key);
			FoundryScript::MemberInfo info = entry.value;
			rewrite_member_info(info, key);
			script->member_indices.insert(key, info);
		}

		script->members.clear();
		for (const StringName &member : snapshot.members) {
			script->members.insert(rename_atomic(member));
		}

		script->member_type_argument_bindings = snapshot.member_type_argument_bindings;
		for (FoundryScript::TypeArgumentBinding &binding : script->member_type_argument_bindings) {
			rewrite_type_argument_binding(binding);
		}

		script->type_parameter_bindings_by_ancestor =
				snapshot.type_parameter_bindings_by_ancestor;
		for (KeyValue<FoundryScript *, Vector<FoundryScript::TypeArgumentBinding>> &entry :
				script->type_parameter_bindings_by_ancestor) {
			for (FoundryScript::TypeArgumentBinding &binding : entry.value) {
				rewrite_type_argument_binding(binding);
			}
		}

		script->static_variables_indices.clear();
		for (const KeyValue<StringName, FoundryScript::MemberInfo> &entry :
				snapshot.static_variables_indices) {
			const StringName key = rename_atomic(entry.key);
			FoundryScript::MemberInfo info = entry.value;
			rewrite_member_info(info, key);
			script->static_variables_indices.insert(key, info);
		}

		script->constants = rewrite_atomic_key_map(snapshot.constants);
		script->member_functions = rewrite_atomic_key_map(snapshot.member_functions);

		script->enum_functions.clear();
		for (const KeyValue<StringName, FoundryScript::EnumFunctionSet> &enum_entry :
				snapshot.enum_functions) {
			FoundryScript::EnumFunctionSet transformed;
			transformed.instance_functions =
					rewrite_atomic_key_map(enum_entry.value.instance_functions);
			transformed.static_functions =
					rewrite_atomic_key_map(enum_entry.value.static_functions);
			script->enum_functions.insert(rename_atomic(enum_entry.key), transformed);
		}

		script->subclasses = rewrite_atomic_key_map(snapshot.subclasses);

		script->_signals.clear();
		for (const KeyValue<StringName, MethodInfo> &signal : snapshot.signals) {
			const StringName key = rename_atomic(signal.key);
			MethodInfo info = signal.value;
			const HashMap<StringName,
					Vector<FoundryScript::AnnotationUsage>>
					*annotation_parameters =
							snapshot.signal_parameter_annotations.getptr(signal.key);
			const HashMap<StringName, StringName> parameter_names =
					rename_map.has(signal.key) ? allocate_parameter_names(
														 signal.value,
														 annotation_parameters)
											   : HashMap<StringName, StringName>();
			rewrite_method_info(info, signal.key,
					parameter_names.is_empty() ? nullptr : &parameter_names);
			script->_signals.insert(key, info);
		}

		script->script_trait_list = snapshot.script_trait_list;
		for (StringName &trait_name : script->script_trait_list) {
			trait_name = StringName(rewrite_identity(String(trait_name)));
		}

		script->abstract_trait_requirements.clear();
		for (const KeyValue<StringName, FoundryScript::AbstractTraitRequirement> &requirement :
				snapshot.abstract_trait_requirements) {
			FoundryScript::AbstractTraitRequirement transformed = requirement.value;
			rewrite_data_type(transformed.return_type);
			const HashMap<StringName,
					Vector<FoundryScript::AnnotationUsage>>
					*annotation_parameters =
							snapshot.method_parameter_annotations.getptr(
									requirement.key);
			const HashMap<StringName, StringName> parameter_names =
					rename_map.has(requirement.key)
					? allocate_parameter_names(requirement.value.method_info,
							  annotation_parameters)
					: HashMap<StringName, StringName>();
			rewrite_method_info(transformed.method_info, requirement.key,
					parameter_names.is_empty() ? nullptr : &parameter_names);
			script->abstract_trait_requirements.insert(
					rename_atomic(requirement.key), transformed);
		}

		script->type_parameters = snapshot.type_parameters;
		for (FoundryScript::TypeParameter &type_parameter : script->type_parameters) {
			rewrite_property_info(type_parameter.bound);
		}

		script->method_annotations = rewrite_atomic_key_map(snapshot.method_annotations);
		script->variable_annotations = rewrite_atomic_key_map(snapshot.variable_annotations);
		script->signal_annotations = rewrite_atomic_key_map(snapshot.signal_annotations);
		script->constant_annotations = rewrite_atomic_key_map(snapshot.constant_annotations);
		script->method_parameter_annotations.clear();
		for (const KeyValue<StringName,
					 HashMap<StringName, Vector<FoundryScript::AnnotationUsage>>> &owner :
				snapshot.method_parameter_annotations) {
			HashMap<StringName, StringName> parameter_names;
			for (const FunctionSnapshot &function_snapshot : function_snapshots) {
				if (function_snapshot.function->_script == script &&
						function_snapshot.owner_name == owner.key &&
						function_snapshot.safe_arguments) {
					parameter_names = function_snapshot.parameter_names;
					break;
				}
			}
			if (parameter_names.is_empty() && rename_map.has(owner.key)) {
				const FoundryScript::AbstractTraitRequirement *requirement =
						snapshot.abstract_trait_requirements.getptr(owner.key);
				if (requirement != nullptr) {
					parameter_names = allocate_parameter_names(
							requirement->method_info, &owner.value);
				}
			}
			HashMap<StringName, Vector<FoundryScript::AnnotationUsage>> transformed;
			for (const KeyValue<StringName,
						 Vector<FoundryScript::AnnotationUsage>> &parameter :
					owner.value) {
				const StringName *renamed =
						parameter_names.getptr(parameter.key);
				transformed.insert(
						renamed != nullptr ? *renamed : parameter.key,
						parameter.value);
			}
			script->method_parameter_annotations.insert(
					rename_atomic(owner.key), transformed);
		}

		script->signal_parameter_annotations.clear();
		for (const KeyValue<StringName,
					 HashMap<StringName, Vector<FoundryScript::AnnotationUsage>>> &owner :
				snapshot.signal_parameter_annotations) {
			const MethodInfo *signal_info = snapshot.signals.getptr(owner.key);
			const HashMap<StringName, StringName> parameter_names =
					signal_info != nullptr && rename_map.has(owner.key)
					? allocate_parameter_names(*signal_info, &owner.value)
					: HashMap<StringName, StringName>();
			HashMap<StringName, Vector<FoundryScript::AnnotationUsage>> transformed;
			for (const KeyValue<StringName,
						 Vector<FoundryScript::AnnotationUsage>> &parameter :
					owner.value) {
				const StringName *renamed =
						parameter_names.getptr(parameter.key);
				transformed.insert(
						renamed != nullptr ? *renamed : parameter.key,
						parameter.value);
			}
			script->signal_parameter_annotations.insert(
					rename_atomic(owner.key), transformed);
		}

		script->old_static_variables_indices.clear();
		for (const KeyValue<StringName, FoundryScript::MemberInfo> &entry :
				snapshot.old_static_variables_indices) {
			const StringName key = rename_atomic(entry.key);
			FoundryScript::MemberInfo info = entry.value;
			rewrite_member_info(info, key);
			script->old_static_variables_indices.insert(key, info);
		}
		script->member_lines = rewrite_atomic_key_map(snapshot.member_lines);
		script->member_default_values =
				rewrite_atomic_key_map(snapshot.member_default_values);
		script->member_default_values_cache =
				rewrite_atomic_key_map(snapshot.member_default_values_cache);

		script->members_cache.clear();
		for (const PropertyInfo &saved_info : snapshot.members_cache) {
			PropertyInfo info = saved_info;
			rewrite_property_info(info, true);
			script->members_cache.push_back(info);
		}
	}

	void stage_function(FunctionSnapshot &snapshot) {
		FSFunction *function = snapshot.function;
		function->name = snapshot.lambda ? snapshot.name : rename_atomic(snapshot.name);
		function->argument_types = snapshot.argument_types;
		for (FSDataType &argument_type : function->argument_types) {
			rewrite_data_type(argument_type);
		}
		function->return_type = snapshot.return_type;
		rewrite_data_type(function->return_type);
		function->method_info = snapshot.method_info;
		const StringName owner_name =
				snapshot.owner_name != StringName() ? snapshot.owner_name : snapshot.name;
		rewrite_method_info(function->method_info, owner_name,
				snapshot.safe_arguments ? &snapshot.parameter_names : nullptr);
		if (snapshot.lambda) {
			function->method_info.name = snapshot.method_info.name;
		}
		function->global_names = snapshot.global_names;
		for (StringName &global_name : function->global_names) {
			global_name = rewrite_dispatch_name(global_name);
		}
		function->setup_runtime_pointers();
	}

	void stage() {
		for (ClassSnapshot &snapshot : class_snapshots) {
			stage_class(snapshot);
		}
		for (FunctionSnapshot &snapshot : function_snapshots) {
			stage_function(snapshot);
		}
		FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
		for (const RegistrySnapshot &snapshot : registry_snapshots) {
			registry->register_file_conformances(
					snapshot.source, snapshot.transformed_conformances);
			registry->register_runtime_witnesses(
					snapshot.source, snapshot.transformed_entries);
		}
	}

	void restore() {
		for (const FunctionSnapshot &snapshot : function_snapshots) {
			snapshot.function->name = snapshot.name;
			snapshot.function->argument_types = snapshot.argument_types;
			snapshot.function->return_type = snapshot.return_type;
			snapshot.function->method_info = snapshot.method_info;
			snapshot.function->global_names = snapshot.global_names;
			snapshot.function->setup_runtime_pointers();
		}

		for (const ClassSnapshot &snapshot : class_snapshots) {
			FoundryScript *script = snapshot.script.ptr();
			script->trait_type_name = snapshot.trait_type_name;
			script->local_name = snapshot.local_name;
			script->global_name = snapshot.global_name;
			script->fully_qualified_name = snapshot.fully_qualified_name;
			script->member_indices = snapshot.member_indices;
			script->members = snapshot.members;
			script->member_type_argument_bindings =
					snapshot.member_type_argument_bindings;
			script->type_parameter_bindings_by_ancestor =
					snapshot.type_parameter_bindings_by_ancestor;
			script->static_variables_indices = snapshot.static_variables_indices;
			script->constants = snapshot.constants;
			script->member_functions = snapshot.member_functions;
			script->enum_functions = snapshot.enum_functions;
			script->subclasses = snapshot.subclasses;
			script->_signals = snapshot.signals;
			script->script_trait_list = snapshot.script_trait_list;
			script->abstract_trait_requirements =
					snapshot.abstract_trait_requirements;
			script->type_parameters = snapshot.type_parameters;
			script->method_annotations = snapshot.method_annotations;
			script->variable_annotations = snapshot.variable_annotations;
			script->signal_annotations = snapshot.signal_annotations;
			script->constant_annotations = snapshot.constant_annotations;
			script->method_parameter_annotations =
					snapshot.method_parameter_annotations;
			script->signal_parameter_annotations =
					snapshot.signal_parameter_annotations;
			script->old_static_variables_indices =
					snapshot.old_static_variables_indices;
			script->member_lines = snapshot.member_lines;
			script->member_default_values = snapshot.member_default_values;
			script->members_cache = snapshot.members_cache;
			script->member_default_values_cache =
					snapshot.member_default_values_cache;
		}

		FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
		for (const RegistrySnapshot &snapshot : registry_snapshots) {
			registry->register_file_conformances(
					snapshot.source, snapshot.conformances);
			registry->register_runtime_witnesses(snapshot.source, snapshot.entries);
		}
	}
};

static FSNameManglerApplication::Transaction *active_name_mangler_transaction = nullptr;

String FSNameManglerApplication::Diagnostic::format() const {
	String prefix = surface;
	if (source_name != StringName()) {
		if (!prefix.is_empty()) {
			prefix += " ";
		}
		prefix += vformat("`%s`", source_name);
	}
	return prefix.is_empty() ? message : prefix + ": " + message;
}

FSNameManglerApplication::Transaction::Transaction() {
	data = memnew(Data);
}

FSNameManglerApplication::Transaction::~Transaction() {
	rollback();
	memdelete(data);
}

Error FSNameManglerApplication::Transaction::_fail(const String &p_surface,
		const StringName &p_source_name, const String &p_message, Error p_error,
		Vector<Diagnostic> &r_diagnostics) {
	Diagnostic diagnostic;
	diagnostic.surface = p_surface;
	diagnostic.source_name = p_source_name;
	diagnostic.message = p_message;
	r_diagnostics.push_back(diagnostic);
	if (active_name_mangler_transaction == this) {
		active_name_mangler_transaction = nullptr;
	}
	state = STATE_FINISHED;
	data->clear();
	return p_error;
}

Error FSNameManglerApplication::Transaction::begin(
		const Vector<Ref<FoundryScript>> &p_scripts,
		const RBMap<StringName, StringName> &p_rename_map,
		Vector<Diagnostic> &r_diagnostics) {
	r_diagnostics.clear();
	if (state != STATE_UNUSED) {
		Diagnostic diagnostic;
		diagnostic.surface = "transaction";
		diagnostic.message =
				"This name-application transaction has already been used.";
		r_diagnostics.push_back(diagnostic);
		return ERR_ALREADY_IN_USE;
	}
	if (!Thread::is_main_thread()) {
		return _fail("transaction", StringName(),
				"Compiled-graph name application must run on the main thread.",
				ERR_UNAVAILABLE, r_diagnostics);
	}
	if (active_name_mangler_transaction != nullptr) {
		return _fail("transaction", StringName(),
				"Another compiled-graph name application transaction is active.",
				ERR_ALREADY_IN_USE, r_diagnostics);
	}
	state = STATE_PREPARING;
	active_name_mangler_transaction = this;
	if (!data->prepare(p_scripts, p_rename_map, r_diagnostics)) {
		if (active_name_mangler_transaction == this) {
			active_name_mangler_transaction = nullptr;
		}
		state = STATE_FINISHED;
		data->clear();
		return ERR_INVALID_PARAMETER;
	}

	data->stage();
	state = STATE_ACTIVE;
	return OK;
}

void FSNameManglerApplication::Transaction::rollback() {
	if (state != STATE_ACTIVE && state != STATE_PREPARING) {
		return;
	}
	if (state == STATE_ACTIVE) {
		data->restore();
	}
	data->clear();
	if (active_name_mangler_transaction == this) {
		active_name_mangler_transaction = nullptr;
	}
	state = STATE_FINISHED;
}

bool FSNameManglerApplication::Transaction::is_active() const {
	return state == STATE_ACTIVE;
}

FSNameManglerApplication::Transaction::State
FSNameManglerApplication::Transaction::get_state() const {
	return state;
}

#endif // TOOLS_ENABLED
