# Foundry Script Type-Completeness Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the executable manifest, bounded-derivation validator, union-membership pilot, stable case IDs, and
structured report that prove the completeness design against one real Foundry Script rule family.

**Architecture:** Test-only C++ loads source-controlled JSON catalogs into typed records, validates a finite semantic
domain, and derives each required observable through simple relation chains of at most three steps. A dedicated union
adapter renders and executes the pilot on the analyzer, text runtime, and compiled-bytecode runtime, while a common
runner emits stable case-level JSON for later CI and ledger automation.

**Tech Stack:** C++17, the engine's `Variant`/`Dictionary` JSON support, Foundry Script parser/analyzer/compiler,
`FSTestRunner`, doctest, SHA-256 case IDs, SCons/Ninja through `scripts/agent_build.py`.

**Design:** `docs/superpowers/specs/2026-08-20-foundry-script-type-system-completeness-design.md`

---

## Scope Boundary

This plan implements the shared Phase 1 foundation and the worked union destination rule. It deliberately leaves
three independently deployable systems for follow-on plans:

- `develop` comparison, provisional GitHub records, reviewed bot-ledger PRs, and deadline enforcement;
- historical PR/issue mining plus scheduled mutation recipes;
- the representation census and the broader semantic, lifecycle, editor, and LSP workstreams.

Do not let presubmit treat a `develop` mismatch as non-blocking after this plan. That policy remains disabled until the
separate comparator/deadline plan is implemented. This slice is complete when the strict test build can validate and
execute the pilot end to end and emit a machine-readable report.

## File Structure

- Create `modules/foundry_script/tests/fs_type_completeness_manifest.h`: typed catalog, predicate, anchor, relation,
  exception, and dimension declarations.
- Create `modules/foundry_script/tests/fs_type_completeness_manifest.cpp`: JSON loading and structural validation.
- Create `modules/foundry_script/tests/fs_type_completeness_graph.h`: cells, provenance, resolved expectations, and
  graph-validation API.
- Create `modules/foundry_script/tests/fs_type_completeness_graph.cpp`: Cartesian expansion, frame-preserving relation
  application, specificity, simple-chain enumeration, and dimension coverage.
- Create `modules/foundry_script/tests/fs_type_completeness_case_id.h`: canonical coordinate serialization, SHA-256
  IDs, and migration resolution.
- Create `modules/foundry_script/tests/fs_type_completeness_case_id.cpp`: stable-ID and alias validation logic.
- Create `modules/foundry_script/tests/fs_type_completeness_union_adapter.h`: pilot source-rendering and adapter API.
- Create `modules/foundry_script/tests/fs_type_completeness_union_adapter.cpp`: analyzer, text, bytecode, runtime-check,
  and carrier observations for the worked family.
- Create `modules/foundry_script/tests/fs_type_completeness_runner.h`: adapter result, finding, report, and runner API.
- Create `modules/foundry_script/tests/fs_type_completeness_runner.cpp`: execution, expectation comparison, ledger
  reconciliation, and JSON report writing.
- Create `modules/foundry_script/tests/test_type_completeness_manifest.h`: loader and vocabulary behavior tests.
- Create `modules/foundry_script/tests/test_type_completeness_graph.h`: derivation, ambiguity, coverage, and ID tests.
- Create `modules/foundry_script/tests/test_type_completeness_union_pilot.h`: real analyzer/runtime/bytecode acceptance.
- Create `modules/foundry_script/tests/type_completeness/partitions/*.json`: normalized pilot axes and classes.
- Create `modules/foundry_script/tests/type_completeness/dimensions/core.json`: observable value vocabularies; each
  family manifest supplies its coordinate-specific requiredness predicates.
- Create `modules/foundry_script/tests/type_completeness/rules/union_destination_membership.json`: approved worked
  rule.
- Create `modules/foundry_script/tests/type_completeness/migrations/v1.json`: initial empty ID-migration set.
- Create `modules/foundry_script/tests/type_completeness/findings/.gitkeep`: empty per-finding ledger directory.
- Create `modules/foundry_script/tests/type_completeness/capabilities.json`: production-path-to-family ownership.
- Do not edit generated `modules/modules_tests.gen.h`; SCons discovers module test headers automatically.

## Task 1: Lock the Typed Manifest Boundary

**Files:**

- Create: `modules/foundry_script/tests/fs_type_completeness_manifest.h`
- Create: `modules/foundry_script/tests/fs_type_completeness_manifest.cpp`
- Create: `modules/foundry_script/tests/test_type_completeness_manifest.h`

- [ ] **Step 1: Write the failing JSON loader tests**

Create the test header with the standard license, `#pragma once`, and a `[TypeCompleteness][Manifest]` suite. Write
documents under `TemporaryProjectTree`; assert on parsed records and errors, never on another source file's text.

```cpp
#include "fs_temporary_project_tree.h"
#include "fs_type_completeness_manifest.h"

#include "core/io/file_access.h"
#include "core/os/os.h"
#include "tests/test_macros.h"

namespace FSTests {

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness][Manifest]") {
TEST_CASE("A manifest loads typed domain and rule records") {
	TemporaryProjectTree tree(vformat("type_completeness_manifest_%d",
			OS::get_singleton()->get_process_id()));
	REQUIRE(tree.is_valid());
	tree.write_file("rule.json", R"({
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
})");
	const String path = tree.root.path_join("rule.json");

	FSCompletenessManifest manifest;
	Vector<String> errors;
	CHECK(FSCompletenessManifest::load(path, manifest, errors) == OK);
	CHECK(errors.is_empty());
	CHECK_EQ(manifest.family, "sample");
	CHECK_EQ(manifest.domain.size(), 2);
	CHECK_EQ(manifest.anchors.size(), 1);
	CHECK_EQ(manifest.relations.size(), 1);
}

TEST_CASE("Malformed and duplicate records are rejected together") {
	TemporaryProjectTree tree(vformat("type_completeness_manifest_errors_%d",
			OS::get_singleton()->get_process_id()));
	REQUIRE(tree.is_valid());
	tree.write_file("rule.json", R"({
  "schema_version": 1,
  "family": "sample",
  "domain": {"shape": ["plain", "plain"]},
  "required_dimensions": [],
  "anchors": [{"id": "same"}, {"id": "same"}],
  "relations": [],
  "exceptions": []
})");
	const String path = tree.root.path_join("rule.json");

	FSCompletenessManifest manifest;
	Vector<String> errors;
	CHECK(FSCompletenessManifest::load(path, manifest, errors) == ERR_INVALID_DATA);
	CHECK(errors.size() >= 3);
}
} // TEST_SUITE

} // namespace FSTests
```

