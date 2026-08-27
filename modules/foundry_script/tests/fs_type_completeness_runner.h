/**************************************************************************/
/*  fs_type_completeness_runner.h                                         */
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

#include "fs_type_completeness_common.h"
#include "fs_type_completeness_destination_wrapper_adapter.h"

#include "core/error/error_list.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

namespace FSTests {

// Every stage a structural failure can name. The strings live here once so a producer, a report, a
// comparator, and a test cannot drift apart on the spelling of a verdict.
namespace FSCompletenessStructuralStage {

static constexpr const char *ADAPTER_UNKNOWN = "adapter_unknown";
static constexpr const char *ADAPTER_DUPLICATE = "adapter_duplicate";
static constexpr const char *PROGRAM_CARDINALITY = "program_cardinality";
static constexpr const char *PUBLISHED_SURFACE_NOT_EXECUTED = "published_surface_not_executed";
static constexpr const char *NO_EVIDENCE = "no_evidence";
static constexpr const char *WITNESS_DECLARED_TWICE = "witness_declared_twice";
static constexpr const char *WITNESS_ID_UNKNOWN = "witness_id_unknown";
static constexpr const char *WITNESS_CELL_MISSING = "witness_cell_missing";
static constexpr const char *WITNESS_CELL_AMBIGUOUS = "witness_cell_ambiguous";
static constexpr const char *WITNESS_EXCEPTION_PROVENANCE_MISSING = "witness_exception_provenance_missing";
static constexpr const char *WITNESS_BOUNDARY_PROVENANCE_MISSING = "witness_boundary_provenance_missing";
static constexpr const char *WITNESS_RUNTIME_RESULT_MISSING = "witness_runtime_result_missing";
static constexpr const char *WITNESS_RUNTIME_IDENTITY_MISMATCH = "witness_runtime_identity_mismatch";
static constexpr const char *LEDGER_ENTRY_STALE = "ledger_entry_stale";
static constexpr const char *CENSUS_WITNESS_UNRESOLVED = "census_witness_unresolved";
static constexpr const char *RUN_TIMEOUT = "run_timeout";
static constexpr const char *RUN_TIMEOUT_REPORT_UNWRITABLE = "run_timeout_report_unwritable";
static constexpr const char *RUN_ABORTED = "run_aborted";

static constexpr const char *ALL[] = {
	ADAPTER_UNKNOWN,
	ADAPTER_DUPLICATE,
	PROGRAM_CARDINALITY,
	PUBLISHED_SURFACE_NOT_EXECUTED,
	NO_EVIDENCE,
	WITNESS_DECLARED_TWICE,
	WITNESS_ID_UNKNOWN,
	WITNESS_CELL_MISSING,
	WITNESS_CELL_AMBIGUOUS,
	WITNESS_EXCEPTION_PROVENANCE_MISSING,
	WITNESS_BOUNDARY_PROVENANCE_MISSING,
	WITNESS_RUNTIME_RESULT_MISSING,
	WITNESS_RUNTIME_IDENTITY_MISMATCH,
	LEDGER_ENTRY_STALE,
	CENSUS_WITNESS_UNRESOLVED,
	RUN_TIMEOUT,
	RUN_TIMEOUT_REPORT_UNWRITABLE,
	RUN_ABORTED,
};

} // namespace FSCompletenessStructuralStage

// Why a cell the run selected observed nothing this build could judge. A cell is never dropped
// silently: it is published with one of these reasons, and the vocabulary is closed the way the
// structural stages are, so a producer, a report, and a test share one spelling.
namespace FSCompletenessNotCoveredReason {

// The cell's expected evidence is an analyzer warning, and this build's analyzer emits none. Nothing
// it could observe would decide the cell either way, so an observation of "no warning" here is not
// evidence that the warning is missing.
static constexpr const char *DIAGNOSTICS_UNAVAILABLE_IN_CONFIGURATION =
		"diagnostics_unavailable_in_configuration";

// The cell's expected evidence is what a destination's dynamic type check did with a value it cannot
// hold, and this build compiles no such checks. A boundary that admits the value here is what the
// build is, not what the product regressed to, so nothing this cell could observe decides it.
static constexpr const char *DYNAMIC_TYPE_CHECKS_UNAVAILABLE_IN_CONFIGURATION =
		"dynamic_type_checks_unavailable_in_configuration";

static constexpr const char *ALL[] = {
	DIAGNOSTICS_UNAVAILABLE_IN_CONFIGURATION,
	DYNAMIC_TYPE_CHECKS_UNAVAILABLE_IN_CONFIGURATION,
};

} // namespace FSCompletenessNotCoveredReason

struct FSCompletenessFinding {
	String finding_id;
	String case_id;
	String family;
	String dimension;
	Variant expected;
	Variant actual;
	String classification = "unclassified";
	String artifact_path;
	Dictionary parity_evidence;
	String issue_url;
	String closure_packet_url;
	PackedStringArray permanent_test_paths;
	String migrated_from;
	PackedStringArray resolved_case_ids;
};

// Asking git whether a path is tracked is one question with one answer, so the runner and the census
// share the probe type and its default implementation instead of each carrying their own.
using FSCompletenessTrackedFileProbe = Completeness::TrackedFileProbe;
using FSCompletenessPersistedWriteHook = void (*)(const String &p_path);

// Monotonic microsecond clock the deadline is measured on. Injectable so a timeout can be proven
// deterministically instead of by racing a real wall clock.
using FSCompletenessClock = uint64_t (*)();

struct FSCompletenessRunOptions {
	String catalog_root;
	String family;
	String scratch_root;
	String report_path;
	void (*program_mutator)(FSCompletenessProgram &) = nullptr;
	void (*observation_mutator)(FSCompletenessObservation &) = nullptr;
	void (*runtime_result_mutator)(FSCompletenessRuntimeResult &) = nullptr;
	FSCompletenessTrackedFileProbe tracked_file_probe = nullptr;
	FSCompletenessPersistedWriteHook persisted_write_hook = nullptr;

