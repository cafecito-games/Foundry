/**************************************************************************/
/*  script_text_editor.h                                                  */
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

#include "extract_method_name_prompt.h"
#include "script_editor_plugin.h"
#include "script_refactor_apply.h"

#include "editor/gui/code_editor.h"
#include "scene/gui/color_picker.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/item_list.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/tree.h"

#include "modules/foundry_script/editor/fs_refactoring.h"

class RichTextLabel;
class RefactorDiffPreviewDialog;

class ConnectionInfoDialog : public AcceptDialog {
	FOUNDRY_CLASS(ConnectionInfoDialog, AcceptDialog);

	Label *method = nullptr;
	Tree *tree = nullptr;

	virtual void ok_pressed() override;

public:
	void popup_connections(const String &p_method, const Vector<Node *> &p_nodes);

	ConnectionInfoDialog();
};

class ScriptTextEditor : public ScriptEditorBase {
	FOUNDRY_CLASS(ScriptTextEditor, ScriptEditorBase);

	CodeTextEditor *code_editor = nullptr;
	RichTextLabel *warnings_panel = nullptr;
	RichTextLabel *errors_panel = nullptr;

	Ref<Script> script;
	Variant pending_state;
	bool script_is_valid = false;
	bool editor_enabled = false;

	Vector<String> functions;
	List<ScriptLanguage::Warning> warnings;
	List<ScriptLanguage::ScriptError> errors;
	HashMap<String, List<ScriptLanguage::ScriptError>> depended_errors;
	HashSet<int> safe_lines;

	List<Connection> missing_connections;

	HBoxContainer *edit_hb = nullptr;

	MenuButton *edit_menu = nullptr;
	MenuButton *search_menu = nullptr;
	MenuButton *goto_menu = nullptr;
	PopupMenu *bookmarks_menu = nullptr;
	PopupMenu *breakpoints_menu = nullptr;
	PopupMenu *highlighter_menu = nullptr;
	PopupMenu *context_menu = nullptr;
	ConfirmationDialog *rename_dialog = nullptr;
	LineEdit *rename_line_edit = nullptr;
	Label *rename_error_label = nullptr;
	ConfirmationDialog *extract_method_dialog = nullptr;
	LineEdit *extract_method_line_edit = nullptr;
	Label *extract_method_error_label = nullptr;
	ExtractMethodNamePromptModel extract_method_name_prompt;
	ConfirmationDialog *override_method_dialog = nullptr;
	LineEdit *override_method_filter = nullptr;
	ItemList *override_method_list = nullptr;
	Label *override_method_error_label = nullptr;
	Vector<RefactorOverrideMethodCandidate> override_method_candidates;
	Vector<int> override_method_filtered_indices;
	RefactorLocation override_method_location;
	RefactorDiffPreviewDialog *refactor_diff_preview_dialog = nullptr;
	int pending_refactor_anchor_line = -1;
	int pending_refactor_anchor_column = -1;
	String pending_refactor_anchor_path;
	String pending_refactor_warning;

	struct InlineRenameCaretState {
		int line = 0;
		int column = 0;
		bool has_selection = false;
		int selection_from_line = 0;
		int selection_from_column = 0;
		int selection_to_line = 0;
		int selection_to_column = 0;
	};

	bool inline_rename_active = false;
	RefactorContext inline_rename_context;
	RefactorLocation inline_rename_location;
	Vector<RefactorTextEdit> inline_rename_occurrences;
	Vector<InlineRenameCaretState> inline_rename_caret_states;

	int inline_color_line = -1;
	int inline_color_start = -1;
	int inline_color_end = -1;
	PopupPanel *inline_color_popup = nullptr;
	ColorPicker *inline_color_picker = nullptr;
	OptionButton *inline_color_options = nullptr;
	Ref<Texture2D> color_alpha_texture;

	GotoLinePopup *goto_line_popup = nullptr;
	ScriptEditorQuickOpen *quick_open = nullptr;
	ConnectionInfoDialog *connection_info_dialog = nullptr;

