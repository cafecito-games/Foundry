/**************************************************************************/
/*  test_doc_data.h                                                       */
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

#include "core/core_constants.h"
#include "core/doc_data.h"
#include "core/object/object.h"

#include "tests/test_macros.h"

namespace TestDocData {

TEST_CASE("[DocData] method qualifiers include async method flag") {
	MethodInfo method_info("async_method");
	method_info.flags = METHOD_FLAG_VIRTUAL | METHOD_FLAG_VIRTUAL_REQUIRED | METHOD_FLAG_ASYNC | METHOD_FLAG_CONST |
			METHOD_FLAG_VARARG | METHOD_FLAG_STATIC;

	DocData::MethodDoc method_doc;
	DocData::method_doc_from_methodinfo(method_doc, method_info, String());

	CHECK(DocData::get_method_qualifiers_from_methodinfo(method_info) == "virtual required vararg const static async");
	CHECK(method_doc.qualifiers == "virtual required vararg const static async");
}

TEST_CASE("[DocData] method qualifiers preserve vararg before const order") {
	MethodInfo method_info("vararg_const_method");
	method_info.flags = METHOD_FLAG_VARARG | METHOD_FLAG_CONST;

	CHECK(DocData::get_method_qualifiers_from_methodinfo(method_info) == "vararg const");
}

TEST_CASE("[Object] async method flag uses next non-colliding bit") {
	const uint32_t existing_method_flags = METHOD_FLAG_NORMAL | METHOD_FLAG_EDITOR | METHOD_FLAG_CONST |
			METHOD_FLAG_VIRTUAL | METHOD_FLAG_VARARG | METHOD_FLAG_STATIC | METHOD_FLAG_OBJECT_CORE |
			METHOD_FLAG_VIRTUAL_REQUIRED;

	CHECK(uint32_t(METHOD_FLAG_ASYNC) == 256);
	CHECK((METHOD_FLAG_ASYNC & existing_method_flags) == 0);
}

TEST_CASE("[Object] async method flag is exposed as a global bitfield constant") {
	const int constant_index = CoreConstants::get_global_constant_index("METHOD_FLAG_ASYNC");

	REQUIRE(constant_index >= 0);
	CHECK(CoreConstants::get_global_constant_value(constant_index) == METHOD_FLAG_ASYNC);
	CHECK(CoreConstants::get_global_constant_enum(constant_index) == StringName("MethodFlags"));
	CHECK(CoreConstants::is_global_constant_bitfield(constant_index));
}

TEST_CASE("[DocData] method qualifiers omit async method flag") {
	MethodInfo method_info("regular_method");
	method_info.flags = METHOD_FLAG_VIRTUAL | METHOD_FLAG_VIRTUAL_REQUIRED | METHOD_FLAG_CONST | METHOD_FLAG_VARARG |
			METHOD_FLAG_STATIC;

	DocData::MethodDoc method_doc;
	DocData::method_doc_from_methodinfo(method_doc, method_info, String());

	CHECK(method_doc.qualifiers == "virtual required vararg const static");
}

TEST_CASE("[DocData] synthetic AsyncCallable type links to the Callable class page") {
	// `AsyncCallable` is a FoundryScript-only spelling of `Callable` with no dedicated
	// class help page. Its doc-type string must still render as written, but the
	// hyperlink has to resolve to the real `Callable` page instead of a dead link.
	CHECK(DocData::get_type_link_target("AsyncCallable") == "Callable");
}

TEST_CASE("[DocData] non-synthetic types keep their own link target") {
	CHECK(DocData::get_type_link_target("Callable") == "Callable");
	CHECK(DocData::get_type_link_target("Node") == "Node");
	CHECK(DocData::get_type_link_target("int") == "int");
	CHECK(DocData::get_type_link_target("") == "");
}

TEST_CASE("[DocData] non-enum integer default value is left as a number") {
	CHECK(DocData::get_default_value_string(Variant(42), PropertyInfo()) == "42");
}

TEST_CASE("[DocData] enum default value resolves to its constant name") {
	PropertyInfo info(Variant::INT, "hint", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_CLASS_IS_ENUM, "PropertyHint");
	CHECK(DocData::get_default_value_string(Variant(int(PROPERTY_HINT_RANGE)), info) == "PROPERTY_HINT_RANGE");
}

TEST_CASE("[DocData] enum default value falls back to a number when it matches no constant") {
	PropertyInfo info(Variant::INT, "hint", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_CLASS_IS_ENUM, "PropertyHint");
	CHECK(DocData::get_default_value_string(Variant(999999), info) == "999999");
}

TEST_CASE("[DocData] bitfield default value resolves to the combined constant name") {
	// Regression: the constant sort must run after the global-enum map is populated,
	// so greedy decomposition matches PROPERTY_USAGE_DEFAULT before its sub-bits
	// (PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR) instead of emitting them.
	PropertyInfo info(Variant::INT, "usage", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_CLASS_IS_BITFIELD, "PropertyUsageFlags");
	CHECK(DocData::get_default_value_string(Variant(int(PROPERTY_USAGE_DEFAULT)), info) == "PROPERTY_USAGE_DEFAULT");
}

TEST_CASE("[DocData] bitfield default value decomposes into an OR of constants") {
	PropertyInfo info(Variant::INT, "usage", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_CLASS_IS_BITFIELD, "PropertyUsageFlags");
	const int combination = PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_INTERNAL;
	const String result = DocData::get_default_value_string(Variant(combination), info);
	CHECK(result.contains("PROPERTY_USAGE_DEFAULT"));
	CHECK(result.contains("PROPERTY_USAGE_INTERNAL"));
	CHECK(result.contains(" | "));
}

} // namespace TestDocData
