/**************************************************************************/
/*  fs_builtin_test_utils.h                                               */
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

#include "modules/foundry_script/fs_builtin_sources.h"
#include "modules/foundry_script/fs_builtin_types.h"
#include "modules/foundry_script/fs_cache.h"

#include "core/object/script_language.h"

namespace FSTests {

// Registers an extra builtin source for one case and takes it back out again, including the cache
// entries loading it leaves behind, so the registered builtin set other cases see is unchanged.
struct ScopedBuiltinSource {
	String path;

	ScopedBuiltinSource(const String &p_path, const String &p_source) :
			path(p_path) {
		FSBuiltinSources::register_source(path, p_source);
	}

	~ScopedBuiltinSource() {
		FSBuiltinSources::unregister_source(path);
		FSCache::remove_script(path);
	}
};

// Registers a throwaway builtin global class so a scoped builtin source can be referenced by name
// from another script, then restores the shipped set. `ScriptServer` has no single-class removal,
// so the restore clears the builtin globals and re-registers the ones the module owns.
struct ScopedBuiltinGlobalClass {
	ScopedBuiltinGlobalClass(const StringName &p_class, const StringName &p_base, const String &p_path) {
		ScriptServer::add_builtin_global_class(
				p_class, p_base, SNAME("FoundryScript"), p_path, false, false, false, false);
	}

	~ScopedBuiltinGlobalClass() {
		ScriptServer::clear_builtin_global_classes();
		FSBuiltinTypes::register_types();
	}
};

// Makes the named builtin identities load from their private compiled-bytecode companions for the
// duration of the scope, the way a stripped (`foundry_script_frontend=no`) runtime always does.
// Cache entries for those paths are dropped on both entry and exit so neither the surrounding suite
// nor the scope itself observes a script built under the other regime.
struct ScopedForcedBuiltinBytecodeDispatch {
	HashSet<String> paths;

	explicit ScopedForcedBuiltinBytecodeDispatch(const Vector<String> &p_paths) {
		for (const String &path : p_paths) {
			paths.insert(path);
		}
		evict();
		FSCache::set_forced_builtin_bytecode_paths(paths);
	}

	explicit ScopedForcedBuiltinBytecodeDispatch(const String &p_path) :
			ScopedForcedBuiltinBytecodeDispatch(Vector<String>({ p_path })) {}

	~ScopedForcedBuiltinBytecodeDispatch() {
		FSCache::set_forced_builtin_bytecode_paths(HashSet<String>());
		evict();
	}

private:
	void evict() const {
		for (const String &path : paths) {
			FSCache::remove_script(path);
		}
	}
};

} // namespace FSTests
