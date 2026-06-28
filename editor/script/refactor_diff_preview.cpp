/**************************************************************************/
/*  refactor_diff_preview.cpp                                             */
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

#include "refactor_diff_preview.h"

#ifdef TOOLS_ENABLED

#include "editor/editor_string_names.h"
#include "editor/themes/editor_scale.h"
#include "modules/foundry_script/editor/fs_refactoring_edits.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tree.h"

namespace {

Vector<String> split_source_lines(const String &p_source) {
	Vector<String> lines;
	if (p_source.is_empty()) {
		return lines;
	}

	lines = p_source.split("\n", true);
	if (!lines.is_empty() && lines[lines.size() - 1].is_empty()) {
		lines.remove_at(lines.size() - 1);
	}
	return lines;
}

struct DiffOp {
	enum Type {
		EQUAL,
		DELETE,
		INSERT,
	};

	Type type = EQUAL;
	int old_index = -1;
	int new_index = -1;
};

int lcs_at(const Vector<int> &p_lcs, int p_new_line_count, int p_old_index, int p_new_index) {
	return p_lcs[p_old_index * (p_new_line_count + 1) + p_new_index];
}

void set_lcs_at(Vector<int> &r_lcs, int p_new_line_count, int p_old_index, int p_new_index, int p_value) {
	r_lcs.write[p_old_index * (p_new_line_count + 1) + p_new_index] = p_value;
}

void append_diff_row(
		Vector<RefactorDiffPreviewLine> &r_rows,
		const Vector<String> &p_old_lines,
		const Vector<String> &p_new_lines,
		int p_old_index,
		int p_new_index,
		bool p_changed) {
	RefactorDiffPreviewLine row;
	row.old_line = p_old_index >= 0 ? p_old_index + 1 : -1;
	row.new_line = p_new_index >= 0 ? p_new_index + 1 : -1;
	row.old_text = p_old_index >= 0 ? p_old_lines[p_old_index] : String();
	row.new_text = p_new_index >= 0 ? p_new_lines[p_new_index] : String();
	row.changed = p_changed;
	row.old_changed = p_changed && p_old_index >= 0;
	row.new_changed = p_changed && p_new_index >= 0;
	r_rows.push_back(row);
}

void add_diff_cell(RichTextLabel *p_label, const String &p_text, const Color &p_color) {
	p_label->push_cell();
	p_label->push_color(p_color);
	p_label->add_text(p_text);
	p_label->pop();
	p_label->pop();
}

String left_pad_number(int p_line, int p_width) {
	return (p_line >= 0 ? itos(p_line) : String()).lpad(p_width);
}

String format_diff_side(int p_line, int p_width, bool p_changed, bool p_added, const String &p_text) {
	const String marker = p_changed ? (p_added ? "+" : "-") : " ";
	return left_pad_number(p_line, p_width) + " " + marker + " " + p_text;
}

String summarize_edit(const RefactorTextEdit &p_edit) {
	String replacement = p_edit.new_text.replace("\n", "⏎").replace("\t", " ").strip_edges();
	const int max_length = 48;
	if (replacement.length() > max_length) {
		replacement = replacement.left(max_length - 1) + "…";
	}
	const String location = vformat(TTR("Line %d"), p_edit.start_line + 1);
	if (replacement.is_empty()) {
		return location;
	}
	return vformat("%s: %s", location, replacement);
}

} // namespace

void RefactorDiffPreviewModel::set_files(const Vector<RefactorDiffPreviewFile> &p_files) {
	files = p_files;
	accepted_edits.resize(files.size());
	for (int i = 0; i < files.size(); i++) {
		// Files without tracked edits keep a single implicit acceptance slot so
		// whole-file accept/reject still toggles them as one unit.
		const int slot_count = MAX(1, files[i].edits.size());
		Vector<uint8_t> slots;
		slots.resize(slot_count);
		for (int j = 0; j < slot_count; j++) {
			slots.write[j] = true;
		}
		accepted_edits.write[i] = slots;
	}
	selected_index = files.is_empty() ? -1 : 0;
}

