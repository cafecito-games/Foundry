/**************************************************************************/
/*  fs_type_completeness_cache.h                                          */
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

#include "fs_type_completeness_graph.h"
#include "fs_type_completeness_runner.h"

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"

namespace FSTests {

// Repository-relative root of the tracked type-completeness catalog.
String tracked_catalog_root();

// Everything one family's catalog inputs resolve to. Immutable once loaded: the catalog, its rule
// manifest, the resolved matrix, and the case-ID migrations are pure functions of files on disk.
struct FSCompletenessCatalogRecord {
	FSCompletenessCatalog catalog;
	FSCompletenessManifest manifest;
	FSCompletenessResolution resolution;
	FSCompletenessMigrations migrations;
	Error error = OK;
	// How far the load got before `error`: OK once the manifest loaded, validated, and its derivation
	// graph resolved, whatever the migration index then did. A consumer that only needs the matrix -
	// a census witness asking whether a family could run at all - reads this, because migrations are
	// keyed to one family's case IDs and a migration file written for another family says nothing
	// about whether this one resolves.
	Error resolution_error = OK;
	Vector<String> errors;
};

// Process-wide store of resolved catalogs, keyed by canonical catalog root and family.
//
// Loading and resolving a family is the dominant fixed cost of a run, and the whole matrix repeats it
// once per test. The store is deliberately never invalidated: catalog inputs are immutable within a
// run, and keying on the canonical root keeps a staged, mutated copy of the catalog from ever aliasing
// the tracked one. A test that mutates catalog files between loads must therefore stage a fresh root.
class FSCompletenessCatalogCache {
public:
	// The shared record for (p_canonical_catalog_root, p_family), loading it on first request. The
	// pointer is stable for the life of the process and identical for every caller of the same key.
	// A failed load is stored too, so a second request cannot see a different outcome than the first.
	static Error get(const String &p_canonical_catalog_root, const String &p_family,
			const FSCompletenessCatalogRecord *&r_record, Vector<String> &r_errors);

	// How many keys have been loaded from disk. The single-load property is otherwise unobservable.
	static int load_count();
};

// One full run of a family per process against the tracked catalog, for the many tests that only
// assert on a clean baseline report. Tests that install a mutator or a write hook change what the run
// observes and must keep their own scratch root and their own run.
class FSCompletenessBaseline {
public:
	// The shared baseline result for p_family. The first call runs it; every later call returns the
	// same result, so a test can never observe a baseline contaminated by another test's mutation.
	static const FSCompletenessRunResult &shared(const String &p_family);

	// The shared baseline, or nullptr after printing why it is unavailable. Assertion-only tests use
	// this so an environment that cannot produce a baseline is visible instead of silently passing.
	static const FSCompletenessRunResult *shared_or_skip(const String &p_family);

	// Absolute path of p_family's baseline scratch root, or an error explaining why it cannot be
	// used. Exposed so the refusal to reuse an existing root is provable without a second process.
	static Error resolve_scratch_root(const String &p_family, String &r_root);

	// How many baseline runs this process has executed.
	static int run_count();
};

} // namespace FSTests