- [ ] **Step 2: Build to verify the loader tests fail**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypeCompleteness*Manifest*"
```

Expected: compilation fails because `fs_type_completeness_manifest.h` does not exist.

- [ ] **Step 3: Add the typed declarations**

Create `fs_type_completeness_manifest.h` with the standard license and these public records. Keep JSON `Dictionary`
objects only at the coordinate/predicate boundary; identifiers and collections are typed.

```cpp
#pragma once

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
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

} // namespace FSTests
```

- [ ] **Step 4: Implement fail-closed parsing**

In `fs_type_completeness_manifest.cpp`, parse with `JSON`, require an object root and `schema_version == 1`, and use
small helpers named `require_string`, `require_dictionary`, `require_array`, and `append_unique_id`. Each helper
appends `"<path>: <reason>"` to the error vector rather than returning after the first bad field. Implement these
exact validations:

```cpp
// Root: schema_version, family, domain, required_dimensions, anchors, relations, exceptions.
// Domain: at least one axis; each axis has a non-empty array of unique non-empty strings.
// Required dimensions: non-empty `dimension`, Dictionary `when`, no duplicate (dimension, when) record.
// Anchors: unique non-empty id, concrete Dictionary coordinates, non-empty Dictionary expect.
// Relations: unique non-empty id, non-empty Dictionary from/to/derive.
// Exceptions: unique non-empty id across every record kind, existing parent relation, non-empty rationale,
//             non-empty positive and boundary witness arrays, non-empty Dictionary derive.
// Chain bound: optional max_chain_length in [1, 3], default 3.
```

Preserve domain-axis insertion order in `domain_axis_order`, but do not use it for case IDs. Return `OK` only when
`r_errors` is empty; otherwise clear the partially loaded manifest and return `ERR_INVALID_DATA`.

- [ ] **Step 5: Run the focused loader tests**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypeCompleteness*Manifest*"
```

Expected: both selected cases pass.

- [ ] **Step 6: Commit the manifest boundary**

```sh
git add modules/foundry_script/tests/fs_type_completeness_manifest.h \
  modules/foundry_script/tests/fs_type_completeness_manifest.cpp \
  modules/foundry_script/tests/test_type_completeness_manifest.h
git commit -m "Add type completeness manifest loader"
```

## Task 2: Add Shared Partitions and Dimension Registries

**Files:**

- Modify: `modules/foundry_script/tests/fs_type_completeness_manifest.h`
- Modify: `modules/foundry_script/tests/fs_type_completeness_manifest.cpp`
- Modify: `modules/foundry_script/tests/test_type_completeness_manifest.h`
- Create: `modules/foundry_script/tests/type_completeness/partitions/destination.json`
- Create: `modules/foundry_script/tests/type_completeness/partitions/source_proof.json`
- Create: `modules/foundry_script/tests/type_completeness/partitions/boundary.json`
- Create: `modules/foundry_script/tests/type_completeness/partitions/surface.json`
- Create: `modules/foundry_script/tests/type_completeness/dimensions/core.json`

- [ ] **Step 1: Write failing vocabulary-validation cases**

Add tests that load a registry plus a manifest and observe these outcomes:

```cpp
CHECK(catalog.load(root, errors) == OK);
CHECK(catalog.axis_has_leaf("source_proof", "numeric_constant"));
CHECK(catalog.class_contains("source_proof", "unproven", "variant"));
CHECK(catalog.dimension_has_outcome("runtime_obligation", "union_membership_check"));

FSCompletenessManifest unknown_leaf = valid_manifest;
unknown_leaf.anchors.write[0].coordinates["boundary"] = "parameter";
errors.clear();
CHECK(validate_manifest_vocabulary(unknown_leaf, catalog, errors) == ERR_INVALID_DATA);
CHECK(errors.has("anchor 'plain_static_member' uses unknown boundary leaf 'parameter'"));

FSCompletenessManifest unknown_class = valid_manifest;
Dictionary selector;
selector["class"] = "not_registered";
unknown_class.relations.write[0].from["source_proof"] = selector;
errors.clear();
CHECK(validate_manifest_vocabulary(unknown_class, catalog, errors) == ERR_INVALID_DATA);

FSCompletenessManifest unknown_outcome = valid_manifest;
unknown_outcome.anchors.write[0].expect["analysis"] = "maybe";
errors.clear();
CHECK(validate_manifest_vocabulary(unknown_outcome, catalog, errors) == ERR_INVALID_DATA);
```

Load `valid_manifest` from the checked-in worked rule through `FSCompletenessManifest::load`; do not inspect its JSON
as text.

- [ ] **Step 2: Run the test and verify the missing-registry failure**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypeCompleteness*Manifest*"
```

Expected: compilation fails because `FSCompletenessCatalog` is not declared.

- [ ] **Step 3: Add the catalog API and validation**

Add these records and functions to the manifest header:

```cpp
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

class FSCompletenessCatalog {
	HashMap<String, FSCompletenessPartition> partitions;
	HashMap<String, FSCompletenessDimension> dimensions;

public:
	Error load(const String &p_root, Vector<String> &r_errors);
	bool axis_has_leaf(const String &p_axis, const String &p_leaf) const;
	bool class_contains(const String &p_axis, const String &p_class, const String &p_leaf) const;
	bool dimension_has_outcome(const String &p_dimension, const String &p_outcome) const;
};

Error validate_manifest_vocabulary(const FSCompletenessManifest &p_manifest,
		const FSCompletenessCatalog &p_catalog, Vector<String> &r_errors);
```

The catalog loader sorts filenames before parsing, rejects duplicate axes/dimensions, proves every class member is a
declared leaf, and rejects a manifest predicate object unless its only field is `"class"`. Vocabulary validation
checks every domain leaf, concrete coordinate, predicate leaf/class, dimension name, and non-`same` outcome.

- [ ] **Step 4: Add the initial registry JSON**

Use `schema_version: 1` in every file. The exact leaf sets are:

```json
{"schema_version":1,"axis":"destination","leaves":["plain","union"],"classes":{}}
{
  "schema_version": 1,
  "axis": "source_proof",
  "leaves": ["static_member", "numeric_constant", "gradual", "erased", "variant"],
  "classes": {"unproven": ["gradual", "erased", "variant"]}
}
{"schema_version":1,"axis":"boundary","leaves":["argument_binding","reflective_write"],"classes":{}}
{"schema_version":1,"axis":"surface","leaves":["text","bytecode"],"classes":{}}
```

`dimensions/core.json` must declare these outcome vocabularies:

```json
{
  "schema_version": 1,
  "adapter": "core",
  "dimensions": [
    {"id":"analysis","outcomes":["accept","reject"]},
    {"id":"runtime_obligation","outcomes":["typed_destination_check","union_membership_check"]},
    {"id":"stored_carrier","outcomes":["plain_destination","admitting_alternative"]}
  ]
}
```

- [ ] **Step 5: Run manifest tests and commit**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypeCompleteness*Manifest*"
git add modules/foundry_script/tests/fs_type_completeness_manifest.* \
  modules/foundry_script/tests/test_type_completeness_manifest.h \
  modules/foundry_script/tests/type_completeness/partitions \
  modules/foundry_script/tests/type_completeness/dimensions
git commit -m "Define type completeness vocabularies"
```

