/**************************************************************************/
/*  gdscript_proxy.cpp                                                    */
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

#include "gdscript_proxy.h"

#include "core/object/ref_counted.h"

GDScriptProxyInstance::GDScriptProxyInstance(Object *p_owner, const Ref<GDScript> &p_script, const Callable &p_handler) :
		owner(p_owner), proxy_script(p_script), handler(p_handler) {
}

GDScriptProxyInstance::~GDScriptProxyInstance() {
}

bool GDScriptProxyInstance::_is_contract_method(const StringName &p_method) const {
	const GDScript *script = proxy_script.ptr();
	while (script) {
		if (script->get_member_functions().has(p_method)) {
			return true;
		}
		script = script->get_base().ptr();
	}
	return false;
}

Variant GDScriptProxyInstance::callp(const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	if (!_is_contract_method(p_method)) {
		// Not part of `T`'s contract: defer to native `Object`/`RefCounted`
		// built-ins so `get_instance_id`, `connect`, refcounting, etc. work.
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
		return Variant();
	}

	Array call_args;
	call_args.resize(p_argcount);
	for (int i = 0; i < p_argcount; i++) {
		call_args[i] = *p_args[i];
	}

	const Variant method_name_arg = p_method;
	const Variant args_arg = call_args;
	const Variant *handler_args[2] = { &method_name_arg, &args_arg };

	Variant ret;
	Callable::CallError handler_error;
	handler.callp(handler_args, 2, ret, handler_error);

	// The call reached the handler, so it is never an "invalid method"; reporting
	// it as such would wrongly fall through to a native built-in. Surface the
	// handler's own call error verbatim. Return-value coercion and the richer
	// error contract are layered on in #187.
	r_error = handler_error;
	return ret;
}

bool GDScriptProxyInstance::has_method(const StringName &p_method) const {
	return _is_contract_method(p_method);
}

void GDScriptProxyInstance::get_method_list(List<MethodInfo> *p_list) const {
	if (proxy_script.is_valid()) {
		proxy_script->get_script_method_list(p_list);
	}
}

// Property handling is intentionally inert here; the auto-backing store that
// synthesizes slots from `T`'s declared `var`s is added in #185.
bool GDScriptProxyInstance::set(const StringName &p_name, const Variant &p_value) {
	return false;
}

bool GDScriptProxyInstance::get(const StringName &p_name, Variant &r_ret) const {
	return false;
}

void GDScriptProxyInstance::get_property_list(List<PropertyInfo> *p_properties) const {
}

Variant::Type GDScriptProxyInstance::get_property_type(const StringName &p_name, bool *r_is_valid) const {
	if (r_is_valid) {
		*r_is_valid = false;
	}
	return Variant::NIL;
}

void GDScriptProxyInstance::validate_property(PropertyInfo &p_property) const {
}

bool GDScriptProxyInstance::property_can_revert(const StringName &p_name) const {
	return false;
}

bool GDScriptProxyInstance::property_get_revert(const StringName &p_name, Variant &r_ret) const {
	return false;
}

void GDScriptProxyInstance::notification(int p_notification, bool p_reversed) {
}

ScriptLanguage *GDScriptProxyInstance::get_language() {
	return GDScriptLanguage::get_singleton();
}

Object *GDScriptProxy::create_proxy(const Ref<Script> &p_type, const Callable &p_handler, String &r_error_message) {
	Ref<GDScript> gdscript = p_type;
	if (gdscript.is_null()) {
		r_error_message = RTR("Proxy target must be a GDScript trait or abstract type.");
		return nullptr;
	}
	if (!gdscript->is_valid()) {
		r_error_message = RTR("Proxy target script is not compiled/valid.");
		return nullptr;
	}
	if (!gdscript->is_trait_type() && !gdscript->is_abstract()) {
		r_error_message = RTR("Proxy target must be a trait or an abstract type.");
		return nullptr;
	}
	if (!p_handler.is_valid()) {
		r_error_message = RTR("Proxy handler must be a valid Callable.");
		return nullptr;
	}

	// Dedicated construction path: hosts the synthetic instance on a fresh
	// `RefCounted` and bypasses the abstract/trait instantiation guard that
	// `GDScript::_new`/`instance_create` enforce. The Object owns and frees the
	// `ScriptInstance`.
	RefCounted *proxy_owner = memnew(RefCounted);
	GDScriptProxyInstance *instance = memnew(GDScriptProxyInstance(proxy_owner, gdscript, p_handler));
	proxy_owner->set_script_instance(instance);
	return proxy_owner;
}
