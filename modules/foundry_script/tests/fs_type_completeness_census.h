/**************************************************************************/
/*  fs_type_completeness_census.h                                         */
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

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

namespace FSTests {

// Reading a census document is one operation with one set of refusals - unreadable file, malformed
// JSON, duplicate member, non-object root - so the validator and the report producer share it rather
// than each deciding what a bad document means. Appends one message per refusal and returns false.
bool read_completeness_json(const String &p_path, Variant &r_data, Vector<String> &r_errors);

// Accessors over a parsed census document. Every one is total: a member of the wrong type reads as
// absent, so a caller validates what it needs instead of crashing on malformed input.
Vector<Dictionary> census_dictionary_array(const Dictionary &p_document, const String &p_key);
Vector<String> census_string_array(const Dictionary &p_dictionary, const String &p_key);
String census_string(const Dictionary &p_dictionary, const String &p_key);
HashSet<String> census_string_set(const Vector<String> &p_values);

// True when p_reference is the "<suite> <name>" identity of a registered doctest case. Binding a
// witness to the registry rather than to a spelling is what keeps a renamed case from silently
// unwitnessing whatever named it. r_detail explains a refusal.
bool resolve_doctest_case_reference(const String &p_reference, String &r_detail);

// True when p_reference is a repository-relative path, below the repository p_root belongs to, that
// exists and that git reports as tracked. r_detail explains a refusal.
bool resolve_tracked_fixture_reference(const String &p_root, const String &p_reference, String &r_detail);

// What observes one census cell. A witness is data rather than a literal case identity: a doctest is
// named by its registered name, a fixture by its tracked path, and a family case by the coordinates
// the rule matrix resolves to exactly one cell.
struct FSCompletenessCoverageWitness {
	// One of the schema's witness kinds plus "family_case", or empty when the entry declares none.
	String kind;
	// Doctest case name or repository-relative fixture path. Empty for a family_case witness.
	String reference;
	// family_case only: the rules stem and the full concrete coordinates of the observing cell.
	String family;
	Dictionary coordinates;

	bool is_declared() const { return !kind.is_empty(); }
};

// One coverage declaration: what a (representation, child slot, surface) cell's handling policy is
// backed by, and who owns closing it when nothing backs it yet.
struct FSCompletenessCoverageEntry {
	String representation;
	String child_slot;
	String surface;
	String status;
	FSCompletenessCoverageWitness witness;
	String issue_url;

	// Identity of the policy pair this entry covers. Two entries with the same key are a duplicate
	// declaration, whatever else they carry.
	String key() const;
};

struct FSCompletenessCensusSummary {
	// Ascending by key, so a report assembled from a summary is byte-identical between runs.
	Vector<FSCompletenessCoverageEntry> entries;
	int covered = 0;
	int uncovered = 0;
	int unsupported = 0;
	int quality_deferred = 0;

	// covered + uncovered + unsupported + quality_deferred, which always equals entries.size().
	int total() const;
};

// Loading, summarizing, and witness binding for the representation census. Consumers never read the
// census documents themselves: a report producer that parsed them again could disagree with the
// validator about what the census says while both looked correct in isolation.
class FSCompletenessCensus {
public:
	// The census documents of the catalog rooted at p_root.
	static String census_directory(const String &p_root);

	// Reads and validates coverage.json against schema.json, policies.json, and unsupported.json.
	// Every refusal appends one message and no entry is published, so a malformed census can never be
	// mistaken for an absent one. Returns ERR_INVALID_DATA when any message was appended.
	static Error load(const String &p_root, FSCompletenessCensusSummary &r_summary, Vector<String> &r_errors);

	// The report document of a summary: the four status counts as report numbers and one record per
	// entry, in summary order.
	static Dictionary summary_report(const FSCompletenessCensusSummary &p_summary);

	// True when p_entry's witness binds to something that observes it: a registered doctest case, a
	// tracked fixture, or exactly one cell of a registered rule family. r_detail explains a refusal.
	static bool resolve_witness(const String &p_root, const FSCompletenessCoverageEntry &p_entry,
			String &r_detail);

	// One message per entry that declares itself covered and whose witness does not resolve, in
	// summary order. An empty result is what lets a run publish the census as evidence.
	static Vector<String> unresolved_covered_witnesses(
			const String &p_root, const FSCompletenessCensusSummary &p_summary);
};

} // namespace FSTests
