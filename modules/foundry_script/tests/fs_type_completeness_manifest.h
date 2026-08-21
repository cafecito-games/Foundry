/**************************************************************************/
/*  fs_type_completeness_manifest.h                                       */
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
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/pair.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

namespace FSTests {

struct FSCompletenessRequiredDimension {
	String dimension;
	Dictionary when;
};

struct FSCompletenessAnchor {
	String id;
	Dictionary coordinates;
	Dictionary expect;
	Vector<String> surfaces;
};

struct FSCompletenessRelation {
	String id;
	Dictionary from;
	Dictionary to;
	Dictionary derive;
};

// One declared witness of an exception: an identity and the full coordinates of the cell that must
// observe it. The coordinates are data in the rule manifest rather than a table in an adapter, so a
// family can add a witness without a C++ change and a witness can never silently name a cell that the
// manifest domain does not contain.
struct FSCompletenessWitness {
	String id;
	Dictionary coordinates;
};

struct FSCompletenessException {
	String id;
	String parent;
	Dictionary when;
	Dictionary derive;
	String rationale;
	Vector<FSCompletenessWitness> positive_witnesses;
	Vector<FSCompletenessWitness> boundary_witnesses;
};

struct FSCompletenessManifest {
	int schema_version = 0;
	String family;
	// Registered id of the adapter that renders, analyzes, and executes this family.
	String adapter;
	HashMap<String, Vector<String>> domain;
	Vector<String> domain_axis_order;
	Vector<FSCompletenessRequiredDimension> required_dimensions;
	Vector<FSCompletenessAnchor> anchors;
	Vector<FSCompletenessRelation> relations;
	Vector<FSCompletenessException> exceptions;
	int max_chain_length = 3;

	static Error load(const String &p_path, FSCompletenessManifest &r_manifest, Vector<String> &r_errors);
};

struct FSCompletenessPartition {
	int schema_version = 0;
	String axis;
	Vector<String> leaves;
	HashMap<String, HashSet<String>> classes;
};

struct FSCompletenessDimension {
	String id;
	HashSet<String> outcomes;
};

struct FSCompletenessSelection {
	HashSet<String> families;
	bool used_broad_core_fallback = false;
	Vector<String> validation_errors;
};

class FSCompletenessCapabilityMap {
	Vector<Pair<String, HashSet<String>>> production_prefixes;
	Vector<String> nonproduction_prefixes;
	HashSet<String> broad_core_families;
	bool loaded = false;

public:
	Error load(const String &p_path, Vector<String> &r_errors);
	Error validate_against_rule_directory(const String &p_directory, Vector<String> &r_errors) const;
	FSCompletenessSelection select(const Vector<String> &p_changed_paths) const;
};

class FSCompletenessCatalog {
	HashMap<String, FSCompletenessPartition> partitions;
	HashMap<String, FSCompletenessDimension> dimensions;
	friend Error validate_manifest_vocabulary(const FSCompletenessManifest &p_manifest,
			const FSCompletenessCatalog &p_catalog, Vector<String> &r_errors);

public:
	Error load(const String &p_root, Vector<String> &r_errors);
	// True when the catalog declares p_axis at all, whatever leaves it declares on it.
	bool has_axis(const String &p_axis) const;
	bool axis_has_leaf(const String &p_axis, const String &p_leaf) const;
	bool class_contains(const String &p_axis, const String &p_class, const String &p_leaf) const;
	bool dimension_has_outcome(const String &p_dimension, const String &p_outcome) const;
};

Error validate_manifest_vocabulary(const FSCompletenessManifest &p_manifest,
		const FSCompletenessCatalog &p_catalog, Vector<String> &r_errors);

// Coordinates p_manifest declares for p_witness_id, or an empty dictionary when no exception declares
// that witness. Witness identity is manifest data, so this is the one way to resolve it.
Dictionary manifest_witness_coordinates(
		const FSCompletenessManifest &p_manifest, const String &p_witness_id);

} // namespace FSTests
