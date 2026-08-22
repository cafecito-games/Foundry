/**************************************************************************/
/*  fs_type_completeness_common.h                                         */
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
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

namespace FSTests {

// Helpers shared by every type-completeness translation unit. Each one exists exactly once: a second
// copy would let two parts of the harness disagree about identity, ordering, or input validity while
// both looked correct in isolation.
namespace Completeness {

// Dictionary keys as strings in ascending order, so any report or identity derived from a Dictionary
// is independent of insertion order.
Vector<String> sorted_dictionary_keys(const Dictionary &p_dictionary);

// Prefixes a value with its length so concatenated components cannot be re-split ambiguously.
String length_encoded(const String &p_value);

// Order-independent, cycle-guarded, depth-limited identity of an arbitrary Variant. Values that
// cannot be encoded injectively (objects, callables, signals, RIDs, cycles, excessive depth) collapse
// to explicit stable sentinels rather than depending on instance identity, so the encoding stays
// deterministic for malformed programmatic input.
String canonical_variant_identity(const Variant &p_value);

// Accepts a JSON number with no fractional part, in either the INT or the FLOAT form the engine's
// JSON parser and the runner's Variant-based writer produce. Everything else - strings, booleans,
// fractional or non-finite numbers, values outside the 32-bit range - is rejected, so a malformed
// input never reads as an absent one.
bool parse_json_integer(const Variant &p_value, int &r_value);

// Identity of the surface-independent half of a case's coordinates: the key under which the two
// surfaces of one semantic case meet.
String semantic_pair_key(const Dictionary &p_coordinates, const String &p_surface_axis);

// Why a directory entry or the directory itself was refused. The enumeration reports the kind and
// leaves the wording to the caller, so every caller keeps its own diagnostics vocabulary while the
// policy that decides what is admissible exists only once.
enum class JsonDirectoryErrorKind {
	DIRECTORY_UNOPENABLE,
	DIRECTORY_UNLISTABLE,
	FILESYSTEM_UNAVAILABLE,
	DIRECTORY_NOT_CANONICAL,
	LINKED_DIRECTORY,
	LINKED_ENTRY,
	SUBDIRECTORY_ENTRY,
	NON_JSON_ENTRY,
	NON_CONTAINED_ENTRY,
	ENTRY_NOT_CANONICAL,
	EMPTY_DIRECTORY,
};

struct JsonDirectoryError {
	JsonDirectoryErrorKind kind = JsonDirectoryErrorKind::DIRECTORY_UNOPENABLE;
	// Entry name relative to the directory, empty for a directory-level refusal.
	String entry;
	// Absolute path of the offending entry, or of the directory for a directory-level refusal.
	String path;
	Error error_code = OK;
};

// What a caller demands of a JSON input directory beyond the fixed policy every caller shares:
// dot-prefixed entries are skipped, subdirectories and non-lowercase-".json" entries are refused,
// and the accepted files are returned sorted.
struct JsonDirectoryPolicy {
	// Refuse a symlinked directory or entry. A linked input could resolve outside the tree the
	// caller validated.
	bool reject_links = true;
	// Refuse a directory whose listed path is not already its canonical path.
	bool require_canonical_directory = false;
	// Refuse an entry that is not a canonical direct child of the canonical directory.
	bool require_canonical_children = false;
	// Refuse a directory that yields no JSON file.
	bool require_non_empty = true;
	// Stop at the first refusal instead of reporting every one. Callers that abort the whole run on
	// the first defect enumerate no further.
	bool stop_at_first_error = false;
};

// Lists the admissible JSON files of p_directory in ascending path order. Returns OK only when
// nothing was refused; r_errors then stays empty. The file list is meaningful even on failure, so a
// caller that reports every defect can keep validating.
Error enumerate_json_directory(const String &p_directory, const JsonDirectoryPolicy &p_policy,
		Vector<String> &r_files, Vector<JsonDirectoryError> &r_errors);

// True when the refusal is about the directory itself rather than one entry in it: nothing in the
// directory could be enumerated, so a caller has no partial result to keep working with. An empty
// directory is deliberately not directory-level; the caller decides what an empty input means.
bool is_directory_level_error(JsonDirectoryErrorKind p_kind);

// Default wording of a refusal, without any directory prefix. A caller whose inputs have their own
// name in diagnostics words those cases itself and delegates the rest here, so the same refusal never
// reads two different ways for the same reason.
String describe_json_directory_error(const JsonDirectoryError &p_error);

// Wall-clock budgets for the type-completeness matrix. Tracked as data next to the catalog so the
// harness, the presubmit gate, and the scheduled shards cannot drift apart; there are no defaults,
// because a budget that silently falls back is not a budget.
struct FSCompletenessBudgets {
	int presubmit_hard_timeout_seconds = 0;
	int strict_shard_hard_timeout_seconds = 0;
	int scheduled_shard_target_seconds = 0;
	int scheduled_shard_hard_timeout_seconds = 0;
	int second_family_smoke_seconds = 0;

	// Repository-relative path of the tracked budgets document.
	static String tracked_path();

	// Reads and validates p_path. Every refusal appends one JSONPath-prefixed message and no budget
	// is populated, so a malformed document can never be mistaken for an absent or partial one.
	static Error load(const String &p_path, FSCompletenessBudgets &r_budgets, Vector<String> &r_errors);

	// Hard timeout of a named tier, or -1 when the tier is not one of "presubmit", "strict", or
	// "scheduled".
	int hard_timeout_seconds_for_tier(const String &p_tier) const;
};

// How a caller asks git whether a repository-relative path is tracked. Injectable so a refusal can be
// proven without a repository, and shared so the runner and the census ask the question one way.
using TrackedFileProbe = Error (*)(const String &p_repository_root, const String &p_path,
		String &r_output, int &r_exit_code);

// Runs `git ls-files --error-unmatch` for p_path inside p_repository_root. Exit code 1 means the path
// is untracked; any other non-zero code, or a non-OK return, means the question could not be asked.
Error default_tracked_file_probe(const String &p_repository_root, const String &p_path,
		String &r_output, int &r_exit_code);

// Nearest ancestor of p_start_path that carries a `.git` entry, or an empty string.
String find_repository_root_ancestor(const String &p_start_path);

// Repository root a catalog belongs to: the catalog's own ancestor when it has one, otherwise the
// working directory's, which is what a staged copy outside the tree resolves through.
String find_repository_root(const String &p_catalog_root);

// Marks an intentionally skipped type-completeness test visibly. A bare early return makes a skipped
// test indistinguishable from a passing one in the suite output.
void fs_completeness_skip(const char *p_reason);

} // namespace Completeness

} // namespace FSTests
