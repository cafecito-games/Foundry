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
		Vector<String> errors;
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
}

} // namespace FSTests
