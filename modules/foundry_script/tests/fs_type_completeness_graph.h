/**************************************************************************/
/*  fs_type_completeness_graph.h                                          */
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

#include "fs_type_completeness_case_id.h"
#include "fs_type_completeness_manifest.h"

#include "core/variant/variant.h"

namespace FSTests {

struct FSCompletenessProvenanceStep {
	String relation_id;
	String exception_id;
	Dictionary source_coordinates;
	Dictionary target_coordinates;
	Dictionary input_dimensions;
	Dictionary output_dimensions;
};

struct FSCompletenessResolvedDimension {
	String dimension;
	Variant expected;
	Vector<FSCompletenessProvenanceStep> canonical_provenance;
	Vector<Vector<FSCompletenessProvenanceStep>> agreeing_provenance;
};

struct FSCompletenessResolvedCell {
	String case_id;
	Dictionary coordinates;
	HashMap<String, FSCompletenessResolvedDimension> dimensions;

	const FSCompletenessResolvedDimension *find_dimension(const String &p_dimension) const;
};

struct FSCompletenessResolution {
	Vector<FSCompletenessResolvedCell> cells;
	int max_observed_chain_length = 0;
	int uncovered_dimension_count = 0;
	int ambiguous_dimension_count = 0;
};

class FSCompletenessGraph {
public:
	static bool predicate_matches(const Dictionary &p_predicate, const Dictionary &p_coordinates,
			const FSCompletenessCatalog &p_catalog, Vector<String> *r_errors = nullptr);
	static Error resolve(const FSCompletenessManifest &p_manifest, const FSCompletenessCatalog &p_catalog,
			FSCompletenessResolution &r_resolution, Vector<String> &r_errors);
};

} // namespace FSTests
