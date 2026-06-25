/**************************************************************************/
/*  refactor_diff_preview.h                                               */
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

#pragma once

#ifdef TOOLS_ENABLED

#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "editor/script/script_refactor_apply.h"
#include "scene/gui/dialogs.h"

class Button;
class Label;
class RichTextLabel;
class Tree;
class TreeItem;

struct RefactorDiffPreviewFile {
	String path;
	String before_source;
	String after_source;
	int edit_count = 0;
	bool before_source_is_saved_version = true;
	// Individual edits that produced after_source. When present the user can
	// accept or reject each one independently; an empty list keeps the file as a
	// single all-or-nothing unit driven by the stored after_source.
	Vector<RefactorTextEdit> edits;
};

struct RefactorDiffPreviewLine {
	int old_line = -1;
	int new_line = -1;
	String old_text;
	String new_text;
	bool changed = false;
	bool old_changed = false;
	bool new_changed = false;
};

class RefactorDiffPreviewModel {
	Vector<RefactorDiffPreviewFile> files;
	// Per-file, per-edit acceptance flags. A file without tracked edits keeps a
	// single implicit slot so whole-file accept/reject still works.
	Vector<Vector<uint8_t>> accepted_edits;
	int selected_index = -1;

	bool has_accepted_edit(int p_file_index) const;
	Vector<RefactorTextEdit> collect_accepted_edits(int p_file_index) const;

public:
	void set_files(const Vector<RefactorDiffPreviewFile> &p_files);
	void set_apply_plan(const ScriptRefactorApplyPlan &p_plan);

	int get_file_count() const;
	const RefactorDiffPreviewFile &get_file(int p_index) const;

	bool select_file(int p_index);
	int get_selected_index() const;
	RefactorDiffPreviewFile get_selected_file() const;

	bool accept_file(int p_index);
	bool reject_file(int p_index);
	bool is_file_accepted(int p_index) const;

	int get_edit_count(int p_file_index) const;
	bool accept_edit(int p_file_index, int p_edit_index);
	bool reject_edit(int p_file_index, int p_edit_index);
	bool is_edit_accepted(int p_file_index, int p_edit_index) const;

	// Source after applying only the currently-accepted edits of a file. Used to
	// keep the diff preview in sync as individual edits are toggled.
	String get_effective_after_source(int p_file_index) const;

	int get_accepted_file_count() const;
	Vector<String> get_accepted_paths() const;
	ScriptRefactorApplyPlan get_accepted_apply_plan() const;

	static Vector<RefactorDiffPreviewLine> make_diff_lines(const String &p_before_source, const String &p_after_source);
};

class RefactorDiffPreviewDialog : public ConfirmationDialog {
	GDCLASS(RefactorDiffPreviewDialog, ConfirmationDialog);

	enum TreeColumn {
		COLUMN_FILE,
		COLUMN_EDITS,
	};

	RefactorDiffPreviewModel model;

	Tree *file_tree = nullptr;
	RichTextLabel *diff_view = nullptr;
	Label *unresolved_references_label = nullptr;
	Label *summary_label = nullptr;
	Button *accept_file_button = nullptr;
	Button *reject_file_button = nullptr;
	Vector<RefactorUnresolvedReference> unresolved_references;

	void _rebuild_file_tree();
	void _select_file(int p_index);
	void _refresh_diff();
	void _refresh_unresolved_references();
	void _refresh_footer();
	void _set_file_accepted(int p_index, bool p_accepted);

	void _file_selected();
	void _edit_toggled();
	void _accept_current_file();
	void _reject_current_file();

public:
	void popup_preview(
			const ScriptRefactorApplyPlan &p_plan,
			const Vector<RefactorUnresolvedReference> &p_unresolved_references);

	ScriptRefactorApplyPlan get_accepted_apply_plan() const;

	RefactorDiffPreviewDialog();
};

#endif // TOOLS_ENABLED
