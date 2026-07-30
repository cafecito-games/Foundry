/**************************************************************************/
/*  fs_project_scripts.h                                                  */
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

#include "core/object/ref_counted.h"
#include "core/variant/typed_array.h"

class FSAnnotation;
class FSMethodDescriptor;
class FSScriptDescriptor;
class Script;

class FSProjectScripts : public RefCounted {
	FOUNDRY_CLASS(FSProjectScripts, RefCounted);

protected:
	static void _bind_methods();

public:
	TypedArray<FSScriptDescriptor> list_scripts_under(const String &p_root_path, bool p_recursive = true) const;
	Ref<FSScriptDescriptor> get_script_descriptor(const String &p_path) const;
};

class FSScriptDescriptor : public RefCounted {
	FOUNDRY_CLASS(FSScriptDescriptor, RefCounted);

	String path;
	StringName global_class_name;
	StringName fully_qualified_name;
	StringName base_type;
	bool is_trait = false;
	bool is_abstract = false;
	bool indexed_ok = false;
	TypedArray<Dictionary> index_diagnostics;

protected:
	static void _bind_methods();

public:
	String get_path() const { return path; }
	StringName get_global_class_name() const { return global_class_name; }
	StringName get_fully_qualified_name() const { return fully_qualified_name; }
	StringName get_base_type() const { return base_type; }
	bool get_is_trait() const { return is_trait; }
	bool get_is_abstract() const { return is_abstract; }
	bool get_indexed_ok() const { return indexed_ok; }

	TypedArray<Dictionary> get_index_diagnostics() const { return index_diagnostics.duplicate(); }
	TypedArray<FSAnnotation> get_class_annotations() const;
	TypedArray<FSMethodDescriptor> get_methods() const;
	bool implements_trait(const StringName &p_trait_name) const;
	Ref<Script> load_script() const;

	static Ref<FSScriptDescriptor> build(const String &p_path);
};
