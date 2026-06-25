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

// Pins a strong reference to every script type reachable in `p_type` (including
// nested container element types). The compiler leaves `script_type_ref` null
// for local subclasses to avoid reference cycles, so a by-value snapshot would
// otherwise dangle if the handler reloads the proxied script mid-call. Pinning
// is scoped to the snapshot's lifetime, so it introduces no persistent cycle.
static void _pin_script_type_refs(GDScriptDataType &p_type) {
	if (p_type.script_type != nullptr && p_type.script_type_ref.is_null()) {
		p_type.script_type_ref = Ref<Script>(p_type.script_type);
	}
	for (int i = 0; i < p_type.container_element_types.size(); i++) {
		_pin_script_type_refs(p_type.container_element_types.write[i]);
	}
}

// The zero value of a declared GDScript type: typed containers get a correctly
// typed empty value, builtins their constructed default, and everything else
// (objects, scripts) `null`. Mirrors GDScript::_static_default_init and the VM's
// default-on-invalid-return behavior.
static Variant _default_for_data_type(const GDScriptDataType &p_type) {
	if (p_type.kind != GDScriptDataType::BUILTIN || p_type.builtin_type == Variant::NIL) {
		return Variant();
	}
	if (p_type.builtin_type == Variant::ARRAY && p_type.has_container_element_type(0)) {
		Array typed_array;
		typed_array.set_typed(p_type.get_container_element_type(0).to_container_type());
		return typed_array;
	}
	if (p_type.builtin_type == Variant::DICTIONARY && p_type.has_container_element_types()) {
		Dictionary typed_dictionary;
		typed_dictionary.set_typed(
				p_type.get_container_element_type_or_variant(0).to_container_type(),
				p_type.get_container_element_type_or_variant(1).to_container_type());
		return typed_dictionary;
	}
	Variant default_value;
	Callable::CallError construct_error;
	Variant::construct(p_type.builtin_type, default_value, nullptr, 0, construct_error);
	return default_value;
}

GDScriptProxyInstance::GDScriptProxyInstance(Object *p_owner, const Ref<GDScript> &p_script, const Callable &p_handler) :
		owner(p_owner), proxy_script(p_script), handler(p_handler) {
	_init_property_store();
}

void GDScriptProxyInstance::_init_property_store() {
	if (proxy_script.is_null()) {
		return;
	}

	// `T`'s declared `var`s carry PROPERTY_USAGE_SCRIPT_VARIABLE; category/group
	// headers and other entries do not, so they are skipped. Each slot defaults to
	// the zero value of its declared type, matching how GDScript initializes
	// members (typed containers get a correctly-typed empty value).
	List<PropertyInfo> properties;
	proxy_script->get_script_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		if (!(property.usage & PROPERTY_USAGE_SCRIPT_VARIABLE)) {
			continue;
		}
		if (property_store.has(property.name)) {
			continue;
		}

		const GDScriptDataType &data_type = proxy_script->get_member_type(property.name);
		property_store.insert(property.name, _default_for_data_type(data_type));
		property_types.insert(property.name, property.type);
	}
}

GDScriptProxyInstance::~GDScriptProxyInstance() {
}

GDScriptFunction *GDScriptProxyInstance::_find_contract_function(const StringName &p_method) const {
	const GDScript *script = proxy_script.ptr();
	while (script) {
		HashMap<StringName, GDScriptFunction *>::ConstIterator element = script->get_member_functions().find(p_method);
		if (element) {
			return element->value;
		}
		script = script->get_base().ptr();
	}
	return nullptr;
}

Variant GDScriptProxyInstance::_coerce_handler_return(const GDScriptDataType &p_return_type, const StringName &p_method_name, const Variant &p_value) const {
	// `void`: the declared return is `null`, so the handler's value is ignored.
	if (p_return_type.kind == GDScriptDataType::BUILTIN && p_return_type.builtin_type == Variant::NIL) {
		return Variant();
	}

	// Untyped / `Variant` return: pass the handler's value through unchanged.
	if (!p_return_type.has_type()) {
		return p_value;
	}

	// Already an acceptable value for the declared type (covers exact builtins,
	// typed containers, and `null`/instances for native/script types).
	if (p_return_type.is_type(p_value)) {
		return p_value;
	}

	// Implicit builtin conversion (e.g. int -> float), mirroring the VM's
	// OPCODE_RETURN_TYPED_BUILTIN coercion. Typed containers are excluded: the VM
	// requires an exactly-typed Array/Dictionary on return, and converting would
	// silently produce an untyped/mistyped container, so those route to the
	// mismatch path below.
	const bool is_typed_container =
			(p_return_type.builtin_type == Variant::ARRAY && p_return_type.has_container_element_type(0)) ||
			(p_return_type.builtin_type == Variant::DICTIONARY && p_return_type.has_container_element_types());
	if (p_return_type.kind == GDScriptDataType::BUILTIN && !is_typed_container && p_value.get_type() != Variant::NIL &&
			Variant::can_convert_strict(p_value.get_type(), p_return_type.builtin_type)) {
		Variant coerced;
		Callable::CallError convert_error;
		const Variant *convert_args[1] = { &p_value };
		Variant::construct(p_return_type.builtin_type, coerced, convert_args, 1, convert_error);
		if (convert_error.error == Callable::CallError::CALL_OK) {
			return coerced;
		}
	}

	// Hard mismatch: report in debug builds and fall back to the type's default,
	// matching how the VM substitutes a default on an invalid typed return.
	ERR_PRINT(vformat(R"(Dynamic proxy handler for "%s" returned a value of type "%s" that is not compatible with the method's declared return type.)",
			String(p_method_name), Variant::get_type_name(p_value.get_type())));
	return _default_for_data_type(p_return_type);
}