	// Surface whose cases the published report carries; empty publishes every case. The run always
	// executes both surfaces, because parity evidence is only meaningful when both were observed;
	// this only narrows the published document for a consumer that compares one configuration.
	String published_surface;

	// Surface leaves whose cells this run renders and executes. Empty runs every surface the manifest
	// domain declares. A surface outside the domain is refused rather than silently running nothing.
	// Parity is only decided for pairs whose surfaces all ran, because parity evidence a run did not
	// observe is not evidence of agreement.
	HashSet<String> surfaces;

	// Binds every coverage claim the census makes, rather than only the claims that name this run's
	// own family. A run resolves its own family anyway, so the family-scoped default costs nothing;
	// binding another family's claim means resolving that family's whole matrix, which is that
	// family's own run's business. The strict and scheduled tiers ask for everything, because a whole
	// catalog audit is what they exist to be.
	bool validate_full_census = false;

	// Absolute deadline on `clock`, in microseconds. Zero runs without a deadline.
	uint64_t deadline_usec = 0;
	FSCompletenessClock clock = nullptr;
};

// A defect in the harness, the catalog, or the evidence contract rather than an observation about
// the product. Structural failures are reported separately from findings so a gate can never read
// a broken run as a clean one, and never file a harness defect as a product defect.
struct FSCompletenessStructuralFailure {
	String stage;
	String detail;
	String case_id;
	String witness_id;
	String exception_id;
	Error error_code = OK;
};

struct FSCompletenessRunResult {
	bool success = false;
	int executed_cells = 0;
	// Semantic pairs whose surfaces were all observed, and so could be compared. A pair the run saw on
	// one surface only is not compared and is not evidence of agreement, so nothing may reconcile
	// against it.
	int compared_surface_pairs = 0;
	Vector<FSCompletenessFinding> findings;
	Vector<FSCompletenessStructuralFailure> structural_failures;
	// Cells this build could not judge, ascending by case id, each published with the reason it could
	// not. They are not failures and not passes: a build that observes nothing about a cell has no
	// verdict to report, and hiding them would let a narrower configuration look like a clean run.
	Vector<String> not_covered_case_ids;
	// Census claims whose witness names a build configuration this one is not, in census order. They
	// are not failures - this build compiled no case to bind, so it observed nothing either way - but a
	// run that carries them confirmed less of the census than a run that carries none.
	Vector<String> unconfirmed_census_witnesses;
	String outcome = "not_run";
	Dictionary report;
};

// One declared witness bound to the single resolved cell its coordinates name.
struct FSCompletenessWitnessBinding {
	String witness_id;
	String exception_id;
	String parent_relation_id;
	bool boundary = false;
	const FSCompletenessResolvedCell *cell = nullptr;
};

// Resolves every witness a manifest declares to exactly one cell that actually observes the exception
// (or, for a boundary witness, the unexcepted parent relation). Each way a witness can fail to bind
// reports a distinct stage, so a comparator can tell an unresolvable witness from an ambiguous or an
// unobserving one. Exposed so every refusal is provable without running a whole matrix.
Error collect_witness_bindings(const FSCompletenessManifest &p_manifest,
		const FSCompletenessResolution &p_resolution, Vector<FSCompletenessWitnessBinding> &r_bindings,
		Vector<FSCompletenessStructuralFailure> &r_failures);

class FSCompletenessRunner {
public:
	static Error run(const FSCompletenessRunOptions &p_options, FSCompletenessRunResult &r_result);

