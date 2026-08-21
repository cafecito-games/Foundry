/**************************************************************************/
/*  test_type_completeness_manifest.h                                     */
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

#include "fs_type_completeness_manifest.h"

#include "fs_temporary_project_tree.h"
#include "tests/test_macros.h"

namespace FSTests {

static String write_completeness_manifest(TemporaryProjectTree &p_tree, const String &p_contents) {
	p_tree.write_file("manifest.json", p_contents);
	return p_tree.root.path_join("manifest.json");
}

static Error load_completeness_manifest_text(const String &p_tree_name, const String &p_contents,
		FSCompletenessManifest &r_manifest, Vector<String> &r_errors) {
	TemporaryProjectTree tree(p_tree_name);
	if (!tree.is_valid()) {
		return ERR_CANT_CREATE;
	}
	return FSCompletenessManifest::load(write_completeness_manifest(tree, p_contents), r_manifest, r_errors);
}

static bool completeness_errors_contain(const Vector<String> &p_errors, const String &p_expected) {
	for (const String &error : p_errors) {
		if (error == p_expected) {
			return true;
		}
	}
	return false;
}

static bool completeness_errors_contain_text(const Vector<String> &p_errors, const String &p_expected) {
	for (const String &error : p_errors) {
		if (error.contains(p_expected)) {
			return true;
		}
	}
	return false;
}

static String write_representative_completeness_manifest(TemporaryProjectTree &p_tree) {
	return write_completeness_manifest(p_tree, R"JSON({
  "schema_version": 1,
  "family": "assignment_compatibility",
  "domain": {
    "destination": ["plain", "union"],
    "source_proof": ["static_member", "numeric_constant", "gradual", "erased", "variant"],
    "boundary": ["argument_binding", "reflective_write"],
    "surface": ["text", "bytecode"]
  },
  "required_dimensions": [
    {"dimension": "analysis", "when": {}},
    {"dimension": "runtime_obligation", "when": {"source_proof": {"class": "unproven"}}},
    {"dimension": "stored_carrier", "when": {"destination": "union"}}
  ],
  "anchors": [{
    "id": "plain_static_member",
    "coordinates": {
      "destination": "plain",
      "source_proof": "static_member",
      "boundary": "argument_binding",
      "surface": "text"
    },
    "expect": {
      "analysis": "accept",
      "runtime_obligation": "typed_destination_check",
      "stored_carrier": "plain_destination"
    },
    "surfaces": ["text", "bytecode"]
  }],
  "relations": [{
    "id": "unproven_to_union",
    "from": {"destination": "plain", "source_proof": {"class": "unproven"}},
    "to": {"destination": "union", "surface": "bytecode"},
    "derive": {
      "analysis": "same",
      "runtime_obligation": "union_membership_check",
      "stored_carrier": "admitting_alternative"
    }
  }],
  "exceptions": [{
    "id": "reflective_rejection",
    "parent": "unproven_to_union",
    "when": {"boundary": "reflective_write", "source_proof": "variant"},
    "derive": {"analysis": "reject"},
    "rationale": "Reflective writes do not establish a static source proof.",
    "positive_witnesses": ["reflective_union_accepts"],
    "boundary_witnesses": ["reflective_plain_rejects"]
  }]
})JSON");
}

static FSCompletenessManifest load_representative_completeness_manifest(TemporaryProjectTree &p_tree) {
	FSCompletenessManifest manifest;
	Vector<String> errors;
	const Error load_error = FSCompletenessManifest::load(write_representative_completeness_manifest(p_tree), manifest, errors);
	REQUIRE_MESSAGE(load_error == OK, String(" | ").join(errors));
	return manifest;
}

static const char *type_completeness_catalog_root = "modules/foundry_script/tests/type_completeness";
static const char *type_completeness_capability_path =
		"modules/foundry_script/tests/type_completeness/capabilities.json";

static String write_completeness_capability_map(TemporaryProjectTree &p_tree, const String &p_contents) {
	p_tree.write_file("capabilities.json", p_contents);
	return p_tree.root.path_join("capabilities.json");
}

static String make_completeness_rule(const String &p_family) {
	return vformat(R"JSON({
  "schema_version": 1,
  "family": "%s",
  "domain": {"shape": ["plain"]},
  "required_dimensions": [],
  "anchors": [],
  "relations": [],
  "exceptions": []
})JSON",
			p_family);
}