Variant GDScriptProxyInstance::callp(const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	GDScriptFunction *contract_function = _find_contract_function(p_method);
	if (contract_function == nullptr) {
		// Not part of `T`'s contract: defer to native `Object`/`RefCounted`
		// built-ins so `get_instance_id`, `connect`, refcounting, etc. work.
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
		return Variant();
	}

	// Snapshot the declared return type before running the handler: handler code
	// is arbitrary and could reload `T`, freeing the live GDScriptFunction. Pin
	// strong references to any script types in the copy so it stays self-contained
	// even if the originating script is reloaded mid-call.
	GDScriptDataType return_type = contract_function->get_return_type();
	_pin_script_type_refs(return_type);

	if (_is_delegating()) {
		return _delegate_call(return_type, p_method, p_args, p_argcount, r_error);
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

	// `p_method` is part of `T`'s contract, so the call must never fall through to
	// native dispatch. If the handler itself could not be invoked, report it and
	// return the declared type's default with CALL_OK — mirroring how the VM
	// handles a runtime error inside a function body (the error is surfaced out of
	// band, a default is substituted, and the call still reports CALL_OK).
	// Forwarding the handler's CallError would be wrong here: its argument indices
	// describe the handler's own `(StringName, Array)` signature, not `p_method`,
	// and the CALL_ERROR_INVALID_METHOD / CALL_ERROR_INSTANCE_IS_NULL codes are
	// the exact signals `Object::callp` uses to try native dispatch instead.
	if (handler_error.error != Callable::CallError::CALL_OK) {
		ERR_PRINT(vformat(R"(Dynamic proxy handler for "%s" could not be invoked (call error %d); returning the default for its declared return type.)",
				String(p_method), int(handler_error.error)));
		r_error.error = Callable::CallError::CALL_OK;
		return _default_for_data_type(return_type);
	}

	r_error.error = Callable::CallError::CALL_OK;
	return _coerce_handler_return(return_type, p_method, ret);
}

Variant GDScriptProxyInstance::_delegate_call(const GDScriptDataType &p_return_type, const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) const {
	r_error.error = Callable::CallError::CALL_OK;

	Object *target = delegate_target.get_validated_object();
	if (target == nullptr) {
		ERR_PRINT(vformat(R"(Delegating proxy for "%s" has no live target; returning the default for its declared return type.)", String(p_method)));
		return _default_for_data_type(p_return_type);
	}

	Variant ret;
	Callable::CallError call_error;
	if (delegate_interceptor.has(p_method)) {
		// An advised method: invoke the advice as `advice(method_name, args, target)`.
		// The advice may "proceed" by calling `target.callv(method_name, args)`.
		Array call_args;
		call_args.resize(p_argcount);
		for (int i = 0; i < p_argcount; i++) {
			call_args[i] = *p_args[i];
		}
		const Variant method_name_arg = p_method;
		const Variant args_arg = call_args;
		const Variant target_arg = delegate_target;
		const Variant *advice_args[3] = { &method_name_arg, &args_arg, &target_arg };
		const Callable advice = delegate_interceptor[p_method];
		advice.callp(advice_args, 3, ret, call_error);
	} else {
		// Not advised: forward straight to the target.
		ret = target->callp(p_method, p_args, p_argcount, call_error);
	}

	if (call_error.error != Callable::CallError::CALL_OK) {
		ERR_PRINT(vformat(R"(Delegating proxy for "%s" could not complete the call (call error %d); returning the default for its declared return type.)",
				String(p_method), int(call_error.error)));
		return _default_for_data_type(p_return_type);
	}
	return _coerce_handler_return(p_return_type, p_method, ret);
}

void GDScriptProxyInstance::_configure_delegation(const Variant &p_target, const Dictionary &p_interceptor) {
	delegate_target = p_target;
	delegate_interceptor = p_interceptor;
}

bool GDScriptProxyInstance::has_method(const StringName &p_method) const {
	return _find_contract_function(p_method) != nullptr;
}

void GDScriptProxyInstance::get_method_list(List<MethodInfo> *p_list) const {
	if (proxy_script.is_valid()) {
		proxy_script->get_script_method_list(p_list);
	}
}

// Property access reads and writes the auto-backing store directly; it is never
// routed through the handler. In delegation mode, contract properties forward to
// the target instead. Names outside `T`'s declared vars are not handled here so
// native/`Object` property paths still apply.
bool GDScriptProxyInstance::set(const StringName &p_name, const Variant &p_value) {
	if (_is_delegating()) {
		if (!property_types.has(p_name)) {
			return false;
		}
		Object *target = delegate_target.get_validated_object();
		if (target == nullptr) {
			return false;
		}
		bool valid = false;
		target->set(p_name, p_value, &valid);
		return valid;
	}

	HashMap<StringName, Variant>::Iterator element = property_store.find(p_name);
	if (!element) {
		return false;
	}
	element->value = p_value;
	return true;
}

bool GDScriptProxyInstance::get(const StringName &p_name, Variant &r_ret) const {
	if (_is_delegating()) {
		if (!property_types.has(p_name)) {
			return false;
		}
		Object *target = delegate_target.get_validated_object();
		if (target == nullptr) {
			return false;
		}
		bool valid = false;
		r_ret = target->get(p_name, &valid);
		return valid;
	}

	HashMap<StringName, Variant>::ConstIterator element = property_store.find(p_name);
	if (!element) {
		return false;
	}
	r_ret = element->value;
	return true;
}

void GDScriptProxyInstance::get_property_list(List<PropertyInfo> *p_properties) const {
	if (proxy_script.is_valid()) {
		proxy_script->get_script_property_list(p_properties);
	}
}

Variant::Type GDScriptProxyInstance::get_property_type(const StringName &p_name, bool *r_is_valid) const {
	HashMap<StringName, Variant::Type>::ConstIterator element = property_types.find(p_name);
	if (element) {
		if (r_is_valid) {
			*r_is_valid = true;
		}
		return element->value;
	}
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

// Validates that `p_gdscript` is a proxyable trait/abstract type rooted on
// RefCounted (shared by both construction paths). Returns false with a filled
// error message otherwise.
static bool _validate_proxy_target(const Ref<GDScript> &p_gdscript, String &r_error_message) {
	if (p_gdscript.is_null()) {
		r_error_message = RTR("Proxy target must be a GDScript trait or abstract type.");
		return false;
	}
	if (!p_gdscript->is_valid()) {
		r_error_message = RTR("Proxy target script is not compiled/valid.");
		return false;
	}
	if (!p_gdscript->is_trait_type() && !p_gdscript->is_abstract()) {
		r_error_message = RTR("Proxy target must be a trait or an abstract type.");
		return false;
	}
	// The host is a `RefCounted`, so a target rooted on any other native base
	// (Node, Resource, Object, ...) cannot be soundly represented yet: its native
	// methods and `is`-checks against the native base would not hold. Proxying
	// those bases is tracked as a follow-up.
	if (p_gdscript->get_instance_base_type() != SNAME("RefCounted")) {
		r_error_message = vformat(RTR("Proxy target must extend RefCounted; native base \"%s\" is not supported yet."), String(p_gdscript->get_instance_base_type()));
		return false;
	}
	return true;
}

Ref<RefCounted> GDScriptProxy::create_proxy(const Ref<Script> &p_type, const Callable &p_handler, String &r_error_message) {
	Ref<GDScript> gdscript = p_type;
	if (!_validate_proxy_target(gdscript, r_error_message)) {
		return Ref<RefCounted>();
	}
	if (!p_handler.is_valid()) {
		r_error_message = RTR("Proxy handler must be a valid Callable.");
		return Ref<RefCounted>();
	}

	// Dedicated construction path: hosts the synthetic instance on a fresh
	// `RefCounted` and bypasses the abstract/trait instantiation guard that
	// `GDScript::_new`/`instance_create` enforce. The Object owns and frees the
	// `ScriptInstance`; the returned `Ref` owns the host.
	RefCounted *proxy_owner = memnew(RefCounted);
	GDScriptProxyInstance *instance = memnew(GDScriptProxyInstance(proxy_owner, gdscript, p_handler));
	proxy_owner->set_script_instance(instance);
	return Ref<RefCounted>(proxy_owner);
}

Ref<RefCounted> GDScriptProxy::create_delegating_proxy(const Ref<Script> &p_type, const Variant &p_target, const Dictionary &p_interceptor, String &r_error_message) {
	Ref<GDScript> gdscript = p_type;
	if (!_validate_proxy_target(gdscript, r_error_message)) {
		return Ref<RefCounted>();
	}
	if (p_target.get_type() != Variant::OBJECT || p_target.get_validated_object() == nullptr) {
		r_error_message = RTR("Delegating proxy target must be a valid object.");
		return Ref<RefCounted>();
	}

	RefCounted *proxy_owner = memnew(RefCounted);
	// Delegation mode does not use a handler; methods/properties route to the
	// target (and advice) via `_configure_delegation`.
	GDScriptProxyInstance *instance = memnew(GDScriptProxyInstance(proxy_owner, gdscript, Callable()));
	instance->_configure_delegation(p_target, p_interceptor);
	proxy_owner->set_script_instance(instance);
	return Ref<RefCounted>(proxy_owner);
}
