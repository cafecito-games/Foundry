/**************************************************************************/
/*  script_editor_controller.h                                           */
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

#include "core/io/config_file.h"
#include "core/object/script_language.h"
#include "scene/resources/text_file.h"

class EditorSyntaxHighlighter;
class Timer;
class ScriptEditorBase;
typedef ScriptEditorBase *(*CreateScriptEditorFunc)(const Ref<Resource> &p_resource);

class EditorFileDialog;
class EditorHelpSearch;
class AcceptDialog;
class ConfirmationDialog;
class Tree;
class FindInFilesContainer;
class FindInFilesDialog;
class ScriptCreateDialog;
class ScriptEditorBase;
class ScriptEditorView;
struct ScriptRefactorApplyPlan;
class VSplitContainer;
class WindowWrapper;

class ScriptLeaf;

class ScriptEditorController : public Object {
	FOUNDRY_CLASS(ScriptEditorController, Object);

	enum {
		SCRIPT_EDITOR_FUNC_MAX = 32,
	};

	static int script_editor_func_count;
	static CreateScriptEditorFunc script_editor_funcs[SCRIPT_EDITOR_FUNC_MAX];

	static ScriptEditorController *singleton;

	Vector<Ref<EditorSyntaxHighlighter>> syntax_highlighters;

	Ref<ConfigFile> script_editor_cache;

	FindInFilesDialog *find_in_files_dialog = nullptr;
	FindInFilesContainer *find_in_files = nullptr;

	EditorHelpSearch *help_search_dialog = nullptr;

	EditorFileDialog *file_dialog = nullptr;
	ScriptCreateDialog *script_create_dialog = nullptr;
	AcceptDialog *error_dialog = nullptr;
	ConfirmationDialog *disk_changed = nullptr;
	Tree *disk_changed_list = nullptr;

	Timer *autosave_timer = nullptr;

	List<String> previous_scripts;
	List<int> script_close_queue;

	bool pending_auto_reload = false;
	bool auto_reload_running_scripts = true;
	Vector<String> script_paths_to_reload;
	bool trim_trailing_whitespace_on_save = false;
	bool trim_final_newlines_on_save = false;
	bool convert_indent_on_save = false;
	bool format_on_save = false;
	bool external_editor_active = false;
	bool feature_enabled = true;

	int file_dialog_option = -1;

	Vector<ScriptEditorView *> views;
	ScriptEditorView *focused_view = nullptr;
	ScriptEditorView *file_dialog_view = nullptr;

	// The view global actions resolve against: the focused view, else any open
	// view. Returns nullptr only when no script view exists.
	ScriptEditorView *_active_view() const;

	void _on_file_dialog_selected(const String &p_file);
	void _connect_global_signals();
	void _on_request_help(const String &p_topic);
	// Route a help topic to the workspace as its own help tab. Returns false when
	// no workspace exists so callers fall back to the legacy in-view help path.
	bool _open_help_in_workspace(const String &p_topic);
	void _on_request_help_search(const String &p_text);
	void _on_scene_closed(const String &p_path);
	void _on_script_add_function_request(Object *p_obj, const String &p_function, const PackedStringArray &p_args);
	void _on_resource_saved(const Ref<Resource> &p_res);
	void _on_scene_saved(const String &p_path);

public:
	void notify_request_help_search(const String &p_text);
	void notify_scene_closed(const String &p_path);
	void notify_script_add_function_request(Object *p_obj, const String &p_function, const PackedStringArray &p_args);
	void notify_resource_saved(const Ref<Resource> &p_res);
	void notify_scene_saved(const String &p_path);

