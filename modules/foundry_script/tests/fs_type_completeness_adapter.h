/**************************************************************************/
/*  fs_type_completeness_adapter.h                                        */
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
#include "fs_type_completeness_manifest.h"

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

namespace FSTests {

// The rejecting half of one cell: a program that routes a value the cell's destination cannot hold
// through the cell's own boundary. Whether that value is refused, and how, is what makes a runtime
// obligation an observation about the product rather than a reading of the destination's descriptor:
// a descriptor still names the destination when the check that enforces it is gone.
//
// The probe's evidence is the status the runner reports and what the destination held afterwards,
// never the exact wording of a rejection, so `staged_document` pins only the status a checked
// boundary produces and is a staging companion rather than the assertion.
//
// `key` names the probe rather than the cell: every cell with the same destination and boundary
// renders a byte-identical probe, so the same program is staged and executed once. A key that two
// different sources claim is refused rather than resolved in favor of either.
struct FSCompletenessNegativeProbe {
	String key;
	String source;
	String staged_document;

	bool is_empty() const { return source.is_empty(); }
};

// One rendered program: the source a single resolved cell turns into, together with the identity and
// the coordinates it was rendered from. Every family produces the same shape, so the runner never
// needs to know which family rendered it.
struct FSCompletenessProgram {
	String case_id;
	String surface;
	Dictionary coordinates;
	String source;
	String expected_output;
	// Empty for a family whose cells carry no runtime obligation.
	FSCompletenessNegativeProbe negative_probe;
};

struct FSCompletenessObservation {
	String case_id;
	String surface;
	Dictionary dimensions;
	PackedStringArray diagnostics;
	// One dictionary per observed diagnostic carrying "severity", "category", "code", "line",
	// "column", "message", and "suppressed". Warnings are recorded even when an annotation keeps
	// them out of `diagnostics`, so a severity regression cannot hide behind warning suppression.
	Array diagnostic_records;
	String produced_output;
};

struct FSCompletenessRuntimeResult : FSCompletenessObservation {
	bool passed = false;
	String status;
};

// Evidence that must agree between the two surfaces of one semantic case because it does not depend
// on the surface. Parity is decided from this in exactly one place, so a surface disagreement can
// never be counted in one part of the harness and missed in another.
struct FSCompletenessSurfaceEvidenceMismatch {
	bool produced_output = false;
	bool diagnostics = false;
	bool diagnostic_records = false;
	bool runtime_status = false;

	bool any() const {
		return produced_output || diagnostics || diagnostic_records || runtime_status;
	}
};

FSCompletenessSurfaceEvidenceMismatch compare_surface_evidence(
		const FSCompletenessRuntimeResult &p_text, const FSCompletenessRuntimeResult &p_bytecode);

// The runtime results of one execution, grouped by the surface that produced them. The two surface
// leaves are the ones the `surface` partition declares; an adapter that claims to render any other
// leaf is refused when its renderable coordinates are validated, so a result can never be filed
// under a surface no consumer looks at.
struct FSCompletenessRuntimeBatch {
	HashMap<String, FSCompletenessRuntimeResult> text;
	HashMap<String, FSCompletenessRuntimeResult> bytecode;

	HashMap<String, FSCompletenessRuntimeResult> *results_for_surface(const String &p_surface);
	const HashMap<String, FSCompletenessRuntimeResult> *results_for_surface(const String &p_surface) const;
};

// What one completeness family contributes to a run. Everything else - loading the catalog, resolving
// the matrix, binding witnesses, deciding parity, publishing the report - belongs to the runner and is
// identical for every family, so a new family is a new adapter and a new rule manifest, never a new
// branch in the runner.
//
// Adapters are stateless: the registry hands out one shared instance per id and several runs may use
// it concurrently, so every method is const and keeps its state in its arguments.
class FSCompletenessFamilyAdapter {
public:
	// The id a rule manifest names in its `adapter` member. Unique across the registry.
	virtual String id() const = 0;

	// Turns one resolved cell into the program that observes it.
	virtual Error render(const FSCompletenessResolvedCell &p_cell, FSCompletenessProgram &r_program) const = 0;

	// Static evidence about one program, observed on one surface.
	virtual FSCompletenessObservation analyze(
			const FSCompletenessProgram &p_program, const String &p_surface) const = 0;

	// Runs every rendered program and records what each surface observed.
	virtual Error execute(const String &p_scratch_root, const Vector<FSCompletenessProgram> &p_programs,
			FSCompletenessRuntimeBatch &r_batch) const = 0;

	// The dimensions this adapter can produce a value for. A manifest that requires any other
	// dimension is refused when it is validated, so an unobservable dimension is a catalog defect
	// rather than an uncovered cell at run time.
	virtual HashSet<String> observable_dimensions() const = 0;

	// Axis to the leaves of that axis this adapter can render a program for. The single declaration of
	// what the adapter covers: coordinate legality is this plus `FSCompletenessCatalog::axis_has_leaf`.
	virtual HashMap<String, Vector<String>> renderable_leaves() const = 0;

	// Checks the adapter's own declaration against the catalog: every axis it renders must be an axis
	// the catalog declares, and no axis may declare a leaf twice or an empty one. The declared leaves
	// are a capability set rather than a selection, so a catalog that does not yet declare one of them
	// is not a defect; a manifest domain that selects a leaf the adapter cannot render is. Appends one
	// message per defect and returns ERR_INVALID_DATA when any was appended.
	virtual Error renderable_coordinates(const FSCompletenessCatalog &p_catalog, Vector<String> &r_errors) const;

	// True when p_leaf is one of the leaves this adapter declares on p_axis.
	bool can_render(const String &p_axis, const String &p_leaf) const;

	FSCompletenessFamilyAdapter() = default;
	virtual ~FSCompletenessFamilyAdapter() = default;

	FSCompletenessFamilyAdapter(const FSCompletenessFamilyAdapter &) = delete;
	FSCompletenessFamilyAdapter &operator=(const FSCompletenessFamilyAdapter &) = delete;
};

// The one lookup from an adapter id to an adapter. Backed by a single static table rather than by
// self-registering globals, so the set of adapters is the same in every build and does not depend on
// static initialization order.
class FSCompletenessAdapterRegistry {
public:
	static const FSCompletenessFamilyAdapter *find(const String &p_adapter_id);

	// Every registered id, in ascending order.
	static Vector<String> ids();

	// The first id p_table registers twice, or an empty string when every id is unique and non-empty.
	// A malformed entry is reported as "<null>" or "<empty>" rather than silently overwriting an
	// earlier entry, so a caller only has to test the result for emptiness.
	static String validate(const Vector<const FSCompletenessFamilyAdapter *> &p_table);

	// `validate` over the built-in table.
	static String validate();
};

} // namespace FSTests
