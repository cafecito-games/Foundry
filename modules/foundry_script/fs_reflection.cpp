/**************************************************************************/
/*  fs_reflection.cpp                                                     */
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

#include "fs_reflection.h"

#include "foundry_script.h"
#include "fs_proxy.h"

#include "core/variant/container_type_validate.h"

Ref<Script> FSReflection::_resolve_script(const Variant &p_target) {
	if (p_target.get_type() != Variant::OBJECT) {
		return Ref<Script>();
	}
	// Use the validated object so a freed target yields null instead of a stale
	// pointer that would be dereferenced below.
	Object *object = p_target.get_validated_object();
	if (object == nullptr) {
		return Ref<Script>();
	}
	// A script type passed directly.
	if (Script *script = Object::cast_to<Script>(object)) {
		return Ref<Script>(script);
	}
	// An instance: use its script.
	if (ScriptInstance *instance = object->get_script_instance()) {
		return instance->get_script();
	}
	return Ref<Script>();
}

namespace {
// Walks the script base chain to find the most-derived script that declares the named member, so an
// overriding declaration's annotations are effective even when it carries none (the base's are then
// not returned). A member is "declared" by a script if it owns the compiled member or recorded
// annotations for it; the latter also covers static variables, which are not listed in `members`.
const Vector<FoundryScript::AnnotationUsage> *find_effective_method_annotations(const FoundryScript *p_script, const StringName &p_method) {
	HashSet<const FoundryScript *> visited;
	for (const FoundryScript *script = p_script; script != nullptr && !visited.has(script); script = Object::cast_to<FoundryScript>(script->get_base_script().ptr())) {
		visited.insert(script);
		const bool declares = script->get_member_functions().has(p_method) || script->get_method_annotations().has(p_method);
		if (declares) {
			return script->get_method_annotations().getptr(p_method);
		}
	}
	return nullptr;
}

const Vector<FoundryScript::AnnotationUsage> *find_effective_variable_annotations(const FoundryScript *p_script, const StringName &p_variable) {
	HashSet<const FoundryScript *> visited;
	for (const FoundryScript *script = p_script; script != nullptr && !visited.has(script); script = Object::cast_to<FoundryScript>(script->get_base_script().ptr())) {
		visited.insert(script);
		const bool declares = script->get_members().has(p_variable) || script->get_variable_annotations().has(p_variable);
		if (declares) {
			return script->get_variable_annotations().getptr(p_variable);
		}
	}
	return nullptr;
}

const Vector<FoundryScript::AnnotationUsage> *find_effective_signal_annotations(const FoundryScript *p_script, const StringName &p_signal) {
	HashSet<const FoundryScript *> visited;
	for (const FoundryScript *script = p_script; script != nullptr && !visited.has(script); script = Object::cast_to<FoundryScript>(script->get_base_script().ptr())) {
		visited.insert(script);
		const bool declares = script->get_signals().has(p_signal) || script->get_signal_annotations().has(p_signal);
		if (declares) {
			return script->get_signal_annotations().getptr(p_signal);
		}
	}
	return nullptr;
}

const Vector<FoundryScript::AnnotationUsage> *find_effective_constant_annotations(const FoundryScript *p_script, const StringName &p_constant) {
	HashSet<const FoundryScript *> visited;
	for (const FoundryScript *script = p_script; script != nullptr && !visited.has(script); script = Object::cast_to<FoundryScript>(script->get_base_script().ptr())) {
		visited.insert(script);
		const bool declares = script->get_constants().has(p_constant) || script->get_constant_annotations().has(p_constant);
		if (declares) {
			return script->get_constant_annotations().getptr(p_constant);
		}
	}
	return nullptr;
}

TypedArray<FSAnnotation> usages_to_descriptors(const Vector<FoundryScript::AnnotationUsage> &p_usages) {
	TypedArray<FSAnnotation> result;
	for (const FoundryScript::AnnotationUsage &usage : p_usages) {
		result.push_back(FSAnnotation::from_usage(usage));
	}
	return result;
}

bool annotation_matches(const FoundryScript::AnnotationUsage &p_usage, const StringName &p_annotation) {
	return p_usage.name == p_annotation || p_usage.qualified_name == p_annotation;
}

// Looks up the annotations declared directly on `p_script` for `p_member` under `p_kind`, without
// walking the base chain. Trait-flattened members are copied onto the implementer at compile time,
// so they are owned by `p_script` and remain visible through this direct lookup.
const Vector<FoundryScript::AnnotationUsage> *find_direct_annotations(const FoundryScript *p_script, const StringName &p_member, const StringName &p_kind) {
	if (p_kind == SNAME("method")) {
		return p_script->get_method_annotations().getptr(p_member);
	}
	if (p_kind == SNAME("variable")) {
		return p_script->get_variable_annotations().getptr(p_member);
	}
	if (p_kind == SNAME("signal")) {
		return p_script->get_signal_annotations().getptr(p_member);
	}
	if (p_kind == SNAME("constant")) {
		return p_script->get_constant_annotations().getptr(p_member);
	}
	return nullptr;
}

// Resolves the passive custom annotation usages for `p_member` under `p_kind`: "class" (always
// direct-only on the target script, never inherited), or "method", "variable", "signal", "constant".
// For those member kinds, `p_effective` selects the view: true walks the script base chain so the
// most-derived declaring script wins (matching the effective method/property views), while false
// restricts the result to annotations declared on the exact target script. The caller holds
// `p_script` for the whole walk so the base chain it owns stays alive. Returns an empty vector for a
// null, non-FoundryScript script, or an unknown kind.
Vector<FoundryScript::AnnotationUsage> compute_annotations(const Ref<Script> &p_script, const StringName &p_member, const StringName &p_kind, bool p_effective) {
	const FoundryScript *script = Object::cast_to<FoundryScript>(p_script.ptr());
	if (script == nullptr) {
		return Vector<FoundryScript::AnnotationUsage>();
	}
	if (p_kind == SNAME("class")) {
		// Class annotations are direct-only and never inherited, so the flag has no effect here.
		return script->get_class_annotations();
	}
	const Vector<FoundryScript::AnnotationUsage> *usages = nullptr;
	if (p_effective) {
		if (p_kind == SNAME("method")) {
			usages = find_effective_method_annotations(script, p_member);
		} else if (p_kind == SNAME("variable")) {
			usages = find_effective_variable_annotations(script, p_member);
		} else if (p_kind == SNAME("signal")) {
			usages = find_effective_signal_annotations(script, p_member);
		} else if (p_kind == SNAME("constant")) {
			usages = find_effective_constant_annotations(script, p_member);
		}
	} else {
		usages = find_direct_annotations(script, p_member, p_kind);
	}
	return usages != nullptr ? *usages : Vector<FoundryScript::AnnotationUsage>();
}
} // namespace

