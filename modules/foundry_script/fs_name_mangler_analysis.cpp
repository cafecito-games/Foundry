/**************************************************************************/
/*  fs_name_mangler_analysis.cpp                                          */
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

#include "fs_name_mangler_analysis.h"

#ifdef TOOLS_ENABLED

#include "fs_conformance_registry.h"
#include "fs_function.h"
#include "fs_utility_functions.h"

#include "core/config/engine.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/string/char_utils.h"
#include "core/variant/callable.h"

struct FSNameManglerAnalysis::BuildState {
	struct Aggregate {
		HashSet<int> kinds;
		Vector<KeepEvidence> evidence;
	};

	struct ScopedReflectionUse {
		const FoundryScript *owner = nullptr;
		uint8_t kind = FSFunction::REFLECTION_NONE;
		String detail;
	};

	HashMap<StringName, Aggregate> candidates;
	HashSet<StringName> observed_names;
	HashMap<String, Vector<String>> string_sources;
	HashMap<StringName, Vector<String>> protected_sources;
	HashMap<String, Vector<String>> protected_path_sources;
	HashMap<StringName, Vector<String>> external_sources;
	Vector<String> unresolved_method_reflection_sources;
	Vector<String> unresolved_property_reflection_sources;
	Vector<String> unresolved_signal_reflection_sources;
	Vector<ScopedReflectionUse> scoped_reflection_uses;
	HashMap<const FoundryScript *, HashSet<StringName>> method_declarations;
	HashMap<const FoundryScript *, HashSet<StringName>> property_declarations;
	HashMap<const FoundryScript *, HashSet<StringName>> signal_declarations;
	HashMap<const FoundryScript *, HashSet<StringName>>
			scoped_method_reflection_names;
	HashMap<const FoundryScript *, HashSet<StringName>>
			scoped_property_reflection_names;
	HashMap<const FoundryScript *, HashSet<StringName>>
			scoped_signal_reflection_names;
	bool unscannable_protected_surface = false;
	HashSet<const FoundryScript *> included_classes;
	HashSet<String> included_identities;
	HashSet<const FoundryScript *> visited_classes;
	HashSet<const FoundryScript *> visited_external_classes;
	HashSet<const Script *> visited_external_scripts;
	HashSet<const FSFunction *> visited_functions;
	HashSet<const void *> visited_variant_containers;
};