Expected: all manifest cases pass before the commit.

## Task 3: Validate Bounded Derivation and Required Dimensions

**Files:**

- Create: `modules/foundry_script/tests/fs_type_completeness_graph.h`
- Create: `modules/foundry_script/tests/fs_type_completeness_graph.cpp`
- Create: `modules/foundry_script/tests/test_type_completeness_graph.h`
- Create: `modules/foundry_script/tests/type_completeness/rules/union_destination_membership.json`

- [ ] **Step 1: Write the failing graph acceptance test**

Load the checked-in registry and worked rule through their public APIs. Assert on resolved semantic data, not JSON
formatting:

```cpp
#include "fs_type_completeness_graph.h"
#include "fs_type_completeness_manifest.h"
#include "tests/test_macros.h"

namespace FSTests {

static const String completeness_root =
		"modules/foundry_script/tests/type_completeness";

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness][Graph]") {
TEST_CASE("The union pilot resolves every required dimension") {
	FSCompletenessCatalog catalog;
	FSCompletenessManifest manifest;
	Vector<String> errors;
	REQUIRE(catalog.load(completeness_root, errors) == OK);
	REQUIRE(FSCompletenessManifest::load(
			completeness_root.path_join("rules/union_destination_membership.json"),
			manifest, errors) == OK);
	REQUIRE(validate_manifest_vocabulary(manifest, catalog, errors) == OK);

	FSCompletenessResolution resolution;
	CHECK(FSCompletenessGraph::resolve(manifest, catalog, resolution, errors) == OK);
	CHECK(errors.is_empty());
	CHECK_EQ(resolution.cells.size(), 40);
	CHECK_EQ(resolution.max_observed_chain_length, 3);
	CHECK_EQ(resolution.uncovered_dimension_count, 0);
	CHECK_EQ(resolution.ambiguous_dimension_count, 0);
}
} // TEST_SUITE

} // namespace FSTests
```

- [ ] **Step 2: Add negative graph cases before implementation**

Load the valid pilot once, copy its typed manifest per subcase, and mutate records in memory. Add this error helper:

```cpp
static bool completeness_errors_contain(const Vector<String> &p_errors, const String &p_needle) {
	for (const String &error : p_errors) {
		if (error.contains(p_needle)) {
			return true;
		}
	}
	return false;
}
```

Create one subcase for each mutation:

```cpp
SUBCASE("No anchor can reach most cells") {
	FSCompletenessManifest invalid = manifest;
	invalid.anchors.clear();
	CHECK(FSCompletenessGraph::resolve(invalid, catalog, resolution, errors) == ERR_INVALID_DATA);
	CHECK(completeness_errors_contain(errors, "has no reachable disposition"));
}

SUBCASE("A fixed dimension transform cannot be omitted") {
	FSCompletenessManifest invalid = manifest;
	for (FSCompletenessRelation &relation : invalid.relations) {
		if (relation.id == "plain_numeric_constant_parity") {
			relation.derive.erase("stored_carrier");
		}
	}
	CHECK(FSCompletenessGraph::resolve(invalid, catalog, resolution, errors) == ERR_INVALID_DATA);
	CHECK(completeness_errors_contain(errors, "stored_carrier"));
}

SUBCASE("A relation cannot repeat or revisit its target") {
	FSCompletenessManifest invalid = manifest;
	FSCompletenessRelation loop;
	loop.id = "unproven_to_erased_loop";
	Dictionary unproven;
	unproven["class"] = "unproven";
	loop.from["source_proof"] = unproven;
	loop.to["source_proof"] = "erased";
	loop.derive["analysis"] = "same";
	invalid.relations.push_back(loop);
	CHECK(FSCompletenessGraph::resolve(invalid, catalog, resolution, errors) == ERR_INVALID_DATA);
	CHECK(completeness_errors_contain(errors, "revisit") ||
			completeness_errors_contain(errors, "repeat"));
}

SUBCASE("Agreeing cells cannot carry conflicting outcomes") {
	FSCompletenessManifest invalid = manifest;
	FSCompletenessRelation conflict;
	conflict.id = "conflicting_plain_boundary";
	conflict.from["destination"] = "plain";
	conflict.from["boundary"] = "argument_binding";
	conflict.to["boundary"] = "reflective_write";
	conflict.derive["analysis"] = "reject";
	invalid.relations.push_back(conflict);
	CHECK(FSCompletenessGraph::resolve(invalid, catalog, resolution, errors) == ERR_INVALID_DATA);
	CHECK(completeness_errors_contain(errors, "incompatible outcomes"));
}

SUBCASE("Incomparable maximal predicates are ambiguous") {
	FSCompletenessManifest invalid = manifest;
	FSCompletenessRelation peer;
	peer.id = "static_member_wrapper";
	peer.from["source_proof"] = "static_member";
	peer.to["destination"] = "union";
	peer.derive["analysis"] = "same";
	invalid.relations.push_back(peer);
	CHECK(FSCompletenessGraph::resolve(invalid, catalog, resolution, errors) == ERR_INVALID_DATA);
	CHECK(completeness_errors_contain(errors, "incomparable maximal predicates"));
}
```

Clear `errors` and reset `resolution` at the beginning of each subcase.

- [ ] **Step 3: Build to verify the graph API is missing**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypeCompleteness*Graph*"
```

Expected: compilation fails because `fs_type_completeness_graph.h` does not exist.

- [ ] **Step 4: Add graph records and predicate matching**

Create `fs_type_completeness_graph.h` with these public records:

```cpp
#pragma once

