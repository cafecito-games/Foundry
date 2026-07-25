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

#include "fs_utility_functions.h"

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

	RBMap<StringName, StringName> rename_map;
	Vector<ClassSnapshot> class_snapshots;
	Vector<FunctionSnapshot> function_snapshots;
	HashMap<const FoundryScript *, int> class_snapshot_indices;
	HashSet<const FoundryScript *> indexed_classes;
	HashSet<FSFunction *> indexed_functions;
	Vector<IdentityReplacement> identity_replacements;

	void clear() {
		rename_map.clear();
		class_snapshots.clear();
		function_snapshots.clear();
		class_snapshot_indices.clear();
		indexed_classes.clear();
		indexed_functions.clear();
		identity_replacements.clear();
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

	void build_identity_plan() {
		identity_replacements.clear();
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
				if (p_source.is_empty() || p_source == p_replacement) {
					return;
				}
				const String *existing = unique_replacements.getptr(p_source);
				if (existing == nullptr) {
					unique_replacements.insert(p_source, p_replacement);
				} else {
					CRASH_COND(*existing != p_replacement);
				}
			};
			add_replacement(String(snapshot.local_name), String(snapshot.transformed_local_name));
			add_replacement(String(snapshot.global_name), String(snapshot.transformed_global_name));
			add_replacement(snapshot.fully_qualified_name, snapshot.transformed_fully_qualified_name);
			add_replacement(String(snapshot.trait_type_name), String(snapshot.transformed_trait_type_name));
		}

		for (const KeyValue<String, String> &entry : unique_replacements) {
			IdentityReplacement replacement;
			replacement.source = entry.key;
			replacement.replacement = entry.value;
			identity_replacements.push_back(replacement);
		}
		identity_replacements.sort_custom<IdentityReplacementComparator>();
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

		String prefix;
		String body = p_identity;
		if (body.begins_with("res://") || body.begins_with("user://")) {
			const int nested_separator = body.find("::");
			if (nested_separator < 0) {
				return body;
			}
			prefix = body.substr(0, nested_separator + 2);
			body = body.substr(nested_separator + 2);
		}

		for (const IdentityReplacement &replacement : identity_replacements) {
			int offset = 0;
			while (offset <= body.length() - replacement.source.length()) {
				const int found = body.find(replacement.source, offset);
				if (found < 0) {
					break;
				}
				if (!has_identity_boundaries(body, found, replacement.source.length())) {
					offset = found + replacement.source.length();
					continue;
				}
				body = body.substr(0, found) + replacement.replacement +
						body.substr(found + replacement.source.length());
				offset = found + replacement.replacement.length();
			}
		}
		return prefix + body;
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

	HashMap<StringName, StringName> allocate_parameter_names(const MethodInfo &p_info) const {
		HashSet<StringName> reserved;
		for (const PropertyInfo &argument : p_info.arguments) {
			reserved.insert(argument.name);
		}

		HashMap<StringName, StringName> result;
		uint64_t ordinal = 0;
		for (const PropertyInfo &argument : p_info.arguments) {
			StringName candidate;
			do {
				candidate = StringName("_fsb_arg_" + String::num_int64(ordinal++, 36));
			} while (reserved.has(candidate));
			reserved.insert(candidate);
			result.insert(argument.name, candidate);
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

	void snapshot_class(const Ref<FoundryScript> &p_script, FoundryScript *p_parent) {
		CRASH_COND(p_script.is_null());
		CRASH_COND(indexed_classes.has(p_script.ptr()));
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
			snapshot_class(subclass, p_script.ptr());
		}
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
			snapshot.parameter_names =
					allocate_parameter_names(snapshot.method_info);
		}
		function_snapshots.push_back(snapshot);

		for (FSFunction *lambda : p_function->lambdas) {
			snapshot_function(lambda, StringName(), true);
		}
	}

	static bool variant_contains_name(const Variant &p_value, const StringName &p_name,
			int p_depth = 0) {
		if (p_depth > Variant::MAX_RECURSION_DEPTH) {
			return false;
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
			case Variant::CALLABLE:
				return Callable(p_value).get_method() == p_name;
			case Variant::SIGNAL:
				return Signal(p_value).get_name() == p_name;
			case Variant::ARRAY: {
				const Array array = p_value;
				const Variant typed_script = array.get_typed_script();
				if (typed_script.get_type() == Variant::OBJECT) {
					Resource *resource =
							Object::cast_to<Resource>(Object::cast_to<Object>(typed_script));
					if (resource != nullptr &&
							resource->get_path().contains(String(p_name))) {
						return true;
					}
				}
				for (const Variant &value : array) {
					if (variant_contains_name(value, p_name, p_depth + 1)) {
						return true;
					}
				}
			} break;
			case Variant::DICTIONARY: {
				const Dictionary dictionary = p_value;
				const Array keys = dictionary.keys();
				for (const Variant &key : keys) {
					if (variant_contains_name(key, p_name, p_depth + 1) ||
							variant_contains_name(dictionary[key], p_name, p_depth + 1)) {
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
			} break;
			default:
				break;
		}
		return false;
	}

	static bool annotation_usages_contain_name(
			const Vector<FoundryScript::AnnotationUsage> &p_usages,
			const StringName &p_name) {
		for (const FoundryScript::AnnotationUsage &usage : p_usages) {
			if (variant_contains_name(usage.args, p_name) ||
					variant_contains_name(usage.kwargs, p_name)) {
				return true;
			}
		}
		return false;
	}

	bool find_protected_surface(const StringName &p_name, String &r_surface) const {
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

		for (const ClassSnapshot &snapshot : class_snapshots) {
			const FoundryScript *script = snapshot.script.ptr();
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
			if (annotation_usages_contain_name(script->class_annotations, p_name)) {
				r_surface = "annotation argument";
				return true;
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

		roots.sort_custom<ScriptRefComparator>();
		for (const Ref<FoundryScript> &root : roots) {
			snapshot_class(root, nullptr);
		}
		build_identity_plan();
		snapshot_functions();
		if (!validate_protected_names(r_diagnostics)) {
			return false;
		}
		return true;
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
			const HashMap<StringName, StringName> parameter_names =
					rename_map.has(signal.key) ? allocate_parameter_names(signal.value) : HashMap<StringName, StringName>();
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
			const HashMap<StringName, StringName> parameter_names =
					rename_map.has(requirement.key) ? allocate_parameter_names(requirement.value.method_info) : HashMap<StringName, StringName>();
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
					signal_info != nullptr && rename_map.has(owner.key) ? allocate_parameter_names(*signal_info) : HashMap<StringName, StringName>();
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
	if (!data->prepare(p_scripts, p_rename_map, r_diagnostics)) {
		state = STATE_FINISHED;
		data->clear();
		return ERR_INVALID_PARAMETER;
	}

	data->stage();
	state = STATE_ACTIVE;
	active_name_mangler_transaction = this;
	return OK;
}

void FSNameManglerApplication::Transaction::rollback() {
	if (state != STATE_ACTIVE) {
		return;
	}
	data->restore();
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