static bool selection_has_only_family(const FSCompletenessSelection &p_selection, const String &p_family) {
	return p_selection.families.size() == 1 && p_selection.families.has(p_family);
}

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness][Manifest]") {
	TEST_CASE("TypeCompleteness Manifest loads a valid typed manifest") {
		TemporaryProjectTree tree("type_completeness_manifest_happy");
		REQUIRE(tree.is_valid());
		const String path = write_completeness_manifest(tree, R"JSON({
  "schema_version": 1,
  "family": "sample",
  "domain": {"shape": ["plain", "union"], "surface": ["text"]},
  "required_dimensions": [{"dimension": "analysis", "when": {}}],
  "anchors": [{
    "id": "plain",
    "coordinates": {"shape": "plain", "surface": "text"},
    "expect": {"analysis": "accept"}
  }],
  "relations": [{
    "id": "wrap",
    "from": {"shape": "plain"},
    "to": {"shape": "union"},
    "derive": {"analysis": "same"}
  }],
  "exceptions": []
})JSON");

		FSCompletenessManifest manifest;
		manifest.family = "stale";
		Vector<String> errors;
		errors.push_back("stale error");
		const Error load_error = FSCompletenessManifest::load(path, manifest, errors);
		REQUIRE_MESSAGE(load_error == OK, String(" | ").join(errors));
		REQUIRE(errors.is_empty());
		CHECK_EQ(manifest.schema_version, 1);
		CHECK_EQ(manifest.family, "sample");
		CHECK_EQ(manifest.domain.size(), 2);
		CHECK_EQ(manifest.domain_axis_order.size(), 2);
		CHECK_EQ(manifest.domain_axis_order[0], "shape");
		CHECK_EQ(manifest.domain_axis_order[1], "surface");
		CHECK_EQ(manifest.required_dimensions.size(), 1);
		CHECK_EQ(manifest.anchors.size(), 1);
		CHECK_EQ(manifest.anchors[0].id, "plain");
		CHECK_EQ(manifest.relations.size(), 1);
		CHECK_EQ(manifest.relations[0].id, "wrap");
		CHECK_EQ(manifest.max_chain_length, 3);
	}

	TEST_CASE("TypeCompleteness Manifest aggregates malformed records and fails closed") {
		TemporaryProjectTree tree("type_completeness_manifest_malformed");
		REQUIRE(tree.is_valid());
		const String path = write_completeness_manifest(tree, R"JSON({
  "schema_version": 1,
  "family": "sample",
  "domain": {"shape": ["plain", "plain"]},
  "required_dimensions": [],
  "anchors": [{"id": "same"}, {"id": "same"}],
  "relations": [],
  "exceptions": []
})JSON");

		FSCompletenessManifest manifest;
		manifest.family = "stale";
		manifest.anchors.push_back(FSCompletenessAnchor());
		Vector<String> errors;
		CHECK_EQ(FSCompletenessManifest::load(path, manifest, errors), ERR_INVALID_DATA);
		CHECK_GE(errors.size(), 3);
		CHECK_EQ(manifest.schema_version, 0);
		CHECK(manifest.family.is_empty());
		CHECK(manifest.anchors.is_empty());
	}

	TEST_CASE("TypeCompleteness Manifest loads exceptions witnesses surfaces and explicit chain bound") {
		TemporaryProjectTree tree("type_completeness_manifest_full_records");
		REQUIRE(tree.is_valid());
		const String path = write_completeness_manifest(tree, R"JSON({
  "schema_version": 1,
  "family": "complete",
  "domain": {"shape": ["plain"], "surface": ["text", "bytecode"]},
  "required_dimensions": [{"dimension": "analysis", "when": {"surface": "text"}}],
  "anchors": [{
    "id": "anchor",
    "coordinates": {"shape": "plain", "surface": "text"},
    "expect": {"analysis": "accept"},
    "surfaces": ["text", "bytecode"]
  }],
  "relations": [{
    "id": "relation",
    "from": {"surface": "text"},
    "to": {"surface": "bytecode"},
    "derive": {"analysis": "same"}
  }],
  "exceptions": [{
    "id": "exception",
    "parent": "relation",
    "when": {"shape": "plain"},
    "derive": {"analysis": "reject"},
    "rationale": "The bytecode boundary is intentional.",
    "positive_witnesses": ["accept_plain"],
    "boundary_witnesses": ["reject_plain"]
  }],
  "max_chain_length": 2
})JSON");

		FSCompletenessManifest manifest;
		Vector<String> errors;
		const Error load_error = FSCompletenessManifest::load(path, manifest, errors);
		REQUIRE_MESSAGE(load_error == OK, String(" | ").join(errors));
		REQUIRE(errors.is_empty());
		CHECK_EQ(manifest.anchors[0].surfaces.size(), 2);
		CHECK_EQ(manifest.anchors[0].surfaces[1], "bytecode");
		REQUIRE_EQ(manifest.exceptions.size(), 1);
		CHECK_EQ(manifest.exceptions[0].parent, "relation");
		CHECK_EQ(manifest.exceptions[0].positive_witnesses[0], "accept_plain");
		CHECK_EQ(manifest.exceptions[0].boundary_witnesses[0], "reject_plain");
		CHECK_EQ(manifest.max_chain_length, 2);
	}

	TEST_CASE("TypeCompleteness Manifest rejects invalid record contracts and chain bounds together") {
		TemporaryProjectTree tree("type_completeness_manifest_invalid_contract");
		REQUIRE(tree.is_valid());
		const String path = write_completeness_manifest(tree, R"JSON({
  "schema_version": 1,
  "family": "sample",
  "domain": {"shape": [], "surface": [""]},
  "required_dimensions": [
    {"dimension": "analysis", "when": {}},
    {"dimension": "analysis", "when": {}}
  ],
  "anchors": [{"id": "shared", "coordinates": {}, "expect": {}}],
  "relations": [{"id": "shared", "from": {}, "to": {}, "derive": {}}],
  "exceptions": [{
    "id": "exception",
    "parent": "missing",
    "when": {},
    "derive": {},
    "rationale": "",
    "positive_witnesses": [],
    "boundary_witnesses": []
  }],
  "max_chain_length": 4
})JSON");

		FSCompletenessManifest manifest;
		Vector<String> errors;
		CHECK_EQ(FSCompletenessManifest::load(path, manifest, errors), ERR_INVALID_DATA);
		CHECK_GE(errors.size(), 10);
		CHECK(manifest.family.is_empty());
	}

	TEST_CASE("TypeCompleteness Manifest rejects malformed syntax with a parse diagnostic") {
		FSCompletenessManifest manifest;
		manifest.family = "stale";
		Vector<String> errors;
		errors.push_back("stale error");

		CHECK_EQ(load_completeness_manifest_text("type_completeness_manifest_bad_syntax", R"JSON({
  "schema_version": 1,
  "family": "sample"
)JSON",
						 manifest, errors),
				ERR_INVALID_DATA);
		CHECK_EQ(errors.size(), 1);
		CHECK(completeness_errors_contain_text(errors, "$: invalid JSON at line"));
		CHECK(completeness_errors_contain_text(errors, "Expected '}'"));
		CHECK_EQ(manifest.schema_version, 0);
		CHECK(manifest.family.is_empty());
	}

	TEST_CASE("TypeCompleteness Manifest rejects a non-object root and clears caller state") {
		FSCompletenessManifest manifest;
		manifest.family = "stale";
		manifest.relations.push_back(FSCompletenessRelation());
		Vector<String> errors;
		errors.push_back("stale error");

		CHECK_EQ(load_completeness_manifest_text("type_completeness_manifest_array_root", "[]", manifest, errors),
				ERR_INVALID_DATA);
		REQUIRE_EQ(errors.size(), 1);
		CHECK_EQ(errors[0], "$: expected an object");
		CHECK(manifest.family.is_empty());
		CHECK(manifest.relations.is_empty());
	}

	TEST_CASE("TypeCompleteness Manifest reports each wrong required root field type") {
		FSCompletenessManifest manifest;
		Vector<String> errors;
		CHECK_EQ(load_completeness_manifest_text("type_completeness_manifest_wrong_root_types", R"JSON({
  "schema_version": "1",
  "family": 7,
  "domain": [],
  "required_dimensions": {},
  "anchors": {},
  "relations": {},
  "exceptions": {}
})JSON",
						 manifest, errors),
				ERR_INVALID_DATA);
		CHECK(completeness_errors_contain(errors, "$.schema_version: expected an integer"));
		CHECK(completeness_errors_contain(errors, "$.family: expected a string"));
		CHECK(completeness_errors_contain(errors, "$.domain: expected an object"));
		CHECK(completeness_errors_contain(errors, "$.required_dimensions: expected an array"));
		CHECK(completeness_errors_contain(errors, "$.anchors: expected an array"));
		CHECK(completeness_errors_contain(errors, "$.relations: expected an array"));
		CHECK(completeness_errors_contain(errors, "$.exceptions: expected an array"));
	}

	TEST_CASE("TypeCompleteness Manifest quotes dynamic domain axes in duplicate diagnostics") {
		FSCompletenessManifest manifest;
		Vector<String> errors;
		CHECK_EQ(load_completeness_manifest_text("type_completeness_manifest_quoted_axis", R"JSON({
  "schema_version": 1,
  "family": "sample",
  "domain": {"a.b": ["plain", "plain"]},
  "required_dimensions": [],
  "anchors": [],
  "relations": [],
  "exceptions": []
})JSON",
						 manifest, errors),
				ERR_INVALID_DATA);
		CHECK(completeness_errors_contain(errors, "$.domain[\"a.b\"][1]: duplicate value 'plain'"));
	}

	TEST_CASE("TypeCompleteness Manifest rejects a cross-kind duplicate id") {
		FSCompletenessManifest manifest;
		Vector<String> errors;
		CHECK_EQ(load_completeness_manifest_text("type_completeness_manifest_cross_kind_id", R"JSON({
  "schema_version": 1,
  "family": "sample",
  "domain": {"shape": ["plain"]},
  "required_dimensions": [],
  "anchors": [{"id": "shared", "coordinates": {"shape": "plain"}, "expect": {"analysis": "accept"}}],
  "relations": [{"id": "shared", "from": {"shape": "plain"}, "to": {"shape": "plain"}, "derive": {"analysis": "same"}}],
  "exceptions": []
})JSON",
						 manifest, errors),
				ERR_INVALID_DATA);
		CHECK(completeness_errors_contain(errors, "$.relations[0].id: duplicate id 'shared'"));
	}

	TEST_CASE("TypeCompleteness Manifest reports exception parent derive and witness violations") {
		FSCompletenessManifest manifest;
		Vector<String> errors;
		CHECK_EQ(load_completeness_manifest_text("type_completeness_manifest_bad_exception", R"JSON({
  "schema_version": 1,
  "family": "sample",
  "domain": {"shape": ["plain"]},
  "required_dimensions": [],
  "anchors": [],
  "relations": [{"id": "relation", "from": {"shape": "plain"}, "to": {"shape": "plain"}, "derive": {"analysis": "same"}}],
  "exceptions": [{
    "id": "exception",
    "parent": "missing",
    "when": {},
    "derive": {},
    "rationale": "Boundary behavior.",
    "positive_witnesses": ["", 7],
    "boundary_witnesses": []
  }]
})JSON",
						 manifest, errors),
				ERR_INVALID_DATA);
		CHECK(completeness_errors_contain(errors, "$.exceptions[0].parent: unknown relation 'missing'"));
		CHECK(completeness_errors_contain(errors, "$.exceptions[0].derive: must be non-empty"));
		CHECK(completeness_errors_contain(errors, "$.exceptions[0].positive_witnesses[0]: must be non-empty"));
		CHECK(completeness_errors_contain(errors, "$.exceptions[0].positive_witnesses[1]: expected a string"));
		CHECK(completeness_errors_contain(errors,
				"$.exceptions[0].boundary_witnesses: must contain at least one string"));
	}

	TEST_CASE("TypeCompleteness Manifest rejects both max chain bound edges") {
		for (int bound : { 0, 4 }) {
			FSCompletenessManifest manifest;
			Vector<String> errors;
			const String contents = vformat(R"JSON({
  "schema_version": 1,
  "family": "sample",
  "domain": {"shape": ["plain"]},
  "required_dimensions": [],
  "anchors": [],
  "relations": [],
  "exceptions": [],
  "max_chain_length": %d
})JSON",
					bound);
			CHECK_EQ(load_completeness_manifest_text(vformat("type_completeness_manifest_chain_%d", bound), contents,
							 manifest, errors),
					ERR_INVALID_DATA);
			CHECK(completeness_errors_contain(errors, "$.max_chain_length: must be between 1 and 3"));
		}
	}

	TEST_CASE("TypeCompleteness Manifest rejects duplicate root object members") {
		FSCompletenessManifest manifest;
		manifest.family = "stale";
		Vector<String> errors;
		CHECK_EQ(load_completeness_manifest_text("type_completeness_manifest_duplicate_root", R"JSON({
  "schema_version": 1,
  "family": "first",
  "family": "second",
  "domain": {"shape": ["plain"]},
  "required_dimensions": [],
  "anchors": [],
  "relations": [],
  "exceptions": []
})JSON",
						 manifest, errors),
				ERR_INVALID_DATA);
		CHECK(completeness_errors_contain(errors, "$.family: duplicate object member 'family'"));
		CHECK(manifest.family.is_empty());
	}

	TEST_CASE("TypeCompleteness Manifest rejects escaped-equivalent members in nested objects") {
		FSCompletenessManifest manifest;
		Vector<String> errors;
		CHECK_EQ(load_completeness_manifest_text("type_completeness_manifest_duplicate_nested", R"JSON({
  "schema_version": 1,
  "family": "sample",
  "domain": {"shape": ["plain"]},
  "required_dimensions": [],
  "anchors": [{
    "id": "anchor",
    "coordinates": {"a.b": "first", "a\u002eb": "second"},
    "expect": {"analysis": "accept"}
  }],
  "relations": [],
  "exceptions": []
})JSON",
						 manifest, errors),
				ERR_INVALID_DATA);
		CHECK(completeness_errors_contain(errors,
				"$.anchors[0].coordinates[\"a.b\"]: duplicate object member 'a.b'"));
		CHECK(manifest.anchors.is_empty());
	}

	TEST_CASE("TypeCompleteness Manifest catalog loads checked-in vocabularies") {
		FSCompletenessCatalog catalog;
		Vector<String> errors;
		CHECK(catalog.load(type_completeness_catalog_root, errors) == OK);
		CHECK_MESSAGE(errors.is_empty(), String(" | ").join(errors));
		CHECK(catalog.axis_has_leaf("source_proof", "numeric_constant"));
		CHECK(catalog.class_contains("source_proof", "unproven", "variant"));
		CHECK(catalog.dimension_has_outcome("runtime_obligation", "union_membership_check"));
		CHECK_FALSE(catalog.axis_has_leaf("source_proof", "not_registered"));
		CHECK_FALSE(catalog.class_contains("source_proof", "unproven", "static_member"));
		CHECK_FALSE(catalog.dimension_has_outcome("analysis", "same"));
	}

	TEST_CASE("TypeCompleteness Manifest vocabulary accepts a representative valid rule") {
		TemporaryProjectTree tree("type_completeness_vocabulary_valid");
		REQUIRE(tree.is_valid());
		const FSCompletenessManifest manifest = load_representative_completeness_manifest(tree);
		FSCompletenessCatalog catalog;
		Vector<String> errors;
		REQUIRE_MESSAGE(catalog.load(type_completeness_catalog_root, errors) == OK, String(" | ").join(errors));
		errors.push_back("stale error");
		CHECK(validate_manifest_vocabulary(manifest, catalog, errors) == OK);
		CHECK(errors.is_empty());
	}

	TEST_CASE("TypeCompleteness Manifest vocabulary rejects unknown concrete coordinates") {
		TemporaryProjectTree tree("type_completeness_vocabulary_coordinates");
		REQUIRE(tree.is_valid());
		FSCompletenessManifest manifest = load_representative_completeness_manifest(tree);
		FSCompletenessCatalog catalog;
		Vector<String> errors;
		REQUIRE(catalog.load(type_completeness_catalog_root, errors) == OK);

		manifest.anchors.write[0].coordinates["boundary"] = "parameter";
		CHECK(validate_manifest_vocabulary(manifest, catalog, errors) == ERR_INVALID_DATA);
		CHECK(completeness_errors_contain(errors,
				"anchor 'plain_static_member' uses unknown boundary leaf 'parameter'"));

		manifest = load_representative_completeness_manifest(tree);
		manifest.anchors.write[0].coordinates["unregistered_axis"] = "plain";
		manifest.anchors.write[0].surfaces.push_back("native");
		CHECK(validate_manifest_vocabulary(manifest, catalog, errors) == ERR_INVALID_DATA);
		CHECK(completeness_errors_contain(errors,
				"anchor 'plain_static_member' uses unknown axis 'unregistered_axis'"));
		CHECK(completeness_errors_contain(errors,
				"anchor 'plain_static_member' uses unknown surface leaf 'native'"));
	}

	TEST_CASE("TypeCompleteness Manifest vocabulary rejects unknown domain leaves") {
		TemporaryProjectTree tree("type_completeness_vocabulary_domain");
		REQUIRE(tree.is_valid());
		FSCompletenessManifest manifest = load_representative_completeness_manifest(tree);
		manifest.domain["source_proof"].push_back("deduced");
		manifest.domain.insert("unregistered_axis", Vector<String>({ "plain" }));
		manifest.domain_axis_order.push_back("unregistered_axis");

		FSCompletenessCatalog catalog;
		Vector<String> errors;
		REQUIRE(catalog.load(type_completeness_catalog_root, errors) == OK);
		CHECK(validate_manifest_vocabulary(manifest, catalog, errors) == ERR_INVALID_DATA);
		CHECK(completeness_errors_contain(errors, "domain axis 'source_proof' uses unknown leaf 'deduced'"));
		CHECK(completeness_errors_contain(errors, "domain uses unknown axis 'unregistered_axis'"));
	}

	TEST_CASE("TypeCompleteness Manifest vocabulary validates selector leaves and classes") {
		TemporaryProjectTree tree("type_completeness_vocabulary_selectors");
		REQUIRE(tree.is_valid());
		const FSCompletenessManifest valid_manifest = load_representative_completeness_manifest(tree);
		FSCompletenessCatalog catalog;
		Vector<String> errors;
		REQUIRE(catalog.load(type_completeness_catalog_root, errors) == OK);

		FSCompletenessManifest manifest = valid_manifest;
		Dictionary unknown_class;
		unknown_class["class"] = "not_registered";
		manifest.relations.write[0].from["source_proof"] = unknown_class;
		CHECK(validate_manifest_vocabulary(manifest, catalog, errors) == ERR_INVALID_DATA);
		CHECK(completeness_errors_contain(errors,
				"relation 'unproven_to_union' from selector for axis 'source_proof' references unknown class 'not_registered'"));

		manifest = valid_manifest;
		manifest.relations.write[0].to["source_proof"] = "not_registered";
		CHECK(validate_manifest_vocabulary(manifest, catalog, errors) == ERR_INVALID_DATA);
		CHECK(completeness_errors_contain(errors,
				"relation 'unproven_to_union' to coordinate for axis 'source_proof' uses unknown leaf 'not_registered'"));

		manifest = valid_manifest;
		Dictionary extra_field;
		extra_field["class"] = "unproven";
		extra_field["except"] = "variant";
		manifest.required_dimensions.write[1].when["source_proof"] = extra_field;
		CHECK(validate_manifest_vocabulary(manifest, catalog, errors) == ERR_INVALID_DATA);
		CHECK(completeness_errors_contain(errors,
				"required dimension 'runtime_obligation' when selector for axis 'source_proof' must be a string leaf or an object containing only 'class'"));
	}

	TEST_CASE("TypeCompleteness Manifest vocabulary requires concrete relation destination patches") {
		TemporaryProjectTree tree("type_completeness_vocabulary_relation_to");
		REQUIRE(tree.is_valid());
		const FSCompletenessManifest valid_manifest = load_representative_completeness_manifest(tree);
		FSCompletenessCatalog catalog;
		Vector<String> errors;
		REQUIRE(catalog.load(type_completeness_catalog_root, errors) == OK);
		CHECK(validate_manifest_vocabulary(valid_manifest, catalog, errors) == OK);

		FSCompletenessManifest manifest = valid_manifest;
		Dictionary class_predicate;
		class_predicate["class"] = "unproven";
		manifest.relations.write[0].to["source_proof"] = class_predicate;
		CHECK(validate_manifest_vocabulary(manifest, catalog, errors) == ERR_INVALID_DATA);
		CHECK(completeness_errors_contain(errors,
				"relation 'unproven_to_union' to coordinate for axis 'source_proof' must be a non-empty string leaf"));

		manifest = valid_manifest;
		manifest.relations.write[0].to["surface"] = 7;
		manifest.relations.write[0].to["destination"] = "";
		CHECK(validate_manifest_vocabulary(manifest, catalog, errors) == ERR_INVALID_DATA);
		CHECK(completeness_errors_contain(errors,
				"relation 'unproven_to_union' to coordinate for axis 'destination' must be a non-empty string leaf"));
		CHECK(completeness_errors_contain(errors,
				"relation 'unproven_to_union' to coordinate for axis 'surface' must be a non-empty string leaf"));
	}

	TEST_CASE("TypeCompleteness Manifest vocabulary validates dimensions and outcomes") {
		TemporaryProjectTree tree("type_completeness_vocabulary_dimensions");
		REQUIRE(tree.is_valid());
		const FSCompletenessManifest valid_manifest = load_representative_completeness_manifest(tree);
		FSCompletenessCatalog catalog;
		Vector<String> errors;
		REQUIRE(catalog.load(type_completeness_catalog_root, errors) == OK);

		FSCompletenessManifest manifest = valid_manifest;
		manifest.anchors.write[0].expect["analysis"] = "maybe";
		CHECK(validate_manifest_vocabulary(manifest, catalog, errors) == ERR_INVALID_DATA);
		CHECK(completeness_errors_contain(errors,
				"anchor 'plain_static_member' expects unknown analysis outcome 'maybe'"));

		manifest = valid_manifest;
		manifest.required_dimensions.write[0].dimension = "diagnostic";
		manifest.anchors.write[0].expect["diagnostic"] = "accept";
		manifest.relations.write[0].derive["analysis"] = "maybe";
		manifest.exceptions.write[0].derive["runtime_obligation"] = "deferred";
		CHECK(validate_manifest_vocabulary(manifest, catalog, errors) == ERR_INVALID_DATA);
		CHECK(completeness_errors_contain(errors,
				"required dimension references unknown dimension 'diagnostic'"));
		CHECK(completeness_errors_contain(errors,
				"anchor 'plain_static_member' expects unknown dimension 'diagnostic'"));
		CHECK(completeness_errors_contain(errors,
				"relation 'unproven_to_union' derives unknown analysis outcome 'maybe'"));
		CHECK(completeness_errors_contain(errors,
				"exception 'reflective_rejection' derives unknown runtime_obligation outcome 'deferred'"));
	}

	TEST_CASE("TypeCompleteness Manifest vocabulary allows exceptions to preserve dimensions") {
		TemporaryProjectTree tree("type_completeness_vocabulary_exception_same");
		REQUIRE(tree.is_valid());
		FSCompletenessManifest manifest = load_representative_completeness_manifest(tree);
		manifest.exceptions.write[0].derive["runtime_obligation"] = "same";

		FSCompletenessCatalog catalog;
		Vector<String> errors;
		REQUIRE(catalog.load(type_completeness_catalog_root, errors) == OK);
		CHECK(validate_manifest_vocabulary(manifest, catalog, errors) == OK);
		CHECK(errors.is_empty());
	}

	TEST_CASE("TypeCompleteness Manifest catalog rejects duplicate axes and dimensions across files") {
		TemporaryProjectTree tree("type_completeness_catalog_duplicates");
		REQUIRE(tree.is_valid());
		tree.write_file("partitions/a.json",
				R"JSON({"schema_version":1,"axis":"shape","leaves":["plain"],"classes":{}})JSON");
		tree.write_file("partitions/b.json",
				R"JSON({"schema_version":1,"axis":"shape","leaves":["union"],"classes":{}})JSON");
		tree.write_file("dimensions/a.json",
				R"JSON({"schema_version":1,"adapter":"first","dimensions":[{"id":"analysis","outcomes":["accept"]}]})JSON");
		tree.write_file("dimensions/b.json",
				R"JSON({"schema_version":1,"adapter":"second","dimensions":[{"id":"analysis","outcomes":["reject"]}]})JSON");

		FSCompletenessCatalog catalog;
		Vector<String> errors;
		REQUIRE(catalog.load(type_completeness_catalog_root, errors) == OK);
		errors.push_back("stale error");
		CHECK(catalog.load(tree.root, errors) == ERR_INVALID_DATA);
		CHECK_FALSE(completeness_errors_contain(errors, "stale error"));
		CHECK(completeness_errors_contain_text(errors, "b.json: $.axis: duplicate axis 'shape'"));
		CHECK(completeness_errors_contain_text(errors, "b.json: $.dimensions[0].id: duplicate dimension id 'analysis'"));
		CHECK_FALSE(catalog.axis_has_leaf("source_proof", "variant"));
		CHECK_FALSE(catalog.axis_has_leaf("shape", "plain"));
	}

	TEST_CASE("TypeCompleteness Manifest catalog rejects undeclared and malformed class members") {
		TemporaryProjectTree tree("type_completeness_catalog_classes");
		REQUIRE(tree.is_valid());
		tree.write_file("partitions/shape.json", R"JSON({
  "schema_version": 1,
  "axis": "shape",
  "leaves": ["plain"],
  "classes": {
    "undeclared": ["union"],
    "duplicate": ["plain", "plain"],
    "empty": []
  }
})JSON");
		tree.write_file("dimensions/core.json",
				R"JSON({"schema_version":1,"adapter":"core","dimensions":[{"id":"analysis","outcomes":["accept"]}]})JSON");

		FSCompletenessCatalog catalog;
		Vector<String> errors;
		CHECK(catalog.load(tree.root, errors) == ERR_INVALID_DATA);
		CHECK(completeness_errors_contain_text(errors,
				"shape.json: $.classes.undeclared[0]: class member 'union' is not a declared leaf"));
		CHECK(completeness_errors_contain_text(errors,
				"shape.json: $.classes.duplicate[1]: duplicate value 'plain'"));
		CHECK(completeness_errors_contain_text(errors,
				"shape.json: $.classes.empty: must contain at least one string"));
	}

	TEST_CASE("TypeCompleteness Manifest catalog rejects malformed registry records") {
		TemporaryProjectTree tree("type_completeness_catalog_malformed");
		REQUIRE(tree.is_valid());
		tree.write_file("partitions/bad.json",
				R"JSON({"schema_version":2,"axis":7,"leaves":"plain","classes":[]})JSON");
		tree.write_file("dimensions/bad.json",
				R"JSON({"schema_version":"1","adapter":"","dimensions":{}})JSON");

		FSCompletenessCatalog catalog;
		Vector<String> errors;
		CHECK(catalog.load(tree.root, errors) == ERR_INVALID_DATA);
		CHECK(completeness_errors_contain_text(errors, "partitions/bad.json: $.schema_version: must equal 1"));
		CHECK(completeness_errors_contain_text(errors, "partitions/bad.json: $.axis: expected a string"));
		CHECK(completeness_errors_contain_text(errors, "partitions/bad.json: $.leaves: expected an array"));
		CHECK(completeness_errors_contain_text(errors, "partitions/bad.json: $.classes: expected an object"));
		CHECK(completeness_errors_contain_text(errors, "dimensions/bad.json: $.schema_version: expected an integer"));
		CHECK(completeness_errors_contain_text(errors, "dimensions/bad.json: $.adapter: must be non-empty"));
		CHECK(completeness_errors_contain_text(errors, "dimensions/bad.json: $.dimensions: expected an array"));
	}

	TEST_CASE("TypeCompleteness Manifest catalog rejects unknown registry fields deterministically") {
		TemporaryProjectTree tree("type_completeness_catalog_unknown_fields");
		REQUIRE(tree.is_valid());
		tree.write_file("partitions/z.json",
				R"JSON({"schema_version":1,"axis":"z_shape","leaves":["plain"],"classes":{},"z_typo":true})JSON");
		tree.write_file("partitions/a.json",
				R"JSON({"schema_version":1,"axis":"a_shape","leaves":["plain"],"classes":{},"a_typo":true})JSON");
		tree.write_file("dimensions/core.json", R"JSON({
  "schema_version": 1,
  "adapter": "core",
  "dimensions": [{"id": "analysis", "outcomes": ["accept"], "outcome": "accept"}],
  "dimensionz": []
})JSON");

		FSCompletenessCatalog catalog;
		Vector<String> errors;
		CHECK(catalog.load(tree.root, errors) == ERR_INVALID_DATA);
		CHECK_EQ(errors.size(), 4);
		if (errors.size() == 4) {
			CHECK(errors[0].contains("partitions/a.json: $.a_typo: unknown field 'a_typo'"));
			CHECK(errors[1].contains("partitions/z.json: $.z_typo: unknown field 'z_typo'"));
			CHECK(errors[2].contains("dimensions/core.json: $.dimensionz: unknown field 'dimensionz'"));
			CHECK(errors[3].contains(
					"dimensions/core.json: $.dimensions[0].outcome: unknown field 'outcome'"));
		}
	}

	TEST_CASE("TypeCompleteness Manifest catalog rejects duplicate JSON members and clears loaded state") {
		TemporaryProjectTree tree("type_completeness_catalog_duplicate_members");
		REQUIRE(tree.is_valid());
		tree.write_file("partitions/duplicate.json", R"JSON({
  "schema_version": 1,
  "axis": "shape",
  "a\u0078is": "other_shape",
  "leaves": ["plain"],
  "classes": {}
})JSON");
		tree.write_file("dimensions/duplicate.json", R"JSON({
  "schema_version": 1,
  "adapter": "core",
  "dimensions": [{
    "id": "analysis",
    "outcomes": ["accept"],
    "outco\u006des": ["reject"]
  }]
})JSON");

		FSCompletenessCatalog catalog;
		Vector<String> errors;
		REQUIRE(catalog.load(type_completeness_catalog_root, errors) == OK);
		REQUIRE(catalog.axis_has_leaf("source_proof", "variant"));
		REQUIRE(catalog.dimension_has_outcome("analysis", "accept"));

		CHECK(catalog.load(tree.root, errors) == ERR_INVALID_DATA);
		CHECK_EQ(errors.size(), 2);
		if (errors.size() == 2) {
			CHECK(errors[0].contains("partitions/duplicate.json: $.axis: duplicate object member 'axis'"));
			CHECK(errors[1].contains(
					"dimensions/duplicate.json: $.dimensions[0].outcomes: duplicate object member 'outcomes'"));
		}
		CHECK_FALSE(catalog.axis_has_leaf("source_proof", "variant"));
		CHECK_FALSE(catalog.dimension_has_outcome("analysis", "accept"));
		CHECK_FALSE(catalog.axis_has_leaf("other_shape", "plain"));
	}

	TEST_CASE("TypeCompleteness Manifest capability map loads checked-in ownership and applies precedence") {
		FSCompletenessCapabilityMap capability_map;
		Vector<String> errors;
		REQUIRE_MESSAGE(capability_map.load(type_completeness_capability_path, errors) == OK,
				String(" | ").join(errors));
		CHECK(errors.is_empty());

		const FSCompletenessSelection compiler =
				capability_map.select(Vector<String>({ "modules/foundry_script/fs_compiler.cpp" }));
		CHECK(selection_has_only_family(compiler, "union_destination_membership"));
		CHECK_FALSE(compiler.used_broad_core_fallback);
		CHECK(compiler.validation_errors.is_empty());

		const FSCompletenessSelection fallback =
				capability_map.select(Vector<String>({ "modules/foundry_script/new_type_surface.cpp" }));
		CHECK(selection_has_only_family(fallback, "union_destination_membership"));
		CHECK(fallback.used_broad_core_fallback);
		REQUIRE_EQ(fallback.validation_errors.size(), 1);
		CHECK_EQ(fallback.validation_errors[0],
				"unmapped production path: modules/foundry_script/new_type_surface.cpp");

		const FSCompletenessSelection ignored = capability_map.select(Vector<String>({
				"docs/type_completeness.md",
				"modules/foundry_script/tests/test_type_completeness_manifest.h",
		}));
		CHECK(ignored.families.is_empty());
		CHECK_FALSE(ignored.used_broad_core_fallback);
		CHECK(ignored.validation_errors.is_empty());

		const FSCompletenessSelection sibling =
				capability_map.select(Vector<String>({ "modules/foundry_script_extra/fs_compiler.cpp" }));
		CHECK(sibling.families.is_empty());
		CHECK_FALSE(sibling.used_broad_core_fallback);
		CHECK(sibling.validation_errors.is_empty());
	}

	TEST_CASE("TypeCompleteness Manifest capability matching is component-aware and unions changed paths") {
		TemporaryProjectTree tree("type_completeness_capability_matching");
		REQUIRE(tree.is_valid());
		const String path = write_completeness_capability_map(tree, R"JSON({
  "schema_version": 1,
	  "production": [
	    {"paths": ["modules/foundry_script/exact.cpp"], "families": ["exact_family"]},
	    {"paths": ["modules/foundry_script/editor/"], "families": ["editor_family"]},
	    {"paths": ["modules/foundry_script/shared/"], "families": ["shared_family"]},
	    {"paths": ["modules/foundry_script/tests/owned.cpp"], "families": ["owned_test_family"]},
	    {"paths": ["modules/foundry_script/tests/owned_cases/"], "families": ["owned_case_family"]}
  ],
  "nonproduction_prefixes": ["docs/", "modules/foundry_script/tests/"],
  "broad_core_families": ["broad_family"]
})JSON");

		FSCompletenessCapabilityMap capability_map;
		Vector<String> errors;
		REQUIRE_MESSAGE(capability_map.load(path, errors) == OK, String(" | ").join(errors));

		const FSCompletenessSelection selection = capability_map.select(Vector<String>({
				"modules/foundry_script/editor/tool.cpp",
				"modules/foundry_script/exact.cpp",
				"modules/foundry_script/editor/tool.cpp",
		}));
		CHECK_EQ(selection.families.size(), 2);
		CHECK(selection.families.has("exact_family"));
		CHECK(selection.families.has("editor_family"));
		CHECK_FALSE(selection.used_broad_core_fallback);
		CHECK(selection.validation_errors.is_empty());

		const FSCompletenessSelection reversed = capability_map.select(Vector<String>({
				"modules/foundry_script/exact.cpp",
				"modules/foundry_script/editor/tool.cpp",
		}));
		CHECK_EQ(reversed.families, selection.families);
		CHECK_EQ(reversed.validation_errors, selection.validation_errors);

		CHECK(selection_has_only_family(capability_map.select(
												Vector<String>({ "modules/foundry_script/shared/special.cpp" })),
				"shared_family"));
		CHECK(selection_has_only_family(capability_map.select(
												Vector<String>({ "modules/foundry_script/tests/owned.cpp" })),
				"owned_test_family"));
		CHECK(selection_has_only_family(capability_map.select(
												Vector<String>({ "modules/foundry_script/tests/owned_cases/case.fs" })),
				"owned_case_family"));
		CHECK(capability_map.select(Vector<String>({ "modules/foundry_script/exact.cpp/child" }))
						.used_broad_core_fallback);
		CHECK(capability_map.select(Vector<String>({ "modules/foundry_script/editorial/tool.cpp" }))
						.used_broad_core_fallback);
		CHECK(capability_map.select(Vector<String>({ "modules/foundry_script/tests/runner.cpp" }))
						.families.is_empty());
		CHECK(capability_map.select(Vector<String>({ "modules/foundry_script/tests_extra/runner.cpp" }))
						.used_broad_core_fallback);
	}

	TEST_CASE("TypeCompleteness Manifest capability loader rejects malformed schema and ambiguous ownership") {
		struct InvalidCapabilityMap {
			const char *name;
			const char *json;
			const char *diagnostic;
		};
		const InvalidCapabilityMap cases[] = {
			{ "unknown", R"JSON({
  "schema_version":1,
  "production":[],
  "nonproduction_prefixes":["docs/"],
  "broad_core_families":["core"],
  "typo":true
})JSON",
					"$.typo: unknown field 'typo'" },
			{ "duplicate_raw", R"JSON({
  "schema_version":1,
  "production":[],
  "production":[],
  "nonproduction_prefixes":["docs/"],
  "broad_core_families":["core"]
})JSON",
					"$.production: duplicate object member 'production'" },
			{ "duplicate_array", R"JSON({
  "schema_version":1,
  "production":[{"paths":["modules/foundry_script/a.cpp","modules/foundry_script/a.cpp"],"families":["a"]}],
  "nonproduction_prefixes":["docs/","docs/"],
  "broad_core_families":["core","core"]
})JSON",
					"duplicate value" },
			{ "invalid_paths", R"JSON({
  "schema_version":1,
  "production":[{"paths":["/absolute.cpp","scheme://file","a\\b","a/./b","a//b",""] ,"families":[""]}],
  "nonproduction_prefixes":["../docs/"],
  "broad_core_families":[]
})JSON",
					"must be a normalized repository-relative path" },
			{ "ambiguous", R"JSON({
  "schema_version":1,
  "production":[
    {"paths":["modules/foundry_script/editor/"],"families":["editor"]},
    {"paths":["modules/foundry_script/editor/tool.cpp"],"families":["tool"]}
  ],
  "nonproduction_prefixes":["docs/"],
  "broad_core_families":["core"]
	})JSON",
					"ambiguous production ownership" },
			{ "same_record_overlap", R"JSON({
  "schema_version":1,
  "production":[{
    "paths":["modules/foundry_script/shared/","modules/foundry_script/shared/special.cpp"],
    "families":["shared"]
  }],
  "nonproduction_prefixes":["docs/"],
  "broad_core_families":["core"]
})JSON",
					"ambiguous production ownership" },
			{ "cross_category", R"JSON({
  "schema_version":1,
  "production":[{"paths":["modules/foundry_script/tests/"],"families":["owned"]}],
  "nonproduction_prefixes":["modules/foundry_script/tests/"],
  "broad_core_families":["core"]
	})JSON",
					"overlaps nonproduction path" },
			{ "cross_category_reverse", R"JSON({
  "schema_version":1,
  "production":[{"paths":["modules/foundry_script/tests/fixtures/"],"families":["owned"]}],
  "nonproduction_prefixes":["modules/foundry_script/tests/fixtures/case.fs"],
  "broad_core_families":["core"]
})JSON",
					"overlaps nonproduction path" },
			{ "cross_category_production_ancestor", R"JSON({
  "schema_version":1,
  "production":[{"paths":["modules/foundry_script/tests/"],"families":["owned"]}],
  "nonproduction_prefixes":["modules/foundry_script/tests/fixtures/"],
  "broad_core_families":["core"]
})JSON",
					"overlaps nonproduction path" },
			{ "unsafe_family", R"JSON({
  "schema_version":1,
  "production":[{"paths":["modules/foundry_script/a.cpp"],"families":["../unsafe"]}],
  "nonproduction_prefixes":["docs/"],
  "broad_core_families":["core.json"]
})JSON",
					"safe filename-stem family id" },
		};

		for (const InvalidCapabilityMap &test_case : cases) {
			CAPTURE(test_case.name);
			TemporaryProjectTree tree(vformat("type_completeness_capability_invalid_%s", test_case.name));
			REQUIRE(tree.is_valid());
			FSCompletenessCapabilityMap capability_map;
			Vector<String> errors;
			CHECK_EQ(capability_map.load(write_completeness_capability_map(tree, test_case.json), errors),
					ERR_INVALID_DATA);
			CHECK(completeness_errors_contain_text(errors, test_case.diagnostic));
		}
	}

	TEST_CASE("TypeCompleteness Manifest capability loader confines nonproduction ownership to tests") {
		const char *invalid_prefixes[] = {
			"modules/",
			"modules/foundry_script",
			"modules/foundry_script/",
			"modules/foundry_script/editor/",
			"modules/foundry_script/fs_analyzer.cpp",
			"modules/foundry_script/tests_extra/",
		};
		for (const char *prefix : invalid_prefixes) {
			CAPTURE(prefix);
			TemporaryProjectTree tree(vformat("type_completeness_nonproduction_invalid_%s",
					String(prefix).sha256_text().substr(0, 8)));
			REQUIRE(tree.is_valid());
			const String contents = vformat(R"JSON({
  "schema_version":1,
  "production":[{"paths":["modules/foundry_script/fs_compiler.cpp"],"families":["mapped"]}],
  "nonproduction_prefixes":["%s"],
  "broad_core_families":["broad"]
})JSON",
					prefix);
			FSCompletenessCapabilityMap capability_map;
			Vector<String> errors;
			CHECK_EQ(capability_map.load(write_completeness_capability_map(tree, contents), errors),
					ERR_INVALID_DATA);
			CHECK(completeness_errors_contain_text(errors, "may exempt Foundry Script production core"));
		}
	}

	TEST_CASE("TypeCompleteness Manifest capability loader permits only component-aware test exclusions") {
		struct PermittedExclusion {
			const char *prefix;
			const char *ignored_path;
		};
		const PermittedExclusion cases[] = {
			{ "docs/", "docs/type_completeness.md" },
			{ "modules/foundry_script/tests/", "modules/foundry_script/tests/runner.cpp" },
			{ "modules/foundry_script/tests/owned.cpp", "modules/foundry_script/tests/owned.cpp" },
			{ "modules/foundry_script/tests/fixtures/", "modules/foundry_script/tests/fixtures/case.fs" },
		};
		for (const PermittedExclusion &test_case : cases) {
			CAPTURE(test_case.prefix);
			TemporaryProjectTree tree(vformat("type_completeness_nonproduction_valid_%s",
					String(test_case.prefix).sha256_text().substr(0, 8)));
			REQUIRE(tree.is_valid());
			const String contents = vformat(R"JSON({
  "schema_version":1,
  "production":[{"paths":["modules/foundry_script/fs_compiler.cpp"],"families":["mapped"]}],
  "nonproduction_prefixes":["%s"],
  "broad_core_families":["broad"]
})JSON",
					test_case.prefix);
			FSCompletenessCapabilityMap capability_map;
			Vector<String> errors;
			REQUIRE_MESSAGE(capability_map.load(write_completeness_capability_map(tree, contents), errors) == OK,
					String(" | ").join(errors));
			const FSCompletenessSelection ignored =
					capability_map.select(Vector<String>({ test_case.ignored_path }));
			CHECK(ignored.families.is_empty());
			CHECK_FALSE(ignored.used_broad_core_fallback);
			CHECK(ignored.validation_errors.is_empty());
		}
	}

	TEST_CASE("TypeCompleteness Manifest capability reload clears state and invalid changes fail closed") {
		TemporaryProjectTree tree("type_completeness_capability_reload");
		REQUIRE(tree.is_valid());
		FSCompletenessCapabilityMap capability_map;
		Vector<String> errors;
		const String valid_path = write_completeness_capability_map(tree, R"JSON({
  "schema_version":1,
  "production":[{"paths":["modules/foundry_script/exact.cpp"],"families":["exact"]}],
  "nonproduction_prefixes":["modules/foundry_script/tests/"],
  "broad_core_families":["broad"]
})JSON");
		REQUIRE(capability_map.load(valid_path, errors) == OK);
		REQUIRE(selection_has_only_family(
				capability_map.select(Vector<String>({ "modules/foundry_script/exact.cpp" })), "exact"));

		tree.write_file("invalid.json", "[]");
		errors.push_back("stale error");
		CHECK_EQ(capability_map.load(tree.root.path_join("invalid.json"), errors), ERR_INVALID_DATA);
		CHECK_FALSE(completeness_errors_contain(errors, "stale error"));
		const FSCompletenessSelection cleared =
				capability_map.select(Vector<String>({ "modules/foundry_script/exact.cpp" }));
		CHECK(cleared.families.is_empty());
		CHECK(cleared.used_broad_core_fallback);

		const FSCompletenessSelection invalid = capability_map.select(Vector<String>({
				"modules/foundry_script/../outside.cpp",
				"modules\\foundry_script\\fs_compiler.cpp",
				"/modules/foundry_script/fs_compiler.cpp",
		}));
		CHECK(invalid.used_broad_core_fallback);
		CHECK_GE(invalid.validation_errors.size(), 3);
	}

	TEST_CASE("TypeCompleteness Manifest capability validation checks family reachability without mutation") {
		TemporaryProjectTree tree("type_completeness_capability_rule_coverage");
		REQUIRE(tree.is_valid());
		const String map_path = write_completeness_capability_map(tree, R"JSON({
  "schema_version":1,
  "production":[{"paths":["modules/foundry_script/mapped.cpp"],"families":["mapped"]}],
  "nonproduction_prefixes":["docs/"],
  "broad_core_families":["broad"]
})JSON");
		tree.write_file("rules/broad.json", make_completeness_rule("broad"));
		tree.write_file("rules/unreachable.json", make_completeness_rule("unreachable"));

		FSCompletenessCapabilityMap capability_map;
		Vector<String> errors;
		REQUIRE(capability_map.load(map_path, errors) == OK);
		CHECK_EQ(capability_map.validate_against_rule_directory(tree.root.path_join("rules"), errors),
				ERR_INVALID_DATA);
		CHECK(completeness_errors_contain_text(errors, "mapped family 'mapped' has no rule manifest"));
		CHECK(completeness_errors_contain_text(errors, "rule family 'broad' is unreachable"));
		CHECK(completeness_errors_contain_text(errors, "rule family 'unreachable' is unreachable"));
		CHECK(selection_has_only_family(
				capability_map.select(Vector<String>({ "modules/foundry_script/mapped.cpp" })), "mapped"));
	}

	TEST_CASE("TypeCompleteness Manifest capability validation parses rules and rejects collisions") {
		TemporaryProjectTree tree("type_completeness_capability_rule_integrity");
		REQUIRE(tree.is_valid());
		const String map_path = write_completeness_capability_map(tree, R"JSON({
  "schema_version":1,
  "production":[{"paths":["modules/foundry_script/mapped.cpp"],"families":["mapped"]}],
  "nonproduction_prefixes":["docs/"],
  "broad_core_families":["mapped"]
})JSON");
		tree.write_file("rules/a.json", make_completeness_rule("mapped"));
		tree.write_file("rules/b.json", make_completeness_rule("mapped"));
		tree.write_file("rules/malformed.json", "{not json");
		tree.write_file("rules/not-json.txt", "unexpected");
		tree.write_file("rules/.hidden.json", make_completeness_rule("hidden"));
		tree.write_file("rules/unknown_rule.json", R"JSON({
  "schema_version":1,
  "family":"unknown_rule",
  "domain":{"shape":["plain"]},
  "required_dimensions":[],
  "anchors":[],
  "relations":[],
  "exceptions":[],
  "unknown":true
})JSON");
		tree.write_file("rules/nested/rule.json", make_completeness_rule("mapped"));