#include "fs_type_completeness_manifest.h"

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
```

Predicate matching treats a string as a concrete leaf and `{ "class": "name" }` as class membership on that same
axis. An empty predicate matches every cell. Unknown selectors have already failed vocabulary validation.

- [ ] **Step 5: Implement finite expansion and simple-path enumeration**

In `fs_type_completeness_graph.cpp`, implement this algorithm exactly:

1. Expand `domain_axis_order` recursively into the Cartesian product.
2. Expand an anchor's `surfaces` into the `surface` coordinate only when the anchor omitted that coordinate.
3. Start one derivation state per anchor, containing its cell, dimensions, empty relation-ID set, and a visited-cell
   set containing the anchor.
4. For depths `0..max_chain_length - 1`, apply every relation whose `from` matches. Copy the source coordinates and
   patch only keys named by `to`; this is the frame condition.
5. Reject an application that repeats a relation ID or revisits any cell in that state.
6. Select matching exceptions whose `parent` is the relation. Compute specificity from the finite matched-cell set;
   require one unique strict-subset maximum per output dimension. A selected exception replaces the parent derive and
   does not consume another step.
7. Produce only dimensions named by `derive`. `same` requires the input state to contain that dimension; a literal
   outcome creates or replaces it.
8. Collect all paths of length at most three. For each domain cell and required dimension, require at least one path,
   require every outcome to agree, retain every agreeing path, and choose shortest then lexical relation/exception ID
   sequence as canonical provenance.
9. Report every validation error in one run and return `ERR_INVALID_DATA` when any exists.

Use a canonical coordinate key made from `domain_axis_order` only for the in-memory visited set; Task 4 adds the
external stable ID.

- [ ] **Step 6: Add the approved worked rule verbatim as data**

Copy the JSON object from the design's “Worked rule before framework expansion” section into
`rules/union_destination_membership.json`, adding only:

```json
"schema_version": 1,
"max_chain_length": 3
```

Do not change its domain, three required-dimension conditions, three anchors, seven relations, or the
`unproven_source_requires_membership` exception. In particular, retain `plain_numeric_constant_parity`, the separate
reflective analysis/runtime/carrier relations, and `argument_binding` vocabulary.

- [ ] **Step 7: Run graph and manifest tests**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*TypeCompleteness*Manifest*" --case "*TypeCompleteness*Graph*"
```

Expected: the worked family reports 40 resolved cells, maximum chain length three, zero missing required dimensions,
and all negative cases return their named validation error.

- [ ] **Step 8: Commit the executable contract**

```sh
git add modules/foundry_script/tests/fs_type_completeness_graph.* \
  modules/foundry_script/tests/test_type_completeness_graph.h \
  modules/foundry_script/tests/type_completeness/rules/union_destination_membership.json
git commit -m "Validate bounded type completeness derivations"
```

## Task 4: Make Case IDs Stable Across Generation and Repartitioning

**Files:**

- Create: `modules/foundry_script/tests/fs_type_completeness_case_id.h`
- Create: `modules/foundry_script/tests/fs_type_completeness_case_id.cpp`
- Modify: `modules/foundry_script/tests/fs_type_completeness_graph.h`
- Modify: `modules/foundry_script/tests/fs_type_completeness_graph.cpp`
- Modify: `modules/foundry_script/tests/test_type_completeness_graph.h`
- Create: `modules/foundry_script/tests/type_completeness/migrations/v1.json`

- [ ] **Step 1: Write failing order-independence and migration tests**

Add `fs_temporary_project_tree.h`, `fs_type_completeness_case_id.h`, and `core/os/os.h` to the graph test header,
then add:

```cpp
TEST_CASE("Case IDs depend on semantic coordinates, not insertion order") {
	Dictionary first;
	first["surface"] = "text";
	first["destination"] = "union";
	Dictionary second;
	second["destination"] = "union";
	second["surface"] = "text";
	CHECK_EQ(FSCompletenessCaseID::make("family", first),
			FSCompletenessCaseID::make("family", second));
}

TEST_CASE("Migration aliases resolve split coordinates and reject cycles") {
	TemporaryProjectTree tree(vformat("type_completeness_migrations_%d",
			OS::get_singleton()->get_process_id()));
	REQUIRE(tree.is_valid());
	tree.write_file("migrations/split.json", R"({
  "schema_version": 1,
  "migrations": [{"old_id":"a","new_ids":["b","c"],"reason":"partition split"}]
})");
	HashSet<String> current_ids;
	current_ids.insert("b");
	current_ids.insert("c");
	FSCompletenessMigrations migrations;
	Vector<String> errors;
	REQUIRE(migrations.load(tree.root.path_join("migrations"), current_ids, errors) == OK);
	CHECK_EQ(migrations.resolve("a"), Vector<String>({ "b", "c" }));

	TemporaryProjectTree cycle_tree(vformat("type_completeness_migration_cycle_%d",
			OS::get_singleton()->get_process_id()));
	REQUIRE(cycle_tree.is_valid());
	cycle_tree.write_file("migrations/cycle.json", R"({
  "schema_version": 1,
  "migrations": [
    {"old_id":"a","new_ids":["b"],"reason":"first half"},
    {"old_id":"b","new_ids":["a"],"reason":"second half"}
  ]
})");
	current_ids.insert("a");
	CHECK(migrations.load(cycle_tree.root.path_join("migrations"), current_ids, errors) ==
			ERR_INVALID_DATA);
}
```

- [ ] **Step 2: Run and verify the missing ID API**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypeCompleteness*Graph*"
```

Expected: compilation fails because `FSCompletenessCaseID` and migrations are undeclared.

- [ ] **Step 3: Implement canonical IDs and aliases**

Create this public API:

```cpp
class FSCompletenessCaseID {
public:
	static String canonical_coordinates(const Dictionary &p_coordinates);
	static String make(const String &p_family, const Dictionary &p_coordinates);
};

class FSCompletenessMigrations {
	HashMap<String, Vector<String>> aliases;

public:
	Error load(const String &p_directory, const HashSet<String> &p_current_ids,
			Vector<String> &r_errors);
	Vector<String> resolve(const String &p_old_id) const;
};
```

`canonical_coordinates` sorts coordinate names lexically and joins escaped `name=value` pairs with `|`.
`make` returns `fstc-v1-` plus the first 20 hexadecimal characters of
`(family + "|" + canonical_coordinates).sha256_text()`. Migration loading sorts files, rejects duplicate sources,
self aliases, cycles, and replacement IDs absent from `p_current_ids`. The runner supplies the IDs resolved from the
current catalog; focused tests supply a small explicit set.

Add `String case_id` to `FSCompletenessResolvedCell` and assign it after successful graph resolution.

- [ ] **Step 4: Add the initial migration file and rerun tests**

Create:

```json
{"schema_version":1,"migrations":[]}
```

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypeCompleteness*Graph*"
```