StringName FSReflection::_resolve_trait_name(const Variant &p_trait) {
	switch (p_trait.get_type()) {
		case Variant::STRING:
		case Variant::STRING_NAME:
			return p_trait;
		case Variant::OBJECT: {
			// A trait type passed directly resolves to its trait identity.
			Ref<Script> script = _resolve_script(p_trait);
			if (script.is_valid() && script->is_trait_type()) {
				return script->get_trait_type_name();
			}
			if (script.is_valid()) {
				return script->get_global_name();
			}
		} break;
		default:
			break;
	}
	return StringName();
}

TypedArray<FSMethodDescriptor> FSReflection::get_method_descriptors(const Variant &p_target) const {
	TypedArray<FSMethodDescriptor> result;
	Ref<Script> script = _resolve_script(p_target);
	if (script.is_null()) {
		return result;
	}
	const FoundryScript *foundry_script = Object::cast_to<FoundryScript>(script.ptr());
	if (foundry_script == nullptr) {
		List<MethodInfo> methods;
		script->get_script_method_list(&methods);
		for (const MethodInfo &method : methods) {
			result.push_back(FSMethodDescriptor::create(method, TypedArray<FSAnnotation>(), false));
		}
		return result;
	}
	// Enumerate per script down the base chain (matching get_script_method_list's leaf-first order)
	// so each descriptor carries the annotations of the exact declaration it represents. An override
	// and the base method it shadows are distinct entries, so resolving by name from the leaf would
	// misattribute the override's annotations to the base entry.
	HashSet<const FoundryScript *> visited;
	for (const FoundryScript *current = foundry_script; current != nullptr && !visited.has(current); current = Object::cast_to<FoundryScript>(current->get_base_script().ptr())) {
		visited.insert(current);
		for (const KeyValue<StringName, FSFunction *> &entry : current->get_member_functions()) {
			const Vector<FoundryScript::AnnotationUsage> *usages = current->get_method_annotations().getptr(entry.key);
			TypedArray<FSAnnotation> annotations = usages != nullptr ? usages_to_descriptors(*usages) : TypedArray<FSAnnotation>();
			result.push_back(FSMethodDescriptor::create(entry.value->get_method_info(), annotations));
		}
	}
	return result;
}