#ifdef UNIX_ENABLED
		TemporaryProjectTree neighbor("type_completeness_capability_rule_link_target");
		REQUIRE(neighbor.is_valid());
		neighbor.write_file("linked.json", make_completeness_rule("mapped"));
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(filesystem.is_valid());
		REQUIRE_EQ(filesystem->create_link(
						   neighbor.root.path_join("linked.json"), tree.root.path_join("rules/linked.json")),
				OK);
#endif

		FSCompletenessCapabilityMap capability_map;
		Vector<String> errors;
		REQUIRE(capability_map.load(map_path, errors) == OK);
		CHECK_EQ(capability_map.validate_against_rule_directory(tree.root.path_join("rules"), errors),
				ERR_INVALID_DATA);
		CHECK(completeness_errors_contain_text(errors, "duplicate rule family 'mapped'"));
		CHECK(completeness_errors_contain_text(errors, "malformed.json"));
		CHECK(completeness_errors_contain_text(errors, "unexpected non-JSON entry 'not-json.txt'"));
		CHECK(completeness_errors_contain_text(errors, "unexpected hidden entry '.hidden.json'"));
		CHECK(completeness_errors_contain_text(errors, "unexpected directory entry 'nested'"));
		CHECK(completeness_errors_contain_text(errors, "unknown_rule.json: $.unknown: unknown field 'unknown'"));
		CHECK(completeness_errors_contain_text(errors, "filename stem must equal rule family 'mapped'"));
