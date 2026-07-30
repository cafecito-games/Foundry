/**************************************************************************/
/*  fs_proxy.h                                                            */
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

#pragma once

#include "foundry_script.h"

#include "core/object/script_instance.h"
#include "core/variant/callable.h"

// Synthetic instance that statically satisfies a trait/abstract type `T` and
// routes every call in `T`'s script-level contract through a handler `Callable`.
//
// Dispatch rule: a method declared anywhere in `T`'s class/trait/abstract chain
// (abstract or concrete) is intercepted and forwarded to the handler; everything
// else returns `CALL_ERROR_INVALID_METHOD`, so native `RefCounted`/`Object`
// built-ins fall through and are never intercepted. The proxy never executes
// `T`'s real method bodies.
//
// This is the foundation primitive (issue #183). The auto-backing property model
// (#185), trait-conformance for `is` (#186), and the full handler return/error
// contract (#187) build on top of it.
class FSProxyInstance : public ScriptInstance {
	Object *owner = nullptr;
	Ref<FoundryScript> proxy_script;
	Callable handler;

	// Delegation mode (the `create_delegating_proxy` helper): when `delegate_target`
	// holds an object, contract methods route to an advice in `delegate_interceptor`
	// (keyed by method name) if present, otherwise to `target.callv(...)`, and
	// property access forwards to the target. In this mode `handler` is unused.
	Variant delegate_target;
	Dictionary delegate_interceptor;
	bool _is_delegating() const { return delegate_target.get_type() == Variant::OBJECT; }
	Variant _delegate_call(const FSDataType &p_return_type, const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) const;

	// Auto-backing property store: every `var` declared in `T`'s contract becomes a
	// plain data slot, initialized to the zero value of its declared type. Property
	// access is not routed through the handler (delegation is the helper's job).
	HashMap<StringName, Variant> property_store;
	// The reflected `Variant::Type` reported by `get_property_type`, taken from the
	// declared property's `PropertyInfo` so it preserves export-inferred types for
	// otherwise-untyped members (e.g. `@export var n = 1` reports `INT`).
	HashMap<StringName, Variant::Type> property_types;
	// The full declared `FSDataType` per property, used to validate/coerce writes
	// exactly as `FSInstance::set` does — including typed `Array[T]` /
	// `Dictionary[K, V]` element types, which a coarse `Variant::Type` cannot express.
	HashMap<StringName, FSDataType> property_data_types;

	// Populates the property maps from `T`'s declared member vars.
	void _init_property_store();

	// Returns the FSFunction declared for `p_method` somewhere in
	// `proxy_script`'s class/trait/abstract chain, or nullptr if no compiled function
	// provides it. Mirrors how `FSInstance::callp` walks the base chain, but the
	// function is used only for its declared signature (return type) — its body is
	// never executed.
	//
	// Covers the target's own declared methods (abstract + concrete), methods
	// inherited through the FoundryScript base chain, and concrete methods flattened in
	// from applied traits. Abstract requirements contributed transitively by `uses`-ed
	// traits are not flattened into `member_functions`; those are resolved separately
	// via `_resolve_contract_return_type` reading `abstract_trait_requirements`.
	FSFunction *_find_contract_function(const StringName &p_method) const;

	// Resolves whether `p_method` is part of `T`'s contract and, if so, its declared
	// return type. A compiled member function (own/inherited/flattened-in concrete or
	// own abstract) wins; otherwise the transitive abstract trait requirements are
	// consulted. Returns false for names outside the contract, which then fall through
	// to native `RefCounted`/`Object` dispatch.
	bool _resolve_contract_return_type(const StringName &p_method, FSDataType &r_return_type) const;

	// Coerces the handler's return value to the intercepted method's declared
	// return type: ignored for `void`, passed through for untyped/`Variant`, and
	// otherwise coerced (with implicit builtin conversions) or — on a mismatch —
	// reported in debug builds and replaced with the type's default. The return
	// type is passed by value because it is snapshotted before the handler runs
	// (handler code could reload `T` and free the live FSFunction).
	Variant _coerce_handler_return(const FSDataType &p_return_type, const StringName &p_method_name, const Variant &p_value) const;

public:
	// ScriptInstance interface.
	virtual bool set(const StringName &p_name, const Variant &p_value) override;
	virtual bool get(const StringName &p_name, Variant &r_ret) const override;
	virtual void get_property_list(List<PropertyInfo> *p_properties) const override;
	virtual Variant::Type get_property_type(const StringName &p_name, bool *r_is_valid = nullptr) const override;
	virtual void validate_property(PropertyInfo &p_property) const override;

	virtual bool property_can_revert(const StringName &p_name) const override;
	virtual bool property_get_revert(const StringName &p_name, Variant &r_ret) const override;

	virtual Object *get_owner() override { return owner; }

	virtual void get_method_list(List<MethodInfo> *p_list) const override;
	virtual bool has_method(const StringName &p_method) const override;
	virtual int get_method_argument_count(const StringName &p_method, bool *r_is_valid = nullptr) const override;

	virtual Variant callp(const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) override;

	virtual void notification(int p_notification, bool p_reversed = false) override;

	// `Object::to_string()` (and implicit Variant stringification) dispatches through
	// this virtual rather than `callp`, so route it through the handler when `T`'s
	// contract declares `_to_string`. Mirrors `FSInstance::to_string`.
	virtual String to_string(bool *r_valid) override;

	virtual Ref<Script> get_script() const override { return proxy_script; }
	virtual ScriptLanguage *get_language() override;

	// The proxy reports the FoundryScript language but is not a FSInstance, so
	// language-keyed casts to FSInstance must skip it (see is_synthetic).
	virtual bool is_synthetic() const override { return true; }

	// Switches this instance to delegation mode (see the fields above). Called only
	// during construction by `FSProxy::create_delegating_proxy`.
	void _configure_delegation(const Variant &p_target, const Dictionary &p_interceptor);

	FSProxyInstance(Object *p_owner, const Ref<FoundryScript> &p_script, const Callable &p_handler);
	virtual ~FSProxyInstance() override;
};

class FSProxy {
public:
	// Builds a proxy that satisfies trait/abstract type `p_type` and routes its
	// contract through `p_handler`. Returns a `Ref` owning a freshly created
	// `RefCounted` host, or a null `Ref` (with `r_error_message` filled) on invalid
	// input. The target must extend `RefCounted`, since the host is a `RefCounted`.
	static Ref<RefCounted> create_proxy(const Ref<Script> &p_type, const Callable &p_handler, String &r_error_message);

	// Builds a delegating proxy for `p_type`: contract methods whose name is a key
	// in `p_interceptor` route to that advice `Callable` (invoked as
	// `advice.call(method_name, args, target)`); all other contract methods and all
	// property access forward to `p_target`. `p_target` must be a valid object.
	static Ref<RefCounted> create_delegating_proxy(const Ref<Script> &p_type, const Variant &p_target, const Dictionary &p_interceptor, String &r_error_message);
};