	// The one dimension whose evidence is the agreement of two surfaces rather than one observation,
	// so it can only be decided by a run that executed both surfaces of a pair.
	static constexpr const char *TEXT_BYTECODE_PARITY_DIMENSION = "text_bytecode_parity";

	// The dimensions the runner itself observes for every family, whatever its adapter declares. A
	// ledger entry may name one of these even though no cell carries it.
	static HashSet<String> builtin_dimensions();

	// The report record of one structural failure. The one place a failure becomes a document.
	static Dictionary structural_failure_report(const FSCompletenessStructuralFailure &p_failure);

	// What this binary could have observed at all: whether it was built with the editor tooling
	// surfaces compiled in, and which adapter ids the registry hands out. A surface that is not in the
	// build is a property of the build rather than of the product, so every report carries the
	// configuration it was produced under and no consumer has to infer it from missing cases.
	static Dictionary configuration_report();

	// True when the expectations a cell carries can only be observed by an analyzer that emits
	// warnings. Read off the same dimension dictionary a report publishes, so the runner deciding a
	// cell and a consumer reading a published document apply one rule.
	static bool expectation_requires_analyzer_warnings(const Dictionary &p_expected_dimensions);

	// True when this build could observe everything p_expected_dimensions expects. The one place a
	// consumer asks whether this configuration can decide a cell, so a capability added to
	// `configuration_report` is honored everywhere rather than at each site that guards on one.
	static bool configuration_can_observe(const Dictionary &p_expected_dimensions);

	// The reason a cell this build cannot judge reports, or an empty string when it can judge it.
	static String unobservable_expectation_reason(const Dictionary &p_expected_dimensions);

	// True when the expectation is about a destination's dynamic type check. Any runtime obligation is:
	// the dimension records what a boundary did with a value its destination cannot hold, which is
	// decided by checks a build without them never compiled.
	static bool expectation_requires_dynamic_type_checks(const Dictionary &p_expected_dimensions);

	// The part of an evidence document that a build described by p_configuration could have observed:
	// with an analyzer that emits no warnings, the cells whose expectations are warnings, the
	// exceptions those cells witness, and warning-severity diagnostics are dropped. Applied to both
	// sides of a comparison it makes two builds comparable on what both could see; on a build that
	// does emit warnings it is the identity, so nothing that compares documents there gets weaker.
	static Variant evidence_observable_in_configuration(
			const Variant &p_document, const Dictionary &p_configuration);

	// Report members that describe the run or the build rather than the product it observed. They
	// differ between two runs that saw exactly the same thing, so they may never reach a digest, a
	// comparison, or a tracked evidence document.
	static Vector<String> non_evidence_report_members();

	// False when a document claims a matrix and reports nothing observed for it. Such a document has no
	// reading under which the absence of findings means agreement, so it may never be published as a
	// clean run whatever produced it.
	static bool report_carries_evidence(const Dictionary &p_report);

	// Publishes a document that belongs to a run without being a family report - the index over a
	// multi-family invocation - under exactly the contract a report is published with: the path must
	// be owned by the scratch root, must not resolve through a link or into the catalog or the
	// artifact tree, and the file is replaced atomically.
	static Error publish_owned_document(const String &p_scratch_root, const String &p_catalog_root,
			const String &p_document_path, const Dictionary &p_document);

	// Rewrites an already published document's verdict into the structural failure a crossed budget
	// makes it, and republishes it. A run notices its own crossing after its last write; the command
	// driving several runs can only notice after the last write of the whole invocation. Both go
	// through this, so a report, an index over it, and the exit code derived from them cannot disagree.
	static Error republish_timed_out_document(const String &p_scratch_root, const String &p_catalog_root,
			const String &p_document_path, const String &p_detail);
};

} // namespace FSTests
