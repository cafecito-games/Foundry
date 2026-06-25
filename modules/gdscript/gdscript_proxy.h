/**************************************************************************/
/*  gdscript_proxy.h                                                      */
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

#pragma once

#include "gdscript.h"

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
class GDScriptProxyInstance : public ScriptInstance {
	Object *owner = nullptr;
	Ref<GDScript> proxy_script;
	Callable handler;

	// Auto-backing property store: every `var` declared in `T`'s contract becomes a
	// plain data slot, initialized to the zero value of its declared type. Property
	// access is not routed through the handler (delegation is the helper's job).
	HashMap<StringName, Variant> property_store;
	HashMap<StringName, Variant::Type> property_types;

	// Populates `property_store`/`property_types` from `T`'s declared member vars.
	void _init_property_store();

	// Returns the GDScriptFunction declared for `p_method` somewhere in
	// `proxy_script`'s class/trait/abstract chain, or nullptr if `p_method` is not
	// part of `T`'s contract. Mirrors how `GDScriptInstance::callp` walks the base
	// chain, but the function is used only for its declared signature (return type)
	// — its body is never executed.
	//
	// Covers the target's own declared methods (abstract + concrete), methods
	// inherited through the GDScript base chain, and concrete methods flattened in
	// from applied traits. Abstract requirements contributed transitively by a
	// trait the target itself `uses` are not flattened into `member_functions` and
	// are therefore not yet part of the scanned contract; resolving the full trait
	// requirement set is tracked alongside trait conformance (#186).
	GDScriptFunction *_find_contract_function(const StringName &p_method) const;

	// Coerces the handler's return value to `p_function`'s declared return type:
	// ignored for `void`, passed through for untyped/`Variant`, and otherwise
	// coerced (with implicit builtin conversions) or — on a mismatch — reported in
	// debug builds and replaced with the type's default.
	Variant _coerce_handler_return(GDScriptFunction *p_function, const Variant &p_value) const;

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

	virtual Variant callp(const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) override;

	virtual void notification(int p_notification, bool p_reversed = false) override;

	virtual Ref<Script> get_script() const override { return proxy_script; }
	virtual ScriptLanguage *get_language() override;

	// The proxy reports the GDScript language but is not a GDScriptInstance, so
	// language-keyed casts to GDScriptInstance must skip it (see is_synthetic).
	virtual bool is_synthetic() const override { return true; }

	GDScriptProxyInstance(Object *p_owner, const Ref<GDScript> &p_script, const Callable &p_handler);
	virtual ~GDScriptProxyInstance() override;
};

class GDScriptProxy {
public:
	// Builds a proxy that satisfies trait/abstract type `p_type` and routes its
	// contract through `p_handler`. Returns a `Ref` owning a freshly created
	// `RefCounted` host, or a null `Ref` (with `r_error_message` filled) on invalid
	// input. The target must extend `RefCounted`, since the host is a `RefCounted`.
	static Ref<RefCounted> create_proxy(const Ref<Script> &p_type, const Callable &p_handler, String &r_error_message);
};