void RefactorDiffPreviewModel::set_apply_plan(const ScriptRefactorApplyPlan &p_plan) {
	Vector<RefactorDiffPreviewFile> preview_files;
	for (const ScriptRefactorFilePlan &file_plan : p_plan.files) {
		RefactorDiffPreviewFile preview_file;
		preview_file.path = file_plan.path;
		preview_file.before_source = file_plan.before_source;
		preview_file.after_source = file_plan.after_source;
		preview_file.edit_count = file_plan.edit_count;
		preview_file.before_source_is_saved_version = file_plan.before_source_is_saved_version;
		preview_file.edits = file_plan.edits;
		preview_files.push_back(preview_file);
	}
	set_files(preview_files);
}

bool RefactorDiffPreviewModel::has_accepted_edit(int p_file_index) const {
	ERR_FAIL_INDEX_V(p_file_index, accepted_edits.size(), false);
	const Vector<uint8_t> &slots = accepted_edits[p_file_index];
	for (int i = 0; i < slots.size(); i++) {
		if (slots[i]) {
			return true;
		}
	}
	return false;
}

Vector<RefactorTextEdit> RefactorDiffPreviewModel::collect_accepted_edits(int p_file_index) const {
	Vector<RefactorTextEdit> accepted;
	const Vector<RefactorTextEdit> &edits = files[p_file_index].edits;
	const Vector<uint8_t> &slots = accepted_edits[p_file_index];
	for (int i = 0; i < edits.size(); i++) {
		if (i < slots.size() && slots[i]) {
			accepted.push_back(edits[i]);
		}
	}
	return accepted;
}

int RefactorDiffPreviewModel::get_file_count() const {
	return files.size();
}

const RefactorDiffPreviewFile &RefactorDiffPreviewModel::get_file(int p_index) const {
	CRASH_BAD_INDEX(p_index, files.size());
	return files[p_index];
}

bool RefactorDiffPreviewModel::select_file(int p_index) {
	ERR_FAIL_INDEX_V(p_index, files.size(), false);
	selected_index = p_index;
	return true;
}

int RefactorDiffPreviewModel::get_selected_index() const {
	return selected_index;
}

RefactorDiffPreviewFile RefactorDiffPreviewModel::get_selected_file() const {
	if (selected_index < 0 || selected_index >= files.size()) {
		return RefactorDiffPreviewFile();
	}
	return files[selected_index];
}

bool RefactorDiffPreviewModel::accept_file(int p_index) {
	ERR_FAIL_INDEX_V(p_index, accepted_edits.size(), false);
	Vector<uint8_t> &slots = accepted_edits.write[p_index];
	for (int i = 0; i < slots.size(); i++) {
		slots.write[i] = true;
	}
	return true;
}

bool RefactorDiffPreviewModel::reject_file(int p_index) {
	ERR_FAIL_INDEX_V(p_index, accepted_edits.size(), false);
	Vector<uint8_t> &slots = accepted_edits.write[p_index];
	for (int i = 0; i < slots.size(); i++) {
		slots.write[i] = false;
	}
	return true;
}

bool RefactorDiffPreviewModel::is_file_accepted(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, accepted_edits.size(), false);
	return has_accepted_edit(p_index);
}

bool RefactorDiffPreviewModel::is_file_fully_accepted(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, accepted_edits.size(), false);
	const Vector<uint8_t> &slots = accepted_edits[p_index];
	for (int i = 0; i < slots.size(); i++) {
		if (!slots[i]) {
			return false;
		}
	}
	return true;
}

int RefactorDiffPreviewModel::get_edit_count(int p_file_index) const {
	ERR_FAIL_INDEX_V(p_file_index, files.size(), 0);
	return files[p_file_index].edits.size();
}

bool RefactorDiffPreviewModel::accept_edit(int p_file_index, int p_edit_index) {
	ERR_FAIL_INDEX_V(p_file_index, files.size(), false);
	ERR_FAIL_INDEX_V(p_edit_index, files[p_file_index].edits.size(), false);
	accepted_edits.write[p_file_index].write[p_edit_index] = true;
	return true;
}

bool RefactorDiffPreviewModel::reject_edit(int p_file_index, int p_edit_index) {
	ERR_FAIL_INDEX_V(p_file_index, files.size(), false);
	ERR_FAIL_INDEX_V(p_edit_index, files[p_file_index].edits.size(), false);
	accepted_edits.write[p_file_index].write[p_edit_index] = false;
	return true;
}