	int connection_gutter = -1;
	void _gutter_clicked(int p_line, int p_gutter);
	void _update_gutter_indexes();

	int line_number_gutter = -1;
	Color default_line_number_color = Color(1, 1, 1);
	Color safe_line_number_color = Color(1, 1, 1);

	Color marked_line_color = Color(1, 1, 1);
	Color warning_line_color = Color(1, 1, 1);
	Color folded_code_region_color = Color(1, 1, 1);
	int previous_line = 0;

	PopupPanel *color_panel = nullptr;
	ColorPicker *color_picker = nullptr;
	Vector3i color_position;
	String color_args;

	bool theme_loaded = false;

	enum {
		EDIT_UNDO,
		EDIT_REDO,
		EDIT_CUT,
		EDIT_COPY,
		EDIT_PASTE,
		EDIT_SELECT_ALL,
		EDIT_COMPLETE,
		EDIT_AUTO_INDENT,
		EDIT_TRIM_TRAILING_WHITESAPCE,
		EDIT_TRIM_FINAL_NEWLINES,
		EDIT_CONVERT_INDENT_TO_SPACES,
		EDIT_CONVERT_INDENT_TO_TABS,
		EDIT_FORMAT_DOCUMENT,
		EDIT_TOGGLE_COMMENT,
		EDIT_MOVE_LINE_UP,
		EDIT_MOVE_LINE_DOWN,
		EDIT_INDENT,
		EDIT_UNINDENT,
		EDIT_DELETE_LINE,
		EDIT_DUPLICATE_SELECTION,
		EDIT_DUPLICATE_LINES,
		EDIT_PICK_COLOR,
		EDIT_TO_UPPERCASE,
		EDIT_TO_LOWERCASE,
		EDIT_CAPITALIZE,
		EDIT_EVALUATE,
		EDIT_TOGGLE_WORD_WRAP,
		EDIT_TOGGLE_FOLD_LINE,
		EDIT_FOLD_ALL_LINES,
		EDIT_CREATE_CODE_REGION,
		EDIT_UNFOLD_ALL_LINES,
		SEARCH_FIND,
		SEARCH_FIND_NEXT,
		SEARCH_FIND_PREV,
		SEARCH_REPLACE,
		SEARCH_LOCATE_FUNCTION,
		SEARCH_GOTO_LINE,
		SEARCH_IN_FILES,
		REPLACE_IN_FILES,
		BOOKMARK_TOGGLE,
		BOOKMARK_GOTO_NEXT,
		BOOKMARK_GOTO_PREV,
		BOOKMARK_REMOVE_ALL,
		DEBUG_TOGGLE_BREAKPOINT,
		DEBUG_REMOVE_ALL_BREAKPOINTS,
		DEBUG_GOTO_NEXT_BREAKPOINT,
		DEBUG_GOTO_PREV_BREAKPOINT,
		HELP_CONTEXTUAL,
		LOOKUP_SYMBOL,
		EDIT_EMOJI_AND_SYMBOL,
		// These must stay in the same order as RefactorKind: the dispatch maps
		// the option id back to a RefactorKind via (id - EDIT_REFACTOR_RENAME).
		EDIT_REFACTOR_RENAME,
		EDIT_REFACTOR_EXTRACT_VARIABLE,
		EDIT_REFACTOR_EXTRACT_METHOD,
		EDIT_REFACTOR_ADD_TYPE_ANNOTATION,
		EDIT_REFACTOR_INLINE_VARIABLE,
		EDIT_REFACTOR_IMPLEMENT_ABSTRACT_METHODS,
		EDIT_REFACTOR_OVERRIDE_METHOD,
		EDIT_REFACTOR_INSERT_EXPLICIT_CAST,
		EDIT_REFACTOR_WIDEN_TO_NULLABLE,
		EDIT_REFACTOR_SORT_MEMBERS_BY_STYLE_GUIDE,
	};

	enum COLOR_MODE {
		MODE_RGB,
		MODE_STRING,
		MODE_HSV,
		MODE_OKHSL,
		MODE_RGB8,
		MODE_HEX,
		MODE_MAX
	};

