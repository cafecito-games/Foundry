/**************************************************************************/
/*  fs_batch_candidates.cpp                                               */
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

#include "fs_batch_candidates.h"

#ifdef TOOLS_ENABLED

#include "fs_refactoring.h"

#include "core/io/file_access.h"
#include "core/templates/hash_set.h"

BatchCandidatesResult FSBatchCandidates::collect(const Vector<String> &p_paths, RefactorKind p_kind, bool p_allow_member_container_inference) {
	BatchCandidatesResult result;

	// The only supported kind today mirrors find_candidates; reject others up front so the
	// caller gets one clear error instead of a per-file error repeated across the batch.
	if (p_kind != RefactorKind::ADD_TYPE_ANNOTATION) {
		result.error_message = "Headless candidate collection is not implemented for this refactor.";
		return result;
	}

	HashSet<String> seen;
	for (const String &path : p_paths) {
		// Collapse duplicate inputs so a file is never read or reported twice.
		if (seen.has(path)) {
			continue;
		}
		seen.insert(path);

		BatchFileCandidates file_entry;
		file_entry.path = path;

		Error err = OK;
		const String source = FileAccess::get_file_as_string(path, &err);
		if (err != OK) {
			// An unreadable file is reported and skipped; the rest of the batch still runs.
			file_entry.error_message = vformat("Cannot read '%s'.", path);
			result.files.push_back(file_entry);
			continue;
		}

		RefactorContext context;
		context.path = path;
		context.source = source;
		context.allow_member_container_inference = p_allow_member_container_inference;
		const RefactorCandidatesResult candidates = FSRefactoring::find_candidates(context, p_kind);
		if (!candidates.ok) {
			// A file the analyzer cannot process is reported and skipped, not fatal.
			file_entry.error_message = candidates.error_message;
			result.files.push_back(file_entry);
			continue;
		}

		file_entry.ok = true;
		file_entry.candidates = candidates.candidates;
		for (const RefactorCandidate &candidate : candidates.candidates) {
			result.total_candidates++;
			if (candidate.enabled) {
				result.enabled_candidates++;
			}
		}
		result.files.push_back(file_entry);
	}

	result.ok = true;
	return result;
}

#endif // TOOLS_ENABLED