bool RefactorDiffPreviewModel::is_edit_accepted(int p_file_index, int p_edit_index) const {
	ERR_FAIL_INDEX_V(p_file_index, files.size(), false);
	ERR_FAIL_INDEX_V(p_edit_index, files[p_file_index].edits.size(), false);
	return accepted_edits[p_file_index][p_edit_index];
}

String RefactorDiffPreviewModel::get_effective_after_source(int p_file_index) const {
	ERR_FAIL_INDEX_V(p_file_index, files.size(), String());
	const RefactorDiffPreviewFile &file = files[p_file_index];
	if (file.edits.is_empty()) {
		return is_file_accepted(p_file_index) ? file.after_source : file.before_source;
	}

	const Vector<RefactorTextEdit> accepted = collect_accepted_edits(p_file_index);
	if (accepted.is_empty()) {
		return file.before_source;
	}

	String after_source;
	if (!FSRefactorEdits::apply(file.before_source, accepted, after_source)) {
		return file.before_source;
	}
	return after_source;
}

int RefactorDiffPreviewModel::get_accepted_file_count() const {
	int count = 0;
	for (int i = 0; i < files.size(); i++) {
		if (is_file_accepted(i)) {
			count++;
		}
	}
	return count;
}

Vector<String> RefactorDiffPreviewModel::get_accepted_paths() const {
	Vector<String> paths;
	for (int i = 0; i < files.size(); i++) {
		if (is_file_accepted(i)) {
			paths.push_back(files[i].path);
		}
	}
	return paths;
}

ScriptRefactorApplyPlan RefactorDiffPreviewModel::get_accepted_apply_plan() const {
	ScriptRefactorApplyPlan plan;
	for (int i = 0; i < files.size(); i++) {
		const RefactorDiffPreviewFile &file = files[i];

		ScriptRefactorFilePlan file_plan;
		file_plan.path = file.path;
		file_plan.before_source = file.before_source;
		file_plan.before_source_is_saved_version = file.before_source_is_saved_version;

		if (file.edits.is_empty()) {
			if (!is_file_accepted(i)) {
				continue;
			}
			file_plan.after_source = file.after_source;
			file_plan.edit_count = file.edit_count;
		} else {
			const Vector<RefactorTextEdit> accepted = collect_accepted_edits(i);
			if (accepted.is_empty()) {
				continue;
			}
			String after_source;
			if (!FSRefactorEdits::apply(file.before_source, accepted, after_source)) {
				continue;
			}
			file_plan.after_source = after_source;
			file_plan.edit_count = accepted.size();
			file_plan.edits = accepted;
		}

		plan.files.push_back(file_plan);
	}
	return plan;
}