	void _enable_code_editor();

	struct DraggedExport {
		ObjectID obj_id;
		String variable_name;
		Variant value;
		String class_name;
	};

	LocalVector<DraggedExport> pending_dragged_exports;
	Vector<ObjectID> _get_objects_for_export_assignment() const;
	String _get_dropped_resource_as_exported_member(const Ref<Resource> &p_resource, const Vector<ObjectID> &p_script_instance_obj_ids);
	void _assign_dragged_export_variables();

protected:
	void _update_breakpoint_list();
	void _breakpoint_item_pressed(int p_idx);
	void _breakpoint_toggled(int p_row);

	void _on_caret_moved();

	void _validate_script(); // No longer virtual.
	void _update_warnings();
	void _update_errors();
	void _update_bookmark_list();
	void _bookmark_item_pressed(int p_idx);

	static void _code_complete_scripts(void *p_ud, const String &p_code, List<ScriptLanguage::CodeCompletionOption> *r_options, bool &r_force);
	void _code_complete_script(const String &p_code, List<ScriptLanguage::CodeCompletionOption> *r_options, bool &r_force);

	void _load_theme_settings();
	void _set_theme_for_script();
	void _show_errors_panel(bool p_show);
	void _show_warnings_panel(bool p_show);
	void _error_clicked(const Variant &p_line);
	void _warning_clicked(const Variant &p_line);

	bool _is_valid_color_info(const Dictionary &p_info);
	Array _inline_object_parse(const String &p_text);
	void _inline_object_draw(const Dictionary &p_info, const Rect2 &p_rect);
	void _inline_object_handle_click(const Dictionary &p_info, const Rect2 &p_rect);
	String _picker_color_stringify(const Color &p_color, COLOR_MODE p_mode);
	void _picker_color_changed(const Color &p_color);
	void _update_color_constructor_options();
	void _update_background_color();
	void _update_color_text();

	void _notification(int p_what);

	HashMap<String, Ref<EditorSyntaxHighlighter>> highlighters;
	void _change_syntax_highlighter(int p_idx);

	void _edit_option(int p_op);
	void _edit_option_toggle_inline_comment();
	void _make_context_menu(bool p_selection, bool p_color, bool p_foldable, bool p_open_docs, bool p_goto_definition, Vector2 p_pos);

	void _populate_refactor_submenu(PopupMenu *p_refactor_submenu);
	void _run_refactor(int p_kind);
	bool _collect_refactor_sources(const Vector<RefactorFileEdit> &p_file_edits, Vector<ScriptRefactorSource> &r_sources, String &r_error_message) const;
	void _apply_refactor_result(const RefactorResult &p_result, const String &p_source);
	void _show_refactor_diff_preview(
			const ScriptRefactorApplyPlan &p_plan,
			const Vector<RefactorUnresolvedReference> &p_unresolved_references,
			int p_anchor_line,
			int p_anchor_column,
			const String &p_warning);
	void _on_refactor_diff_confirmed();
	void _on_refactor_diff_canceled();
	void _clear_pending_refactor_preview();
	bool _try_start_inline_rename(const RefactorContext &p_context, const RefactorLocation &p_location);
	bool _is_inline_rename_safe(const RefactorResult &p_result, const RefactorContext &p_context) const;
	int _find_inline_rename_primary_occurrence(const Vector<RefactorTextEdit> &p_occurrences, const RefactorLocation &p_location) const;
	bool _select_inline_rename_occurrences(const Vector<RefactorTextEdit> &p_occurrences, int p_primary_occurrence);
	void _save_inline_rename_caret_state();
	void _restore_inline_rename_caret_state();
	String _get_inline_rename_name() const;
	bool _commit_inline_rename(bool p_allow_dialog_fallback = true);
	void _cancel_inline_rename(bool p_restore_text = true);
	void _clear_inline_rename_state();
	void _show_rename_dialog(const String &p_initial_name = String());
	void _on_rename_confirmed();
	void _on_rename_text_changed(const String &p_text);
	void _show_extract_method_dialog(
			const RefactorLocation &p_location,
			const String &p_suggested_name,
			const Vector<String> &p_existing_member_names);
	void _on_extract_method_confirmed();
	void _on_extract_method_canceled();
	void _on_extract_method_text_changed(const String &p_text);
	void _show_override_method_dialog(
			const RefactorLocation &p_location,
			const Vector<RefactorOverrideMethodCandidate> &p_candidates);
	void _populate_override_method_list(const String &p_filter);
	void _on_override_method_confirmed();
	void _on_override_method_canceled();
	void _on_override_method_filter_changed(const String &p_text);
	void _on_override_method_item_activated(int p_index);
	void _update_override_method_confirm_state();
	RefactorContext _make_refactor_context() const;
	RefactorLocation _make_refactor_location() const;
	void _text_edit_gui_input(const Ref<InputEvent> &ev);
	void _color_changed(const Color &p_color);
	void _prepare_edit_menu();

