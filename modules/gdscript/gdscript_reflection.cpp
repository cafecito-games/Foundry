/**************************************************************************/
/*  gdscript_reflection.cpp                                               */
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

#include "gdscript_reflection.h"

#include "gdscript.h"
#include "gdscript_proxy.h"

#include "core/variant/container_type_validate.h"

Ref<Script> GDScriptReflection::_resolve_script(const Variant &p_target) {
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
const Vector<GDScript::AnnotationUsage> *find_effective_method_annotations(const GDScript *p_script, const StringName &p_method) {
	HashSet<const GDScript *> visited;
	for (const GDScript *script = p_script; script != nullptr && !visited.has(script); script = Object::cast_to<GDScript>(script->get_base_script().ptr())) {
		visited.insert(script);
		const bool declares = script->get_member_functions().has(p_method) || script->get_method_annotations().has(p_method);
		if (declares) {
			return script->get_method_annotations().getptr(p_method);
		}
	}
	return nullptr;
}

const Vector<GDScript::AnnotationUsage> *find_effective_variable_annotations(const GDScript *p_script, const StringName &p_variable) {
	HashSet<const GDScript *> visited;
	for (const GDScript *script = p_script; script != nullptr && !visited.has(script); script = Object::cast_to<GDScript>(script->get_base_script().ptr())) {
		visited.insert(script);
		const bool declares = script->get_members().has(p_variable) || script->get_variable_annotations().has(p_variable);
		if (declares) {
			return script->get_variable_annotations().getptr(p_variable);
		}
	}
	return nullptr;
}

TypedArray<GDScriptAnnotation> usages_to_descriptors(const Vector<GDScript::AnnotationUsage> &p_usages) {
	TypedArray<GDScriptAnnotation> result;
	for (const GDScript::AnnotationUsage &usage : p_usages) {
		result.push_back(GDScriptAnnotation::from_usage(usage));
	}
	return result;
}

bool annotation_matches(const GDScript::AnnotationUsage &p_usage, const StringName &p_annotation) {
	return p_usage.name == p_annotation || p_usage.qualified_name == p_annotation;
}

// Resolves the passive custom annotation usages effective for `p_member` under `p_kind`: "class"
// (direct-only on the target script), "method", or "variable" (both walk the script base chain so the
// most-derived declaring script wins, matching the effective method/property views). The caller holds
// `p_script` for the whole walk so the base chain it owns stays alive. Returns an empty vector for a
// null, non-GDScript script, or an unknown kind.
Vector<GDScript::AnnotationUsage> compute_effective_annotations(const Ref<Script> &p_script, const StringName &p_member, const StringName &p_kind) {
	const GDScript *script = Object::cast_to<GDScript>(p_script.ptr());
	if (script == nullptr) {
		return Vector<GDScript::AnnotationUsage>();
	}
	if (p_kind == SNAME("class")) {
		// Class annotations are direct-only and never inherited.
		return script->get_class_annotations();
	}
	if (p_kind == SNAME("method")) {
		const Vector<GDScript::AnnotationUsage> *usages = find_effective_method_annotations(script, p_member);
		return usages != nullptr ? *usages : Vector<GDScript::AnnotationUsage>();
	}
	if (p_kind == SNAME("variable")) {
		const Vector<GDScript::AnnotationUsage> *usages = find_effective_variable_annotations(script, p_member);
		return usages != nullptr ? *usages : Vector<GDScript::AnnotationUsage>();
	}
	return Vector<GDScript::AnnotationUsage>();
}
} // namespace

