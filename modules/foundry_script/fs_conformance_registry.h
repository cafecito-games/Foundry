/**************************************************************************/
/*  fs_conformance_registry.h                                             */
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

#include "fs_parser.h"

#include "core/os/mutex.h"
#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

// Process-global registry of retroactive trait conformances (`extend Target uses Trait: ...`).
//
// A conformance declares, from outside the target's own definition, that a type conforms to a
// trait and supplies the required methods externally as witnesses. The registry answers, by
// stable string identity (target FQCN / global class name) and trait identity name:
//   - does a target conform to a trait?
//   - what are the witness methods for a (target, trait) pair?
//
// Entries are grouped by the source file that declared them so a file can re-register its
// conformances on reload without leaving stale duplicates, mirroring how global classes are
// re-registered. Witnesses are keyed by method name; Phase 3 (compiler/runtime) extends the
// stored witness payload to also hold compiled functions.
class FSConformanceRegistry {
public:
	// Witnesses for a single (target, trait) conformance, keyed by method name. The parser-owned
	// `FunctionNode *` is valid while the declaring file's parse tree is alive; the boolean
	// conformance queries used by the type system never dereference it.
	using WitnessMap = HashMap<StringName, FSParser::FunctionNode *>;

	struct Conformance {
		// Alias keys the target can be looked up by (FQCN, global class name, script path).
		Vector<String> target_keys;
		StringName trait_name;
		String source_file;
		WitnessMap witnesses;
	};

private:
	static FSConformanceRegistry *singleton;

	mutable Mutex mutex;

	// Owning store, grouped by declaring file so a reload can replace a file's entries wholesale.
	HashMap<String, Vector<Conformance>> conformances_by_file;

	// Flattened lookup index: target alias key -> trait identity name -> declaring file.
	// Rebuilt whenever the owning store changes; registrations/clears are infrequent.
	HashMap<String, HashMap<StringName, String>> index;

	void _rebuild_index();

public:
	static FSConformanceRegistry *get_singleton();

	// Replaces every conformance previously registered by `p_source_file` with `p_conformances`.
	void register_file_conformances(const String &p_source_file, const Vector<Conformance> &p_conformances);

	// Drops every conformance previously registered by `p_source_file`.
	void clear_file(const String &p_source_file);

	void clear();

	// True when some target alias `p_target_key` declares an external conformance to `p_trait_name`.
	bool has_conformance(const String &p_target_key, const StringName &p_trait_name) const;

	// The declaring file of the (target, trait) conformance, or an empty string when none exists.
	// Useful for diagnosing cross-file duplicate conformances.
	String get_conformance_source(const String &p_target_key, const StringName &p_trait_name) const;

	// The witnesses for the (target, trait) conformance, or an empty map when none exists.
	WitnessMap get_witnesses(const String &p_target_key, const StringName &p_trait_name) const;

	FSConformanceRegistry();
	~FSConformanceRegistry();
};
