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

struct FSCompletenessException {
	String id;
	String parent;
	Dictionary when;
	Dictionary derive;
	String rationale;
	Vector<String> positive_witnesses;
	Vector<String> boundary_witnesses;
};

struct FSCompletenessManifest {
	int schema_version = 0;
	String family;
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
	bool axis_has_leaf(const String &p_axis, const String &p_leaf) const;
	bool class_contains(const String &p_axis, const String &p_class, const String &p_leaf) const;
	bool dimension_has_outcome(const String &p_dimension, const String &p_outcome) const;
};

Error validate_manifest_vocabulary(const FSCompletenessManifest &p_manifest,
		const FSCompletenessCatalog &p_catalog, Vector<String> &r_errors);

} // namespace FSTests