Expected: all graph and ID cases pass, and all 40 pilot cells have distinct `fstc-v1-` IDs.

- [ ] **Step 5: Commit stable identity support**

```sh
git add modules/foundry_script/tests/fs_type_completeness_case_id.* \
  modules/foundry_script/tests/fs_type_completeness_graph.* \
  modules/foundry_script/tests/test_type_completeness_graph.h \
  modules/foundry_script/tests/type_completeness/migrations/v1.json
git commit -m "Add stable type completeness case IDs"
```

## Task 5: Render the Union Pilot and Observe Static Semantics

**Files:**

- Create: `modules/foundry_script/tests/fs_type_completeness_union_adapter.h`
- Create: `modules/foundry_script/tests/fs_type_completeness_union_adapter.cpp`
- Create: `modules/foundry_script/tests/test_type_completeness_union_pilot.h`

- [ ] **Step 1: Write failing behavioral rendering tests**

The renderer's output is tested by parsing and analyzing it, not by matching generated source text:

```cpp
#include "fs_type_completeness_union_adapter.h"
#include "modules/foundry_script/fs_analyzer.h"
#include "modules/foundry_script/fs_parser.h"
#include "tests/test_macros.h"

namespace FSTests {

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness][UnionPilot]") {
TEST_CASE("Every generated union pilot program has the declared analysis outcome") {
	FSCompletenessResolution resolution = load_union_pilot_resolution();
	for (const FSCompletenessResolvedCell &cell : resolution.cells) {
		CAPTURE(cell.case_id);
		FSCompletenessProgram program;
		REQUIRE(FSUnionCompletenessAdapter::render(cell, program) == OK);
		FSCompletenessObservation observation =
				FSUnionCompletenessAdapter::analyze(program, cell.coordinates["surface"]);
		const FSCompletenessResolvedDimension *analysis = cell.find_dimension("analysis");
		REQUIRE(analysis != nullptr);
		CHECK_EQ(observation.dimensions["analysis"], analysis->expected);
		CHECK(observation.diagnostics.is_empty());
	}
}
} // TEST_SUITE

} // namespace FSTests
```

Define the helper in this header; it composes the production loader and graph rather than adding another loader path:

```cpp
static FSCompletenessResolution load_union_pilot_resolution() {
	FSCompletenessCatalog catalog;
	FSCompletenessManifest manifest;
	FSCompletenessResolution resolution;
	Vector<String> errors;
	REQUIRE(catalog.load(completeness_root, errors) == OK);
	REQUIRE(FSCompletenessManifest::load(
			completeness_root.path_join("rules/union_destination_membership.json"),
			manifest, errors) == OK);
	REQUIRE(validate_manifest_vocabulary(manifest, catalog, errors) == OK);
	REQUIRE(FSCompletenessGraph::resolve(manifest, catalog, resolution, errors) == OK);
	return resolution;
}
```

- [ ] **Step 2: Build and verify the adapter API is absent**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypeCompleteness*UnionPilot*"
```

Expected: compilation fails because `fs_type_completeness_union_adapter.h` does not exist.

- [ ] **Step 3: Define programs and observations**

Create the adapter header:

```cpp
#pragma once

#include "fs_type_completeness_graph.h"

namespace FSTests {

struct FSCompletenessProgram {
	String case_id;
	String surface;
	String source;
	String expected_output;
};

struct FSCompletenessObservation {
	String case_id;
	String surface;
	Dictionary dimensions;
	PackedStringArray diagnostics;
	String produced_output;
};

class FSUnionCompletenessAdapter {
public:
	static Error render(const FSCompletenessResolvedCell &p_cell, FSCompletenessProgram &r_program);
	static FSCompletenessObservation analyze(const FSCompletenessProgram &p_program, const String &p_surface);
	static FSCompletenessObservation inspect_runtime_contract(
			const FSCompletenessProgram &p_program, const Dictionary &p_coordinates);
	static Error witness_coordinates(const String &p_witness_id, Dictionary &r_coordinates);
};

} // namespace FSTests
```

- [ ] **Step 4: Implement one program template with coordinate substitutions**

Render a complete script containing `Holder`, `accept`, `supply`, `erase`, `carrier_of`, and `test`. Substitute only
the destination annotation, source declaration/expression, and boundary body:

```cpp
const String destination = p_cell.coordinates["destination"] == "union" ?
		"uint | String" : "uint";

const HashMap<String, String> source_expressions = {
	{ "static_member", "typed_source" },
	{ "numeric_constant", "5" },
	{ "gradual", "supply(5U)" },
	{ "erased", "erase[uint](5U)" },
	{ "variant", "variant_source" },
};

const String boundary_body = p_cell.coordinates["boundary"] == "argument_binding" ?
		"\tvar stored: Variant = accept(SOURCE)\n" :
		"\tvar holder := Holder.new()\n"
		"\tholder.set(&\"value\", SOURCE)\n"
		"\tvar stored: Variant = holder.value\n";
```

The fixed surrounding Foundry Script is:

```text
class Holder extends RefCounted:
	var value: DESTINATION = 0U

func supply(value):
	return value

func erase[T](value: T) -> Variant:
	return value

func accept(value: DESTINATION) -> Variant:
	return value

func carrier_of(value: Variant) -> String:
	if value is uint:
		return "uint " + str(value)
	if value is int:
		return "int " + str(value)
	return "other"

func test() -> void:
	var typed_source: uint = 5U
	var variant_source: Variant = 5U
BOUNDARY_BODY
	print(carrier_of(stored))
```

Replace `DESTINATION`, `SOURCE`, and `BOUNDARY_BODY` in memory. Set `program.surface` from the surface coordinate,
set expected output to `"uint 5\n"`, and reject an unknown coordinate with `ERR_INVALID_DATA`.

Implement `witness_coordinates` as a closed map for the manifest's three witness IDs:

```text
text_gradual_argument_binding
  destination=union, source_proof=gradual, boundary=argument_binding, surface=text
bytecode_erased_reflective_write
  destination=union, source_proof=erased, boundary=reflective_write, surface=bytecode
text_static_member_argument_binding
  destination=union, source_proof=static_member, boundary=argument_binding, surface=text
