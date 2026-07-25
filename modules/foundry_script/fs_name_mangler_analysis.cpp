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

#include "fs_function.h"

#include "core/object/class_db.h"
#include "core/variant/callable.h"

struct FSNameManglerAnalysis::BuildState {
	struct Aggregate {
		HashSet<int> kinds;
		Vector<KeepEvidence> evidence;
	};

	HashMap<StringName, Aggregate> candidates;
	HashSet<StringName> observed_names;
	HashMap<String, Vector<String>> string_sources;
	HashMap<StringName, Vector<String>> external_sources;
	Vector<String> method_reflection_sources;
	Vector<String> property_reflection_sources;
	Vector<String> signal_reflection_sources;
	HashSet<const FoundryScript *> included_classes;
	HashSet<const FoundryScript *> visited_classes;
	HashSet<const FoundryScript *> visited_external_classes;
	HashSet<const FSFunction *> visited_functions;
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

bool kinds_have(const Vector<FSNameManglerAnalysis::IdentifierKind> &p_kinds,
		FSNameManglerAnalysis::IdentifierKind p_kind) {
	for (const FSNameManglerAnalysis::IdentifierKind kind : p_kinds) {
		if (kind == p_kind) {
			return true;
		}
	}
	return false;
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

void FSNameManglerAnalysis::_record_reflection_use(
		const StringName &p_method, const StringName &p_class, const String &p_source, BuildState &r_state) {
	Vector<String> *sources = nullptr;
	if (p_method == SNAME("get_method_list") ||
			(p_class == SNAME("FSReflection") &&
					(p_method == SNAME("get_methods") || p_method == SNAME("get_method_descriptors")))) {
		sources = &r_state.method_reflection_sources;
	} else if (p_method == SNAME("get_property_list") ||
			(p_class == SNAME("FSReflection") &&
					(p_method == SNAME("get_properties") || p_method == SNAME("get_property_descriptors")))) {
		sources = &r_state.property_reflection_sources;
	} else if (p_method == SNAME("get_signal_list")) {
		sources = &r_state.signal_reflection_sources;
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

void FSNameManglerAnalysis::_collect_variant(
		const Variant &p_value, const String &p_source, BuildState &r_state, int p_depth) {
	if (p_depth > Variant::MAX_RECURSION_DEPTH) {
		return;
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
			for (int i = 0; i < array.size(); i++) {
				_collect_variant(array[i], p_source, r_state, p_depth + 1);
			}
		} break;
		case Variant::DICTIONARY: {
			const Dictionary dictionary = p_value;
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
			const FoundryScript *script = Object::cast_to<FoundryScript>(object);
			_collect_external_class_surface(script, p_source, r_state);
		} break;
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
	r_state.observed_names.insert(p_function->name);
	for (const PropertyInfo &argument : p_function->method_info.arguments) {
		r_state.observed_names.insert(argument.name);
	}
	for (const Variant &default_argument : p_function->method_info.default_arguments) {
		_collect_variant(default_argument, source, r_state);
	}
	_collect_variant(p_function->rpc_config, source, r_state);
	for (const Variant &constant : p_function->constants) {
		_collect_variant(constant, source, r_state);
	}
	for (const StringName &global_name : p_function->global_names) {
		r_state.observed_names.insert(global_name);
		_record_reflection_use(global_name, StringName(), source, r_state);
	}
	for (const FSFunction::ExportFixups::TypedNameKey &key : p_function->export_fixups.setters) {
		r_state.observed_names.insert(key.name);
	}
	for (const FSFunction::ExportFixups::TypedNameKey &key : p_function->export_fixups.getters) {
		r_state.observed_names.insert(key.name);
	}
	for (const FSFunction::ExportFixups::TypedNameKey &key : p_function->export_fixups.builtin_methods) {
		r_state.observed_names.insert(key.name);
	}
	for (const FSFunction::ExportFixups::MethodBindKey &key : p_function->export_fixups.method_binds) {
		r_state.observed_names.insert(key.class_name);
		r_state.observed_names.insert(key.method_name);
		_record_reflection_use(key.method_name, key.class_name, source, r_state);
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
	_collect_external_class_surface(p_class->base.ptr(), source + " external base", r_state);
	_add_candidate(p_class->local_name, IDENTIFIER_CLASS, r_state);
	r_state.observed_names.insert(p_class->global_name);
	r_state.observed_names.insert(StringName(p_class->fully_qualified_name));

	for (const StringName &member : p_class->members) {
		_add_candidate(member, IDENTIFIER_MEMBER, r_state);
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
	for (const KeyValue<StringName, FoundryScript::MemberInfo> &static_variable : p_class->static_variables_indices) {
		_add_candidate(static_variable.key, IDENTIFIER_MEMBER, r_state);
	}
	for (const KeyValue<StringName, Variant> &constant : p_class->constants) {
		_add_candidate(constant.key, IDENTIFIER_ENUM_OR_CONSTANT, r_state);
		_collect_variant(constant.value, source, r_state);
	}
	for (const KeyValue<StringName, MethodInfo> &signal : p_class->_signals) {
		_add_candidate(signal.key, IDENTIFIER_SIGNAL, r_state);
		for (const PropertyInfo &argument : signal.value.arguments) {
			r_state.observed_names.insert(argument.name);
		}
	}
	for (const KeyValue<StringName, FSFunction *> &method : p_class->member_functions) {
		_add_candidate(method.key, IDENTIFIER_METHOD, r_state);
		_collect_function(method.value, r_state);
	}
	for (const KeyValue<StringName, FoundryScript::EnumFunctionSet> &enum_entry : p_class->enum_functions) {
		_add_candidate(enum_entry.key, IDENTIFIER_ENUM_OR_CONSTANT, r_state);
		for (const KeyValue<StringName, FSFunction *> &method : enum_entry.value.instance_functions) {
			_add_candidate(method.key, IDENTIFIER_METHOD, r_state);
			_collect_function(method.value, r_state);
		}
		for (const KeyValue<StringName, FSFunction *> &method : enum_entry.value.static_functions) {
			_add_candidate(method.key, IDENTIFIER_METHOD, r_state);
			_collect_function(method.value, r_state);
		}
	}

	for (const Variant &value : p_class->static_variables) {
		_collect_variant(value, source, r_state);
	}
	for (const KeyValue<StringName, Variant> &default_value : p_class->member_default_values) {
		_collect_variant(default_value.value, source, r_state);
	}
	_collect_variant(p_class->rpc_config, source, r_state);
	for (const Variant &key : p_class->rpc_config.keys()) {
		const StringName method_name = key;
		_add_evidence(method_name, KEEP_RPC, source, r_state);
	}

	const auto collect_annotations = [&](const Vector<FoundryScript::AnnotationUsage> &p_usages) {
		for (const FoundryScript::AnnotationUsage &usage : p_usages) {
			_collect_variant(usage.args, source, r_state);
			_collect_variant(usage.kwargs, source, r_state);
		}
	};
	collect_annotations(p_class->class_annotations);
	for (const KeyValue<StringName, Vector<FoundryScript::AnnotationUsage>> &entry : p_class->method_annotations) {
		collect_annotations(entry.value);
	}
	for (const KeyValue<StringName, Vector<FoundryScript::AnnotationUsage>> &entry : p_class->variable_annotations) {
		collect_annotations(entry.value);
	}
	for (const KeyValue<StringName, Vector<FoundryScript::AnnotationUsage>> &entry : p_class->signal_annotations) {
		collect_annotations(entry.value);
	}
	for (const KeyValue<StringName, Vector<FoundryScript::AnnotationUsage>> &entry : p_class->constant_annotations) {
		collect_annotations(entry.value);
	}

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

bool FSNameManglerAnalysis::_collides_with_builtin_api(
		const StringName &p_name, const Vector<IdentifierKind> &p_kinds) {
	for (int type = 0; type < Variant::VARIANT_MAX; type++) {
		if (kinds_have(p_kinds, IDENTIFIER_METHOD) &&
				Variant::has_builtin_method((Variant::Type)type, p_name)) {
			return true;
		}
		if (kinds_have(p_kinds, IDENTIFIER_MEMBER) &&
				Variant::has_member((Variant::Type)type, p_name)) {
			return true;
		}
	}
	return false;
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
	for (const KeyValue<StringName, BuildState::Aggregate> &candidate : state.candidates) {
		const auto add_reflection_evidence = [&](const Vector<String> &p_sources) {
			for (const String &source : p_sources) {
				_add_evidence(candidate.key, KEEP_REFLECTION, source, state);
			}
		};
		if (candidate.value.kinds.has((int)IDENTIFIER_METHOD)) {
			add_reflection_evidence(state.method_reflection_sources);
		}
		if (candidate.value.kinds.has((int)IDENTIFIER_MEMBER)) {
			add_reflection_evidence(state.property_reflection_sources);
		}
		if (candidate.value.kinds.has((int)IDENTIFIER_SIGNAL)) {
			add_reflection_evidence(state.signal_reflection_sources);
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
		if (_collides_with_builtin_api(name, classification.kinds)) {
			KeepEvidence evidence;
			evidence.name = name;
			evidence.reason = KEEP_EXTERNAL_OR_UNPROVABLE;
			evidence.detail = "builtin Variant API";
			classification.keep_evidence.push_back(evidence);
		}
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
			do {
				replacement = StringName("_fsb_" + String::num_uint64(replacement_ordinal++, 36));
			} while (state.observed_names.has(replacement));
			classification.replacement = replacement;
			state.observed_names.insert(replacement);
			result.rename_map.insert(name, replacement);
		}

		result.classifications.push_back(classification);
	}

	return result;
}

#endif // TOOLS_ENABLED