Ref<FSMethodDescriptor> FSReflection::get_method_descriptor(const Variant &p_target, const StringName &p_method) const {
	// Walk the script base chain so an inherited method resolves, matching
	// get_method_descriptors() (which uses get_script_method_list, base chain included). The
	// visited set guards against a malformed cyclic base chain.
	Ref<Script> leaf = _resolve_script(p_target);
	const FoundryScript *foundry_script = Object::cast_to<FoundryScript>(leaf.ptr());
	Ref<Script> script = leaf;
	HashSet<const Script *> visited;
	while (script.is_valid() && !visited.has(script.ptr())) {
		visited.insert(script.ptr());
		if (script->has_method(p_method)) {
			TypedArray<FSAnnotation> annotations;
			if (foundry_script != nullptr) {
				// Resolve annotations from the leaf so an override's annotations win, matching get_method_descriptors().
				const Vector<FoundryScript::AnnotationUsage> *usages = find_effective_method_annotations(foundry_script, p_method);
				annotations = usages != nullptr ? usages_to_descriptors(*usages) : TypedArray<FSAnnotation>();
			}
			return FSMethodDescriptor::create(script->get_method_info(p_method), annotations, foundry_script != nullptr);
		}
		script = script->get_base_script();
	}
	return Ref<FSMethodDescriptor>();
}

TypedArray<FSPropertyDescriptor> FSReflection::get_property_descriptors(const Variant &p_target) const {
	TypedArray<FSPropertyDescriptor> result;
	Ref<Script> script = _resolve_script(p_target);
	if (script.is_null()) {
		return result;
	}
	const FoundryScript *foundry_script = Object::cast_to<FoundryScript>(script.ptr());
	List<PropertyInfo> properties;
	script->get_script_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		// Only declared `var`s; category/group headers carry no SCRIPT_VARIABLE.
		if (!(property.usage & PROPERTY_USAGE_SCRIPT_VARIABLE)) {
			continue;
		}
		TypedArray<FSAnnotation> annotations;
		if (foundry_script != nullptr) {
			const Vector<FoundryScript::AnnotationUsage> *usages = find_effective_variable_annotations(foundry_script, property.name);
			annotations = usages != nullptr ? usages_to_descriptors(*usages) : TypedArray<FSAnnotation>();
		}
		result.push_back(FSPropertyDescriptor::create(property, annotations, foundry_script != nullptr));
	}
	return result;
}

TypedArray<Dictionary> FSReflection::get_methods(const Variant &p_target) const {
	// Preserve the loosely-keyed descriptor surface by projecting each structured descriptor down to
	// its Dictionary form, so the two views never diverge. The descriptor's to_dictionary() already
	// omits the "annotations" key for native methods, matching the historical shape.
	TypedArray<FSMethodDescriptor> descriptors = get_method_descriptors(p_target);
	TypedArray<Dictionary> result;
	for (int i = 0; i < descriptors.size(); i++) {
		Ref<FSMethodDescriptor> descriptor = descriptors[i];
		result.push_back(descriptor->to_dictionary());
	}
	return result;
}