```

Return `ERR_DOES_NOT_EXIST` for any other witness. The runner will fail manifest validation when an exception names a
witness its family adapter does not own.

- [ ] **Step 5: Implement the analyzer observation**

Parse the rendered source at a scratch `user://type_completeness/<case_id>.fs` path, run `FSAnalyzer`, copy every
parser/analyzer diagnostic into the observation, and set `analysis` to `accept` only when both stages return `OK`.
The surface does not change static semantics, but retain it in the observation for parity reporting.

- [ ] **Step 6: Run the analyzer pilot and commit**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypeCompleteness*UnionPilot*"
git add modules/foundry_script/tests/fs_type_completeness_union_adapter.* \
  modules/foundry_script/tests/test_type_completeness_union_pilot.h
git commit -m "Generate the union completeness pilot"
```

Expected: all 40 rendered programs analyze as declared.

## Task 6: Execute Text and Compiled-Bytecode Surfaces

**Files:**

- Modify: `modules/foundry_script/tests/fs_type_completeness_union_adapter.h`
- Modify: `modules/foundry_script/tests/fs_type_completeness_union_adapter.cpp`
- Modify: `modules/foundry_script/tests/test_type_completeness_union_pilot.h`

- [ ] **Step 1: Add failing runtime, carrier, and parity assertions**

Add `fs_temporary_project_tree.h` and `core/os/os.h` to the union-pilot test header, then add:

```cpp
TEST_CASE("The union pilot agrees across text and compiled bytecode") {
	FSCompletenessResolution resolution = load_union_pilot_resolution();
	TemporaryProjectTree tree(vformat("type_completeness_union_runtime_%d",
			OS::get_singleton()->get_process_id()));
	REQUIRE(tree.is_valid());

	Vector<FSCompletenessProgram> programs;
	for (const FSCompletenessResolvedCell &cell : resolution.cells) {
		FSCompletenessProgram program;
		REQUIRE(FSUnionCompletenessAdapter::render(cell, program) == OK);
		programs.push_back(program);
	}

	FSCompletenessRuntimeBatch batch;
	REQUIRE(FSUnionCompletenessAdapter::execute(tree.root, programs, batch) == OK);
	CHECK_EQ(batch.text.size(), 20);
	CHECK_EQ(batch.bytecode.size(), 20);
	CHECK_EQ(batch.parity_failures, 0);
	for (const FSCompletenessResolvedCell &cell : resolution.cells) {
		CAPTURE(cell.case_id);
		const bool is_text = cell.coordinates["surface"] == "text";
		const FSCompletenessRuntimeResult &observed = is_text ?
				batch.text[cell.case_id] : batch.bytecode[cell.case_id];
		CHECK(observed.passed);
		for (const KeyValue<String, FSCompletenessResolvedDimension> &required :
				cell.dimensions) {
			CHECK_EQ(observed.dimensions[required.key], required.value.expected);
		}
	}
}
```

- [ ] **Step 2: Run and verify `execute` is missing**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypeCompleteness*UnionPilot*"
```

Expected: compilation fails because `FSCompletenessRuntimeBatch` and `execute` are undeclared.

- [ ] **Step 3: Add batch types and scratch fixture execution**

Add:

```cpp
struct FSCompletenessRuntimeResult : FSCompletenessObservation {
	bool passed = false;
	String status;
};

struct FSCompletenessRuntimeBatch {
	HashMap<String, FSCompletenessRuntimeResult> text;
	HashMap<String, FSCompletenessRuntimeResult> bytecode;
	int parity_failures = 0;
};

static Error execute(const String &p_scratch_root,
		const Vector<FSCompletenessProgram> &p_programs,
		FSCompletenessRuntimeBatch &r_batch);
```

Set `program.surface` from the cell coordinate. Partition programs into `<scratch>/text` and `<scratch>/bytecode`,
with an empty `project.foundry` in each subproject. Create each `<case_id>.fs` and `<case_id>.out` only in its declared
surface directory. Use `FSTestRunner(text_root, true, false, false, false)` for text and
`FSTestRunner(bytecode_root, true, false, false, true)` for compiled bytecode. Call `run_tests_collecting`, require
setup to succeed, and map each outcome's path stem back to the stable case ID. Pair results by the canonical
coordinates with `surface` removed, compare output and every common dimension, and increment `parity_failures` for
each differing pair. Never write generated cases into the tracked fixture tree.

- [ ] **Step 4: Observe runtime obligation without source-text assertions**

Compile each rendered program with the existing parser/analyzer/compiler path used by
`compile_bytecode_test_source`. For the bytecode surface, serialize and reload the compiled script before observing
it. Read the destination descriptor from the `accept` function's first argument for `argument_binding`, and from
`Holder.value` through `FoundryScript::find_member_data_type` for `reflective_write`. Record:

```cpp
const bool expects_union = coordinates["destination"] == "union";
if ((expects_union && destination_type.kind != FSDataType::UNION) ||
		(!expects_union && destination_type.kind != FSDataType::BUILTIN)) {
	return ERR_INVALID_DATA;
}
result.dimensions["runtime_obligation"] = expects_union ?
		"union_membership_check" : "typed_destination_check";
```

Only attach `runtime_obligation` when the resolved cell requires it. Prove the descriptor rejects `RefCounted.new()`
through `FSDataType::is_type`; this tests behavior and does not inspect emitted source or instruction text.

- [ ] **Step 5: Observe numeric carrier from execution**

For cells requiring `stored_carrier`, parse `produced_output` as the adapter's structured `"uint 5"` observation and
record `plain_destination` for a plain destination or `admitting_alternative` for a union destination. Reject any
other carrier token as an adapter failure. Keep text and bytecode observations separate until the parity assertion.

- [ ] **Step 6: Run the two-surface pilot and commit**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypeCompleteness*UnionPilot*"
git add modules/foundry_script/tests/fs_type_completeness_union_adapter.* \
  modules/foundry_script/tests/test_type_completeness_union_pilot.h
