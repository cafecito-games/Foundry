/**************************************************************************/
/*  class_handle.h                                                        */
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
#include "core/templates/vector.h"

struct ContainerType;
class Script;

// A value that denotes a class rather than an instance of one. Scripting languages that expose class
// handles as first-class values derive from this so the engine can validate a container slot declared
// to hold handles without knowing which language produced the value.
//
// A bare `Script` is not a `ClassHandle`: the engine already recognizes a script resource as denoting
// the class it defines, so it is resolved directly wherever a handle is expected.
class ClassHandle : public RefCounted {
	FOUNDRY_CLASS(ClassHandle, RefCounted);

public:
	// The engine class this handle denotes, or an empty name when the handle denotes a scripted class.
	virtual StringName get_represented_native_class() const;
	// The script this handle denotes, or null when the handle denotes a native engine class.
	virtual Ref<Script> get_represented_script() const;
	// The reified generic arguments the represented class was specialized with, e.g. the `int` in
	// `Box[int]`. Left empty for an unspecialized handle.
	virtual void get_represented_type_arguments(Vector<ContainerType> &r_arguments) const;
};
