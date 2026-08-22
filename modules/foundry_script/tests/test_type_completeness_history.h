/**************************************************************************/
/*  test_type_completeness_history.h                                      */
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

#include "fs_temporary_project_tree.h"
#include "fs_type_completeness_graph.h"
#include "fs_type_completeness_json.h"
#include "fs_type_completeness_manifest.h"
#include "fs_type_completeness_runner.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "tests/test_macros.h"

// Validator of the mined-history records under type_completeness/history and of the mutation
// recipes under type_completeness/mutations. Both are hand-written data whose every closed
// vocabulary lives in the data itself (history/schema.json) or in the rule catalog; this file is
// the only code that reads them, and every refusal names the file and the JSONPath it refused.

namespace FSTests {

static const String type_completeness_history_catalog_root = "modules/foundry_script/tests/type_completeness";
static const String type_completeness_history_directory = type_completeness_history_catalog_root.path_join("history");
static const String type_completeness_mutations_directory = type_completeness_history_catalog_root.path_join("mutations");

// Dimensions the runner judges for every case in addition to the family's own dimension ids
// (`fs_type_completeness_runner.cpp`, `ledger_dimension_is_known`).
static const char *type_completeness_runner_builtin_dimensions[] = {
	"output", "diagnostics", "runtime_status", "text_bytecode_parity", "diagnostic_severity"
};

// `git merge-base --is-ancestor <commit> HEAD`; exit 0 is an ancestor, exit 1 is not, and anything
// else is an unavailable probe, exactly the way the tracked-file probe is read.
using HistoryAncestryProbe = Error (*)(const String &p_repository_root, const String &p_commit,
		String &r_output, int &r_exit_code);

static Error default_history_ancestry_probe(const String &p_repository_root, const String &p_commit,
		String &r_output, int &r_exit_code) {
	if (OS::get_singleton() == nullptr) {
		return ERR_UNAVAILABLE;
	}
	List<String> arguments;
	arguments.push_back("-C");
	arguments.push_back(p_repository_root);
	arguments.push_back("merge-base");
	arguments.push_back("--is-ancestor");
	arguments.push_back(p_commit);
	arguments.push_back("HEAD");
	return OS::get_singleton()->execute("git", arguments, &r_output, &r_exit_code, true);
}

static Error default_history_tracked_file_probe(const String &p_repository_root, const String &p_path,
		String &r_output, int &r_exit_code) {
	if (OS::get_singleton() == nullptr) {
		return ERR_UNAVAILABLE;
	}
	List<String> arguments;
	arguments.push_back("-C");
	arguments.push_back(p_repository_root);
	arguments.push_back("ls-files");
	arguments.push_back("--error-unmatch");
	arguments.push_back("--");
	arguments.push_back(p_path);
	return OS::get_singleton()->execute("git", arguments, &r_output, &r_exit_code, true);
}

struct HistoryValidationContext {
	String history_directory;
	String catalog_root;
	String repository_root;
	FSCompletenessTrackedFileProbe tracked_file_probe = nullptr;
	HistoryAncestryProbe ancestry_probe = nullptr;
};

static bool history_is_safe_id(const String &p_id) {
	if (p_id.is_empty() || p_id.length() > 64) {
		return false;
	}
	for (int i = 0; i < p_id.length(); i++) {
		const char32_t c = p_id[i];
		const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
		if (!ok) {
			return false;
		}
	}
	return true;
}

static bool history_is_full_commit(const String &p_commit) {
	if (p_commit.length() != 40) {
		return false;
	}
	for (int i = 0; i < 40; i++) {
		const char32_t c = p_commit[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
			return false;
		}
	}
	return true;
}

static bool history_read_json_file(const String &p_path, Variant &r_data, Vector<String> &r_errors) {
	Error read_error = OK;
	const String source = FileAccess::get_file_as_string(p_path, &read_error);
	if (read_error != OK) {
		r_errors.push_back(vformat("%s: could not read file (error %d)", p_path, read_error));
		return false;
	}
	Vector<String> parse_errors;
	const Error parse_error = parse_type_completeness_json(source, String(), r_data, parse_errors);
	for (const String &error : parse_errors) {
		r_errors.push_back(vformat("%s: %s", p_path, error));
	}
	if (parse_error != OK || r_data.get_type() != Variant::DICTIONARY) {
		r_errors.push_back(vformat("%s: $ must be a JSON object", p_path));
		return false;
	}
	return true;
}

static Vector<String> history_string_array(const Dictionary &p_object, const String &p_key) {
	Vector<String> result;
	const Variant value = p_object.get(p_key, Variant());
	if (value.get_type() != Variant::ARRAY) {
		return result;
	}
	const Array array = value;
	for (int i = 0; i < array.size(); i++) {
		if (array[i].get_type() == Variant::STRING) {
			result.push_back(array[i]);
		}
	}
	return result;
}

static bool history_require_string(const Dictionary &p_object, const String &p_key, const String &p_file,
		const String &p_json_path, Vector<String> &r_errors, String &r_value) {
	const Variant value = p_object.get(p_key, Variant());
	if (value.get_type() != Variant::STRING || String(value).is_empty()) {
		r_errors.push_back(vformat("%s: %s.%s must be a non-empty string", p_file, p_json_path, p_key));
		return false;
	}
	r_value = value;
	return true;
}

static bool history_require_dictionary(const Dictionary &p_object, const String &p_key, const String &p_file,
		const String &p_json_path, Vector<String> &r_errors, Dictionary &r_value) {
	const Variant value = p_object.get(p_key, Variant());
	if (value.get_type() != Variant::DICTIONARY) {
		r_errors.push_back(vformat("%s: %s.%s must be an object", p_file, p_json_path, p_key));
		return false;
	}
	r_value = value;
	return true;
}

static bool history_require_json_integer(const Variant &p_value, int p_expected, const String &p_file,
		const String &p_json_path, Vector<String> &r_errors) {
	// The engine parser reads every JSON number as a FLOAT; an integer is one whose rendered form
	// carries no fraction, which is how the manifest loader reads schema_version too.
	if (p_value.get_type() != Variant::FLOAT && p_value.get_type() != Variant::INT) {
		r_errors.push_back(vformat("%s: %s must be a JSON integer", p_file, p_json_path));
		return false;
	}
	const double number = p_value;
	if (number != double(int64_t(number))) {
		r_errors.push_back(vformat("%s: %s must be a JSON integer", p_file, p_json_path));
		return false;
	}
	if (int64_t(number) != p_expected) {
		r_errors.push_back(vformat("%s: %s must be %d; got %d", p_file, p_json_path, p_expected, int(number)));
		return false;
	}
	return true;
}

static bool history_is_relative_repository_path(const String &p_path) {
	return !p_path.is_empty() && !p_path.is_absolute_path() && p_path.simplify_path() == p_path &&
			!p_path.contains("://") && !p_path.begins_with("../") && !p_path.contains("/../") &&
			!p_path.ends_with("/..") && p_path != "..";
}

// A repository-relative path that git reports as tracked. Exit 1 is "not tracked"; an unavailable
// git is warned about and accepted, the way the runner's permanent-test-path probe behaves.
static bool history_path_is_tracked(const HistoryValidationContext &p_context, const String &p_path,
		const String &p_file, const String &p_json_path, Vector<String> &r_errors) {
	if (!history_is_relative_repository_path(p_path)) {
		r_errors.push_back(vformat("%s: %s must be a repository-relative path without '..'", p_file, p_json_path));
		return false;
	}
	const String absolute = p_context.repository_root.path_join(p_path);
	if (!FileAccess::exists(absolute) || DirAccess::dir_exists_absolute(absolute)) {
		r_errors.push_back(vformat("%s: %s names '%s', which is not a file in the repository", p_file, p_json_path, p_path));
		return false;
	}
	const FSCompletenessTrackedFileProbe probe =
			p_context.tracked_file_probe == nullptr ? default_history_tracked_file_probe : p_context.tracked_file_probe;
	String output;
	int exit_code = -1;
	const Error probe_error = probe(p_context.repository_root, p_path, output, exit_code);
	if (probe_error != OK) {
		WARN_PRINT(vformat("Git tracked-file verification is unavailable for '%s' (error %d); accepting the existing file.",
				p_path, probe_error));
		return true;
	}
	if (exit_code == 1) {
		r_errors.push_back(vformat("%s: %s names '%s', which is not tracked by git", p_file, p_json_path, p_path));
		return false;
	}
	if (exit_code != 0) {
		WARN_PRINT(vformat("Git tracked-file verification is unavailable for '%s' (exit %d); accepting the existing file.",
				p_path, exit_code));
	}
	return true;
}

static bool history_commit_is_ancestor(const HistoryValidationContext &p_context, const String &p_commit,
		const String &p_file, const String &p_json_path, Vector<String> &r_errors) {
	const HistoryAncestryProbe probe =
			p_context.ancestry_probe == nullptr ? default_history_ancestry_probe : p_context.ancestry_probe;
	String output;
	int exit_code = -1;
	const Error probe_error = probe(p_context.repository_root, p_commit, output, exit_code);
	if (probe_error != OK) {
		WARN_PRINT(vformat("Git ancestry verification is unavailable for '%s' (error %d); accepting the recorded commit.",
				p_commit, probe_error));
		return true;
	}
	if (exit_code == 1) {
		r_errors.push_back(vformat("%s: %s commit '%s' is not an ancestor of HEAD", p_file, p_json_path, p_commit));
		return false;
	}
	if (exit_code != 0) {
		WARN_PRINT(vformat("Git ancestry verification is unavailable for '%s' (exit %d%s); accepting the recorded commit.",
				p_commit, exit_code, output.strip_edges().is_empty() ? String() : ": " + output.strip_edges()));
	}
	return true;
}

// One resolved family, shared by every record and recipe that names it.
struct HistoryFamilyResolution {
	bool loaded = false;
	FSCompletenessManifest manifest;
	FSCompletenessResolution resolution;
	Vector<String> errors;
};

struct HistoryCatalogState {
	String catalog_root;
	bool catalog_loaded = false;
	FSCompletenessCatalog catalog;
	Vector<String> catalog_errors;
	HashMap<String, HistoryFamilyResolution> families;
	HashSet<String> dimensions;

