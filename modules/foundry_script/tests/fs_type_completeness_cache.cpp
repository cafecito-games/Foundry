/**************************************************************************/
/*  fs_type_completeness_cache.cpp                                        */
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

#include "fs_type_completeness_cache.h"

#include "fs_temporary_project_tree.h"
#include "fs_type_completeness_common.h"
#include "fs_type_completeness_manifest.h"

#include "core/io/dir_access.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

namespace FSTests {

namespace {

// Owns every cached record and every baseline for the life of the process, and releases them at exit
// so a long test run does not end holding the whole matrix.
struct CompletenessStore {
	Mutex mutex;
	HashMap<String, FSCompletenessCatalogRecord *> records;
	int load_count = 0;

	HashMap<String, FSCompletenessRunResult *> baselines;
	int baseline_run_count = 0;

	~CompletenessStore() {
		for (const KeyValue<String, FSCompletenessCatalogRecord *> &entry : records) {
			memdelete(entry.value);
		}
		for (const KeyValue<String, FSCompletenessRunResult *> &entry : baselines) {
			memdelete(entry.value);
		}
	}
};

CompletenessStore &store() {
	static CompletenessStore instance;
	return instance;
}

String baseline_scratch_name(const String &p_family) {
	return vformat("type_completeness_baseline_%s_%d", p_family, OS::get_singleton()->get_process_id());
}

FSCompletenessStructuralFailure baseline_failure(Error p_error, const String &p_detail) {
	FSCompletenessStructuralFailure failure;
	failure.stage = "baseline_unavailable";
	failure.detail = p_detail;
	failure.error_code = p_error;
	return failure;
}

String cache_key(const String &p_canonical_catalog_root, const String &p_family) {
	// The family cannot contain the separator: rule families are lowercase portable filename stems.
	return p_canonical_catalog_root + "\n" + p_family;
}

void load_record(const String &p_canonical_catalog_root, const String &p_family,
		FSCompletenessCatalogRecord &r_record) {
	r_record.error = r_record.catalog.load(p_canonical_catalog_root, r_record.errors);
	if (r_record.error != OK) {
		r_record.resolution_error = r_record.error;
		return;
	}
	r_record.error = FSCompletenessManifest::load(
			p_canonical_catalog_root.path_join("rules").path_join(p_family + ".json"), r_record.manifest,
			r_record.errors);
	if (r_record.error != OK) {
		r_record.resolution_error = r_record.error;
		return;
	}
	if (r_record.manifest.family != p_family) {
		r_record.errors.push_back(vformat("%s: rule family does not match the requested family '%s'",
				p_canonical_catalog_root, p_family));
		r_record.error = ERR_INVALID_DATA;
		r_record.resolution_error = r_record.error;
		return;
	}
	r_record.error = validate_manifest_vocabulary(r_record.manifest, r_record.catalog, r_record.errors);
	if (r_record.error != OK) {
		r_record.resolution_error = r_record.error;
		return;
	}
	r_record.error = FSCompletenessGraph::resolve(
			r_record.manifest, r_record.catalog, r_record.resolution, r_record.errors);
	r_record.resolution_error = r_record.error;
	if (r_record.error != OK) {
		return;
	}
	HashSet<String> current_ids;
	for (const FSCompletenessResolvedCell &cell : r_record.resolution.cells) {
		current_ids.insert(cell.case_id);
	}
	r_record.error = r_record.migrations.load(
			p_canonical_catalog_root.path_join("migrations"), current_ids, r_record.errors);
}

} // namespace

String tracked_catalog_root() {
	return "modules/foundry_script/tests/type_completeness";
}

Error FSCompletenessCatalogCache::get(const String &p_canonical_catalog_root, const String &p_family,
		const FSCompletenessCatalogRecord *&r_record, Vector<String> &r_errors) {
	r_record = nullptr;
	r_errors.clear();
	if (p_canonical_catalog_root.is_empty() || p_family.is_empty()) {
		return ERR_INVALID_PARAMETER;
	}
	CompletenessStore &shared_store = store();
	MutexLock lock(shared_store.mutex);
	const String key = cache_key(p_canonical_catalog_root, p_family);
	FSCompletenessCatalogRecord **existing = shared_store.records.getptr(key);
	if (existing == nullptr) {
		FSCompletenessCatalogRecord *record = memnew(FSCompletenessCatalogRecord);
		load_record(p_canonical_catalog_root, p_family, *record);
		shared_store.records.insert(key, record);
		shared_store.load_count++;
		existing = shared_store.records.getptr(key);
	}
	r_record = *existing;
	r_errors = (*existing)->errors;
	return (*existing)->error;
}

int FSCompletenessCatalogCache::load_count() {
	CompletenessStore &shared_store = store();
	MutexLock lock(shared_store.mutex);
	return shared_store.load_count;
}

const FSCompletenessRunResult &FSCompletenessBaseline::shared(const String &p_family) {
	CompletenessStore &shared_store = store();
	MutexLock lock(shared_store.mutex);
	FSCompletenessRunResult **existing = shared_store.baselines.getptr(p_family);
	if (existing != nullptr) {
		return **existing;
	}

	FSCompletenessRunResult *result = memnew(FSCompletenessRunResult);
	shared_store.baselines.insert(p_family, result);
	shared_store.baseline_run_count++;

	String root;
	const Error root_error = resolve_scratch_root(p_family, root);
	if (root_error != OK) {
		result->outcome = "structural_failure";
		result->structural_failures.push_back(baseline_failure(root_error,
				root_error == ERR_ALREADY_IN_USE
						? "The baseline scratch root already exists and is not this run's to reuse."
						: "The test scratch space is unavailable, so no baseline root could be resolved."));
		return *result;
	}

	// The tree is scoped to this call: the baseline is consumed as an in-memory result, so its scratch
	// artifacts are removed as soon as the run publishes them rather than surviving the whole process.
	// A test that needs the artifact files on disk runs its own family instead.
	TemporaryProjectTree tree(baseline_scratch_name(p_family));
	if (!tree.is_valid()) {
		result->outcome = "structural_failure";
		result->structural_failures.push_back(
				baseline_failure(tree.get_setup_error(), "The baseline scratch root could not be created."));
		return *result;
	}

	FSCompletenessRunOptions options;
	options.catalog_root = tracked_catalog_root();
	options.family = p_family;
	options.scratch_root = tree.root;
	options.report_path = tree.root.path_join("report.json");
	FSCompletenessRunner::run(options, *result);
	return *result;
}

Error FSCompletenessBaseline::resolve_scratch_root(const String &p_family, String &r_root) {
	r_root = TemporaryProjectTree::get_test_scratch_path(baseline_scratch_name(p_family));
	if (r_root.is_empty()) {
		return ERR_UNCONFIGURED;
	}
	// The leaf carries this process's ID, so an existing directory means either a live sibling process
	// that reused the same scratch space or a crashed run that happened to hold this ID. Reusing it
	// would mix two runs' artifacts into one baseline, so the baseline refuses instead.
	if (DirAccess::dir_exists_absolute(r_root)) {
		return ERR_ALREADY_IN_USE;
	}
	return OK;
}

const FSCompletenessRunResult *FSCompletenessBaseline::shared_or_skip(const String &p_family) {
	const FSCompletenessRunResult &baseline = shared(p_family);
	if (baseline.outcome == "passed") {
		return &baseline;
	}
	Completeness::fs_completeness_skip(
			vformat("the shared type-completeness baseline for '%s' is unavailable", p_family).utf8().get_data());
	return nullptr;
}

int FSCompletenessBaseline::run_count() {
	CompletenessStore &shared_store = store();
	MutexLock lock(shared_store.mutex);
	return shared_store.baseline_run_count;
}

} // namespace FSTests