StringName GDScriptReflection::_resolve_trait_name(const Variant &p_trait) {
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

TypedArray<Dictionary> GDScriptReflection::get_methods(const Variant &p_target) const {
	TypedArray<Dictionary> result;
	Ref<Script> script = _resolve_script(p_target);
	if (script.is_null()) {
		return result;
	}
	const GDScript *gdscript = Object::cast_to<GDScript>(script.ptr());
	List<MethodInfo> methods;
	script->get_script_method_list(&methods);
	for (const MethodInfo &method : methods) {
		Dictionary descriptor(method);
		if (gdscript != nullptr) {
			// Embed the effective custom annotations (empty array when none) so every GDScript
			// method descriptor returned here carries the key consistently.
			const Vector<GDScript::AnnotationUsage> *usages = find_effective_method_annotations(gdscript, method.name);
			descriptor["annotations"] = usages != nullptr ? usages_to_descriptors(*usages) : TypedArray<GDScriptAnnotation>();
		}
		result.push_back(descriptor);
	}
	return result;
}

Dictionary GDScriptReflection::get_method_info(const Variant &p_target, const StringName &p_method) const {
	// Walk the script base chain so an inherited method resolves, matching
	// get_methods() (which uses get_script_method_list, base chain included). The
	// visited set guards against a malformed cyclic base chain.
	Ref<Script> leaf = _resolve_script(p_target);
	const GDScript *gdscript = Object::cast_to<GDScript>(leaf.ptr());
	Ref<Script> script = leaf;
	HashSet<const Script *> visited;
	while (script.is_valid() && !visited.has(script.ptr())) {
		visited.insert(script.ptr());
		if (script->has_method(p_method)) {
			Dictionary descriptor(script->get_method_info(p_method));
			if (gdscript != nullptr) {
				// Resolve annotations from the leaf so an override's annotations win, matching get_methods().
				const Vector<GDScript::AnnotationUsage> *usages = find_effective_method_annotations(gdscript, p_method);
				descriptor["annotations"] = usages != nullptr ? usages_to_descriptors(*usages) : TypedArray<GDScriptAnnotation>();
			}
			return descriptor;
		}
		script = script->get_base_script();
	}
	return Dictionary();
}

TypedArray<Dictionary> GDScriptReflection::get_properties(const Variant &p_target) const {
	TypedArray<Dictionary> result;
	Ref<Script> script = _resolve_script(p_target);
	if (script.is_null()) {
		return result;
	}
	const GDScript *gdscript = Object::cast_to<GDScript>(script.ptr());
	List<PropertyInfo> properties;
	script->get_script_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		// Only declared `var`s; category/group headers carry no SCRIPT_VARIABLE.
		if (!(property.usage & PROPERTY_USAGE_SCRIPT_VARIABLE)) {
			continue;
		}
		Dictionary descriptor(property);
		if (gdscript != nullptr) {
			const Vector<GDScript::AnnotationUsage> *usages = find_effective_variable_annotations(gdscript, property.name);
			descriptor["annotations"] = usages != nullptr ? usages_to_descriptors(*usages) : TypedArray<GDScriptAnnotation>();
		}
		result.push_back(descriptor);
	}
	return result;
}