	const HistoryFamilyResolution &family(const String &p_family) {
		if (!families.has(p_family)) {
			HistoryFamilyResolution entry;
			if (!catalog_loaded) {
				catalog_errors.clear();
				catalog_loaded = catalog.load(catalog_root, catalog_errors) == OK;
				if (catalog_loaded) {
					for (const char *dimension : type_completeness_runner_builtin_dimensions) {
						dimensions.insert(dimension);
					}
				}
			}
			if (!catalog_loaded) {
				entry.errors = catalog_errors;
			} else if (!history_is_safe_id(p_family)) {
				entry.errors.push_back(vformat("family '%s' does not match ^[a-z0-9_]{1,64}$", p_family));
			} else {
				const String manifest_path = catalog_root.path_join("rules").path_join(p_family + ".json");
				if (FSCompletenessManifest::load(manifest_path, entry.manifest, entry.errors) == OK &&
						validate_manifest_vocabulary(entry.manifest, catalog, entry.errors) == OK &&
						FSCompletenessGraph::resolve(entry.manifest, catalog, entry.resolution, entry.errors) == OK) {
					entry.loaded = true;
					for (const FSCompletenessResolvedCell &cell : entry.resolution.cells) {
						for (const KeyValue<String, FSCompletenessResolvedDimension> &dimension : cell.dimensions) {
							dimensions.insert(dimension.key);
						}
					}
				}
			}
			families.insert(p_family, entry);
		}
		return families[p_family];
	}
};

// Resolves p_coordinates through the family graph. Coordinates may omit the surface axis, in which
// case every surface of the family domain must resolve; each resolved cell must carry every
// dimension in p_required_dimensions.
static bool history_coordinates_resolve(HistoryCatalogState &p_state, const String &p_family,
		const Dictionary &p_coordinates, const Vector<String> &p_required_dimensions, const String &p_file,
		const String &p_json_path, Vector<String> &r_errors) {
	const HistoryFamilyResolution &family = p_state.family(p_family);
	if (!family.loaded) {
		r_errors.push_back(vformat("%s: %s.family '%s' cannot be resolved: %s", p_file, p_json_path, p_family,
				String(" | ").join(family.errors)));
		return false;
	}
	for (const KeyValue<Variant, Variant> &axis : p_coordinates) {
		if (axis.value.get_type() != Variant::STRING) {
			r_errors.push_back(vformat("%s: %s.case_coordinates.%s must be a string", p_file, p_json_path, String(axis.key)));
			return false;
		}
	}
	Vector<Dictionary> probes;
	if (p_coordinates.has("surface") || !family.manifest.domain.has("surface")) {
		probes.push_back(p_coordinates);
	} else {
		for (const String &surface : family.manifest.domain["surface"]) {
			Dictionary with_surface = p_coordinates.duplicate();
			with_surface["surface"] = surface;
			probes.push_back(with_surface);
		}
	}
	bool ok = true;
	for (const Dictionary &probe : probes) {
		int matches = 0;
		const FSCompletenessResolvedCell *cell = family.resolution.find_cell_by_coordinates(probe, matches);
		if (cell == nullptr || matches != 1) {
			r_errors.push_back(vformat("%s: %s.case_coordinates %s resolve to %d cells of family '%s'; expected exactly one",
					p_file, p_json_path, JSON::stringify(probe), matches, p_family));
			ok = false;
			continue;
		}
		for (const String &dimension : p_required_dimensions) {
			if (cell->find_dimension(dimension) == nullptr) {
				r_errors.push_back(vformat("%s: %s.required_dimensions names '%s', which cell %s does not cover",
						p_file, p_json_path, dimension, cell->case_id));
				ok = false;
			}
		}
	}
	return ok;
}

struct HistorySchema {
	HashSet<String> source_kinds;
	HashSet<String> reverification_results;
	HashSet<String> dispositions;
	HashSet<String> deferral_reasons;
	Vector<String> required_members;
};

static bool history_load_schema(const String &p_directory, HistorySchema &r_schema, Vector<String> &r_errors) {
	const String path = p_directory.path_join("schema.json");
	Variant data;
	if (!history_read_json_file(path, data, r_errors)) {
		return false;
	}
	const Dictionary schema = data;
	bool ok = history_require_json_integer(schema.get("schema_version", Variant()), 1, path, "$.schema_version", r_errors);
	struct Vocabulary {
		const char *key;
		HashSet<String> *target;
	};
	Vocabulary vocabularies[] = {
		{ "source_kinds", &r_schema.source_kinds },
		{ "reverification_results", &r_schema.reverification_results },
		{ "dispositions", &r_schema.dispositions },
		{ "deferral_reasons", &r_schema.deferral_reasons },
	};
	for (const Vocabulary &vocabulary : vocabularies) {
		const Vector<String> values = history_string_array(schema, vocabulary.key);
		if (values.is_empty()) {
			r_errors.push_back(vformat("%s: $.%s must be a non-empty array of strings", path, vocabulary.key));
			ok = false;
		}
		for (const String &value : values) {
			vocabulary.target->insert(value);
		}
	}
	r_schema.required_members = history_string_array(schema, "required_members");
	if (r_schema.required_members.is_empty()) {
		r_errors.push_back(vformat("%s: $.required_members must be a non-empty array of strings", path));
		ok = false;
	}
	return ok;
}

static bool history_validate_record(const HistoryValidationContext &p_context, const HistorySchema &p_schema,
		HistoryCatalogState &p_state, const String &p_file_name, Vector<String> &r_errors) {
	const int errors_before = r_errors.size();
	const String stem = p_file_name.get_basename();
	const String path = p_context.history_directory.path_join(p_file_name);
	if (!history_is_safe_id(stem)) {
		r_errors.push_back(vformat("%s: file name stem does not match ^[a-z0-9_]{1,64}$", path));
		return false;
	}
	Variant data;
	if (!history_read_json_file(path, data, r_errors)) {
		return false;
	}
	const Dictionary record = data;
	bool ok = history_require_json_integer(record.get("schema_version", Variant()), 1, path, "$.schema_version", r_errors);
	for (const String &member : p_schema.required_members) {
		if (!record.has(member)) {
			r_errors.push_back(vformat("%s: $.%s is required", path, member));
			ok = false;
		}
	}
	String item_id;
	if (history_require_string(record, "item_id", path, "$", r_errors, item_id)) {
		if (!history_is_safe_id(item_id)) {
			r_errors.push_back(vformat("%s: $.item_id does not match ^[a-z0-9_]{1,64}$", path));
			ok = false;
		} else if (item_id != stem) {
			r_errors.push_back(vformat("%s: $.item_id '%s' does not equal the file name stem '%s'", path, item_id, stem));
			ok = false;
		}
	} else {
		ok = false;
	}

	Dictionary source;
	if (history_require_dictionary(record, "source", path, "$", r_errors, source)) {
		String kind;
		if (history_require_string(source, "kind", path, "$.source", r_errors, kind) && !p_schema.source_kinds.has(kind)) {
			r_errors.push_back(vformat("%s: $.source.kind '%s' is not in schema.json source_kinds", path, kind));
			ok = false;
		}
		const Variant number = source.get("number", Variant());
		if (number.get_type() != Variant::FLOAT && number.get_type() != Variant::INT) {
			r_errors.push_back(vformat("%s: $.source.number must be a number", path));
			ok = false;
		}
		String text;
		ok = history_require_string(source, "url", path, "$.source", r_errors, text) && ok;
		ok = history_require_string(source, "title", path, "$.source", r_errors, text) && ok;
	} else {
		ok = false;
	}

	String result;
	String evidence_path;
	Dictionary reverification;
	if (history_require_dictionary(record, "reverification", path, "$", r_errors, reverification)) {
		String commit;
		if (history_require_string(reverification, "commit", path, "$.reverification", r_errors, commit)) {
			if (!history_is_full_commit(commit)) {
				r_errors.push_back(vformat("%s: $.reverification.commit must be a full 40-hex lowercase commit", path));
				ok = false;
			} else if (!history_commit_is_ancestor(p_context, commit, path, "$.reverification", r_errors)) {
				ok = false;
			}
		} else {
			ok = false;
		}
		if (history_require_string(reverification, "result", path, "$.reverification", r_errors, result)) {
			if (!p_schema.reverification_results.has(result)) {
				r_errors.push_back(vformat("%s: $.reverification.result '%s' is not in schema.json reverification_results",
						path, result));
				ok = false;
			}
		} else {
			ok = false;
		}
		if (history_require_string(reverification, "evidence_path", path, "$.reverification", r_errors, evidence_path)) {
			ok = history_path_is_tracked(p_context, evidence_path, path, "$.reverification.evidence_path", r_errors) && ok;
		} else {
			ok = false;
		}
		String probe;
		ok = history_require_string(reverification, "probe", path, "$.reverification", r_errors, probe) && ok;
	} else {
		ok = false;
	}

	Dictionary mapping;
	String mapping_family;
	if (history_require_dictionary(record, "mapping", path, "$", r_errors, mapping)) {
		ok = history_require_string(mapping, "family", path, "$.mapping", r_errors, mapping_family) && ok;
	} else {
		ok = false;
	}

	String disposition;
	if (!history_require_string(record, "disposition", path, "$", r_errors, disposition)) {
		return false;
	}
	if (!p_schema.dispositions.has(disposition)) {
		r_errors.push_back(vformat("%s: $.disposition '%s' is not in schema.json dispositions", path, disposition));
		return false;
	}
	String rationale;
	ok = history_require_string(record, "rationale", path, "$", r_errors, rationale) && ok;

	if (disposition == "seeded") {
		if (result == "premise_false" || result == "fixed") {
			r_errors.push_back(vformat("%s: $.disposition 'seeded' is not allowed with $.reverification.result '%s'", path, result));
			ok = false;
		}
		Dictionary seed;
		if (history_require_dictionary(record, "seed", path, "$", r_errors, seed)) {
			String seed_family;
			Dictionary coordinates;
			if (history_require_string(seed, "family", path, "$.seed", r_errors, seed_family) &&
					history_require_dictionary(seed, "case_coordinates", path, "$.seed", r_errors, coordinates)) {
				if (seed_family != mapping_family) {
					r_errors.push_back(vformat("%s: $.seed.family '%s' differs from $.mapping.family '%s'", path, seed_family, mapping_family));
					ok = false;
				}
				ok = history_coordinates_resolve(p_state, seed_family, coordinates,
							 history_string_array(seed, "required_dimensions"), path, "$.seed", r_errors) &&
						ok;
			} else {
				ok = false;
			}
		} else {
			ok = false;
		}
	} else if (disposition == "expanded") {
		Dictionary coordinates;
		if (history_require_dictionary(mapping, "case_coordinates", path, "$.mapping", r_errors, coordinates)) {
			const Vector<String> required = history_string_array(mapping, "required_dimensions");
			if (required.is_empty()) {
				r_errors.push_back(vformat("%s: $.mapping.required_dimensions must name at least one dimension", path));
				ok = false;
			}
			ok = history_coordinates_resolve(p_state, mapping_family, coordinates, required, path, "$.mapping", r_errors) && ok;
		} else {
			ok = false;
		}
	} else if (disposition == "discarded") {
		if (result != "fixed" && result != "premise_false") {
			r_errors.push_back(vformat("%s: $.disposition 'discarded' requires $.reverification.result 'fixed' or 'premise_false'; got '%s'",
					path, result));
			ok = false;
		}
	} else if (disposition == "deferred_to_family") {
		Dictionary deferral;
		if (history_require_dictionary(record, "deferral", path, "$", r_errors, deferral)) {
			String family;
			if (history_require_string(deferral, "family", path, "$.deferral", r_errors, family)) {
				if (!history_is_safe_id(family)) {
					r_errors.push_back(vformat("%s: $.deferral.family does not match ^[a-z0-9_]{1,64}$", path));
					ok = false;
				} else if (family != mapping_family) {
					r_errors.push_back(vformat("%s: $.deferral.family '%s' differs from $.mapping.family '%s'", path, family, mapping_family));
					ok = false;
				}
			} else {
				ok = false;
			}
			String reason;
			if (history_require_string(deferral, "reason", path, "$.deferral", r_errors, reason) &&
					!p_schema.deferral_reasons.has(reason)) {
				r_errors.push_back(vformat("%s: $.deferral.reason '%s' is not in schema.json deferral_reasons", path, reason));
				ok = false;
			}
		} else {
			ok = false;
		}
	} else {
		// Every disposition schema.json may list is handled above; one it lists that this validator
		// does not know cannot be accepted, or a record could carry a disposition nobody checks.
		r_errors.push_back(vformat("%s: $.disposition '%s' is listed by schema.json but has no validation rule", path, disposition));
		ok = false;
	}
	return ok && r_errors.size() == errors_before;
}

static bool history_list_json_files(const String &p_directory, Vector<String> &r_files, Vector<String> &r_errors) {
	Ref<DirAccess> directory = DirAccess::open(p_directory);
	if (directory.is_null()) {
		r_errors.push_back(vformat("%s: cannot open directory", p_directory));
		return false;
	}
	if (directory->list_dir_begin() != OK) {
		r_errors.push_back(vformat("%s: cannot list directory", p_directory));
		return false;
	}
	for (String entry = directory->get_next(); !entry.is_empty(); entry = directory->get_next()) {
		if (directory->current_is_dir() || entry.get_extension() != "json") {
			continue;
		}
		r_files.push_back(entry);
	}
	directory->list_dir_end();
	r_files.sort();
	return true;
}

static bool validate_history_directory(const HistoryValidationContext &p_context, Vector<String> &r_errors) {
	HistorySchema schema;
	if (!history_load_schema(p_context.history_directory, schema, r_errors)) {
		return false;
	}
	Vector<String> files;
	if (!history_list_json_files(p_context.history_directory, files, r_errors)) {
		return false;
	}
	HistoryCatalogState state;
	state.catalog_root = p_context.catalog_root;
	bool ok = true;
	int record_count = 0;
	for (const String &file : files) {
		if (file == "schema.json") {
			continue;
		}
		record_count++;
		ok = history_validate_record(p_context, schema, state, file, r_errors) && ok;
	}
	if (record_count == 0) {
		r_errors.push_back(vformat("%s: holds no history record", p_context.history_directory));
		ok = false;
	}
	return ok;
}

// Mutation recipes: `schema_version` is a JSON integer, `expected_detectors` is non-empty, every
// detector names a family whose graph resolves its full coordinates to exactly one cell, and a
// dimension the family or the runner judges. The patch must sit at patches/<recipe_id>.patch.
static bool validate_mutation_recipe(HistoryCatalogState &p_state, const String &p_directory,
		const String &p_file_name, Vector<String> &r_errors) {
	const int errors_before = r_errors.size();
	const String stem = p_file_name.get_basename();
	const String path = p_directory.path_join(p_file_name);
	if (!history_is_safe_id(stem)) {
		r_errors.push_back(vformat("%s: file name stem does not match ^[a-z0-9_]{1,64}$", path));
		return false;
	}
	Variant data;
	if (!history_read_json_file(path, data, r_errors)) {
		return false;
	}
	const Dictionary recipe = data;
	bool ok = history_require_json_integer(recipe.get("schema_version", Variant()), 1, path, "$.schema_version", r_errors);
	String recipe_id;
	if (history_require_string(recipe, "recipe_id", path, "$", r_errors, recipe_id)) {
		if (!history_is_safe_id(recipe_id)) {
			r_errors.push_back(vformat("%s: $.recipe_id does not match ^[a-z0-9_]{1,64}$", path));
			ok = false;
		} else if (recipe_id != stem) {
			r_errors.push_back(vformat("%s: $.recipe_id '%s' does not equal the file name stem '%s'", path, recipe_id, stem));
			ok = false;
		} else if (!FileAccess::exists(p_directory.path_join("patches").path_join(recipe_id + ".patch"))) {
			r_errors.push_back(vformat("%s: patches/%s.patch does not exist", path, recipe_id));
			ok = false;
		}
	} else {
		ok = false;
	}
	String text;
	ok = history_require_string(recipe, "description", path, "$", r_errors, text) && ok;
	if (history_require_string(recipe, "escape_class", path, "$", r_errors, text) && !history_is_safe_id(text)) {
		r_errors.push_back(vformat("%s: $.escape_class does not match ^[a-z0-9_]{1,64}$", path));
		ok = false;
	}
	const Variant detectors_value = recipe.get("expected_detectors", Variant());
	if (detectors_value.get_type() != Variant::ARRAY || Array(detectors_value).is_empty()) {
		r_errors.push_back(vformat("%s: $.expected_detectors must be a non-empty array", path));
		return false;
	}
	const Array detectors = detectors_value;
	for (int i = 0; i < detectors.size(); i++) {
		const String json_path = vformat("$.expected_detectors[%d]", i);
		if (detectors[i].get_type() != Variant::DICTIONARY) {
			r_errors.push_back(vformat("%s: %s must be an object", path, json_path));
			ok = false;
			continue;
		}
		const Dictionary detector = detectors[i];
		String family;
		String dimension;
		Dictionary coordinates;
		if (!history_require_string(detector, "family", path, json_path, r_errors, family) ||
				!history_require_string(detector, "dimension", path, json_path, r_errors, dimension) ||
				!history_require_dictionary(detector, "case_coordinates", path, json_path, r_errors, coordinates)) {
			ok = false;
			continue;
		}
		const HistoryFamilyResolution &resolved = p_state.family(family);
		if (!resolved.loaded) {
			r_errors.push_back(vformat("%s: %s.family '%s' cannot be resolved: %s", path, json_path, family,
					String(" | ").join(resolved.errors)));
			ok = false;
			continue;
		}
		if (coordinates.size() != resolved.manifest.domain_axis_order.size()) {
			r_errors.push_back(vformat("%s: %s.case_coordinates must name every domain axis of '%s'", path, json_path, family));
			ok = false;
			continue;
		}
		// A runner built-in is judged for every cell; a catalog dimension only where the cell carries it,
		// so a detector naming one the cell lacks could never be detected.
		bool builtin = false;
		for (const char *candidate : type_completeness_runner_builtin_dimensions) {
			builtin = builtin || dimension == candidate;
		}
		Vector<String> required;
		if (!builtin) {
			required.push_back(dimension);
		}
		ok = history_coordinates_resolve(p_state, family, coordinates, required, path, json_path, r_errors) && ok;
	}
	return ok && r_errors.size() == errors_before;
}

static bool validate_mutation_directory(const String &p_catalog_root, const String &p_directory, Vector<String> &r_errors) {
	const int errors_before = r_errors.size();
	Vector<String> files;
	if (!history_list_json_files(p_directory, files, r_errors)) {
		return false;
	}
	HistoryCatalogState state;
	state.catalog_root = p_catalog_root;
	bool ok = true;
	HashSet<String> recipe_ids;
	for (const String &file : files) {
		if (file == "shards.json") {
			continue;
		}
		ok = validate_mutation_recipe(state, p_directory, file, r_errors) && ok;
		recipe_ids.insert(file.get_basename());
	}
	if (recipe_ids.is_empty()) {
		r_errors.push_back(vformat("%s: holds no mutation recipe", p_directory));
		return false;
	}
	Variant shards_data;
	const String shards_path = p_directory.path_join("shards.json");
	if (!history_read_json_file(shards_path, shards_data, r_errors)) {
		return false;
	}
	const Dictionary shards = shards_data;
	ok = history_require_json_integer(shards.get("schema_version", Variant()), 1, shards_path, "$.schema_version", r_errors) && ok;
	const Variant shard_list = shards.get("shards", Variant());
	if (shard_list.get_type() != Variant::ARRAY || Array(shard_list).is_empty()) {
		r_errors.push_back(vformat("%s: $.shards must be a non-empty array", shards_path));
		return false;
	}
	HashSet<String> scheduled;
	const Array shard_array = shard_list;
	for (int i = 0; i < shard_array.size(); i++) {
		const String json_path = vformat("$.shards[%d]", i);
		if (shard_array[i].get_type() != Variant::DICTIONARY) {
			r_errors.push_back(vformat("%s: %s must be an object", shards_path, json_path));
			ok = false;
			continue;
		}
		const Dictionary shard = shard_array[i];
		String shard_id;
		if (history_require_string(shard, "shard_id", shards_path, json_path, r_errors, shard_id) && !history_is_safe_id(shard_id)) {
			r_errors.push_back(vformat("%s: %s.shard_id does not match ^[a-z0-9_]{1,64}$", shards_path, json_path));
			ok = false;
		}
		const Variant budget = shard.get("budget_seconds", Variant());
		if ((budget.get_type() != Variant::FLOAT && budget.get_type() != Variant::INT) || double(budget) <= 0 || double(budget) > 1800) {
			r_errors.push_back(vformat("%s: %s.budget_seconds must be within 1..1800", shards_path, json_path));
			ok = false;
		}
		const Vector<String> recipes = history_string_array(shard, "recipes");
		if (recipes.is_empty()) {
			r_errors.push_back(vformat("%s: %s.recipes must be a non-empty array", shards_path, json_path));
			ok = false;
		}
		for (const String &recipe : recipes) {
			if (!recipe_ids.has(recipe)) {
				r_errors.push_back(vformat("%s: %s.recipes names unknown recipe '%s'", shards_path, json_path, recipe));
				ok = false;
			}
			scheduled.insert(recipe);
		}
	}
	for (const String &recipe_id : recipe_ids) {
		if (!scheduled.has(recipe_id)) {
			r_errors.push_back(vformat("%s: recipe '%s' is not scheduled by any shard", shards_path, recipe_id));
			ok = false;
		}
	}
	return ok && r_errors.size() == errors_before;
}

// ---------------------------------------------------------------------------------------------
// Test fixtures.

static Error history_test_tracked_probe_ok(const String &, const String &, String &r_output, int &r_exit_code) {
	r_output = String();
	r_exit_code = 0;
	return OK;
}

static Error history_test_tracked_probe_untracked(const String &, const String &, String &r_output, int &r_exit_code) {
	r_output = String();
	r_exit_code = 1;
	return OK;
}

static Error history_test_ancestry_probe_ok(const String &, const String &, String &r_output, int &r_exit_code) {
	r_output = String();
	r_exit_code = 0;
	return OK;
}

static Error history_test_ancestry_probe_not_ancestor(const String &, const String &, String &r_output, int &r_exit_code) {
	r_output = String();
	r_exit_code = 1;
	return OK;
}

static String history_repository_root() {
	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	return filesystem.is_null() ? String(".") : filesystem->get_current_dir();
}

static String history_test_schema_text() {
	return FileAccess::get_file_as_string(type_completeness_history_directory.path_join("schema.json"));
}

// A record that validates: a still-failing union gap seeded at coordinates the tracked manifest
// resolves on both surfaces. Callers rewrite members to produce each refusal.
static String history_test_record_text(const String &p_item_id, const String &p_result, const String &p_disposition,
		const String &p_extra_members) {
	return vformat(R"JSON({
  "schema_version": 1,
  "item_id": "%s",
  "source": { "kind": "issue", "number": 1, "url": "https://example.invalid/1", "title": "Fixture" },
  "reverification": {
    "commit": "ea7eb0e37b35cc72f1ef5ef510234575052997e3",
    "result": "%s",
    "evidence_path": "modules/foundry_script/tests/type_completeness/history/schema.json",
    "probe": "fixture"
  },
  "mapping": { "family": "union_destination_membership" },
  "disposition": "%s",
  "rationale": "fixture"%s
}
)JSON",
			p_item_id, p_result, p_disposition, p_extra_members);
}

