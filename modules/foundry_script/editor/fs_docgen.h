/**************************************************************************/
/*  fs_docgen.h                                                           */
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

#include "../fs_parser.h"

#include "core/doc_data.h"

class FSDocGen {
	using GDP = FSParser;
	using GDType = GDP::DataType;

	static HashMap<String, String> singletons; // Script path to singleton name.

	static String _get_script_name(const String &p_path);
	static String _get_class_name(const GDP::ClassNode &p_class);
	static void _populate_singletons_from_autoload_index();
	static String _qualified_declared_type_name(const StringName &p_native_type);
	static void _doctype_from_gdtype(const GDType &p_gdtype, String &r_type, String &r_enum, String &r_tuple, bool p_is_return = false);
	// Spells a type that appears inside another type's spelling, where no separate metadata channel
	// can travel with it.
	static void _doctype_from_gdtype_nested(const GDType &p_gdtype, String &r_type, String &r_enum);
	static String _structural_tuple_spelling(const GDType &p_gdtype);
	static String _docvalue_from_variant(const Variant &p_variant, int p_recursion_level = 1);
	static void _generate_docs(FoundryScript *p_script, const GDP::ClassNode *p_class);

public:
	static void generate_docs(FoundryScript *p_script, const GDP::ClassNode *p_class);
	static void doctype_from_gdtype(const GDType &p_gdtype, String &r_type, String &r_enum, String &r_tuple, bool p_is_return = false);
	// Resolves an integer value to its enum constant name when the declared type is an enum.
	// Returns the bare constant name on an exact match, or the integer as a string otherwise.
	static String docvalue_from_enum_value(int64_t p_value, const HashMap<StringName, int64_t> &p_enum_values);
	static String docvalue_from_expression(const GDP::ExpressionNode *p_expression, const GDType &p_type = GDType());
};
