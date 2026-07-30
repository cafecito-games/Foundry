/**************************************************************************/
/*  script_refactor_apply.h                                               */
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

#ifdef TOOLS_ENABLED

#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "modules/foundry_script/editor/fs_refactoring.h"

class CodeEdit;

struct ScriptRefactorSource {
	String path;
	String source;
	bool source_is_saved_version = true;
};

struct ScriptRefactorFilePlan {
	String path;
	String before_source;
	String after_source;
	int edit_count = 0;
	bool before_source_is_saved_version = true;
	// Individual edits that produced after_source, retained so the preview can
	// recompute the result from a user-selected subset of edits.
	Vector<RefactorTextEdit> edits;
};

struct ScriptRefactorApplyPlan {
	Vector<ScriptRefactorFilePlan> files;

	bool is_empty() const {
		return files.is_empty();
	}
};

namespace ScriptRefactorApply {

bool build_plan(
		const Vector<RefactorFileEdit> &p_file_edits,
		const Vector<ScriptRefactorSource> &p_sources,
		ScriptRefactorApplyPlan &r_plan,
		String &r_error_message);
void replace_editor_text(CodeEdit *p_text_editor, const String &p_source, bool p_source_is_saved_version);
bool can_write_file(const String &p_path, String &r_error_message);
bool write_file(const String &p_path, const String &p_source, String &r_error_message);

} // namespace ScriptRefactorApply

#endif // TOOLS_ENABLED
