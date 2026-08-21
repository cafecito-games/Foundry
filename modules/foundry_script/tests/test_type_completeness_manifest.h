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
				"relation 'unproven_to_union' to selector for axis 'source_proof' uses unknown leaf 'not_registered'"));

		manifest = valid_manifest;
		Dictionary extra_field;
		extra_field["class"] = "unproven";
		extra_field["except"] = "variant";
		manifest.required_dimensions.write[1].when["source_proof"] = extra_field;
		CHECK(validate_manifest_vocabulary(manifest, catalog, errors) == ERR_INVALID_DATA);
		CHECK(completeness_errors_contain(errors,
				"required dimension 'runtime_obligation' when selector for axis 'source_proof' must be a string leaf or an object containing only 'class'"));
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
		manifest.exceptions.write[0].derive["runtime_obligation"] = "same";
		CHECK(validate_manifest_vocabulary(manifest, catalog, errors) == ERR_INVALID_DATA);
		CHECK(completeness_errors_contain(errors,
				"required dimension references unknown dimension 'diagnostic'"));
		CHECK(completeness_errors_contain(errors,
				"anchor 'plain_static_member' expects unknown dimension 'diagnostic'"));
		CHECK(completeness_errors_contain(errors,
				"relation 'unproven_to_union' derives unknown analysis outcome 'maybe'"));
		CHECK(completeness_errors_contain(errors,
				"exception 'reflective_rejection' derives unknown runtime_obligation outcome 'same'"));
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
}

} // namespace FSTests