	void _script_created(Ref<Script> p_script);
	void _open_script_request(const String &p_path);
	void _on_replace_in_files_requested(const String &text);
	void _on_find_in_files_result_selected(const String &fpath, int line_number, int begin, int end);
	void _start_find_in_files(bool with_replace);
	void _on_find_in_files_modified_files(const PackedStringArray &paths);
	void _autosave_scripts();
	void _update_autosave_timer();
	void _resave_scripts(const String &p_str);
	void _filesystem_changed();
	void _files_moved(const String &p_old_file, const String &p_new_file);
	void _file_removed(const String &p_file);
	void _live_auto_reload_running_scripts();
	void _goto_script_line(Ref<RefCounted> p_script, int p_line);
	void _set_execution(Ref<RefCounted> p_script, int p_line);
	void _clear_execution(Ref<RefCounted> p_script);
	void _breaked(bool p_breaked, bool p_can_debug);
	void _set_breakpoint(Ref<RefCounted> p_script, int p_line, bool p_enabled);
	void _clear_breakpoints();
	void _help_class_goto(const String &p_desc);

	static void _open_script_request_static(const String &p_path);

protected:
	static void _bind_methods();

public:
	static ScriptEditorController *get_singleton() { return singleton; }

	void init_global_services(Node *p_dialog_parent);
	ScriptEditorView *create_view_for_leaf(ScriptLeaf *p_leaf);
	void register_view(ScriptEditorView *p_view);
	void unregister_view(ScriptEditorView *p_view);
	void set_focused_view(ScriptEditorView *p_view);
	ScriptEditorView *get_focused_view() const { return focused_view; }
	const Vector<ScriptEditorView *> &get_views() const { return views; }

	Ref<ConfigFile> get_script_editor_cache() const { return script_editor_cache; }
	Array get_cached_breakpoints_for_script(const String &p_path) const;
	void restore_cached_breakpoints();
	void save_script_editor_cache() const;

	const Vector<Ref<EditorSyntaxHighlighter>> &get_syntax_highlighters() const { return syntax_highlighters; }
	int get_script_editor_func_count() const { return script_editor_func_count; }
	CreateScriptEditorFunc get_script_editor_func(int p_index) const;

	bool get_trim_trailing_whitespace_on_save() const { return trim_trailing_whitespace_on_save; }
	bool get_trim_final_newlines_on_save() const { return trim_final_newlines_on_save; }
	bool get_convert_indent_on_save() const { return convert_indent_on_save; }
	bool get_format_on_save() const { return format_on_save; }
	bool is_external_editor_active() const { return external_editor_active; }
	void set_external_editor_active(bool p_active) { external_editor_active = p_active; }

	// Whether the script feature is available (a feature profile can disable it).
	// The editor gates revealing the script workspace leaf on this flag, which
	// replaces the availability that a hidden main-screen button used to track.
	bool is_feature_enabled() const { return feature_enabled; }
	void set_feature_enabled(bool p_enabled) { feature_enabled = p_enabled; }

	void set_trim_trailing_whitespace_on_save(bool p_enabled) { trim_trailing_whitespace_on_save = p_enabled; }
	void set_trim_final_newlines_on_save(bool p_enabled) { trim_final_newlines_on_save = p_enabled; }
	void set_convert_indent_on_save(bool p_enabled) { convert_indent_on_save = p_enabled; }
	void set_format_on_save(bool p_enabled) { format_on_save = p_enabled; }

	List<int> &get_script_close_queue() { return script_close_queue; }

	bool is_pending_auto_reload() const { return pending_auto_reload; }
	void set_pending_auto_reload(bool p_enabled) { pending_auto_reload = p_enabled; }
	bool is_auto_reload_running_scripts() const { return auto_reload_running_scripts; }
	void set_auto_reload_running_scripts(bool p_enabled) { auto_reload_running_scripts = p_enabled; }
	Vector<String> &get_script_paths_to_reload() { return script_paths_to_reload; }

	void set_file_dialog_view(ScriptEditorView *p_view) { file_dialog_view = p_view; }

	List<String> &get_previous_scripts() { return previous_scripts; }
	int get_file_dialog_option() const { return file_dialog_option; }
	void set_file_dialog_option(int p_option) { file_dialog_option = p_option; }