bool GDScriptReflection::implements_trait(const Variant &p_target, const Variant &p_trait) const {
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

TypedArray<Dictionary> GDScriptReflection::get_type_arguments(const Variant &p_target) const {
	TypedArray<Dictionary> result;
	if (p_target.get_type() != Variant::OBJECT) {
		return result;
	}
	Object *object = p_target.get_validated_object();
	if (object == nullptr) {
		return result;
	}
	ScriptInstance *instance = object->get_script_instance();
	// Only a real GDScriptInstance carries reified type arguments. A proxy instance
	// (is_synthetic) and a placeholder instance also report the GDScript language but are not
	// GDScriptInstance, so they must be excluded before the cast.
	if (instance == nullptr || instance->get_language() != GDScriptLanguage::get_singleton() ||
			instance->is_synthetic() || instance->is_placeholder()) {
		return result;
	}
	const GDScriptInstance *gdscript_instance = static_cast<const GDScriptInstance *>(instance);
	for (const ContainerType &type_argument : gdscript_instance->get_type_arguments()) {
		result.push_back(ContainerTypeDescriptor::to_variant(type_argument));
	}
	return result;
}

Ref<RefCounted> GDScriptReflection::create_proxy_dynamic(const Ref<Script> &p_type, const Callable &p_handler) const {
	String error_message;
	Ref<RefCounted> proxy = GDScriptProxy::create_proxy(p_type, p_handler, error_message);
	if (proxy.is_null()) {
		ERR_PRINT(error_message);
	}
	return proxy;
}

Ref<RefCounted> GDScriptReflection::create_delegating_proxy(const Ref<Script> &p_type, const Variant &p_target, const Dictionary &p_interceptor) const {
	String error_message;
	Ref<RefCounted> proxy = GDScriptProxy::create_delegating_proxy(p_type, p_target, p_interceptor, error_message);
	if (proxy.is_null()) {
		ERR_PRINT(error_message);
	}
	return proxy;
}

TypedArray<GDScriptAnnotation> GDScriptReflection::get_class_annotations(const Variant &p_target) const {
	return usages_to_descriptors(compute_effective_annotations(_resolve_script(p_target), StringName(), SNAME("class")));
}

TypedArray<GDScriptAnnotation> GDScriptReflection::get_method_annotations(const Variant &p_target, const StringName &p_method) const {
	return usages_to_descriptors(compute_effective_annotations(_resolve_script(p_target), p_method, SNAME("method")));
}

TypedArray<GDScriptAnnotation> GDScriptReflection::get_variable_annotations(const Variant &p_target, const StringName &p_variable) const {
	return usages_to_descriptors(compute_effective_annotations(_resolve_script(p_target), p_variable, SNAME("variable")));
}

bool GDScriptReflection::has_annotation(const Variant &p_target, const StringName &p_member, const StringName &p_annotation, const StringName &p_kind) const {
	const Vector<GDScript::AnnotationUsage> usages = compute_effective_annotations(_resolve_script(p_target), p_member, p_kind);
	for (const GDScript::AnnotationUsage &usage : usages) {
		if (annotation_matches(usage, p_annotation)) {
			return true;
		}
	}
	return false;
}

Ref<GDScriptAnnotation> GDScriptReflection::get_annotation(const Variant &p_target, const StringName &p_member, const StringName &p_annotation, const StringName &p_kind) const {
	const Vector<GDScript::AnnotationUsage> usages = compute_effective_annotations(_resolve_script(p_target), p_member, p_kind);
	for (const GDScript::AnnotationUsage &usage : usages) {
		// First match in reflected order; callers that care about repeats use the array APIs.
		if (annotation_matches(usage, p_annotation)) {
			return GDScriptAnnotation::from_usage(usage);
		}
	}
	return Ref<GDScriptAnnotation>();
}

TypedArray<GDScriptAnnotation> GDScriptReflection::get_annotations(const Variant &p_target, const StringName &p_member, const StringName &p_kind) const {
	return usages_to_descriptors(compute_effective_annotations(_resolve_script(p_target), p_member, p_kind));
}

void GDScriptReflection::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_methods", "target"), &GDScriptReflection::get_methods);
	ClassDB::bind_method(D_METHOD("get_method_info", "target", "method"), &GDScriptReflection::get_method_info);
	ClassDB::bind_method(D_METHOD("get_properties", "target"), &GDScriptReflection::get_properties);
	ClassDB::bind_method(D_METHOD("implements_trait", "target", "trait"), &GDScriptReflection::implements_trait);
	ClassDB::bind_method(D_METHOD("get_type_arguments", "target"), &GDScriptReflection::get_type_arguments);

	ClassDB::bind_method(D_METHOD("get_class_annotations", "target"), &GDScriptReflection::get_class_annotations);
	ClassDB::bind_method(D_METHOD("get_method_annotations", "target", "method"), &GDScriptReflection::get_method_annotations);
	ClassDB::bind_method(D_METHOD("get_variable_annotations", "target", "variable"), &GDScriptReflection::get_variable_annotations);
	ClassDB::bind_method(D_METHOD("has_annotation", "target", "member", "annotation", "kind"), &GDScriptReflection::has_annotation, DEFVAL(SNAME("method")));
	ClassDB::bind_method(D_METHOD("get_annotation", "target", "member", "annotation", "kind"), &GDScriptReflection::get_annotation, DEFVAL(SNAME("method")));
	ClassDB::bind_method(D_METHOD("get_annotations", "target", "member", "kind"), &GDScriptReflection::get_annotations, DEFVAL(StringName()), DEFVAL(SNAME("class")));
	ClassDB::bind_method(D_METHOD("create_proxy_dynamic", "type", "handler"), &GDScriptReflection::create_proxy_dynamic);
	ClassDB::bind_method(D_METHOD("create_delegating_proxy", "type", "target", "interceptor"), &GDScriptReflection::create_delegating_proxy);
}

void GDScriptGodotNamespace::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_reflection"), &GDScriptGodotNamespace::get_reflection);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "reflection", PROPERTY_HINT_RESOURCE_TYPE, "GDScriptReflection", PROPERTY_USAGE_NONE), "", "get_reflection");
}
