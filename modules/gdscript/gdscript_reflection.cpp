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

#include "gdscript_proxy.h"

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
	List<MethodInfo> methods;
	script->get_script_method_list(&methods);
	for (const MethodInfo &method : methods) {
		result.push_back(Dictionary(method));
	}
	return result;
}

Dictionary GDScriptReflection::get_method_info(const Variant &p_target, const StringName &p_method) const {
	// Walk the script base chain so an inherited method resolves, matching
	// get_methods() (which uses get_script_method_list, base chain included). The
	// visited set guards against a malformed cyclic base chain.
	Ref<Script> script = _resolve_script(p_target);
	HashSet<const Script *> visited;
	while (script.is_valid() && !visited.has(script.ptr())) {
		visited.insert(script.ptr());
		if (script->has_method(p_method)) {
			return Dictionary(script->get_method_info(p_method));
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
	List<PropertyInfo> properties;
	script->get_script_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		// Only declared `var`s; category/group headers carry no SCRIPT_VARIABLE.
		if (!(property.usage & PROPERTY_USAGE_SCRIPT_VARIABLE)) {
			continue;
		}
		result.push_back(Dictionary(property));
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

void GDScriptReflection::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_methods", "target"), &GDScriptReflection::get_methods);
	ClassDB::bind_method(D_METHOD("get_method_info", "target", "method"), &GDScriptReflection::get_method_info);
	ClassDB::bind_method(D_METHOD("get_properties", "target"), &GDScriptReflection::get_properties);
	ClassDB::bind_method(D_METHOD("implements_trait", "target", "trait"), &GDScriptReflection::implements_trait);
	ClassDB::bind_method(D_METHOD("create_proxy_dynamic", "type", "handler"), &GDScriptReflection::create_proxy_dynamic);
	ClassDB::bind_method(D_METHOD("create_delegating_proxy", "type", "target", "interceptor"), &GDScriptReflection::create_delegating_proxy);
}

void GDScriptGodotNamespace::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_reflection"), &GDScriptGodotNamespace::get_reflection);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "reflection", PROPERTY_HINT_RESOURCE_TYPE, "GDScriptReflection", PROPERTY_USAGE_NONE), "", "get_reflection");
}
