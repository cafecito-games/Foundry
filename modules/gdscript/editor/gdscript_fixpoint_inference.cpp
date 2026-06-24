/**************************************************************************/
/*  gdscript_fixpoint_inference.cpp                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "gdscript_fixpoint_inference.h"

#ifdef TOOLS_ENABLED

#include "gdscript_refactoring.h"
#include "gdscript_refactoring_edits.h"

#include "../gdscript_analyzer.h"
#include "../gdscript_cache.h"
#include "../gdscript_parser.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

namespace {

String read_source(const String &p_path, bool &r_ok) {
	Error err = OK;
	const String source = FileAccess::get_file_as_string(p_path, &err);
	r_ok = (err == OK);
	return source;
}

String make_temp_path(const String &p_path) {
	const String base = p_path + ".tmp";
	uint64_t suffix = OS::get_singleton()->get_ticks_usec();
	while (true) {
		const String candidate = base + "." + itos(suffix++);
		if (!FileAccess::exists(candidate) && !DirAccess::exists(candidate)) {
			return candidate;
		}
	}
}

// Write atomically: stage to a temp file, flush, verify no I/O error, then rename
// the temp file over p_path. A partial or failed write never replaces the original.
bool write_source(const String &p_path, const String &p_source) {
	const String temp_path = make_temp_path(p_path);

	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(temp_path, FileAccess::WRITE, &err);
	if (file.is_null() || err != OK) {
		return false;
	}

	const bool stored = file->store_string(p_source);
	file->flush();
	const Error store_error = file->get_error();
	file->close();

	if (!stored || (store_error != OK && store_error != ERR_FILE_EOF)) {
		DirAccess::remove_absolute(temp_path);
		return false;
	}

	if (FileAccess::exists(p_path)) {
		FileAccess::set_unix_permissions(temp_path, FileAccess::get_unix_permissions(p_path));
	}

	err = DirAccess::rename_absolute(temp_path, p_path);
	if (err != OK) {
		DirAccess::remove_absolute(temp_path);
		return false;
	}
	return true;
}

// Drop cached parse/script state so a later analysis re-reads p_path from disk.
void invalidate_cache(const String &p_path) {
	GDScriptCache::remove_parser(p_path);
	GDScriptCache::remove_script(p_path);
}

// Re-parse and analyze p_source as if saved at p_path. True when analysis
// succeeds, i.e. the applied annotations introduced no new errors.
bool verify_source(const String &p_path, const String &p_source) {
	GDScriptParser parser;
	if (parser.parse(p_source, p_path, false) != OK) {
		return false;
	}
	GDScriptAnalyzer analyzer(&parser);
	return analyzer.analyze() == OK;
}

int count_enabled_edits(const RefactorContext &p_ctx, Vector<RefactorTextEdit> &r_edits) {
	RefactorCandidatesResult candidates = GDScriptRefactoring::find_candidates(p_ctx, RefactorKind::ADD_TYPE_ANNOTATION);
	if (!candidates.ok) {
		return 0;
	}
	int collected = 0;
	for (const RefactorCandidate &candidate : candidates.candidates) {
		if (!candidate.enabled) {
			continue;
		}
		for (const RefactorTextEdit &edit : candidate.edits) {
			r_edits.push_back(edit);
		}
		collected++;
	}
	return collected;
}

} // namespace

FixpointInferenceResult GDScriptFixpointInference::run(const Vector<String> &p_paths, const FixpointInferenceOptions &p_options) {
	FixpointInferenceResult result;

	// Collapse duplicate inputs while preserving first-seen order so the same file is
	// never snapshotted, committed, or reported twice.
	Vector<String> paths;
	{
		HashSet<String> seen;
		for (const String &path : p_paths) {
			if (seen.has(path)) {
				continue;
			}
			seen.insert(path);
			paths.push_back(path);
		}
	}

	// Capture originals for before/after reporting and change detection.
	HashMap<String, String> original_source;
	for (const String &path : paths) {
		bool ok = false;
		const String source = read_source(path, ok);
		if (!ok) {
			result.ok = false;
			result.error_message = vformat("Cannot read '%s'.", path);
			return result;
		}
		original_source[path] = source;
	}

	// Determine the iteration ceiling.
	int bound = p_options.max_iterations;
	if (bound <= 0) {
		int first_pass_candidates = 0;
		for (const String &path : paths) {
			RefactorContext ctx;
			ctx.path = path;
			ctx.source = original_source[path];
			RefactorCandidatesResult candidates = GDScriptRefactoring::find_candidates(ctx, RefactorKind::ADD_TYPE_ANNOTATION);
			first_pass_candidates += candidates.candidates.size();
		}
		bound = first_pass_candidates + 1;
	}
	const int hard_ceiling = 1000;
	if (bound > hard_ceiling) {
		bound = hard_ceiling;
	}

	HashMap<String, int> applied_per_file;

	int iteration = 0;
	while (iteration < bound) {
		iteration++;

		// Snapshot a consistent view of every file for this pass.
		HashMap<String, String> snapshot;
		for (const String &path : paths) {
			bool ok = false;
			const String source = read_source(path, ok);
			if (ok) {
				snapshot[path] = source;
			}
		}

		// Collect + verify accepted rewrites without committing yet.
		HashMap<String, String> pending_source;
		HashMap<String, int> pending_count;
		for (const String &path : paths) {
			if (!snapshot.has(path)) {
				continue;
			}
			RefactorContext ctx;
			ctx.path = path;
			ctx.source = snapshot[path];

			Vector<RefactorTextEdit> edits;
			const int enabled = count_enabled_edits(ctx, edits);
			if (enabled == 0 || edits.is_empty()) {
				continue;
			}

			String new_source;
			if (!GDScriptRefactorEdits::apply(snapshot[path], edits, new_source)) {
				continue; // Overlapping/out-of-range edits; skip this file this pass.
			}
			if (!verify_source(path, new_source)) {
				result.skipped.push_back({ path, -1, "verification rejected the applied annotations" });
				continue;
			}
			pending_source[path] = new_source;
			pending_count[path] = enabled;
		}

		// Commit at pass end so each iteration advances exactly one dependency layer.
		int applied_this_pass = 0;
		for (const KeyValue<String, String> &entry : pending_source) {
			if (!write_source(entry.key, entry.value)) {
				result.skipped.push_back({ entry.key, -1, "could not write file" });
				continue;
			}
			invalidate_cache(entry.key);
			const int count = pending_count[entry.key];
			applied_this_pass += count;
			applied_per_file[entry.key] += count;
		}

		if (applied_this_pass == 0) {
			result.converged = true;
			break; // Fixpoint reached.
		}
	}

	result.iterations = iteration;

	// Report candidates that remain found-but-not-applicable so the run gives an honest
	// account of annotations it could not resolve. Verification rejections are already
	// recorded during passes, so only disabled candidates are added here.
	for (const String &path : paths) {
		RefactorContext ctx;
		ctx.path = path;
		bool ok = false;
		ctx.source = read_source(path, ok);
		if (!ok) {
			continue;
		}
		RefactorCandidatesResult candidates = GDScriptRefactoring::find_candidates(ctx, RefactorKind::ADD_TYPE_ANNOTATION);
		if (!candidates.ok) {
			continue;
		}
		for (const RefactorCandidate &candidate : candidates.candidates) {
			if (candidate.enabled) {
				continue;
			}
			result.skipped.push_back({ path, candidate.line, candidate.disabled_reason });
		}
	}

	// Build the change report from captured originals vs final on-disk source.
	for (const String &path : paths) {
		bool ok = false;
		const String after = read_source(path, ok);
		if (!ok) {
			continue;
		}
		const String &before = original_source[path];
		if (before == after) {
			continue;
		}
		FixpointFileChange change;
		change.path = path;
		change.before_source = before;
		change.after_source = after;
		change.annotations_applied = applied_per_file.has(path) ? applied_per_file[path] : 0;
		result.total_annotations_applied += change.annotations_applied;
		result.changed_files.push_back(change);
	}

	result.ok = true;
	return result;
}

#endif // TOOLS_ENABLED
