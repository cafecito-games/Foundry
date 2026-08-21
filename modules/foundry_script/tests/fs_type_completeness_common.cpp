/**************************************************************************/
/*  fs_type_completeness_common.cpp                                       */
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

#include "fs_type_completeness_common.h"

#include "fs_temporary_project_tree.h"
#include "fs_type_completeness_case_id.h"
#include "fs_type_completeness_json.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/math/math_funcs.h"
#include "core/templates/hash_set.h"

#include "tests/test_macros.h"

namespace FSTests {
namespace Completeness {

namespace {

constexpr int MAX_VARIANT_IDENTITY_DEPTH = 32;

String canonical_variant_identity_recursive(
		const Variant &p_value, int p_depth, HashSet<uint64_t> &r_active_containers) {
	if (p_depth >= MAX_VARIANT_IDENTITY_DEPTH) {
		return "X:DEPTH_LIMIT";
	}
	if (p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary dictionary = p_value;
		const uint64_t identity_key = uint64_t(reinterpret_cast<uintptr_t>(dictionary.id()));
		if (r_active_containers.has(identity_key)) {
			return "X:CYCLE";
		}
		r_active_containers.insert(identity_key);
		Vector<String> members;
		for (const Variant &key : dictionary.keys()) {
			members.push_back(
					length_encoded(canonical_variant_identity_recursive(key, p_depth + 1, r_active_containers)) +
					length_encoded(canonical_variant_identity_recursive(
							dictionary[key], p_depth + 1, r_active_containers)));
		}
		r_active_containers.erase(identity_key);
		members.sort();
		String identity = "D{";
		for (const String &member : members) {
			identity += member;
		}
		return identity + "}";
	}
	if (p_value.get_type() == Variant::ARRAY) {
		const Array array = p_value;
		const uint64_t identity_key = uint64_t(reinterpret_cast<uintptr_t>(array.id()));
		if (r_active_containers.has(identity_key)) {
			return "X:CYCLE";
		}
		r_active_containers.insert(identity_key);
		String identity = "A[";
		for (int index = 0; index < array.size(); index++) {
			identity += length_encoded(
					canonical_variant_identity_recursive(array[index], p_depth + 1, r_active_containers));
		}
		r_active_containers.erase(identity_key);
		return identity + "]";
	}
	if (p_value.get_type() == Variant::STRING) {
		return "S" + length_encoded(p_value);
	}
	switch (p_value.get_type()) {
		case Variant::OBJECT:
		case Variant::CALLABLE:
		case Variant::SIGNAL:
		case Variant::RID:
			return vformat("X:UNSUPPORTED:T%d", p_value.get_type());
		default:
			break;
	}
	return vformat("T%d:%s", p_value.get_type(), length_encoded(p_value.stringify()));
}

void append_error(Vector<JsonDirectoryError> &r_errors, JsonDirectoryErrorKind p_kind,
		const String &p_entry, const String &p_path, Error p_error_code) {
	JsonDirectoryError error;
	error.kind = p_kind;
	error.entry = p_entry;
	error.path = p_path;
	error.error_code = p_error_code;
	r_errors.push_back(error);
}

void append_budget_error(Vector<String> &r_errors, const String &p_file, const String &p_json_path,
		const String &p_reason) {
	r_errors.push_back(vformat("%s: %s: %s", p_file, p_json_path, p_reason));
}

struct BudgetMember {
	const char *name;
	int FSCompletenessBudgets::*field;
};

const BudgetMember BUDGET_MEMBERS[] = {
	{ "presubmit_hard_timeout_seconds", &FSCompletenessBudgets::presubmit_hard_timeout_seconds },
	{ "strict_shard_hard_timeout_seconds", &FSCompletenessBudgets::strict_shard_hard_timeout_seconds },
	{ "scheduled_shard_target_seconds", &FSCompletenessBudgets::scheduled_shard_target_seconds },
	{ "scheduled_shard_hard_timeout_seconds", &FSCompletenessBudgets::scheduled_shard_hard_timeout_seconds },
	{ "second_family_smoke_seconds", &FSCompletenessBudgets::second_family_smoke_seconds },
};

} // namespace

Vector<String> sorted_dictionary_keys(const Dictionary &p_dictionary) {
	Vector<String> keys;
	const Array raw_keys = p_dictionary.keys();
	keys.reserve(raw_keys.size());
	for (int index = 0; index < raw_keys.size(); index++) {
		keys.push_back(raw_keys[index]);
	}
	keys.sort();
	return keys;
}

String length_encoded(const String &p_value) {
	return vformat("%d:%s", p_value.length(), p_value);
}

String canonical_variant_identity(const Variant &p_value) {
	HashSet<uint64_t> active_containers;
	return canonical_variant_identity_recursive(p_value, 0, active_containers);
}

bool parse_json_integer(const Variant &p_value, int &r_value) {
	if (p_value.get_type() != Variant::FLOAT && p_value.get_type() != Variant::INT) {
		return false;
	}
	const double number = p_value;
	if (!Math::is_finite(number) || number != Math::floor(number) || number < INT32_MIN || number > INT32_MAX) {
		return false;
	}
	r_value = int(number);
	return true;
}

String semantic_pair_key(const Dictionary &p_coordinates, const String &p_surface_axis) {
	Dictionary semantic_coordinates = p_coordinates.duplicate();
	semantic_coordinates.erase(p_surface_axis);
	return FSCompletenessCaseID::canonical_coordinates(semantic_coordinates);
}

Error enumerate_json_directory(const String &p_directory, const JsonDirectoryPolicy &p_policy,
		Vector<String> &r_files, Vector<JsonDirectoryError> &r_errors) {
	r_files.clear();
	r_errors.clear();

	Error open_error = OK;
	Ref<DirAccess> directory = DirAccess::open(p_directory, &open_error);
	if (directory.is_null()) {
		append_error(r_errors, JsonDirectoryErrorKind::DIRECTORY_UNOPENABLE, String(), p_directory,
				open_error == OK ? ERR_CANT_OPEN : open_error);
		return ERR_INVALID_DATA;
	}
	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (filesystem.is_null()) {
		append_error(r_errors, JsonDirectoryErrorKind::FILESYSTEM_UNAVAILABLE, String(), p_directory,
				ERR_UNAVAILABLE);
		return ERR_INVALID_DATA;
	}
	if (p_policy.reject_links && filesystem->is_link(p_directory)) {
		append_error(r_errors, JsonDirectoryErrorKind::LINKED_DIRECTORY, String(), p_directory, ERR_UNAUTHORIZED);
		return ERR_INVALID_DATA;
	}
	const String directory_path = directory->get_current_dir().replace("\\", "/").simplify_path();
	String canonical_directory_path;
	if (p_policy.require_canonical_directory || p_policy.require_canonical_children) {
		canonical_directory_path = TemporaryProjectTree::canonicalize_existing_path(directory_path);
		if (p_policy.require_canonical_directory &&
				(canonical_directory_path.is_empty() || canonical_directory_path != directory_path)) {
			append_error(r_errors, JsonDirectoryErrorKind::DIRECTORY_NOT_CANONICAL, String(), directory_path,
					ERR_UNAUTHORIZED);
			return ERR_INVALID_DATA;
		}
	}
	if (directory->list_dir_begin() != OK) {
		append_error(r_errors, JsonDirectoryErrorKind::DIRECTORY_UNLISTABLE, String(), directory_path,
				ERR_CANT_OPEN);
		return ERR_INVALID_DATA;
	}

	bool stopped = false;
	for (String entry = directory->get_next(); !entry.is_empty(); entry = directory->get_next()) {
		// Dot-prefixed entries are the caller's own metadata (and `.`/`..`), never inputs.
		if (entry.begins_with(".")) {
			continue;
		}
		const String entry_path = directory_path.path_join(entry).simplify_path();
		JsonDirectoryErrorKind kind = JsonDirectoryErrorKind::NON_JSON_ENTRY;
		bool refused = true;
		if (entry_path.get_base_dir() != directory_path) {
			kind = JsonDirectoryErrorKind::NON_CONTAINED_ENTRY;
		} else if (p_policy.reject_links && (directory->is_link(entry) || filesystem->is_link(entry_path))) {
			kind = JsonDirectoryErrorKind::LINKED_ENTRY;
		} else if (directory->current_is_dir()) {
			kind = JsonDirectoryErrorKind::SUBDIRECTORY_ENTRY;
		} else if (entry.get_extension() != "json") {
			kind = JsonDirectoryErrorKind::NON_JSON_ENTRY;
		} else {
			refused = false;
		}
		if (!refused) {
			r_files.push_back(entry_path);
			continue;
		}
		append_error(r_errors, kind, entry, entry_path,
				kind == JsonDirectoryErrorKind::LINKED_ENTRY ? ERR_UNAUTHORIZED : ERR_INVALID_DATA);
		if (p_policy.stop_at_first_error) {
			stopped = true;
			break;
		}
	}
	directory->list_dir_end();
	if (stopped) {
		r_files.clear();
		return ERR_INVALID_DATA;
	}
	r_files.sort();

	if (p_policy.require_non_empty && r_files.is_empty()) {
		append_error(r_errors, JsonDirectoryErrorKind::EMPTY_DIRECTORY, String(), directory_path, ERR_INVALID_DATA);
		if (p_policy.stop_at_first_error) {
			return ERR_INVALID_DATA;
		}
	}
	if (p_policy.require_canonical_children) {
		Vector<String> canonical_files;
		for (const String &file : r_files) {
			const String canonical_file = TemporaryProjectTree::canonicalize_existing_path(file);
			if (canonical_file.is_empty() || canonical_file != file ||
					canonical_file.get_base_dir() != canonical_directory_path ||
					!TemporaryProjectTree::is_strict_descendant(canonical_directory_path, canonical_file)) {
				append_error(r_errors, JsonDirectoryErrorKind::ENTRY_NOT_CANONICAL, file.get_file(), file,
						ERR_UNAUTHORIZED);
				if (p_policy.stop_at_first_error) {
					r_files.clear();
					return ERR_INVALID_DATA;
				}
				continue;
			}
			canonical_files.push_back(canonical_file);
		}
		r_files = canonical_files;
	}
	return r_errors.is_empty() ? OK : ERR_INVALID_DATA;
}

bool is_directory_level_error(JsonDirectoryErrorKind p_kind) {
	switch (p_kind) {
		case JsonDirectoryErrorKind::DIRECTORY_UNOPENABLE:
		case JsonDirectoryErrorKind::DIRECTORY_UNLISTABLE:
		case JsonDirectoryErrorKind::FILESYSTEM_UNAVAILABLE:
		case JsonDirectoryErrorKind::DIRECTORY_NOT_CANONICAL:
		case JsonDirectoryErrorKind::LINKED_DIRECTORY:
			return true;
		default:
			break;
	}
	return false;
}

String describe_json_directory_error(const JsonDirectoryError &p_error) {
	switch (p_error.kind) {
		case JsonDirectoryErrorKind::DIRECTORY_UNOPENABLE:
			return vformat("could not open directory (error %d)", p_error.error_code);
		case JsonDirectoryErrorKind::DIRECTORY_UNLISTABLE:
			return "could not list directory";
		case JsonDirectoryErrorKind::FILESYSTEM_UNAVAILABLE:
			return "filesystem access is unavailable";
		case JsonDirectoryErrorKind::DIRECTORY_NOT_CANONICAL:
			return "directory must be canonical";
		case JsonDirectoryErrorKind::LINKED_DIRECTORY:
			return "linked directory is not allowed";
		case JsonDirectoryErrorKind::LINKED_ENTRY:
			return vformat("linked entry '%s' is not allowed", p_error.entry);
		case JsonDirectoryErrorKind::SUBDIRECTORY_ENTRY:
			return vformat("unexpected directory entry '%s'", p_error.entry);
		case JsonDirectoryErrorKind::NON_JSON_ENTRY:
			return vformat("unexpected non-JSON entry '%s'", p_error.entry);
		case JsonDirectoryErrorKind::NON_CONTAINED_ENTRY:
			return vformat("non-contained entry '%s'", p_error.entry);
		case JsonDirectoryErrorKind::ENTRY_NOT_CANONICAL:
			return vformat("entry '%s' must be a canonical direct child", p_error.entry);
		case JsonDirectoryErrorKind::EMPTY_DIRECTORY:
			return "must contain at least one JSON file";
	}
	return "was refused";
}

String FSCompletenessBudgets::tracked_path() {
	return "modules/foundry_script/tests/type_completeness/budgets.json";
}

Error FSCompletenessBudgets::load(
		const String &p_path, FSCompletenessBudgets &r_budgets, Vector<String> &r_errors) {
	r_budgets = FSCompletenessBudgets();
	r_errors.clear();

	Error read_error = OK;
	const String source = FileAccess::get_file_as_string(p_path, &read_error);
	if (read_error != OK) {
		append_budget_error(r_errors, p_path, "$", vformat("could not read file (error %d)", read_error));
		return read_error;
	}
	Variant data;
	Vector<String> parse_errors;
	if (parse_type_completeness_json(source, p_path, data, parse_errors) != OK) {
		for (const String &error : parse_errors) {
			r_errors.push_back(error);
		}
		if (r_errors.is_empty()) {
			append_budget_error(r_errors, p_path, "$", "could not be parsed as JSON");
		}
		return ERR_PARSE_ERROR;
	}
	if (data.get_type() != Variant::DICTIONARY) {
		append_budget_error(r_errors, p_path, "$", "expected an object");
		return ERR_INVALID_DATA;
	}
	const Dictionary root = data;

	FSCompletenessBudgets parsed;
	int schema_version = 0;
	if (!root.has("schema_version")) {
		append_budget_error(r_errors, p_path, "$.schema_version", "required member is missing");
	} else if (!parse_json_integer(root["schema_version"], schema_version)) {
		append_budget_error(r_errors, p_path, "$.schema_version", "expected an integer");
	} else if (schema_version != 1) {
		append_budget_error(r_errors, p_path, "$.schema_version", "expected 1");
	}
	for (const BudgetMember &member : BUDGET_MEMBERS) {
		const String json_path = String("$.") + member.name;
		if (!root.has(member.name)) {
			append_budget_error(r_errors, p_path, json_path, "required member is missing");
			continue;
		}
		int value = 0;
		if (!parse_json_integer(root[member.name], value)) {
			append_budget_error(r_errors, p_path, json_path, "expected an integer");
			continue;
		}
		if (value <= 0) {
			append_budget_error(r_errors, p_path, json_path, "expected a positive number of seconds");
			continue;
		}
		parsed.*member.field = value;
	}
	for (const String &member : sorted_dictionary_keys(root)) {
		if (member == "schema_version") {
			continue;
		}
		bool known = false;
		for (const BudgetMember &budget_member : BUDGET_MEMBERS) {
			known = known || member == budget_member.name;
		}
		if (!known) {
			append_budget_error(r_errors, p_path, String("$.") + member, vformat("unknown member '%s'", member));
		}
	}
	if (!r_errors.is_empty()) {
		return ERR_INVALID_DATA;
	}
	r_budgets = parsed;
	return OK;
}

int FSCompletenessBudgets::hard_timeout_seconds_for_tier(const String &p_tier) const {
	if (p_tier == "presubmit") {
		return presubmit_hard_timeout_seconds;
	}
	if (p_tier == "strict") {
		return strict_shard_hard_timeout_seconds;
	}
	if (p_tier == "scheduled") {
		return scheduled_shard_hard_timeout_seconds;
	}
	return -1;
}

void fs_completeness_skip(const char *p_reason) {
	const String message = vformat("[type-completeness] SKIP %s", String::utf8(p_reason));
	print_line(message);
	// The command-line entry point drives the same helpers with no doctest context, where the
	// reporting machinery is unusable; the printed line above is the record there.
	if (doctest::is_running_in_test) {
		MESSAGE(message);
	}
}

} // namespace Completeness
} // namespace FSTests