git commit -m "Run completeness cases across text and bytecode"
```

Expected: 20 text and 20 compiled-bytecode cells pass, and all 20 semantic coordinate pairs agree.

## Task 7: Emit Structured Reports and Reconcile the Empty Ledger

**Files:**

- Create: `modules/foundry_script/tests/fs_type_completeness_runner.h`
- Create: `modules/foundry_script/tests/fs_type_completeness_runner.cpp`
- Modify: `modules/foundry_script/tests/test_type_completeness_union_pilot.h`
- Create: `modules/foundry_script/tests/type_completeness/findings/.gitkeep`

- [ ] **Step 1: Write the failing end-to-end report test**

Add explicit includes for `fs_type_completeness_runner.h`, `core/io/file_access.h`, and `core/io/json.h`, then add:

```cpp
TEST_CASE("The pilot report accounts for every cell and provenance class") {
	TemporaryProjectTree tree(vformat("type_completeness_report_%d",
			OS::get_singleton()->get_process_id()));
	REQUIRE(tree.is_valid());
	const String report_path = tree.root.path_join("report.json");

	FSCompletenessRunOptions options;
	options.catalog_root = completeness_root;
	options.family = "union_destination_membership";
	options.scratch_root = tree.root.path_join("cases");
	options.report_path = report_path;
	FSCompletenessRunResult result;
	REQUIRE(FSCompletenessRunner::run(options, result) == OK);
	CHECK(result.success);
	CHECK_EQ(result.executed_cells, 40);
	CHECK_EQ(result.findings.size(), 0);

	Ref<JSON> json;
	json.instantiate();
	REQUIRE(json->parse(FileAccess::get_file_as_string(report_path)) == OK);
	Dictionary report = json->get_data();
	CHECK_EQ(report["schema_version"], 1);
	CHECK_EQ(report["family"], "union_destination_membership");
	CHECK_EQ(report["cell_count"], 40);
	CHECK_EQ(report["uncovered_required_dimensions"], 0);
	CHECK_EQ(report["text_bytecode_parity_failures"], 0);
	CHECK(Dictionary(report["coverage_by_chain_length"]).has("3"));
}
```

- [ ] **Step 2: Add a mismatch test using an injected observation**

Expose a test-only adapter callback in the run options, defaulting to the real union adapter. Inject one observation
that changes `stored_carrier` for a known case:

```cpp
static String corrupted_carrier_case_id;

static void corrupt_carrier_observation(FSCompletenessObservation &r_observation) {
	if (r_observation.case_id == corrupted_carrier_case_id &&
			r_observation.dimensions.has("stored_carrier")) {
		r_observation.dimensions["stored_carrier"] = "plain_destination";
	}
}
```

Select the text, union, numeric-constant, argument-binding cell from `load_union_pilot_resolution()`, assign its ID to
`corrupted_carrier_case_id`, set `options.observation_mutator = corrupt_carrier_observation`, and assert:

```cpp
CHECK(FSCompletenessRunner::run(options, result) == FAILED);
CHECK_FALSE(result.success);
REQUIRE_EQ(result.findings.size(), 1);
CHECK_EQ(result.findings[0].case_id, changed_case_id);
CHECK_EQ(result.findings[0].classification, "unclassified");
CHECK_EQ(result.findings[0].dimension, "stored_carrier");
CHECK_EQ(result.findings[0].expected, "admitting_alternative");
CHECK_EQ(result.findings[0].actual, "plain_destination");
```

This is dependency injection at the adapter boundary, not a production fault hook.

- [ ] **Step 3: Build and verify the runner API is absent**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypeCompleteness*UnionPilot*"
```

Expected: compilation fails because `fs_type_completeness_runner.h` does not exist.

- [ ] **Step 4: Define runner, finding, and report records**

```cpp
#pragma once

#include "fs_type_completeness_union_adapter.h"

namespace FSTests {

struct FSCompletenessFinding {
	String finding_id;
	String case_id;
	String family;
	String dimension;
	Variant expected;
	Variant actual;
	String classification = "unclassified";
	String artifact_path;
};

struct FSCompletenessRunOptions {
	String catalog_root;
	String family;
	String scratch_root;
	String report_path;
	void (*observation_mutator)(FSCompletenessObservation &) = nullptr;
};

struct FSCompletenessRunResult {
	bool success = false;
	int executed_cells = 0;
	Vector<FSCompletenessFinding> findings;
	Dictionary report;
};

class FSCompletenessRunner {
public:
	static Error run(const FSCompletenessRunOptions &p_options, FSCompletenessRunResult &r_result);
};

} // namespace FSTests
```

- [ ] **Step 5: Implement runner and fail-closed ledger loading**

The runner performs these stages in order and stops only for catalog/harness initialization errors:

```text
load catalog -> load family -> validate vocabulary -> resolve graph -> load migrations -> load findings
-> render all cells -> analyze -> execute text and bytecode -> compare dimensions and parity
-> reconcile case IDs -> write report
```

Before execution, resolve every exception's positive and boundary witness through the family adapter. Require each
coordinate set to name one generated cell. After execution, require positive witnesses to observe the exception's
transformed dimensions and boundary witnesses to observe the parent relation's dimensions. Missing, duplicate, or
misclassified witnesses are harness-validation failures, not product findings.

Load `findings/*.json` in lexical order, ignoring `.gitkeep`. A finding file must contain schema version, stable
finding ID, current or migrated case ID, family, classification, issue URL, closure-packet URL, and permanent-test
paths. Reject duplicate finding IDs, unresolved case IDs, and unknown classification tokens. The five terminal
machine tokens are `product_defect`, `specification_defect`, `harness_defect`, `intentional_unsupported`, and
`duplicate`; `unclassified` is the only nonterminal token and always keeps the run failing.
An observed mismatch with no matching ledger entry becomes an in-memory `unclassified` finding and makes the run
return `FAILED`; a known mismatch remains a mismatch in the report even when gate scoping later permits unrelated
work. When a direct expected-value mismatch also creates text/bytecode disagreement, attach the parity evidence to
that same finding instead of emitting a duplicate parity finding. A standalone parity disagreement still creates its
own finding.

Write JSON through `JSON::stringify(report, "\t", false, true) + "\n"`. The report must contain:

```json
{
  "schema_version": 1,
  "family": "union_destination_membership",
  "success": true,
  "cell_count": 40,
  "executed_by_surface": {"text": 20, "bytecode": 20},
  "coverage_by_chain_length": {"0": 10, "1": 28, "2": 26, "3": 8},
  "coverage_by_dimension": {},
  "uncovered_required_dimensions": 0,
  "text_bytecode_parity_failures": 0,
  "findings": [],
  "cases": []
}
```

The chain-length counts cover 72 required cell/dimension pairs and are the expected result of the approved pilot
manifest. Every case entry includes coordinates, expected and actual dimensions by surface, canonical provenance per
dimension, all agreeing provenance, status, and artifact path.

- [ ] **Step 6: Run the end-to-end report test and commit**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypeCompleteness*UnionPilot*"
git add modules/foundry_script/tests/fs_type_completeness_runner.* \
  modules/foundry_script/tests/test_type_completeness_union_pilot.h \
  modules/foundry_script/tests/type_completeness/findings/.gitkeep