	EditorFileDialog *get_file_dialog() const { return file_dialog; }
	ScriptCreateDialog *get_script_create_dialog() const { return script_create_dialog; }
	EditorHelpSearch *get_help_search_dialog() const { return help_search_dialog; }
	FindInFilesDialog *get_find_in_files_dialog() const { return find_in_files_dialog; }
	FindInFilesContainer *get_find_in_files() const { return find_in_files; }
	AcceptDialog *get_error_dialog() const { return error_dialog; }
	ConfirmationDialog *get_disk_changed() const { return disk_changed; }
	Tree *get_disk_changed_list() const { return disk_changed_list; }
	Timer *get_autosave_timer() const { return autosave_timer; }

	void add_recent_script(const String &p_path);
	void show_error_dialog(const String &p_path);
	void trigger_live_script_reload(const String &p_script_path);

	ScriptEditorView *find_view_for_script_path(const String &p_path) const;
	ScriptEditorBase *find_open_editor_for_path(const String &p_path) const;
	ScriptEditorBase *get_open_editor_for_path(const String &p_path) const { return find_open_editor_for_path(p_path); }

	_FORCE_INLINE_ bool edit(const Ref<Resource> &p_resource, bool p_grab_focus = true) {
		return edit(p_resource, -1, 0, p_grab_focus);
	}
	bool edit(const Ref<Resource> &p_resource, int p_line, int p_col, bool p_grab_focus = true);

	void ensure_select_current();
	bool toggle_files_panel();
	bool is_files_panel_toggled();
	void apply_scripts() const;
	void reload_scripts(bool p_refresh_only = false);
	void open_find_in_files_dialog(const String &text);
	void open_replace_in_files_dialog(const String &text);
	void open_script_create_dialog(const String &p_base_name, const String &p_base_path);
	void open_text_file_create_dialog(const String &p_base_path, const String &p_base_name = "");
	Ref<Resource> open_file(const String &p_file);

	bool apply_script_refactor_plan(const ScriptRefactorApplyPlan &p_plan, String &r_error_message);
	bool can_undo_script_refactor() const;
	bool can_redo_script_refactor() const;
	bool undo_script_refactor();
	bool redo_script_refactor();

	Vector<String> _get_breakpoints();
	void get_breakpoints(List<String> *p_breakpoints);

	PackedStringArray get_unsaved_scripts() const;
	void save_current_script();
	void save_all_scripts();
	void update_script_times();
	bool test_script_times_on_disk(Ref<Resource> p_for_script = Ref<Resource>());

	void set_window_layout(Ref<ConfigFile> p_layout);
	void get_window_layout(Ref<ConfigFile> p_layout);

	void set_scene_root_script(Ref<Script> p_script);
	Vector<Ref<Script>> get_open_scripts() const;

	bool get_current_script_view_state(String &r_path, int &r_line, int &r_column) const;

	bool script_goto_method(Ref<Script> p_script, const String &p_method);

	void edited_scene_changed();

	void notify_script_close(const Ref<Script> &p_script);
	void notify_script_changed(const Ref<Script> &p_script);

	void goto_help(const String &p_desc);
	void update_doc(const String &p_name);
	void clear_docs_from_script(const Ref<Script> &p_script);
	void update_docs_from_script(const Ref<Script> &p_script);

	void set_live_auto_reload_running_scripts(bool p_enabled);

	void register_syntax_highlighter(const Ref<EditorSyntaxHighlighter> &p_syntax_highlighter);
	void unregister_syntax_highlighter(const Ref<EditorSyntaxHighlighter> &p_syntax_highlighter);

	static void register_create_script_editor_function(CreateScriptEditorFunc p_func);

	VSplitContainer *get_left_list_split();

	ScriptEditorController();
	~ScriptEditorController();
};
