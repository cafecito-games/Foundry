/**************************************************************************/
/*  fs_name_mangler_binding_safety.h                                      */
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

#include "fs_name_mangler_analysis.h"

#include "core/io/resource.h"

#ifdef TOOLS_ENABLED

class FSNameManglerBindingSafety {
public:
	enum BindingKind {
		BINDING_CONNECTION_SIGNAL,
		BINDING_CONNECTION_METHOD,
		BINDING_SERIALIZED_PROPERTY,
		BINDING_ANIMATION_PROPERTY,
		BINDING_ANIMATION_METHOD,
		BINDING_RESOURCE_SCRIPT_CLASS,
		BINDING_TYPED_CONTAINER_SCRIPT_CLASS,
	};

	struct ResourceRoot {
		Ref<Resource> resource;
		String source;
	};

	struct Input {
		Vector<ResourceRoot> resources;

		void add_resource(const Ref<Resource> &p_resource, const String &p_source = String());
	};

	struct Evidence {
		StringName name;
		BindingKind kind = BINDING_SERIALIZED_PROPERTY;
		String source;
		String owner;

		String detail() const;
	};

	struct Diagnostic {
		String source;
		String context;
		String message;

		String format() const;
	};

	struct Result {
		Error error = OK;
		bool complete = true;
		Vector<Evidence> evidence;
		Vector<Diagnostic> diagnostics;

		Error apply_to_input(FSNameManglerAnalysis::Input &r_input) const;
	};

	static Result collect(const Input &p_input, const FSNameManglerAnalysis::Input &p_analysis_input);
};

#endif // TOOLS_ENABLED
