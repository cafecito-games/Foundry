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

	// Builds a `JsonNode` for a plain Variant tree produced by the JSON parser, by calling the
	// builtin `JsonNode.of()` so the tagged union's representation stays defined in one place, in
	// script. Returns false when the tree contains a value JSON has no node for, or is nested
	// deeper than `Variant::MAX_RECURSION_DEPTH`.
	static bool make_json_node(const Variant &p_value, Variant &r_node);

	// Builds a successful `JsonResult` through the builtin `JsonResult.ok()`.
	static bool make_result_ok(const Variant &p_value, Variant &r_result);

	// Builds a failed `JsonResult` through the builtin `JsonResult.fail()`.
	static bool make_result_failure(const String &p_message, const String &p_path, Variant &r_result);
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

	// Rewrites a plain Variant tree produced by the JSON parser into the form `JsonNode.of()`
	// expects, and reports whether it can be represented at all. Returns false when a value has no
	// `JsonNode` case, when an object key is not a String, or when the tree is nested deeper than
	// `Variant::MAX_RECURSION_DEPTH`.
	//
	// JSON has a single number type, so the parser reports every number as a float. A number with
	// no fractional part becomes an `Int` node here, otherwise the `Int` case would exist but never
	// occur in a parsed document and an `int` field could not be decoded by matching it. A decoder
	// for a `float` field therefore accepts both `Int` and `Float`, as JSON decoders generally do.
	//
	// The node carries exactly what the parser produced and never invents precision: a literal too
	// large for a double to hold exactly has already been rounded before this runs, so it becomes
	// the `Int` nearest to the rounded double rather than the digits in the text. A number no
	// `int64_t` can hold stays a `Float`. Exposed for tests.
	static bool prepare_parsed_value(const Variant &p_value, int p_depth, Variant &r_prepared);

	virtual bool marshal_object(Object *p_object, Variant &r_result) override;
	virtual bool lift_variant(const Variant &p_parsed, Variant &r_node) override;
	virtual bool make_parse_failure(const String &p_message, int p_line, Variant &r_result) override;
	virtual bool make_parse_success(const Variant &p_node, Variant &r_result) override;
};