namespace {

struct NameComparator {
	bool operator()(const StringName &p_left, const StringName &p_right) const {
		return String(p_left) < String(p_right);
	}
};

struct EvidenceComparator {
	bool operator()(const FSNameManglerAnalysis::KeepEvidence &p_left,
			const FSNameManglerAnalysis::KeepEvidence &p_right) const {
		if (p_left.reason != p_right.reason) {
			return p_left.reason < p_right.reason;
		}
		return p_left.detail < p_right.detail;
	}
};

bool evidence_matches(const FSNameManglerAnalysis::KeepEvidence &p_left,
		const FSNameManglerAnalysis::KeepEvidence &p_right) {
	return p_left.reason == p_right.reason && p_left.detail == p_right.detail;
}

bool is_type_bearing_hint(PropertyHint p_hint) {
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

Vector<String> get_encoded_type_identities(const String &p_text) {
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

} // namespace

void FSNameManglerAnalysis::Input::add_keep(const StringName &p_name, KeepReason p_reason, const String &p_detail) {
	KeepEvidence evidence;
	evidence.name = p_name;
	evidence.reason = p_reason;
	evidence.detail = p_detail;
	keep_evidence.push_back(evidence);
}

bool FSNameManglerAnalysis::Classification::is_kept() const {
	return !keep_evidence.is_empty();
}

const FSNameManglerAnalysis::Classification *FSNameManglerAnalysis::Result::find(const StringName &p_name) const {
	for (const Classification &classification : classifications) {
		if (classification.name == p_name) {
			return &classification;
		}
	}
	return nullptr;
}

String FSNameManglerAnalysis::get_keep_reason_label(KeepReason p_reason) {
	switch (p_reason) {
		case KEEP_SCENE_OR_RESOURCE:
			return "scene/resource reference";
		case KEEP_RPC:
			return "RPC method";
		case KEEP_NATIVE_VIRTUAL:
			return "native virtual override";
		case KEEP_STRING_LITERAL:
			return "string-like constant";
		case KEEP_REFLECTION:
			return "reflection enumeration";
		case KEEP_EXTERNAL_OR_UNPROVABLE:
			return "external or unprovable reference";
		case KEEP_RULE:
			return "explicit keep rule";
	}
	return "unknown reason";
}

bool FSNameManglerAnalysis::_is_candidate_name(const StringName &p_name) {
	return p_name != StringName() && !String(p_name).begins_with("@");
}

void FSNameManglerAnalysis::_add_candidate(const StringName &p_name, IdentifierKind p_kind, BuildState &r_state) {
	if (!_is_candidate_name(p_name)) {
		return;
	}
	r_state.observed_names.insert(p_name);
	r_state.candidates[p_name].kinds.insert((int)p_kind);
}

void FSNameManglerAnalysis::_add_evidence(
		const StringName &p_name, KeepReason p_reason, const String &p_detail, BuildState &r_state) {
	BuildState::Aggregate *aggregate = r_state.candidates.getptr(p_name);
	if (aggregate == nullptr) {
		return;
	}

	KeepEvidence evidence;
	evidence.name = p_name;
	evidence.reason = p_reason;
	evidence.detail = p_detail;
	for (const KeepEvidence &existing : aggregate->evidence) {
		if (evidence_matches(existing, evidence)) {
			return;
		}
	}
	aggregate->evidence.push_back(evidence);
}

void FSNameManglerAnalysis::_add_string_evidence(
		const String &p_name, const String &p_source, BuildState &r_state) {
	if (p_name.is_empty()) {
		return;
	}
	r_state.observed_names.insert(StringName(p_name));
	Vector<String> &sources = r_state.string_sources[p_name];
	if (!sources.has(p_source)) {
		sources.push_back(p_source);
	}
}

void FSNameManglerAnalysis::_add_protected_name(
		const StringName &p_name, const String &p_source, BuildState &r_state) {
	if (p_name == StringName()) {
		return;
	}
	r_state.observed_names.insert(p_name);
	Vector<String> &sources = r_state.protected_sources[p_name];
	if (!sources.has(p_source)) {
		sources.push_back(p_source);
	}
}

void FSNameManglerAnalysis::_add_protected_identity(
		const String &p_identity, const String &p_source, BuildState &r_state) {
	if (p_identity.is_empty()) {
		return;
	}
	r_state.observed_names.insert(StringName(p_identity));
	if (p_identity.is_valid_unicode_identifier()) {
		_add_protected_name(StringName(p_identity), p_source, r_state);
		return;
	}
	const PackedStringArray components =
			p_identity.replace("::", ".").split(".", false);
	for (const String &component : components) {
		if (component.is_valid_unicode_identifier()) {
			_add_protected_name(StringName(component), p_source, r_state);
		}
	}
}

void FSNameManglerAnalysis::_add_protected_path(
		const String &p_path, const String &p_source, BuildState &r_state) {
	if (p_path.is_empty()) {
		return;
	}
	Vector<String> &sources = r_state.protected_path_sources[p_path];
	if (!sources.has(p_source)) {
		sources.push_back(p_source);
	}
}

void FSNameManglerAnalysis::_add_external_surface_name(
		const StringName &p_name, const String &p_source, BuildState &r_state) {
	if (!_is_candidate_name(p_name)) {
		return;
	}
	r_state.observed_names.insert(p_name);
	Vector<String> &sources = r_state.external_sources[p_name];
	if (!sources.has(p_source)) {
		sources.push_back(p_source);
	}
}

bool FSNameManglerAnalysis::_is_included_identity(const String &p_identity, const BuildState &p_state) {
	return !p_identity.is_empty() &&
			p_state.included_identities.has(p_identity);
}

void FSNameManglerAnalysis::_collect_class_identity_reference(
		const String &p_identity, const String &p_source, BuildState &r_state) {
	if (p_identity.is_empty()) {
		return;
	}
	const StringName identity = StringName(p_identity);
	r_state.observed_names.insert(identity);
	if (!_is_included_identity(p_identity, r_state)) {
		_add_external_surface_name(identity, p_source, r_state);
	}
}

void FSNameManglerAnalysis::_record_reflection_use(
		const StringName &p_method, const StringName &p_class, const String &p_source, BuildState &r_state) {
	Vector<String> *sources = nullptr;
	switch (FSFunction::get_reflection_kind(p_method, p_class)) {
		case FSFunction::REFLECTION_METHODS:
			sources = &r_state.unresolved_method_reflection_sources;
			break;
		case FSFunction::REFLECTION_PROPERTIES:
			sources = &r_state.unresolved_property_reflection_sources;
			break;
		case FSFunction::REFLECTION_SIGNALS:
			sources = &r_state.unresolved_signal_reflection_sources;
			break;
		default:
			break;
	}
	if (sources == nullptr) {
		return;
	}

	String detail = p_source;
	if (!detail.is_empty()) {
		detail += " calls ";
	}
	detail += String(p_method);
	if (!sources->has(detail)) {
		sources->push_back(detail);
	}
}

void FSNameManglerAnalysis::_index_class(const FoundryScript *p_class, BuildState &r_state) {
	if (p_class == nullptr || r_state.included_classes.has(p_class)) {
		return;
	}
	r_state.included_classes.insert(p_class);
	r_state.included_identities.insert(String(p_class->local_name));
	r_state.included_identities.insert(String(p_class->global_name));
	r_state.included_identities.insert(
			p_class->fully_qualified_name);
	r_state.included_identities.insert(String(p_class->trait_type_name));
	r_state.included_identities.insert(p_class->get_script_path());
	const String script_prefix = p_class->get_script_path() + "::";
	String relative_identity;
	if (!p_class->get_script_path().is_empty() &&
			p_class->fully_qualified_name.begins_with(script_prefix)) {
		relative_identity = p_class->fully_qualified_name.substr(
				script_prefix.length());
		r_state.included_identities.insert(relative_identity);
	}
	for (const KeyValue<StringName, Variant> &constant :
			p_class->constants) {
		if (constant.value.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const String enum_name = constant.key;
		r_state.included_identities.insert(enum_name);
		const auto add_owned_enum = [&](const String &p_owner) {
			if (p_owner.is_empty()) {
				return;
			}
			r_state.included_identities.insert(
					p_owner + "." + enum_name);
			r_state.included_identities.insert(
					p_owner + "::" + enum_name);
		};
		add_owned_enum(String(p_class->local_name));
		add_owned_enum(String(p_class->global_name));
		add_owned_enum(p_class->fully_qualified_name);
		add_owned_enum(p_class->fully_qualified_name.replace("::", "."));
		add_owned_enum(relative_identity);
		add_owned_enum(relative_identity.replace("::", "."));
	}
	for (const KeyValue<StringName, Ref<FoundryScript>> &subclass : p_class->subclasses) {
		_index_class(subclass.value.ptr(), r_state);
	}
}

void FSNameManglerAnalysis::_collect_external_class_surface(
		const FoundryScript *p_class, const String &p_source, BuildState &r_state) {
	if (p_class == nullptr || r_state.included_classes.has(p_class) ||
			r_state.visited_external_classes.has(p_class)) {
		return;
	}
	r_state.visited_external_classes.insert(p_class);

	const String script_path = p_class->get_script_path();
	const String source = script_path.is_empty() ? p_source : "external script " + script_path;
	_add_external_surface_name(p_class->local_name, source, r_state);
	_add_external_surface_name(p_class->global_name, source, r_state);
	_add_external_surface_name(StringName(p_class->fully_qualified_name), source, r_state);
	for (const StringName &member : p_class->members) {
		_add_external_surface_name(member, source, r_state);
	}
	for (const KeyValue<StringName, FoundryScript::MemberInfo> &member : p_class->static_variables_indices) {
		_add_external_surface_name(member.key, source, r_state);
	}
	for (const KeyValue<StringName, Variant> &constant : p_class->constants) {
		_add_external_surface_name(constant.key, source, r_state);
		_collect_variant(constant.value, source, r_state);
	}
	for (const KeyValue<StringName, MethodInfo> &signal : p_class->_signals) {
		_add_external_surface_name(signal.key, source, r_state);
	}
	for (const KeyValue<StringName, FSFunction *> &method : p_class->member_functions) {
		_add_external_surface_name(method.key, source, r_state);
	}
	for (const KeyValue<StringName, FoundryScript::EnumFunctionSet> &enum_entry : p_class->enum_functions) {
		_add_external_surface_name(enum_entry.key, source, r_state);
		for (const KeyValue<StringName, FSFunction *> &method : enum_entry.value.instance_functions) {
			_add_external_surface_name(method.key, source, r_state);
		}
		for (const KeyValue<StringName, FSFunction *> &method : enum_entry.value.static_functions) {
			_add_external_surface_name(method.key, source, r_state);
		}
	}
	for (const KeyValue<StringName, FoundryScript::AbstractTraitRequirement> &requirement :
			p_class->abstract_trait_requirements) {
		_add_external_surface_name(requirement.key, source, r_state);
	}
	for (const Variant &value : p_class->static_variables) {
		_collect_variant(value, source, r_state);
	}
	for (const KeyValue<StringName, Variant> &default_value : p_class->member_default_values) {
		_collect_variant(default_value.value, source, r_state);
	}
	_collect_external_class_surface(p_class->base.ptr(), source, r_state);
	for (const KeyValue<StringName, Ref<FoundryScript>> &subclass : p_class->subclasses) {
		_collect_external_class_surface(subclass.value.ptr(), source, r_state);
	}
}

void FSNameManglerAnalysis::_collect_external_script_surface(
		const Script *p_script, const String &p_source, BuildState &r_state,
		int p_depth) {
	if (p_script == nullptr) {
		return;
	}
	if (p_depth > Variant::MAX_RECURSION_DEPTH) {
		r_state.unscannable_protected_surface = true;
		return;
	}
	if (const FoundryScript *foundry_script =
					Object::cast_to<FoundryScript>(p_script)) {
		_collect_external_class_surface(foundry_script, p_source, r_state);
		return;
	}
	if (r_state.visited_external_scripts.has(p_script)) {
		return;
	}
	r_state.visited_external_scripts.insert(p_script);

	const String script_path = p_script->get_path();
	const String source = script_path.is_empty()
			? p_source
			: "external script " + script_path;
	_add_protected_path(script_path, source, r_state);
	_add_protected_name(p_script->get_global_name(), source, r_state);

	List<MethodInfo> methods;
	p_script->get_script_method_list(&methods);
	for (const MethodInfo &method : methods) {
		_add_protected_name(method.name, source + " method", r_state);
		_collect_property_info(
				method.return_val, source + " return type", r_state, true);
		for (const PropertyInfo &argument : method.arguments) {
			_collect_property_info(
					argument, source + " argument type", r_state, true);
		}
		for (const Variant &default_argument : method.default_arguments) {
			_collect_variant(
					default_argument, source + " default argument", r_state);
		}
	}
	List<PropertyInfo> properties;
	p_script->get_script_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		_add_protected_name(
				StringName(property.name), source + " property", r_state);
		_collect_property_info(
				property, source + " property type", r_state, true);
	}
	List<MethodInfo> signals;
	p_script->get_script_signal_list(&signals);
	for (const MethodInfo &signal : signals) {
		_add_protected_name(signal.name, source + " signal", r_state);
		_collect_property_info(
				signal.return_val, source + " signal return type", r_state,
				true);
		for (const PropertyInfo &argument : signal.arguments) {
			_collect_property_info(
					argument, source + " signal argument type", r_state,
					true);
		}
		for (const Variant &default_argument : signal.default_arguments) {
			_collect_variant(
					default_argument, source + " signal default argument",
					r_state);
		}
	}
	HashMap<StringName, Variant> constants;
	const_cast<Script *>(p_script)->get_constants(&constants);
	for (const KeyValue<StringName, Variant> &constant : constants) {
		_add_protected_name(
				constant.key, source + " constant", r_state);
	}
	HashSet<StringName> members;
	const_cast<Script *>(p_script)->get_members(&members);
	for (const StringName &member : members) {
		_add_protected_name(member, source + " member", r_state);
	}
	const Ref<Script> base = p_script->get_base_script();
	_collect_external_script_surface(
			base.ptr(), source + " base", r_state, p_depth + 1);
}

void FSNameManglerAnalysis::_collect_container_type(
		const ContainerType &p_type, const String &p_source,
		BuildState &r_state, int p_depth) {
	if (p_depth > Variant::MAX_RECURSION_DEPTH) {
		r_state.unscannable_protected_surface = true;
		return;
	}
	_add_protected_identity(
			String(p_type.class_name), p_source + " class identity", r_state);
	_collect_external_script_surface(
			p_type.script.ptr(), p_source + " script", r_state, p_depth + 1);
	for (const ContainerType &element_type : p_type.element_types) {
		_collect_container_type(
				element_type, p_source + " element type", r_state,
				p_depth + 1);
	}
	for (const ContainerType &type_argument : p_type.type_arguments) {
		_collect_container_type(
				type_argument, p_source + " type argument", r_state,
				p_depth + 1);
	}
}

void FSNameManglerAnalysis::_collect_data_type(
		const FSDataType &p_type, const String &p_source,
		BuildState &r_state, int p_depth) {
	if (p_depth > Variant::MAX_RECURSION_DEPTH) {
		r_state.unscannable_protected_surface = true;
		return;
	}
	if (p_type.script_type != nullptr) {
		_collect_external_script_surface(
				p_type.script_type, p_source + " script type", r_state,
				p_depth + 1);
	}
	if (p_type.script_type_ref.is_valid()) {
		_collect_external_script_surface(
				p_type.script_type_ref.ptr(),
				p_source + " script type reference", r_state, p_depth + 1);
	}
	if (p_type.native_type != StringName()) {
		_add_protected_identity(
				String(p_type.native_type), p_source + " native type",
				r_state);
	}
	if (p_type.is_script_trait &&
			!_is_included_identity(String(p_type.script_trait), r_state)) {
		_add_protected_identity(
				String(p_type.script_trait), p_source + " trait type",
				r_state);
	}
	for (const FSDataType &element_type :
			p_type.container_element_types) {
		_collect_data_type(
				element_type, p_source + " element type", r_state,
				p_depth + 1);
	}
	for (const FSDataType &type_argument : p_type.type_arguments) {
		_collect_data_type(
				type_argument, p_source + " type argument", r_state,
				p_depth + 1);
	}
}

void FSNameManglerAnalysis::_collect_property_info(
		const PropertyInfo &p_info, const String &p_source,
		BuildState &r_state, bool p_external_surface) {
	const String identity = p_info.class_name;
	if (!identity.is_empty() &&
			(p_external_surface ||
					!_is_included_identity(identity, r_state))) {
		_add_protected_identity(
				identity, p_source + " class identity", r_state);
	}
	if (!is_type_bearing_hint(p_info.hint) ||
			p_info.hint_string.is_empty()) {
		return;
	}
	for (const String &hint_identity :
			get_encoded_type_identities(p_info.hint_string)) {
		if (p_external_surface ||
				!_is_included_identity(hint_identity, r_state)) {
			_add_protected_identity(
					hint_identity, p_source + " encoded type identity",
					r_state);
		}
	}
}

void FSNameManglerAnalysis::_index_global_protected_names(
		BuildState &r_state) {
	LocalVector<StringName> classes;
	ClassDB::get_class_list(classes); // Returned in deterministic lexical order.
	for (const StringName &class_name : classes) {
		const String source = "ClassDB " + String(class_name);
		_add_protected_name(class_name, source + " class", r_state);
		List<MethodInfo> methods;
		ClassDB::get_method_list(class_name, &methods, true);
		for (const MethodInfo &method : methods) {
			_add_protected_name(method.name, source + " method", r_state);
		}
		List<MethodInfo> virtual_methods;
		ClassDB::get_virtual_methods(class_name, &virtual_methods, true);
		for (const MethodInfo &method : virtual_methods) {
			_add_protected_name(
					method.name, source + " virtual method", r_state);
		}
		List<PropertyInfo> properties;
		ClassDB::get_property_list(class_name, &properties, true);
		for (const PropertyInfo &property : properties) {
			const StringName property_name = StringName(property.name);
			_add_protected_name(
					property_name, source + " property", r_state);
			_add_protected_name(
					ClassDB::get_property_setter(class_name, property_name),
					source + " property setter", r_state);
			_add_protected_name(
					ClassDB::get_property_getter(class_name, property_name),
					source + " property getter", r_state);
		}
		List<MethodInfo> signals;
		ClassDB::get_signal_list(class_name, &signals, true);
		for (const MethodInfo &signal : signals) {
			_add_protected_name(signal.name, source + " signal", r_state);
		}
		List<String> constants;
		ClassDB::get_integer_constant_list(class_name, &constants, true);
		for (const String &constant : constants) {
			_add_protected_name(
					StringName(constant), source + " constant", r_state);
		}
	}

	for (int type = 0; type < Variant::VARIANT_MAX; type++) {
		const Variant::Type variant_type = (Variant::Type)type;
		_add_protected_name(
				StringName(Variant::get_type_name(variant_type)),
				"Variant type", r_state);
		List<StringName> methods;
		Variant::get_builtin_method_list(variant_type, &methods);
		for (const StringName &method : methods) {
			_add_protected_name(method, "Variant builtin method", r_state);
		}
		List<StringName> members;
		Variant::get_member_list(variant_type, &members);
		for (const StringName &member : members) {
			_add_protected_name(member, "Variant member", r_state);
		}
	}
	List<StringName> utilities;
	Variant::get_utility_function_list(&utilities);
	for (const StringName &utility : utilities) {
		_add_protected_name(utility, "Variant utility", r_state);
	}
	List<StringName> foundry_utilities;
	FSUtilityFunctions::get_function_list(&foundry_utilities);
	for (const StringName &utility : foundry_utilities) {
		_add_protected_name(utility, "Foundry utility", r_state);
	}
	if (Engine::get_singleton() != nullptr) {
		List<Engine::Singleton> singletons;
		Engine::get_singleton()->get_singletons(&singletons);
		for (const Engine::Singleton &singleton : singletons) {
			_add_protected_name(singleton.name, "engine singleton", r_state);
		}
	}
	if (FSLanguage::get_singleton() != nullptr) {
		for (const String &name :
				FSLanguage::get_singleton()->get_reserved_global_names()) {
			_add_protected_name(
					StringName(name), "language global", r_state);
		}
	}

	LocalVector<StringName> global_classes;
	ScriptServer::get_global_class_list(global_classes);
	for (const StringName &global_class : global_classes) {
		if (FSLanguage::get_singleton() != nullptr &&
				ScriptServer::get_global_class_language(global_class) ==
						FSLanguage::get_singleton()->get_name()) {
			continue;
		}
		const String path =
				ScriptServer::get_global_class_path(global_class);
		_add_protected_name(
				global_class, "external global script class", r_state);
		_add_protected_path(
				path, "external global script path", r_state);
		Ref<Resource> resource = ResourceCache::get_ref(path);
		if (resource.is_null() && ResourceLoader::exists(path)) {
			resource = ResourceLoader::load(path);
		}
		_collect_external_script_surface(
				Object::cast_to<Script>(resource.ptr()),
				"external global script " + path, r_state);
	}
}

void FSNameManglerAnalysis::_collect_variant(
		const Variant &p_value, const String &p_source, BuildState &r_state, int p_depth) {
	const bool is_array = p_value.get_type() == Variant::ARRAY;
	const bool is_dictionary = p_value.get_type() == Variant::DICTIONARY;
	const void *container_id = nullptr;
	if (is_array) {
		container_id = Array(p_value).id();
	} else if (is_dictionary) {
		container_id = Dictionary(p_value).id();
	}
	if (container_id != nullptr &&
			r_state.visited_variant_containers.has(container_id)) {
		return;
	}
	if (p_depth > Variant::MAX_RECURSION_DEPTH) {
		r_state.unscannable_protected_surface = true;
		return;
	}
	if (container_id != nullptr) {
		r_state.visited_variant_containers.insert(container_id);
	}

	switch (p_value.get_type()) {
		case Variant::STRING:
			_add_string_evidence(p_value, p_source, r_state);
			break;
		case Variant::STRING_NAME:
			_add_string_evidence(String((StringName)p_value), p_source, r_state);
			break;
		case Variant::NODE_PATH: {
			const NodePath path = p_value;
			for (int i = 0; i < path.get_subname_count(); i++) {
				_add_string_evidence(String(path.get_subname(i)), p_source, r_state);
			}
		} break;
		case Variant::CALLABLE: {
			const Callable callable = p_value;
			_add_string_evidence(String(callable.get_method()), p_source, r_state);
			const Array bound_arguments = callable.get_bound_arguments();
			for (int i = 0; i < bound_arguments.size(); i++) {
				_collect_variant(bound_arguments[i], p_source, r_state, p_depth + 1);
			}
		} break;
		case Variant::SIGNAL: {
			const Signal signal = p_value;
			_add_string_evidence(String(signal.get_name()), p_source, r_state);
		} break;
		case Variant::ARRAY: {
			const Array array = p_value;
			_collect_container_type(
					array.get_element_type(), p_source + " typed Array",
					r_state);
			for (int i = 0; i < array.size(); i++) {
				_collect_variant(array[i], p_source, r_state, p_depth + 1);
			}
		} break;
		case Variant::DICTIONARY: {
			const Dictionary dictionary = p_value;
			_collect_container_type(
					dictionary.get_key_type(),
					p_source + " typed Dictionary key", r_state);
			_collect_container_type(
					dictionary.get_value_type(),
					p_source + " typed Dictionary value", r_state);
			for (int i = 0; i < dictionary.size(); i++) {
				_collect_variant(dictionary.get_key_at_index(i), p_source, r_state, p_depth + 1);
				_collect_variant(dictionary.get_value_at_index(i), p_source, r_state, p_depth + 1);
			}
		} break;
		case Variant::PACKED_STRING_ARRAY: {
			const PackedStringArray strings = p_value;
			for (const String &string : strings) {
				_add_string_evidence(string, p_source, r_state);
			}
		} break;
		case Variant::OBJECT: {
			Object *object = p_value;
			if (Resource *resource = Object::cast_to<Resource>(object)) {
				_add_protected_path(
						resource->get_path(), p_source + " resource", r_state);
			}
			_collect_external_script_surface(
					Object::cast_to<Script>(object), p_source, r_state,
					p_depth + 1);
			if (FSSpecializedClassHandle *specialized =
							Object::cast_to<FSSpecializedClassHandle>(object)) {
				_collect_external_script_surface(
						specialized->get_specialized_script().ptr(),
						p_source + " specialized script", r_state,
						p_depth + 1);
				for (const ContainerType &type_argument :
						specialized->get_type_arguments()) {
					_collect_container_type(
							type_argument,
							p_source + " specialized type argument",
							r_state);
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
			// These packed forms carry only numeric/vector/color payloads.
			break;
		default:
			break;
	}
}

void FSNameManglerAnalysis::_collect_function(const FSFunction *p_function, BuildState &r_state) {
	if (p_function == nullptr || r_state.visited_functions.has(p_function)) {
		return;
	}
	r_state.visited_functions.insert(p_function);

	const String source = p_function->source;
	_add_protected_path(
			source, source + " function source path", r_state);
	r_state.observed_names.insert(p_function->name);
	for (const PropertyInfo &argument : p_function->method_info.arguments) {
		r_state.observed_names.insert(argument.name);
		_collect_property_info(
				argument, source + " function argument", r_state);
	}
	_collect_property_info(
			p_function->method_info.return_val,
			source + " function return", r_state);
	for (const FSDataType &argument_type : p_function->argument_types) {
		_collect_data_type(
				argument_type, source + " function argument type", r_state);
	}
	_collect_data_type(
			p_function->return_type, source + " function return type",
			r_state);
	for (const Variant &default_argument : p_function->method_info.default_arguments) {
		_collect_variant(default_argument, source, r_state);
	}
	_collect_variant(p_function->rpc_config, source, r_state);
	for (const Variant &constant : p_function->constants) {
		_collect_variant(constant, source, r_state);
	}
	const auto record_reflection_kind =
			[&](uint8_t p_kind, const StringName &p_method) {
				if ((p_function->unresolved_reflection_kinds & p_kind) != 0) {
					_record_reflection_use(
							p_method, StringName(), source, r_state);
				}
				if ((p_function->self_reflection_kinds & p_kind) == 0) {
					return;
				}
				const FoundryScript *owner = p_function->_script;
				if (owner == nullptr ||
						!r_state.included_classes.has(owner)) {
					_record_reflection_use(
							p_method, StringName(), source, r_state);
					return;
				}
				BuildState::ScopedReflectionUse use;
				use.owner = owner;
				use.kind = p_kind;
				use.detail = source;
				if (!use.detail.is_empty()) {
					use.detail += " calls ";
				}
				use.detail += String(p_method) + " on self";
				for (const BuildState::ScopedReflectionUse &existing :
						r_state.scoped_reflection_uses) {
					if (existing.owner == use.owner &&
							existing.kind == use.kind &&
							existing.detail == use.detail) {
						return;
					}
				}
				r_state.scoped_reflection_uses.push_back(use);
			};
	record_reflection_kind(
			FSFunction::REFLECTION_METHODS, SNAME("get_method_list"));
	record_reflection_kind(
			FSFunction::REFLECTION_PROPERTIES, SNAME("get_property_list"));
	record_reflection_kind(
			FSFunction::REFLECTION_SIGNALS, SNAME("get_signal_list"));
	for (const StringName &global_name : p_function->global_names) {
		r_state.observed_names.insert(global_name);
	}
	for (const FSFunction::ExportFixups::TypedNameKey &key : p_function->export_fixups.setters) {
		_add_protected_name(
				key.name, source + " Variant setter fixup", r_state);
	}
	for (const FSFunction::ExportFixups::TypedNameKey &key : p_function->export_fixups.getters) {
		_add_protected_name(
				key.name, source + " Variant getter fixup", r_state);
	}
	for (const FSFunction::ExportFixups::TypedNameKey &key : p_function->export_fixups.builtin_methods) {
		_add_protected_name(
				key.name, source + " Variant builtin-method fixup",
				r_state);
	}
	for (const FSFunction::ExportFixups::MethodBindKey &key : p_function->export_fixups.method_binds) {
		_add_protected_identity(
				String(key.class_name), source + " MethodBind class",
				r_state);
		_add_protected_name(
				key.method_name, source + " MethodBind method", r_state);
	}
	for (const StringName &utility :
			p_function->export_fixups.utilities) {
		_add_protected_name(
				utility, source + " Variant utility fixup", r_state);
	}
	for (const StringName &utility :
			p_function->export_fixups.gds_utilities) {
		_add_protected_name(
				utility, source + " Foundry utility fixup", r_state);
	}
	for (const FSFunction::ExportFixups::GlobalStore &global_store : p_function->export_fixups.global_stores) {
		_add_protected_name(
				global_store.global_name, source + " global-store fixup",
				r_state);
	}
	for (const StringName &name : p_function->export_fixups.named_globals) {
		_add_protected_name(
				name, source + " named-global fixup", r_state);
	}
	for (const StringName &name : p_function->builtin_method_names) {
		_add_protected_name(
				name, source + " builtin method table", r_state);
	}
	for (const FSFunction *lambda : p_function->lambdas) {
		_collect_function(lambda, r_state);
	}
}

void FSNameManglerAnalysis::_collect_class(const FoundryScript *p_class, BuildState &r_state) {
	if (p_class == nullptr || r_state.visited_classes.has(p_class)) {
		return;
	}
	r_state.visited_classes.insert(p_class);

	const String source = p_class->get_script_path();
	_add_protected_path(
			p_class->get_script_path(), source + " script path", r_state);
	_add_protected_path(
			p_class->simplified_icon_path, source + " icon path", r_state);
	_add_protected_path(
			p_class->registered_conformance_source,
			source + " conformance source path", r_state);
	_collect_external_class_surface(p_class->base.ptr(), source + " external base", r_state);
	_add_candidate(p_class->local_name, IDENTIFIER_CLASS, r_state);
	r_state.observed_names.insert(p_class->global_name);
	r_state.observed_names.insert(StringName(p_class->fully_qualified_name));

	for (const StringName &member : p_class->members) {
		_add_candidate(member, IDENTIFIER_MEMBER, r_state);
		r_state.property_declarations[p_class].insert(member);
		const HashMap<StringName, Vector<FoundryScript::AnnotationUsage>>::ConstIterator annotations =
				p_class->variable_annotations.find(member);
		if (annotations) {
			for (const FoundryScript::AnnotationUsage &usage : annotations->value) {
				if (usage.is_builtin && String(usage.name).begins_with("export")) {
					_add_evidence(member, KEEP_SCENE_OR_RESOURCE, source + " exported property", r_state);
					break;
				}
			}
		}
	}
	for (const KeyValue<StringName, FoundryScript::MemberInfo> &member :
			p_class->member_indices) {
		r_state.property_declarations[p_class].insert(member.key);
		r_state.observed_names.insert(
				StringName(member.value.property_info.name));
		_collect_data_type(
				member.value.data_type, source + " member type", r_state);
		_collect_data_type(
				member.value.type_argument_binding.fixed,
				source + " member type argument binding", r_state);
		_collect_property_info(
				member.value.property_info, source + " member property",
				r_state);
	}
	for (const KeyValue<StringName, FoundryScript::MemberInfo> &static_variable : p_class->static_variables_indices) {
		_add_candidate(static_variable.key, IDENTIFIER_MEMBER, r_state);
		r_state.property_declarations[p_class].insert(static_variable.key);
		r_state.observed_names.insert(
				StringName(static_variable.value.property_info.name));
		_collect_data_type(
				static_variable.value.data_type,
				source + " static member type", r_state);
		_collect_data_type(
				static_variable.value.type_argument_binding.fixed,
				source + " static member type argument binding", r_state);
		_collect_property_info(
				static_variable.value.property_info,
				source + " static member property", r_state);
	}
	for (const FoundryScript::TypeArgumentBinding &binding :
			p_class->member_type_argument_bindings) {
		_collect_data_type(
				binding.fixed, source + " member type argument vector",
				r_state);
	}
	for (const KeyValue<FoundryScript *,
				 Vector<FoundryScript::TypeArgumentBinding>> &ancestor :
			p_class->type_parameter_bindings_by_ancestor) {
		_collect_external_script_surface(
				ancestor.key, source + " ancestor type binding", r_state);
		for (const FoundryScript::TypeArgumentBinding &binding :
				ancestor.value) {
			_collect_data_type(
					binding.fixed, source + " ancestor type argument binding",
					r_state);
		}
	}
	for (const KeyValue<StringName, FoundryScript::MemberInfo> &old_static :
			p_class->old_static_variables_indices) {
		r_state.observed_names.insert(old_static.key);
		r_state.observed_names.insert(
				StringName(old_static.value.property_info.name));
		_collect_data_type(
				old_static.value.data_type,
				source + " old static member type", r_state);
		_collect_data_type(
				old_static.value.type_argument_binding.fixed,
				source + " old static member type argument binding",
				r_state);
		_collect_property_info(
				old_static.value.property_info,
				source + " old static member property", r_state);
	}
	for (const KeyValue<StringName, Variant> &constant : p_class->constants) {
		_add_candidate(constant.key, IDENTIFIER_ENUM_OR_CONSTANT, r_state);
		_collect_variant(constant.value, source, r_state);
	}
	for (const KeyValue<StringName, MethodInfo> &signal : p_class->_signals) {
		_add_candidate(signal.key, IDENTIFIER_SIGNAL, r_state);
		r_state.signal_declarations[p_class].insert(signal.key);
		_collect_property_info(
				signal.value.return_val, source + " signal return", r_state);
		for (const PropertyInfo &argument : signal.value.arguments) {
			r_state.observed_names.insert(argument.name);
			_collect_property_info(
					argument, source + " signal argument", r_state);
		}
		for (const Variant &default_argument :
				signal.value.default_arguments) {
			_collect_variant(
					default_argument, source + " signal default argument",
					r_state);
		}
	}
	for (const KeyValue<StringName, FSFunction *> &method : p_class->member_functions) {
		_add_candidate(method.key, IDENTIFIER_METHOD, r_state);
		r_state.method_declarations[p_class].insert(method.key);
		_collect_function(method.value, r_state);
	}
	for (const KeyValue<StringName, FoundryScript::EnumFunctionSet> &enum_entry : p_class->enum_functions) {
		_add_candidate(enum_entry.key, IDENTIFIER_ENUM_OR_CONSTANT, r_state);
		for (const KeyValue<StringName, FSFunction *> &method : enum_entry.value.instance_functions) {
			_add_candidate(method.key, IDENTIFIER_METHOD, r_state);
			r_state.method_declarations[p_class].insert(method.key);
			_collect_function(method.value, r_state);
		}
		for (const KeyValue<StringName, FSFunction *> &method : enum_entry.value.static_functions) {
			_add_candidate(method.key, IDENTIFIER_METHOD, r_state);
			r_state.method_declarations[p_class].insert(method.key);
			_collect_function(method.value, r_state);
		}
	}
	for (const StringName &trait_name : p_class->script_trait_list) {
		_collect_class_identity_reference(String(trait_name), source + " trait identity", r_state);
	}
	for (const KeyValue<StringName, FoundryScript::AbstractTraitRequirement> &requirement :
			p_class->abstract_trait_requirements) {
		_add_candidate(requirement.key, IDENTIFIER_METHOD, r_state);
		r_state.method_declarations[p_class].insert(requirement.key);
		for (const PropertyInfo &argument : requirement.value.method_info.arguments) {
			r_state.observed_names.insert(argument.name);
			_collect_property_info(
					argument, source + " trait requirement argument",
					r_state);
		}
		_collect_data_type(
				requirement.value.return_type,
				source + " trait requirement return", r_state);
		_collect_property_info(
				requirement.value.method_info.return_val,
				source + " trait requirement return property", r_state);
		for (const Variant &default_argument : requirement.value.method_info.default_arguments) {
			_collect_variant(default_argument, source, r_state);
		}
	}
	for (const FoundryScript::TypeParameter &type_parameter : p_class->type_parameters) {
		r_state.observed_names.insert(type_parameter.name);
		_collect_property_info(
				type_parameter.bound, source + " type parameter bound",
				r_state);
	}
	if (!p_class->registered_conformance_source.is_empty()) {
		const Vector<FSConformanceRegistry::RuntimeConformance> conformances =
				FSConformanceRegistry::get_singleton()->get_runtime_witnesses(
						p_class->registered_conformance_source);
		for (const FSConformanceRegistry::RuntimeConformance &conformance : conformances) {
			_collect_external_script_surface(
					conformance.target_script,
					source + " conformance target script", r_state);
			for (const String &target_key : conformance.target_keys) {
				_collect_class_identity_reference(
						target_key, source + " conformance target identity", r_state);
			}
			_collect_class_identity_reference(
					String(conformance.trait_name), source + " conformance trait identity", r_state);
			for (const KeyValue<StringName, FSFunction *> &witness : conformance.functions) {
				_add_candidate(witness.key, IDENTIFIER_METHOD, r_state);
				if (conformance.target_script != nullptr) {
					r_state.method_declarations[conformance.target_script]
							.insert(witness.key);
				}
				_collect_function(witness.value, r_state);
			}
		}
	}

	for (const Variant &value : p_class->static_variables) {
		_collect_variant(value, source, r_state);
	}
	for (const KeyValue<StringName, Variant> &default_value : p_class->member_default_values) {
		r_state.observed_names.insert(default_value.key);
		_collect_variant(default_value.value, source, r_state);
	}
	for (const KeyValue<StringName, Variant> &default_value :
			p_class->member_default_values_cache) {
		r_state.observed_names.insert(default_value.key);
		_collect_variant(
				default_value.value, source + " cached member default", r_state);
	}
	for (const KeyValue<StringName, int> &member_line :
			p_class->member_lines) {
		r_state.observed_names.insert(member_line.key);
	}
	for (const PropertyInfo &property : p_class->members_cache) {
		r_state.observed_names.insert(StringName(property.name));
		_collect_property_info(
				property, source + " cached member property", r_state);
	}
	_collect_variant(p_class->rpc_config, source, r_state);
	for (const Variant &key : p_class->rpc_config.keys()) {
		const StringName method_name = key;
		_add_evidence(method_name, KEEP_RPC, source, r_state);
	}

	const auto collect_annotations = [&](const Vector<FoundryScript::AnnotationUsage> &p_usages,
											 const StringName &p_declaration_name = StringName()) {
		for (const FoundryScript::AnnotationUsage &usage : p_usages) {
			_add_protected_name(
					usage.name, source + " annotation name", r_state);
			_add_protected_identity(
					String(usage.qualified_name),
					source + " annotation identity", r_state);
			_collect_variant(usage.args, source, r_state);
			_collect_variant(usage.kwargs, source, r_state);
			if (p_declaration_name != StringName() && usage.is_builtin && usage.name == SNAME("keep_name")) {
				_add_evidence(p_declaration_name, KEEP_RULE,
						vformat("@keep_name on %s::%s", p_class->fully_qualified_name, p_declaration_name),
						r_state);
			}
		}
	};
	collect_annotations(p_class->class_annotations);
	for (const FoundryScript::AnnotationUsage &usage : p_class->class_annotations) {
		if (!usage.is_builtin || usage.name != SNAME("keep_name")) {
			continue;
		}
		const String detail = vformat("@keep_name on class %s", p_class->fully_qualified_name);
		_add_evidence(p_class->local_name, KEEP_RULE, detail, r_state);
	}
	for (const KeyValue<StringName, Vector<FoundryScript::AnnotationUsage>> &entry : p_class->method_annotations) {
		r_state.observed_names.insert(entry.key);
		collect_annotations(entry.value, entry.key);
	}
	for (const KeyValue<StringName, Vector<FoundryScript::AnnotationUsage>> &entry : p_class->variable_annotations) {
		r_state.observed_names.insert(entry.key);
		collect_annotations(entry.value, entry.key);
	}
	for (const KeyValue<StringName, Vector<FoundryScript::AnnotationUsage>> &entry : p_class->signal_annotations) {
		r_state.observed_names.insert(entry.key);
		collect_annotations(entry.value, entry.key);
	}
	for (const KeyValue<StringName, Vector<FoundryScript::AnnotationUsage>> &entry : p_class->constant_annotations) {
		r_state.observed_names.insert(entry.key);
		collect_annotations(entry.value, entry.key);
	}
	const auto collect_parameter_annotations =
			[&](const HashMap<StringName, HashMap<StringName, Vector<FoundryScript::AnnotationUsage>>> &p_annotations) {
				for (const KeyValue<StringName, HashMap<StringName, Vector<FoundryScript::AnnotationUsage>>> &owner :
						p_annotations) {
					r_state.observed_names.insert(owner.key);
					for (const KeyValue<StringName, Vector<FoundryScript::AnnotationUsage>> &parameter : owner.value) {
						r_state.observed_names.insert(parameter.key);
						collect_annotations(parameter.value);
					}
				}
			};
	collect_parameter_annotations(p_class->method_parameter_annotations);
	collect_parameter_annotations(p_class->signal_parameter_annotations);

	if (p_class->native.is_valid() && ClassDB::class_exists(p_class->native->get_name())) {
		const StringName native_name = p_class->native->get_name();
		List<MethodInfo> virtual_methods;
		ClassDB::get_virtual_methods(native_name, &virtual_methods);
		for (const MethodInfo &method : virtual_methods) {
			if (p_class->member_functions.has(method.name)) {
				_add_evidence(method.name, KEEP_NATIVE_VIRTUAL, String(native_name), r_state);
			}
		}
		for (const KeyValue<StringName, FSFunction *> &method : p_class->member_functions) {
			if (ClassDB::has_method(native_name, method.key)) {
				_add_evidence(method.key, KEEP_EXTERNAL_OR_UNPROVABLE,
						vformat("%s native method", native_name), r_state);
			}
		}
		for (const StringName &member : p_class->members) {
			if (ClassDB::has_property(native_name, member)) {
				_add_evidence(member, KEEP_EXTERNAL_OR_UNPROVABLE,
						vformat("%s native property", native_name), r_state);
			}
		}
		for (const KeyValue<StringName, MethodInfo> &signal : p_class->_signals) {
			if (ClassDB::has_signal(native_name, signal.key)) {
				_add_evidence(signal.key, KEEP_EXTERNAL_OR_UNPROVABLE,
						vformat("%s native signal", native_name), r_state);
			}
		}
	}

	_collect_function(p_class->implicit_initializer, r_state);
	_collect_function(p_class->implicit_ready, r_state);
	_collect_function(p_class->static_initializer, r_state);
	for (const FSFunction *witness : p_class->witness_functions) {
		_collect_function(witness, r_state);
	}
	for (const KeyValue<StringName, Ref<FoundryScript>> &subclass : p_class->subclasses) {
		_collect_class(subclass.value.ptr(), r_state);
	}
}

FSNameManglerAnalysis::Result FSNameManglerAnalysis::analyze(const Input &p_input) {
	Result result;
	BuildState state;

	for (const Ref<FoundryScript> &script : p_input.scripts) {
		if (script.is_null()) {
			result.error = ERR_INVALID_PARAMETER;
			return result;
		}
		_index_class(script.ptr(), state);
	}
	if (!p_input.scripts.is_empty()) {
		_index_global_protected_names(state);
	}
	for (const Ref<FoundryScript> &script : p_input.scripts) {
		_collect_class(script.ptr(), state);
	}

	for (const KeepEvidence &evidence : p_input.keep_evidence) {
		state.observed_names.insert(evidence.name);
		_add_evidence(evidence.name, evidence.reason, evidence.detail, state);
	}
	for (const KeyValue<String, Vector<String>> &string_entry : state.string_sources) {
		const StringName name = StringName(string_entry.key);
		for (const String &source : string_entry.value) {
			_add_evidence(name, KEEP_STRING_LITERAL, source, state);
		}
	}
	for (const KeyValue<StringName, Vector<String>> &external_entry : state.external_sources) {
		for (const String &source : external_entry.value) {
			_add_evidence(external_entry.key, KEEP_EXTERNAL_OR_UNPROVABLE, source, state);
		}
	}
	for (const KeyValue<StringName, Vector<String>> &protected_entry :
			state.protected_sources) {
		for (const String &source : protected_entry.value) {
			_add_evidence(
					protected_entry.key, KEEP_EXTERNAL_OR_UNPROVABLE,
					source, state);
		}
	}
	for (const KeyValue<StringName, BuildState::Aggregate> &candidate :
			state.candidates) {
		const String candidate_name = candidate.key;
		for (const KeyValue<String, Vector<String>> &path_entry :
				state.protected_path_sources) {
			if (!path_entry.key.contains(candidate_name)) {
				continue;
			}
			for (const String &source : path_entry.value) {
				_add_evidence(
						candidate.key, KEEP_EXTERNAL_OR_UNPROVABLE,
						source + " path " + path_entry.key, state);
			}
		}
		if (state.unscannable_protected_surface) {
			_add_evidence(
					candidate.key, KEEP_EXTERNAL_OR_UNPROVABLE,
					"protected serialized surface exceeded recursion limits",
					state);
		}
	}
	for (const FoundryScript *receiver : state.included_classes) {
		HashSet<StringName> visible_methods;
		HashSet<StringName> visible_properties;
		HashSet<StringName> visible_signals;
		HashSet<const FoundryScript *> visited;
		const FoundryScript *current = receiver;
		while (current != nullptr &&
				state.included_classes.has(current) &&
				!visited.has(current)) {
			visited.insert(current);
			const HashSet<StringName> *methods =
					state.method_declarations.getptr(current);
			if (methods != nullptr) {
				for (const StringName &name : *methods) {
					visible_methods.insert(name);
				}
			}
			const HashSet<StringName> *properties =
					state.property_declarations.getptr(current);
			if (properties != nullptr) {
				for (const StringName &name : *properties) {
					visible_properties.insert(name);
				}
			}
			const HashSet<StringName> *signals =
					state.signal_declarations.getptr(current);
			if (signals != nullptr) {
				for (const StringName &name : *signals) {
					visible_signals.insert(name);
				}
			}
			current = current->base.ptr();
		}

		visited.clear();
		current = receiver;
		while (current != nullptr &&
				state.included_classes.has(current) &&
				!visited.has(current)) {
			visited.insert(current);
			HashSet<StringName> &method_names =
					state.scoped_method_reflection_names[current];
			for (const StringName &name : visible_methods) {
				method_names.insert(name);
			}
			HashSet<StringName> &property_names =
					state.scoped_property_reflection_names[current];
			for (const StringName &name : visible_properties) {
				property_names.insert(name);
			}
			HashSet<StringName> &signal_names =
					state.scoped_signal_reflection_names[current];
			for (const StringName &name : visible_signals) {
				signal_names.insert(name);
			}
			current = current->base.ptr();
		}
	}
	const auto reflection_name_is_visible =
			[&](const FoundryScript *p_owner, uint8_t p_kind,
					const StringName &p_name) {
				const HashSet<StringName> *names = nullptr;
				switch (p_kind) {
					case FSFunction::REFLECTION_METHODS:
						names =
								state.scoped_method_reflection_names.getptr(
										p_owner);
						break;
					case FSFunction::REFLECTION_PROPERTIES:
						names =
								state.scoped_property_reflection_names.getptr(
										p_owner);
						break;
					case FSFunction::REFLECTION_SIGNALS:
						names =
								state.scoped_signal_reflection_names.getptr(
										p_owner);
						break;
					default:
						break;
				}
				return names != nullptr && names->has(p_name);
			};
	for (const KeyValue<StringName, BuildState::Aggregate> &candidate :
			state.candidates) {
		const auto add_reflection_evidence =
				[&](const Vector<String> &p_sources) {
					for (const String &source : p_sources) {
						_add_evidence(
								candidate.key, KEEP_REFLECTION, source,
								state);
					}
				};
		if (candidate.value.kinds.has((int)IDENTIFIER_METHOD)) {
			add_reflection_evidence(
					state.unresolved_method_reflection_sources);
		}
		if (candidate.value.kinds.has((int)IDENTIFIER_MEMBER)) {
			add_reflection_evidence(
					state.unresolved_property_reflection_sources);
		}
		if (candidate.value.kinds.has((int)IDENTIFIER_SIGNAL)) {
			add_reflection_evidence(
					state.unresolved_signal_reflection_sources);
		}
		for (const BuildState::ScopedReflectionUse &use :
				state.scoped_reflection_uses) {
			const bool compatible =
					(use.kind == FSFunction::REFLECTION_METHODS &&
							candidate.value.kinds.has(
									(int)IDENTIFIER_METHOD)) ||
					(use.kind == FSFunction::REFLECTION_PROPERTIES &&
							candidate.value.kinds.has(
									(int)IDENTIFIER_MEMBER)) ||
					(use.kind == FSFunction::REFLECTION_SIGNALS &&
							candidate.value.kinds.has(
									(int)IDENTIFIER_SIGNAL));
			if (compatible &&
					reflection_name_is_visible(
							use.owner, use.kind, candidate.key)) {
				_add_evidence(
						candidate.key, KEEP_REFLECTION, use.detail, state);
			}
		}
	}
	if (!p_input.complete_project_graph) {
		for (const KeyValue<StringName, BuildState::Aggregate> &candidate : state.candidates) {
			_add_evidence(candidate.key, KEEP_EXTERNAL_OR_UNPROVABLE,
					"whole-program input was incomplete", state);
		}
	}

	Vector<StringName> names;
	for (const KeyValue<StringName, BuildState::Aggregate> &candidate : state.candidates) {
		names.push_back(candidate.key);
	}
	names.sort_custom<NameComparator>();

	uint64_t replacement_ordinal = 0;
	for (const StringName &name : names) {
		BuildState::Aggregate &aggregate = state.candidates[name];
		Classification classification;
		classification.name = name;
		for (const int kind : aggregate.kinds) {
			classification.kinds.push_back((IdentifierKind)kind);
		}
		classification.kinds.sort();

		classification.keep_evidence = aggregate.evidence;
		classification.keep_evidence.sort_custom<EvidenceComparator>();
		for (int i = classification.keep_evidence.size() - 1; i > 0; i--) {
			if (evidence_matches(classification.keep_evidence[i - 1], classification.keep_evidence[i])) {
				classification.keep_evidence.remove_at(i);
			}
		}

		if (classification.is_kept()) {
			for (const KeepEvidence &evidence : classification.keep_evidence) {
				String line = vformat("Keeping \"%s\": %s", name, get_keep_reason_label(evidence.reason));
				if (!evidence.detail.is_empty()) {
					line += " (" + evidence.detail + ")";
				}
				result.keep_log.push_back(line + ".");
			}
		} else {
			StringName replacement;
			const auto replacement_occurs_in_protected_path =
					[&](const StringName &p_replacement) {
						const String replacement_text = p_replacement;
						for (const KeyValue<String, Vector<String>> &path_entry :
								state.protected_path_sources) {
							if (path_entry.key.contains(replacement_text)) {
								return true;
							}
						}
						return false;
					};
			do {
				replacement = StringName("_fsb_" + String::num_uint64(replacement_ordinal++, 36));
			} while (state.observed_names.has(replacement) ||
					replacement_occurs_in_protected_path(replacement));
			classification.replacement = replacement;
			state.observed_names.insert(replacement);
			result.rename_map.insert(name, replacement);
		}

		result.classifications.push_back(classification);
	}

	return result;
}

#endif // TOOLS_ENABLED
