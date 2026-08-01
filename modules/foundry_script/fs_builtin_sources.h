/**************************************************************************/
/*  fs_builtin_sources.h                                                  */
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

#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/list.h"

// Source text for Foundry Script types that ship inside the binary rather than as project
// files. A global type is resolved by path (see `ScriptServer::get_global_class_path`), so a
// built-in type still needs parseable source behind a path; these paths use a reserved scheme
// that no project can write to or shadow.
class FSBuiltinSources {
	static HashMap<String, String> sources;

public:
	static const char *PATH_PREFIX;
	// Reserved project directory holding the private compiled-bytecode companions emitted for
	// builtins during a compiled-bytecode export. Nothing writes source files here.
	static const char *EXPORTED_BYTECODE_PREFIX;

	static bool is_builtin_path(const String &p_path);
	// Canonical virtual-source to private-bytecode mapping. Preserves the builtin's relative
	// subdirectories and replaces only the final `.fs` extension; returns an empty string for any
	// path that is not a registrable `foundry://builtin/<relative>.fs` identity. Callers must never
	// build this path themselves: the exporter and the stripped runtime have to agree exactly.
	static String get_exported_bytecode_path(const String &p_path);
	static void register_source(const String &p_path, const String &p_source);
	static void unregister_source(const String &p_path);
	static bool get_source(const String &p_path, String &r_source);
	// Sorted so every consumer (notably the exporter) enumerates builtins deterministically.
	static void get_registered_paths(List<String> *r_paths);
	static void clear();
};