	void _goto_line(int p_line) { goto_line(p_line); }
	void _lookup_symbol(const String &p_symbol, int p_row, int p_column);
	void _validate_symbol(const String &p_symbol);

	void _show_symbol_tooltip(const String &p_symbol, int p_row, int p_column);

	void _convert_case(CodeTextEditor::CaseStyle p_case);

	Variant get_drag_data_fw(const Point2 &p_point, Control *p_from);
	bool can_drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from) const;
	void drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from);

	String _get_absolute_path(const String &rel_path);

public:
	void _update_connected_methods();

	virtual void add_syntax_highlighter(Ref<EditorSyntaxHighlighter> p_highlighter) override;
	virtual void set_syntax_highlighter(Ref<EditorSyntaxHighlighter> p_highlighter) override;
	void update_toggle_files_button() override;

	virtual void apply_code() override;
	virtual Ref<Resource> get_edited_resource() const override;
	virtual void set_edited_resource(const Ref<Resource> &p_res) override;
	virtual void enable_editor(Control *p_shortcut_context = nullptr) override;
	virtual Vector<String> get_functions() override;
	virtual void reload_text() override;
	virtual String get_name() override;
	virtual Ref<Texture2D> get_theme_icon() override;
	virtual bool is_unsaved() override;
	virtual Variant get_edit_state() override;
	virtual void set_edit_state(const Variant &p_state) override;
	virtual Variant get_navigation_state() override;
	virtual void ensure_focus() override;
	virtual void trim_trailing_whitespace() override;
	virtual void trim_final_newlines() override;
	virtual void insert_final_newline() override;
	virtual void convert_indent() override;
	virtual void format_document(bool p_notify_on_error) override;
	virtual void tag_saved_version() override;

	virtual void goto_line(int p_line, int p_column = 0) override;
	void goto_line_selection(int p_line, int p_begin, int p_end);
	void goto_line_centered(int p_line, int p_column = 0);
	virtual void set_executing_line(int p_line) override;
	virtual void clear_executing_line() override;

	virtual void reload(bool p_soft) override;
	virtual PackedInt32Array get_breakpoints() override;
	virtual void set_breakpoint(int p_line, bool p_enabled) override;
	virtual void clear_breakpoints() override;

	virtual void add_callback(const String &p_function, const PackedStringArray &p_args) override;
	virtual void update_settings() override;

	virtual bool show_members_overview() override;

	virtual void set_tooltip_request_func(const Callable &p_toolip_callback) override;

	virtual void set_debugger_active(bool p_active) override;

	Control *get_edit_menu() override;
	virtual void clear_edit_menu() override;
	virtual void set_find_replace_bar(FindReplaceBar *p_bar) override;

	static void register_editor();

	virtual Control *get_base_editor() const override;
	virtual CodeTextEditor *get_code_editor() const override;

	virtual void validate() override;

	Variant get_previous_state();
	void store_previous_state();

	ScriptTextEditor();
	~ScriptTextEditor();
};
