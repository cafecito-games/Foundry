/**************************************************************************/
/*  script_refactor_apply.cpp                                             */
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

#include "script_refactor_apply.h"

#ifdef TOOLS_ENABLED

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "modules/foundry_script/editor/fs_refactoring_edits.h"
#include "scene/gui/code_edit.h"

namespace {

bool find_source(const Vector<ScriptRefactorSource> &p_sources, const String &p_path, ScriptRefactorSource &r_source) {
	for (const ScriptRefactorSource &source : p_sources) {
		if (source.path == p_path) {
			r_source = source;
			return true;
		}
	}
	return false;
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

} // namespace

bool ScriptRefactorApply::build_plan(
		const Vector<RefactorFileEdit> &p_file_edits,
		const Vector<ScriptRefactorSource> &p_sources,
		ScriptRefactorApplyPlan &r_plan,
		String &r_error_message) {
	ScriptRefactorApplyPlan plan;
	r_error_message = String();

	// This is intentionally validated here rather than trusting the editor-side
	// source collection step, since issue-26 preview consumers call this directly.
	for (const RefactorFileEdit &file_edit : p_file_edits) {
		if (file_edit.path.is_empty()) {
			r_error_message = TTR("Refactor target path is empty.");
			r_plan.files.clear();
			return false;
		}

		ScriptRefactorSource source;
		if (!find_source(p_sources, file_edit.path, source)) {
			r_error_message = vformat(TTR("Cannot apply refactor because '%s' could not be read."), file_edit.path);
			r_plan.files.clear();
			return false;
		}

		String after_source;
		if (!FSRefactorEdits::apply(source.source, file_edit.edits, after_source)) {
			r_error_message = vformat(TTR("Cannot apply refactor because '%s' changed or contains invalid edit ranges."), file_edit.path);
			r_plan.files.clear();
			return false;
		}

		ScriptRefactorFilePlan file_plan;
		file_plan.path = file_edit.path;
		file_plan.before_source = source.source;
		file_plan.after_source = after_source;
		file_plan.edit_count = file_edit.edits.size();
		file_plan.before_source_is_saved_version = source.source_is_saved_version;
		file_plan.edits = file_edit.edits;
		plan.files.push_back(file_plan);
	}

	r_plan = plan;
	return true;
}

void ScriptRefactorApply::replace_editor_text(CodeEdit *p_text_editor, const String &p_source, bool p_source_is_saved_version) {
	ERR_FAIL_NULL(p_text_editor);

	p_text_editor->set_text(p_source);
	p_text_editor->clear_undo_history();
	if (p_source_is_saved_version) {
		p_text_editor->tag_saved_version();
	}
}

bool ScriptRefactorApply::can_write_file(const String &p_path, String &r_error_message) {
	if (p_path.is_empty()) {
		r_error_message = TTR("Refactor target path is empty.");
		return false;
	}

	const String temp_path = make_temp_path(p_path);
	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(temp_path, FileAccess::WRITE, &err);
	if (err != OK || file.is_null()) {
		r_error_message = vformat(TTR("Cannot write refactor target '%s'."), p_path);
		return false;
	}

	file->close();
	DirAccess::remove_absolute(temp_path);
	r_error_message = String();
	return true;
}

bool ScriptRefactorApply::write_file(const String &p_path, const String &p_source, String &r_error_message) {
	if (p_path.is_empty()) {
		r_error_message = TTR("Refactor target path is empty.");
		return false;
	}

	const String temp_path = make_temp_path(p_path);

	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(temp_path, FileAccess::WRITE, &err);
	if (err != OK || file.is_null()) {
		r_error_message = vformat(TTR("Cannot create temporary refactor target '%s'."), temp_path);
		return false;
	}

	const bool stored = file->store_string(p_source);
	file->flush();
	const Error store_error = file->get_error();
	file->close();

	if (!stored || (store_error != OK && store_error != ERR_FILE_EOF)) {
		DirAccess::remove_absolute(temp_path);
		r_error_message = vformat(TTR("Cannot write temporary refactor target '%s'."), temp_path);
		return false;
	}

	if (FileAccess::exists(p_path)) {
		FileAccess::set_unix_permissions(temp_path, FileAccess::get_unix_permissions(p_path));
	}

	err = DirAccess::rename_absolute(temp_path, p_path);
	if (err != OK) {
		DirAccess::remove_absolute(temp_path);
		r_error_message = vformat(TTR("Cannot replace refactor target '%s'."), p_path);
		return false;
	}

	r_error_message = String();
	return true;
}

#endif // TOOLS_ENABLED