Dictionary FSReflection::get_method_info(const Variant &p_target, const StringName &p_method) const {
	Ref<FSMethodDescriptor> descriptor = get_method_descriptor(p_target, p_method);
	return descriptor.is_valid() ? descriptor->to_dictionary() : Dictionary();
}

TypedArray<Dictionary> FSReflection::get_properties(const Variant &p_target) const {
	TypedArray<FSPropertyDescriptor> descriptors = get_property_descriptors(p_target);
	TypedArray<Dictionary> result;
	for (int i = 0; i < descriptors.size(); i++) {
		Ref<FSPropertyDescriptor> descriptor = descriptors[i];
		result.push_back(descriptor->to_dictionary());
	}
	return result;
}

bool FSReflection::implements_trait(const Variant &p_target, const Variant &p_trait) const {
	Ref<Script> script = _resolve_script(p_target);
	if (script.is_null()) {
		return false;
	}
	const StringName trait_name = _resolve_trait_name(p_trait);
	if (trait_name == StringName()) {
		return false;
	}
	return script->has_script_trait(trait_name);
}

TypedArray<Dictionary> FSReflection::get_type_arguments(const Variant &p_target) const {
	TypedArray<Dictionary> result;
	if (p_target.get_type() != Variant::OBJECT) {
		return result;
	}
	Object *object = p_target.get_validated_object();
	if (object == nullptr) {
		return result;
	}
	ScriptInstance *instance = object->get_script_instance();
	// Only a real FSInstance carries reified type arguments. A proxy instance
	// (is_synthetic) and a placeholder instance also report the FoundryScript language but are not
	// FSInstance, so they must be excluded before the cast.
	if (instance == nullptr || instance->get_language() != FSLanguage::get_singleton() ||
			instance->is_synthetic() || instance->is_placeholder()) {
		return result;
	}
	const FSInstance *fs_instance = static_cast<const FSInstance *>(instance);
	for (const ContainerType &type_argument : fs_instance->get_type_arguments()) {
		result.push_back(ContainerTypeDescriptor::to_variant(type_argument));
	}
	return result;
}

Ref<RefCounted> FSReflection::create_proxy_dynamic(const Ref<Script> &p_type, const Callable &p_handler) const {
	String error_message;
	Ref<RefCounted> proxy = FSProxy::create_proxy(p_type, p_handler, error_message);
	if (proxy.is_null()) {
		ERR_PRINT(error_message);
	}
	return proxy;
}

Ref<RefCounted> FSReflection::create_delegating_proxy(const Ref<Script> &p_type, const Variant &p_target, const Dictionary &p_interceptor) const {
	String error_message;
	Ref<RefCounted> proxy = FSProxy::create_delegating_proxy(p_type, p_target, p_interceptor, error_message);
	if (proxy.is_null()) {
		ERR_PRINT(error_message);
	}
	return proxy;
}

TypedArray<FSAnnotation> FSReflection::get_class_annotations(const Variant &p_target) const {
	return usages_to_descriptors(compute_annotations(_resolve_script(p_target), StringName(), SNAME("class"), true));
}

TypedArray<FSAnnotation> FSReflection::get_method_annotations(const Variant &p_target, const StringName &p_method, bool p_effective) const {
	return usages_to_descriptors(compute_annotations(_resolve_script(p_target), p_method, SNAME("method"), p_effective));
}

TypedArray<FSAnnotation> FSReflection::get_variable_annotations(const Variant &p_target, const StringName &p_variable, bool p_effective) const {
	return usages_to_descriptors(compute_annotations(_resolve_script(p_target), p_variable, SNAME("variable"), p_effective));
}

TypedArray<FSAnnotation> FSReflection::get_signal_annotations(const Variant &p_target, const StringName &p_signal, bool p_effective) const {
	return usages_to_descriptors(compute_annotations(_resolve_script(p_target), p_signal, SNAME("signal"), p_effective));
}

