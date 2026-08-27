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
	// The build configuration this witness can be confirmed in, or empty when every configuration
	// compiles it. A witness that only exists in an editor build cannot be bound by a template build,
	// so declaring that here keeps an absent case attributable to the configuration instead of being
	// read as a coverage claim nothing backs.
	String build_configuration;

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

// A declared negative witness of an unsupported configuration, kept on the summary so a consumer
// resolves the same evidence the census claims without reading unsupported.json a second time.
struct FSCompletenessUnsupportedWitness {
	String entry_id;
	String representation;
	FSCompletenessCoverageWitness witness;
};

struct FSCompletenessCensusSummary {
	// Ascending by key, so a report assembled from a summary is byte-identical between runs.
	Vector<FSCompletenessCoverageEntry> entries;
	int covered = 0;
	int uncovered = 0;
	int unsupported = 0;
	int quality_deferred = 0;
	// Present witnesses of unsupported.json, in document order. A deferred witness observes nothing and
	// is not recorded here, so it can never authorize an exemption.
	Vector<FSCompletenessUnsupportedWitness> unsupported_witnesses;

	// covered + uncovered + unsupported + quality_deferred, which always equals entries.size().
	int total() const;
};

// Which coverage claims one consumer is answerable for.
//
// Binding a `family_case` witness means resolving the family it names, and a run that publishes one
// family has resolved exactly one matrix. Resolving every other family a witness mentions makes a
// single-family run pay for claims that another family's own run already answers, which is the whole
// difference between a per-family gate and a whole-catalog audit. A scope is therefore data on the
// call rather than a mode inside the census: the strict and scheduled tiers, and the census tests,
// ask for everything; the presubmit tier asks for the family it is running.
//
// Only `family_case` witnesses are ever narrowed. A doctest case and a tracked fixture belong to no
// family and cost a registry lookup, so every scope binds them and no configuration can publish a
// coverage claim of those kinds without confirming it.
struct FSCompletenessCensusScope {
	// Empty means every family. Otherwise a `family_case` witness is bound when it names one of these.
	HashSet<String> families;
	// True when this scope was asked for everything, which is not the same as a scope that happens to
	// list every family the census currently names: the report has to say which question was asked.
	bool exhaustive = true;

	static FSCompletenessCensusScope everything();
	static FSCompletenessCensusScope for_family(const String &p_family);

	bool includes(const FSCompletenessCoverageWitness &p_witness) const;
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

	// The report document of a summary: one count per coverage status as a report number, one record
	// per entry in summary order, and the scope the publishing consumer bound its witnesses under.
	// `declared_families` counts the distinct families the census's own claims name and
	// `validated_families` how many of them this consumer bound, so a family-scoped census and a whole
	// catalog audit are told apart by the document rather than by knowing who wrote it.
	static Dictionary summary_report(
			const FSCompletenessCensusSummary &p_summary, const FSCompletenessCensusScope &p_scope);

	// Every distinct family the summary's claims name, ascending.
	static Vector<String> claimed_families(const FSCompletenessCensusSummary &p_summary);

	// The count members a summary report carries, in status order. Named here rather than spelled by
	// each consumer, so nothing can check a member the producer does not write.
	static Vector<String> summary_count_members();

	// True when p_entry's witness binds to something that observes it: a registered doctest case, a
	// tracked fixture, or exactly one cell of a registered rule family. r_detail explains a refusal.
	static bool resolve_witness(const String &p_root, const FSCompletenessCoverageEntry &p_entry,
			String &r_detail);

	// One message per witness the census claims and that does not resolve: the witness of every
	// `covered` cell, and every present negative witness an exemption stands on. An empty result is
	// what lets a run publish the census as evidence.
	static Vector<String> unresolved_witnesses(const String &p_root,
			const FSCompletenessCensusSummary &p_summary, const FSCompletenessCensusScope &p_scope);

	// One message per witness this configuration cannot bind because the configuration it declares is
	// not the one running. Such a witness is never counted as unresolved - a build that does not
	// compile a case learns nothing about whether the case exists - and never counted as confirmed
	// either, so a consumer reports the claim as unconfirmed rather than as evidence.
	static Vector<String> unconfirmable_witnesses(const String &p_root,
			const FSCompletenessCensusSummary &p_summary, const FSCompletenessCensusScope &p_scope);

	// True when p_witness declares a configuration this build is not. The declaration is the census
	// document's, so the same catalog reads the same way in every build; only the verdict differs.
	static bool witness_needs_other_configuration(const FSCompletenessCoverageWitness &p_witness);
};

} // namespace FSTests