static const char *history_test_seed_members = R"JSON(,
  "seed": {
    "family": "union_destination_membership",
    "case_coordinates": { "destination": "union", "source_proof": "gradual", "boundary": "argument_binding" },
    "required_dimensions": ["analysis", "runtime_obligation"]
  })JSON";

static HistoryValidationContext history_test_context(const TemporaryProjectTree &p_tree) {
	HistoryValidationContext context;
	context.history_directory = p_tree.root.path_join("history");
	context.catalog_root = type_completeness_history_catalog_root;
	context.repository_root = history_repository_root();
	context.tracked_file_probe = history_test_tracked_probe_ok;
	context.ancestry_probe = history_test_ancestry_probe_ok;
	return context;
}

static bool history_errors_mention(const Vector<String> &p_errors, const String &p_needle) {
	for (const String &error : p_errors) {
		if (error.contains(p_needle)) {
			return true;
		}
	}
	return false;
}

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness][History]") {
	TEST_CASE("TypeCompleteness History tracked records validate against the tracked catalog") {
		HistoryValidationContext context;
		context.history_directory = type_completeness_history_directory;
		context.catalog_root = type_completeness_history_catalog_root;
		context.repository_root = history_repository_root();
		Vector<String> errors;
		CHECK_MESSAGE(validate_history_directory(context, errors), String(" | ").join(errors));
	}

	TEST_CASE("TypeCompleteness History no fixed or premise_false record is seeded") {
		Vector<String> files;
		Vector<String> errors;
		REQUIRE_MESSAGE(history_list_json_files(type_completeness_history_directory, files, errors), String(" | ").join(errors));
		int records = 0;
		for (const String &file : files) {
			if (file == "schema.json") {
				continue;
			}
			Variant data;
			REQUIRE_MESSAGE(history_read_json_file(type_completeness_history_directory.path_join(file), data, errors),
					String(" | ").join(errors));
			const Dictionary record = data;
			const Dictionary reverification = record.get("reverification", Dictionary());
			const String result = reverification.get("result", String());
			const String disposition = record.get("disposition", String());
			records++;
			if (result == "fixed" || result == "premise_false") {
				CHECK_MESSAGE(disposition != "seeded", vformat("%s is %s but seeded", file, result));
			}
			if (disposition == "seeded") {
				CHECK_EQ(result, "still_failing");
			}
		}
		CHECK_GT(records, 0);
	}

	TEST_CASE("TypeCompleteness History a seeded record resolves on both surfaces with its required dimensions") {
		TemporaryProjectTree tree("type_completeness_history_seeded");
		REQUIRE(tree.is_valid());
		tree.write_file("history/schema.json", history_test_schema_text());
		tree.write_file("history/seed_fixture.json",
				history_test_record_text("seed_fixture", "still_failing", "seeded", history_test_seed_members));
		Vector<String> errors;
		CHECK_MESSAGE(validate_history_directory(history_test_context(tree), errors), String(" | ").join(errors));

		// The same coordinates resolve to exactly one text and one bytecode cell of the tracked manifest,
		// and each covers the dimensions the seed requires.
		HistoryCatalogState state;
		state.catalog_root = type_completeness_history_catalog_root;
		const HistoryFamilyResolution &family = state.family("union_destination_membership");
		REQUIRE_MESSAGE(family.loaded, String(" | ").join(family.errors));
		for (const String &surface : { String("text"), String("bytecode") }) {
			Dictionary coordinates;
			coordinates["destination"] = "union";
			coordinates["source_proof"] = "gradual";
			coordinates["boundary"] = "argument_binding";
			coordinates["surface"] = surface;
			int matches = 0;
			const FSCompletenessResolvedCell *cell = family.resolution.find_cell_by_coordinates(coordinates, matches);
			REQUIRE_MESSAGE(cell != nullptr, surface);
			CHECK_EQ(matches, 1);
			CHECK(cell->find_dimension("analysis") != nullptr);
			CHECK(cell->find_dimension("runtime_obligation") != nullptr);
		}
	}

	TEST_CASE("TypeCompleteness History rejects a reverification result outside the vocabulary") {
		TemporaryProjectTree tree("type_completeness_history_bad_result");
		REQUIRE(tree.is_valid());
		tree.write_file("history/schema.json", history_test_schema_text());
		tree.write_file("history/bad_result.json",
				history_test_record_text("bad_result", "maybe", "deferred_to_family",
						R"JSON(,
  "deferral": { "family": "union_destination_membership", "reason": "domain_axis_missing" })JSON"));
		Vector<String> errors;
		CHECK_FALSE(validate_history_directory(history_test_context(tree), errors));
		CHECK(history_errors_mention(errors, "bad_result.json"));
		CHECK(history_errors_mention(errors, "$.reverification.result"));
	}

	TEST_CASE("TypeCompleteness History rejects a disposition outside the vocabulary") {
		TemporaryProjectTree tree("type_completeness_history_bad_disposition");
		REQUIRE(tree.is_valid());
		tree.write_file("history/schema.json", history_test_schema_text());
		tree.write_file("history/bad_disposition.json",
				history_test_record_text("bad_disposition", "still_failing", "parked", ""));
		Vector<String> errors;
		CHECK_FALSE(validate_history_directory(history_test_context(tree), errors));
		CHECK(history_errors_mention(errors, "$.disposition"));
		CHECK(history_errors_mention(errors, "parked"));
	}

	TEST_CASE("TypeCompleteness History rejects a seed whose coordinates resolve to no cell") {
		TemporaryProjectTree tree("type_completeness_history_seed_unresolved");
		REQUIRE(tree.is_valid());
		tree.write_file("history/schema.json", history_test_schema_text());
		tree.write_file("history/seed_unresolved.json",
				history_test_record_text("seed_unresolved", "still_failing", "seeded",
						R"JSON(,
  "seed": {
    "family": "union_destination_membership",
    "case_coordinates": { "destination": "union", "source_proof": "hand_written_case_id", "boundary": "argument_binding" },
    "required_dimensions": ["analysis"]
  })JSON"));
		Vector<String> errors;
		CHECK_FALSE(validate_history_directory(history_test_context(tree), errors));
		CHECK(history_errors_mention(errors, "$.seed.case_coordinates"));
	}

	TEST_CASE("TypeCompleteness History rejects a seed that lacks a required dimension") {
		TemporaryProjectTree tree("type_completeness_history_seed_dimension");
		REQUIRE(tree.is_valid());
		tree.write_file("history/schema.json", history_test_schema_text());
		tree.write_file("history/seed_dimension.json",
				history_test_record_text("seed_dimension", "still_failing", "seeded",
						R"JSON(,
  "seed": {
    "family": "union_destination_membership",
    "case_coordinates": { "destination": "plain", "source_proof": "static_member", "boundary": "argument_binding" },
    "required_dimensions": ["runtime_obligation"]
  })JSON"));
		Vector<String> errors;
		CHECK_FALSE(validate_history_directory(history_test_context(tree), errors));
		CHECK(history_errors_mention(errors, "$.seed.required_dimensions"));
	}

	TEST_CASE("TypeCompleteness History rejects a discarded record that still fails") {
		TemporaryProjectTree tree("type_completeness_history_discarded_failing");
		REQUIRE(tree.is_valid());
		tree.write_file("history/schema.json", history_test_schema_text());
		tree.write_file("history/discarded_failing.json",
				history_test_record_text("discarded_failing", "still_failing", "discarded", ""));
		Vector<String> errors;
		CHECK_FALSE(validate_history_directory(history_test_context(tree), errors));
		CHECK(history_errors_mention(errors, "'discarded' requires"));

		// The same record is accepted once the result is one the disposition allows.
		tree.write_file("history/discarded_failing.json",
				history_test_record_text("discarded_failing", "premise_false", "discarded", ""));
		errors.clear();
		CHECK_MESSAGE(validate_history_directory(history_test_context(tree), errors), String(" | ").join(errors));
	}

	TEST_CASE("TypeCompleteness History rejects a discarded record whose evidence is not tracked") {
		TemporaryProjectTree tree("type_completeness_history_discarded_untracked");
		REQUIRE(tree.is_valid());
		tree.write_file("history/schema.json", history_test_schema_text());
		tree.write_file("history/discarded_untracked.json",
				history_test_record_text("discarded_untracked", "fixed", "discarded", ""));
		HistoryValidationContext context = history_test_context(tree);
		context.tracked_file_probe = history_test_tracked_probe_untracked;
		Vector<String> errors;
		CHECK_FALSE(validate_history_directory(context, errors));
		CHECK(history_errors_mention(errors, "$.reverification.evidence_path"));
		CHECK(history_errors_mention(errors, "not tracked"));

		// An evidence path that escapes the repository is refused before git is asked.
		String escaping = history_test_record_text("discarded_untracked", "fixed", "discarded", "");
		escaping = escaping.replace("modules/foundry_script/tests/type_completeness/history/schema.json", "../outside.json");
		tree.write_file("history/discarded_untracked.json", escaping);
		errors.clear();
		CHECK_FALSE(validate_history_directory(history_test_context(tree), errors));
		CHECK(history_errors_mention(errors, "without '..'"));
	}

	TEST_CASE("TypeCompleteness History rejects seeding a non-reproducing or fixed claim") {
		for (const String &result : { String("premise_false"), String("fixed") }) {
			TemporaryProjectTree tree("type_completeness_history_seeded_" + result);
			REQUIRE(tree.is_valid());
			tree.write_file("history/schema.json", history_test_schema_text());
			tree.write_file("history/seeded_claim.json",
					history_test_record_text("seeded_claim", result, "seeded", history_test_seed_members));
			Vector<String> errors;
			CHECK_FALSE(validate_history_directory(history_test_context(tree), errors));
			CHECK_MESSAGE(history_errors_mention(errors, "'seeded' is not allowed"), String(" | ").join(errors));
		}
	}

	TEST_CASE("TypeCompleteness History rejects a reverification commit that is not an ancestor of HEAD") {
		TemporaryProjectTree tree("type_completeness_history_stale_commit");
		REQUIRE(tree.is_valid());
		tree.write_file("history/schema.json", history_test_schema_text());
		tree.write_file("history/stale_commit.json",
				history_test_record_text("stale_commit", "still_failing", "seeded", history_test_seed_members));
		HistoryValidationContext context = history_test_context(tree);
		context.ancestry_probe = history_test_ancestry_probe_not_ancestor;
		Vector<String> errors;
		CHECK_FALSE(validate_history_directory(context, errors));
		CHECK(history_errors_mention(errors, "not an ancestor of HEAD"));

		// A short or upper-case commit never reaches git at all.
		String short_commit = history_test_record_text("stale_commit", "still_failing", "seeded", history_test_seed_members);
		short_commit = short_commit.replace("ea7eb0e37b35cc72f1ef5ef510234575052997e3", "ea7eb0e37b");
		tree.write_file("history/stale_commit.json", short_commit);
		errors.clear();
		CHECK_FALSE(validate_history_directory(history_test_context(tree), errors));
		CHECK(history_errors_mention(errors, "full 40-hex"));
	}

	TEST_CASE("TypeCompleteness History rejects an id that does not match its file name or grammar") {
		TemporaryProjectTree tree("type_completeness_history_bad_id");
		REQUIRE(tree.is_valid());
		tree.write_file("history/schema.json", history_test_schema_text());
		tree.write_file("history/other_name.json",
				history_test_record_text("wrong_id", "still_failing", "seeded", history_test_seed_members));
		Vector<String> errors;
		CHECK_FALSE(validate_history_directory(history_test_context(tree), errors));
		CHECK(history_errors_mention(errors, "does not equal the file name stem"));

		tree.write_file("history/Upper-Case.json",
				history_test_record_text("Upper-Case", "still_failing", "seeded", history_test_seed_members));
		errors.clear();
		CHECK_FALSE(validate_history_directory(history_test_context(tree), errors));
		CHECK(history_errors_mention(errors, "Upper-Case.json: file name stem"));
	}

	TEST_CASE("TypeCompleteness History rejects duplicate members and a float schema version") {
		TemporaryProjectTree tree("type_completeness_history_duplicates");
		REQUIRE(tree.is_valid());
		tree.write_file("history/schema.json", history_test_schema_text());
		String duplicated = history_test_record_text("duplicated", "still_failing", "seeded", history_test_seed_members);
		duplicated = duplicated.replace("\"rationale\": \"fixture\"", "\"rationale\": \"fixture\",\n  \"rationale\": \"again\"");
		tree.write_file("history/duplicated.json", duplicated);
		Vector<String> errors;
		CHECK_FALSE(validate_history_directory(history_test_context(tree), errors));
		CHECK(history_errors_mention(errors, "duplicated.json"));

		String fractional = history_test_record_text("duplicated", "still_failing", "seeded", history_test_seed_members);
		fractional = fractional.replace("\"schema_version\": 1,", "\"schema_version\": 1.5,");
		tree.write_file("history/duplicated.json", fractional);
		errors.clear();
		CHECK_FALSE(validate_history_directory(history_test_context(tree), errors));
		CHECK(history_errors_mention(errors, "$.schema_version must be a JSON integer"));
	}

	TEST_CASE("TypeCompleteness History rejects a deferral outside its vocabulary or family") {
		TemporaryProjectTree tree("type_completeness_history_deferral");
		REQUIRE(tree.is_valid());
		tree.write_file("history/schema.json", history_test_schema_text());
		tree.write_file("history/deferred.json",
				history_test_record_text("deferred", "still_failing", "deferred_to_family",
						R"JSON(,
  "deferral": { "family": "union_destination_membership", "reason": "because" })JSON"));
		Vector<String> errors;
		CHECK_FALSE(validate_history_directory(history_test_context(tree), errors));
		CHECK(history_errors_mention(errors, "$.deferral.reason"));

		tree.write_file("history/deferred.json",
				history_test_record_text("deferred", "still_failing", "deferred_to_family",
						R"JSON(,
  "deferral": { "family": "some_other_family", "reason": "adapter_not_registered" })JSON"));
		errors.clear();
		CHECK_FALSE(validate_history_directory(history_test_context(tree), errors));
		CHECK(history_errors_mention(errors, "$.deferral.family"));
	}

	TEST_CASE("TypeCompleteness History tracked mutation recipes resolve through the graph") {
		Vector<String> errors;
		CHECK_MESSAGE(validate_mutation_directory(type_completeness_history_catalog_root, type_completeness_mutations_directory, errors),
				String(" | ").join(errors));
	}

	TEST_CASE("TypeCompleteness History rejects a recipe with empty detectors, partial coordinates, or unknown names") {
		const String recipe_text = FileAccess::get_file_as_string(
				type_completeness_mutations_directory.path_join("union_membership_drops_last_alternative.json"));
		REQUIRE_FALSE(recipe_text.is_empty());
		struct RecipeVariant {
			const char *name;
			String text;
			const char *expected;
		};
		const String valid_detector = R"JSON({ "family": "union_destination_membership", "case_coordinates": { "destination": "union", "source_proof": "gradual", "boundary": "argument_binding", "surface": "text" }, "dimension": "runtime_status" })JSON";
		const String head = R"JSON({ "schema_version": 1, "recipe_id": "fixture", "description": "d", "escape_class": "skip_runtime_boundary_check", "expected_detectors": [)JSON";
		const RecipeVariant variants[] = {
			{ "empty_detectors", head + "] }", "$.expected_detectors must be a non-empty array" },
			{ "partial_coordinates", head + valid_detector.replace(", \"surface\": \"text\"", "") + "] }", "must name every domain axis" },
			{ "unknown_family", head + valid_detector.replace("union_destination_membership", "no_such_family") + "] }", "cannot be resolved" },
			{ "unknown_dimension", head + valid_detector.replace("runtime_status", "no_such_dimension") + "] }", "required_dimensions names 'no_such_dimension'" },
			{ "dimension_absent_from_cell", head + valid_detector.replace("runtime_status", "stored_carrier") + "] }", "required_dimensions names 'stored_carrier'" },
			{ "unknown_leaf", head + valid_detector.replace("\"gradual\"", "\"psychic\"") + "] }", "$.expected_detectors[0].case_coordinates" },
			{ "float_schema_version", (head + valid_detector + "] }").replace("\"schema_version\": 1,", "\"schema_version\": 1.25,"), "$.schema_version must be a JSON integer" },
		};
		for (const RecipeVariant &variant : variants) {
			TemporaryProjectTree tree(String("type_completeness_history_recipe_") + variant.name);
			REQUIRE(tree.is_valid());
			tree.write_file("mutations/fixture.json", variant.text);
			tree.write_file("mutations/patches/fixture.patch", "");
			tree.write_file("mutations/shards.json", R"JSON({ "schema_version": 1, "shards": [ { "shard_id": "s", "recipes": ["fixture"], "budget_seconds": 60 } ] })JSON");
			Vector<String> errors;
			CHECK_FALSE_MESSAGE(validate_mutation_directory(type_completeness_history_catalog_root, tree.root.path_join("mutations"), errors), variant.name);
			CHECK_MESSAGE(history_errors_mention(errors, variant.expected), vformat("%s: %s", variant.name, String(" | ").join(errors)));
		}

		// The unmodified detector validates, so each refusal above is caused by its one edit.
		TemporaryProjectTree tree("type_completeness_history_recipe_valid");
		REQUIRE(tree.is_valid());
		tree.write_file("mutations/fixture.json", head + valid_detector + "] }");
		tree.write_file("mutations/patches/fixture.patch", "");
		tree.write_file("mutations/shards.json", R"JSON({ "schema_version": 1, "shards": [ { "shard_id": "s", "recipes": ["fixture"], "budget_seconds": 1800 } ] })JSON");
		Vector<String> errors;
		CHECK_MESSAGE(validate_mutation_directory(type_completeness_history_catalog_root, tree.root.path_join("mutations"), errors), String(" | ").join(errors));

		tree.write_file("mutations/shards.json", R"JSON({ "schema_version": 1, "shards": [ { "shard_id": "s", "recipes": ["fixture"], "budget_seconds": 1801 } ] })JSON");
		errors.clear();
		CHECK_FALSE(validate_mutation_directory(type_completeness_history_catalog_root, tree.root.path_join("mutations"), errors));
		CHECK(history_errors_mention(errors, "budget_seconds"));
	}
}

} // namespace FSTests