TypedArray<FSAnnotation> FSReflection::get_constant_annotations(const Variant &p_target, const StringName &p_constant, bool p_effective) const {
	return usages_to_descriptors(compute_annotations(_resolve_script(p_target), p_constant, SNAME("constant"), p_effective));
}

bool FSReflection::has_annotation(const Variant &p_target, const StringName &p_member, const StringName &p_annotation, const StringName &p_kind, bool p_effective) const {
	const Vector<FoundryScript::AnnotationUsage> usages = compute_annotations(_resolve_script(p_target), p_member, p_kind, p_effective);
	for (const FoundryScript::AnnotationUsage &usage : usages) {
		if (annotation_matches(usage, p_annotation)) {
			return true;
		}
	}
	return false;
}

Ref<FSAnnotation> FSReflection::get_annotation(const Variant &p_target, const StringName &p_member, const StringName &p_annotation, const StringName &p_kind, bool p_effective) const {
	const Vector<FoundryScript::AnnotationUsage> usages = compute_annotations(_resolve_script(p_target), p_member, p_kind, p_effective);
	for (const FoundryScript::AnnotationUsage &usage : usages) {
		// First match in reflected order; callers that care about repeats use the array APIs.
		if (annotation_matches(usage, p_annotation)) {
			return FSAnnotation::from_usage(usage);
		}
	}
	return Ref<FSAnnotation>();
}

TypedArray<FSAnnotation> FSReflection::get_annotations(const Variant &p_target, const StringName &p_member, const StringName &p_kind, bool p_effective) const {
	return usages_to_descriptors(compute_annotations(_resolve_script(p_target), p_member, p_kind, p_effective));
}

void FSReflection::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_methods", "target"), &FSReflection::get_methods);
	ClassDB::bind_method(D_METHOD("get_method_info", "target", "method"), &FSReflection::get_method_info);
	ClassDB::bind_method(D_METHOD("get_properties", "target"), &FSReflection::get_properties);
	ClassDB::bind_method(D_METHOD("get_method_descriptors", "target"), &FSReflection::get_method_descriptors);
	ClassDB::bind_method(D_METHOD("get_method_descriptor", "target", "method"), &FSReflection::get_method_descriptor);
	ClassDB::bind_method(D_METHOD("get_property_descriptors", "target"), &FSReflection::get_property_descriptors);
	ClassDB::bind_method(D_METHOD("implements_trait", "target", "trait"), &FSReflection::implements_trait);
	ClassDB::bind_method(D_METHOD("get_type_arguments", "target"), &FSReflection::get_type_arguments);

	ClassDB::bind_method(D_METHOD("get_class_annotations", "target"), &FSReflection::get_class_annotations);
	ClassDB::bind_method(D_METHOD("get_method_annotations", "target", "method", "effective"), &FSReflection::get_method_annotations, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("get_variable_annotations", "target", "variable", "effective"), &FSReflection::get_variable_annotations, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("get_signal_annotations", "target", "signal", "effective"), &FSReflection::get_signal_annotations, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("get_constant_annotations", "target", "constant", "effective"), &FSReflection::get_constant_annotations, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("has_annotation", "target", "member", "annotation", "kind", "effective"), &FSReflection::has_annotation, DEFVAL(SNAME("method")), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("get_annotation", "target", "member", "annotation", "kind", "effective"), &FSReflection::get_annotation, DEFVAL(SNAME("method")), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("get_annotations", "target", "member", "kind", "effective"), &FSReflection::get_annotations, DEFVAL(StringName()), DEFVAL(SNAME("class")), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("create_proxy_dynamic", "type", "handler"), &FSReflection::create_proxy_dynamic);
	ClassDB::bind_method(D_METHOD("create_delegating_proxy", "type", "target", "interceptor"), &FSReflection::create_delegating_proxy);
}

void FSGodotNamespace::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_reflection"), &FSGodotNamespace::get_reflection);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "reflection", PROPERTY_HINT_RESOURCE_TYPE, "FSReflection", PROPERTY_USAGE_NONE), "", "get_reflection");
}