git commit -m "Report type completeness pilot coverage"
```

Expected: the real pilot succeeds with 40 cells and zero findings; the injected mismatch creates exactly one
unclassified finding and returns failure.

## Task 8: Add Fail-Closed Capability Selection Without Enabling Baseline Exemptions

**Files:**

- Modify: `modules/foundry_script/tests/fs_type_completeness_manifest.h`
- Modify: `modules/foundry_script/tests/fs_type_completeness_manifest.cpp`
- Modify: `modules/foundry_script/tests/test_type_completeness_manifest.h`
- Create: `modules/foundry_script/tests/type_completeness/capabilities.json`

- [ ] **Step 1: Write failing selector behavior tests**

```cpp
TEST_CASE("Capability selection is conservative and manifest-reachable") {
	FSCompletenessCapabilityMap map;
	Vector<String> errors;
	REQUIRE(map.load(completeness_root.path_join("capabilities.json"), errors) == OK);

	FSCompletenessSelection selected = map.select({
		"modules/foundry_script/fs_compiler.cpp",
	});
	CHECK(selected.families.has("union_destination_membership"));
	CHECK_FALSE(selected.used_broad_core_fallback);

	selected = map.select({ "modules/foundry_script/new_type_path.cpp" });
	CHECK(selected.used_broad_core_fallback);
	CHECK(selected.validation_errors.has(
			"unmapped production path: modules/foundry_script/new_type_path.cpp"));

	selected = map.select({ "docs/type_notes.md" });
	CHECK(selected.families.is_empty());
	CHECK_FALSE(selected.used_broad_core_fallback);
}
```

Add a second test proving that a mapped family absent from `rules/` and a rule family unreachable from any mapping
both fail map validation.

- [ ] **Step 2: Run and verify the selector types are absent**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypeCompleteness*Manifest*"
```

Expected: compilation fails because `FSCompletenessCapabilityMap` is undeclared.

- [ ] **Step 3: Implement selector records and path categories**

Add `core/templates/pair.h` to the manifest header, then add:

```cpp
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
	Error validate_against_rule_directory(const String &p_rule_directory,
			Vector<String> &r_errors) const;
	FSCompletenessSelection select(const Vector<String> &p_changed_paths) const;
};
```

Use component-aware prefix matching: `modules/foundry_script/fs_compiler.cpp` may match its exact file or a directory
prefix ending in `/`; `modules/foundry_script_extra/` must not. Unmapped paths below `modules/foundry_script/` select
the fixed `broad_core_families` and add a validation error. Nonproduction categories select nothing. Do not implement
irrelevance declarations in this first map; that expiring policy belongs with the later CI selector service.

- [ ] **Step 4: Add the initial ownership map**

```json
{
  "schema_version": 1,
  "production": [
    {
      "paths": [
        "modules/foundry_script/fs_analyzer.cpp",
        "modules/foundry_script/fs_compiler.cpp",
        "modules/foundry_script/fs_vm.cpp",
        "modules/foundry_script/fs_type.cpp",
        "modules/foundry_script/fs_function.cpp",
        "modules/foundry_script/fs_reflection.cpp"
      ],
      "families": ["union_destination_membership"]
    }
  ],
  "nonproduction_prefixes": [
    "docs/",
    "modules/foundry_script/tests/"
  ],
  "broad_core_families": ["union_destination_membership"]
}
```

- [ ] **Step 5: Run selector tests and commit**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypeCompleteness*Manifest*"
git add modules/foundry_script/tests/fs_type_completeness_manifest.* \
  modules/foundry_script/tests/test_type_completeness_manifest.h \
  modules/foundry_script/tests/type_completeness/capabilities.json
git commit -m "Map Foundry Script paths to completeness families"
```

Expected: mapped paths select the pilot, an unmapped production path selects the pilot fallback and fails mapping
validation, and docs/tests select no semantic family.

## Task 9: Verify the Foundation on Required Build Surfaces

**Files:**

- Modify only files from Tasks 1–8 if verification exposes a defect.

- [ ] **Step 1: Run all focused completeness cases on Ninja**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TypeCompleteness*"
```

Expected: every manifest, graph, ID, selector, union-pilot, text, bytecode, report, and fault-injection case passes.

- [ ] **Step 2: Run the existing union regression fixtures**

```sh
./bin/foundry.* --headless test fixtures "union_numeric_constant" \
  "type_union_membership" --pass text
./bin/foundry.* --headless test fixtures "union_numeric_constant" \
  "type_union_membership" --pass bytecode
```

Expected: both commands select fixtures and report zero failures. These preserve the handwritten regressions beside
the new generated pilot.

- [ ] **Step 3: Run the native strict build and focused suite**

```sh
python3 scripts/agent_build.py --test --case "*TypeCompleteness*"
```

Expected: the native SCons strict build succeeds and every selected case passes.

- [ ] **Step 4: Run repository hooks on the complete change**

```sh
pre-commit run --all-files
```

Expected: every hook passes. If the known ignored cache containing `/Users/` still trips the repository-wide
file-policy hook, verify the cache is untracked and unchanged, run every other hook, and record that external blocker
without deleting or staging the user's cache.

- [ ] **Step 5: Perform the plan's closure audit**

Confirm from the generated report and test output:

- 40 semantic cells resolve, 20 execute on each surface, and all 20 cross-surface pairs agree;
- every required dimension has declared or derived coverage;
- maximum canonical chain length is three;
- every case ID is unique and every migration reference resolves;
- injected expectation corruption creates a blocking unclassified finding;
- no code path permits a pre-existing mismatch to become non-blocking;
- generated projects live below `FOUNDRY_TEST_SCRATCH` and the tracked fixture tree is unchanged.

- [ ] **Step 6: Commit any verification-only corrections**

If Step 1–5 required changes, stage only those files and commit:

```sh
git add modules/foundry_script/tests
git commit -m "Harden type completeness foundation"
```

If no changes were needed, do not create an empty commit.

## Follow-on Plans Required Before Presubmit Rollout

Write these as separate plans after this foundation is merged and its real runtime data is available:

1. `foundry-script-type-completeness-triage-automation`: `develop` comparator, machine-readable provisional GitHub
   issue, reviewed bot-ledger PR, deadline service, merged/provisional reconciliation, manual fallback, and
   slice-scoped blocking.
2. `foundry-script-type-completeness-history-and-mutation`: HEAD re-verification of mined findings, permanent seed
   families, patch-recipe mutation catalog, nightly cadence, and detector mappings.
3. One plan per remaining rollout workstream: representation census, destination-wrapper parity expansion, soundness
   closure, lifecycle/tooling closure, and final release/soak activation.
