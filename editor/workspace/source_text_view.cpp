/**************************************************************************/
/*  source_text_view.cpp                                                  */
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

#include "source_text_view.h"

#include "core/object/callable_method_pointer.h"
#include "core/os/keyboard.h"
#include "editor/gui/code_editor.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/settings/editor_settings.h"
#include "scene/gui/code_edit.h"

Ref<TextView> SourceTextView::create(const Ref<TextDocument> &p_document) {
	Ref<SourceTextView> view;
	view.instantiate();
	view->set_document(p_document);
	return view;
}

String SourceTextView::label() const {
	return TTR("Source");
}

CodeTextEditor *SourceTextView::_editor() const {
	return Object::cast_to<CodeTextEditor>(ObjectDB::get_instance(code_editor_id));
}

void SourceTextView::_apply_highlighter(CodeTextEditor *p_editor) const {
	ERR_FAIL_NULL(p_editor);
	CodeEdit *code_edit = p_editor->get_text_editor();
	ERR_FAIL_NULL(code_edit);

	const String extension = document.is_valid() ? document->get_path().get_extension().to_lower() : String();

	Ref<EditorSyntaxHighlighter> highlighter;
	if (extension == "md" || extension == "markdown") {
		highlighter = Ref<EditorSyntaxHighlighter>(memnew(EditorMarkdownSyntaxHighlighter));
	} else if (extension == "json") {
		highlighter = Ref<EditorSyntaxHighlighter>(memnew(EditorJSONSyntaxHighlighter));
	} else if (extension == "ini" || extension == "cfg" || extension == "tscn" || extension == "tres" || extension == "godot" || extension == "foundry") {
		highlighter = Ref<EditorSyntaxHighlighter>(memnew(EditorConfigFileSyntaxHighlighter));
	} else {
		highlighter = Ref<EditorSyntaxHighlighter>(memnew(EditorPlainTextSyntaxHighlighter));
	}

	code_edit->set_syntax_highlighter(highlighter);
}

Control *SourceTextView::get_control() {
	CodeTextEditor *editor = _editor();
	if (editor) {
		return editor;
	}

	editor = memnew(CodeTextEditor);
	code_editor_id = editor->get_instance_id();
	editor->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	editor->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	// Apply editor settings so the idle-parse timer and appearance are configured
	// (an unconfigured idle timer asserts on a zero wait time).
	editor->update_editor_settings();
	// CodeTextEditor's toggle-files affordance resolves this script-editor shortcut
	// during its notifications; ensure it exists so a standalone view (e.g. in a
	// test with no ScriptEditorController) does not dereference a null shortcut.
	// ED_SHORTCUT is idempotent, so the script editor's own registration is reused.
	ED_SHORTCUT("script_editor/toggle_files_panel", TTRC("Toggle Files Panel"), KeyModifierMask::CMD_OR_CTRL | Key::BACKSLASH);
	// The toggle-files affordance is script-editor chrome this standalone view does
	// not use; give it an inert list control so its notifications do not assert on
	// a null one.
	Control *inert_toggle_list = memnew(Control);
	inert_toggle_list->hide();
	editor->add_child(inert_toggle_list);
	editor->set_toggle_list_control(inert_toggle_list);
	editor->get_text_editor()->connect(SNAME("text_changed"), callable_mp(this, &SourceTextView::_on_text_changed));
	_apply_highlighter(editor);
	return editor;
}

void SourceTextView::_on_text_changed() {
	if (syncing || document.is_null()) {
		return;
	}
	CodeTextEditor *editor = _editor();
	if (!editor) {
		return;
	}
	// set_text only flips the dirty flag when the content truly changed.
	document->set_text(editor->get_text_editor()->get_text());
}

void SourceTextView::sync_from_document() {
	CodeTextEditor *editor = _editor();
	if (!editor || document.is_null()) {
		return;
	}
	// Loading the document text is not a user edit; suppress the text_changed
	// handler so it does not fabricate a dirty state or clobber the document.
	syncing = true;
	CodeEdit *code_edit = editor->get_text_editor();
	code_edit->set_text(document->get_text());
	if (!document->is_dirty()) {
		code_edit->tag_saved_version();
	}
	syncing = false;
}

void SourceTextView::flush_to_document() {
	CodeTextEditor *editor = _editor();
	if (!editor || document.is_null()) {
		return;
	}
	document->set_text(editor->get_text_editor()->get_text());
}

int SourceTextView::get_caret_line() const {
	CodeTextEditor *editor = _editor();
	return editor ? editor->get_text_editor()->get_caret_line() : 0;
}

int SourceTextView::get_caret_column() const {
	CodeTextEditor *editor = _editor();
	return editor ? editor->get_text_editor()->get_caret_column() : 0;
}

int SourceTextView::get_scroll() const {
	CodeTextEditor *editor = _editor();
	return editor ? (int)editor->get_text_editor()->get_v_scroll() : 0;
}

void SourceTextView::set_caret(int p_line, int p_column) {
	CodeTextEditor *editor = _editor();
	if (!editor) {
		return;
	}
	CodeEdit *code_edit = editor->get_text_editor();
	const int line_count = code_edit->get_line_count();
	const int line = CLAMP(p_line, 0, MAX(line_count - 1, 0));
	code_edit->set_caret_line(line);
	code_edit->set_caret_column(CLAMP(p_column, 0, code_edit->get_line(line).length()));
}

void SourceTextView::set_scroll(int p_scroll) {
	CodeTextEditor *editor = _editor();
	if (!editor) {
		return;
	}
	editor->get_text_editor()->set_v_scroll(p_scroll);
}

void SourceTextView::grab_view_focus() {
	CodeTextEditor *editor = _editor();
	if (editor) {
		editor->get_text_editor()->grab_focus();
	}
}
