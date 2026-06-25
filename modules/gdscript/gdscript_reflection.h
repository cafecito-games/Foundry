/**************************************************************************/
/*  gdscript_reflection.h                                                 */
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

#include "core/object/ref_counted.h"
#include "core/object/script_language.h"
#include "core/variant/typed_array.h"

// Read-only introspection surface for GDScript, exposed as `godot.reflection`.
// The target of each call is a script type (a `Script`) or an instance whose
// script is used. Method/property descriptors are returned as the same
// Dictionaries `Object.get_method_list()` / `get_property_list()` produce.
//
// RefCounted so that, registered as a named global, it is released when the
// language's globals are torn down (mirroring the native-class globals) rather
// than relying on an explicit free.
class GDScriptReflection : public RefCounted {
	GDCLASS(GDScriptReflection, RefCounted);

	static Ref<Script> _resolve_script(const Variant &p_target);
	static StringName _resolve_trait_name(const Variant &p_trait);

protected:
	static void _bind_methods();

public:
	TypedArray<Dictionary> get_methods(const Variant &p_target) const;
	Dictionary get_method_info(const Variant &p_target, const StringName &p_method) const;
	TypedArray<Dictionary> get_properties(const Variant &p_target) const;
	bool implements_trait(const Variant &p_target, const Variant &p_trait) const;

	// "Intercept some, delegate the rest": contract methods named in `p_interceptor`
	// route to that advice; all other methods and property access forward to
	// `p_target`. Returns a null Ref (with an error printed) on invalid input.
	Ref<RefCounted> create_delegating_proxy(const Ref<Script> &p_type, const Variant &p_target, const Dictionary &p_interceptor) const;
};

// The `godot` global namespace object. Currently it only exposes the read-only
// `reflection` member; this is the nested-singleton binding for the
// `godot.reflection.*` surface (a true language namespace is not available).
class GDScriptGodotNamespace : public RefCounted {
	GDCLASS(GDScriptGodotNamespace, RefCounted);

	Ref<GDScriptReflection> reflection;

protected:
	static void _bind_methods();

public:
	void set_reflection(const Ref<GDScriptReflection> &p_reflection) { reflection = p_reflection; }
	// Returns a Ref (not a raw pointer) so the binding carries
	// PROPERTY_HINT_RESOURCE_TYPE for the reference return, as ClassDB expects.
	Ref<GDScriptReflection> get_reflection() const { return reflection; }
};