#ifdef UNIX_ENABLED
		CHECK(completeness_errors_contain_text(errors, "linked rule entry 'linked.json' is not allowed"));
#endif
	}

	TEST_CASE("TypeCompleteness Manifest capability validation requires a lowercase json extension") {
		TemporaryProjectTree tree("type_completeness_capability_rule_extension");
		REQUIRE(tree.is_valid());
		const String map_path = write_completeness_capability_map(tree, R"JSON({
  "schema_version":1,
  "production":[{"paths":["modules/foundry_script/mapped.cpp"],"families":["mapped"]}],
  "nonproduction_prefixes":["docs/"],
  "broad_core_families":["mapped"]
})JSON");
		tree.write_file("rules/mapped.JSON", make_completeness_rule("mapped"));

		FSCompletenessCapabilityMap capability_map;
		Vector<String> errors;
		REQUIRE(capability_map.load(map_path, errors) == OK);
		CHECK_EQ(capability_map.validate_against_rule_directory(tree.root.path_join("rules"), errors),
				ERR_INVALID_DATA);
		CHECK(completeness_errors_contain_text(errors, "unexpected non-JSON entry 'mapped.JSON'"));
		CHECK(completeness_errors_contain_text(errors, "mapped family 'mapped' has no rule manifest"));
	}

	TEST_CASE("TypeCompleteness Manifest checked-in capability families have exactly one reachable rule") {
		FSCompletenessCapabilityMap capability_map;
		Vector<String> errors;
		REQUIRE(capability_map.load(type_completeness_capability_path, errors) == OK);
		CHECK_EQ(capability_map.validate_against_rule_directory(
						 String(type_completeness_catalog_root).path_join("rules"), errors),
				OK);
		CHECK_MESSAGE(errors.is_empty(), String(" | ").join(errors));
	}
}

} // namespace FSTests
