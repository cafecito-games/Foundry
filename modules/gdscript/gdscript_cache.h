/**************************************************************************/
/*  gdscript_cache.h                                                      */
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

#include "gdscript.h"

#include "core/object/ref_counted.h"
#include "core/os/safe_binary_mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"

class GDScriptAnalyzer;
class GDScriptParser;

class GDScriptParserRef : public RefCounted {
	GDSOFTCLASS(GDScriptParserRef, RefCounted);

public:
	enum Status {
		EMPTY,
		PARSED,
		INHERITANCE_SOLVED,
		INTERFACE_SOLVED,
		FULLY_SOLVED,
	};

private:
	GDScriptParser *parser = nullptr;
	GDScriptAnalyzer *analyzer = nullptr;
	Status status = EMPTY;
	Error result = OK;
	String path;
	uint32_t source_hash = 0;
	bool clearing = false;
	bool abandoned = false;

	friend class GDScriptCache;
	friend class GDScript;

public:
	Status get_status() const;
	String get_path() const;
	uint32_t get_source_hash() const;
	GDScriptParser *get_parser();
	GDScriptAnalyzer *get_analyzer();
	Error raise_status(Status p_new_status);
	void clear();

	GDScriptParserRef() {}
	~GDScriptParserRef();
};

#ifdef TESTS_ENABLED
namespace GDScriptTests {
class TestGDScriptCacheAccessor;
}
#endif // TESTS_ENABLED

class GDScriptCache {
	// String key is full path.
	HashMap<String, GDScriptParserRef *> parser_map;
	HashMap<String, Vector<ObjectID>> abandoned_parser_map;
	HashMap<String, Ref<GDScript>> shallow_gdscript_cache;
	HashMap<String, Ref<GDScript>> full_gdscript_cache;
	HashMap<String, Ref<GDScript>> static_gdscript_cache;
	HashMap<String, HashSet<String>> dependencies;
	HashMap<String, HashSet<String>> parser_inverse_dependencies;

	// In-memory source overrides keyed by path. When present, get_source_code() (and the
	// script loaders) return the overridden source instead of reading disk, so the analyzer
	// resolves cross-file references against edited-but-unsaved buffers with no disk writes.
	HashMap<String, String> source_overrides;

	friend class GDScript;
	friend class GDScriptParserRef;
	friend class GDScriptInstance;
#ifdef TESTS_ENABLED
	friend class GDScriptTests::TestGDScriptCacheAccessor;
#endif // TESTS_ENABLED

	static GDScriptCache *singleton;

	bool cleared = false;

public:
	static const int BINARY_MUTEX_TAG = 2;

private:
	static SafeBinaryMutex<BINARY_MUTEX_TAG> mutex;
	friend SafeBinaryMutex<BINARY_MUTEX_TAG> &_get_gdscript_cache_mutex();

public:
	static void move_script(const String &p_from, const String &p_to);
	static void remove_script(const String &p_path);
	static Ref<GDScriptParserRef> get_parser(const String &p_path, GDScriptParserRef::Status status, Error &r_error, const String &p_owner = String());
	static bool has_parser(const String &p_path);
	static void remove_parser(const String &p_path);
	static String get_source_code(const String &p_path);

	// In-memory source-override map. While an override is set for a path, get_source_code()
	// and the shallow/full script loaders return the overridden source instead of reading
	// disk, so cross-file resolution sees edited-but-unsaved buffers. Setting or clearing an
	// override does not by itself invalidate parsers or scripts already built from the old
	// source; callers must invalidate the affected paths (e.g. remove_parser/remove_script)
	// around a change for it to take effect. Thread-safe (guarded by the cache mutex).
	static void set_source_override(const String &p_path, const String &p_source);
	static bool has_source_override(const String &p_path);
	static void clear_source_override(const String &p_path);
	static void clear_source_overrides();
	// Returns a snapshot of the set of files that directly depend on p_path (its
	// inverse dependencies), as recorded during compilation. Empty if none are known.
	// Snapshot-by-value so callers are safe against concurrent cache mutation.
	static HashSet<String> get_inverse_dependencies(const String &p_path);
	static Vector<uint8_t> get_binary_tokens(const String &p_path);
	static Ref<GDScript> get_shallow_script(const String &p_path, Error &r_error, const String &p_owner = String());
	/**
	 * Returns a fully loaded GDScript using an already cached script if one exists.
	 *
	 * The returned instance is present in GDScriptCache and ResourceCache.
	 */
	static Ref<GDScript> get_full_script(const String &p_path, Error &r_error, const String &p_owner = String(), bool p_update_from_disk = false);
	static Ref<GDScript> get_cached_script(const String &p_path);
	static Error finish_compiling(const String &p_owner);
	static void add_static_script(Ref<GDScript> p_script);
	static void remove_static_script(const String &p_fqcn);

	static void clear();

	// Drops every cached parser and analyzed-script entry so the next access re-parses and
	// re-analyzes against the current project settings. Used when an analysis-affecting setting
	// (e.g. the strict-mode flags read by GDScriptAnalyzer) changes mid-session, so a live editor
	// session re-reports already-cached scripts under the new flags instead of the stale ones the
	// entries were built with. Source overrides are preserved; only built artifacts are dropped.
	static void invalidate_analysis();

	GDScriptCache();
	~GDScriptCache();
};

// RAII guard that installs a set of source overrides on construction and clears exactly
// those paths on destruction, restoring the cache's override state. It does NOT invalidate
// parsers/scripts; the caller is responsible for invalidating affected paths after
// construction (so the overrides are seen) and again after destruction (so disk content is
// seen again). Overrides for paths it did not install are left untouched.
class GDScriptCacheSourceOverrideGuard {
	Vector<String> paths;

public:
	explicit GDScriptCacheSourceOverrideGuard(const HashMap<String, String> &p_overrides) {
		for (const KeyValue<String, String> &entry : p_overrides) {
			GDScriptCache::set_source_override(entry.key, entry.value);
			paths.push_back(entry.key);
		}
	}

	~GDScriptCacheSourceOverrideGuard() {
		for (const String &path : paths) {
			GDScriptCache::clear_source_override(path);
		}
	}

	GDScriptCacheSourceOverrideGuard(const GDScriptCacheSourceOverrideGuard &) = delete;
	GDScriptCacheSourceOverrideGuard &operator=(const GDScriptCacheSourceOverrideGuard &) = delete;
};