Vector<RefactorDiffPreviewLine> RefactorDiffPreviewModel::make_diff_lines(
		const String &p_before_source,
		const String &p_after_source) {
	const Vector<String> old_lines = split_source_lines(p_before_source);
	const Vector<String> new_lines = split_source_lines(p_after_source);
	const int old_count = old_lines.size();
	const int new_count = new_lines.size();

	Vector<int> lcs;
	lcs.resize_initialized((old_count + 1) * (new_count + 1));
	for (int i = old_count - 1; i >= 0; i--) {
		for (int j = new_count - 1; j >= 0; j--) {
			if (old_lines[i] == new_lines[j]) {
				set_lcs_at(lcs, new_count, i, j, lcs_at(lcs, new_count, i + 1, j + 1) + 1);
			} else {
				set_lcs_at(lcs, new_count, i, j, MAX(lcs_at(lcs, new_count, i + 1, j), lcs_at(lcs, new_count, i, j + 1)));
			}
		}
	}

	Vector<DiffOp> ops;
	int old_index = 0;
	int new_index = 0;
	while (old_index < old_count || new_index < new_count) {
		DiffOp op;
		if (old_index < old_count && new_index < new_count && old_lines[old_index] == new_lines[new_index]) {
			op.type = DiffOp::EQUAL;
			op.old_index = old_index++;
			op.new_index = new_index++;
		} else if (old_index < old_count &&
				(new_index >= new_count ||
						lcs_at(lcs, new_count, old_index + 1, new_index) >=
								lcs_at(lcs, new_count, old_index, new_index + 1))) {
			op.type = DiffOp::DELETE;
			op.old_index = old_index++;
		} else {
			op.type = DiffOp::INSERT;
			op.new_index = new_index++;
		}
		ops.push_back(op);
	}

	Vector<RefactorDiffPreviewLine> rows;
	int op_index = 0;
	while (op_index < ops.size()) {
		if (ops[op_index].type == DiffOp::EQUAL) {
			append_diff_row(rows, old_lines, new_lines, ops[op_index].old_index, ops[op_index].new_index, false);
			op_index++;
			continue;
		}

		Vector<int> deleted;
		Vector<int> inserted;
		while (op_index < ops.size() && ops[op_index].type != DiffOp::EQUAL) {
			if (ops[op_index].type == DiffOp::DELETE) {
				deleted.push_back(ops[op_index].old_index);
			} else {
				inserted.push_back(ops[op_index].new_index);
			}
			op_index++;
		}

		const int paired_count = MAX(deleted.size(), inserted.size());
		for (int i = 0; i < paired_count; i++) {
			append_diff_row(
					rows,
					old_lines,
					new_lines,
					i < deleted.size() ? deleted[i] : -1,
					i < inserted.size() ? inserted[i] : -1,
					true);
		}
	}

	return rows;
}

void RefactorDiffPreviewDialog::_rebuild_file_tree() {
	file_tree->clear();
	TreeItem *root = file_tree->create_item();
	const Color disabled_font_color = get_theme_color(SNAME("disabled_font_color"), EditorStringName(Editor));

	for (int i = 0; i < model.get_file_count(); i++) {
		const RefactorDiffPreviewFile &file = model.get_file(i);
		TreeItem *item = file_tree->create_item(root);
		item->set_text(COLUMN_FILE, file.path);
		item->set_tooltip_text(COLUMN_FILE, file.path);
		item->set_metadata(COLUMN_FILE, Vector2i(i, -1));
		item->set_text(COLUMN_EDITS, vformat(TTRN("%d edit", "%d edits", file.edit_count), file.edit_count));
		item->set_selectable(COLUMN_EDITS, false);
		const bool file_accepted = model.is_file_accepted(i);
		if (!file_accepted) {
			item->set_custom_color(COLUMN_FILE, disabled_font_color);
			item->set_custom_color(COLUMN_EDITS, disabled_font_color);
		}

		const int edit_count = model.get_edit_count(i);
		for (int j = 0; j < edit_count; j++) {
			TreeItem *edit_item = file_tree->create_item(item);
			edit_item->set_cell_mode(COLUMN_FILE, TreeItem::CELL_MODE_CHECK);
			edit_item->set_editable(COLUMN_FILE, true);
			edit_item->set_checked(COLUMN_FILE, model.is_edit_accepted(i, j));
			const String summary = summarize_edit(file.edits[j]);
			edit_item->set_text(COLUMN_FILE, summary);
			edit_item->set_tooltip_text(COLUMN_FILE, summary);
			edit_item->set_metadata(COLUMN_FILE, Vector2i(i, j));
			edit_item->set_selectable(COLUMN_EDITS, false);
		}

		if (i == model.get_selected_index()) {
			item->select(COLUMN_FILE);
		}
	}
}

void RefactorDiffPreviewDialog::_select_file(int p_index) {
	if (!model.select_file(p_index)) {
		return;
	}
	_rebuild_file_tree();
	_refresh_diff();
	_refresh_footer();
}

