/**************************************************************************/
/*  fs_builtin_sources.cpp                                                */
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

#include "fs_builtin_sources.h"

#include "core/error/error_macros.h"
#include "core/variant/variant.h"

HashMap<String, String> FSBuiltinSources::sources;

const char *FSBuiltinSources::PATH_PREFIX = "foundry://builtin/";

bool FSBuiltinSources::is_builtin_path(const String &p_path) {
	return p_path.begins_with(PATH_PREFIX);
}

void FSBuiltinSources::register_source(const String &p_path, const String &p_source) {
	ERR_FAIL_COND_MSG(!is_builtin_path(p_path),
			vformat("Builtin source path must start with \"%s\", got \"%s\".", PATH_PREFIX, p_path));
	sources[p_path] = p_source;
}

void FSBuiltinSources::unregister_source(const String &p_path) {
	sources.erase(p_path);
}

bool FSBuiltinSources::get_source(const String &p_path, String &r_source) {
	const String *found = sources.getptr(p_path);
	if (found == nullptr) {
		r_source = String();
		return false;
	}
	r_source = *found;
	return true;
}

void FSBuiltinSources::get_registered_paths(List<String> *r_paths) {
	ERR_FAIL_NULL(r_paths);
	for (const KeyValue<String, String> &entry : sources) {
		r_paths->push_back(entry.key);
	}
}

void FSBuiltinSources::clear() {
	sources.clear();
}
