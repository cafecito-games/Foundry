/**************************************************************************/
/*  fs_json_marshal.h                                                     */
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

#include "core/io/json.h"
#include "core/variant/variant.h"

class Object;

// The `to_json()` hook of the builtin `JsonSerializable` trait is script-dispatched: it is
// implemented in Foundry Script, so a native caller holding a C++ pointer cannot reach it
// through virtual dispatch and must go through `Object::callp()`. This class is that single
// native call site; do not call `to_json` by name anywhere else.
class FSJsonMarshal {
public:
	// Name of the script-dispatched hook. Exposed so callers can probe for it without
	// re-spelling the literal.
	static StringName to_json_method_name();

	// True when `p_object` exposes the hook at all, whether from a script implementation or
	// from a native class that binds the same name.
	static bool has_to_json(Object *p_object);

	// Invokes the hook and stores its return value in `r_node`. Returns false, leaving
	// `r_node` untouched, when the object is null or the call fails.
	static bool call_to_json(Object *p_object, Variant &r_node);
};

// Teaches `JSON.stringify()` how to encode a Foundry Script object that conforms to the builtin
// `JsonSerializable` trait: the object's `to_json()` is called through `FSJsonMarshal`, and the
// `JsonNode` it returns is lowered to a plain Variant tree that core can serialize. Objects that
// do not conform are declined, so they keep the pre-existing quoted `to_string` representation.
class FSJsonObjectMarshaller : public JSONObjectMarshaller {
public:
	// Case ordinals of the builtin `JsonNode` tagged union. This is a wire contract shared with
	// `modules/foundry_script/builtin/json_node.fs`; the two must stay in sync and must not be
	// reordered.
	enum Tag {
		TAG_NULL = 0,
		TAG_BOOL = 1,
		TAG_INT = 2,
		TAG_FLOAT = 3,
		TAG_STR = 4,
		TAG_ARRAY = 5,
		TAG_OBJECT = 6,
	};

	// Name of the builtin trait an object must conform to for its `to_json()` to be honored.
	static StringName serializable_trait_name();

	// Lowers a `JsonNode` value (a `[tag, payload...]` read-only Array) to a plain Variant tree.
	// Reports an error and returns false for anything that is not a well-formed node, including a
	// tree deeper than `Variant::MAX_RECURSION_DEPTH`. Exposed for tests.
	static bool lower_node(const Variant &p_node, Variant &r_result, int p_depth, const String &p_source_name);

	virtual bool marshal_object(Object *p_object, Variant &r_result) override;
};