void RefactorDiffPreviewDialog::_refresh_diff() {
	diff_view->clear();

	if (model.get_selected_index() < 0) {
		diff_view->add_text(TTR("No refactor edits to preview."));
		return;
	}

	const RefactorDiffPreviewFile file = model.get_selected_file();
	const String after_source = model.get_effective_after_source(model.get_selected_index());
	const Vector<RefactorDiffPreviewLine> rows =
			RefactorDiffPreviewModel::make_diff_lines(file.before_source, after_source);
	const Color unchanged_color = get_theme_color(SNAME("font_color"), EditorStringName(Editor));
	const Color removed_color = get_theme_color(SNAME("error_color"), EditorStringName(Editor));
	const Color added_color = get_theme_color(SNAME("success_color"), EditorStringName(Editor));
	const Color gutter_color = get_theme_color(SNAME("disabled_font_color"), EditorStringName(Editor));

	int line_number_width = 1;
	for (const RefactorDiffPreviewLine &row : rows) {
		if (row.old_line >= 0) {
			line_number_width = MAX(line_number_width, itos(row.old_line).length());
		}
		if (row.new_line >= 0) {
			line_number_width = MAX(line_number_width, itos(row.new_line).length());
		}
	}

	diff_view->push_mono();
	diff_view->push_table(2);
	diff_view->set_table_column_expand(0, true);
	diff_view->set_table_column_expand(1, true);

	add_diff_cell(diff_view, TTR("Before"), gutter_color);
	add_diff_cell(diff_view, TTR("After"), gutter_color);

	for (const RefactorDiffPreviewLine &row : rows) {
		const Color old_color = row.old_changed ? removed_color : unchanged_color;
		const Color new_color = row.new_changed ? added_color : unchanged_color;

		add_diff_cell(
				diff_view,
				format_diff_side(row.old_line, line_number_width, row.old_changed, false, row.old_text),
				old_color);
		add_diff_cell(
				diff_view,
				format_diff_side(row.new_line, line_number_width, row.new_changed, true, row.new_text),
				new_color);
	}

	diff_view->pop();
	diff_view->pop();
}

void RefactorDiffPreviewDialog::_refresh_unresolved_references() {
	if (unresolved_references.is_empty()) {
		unresolved_references_label->hide();
		unresolved_references_label->set_text(String());
		return;
	}

	String text = vformat(
			TTRN(
					"%d unresolved dynamic reference will not be changed.",
					"%d unresolved dynamic references will not be changed.",
					unresolved_references.size()),
			unresolved_references.size());

	const int max_visible_references = MIN(unresolved_references.size(), 4);
	for (int i = 0; i < max_visible_references; i++) {
		const RefactorUnresolvedReference &reference = unresolved_references[i];
		text += "\n" + vformat("%s:%d:%d %s", reference.path, reference.line + 1, reference.column + 1, reference.message);
	}
	if (unresolved_references.size() > max_visible_references) {
		const int omitted_count = unresolved_references.size() - max_visible_references;
		text += "\n" + vformat(TTRN("%d more unresolved reference omitted.", "%d more unresolved references omitted.", omitted_count), omitted_count);
	}

	unresolved_references_label->set_text(text);
	unresolved_references_label->show();
}

void RefactorDiffPreviewDialog::_refresh_footer() {
	const int accepted_count = model.get_accepted_file_count();
	const int total_count = model.get_file_count();
	summary_label->set_text(vformat(
			TTRN("%d of %d file selected", "%d of %d files selected", total_count),
			accepted_count,
			total_count));

	const int selected = model.get_selected_index();
	const bool has_selected = selected >= 0;
	accept_file_button->set_disabled(!has_selected || model.is_file_fully_accepted(selected));
	reject_file_button->set_disabled(!has_selected || !model.is_file_accepted(selected));
	get_ok_button()->set_disabled(accepted_count == 0);
}

void RefactorDiffPreviewDialog::_set_file_accepted(int p_index, bool p_accepted) {
	if (p_accepted) {
		model.accept_file(p_index);
	} else {
		model.reject_file(p_index);
	}
	_rebuild_file_tree();
	_refresh_diff();
	_refresh_footer();
}

void RefactorDiffPreviewDialog::_file_selected() {
	TreeItem *selected = file_tree->get_selected();
	if (selected == nullptr) {
		return;
	}

	// Selecting either a file row or one of its edit rows previews that file.
	const Vector2i meta = selected->get_metadata(COLUMN_FILE);
	_select_file(meta.x);
}

