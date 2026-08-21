/**************************************************************************/
/*  fs_type_completeness_case_id.cpp                                      */
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

#include "fs_type_completeness_case_id.h"
#include "fs_type_completeness_json.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/math/math_funcs.h"
#include "core/variant/array.h"

#include <cstdint>

namespace FSTests {

namespace {

static String escape_case_id_component(const String &p_value) {
	String escaped;
	for (int i = 0; i < p_value.length(); i++) {
		const char32_t character = p_value[i];
		switch (character) {
			case '%':
				escaped += "%25";
				break;
			case '=':
				escaped += "%3D";
				break;
			case '|':
				escaped += "%7C";
				break;
			default:
				escaped += String::chr(character);
				break;
		}
	}
	return escaped;
}

static String length_encoded(const String &p_value) {
	return vformat("%d:%s", p_value.length(), p_value);
}

static constexpr int MAX_COORDINATE_VARIANT_DEPTH = 32;

static String canonical_variant_identity(const Variant &p_value, int p_depth, HashSet<uint64_t> &r_active_containers) {
	if (p_depth >= MAX_COORDINATE_VARIANT_DEPTH) {
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
			members.push_back(length_encoded(canonical_variant_identity(key, p_depth + 1, r_active_containers)) +
					length_encoded(canonical_variant_identity(dictionary[key], p_depth + 1, r_active_containers)));
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
		for (int i = 0; i < array.size(); i++) {
			identity += length_encoded(canonical_variant_identity(array[i], p_depth + 1, r_active_containers));
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

static String deterministic_coordinate_value(const Variant &p_value) {
	if (p_value.get_type() == Variant::STRING) {
		return escape_case_id_component(p_value);
	}
	// Valid manifests use string leaves. The sentinel begins with a raw percent sign, which no
	// escaped string value can contain, so malformed programmatic inputs remain deterministic and
	// cannot collide with a valid string coordinate. This hardening is intentionally not an
	// injective semantic encoding for invalid object-like, cyclic, or excessively deep values:
	// those values collapse to explicit stable sentinels instead of depending on instance identity.
	HashSet<uint64_t> active_containers;
	return "%!" + escape_case_id_component(canonical_variant_identity(p_value, 0, active_containers));
}

static void append_file_error(Vector<String> &r_errors, const String &p_file, const String &p_path,
		const String &p_reason) {
	r_errors.push_back(vformat("%s: %s: %s", p_file, p_path, p_reason));
}

static Vector<String> sorted_dictionary_keys(const Dictionary &p_dictionary) {
	Vector<String> keys;
	const Array raw_keys = p_dictionary.keys();
	keys.reserve(raw_keys.size());
	for (int i = 0; i < raw_keys.size(); i++) {
		keys.push_back(raw_keys[i]);
	}
	keys.sort();
	return keys;
}

static void validate_exact_fields(const Dictionary &p_object, const Vector<String> &p_allowed,
		const String &p_file, const String &p_path, Vector<String> &r_errors) {
	for (const String &field : sorted_dictionary_keys(p_object)) {
		if (!p_allowed.has(field)) {
			append_file_error(r_errors, p_file, p_path, vformat("unknown field '%s'", field));
		}
	}
	for (const String &field : p_allowed) {
		if (!p_object.has(field)) {
			append_file_error(r_errors, p_file, p_path + "." + field, "required field is missing");
		}
	}
}

static bool parse_json_integer(const Variant &p_value, int &r_value) {
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

static void detect_migration_cycle(const String &p_id, const HashMap<String, Vector<String>> &p_aliases,
		HashMap<String, int> &r_colors, Vector<String> &r_stack, HashSet<String> &r_reported,
		Vector<String> &r_errors) {
	r_colors.insert(p_id, 1);
	r_stack.push_back(p_id);
	const Vector<String> *targets = p_aliases.getptr(p_id);
	if (targets != nullptr) {
		for (const String &target : *targets) {
			if (!p_aliases.has(target)) {
				continue;
			}
			const int *color_ptr = r_colors.getptr(target);
			const int color = color_ptr == nullptr ? 0 : *color_ptr;
			if (color == 0) {
				detect_migration_cycle(target, p_aliases, r_colors, r_stack, r_reported, r_errors);
				continue;
			}
			if (color != 1) {
				continue;
			}
			int cycle_start = 0;
			while (cycle_start < r_stack.size() && r_stack[cycle_start] != target) {
				cycle_start++;
			}
			Vector<String> cycle;
			for (int i = cycle_start; i < r_stack.size(); i++) {
				cycle.push_back(r_stack[i]);
			}
			cycle.push_back(target);
			const String description = String(" -> ").join(cycle);
			if (!r_reported.has(description)) {
				r_reported.insert(description);
				r_errors.push_back("migration alias cycle: " + description);
			}
		}
	}
	r_stack.resize(r_stack.size() - 1);
	r_colors.insert(p_id, 2);
}

} // namespace

String FSCompletenessCaseID::canonical_coordinates(const Dictionary &p_coordinates) {
	Vector<String> names = sorted_dictionary_keys(p_coordinates);
	Vector<String> pairs;
	pairs.reserve(names.size());
	for (const String &name : names) {
		pairs.push_back(escape_case_id_component(name) + "=" + deterministic_coordinate_value(p_coordinates[name]));
	}
	return String("|").join(pairs);
}

String FSCompletenessCaseID::make(const String &p_family, const Dictionary &p_coordinates) {
	const String payload = escape_case_id_component(p_family) + "|" + canonical_coordinates(p_coordinates);
	return "fstc-v1-" + payload.sha256_text().substr(0, 20);
}

Error FSCompletenessMigrations::load(const String &p_directory, const HashSet<String> &p_current_ids,
		Vector<String> &r_errors) {
	aliases.clear();
	r_errors.clear();

	Error directory_error = OK;
	Ref<DirAccess> directory = DirAccess::open(p_directory, &directory_error);
	if (directory.is_null()) {
		r_errors.push_back(vformat("%s: could not open directory (error %d)", p_directory, directory_error));
		return directory_error == OK ? ERR_CANT_OPEN : directory_error;
	}
	if (directory->list_dir_begin() != OK) {
		r_errors.push_back(vformat("%s: could not list directory", p_directory));
		return ERR_CANT_OPEN;
	}
	Vector<String> files;
	for (String entry = directory->get_next(); !entry.is_empty(); entry = directory->get_next()) {
		if (!directory->current_is_dir() && entry.get_extension().to_lower() == "json") {
			files.push_back(p_directory.path_join(entry));
		}
	}
	directory->list_dir_end();
	files.sort();
	if (files.is_empty()) {
		r_errors.push_back(vformat("%s: must contain at least one JSON file", p_directory));
		return ERR_INVALID_DATA;
	}

	HashMap<String, Vector<String>> parsed_aliases;
	HashMap<String, String> old_id_sources;
	bool had_parse_error = false;
	for (const String &file : files) {
		Error read_error = OK;
		const String source = FileAccess::get_file_as_string(file, &read_error);
		if (read_error != OK) {
			r_errors.push_back(vformat("%s: could not read file (error %d)", file, read_error));
			continue;
		}
		Variant json_data;
		const Error json_error = parse_type_completeness_json(source, file, json_data, r_errors);
		if (json_error == ERR_PARSE_ERROR) {
			had_parse_error = true;
			continue;
		}
		if (json_data.get_type() != Variant::DICTIONARY) {
			append_file_error(r_errors, file, "$", "expected object");
			continue;
		}
		const Dictionary root = json_data;
		validate_exact_fields(root, Vector<String>({ "schema_version", "migrations" }), file, "$", r_errors);
		if (root.has("schema_version")) {
			int schema_version = 0;
			if (!parse_json_integer(root["schema_version"], schema_version)) {
				append_file_error(r_errors, file, "$.schema_version", "expected integer");
			} else if (schema_version != 1) {
				append_file_error(r_errors, file, "$.schema_version", "expected 1");
			}
		}
		if (!root.has("migrations")) {
			continue;
		}
		if (root["migrations"].get_type() != Variant::ARRAY) {
			append_file_error(r_errors, file, "$.migrations", "expected array");
			continue;
		}

		const Array records = root["migrations"];
		for (int record_index = 0; record_index < records.size(); record_index++) {
			const String record_path = vformat("$.migrations[%d]", record_index);
			if (records[record_index].get_type() != Variant::DICTIONARY) {
				append_file_error(r_errors, file, record_path, "expected object");
				continue;
			}
			const Dictionary record = records[record_index];
			validate_exact_fields(record, Vector<String>({ "old_id", "new_ids", "reason" }), file, record_path, r_errors);

			String old_id;
			bool valid_old_id = false;
			if (record.has("old_id")) {
				if (record["old_id"].get_type() != Variant::STRING) {
					append_file_error(r_errors, file, record_path + ".old_id", "expected string");
				} else {
					old_id = record["old_id"];
					valid_old_id = !old_id.is_empty();
					if (!valid_old_id) {
						append_file_error(r_errors, file, record_path + ".old_id", "must not be empty");
					} else if (old_id_sources.has(old_id)) {
						append_file_error(r_errors, file, record_path + ".old_id",
								vformat("duplicate old_id '%s' (first declared in %s)", old_id, old_id_sources[old_id]));
						valid_old_id = false;
					} else {
						old_id_sources.insert(old_id, file);
					}
				}
			}

			if (record.has("reason")) {
				if (record["reason"].get_type() != Variant::STRING) {
					append_file_error(r_errors, file, record_path + ".reason", "expected string");
				} else if (String(record["reason"]).is_empty()) {
					append_file_error(r_errors, file, record_path + ".reason", "reason must not be empty");
				}
			}

			Vector<String> replacements;
			if (record.has("new_ids")) {
				if (record["new_ids"].get_type() != Variant::ARRAY) {
					append_file_error(r_errors, file, record_path + ".new_ids", "expected array");
				} else {
					const Array new_ids = record["new_ids"];
					if (new_ids.is_empty()) {
						append_file_error(r_errors, file, record_path + ".new_ids", "new_ids must not be empty");
					}
					HashSet<String> seen_replacements;
					for (int replacement_index = 0; replacement_index < new_ids.size(); replacement_index++) {
						const String replacement_path = vformat("%s.new_ids[%d]", record_path, replacement_index);
						if (new_ids[replacement_index].get_type() != Variant::STRING) {
							append_file_error(r_errors, file, replacement_path, "expected string");
							continue;
						}
						const String replacement = new_ids[replacement_index];
						if (replacement.is_empty()) {
							append_file_error(r_errors, file, replacement_path, "replacement ID must not be empty");
						} else if (seen_replacements.has(replacement)) {
							append_file_error(r_errors, file, replacement_path,
									vformat("duplicate replacement ID '%s'", replacement));
						} else {
							seen_replacements.insert(replacement);
							replacements.push_back(replacement);
						}
						if (!replacement.is_empty() && !p_current_ids.has(replacement)) {
							append_file_error(r_errors, file, replacement_path,
									vformat("replacement ID '%s' is not a current case ID", replacement));
						}
						if (valid_old_id && replacement == old_id) {
							append_file_error(r_errors, file, replacement_path, "old_id must not alias itself");
						}
					}
				}
			}
			if (valid_old_id) {
				parsed_aliases.insert(old_id, replacements);
			}
		}
	}

	Vector<String> old_ids;
	for (const KeyValue<String, Vector<String>> &alias : parsed_aliases) {
		old_ids.push_back(alias.key);
	}
	old_ids.sort();
	HashMap<String, int> colors;
	Vector<String> stack;
	HashSet<String> reported_cycles;
	for (const String &old_id : old_ids) {
		const int *color = colors.getptr(old_id);
		if (color == nullptr || *color == 0) {
			detect_migration_cycle(old_id, parsed_aliases, colors, stack, reported_cycles, r_errors);
		}
	}

	if (!r_errors.is_empty()) {
		r_errors.sort();
		return had_parse_error ? ERR_PARSE_ERROR : ERR_INVALID_DATA;
	}
	aliases = parsed_aliases;
	return OK;
}

Vector<String> FSCompletenessMigrations::resolve(const String &p_old_id) const {
	const Vector<String> *resolved = aliases.getptr(p_old_id);
	return resolved == nullptr ? Vector<String>() : *resolved;
}

} // namespace FSTests