void RefactorDiffPreviewDialog::_edit_toggled() {
	TreeItem *edited = file_tree->get_edited();
	if (edited == nullptr) {
		return;
	}

	const Vector2i meta = edited->get_metadata(COLUMN_FILE);
	if (meta.y < 0) {
		return;
	}

	if (edited->is_checked(COLUMN_FILE)) {
		model.accept_edit(meta.x, meta.y);
	} else {
		model.reject_edit(meta.x, meta.y);
	}

	// The toggled edit's file drives the preview so its diff reflects the change.
	model.select_file(meta.x);
	_refresh_diff();
	_refresh_footer();
	// This runs from Tree::item_edited while the Tree is still dispatching the
	// checkbox click (blocked > 0), where clear()/create_item() are rejected.
	// Defer the rebuild so the parent file row's accepted styling refreshes once
	// the Tree is no longer blocked.
	callable_mp(this, &RefactorDiffPreviewDialog::_rebuild_file_tree).call_deferred();
}

void RefactorDiffPreviewDialog::_accept_current_file() {
	const int selected = model.get_selected_index();
	if (selected >= 0) {
		_set_file_accepted(selected, true);
	}
}

void RefactorDiffPreviewDialog::_reject_current_file() {
	const int selected = model.get_selected_index();
	if (selected >= 0) {
		_set_file_accepted(selected, false);
	}
}

void RefactorDiffPreviewDialog::popup_preview(
		const ScriptRefactorApplyPlan &p_plan,
		const Vector<RefactorUnresolvedReference> &p_unresolved_references) {
	unresolved_references = p_unresolved_references;
	model.set_apply_plan(p_plan);
	_rebuild_file_tree();
	_refresh_diff();
	_refresh_unresolved_references();
	_refresh_footer();
	popup_centered_ratio(0.78);
}

ScriptRefactorApplyPlan RefactorDiffPreviewDialog::get_accepted_apply_plan() const {
	return model.get_accepted_apply_plan();
}

RefactorDiffPreviewDialog::RefactorDiffPreviewDialog() {
	set_title(TTRC("Preview Refactor"));
	set_ok_button_text(TTRC("Apply Selected"));
	set_cancel_button_text(TTRC("Cancel"));

	VBoxContainer *content = memnew(VBoxContainer);
	content->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	content->set_custom_minimum_size(Size2(760, 420) * EDSCALE);
	add_child(content);

	HSplitContainer *split = memnew(HSplitContainer);
	split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	content->add_child(split);

	file_tree = memnew(Tree);
	file_tree->set_columns(2);
	file_tree->set_column_title(0, TTRC("File"));
	file_tree->set_column_title(1, TTRC("Edits"));
	file_tree->set_column_titles_visible(true);
	file_tree->set_hide_root(true);
	file_tree->set_select_mode(Tree::SELECT_ROW);
	file_tree->set_custom_minimum_size(Size2(240, 260) * EDSCALE);
	file_tree->connect(SNAME("cell_selected"), callable_mp(this, &RefactorDiffPreviewDialog::_file_selected));
	file_tree->connect(SNAME("item_edited"), callable_mp(this, &RefactorDiffPreviewDialog::_edit_toggled));
	split->add_child(file_tree);

	diff_view = memnew(RichTextLabel);
	diff_view->set_use_bbcode(true);
	diff_view->set_selection_enabled(true);
	diff_view->set_context_menu_enabled(true);
	diff_view->set_scroll_active(true);
	diff_view->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	diff_view->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	split->add_child(diff_view);

	unresolved_references_label = memnew(Label);
	unresolved_references_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	unresolved_references_label->hide();
	content->add_child(unresolved_references_label);

	HBoxContainer *footer = memnew(HBoxContainer);
	content->add_child(footer);

	summary_label = memnew(Label);
	summary_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	footer->add_child(summary_label);

	accept_file_button = memnew(Button);
	accept_file_button->set_text(TTRC("Accept File"));
	accept_file_button->connect(
			SceneStringName(pressed),
			callable_mp(this, &RefactorDiffPreviewDialog::_accept_current_file));
	footer->add_child(accept_file_button);

	reject_file_button = memnew(Button);
	reject_file_button->set_text(TTRC("Reject File"));
	reject_file_button->connect(
			SceneStringName(pressed),
			callable_mp(this, &RefactorDiffPreviewDialog::_reject_current_file));
	footer->add_child(reject_file_button);
}

#endif // TOOLS_ENABLED
