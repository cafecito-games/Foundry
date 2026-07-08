/**************************************************************************/
/*  script_editor_view.h                                                  */
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

#include "core/object/script_language.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/item_list.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_container.h"
#include "scene/resources/text_file.h"

class CodeTextEditor;
class ConfirmationDialog;
class EditorFileDialog;
class FindReplaceBar;
class HSplitContainer;
class ItemList;
class MenuButton;
class ScriptCreateDialog;
class ScriptEditorBase;
class ScriptEditorController;
class ScriptLeaf;
class TabContainer;
class TreeItem;
class VSplitContainer;
class WindowWrapper;
struct ScriptRefactorApplyPlan;

class ScriptEditorView : public PanelContainer {
	FOUNDRY_CLASS(ScriptEditorView, PanelContainer);

	friend class ScriptEditorController;

public:
	enum MenuOptions {
		// File.
		FILE_MENU_NEW,
		FILE_MENU_NEW_TEXTFILE,
		FILE_MENU_OPEN,
		FILE_MENU_REOPEN_CLOSED,
		FILE_MENU_OPEN_RECENT,

		FILE_MENU_SAVE,
		FILE_MENU_SAVE_AS,
		FILE_MENU_SAVE_ALL,

		FILE_MENU_SOFT_RELOAD_TOOL,
		FILE_MENU_COPY_PATH,
		FILE_MENU_COPY_UID,
		FILE_MENU_SHOW_IN_FILE_SYSTEM,

		FILE_MENU_HISTORY_PREV,
		FILE_MENU_HISTORY_NEXT,

		FILE_MENU_THEME_SUBMENU,

		FILE_MENU_CLOSE,
		FILE_MENU_CLOSE_ALL,
		FILE_MENU_CLOSE_OTHER_TABS,
		FILE_MENU_CLOSE_TABS_BELOW,

		FILE_MENU_RUN,

		FILE_MENU_TOGGLE_FILES_PANEL,

		FILE_MENU_MOVE_UP,
		FILE_MENU_MOVE_DOWN,
		FILE_MENU_SORT,

		// Search.
		SEARCH_IN_FILES,
		REPLACE_IN_FILES,

		SEARCH_HELP,
		SEARCH_WEBSITE,

		// Theme.
		THEME_IMPORT,
		THEME_RELOAD,
		THEME_SAVE_AS,
	};

private:
	friend class ScriptEditorController;

	enum ScriptSortBy {
		SORT_BY_NAME,
		SORT_BY_PATH,
		SORT_BY_NONE,
	};

	enum ScriptListName {
		DISPLAY_NAME,
		DISPLAY_DIR_AND_NAME,
		DISPLAY_FULL_PATH,
	};

	HBoxContainer *menu_hb = nullptr;
	MenuButton *file_menu = nullptr;
	MenuButton *edit_menu = nullptr;
	MenuButton *script_search_menu = nullptr;
	MenuButton *debug_menu = nullptr;
	PopupMenu *context_menu = nullptr;
	PopupMenu *recent_scripts = nullptr;
	PopupMenu *theme_submenu = nullptr;

	Button *help_search = nullptr;
	Button *site_search = nullptr;
	Button *make_floating = nullptr;
	bool is_floating = false;

	ItemList *script_list = nullptr;
	HSplitContainer *script_split = nullptr;
	ItemList *members_overview = nullptr;
	LineEdit *filter_scripts = nullptr;
	LineEdit *filter_methods = nullptr;
	VBoxContainer *scripts_vbox = nullptr;
	VBoxContainer *overview_vbox = nullptr;
	HBoxContainer *buttons_hbox = nullptr;
	Button *members_overview_alphabeta_sort_button = nullptr;
	bool members_overview_enabled;
	VSplitContainer *list_split = nullptr;
	TabContainer *tab_container = nullptr;
	ConfirmationDialog *erase_tab_confirm = nullptr;
	FindReplaceBar *find_replace_bar = nullptr;

	float zoom_factor = 1.0f;

	Label *script_name_label = nullptr;

	Button *script_back = nullptr;
	Button *script_forward = nullptr;

#ifdef ANDROID_ENABLED
	Control *virtual_keyboard_spacer = nullptr;
	int last_kb_height = -1;
#endif

	struct ScriptHistory {
		Control *control = nullptr;
		Variant state;
	};

	Vector<ScriptHistory> history;
	int history_pos = -1;

	bool restoring_layout = false;
	bool grab_focus_block = false;
	bool waiting_update_names = false;
	bool lock_history = false;
	int edit_pass = 0;
	bool _sort_list_on_update = true;

	HashSet<String> textfile_extensions;

	WindowWrapper *window_wrapper = nullptr;

	void _tab_changed(int p_which);
	void _menu_option(int p_option);
	void _theme_option(int p_option);
	void _show_save_theme_as_dialog();
	bool _has_script_tab() const;
	void _prepare_file_menu();
	void _file_menu_closed();

	void _collect_scripts_modified_on_disk(TreeItem *p_root, bool &r_need_ask, bool &r_need_reload, Ref<Resource> p_for_script = Ref<Resource>());
	bool _script_exists(const String &p_path) const;

	void _open_recent_script(int p_idx);
	void _update_recent_scripts();

	void _close_tab(int p_idx, bool p_save = true, bool p_history_back = true);
	void _update_find_replace_bar();

	void _close_current_tab(bool p_save = true, bool p_history_back = true);
	void _close_discard_current_tab(const String &p_str);
	void _close_other_tabs();
	void _close_tabs_below();
	void _close_all_tabs();
	void _queue_close_tabs();

	void _copy_script_path();
	void _copy_script_uid();
	void _set_refactor_file_source(const String &p_path, const String &p_source, bool p_source_is_saved_version);

	void _ask_close_current_unsaved_tab(ScriptEditorBase *current);

	void _update_selected_editor_menu();
	void _editor_stop();

	void _add_callback(Object *p_obj, const String &p_function, const PackedStringArray &p_args);
	void _res_saved_callback(const Ref<Resource> &p_res);
	void _mark_built_in_text_resources_as_saved(const String &p_scene_path);

	void _goto_script_line2(int p_line);
	String _get_debug_tooltip(const String &p_text, Node *p_se);

	ScriptEditorBase *_get_current_editor() const;
	TypedArray<ScriptEditorBase> _get_open_script_editors() const;

	void _save_editor_state(ScriptEditorBase *p_editor);
	void _save_layout();
	void _apply_editor_settings();
	void _reload_scripts(bool p_refresh_only = false);

	void _update_members_overview_visibility();
	void _update_members_overview();
	void _toggle_members_overview_alpha_sort(bool p_alphabetic_sort);
	void _filter_scripts_text_changed(const String &p_newtext);
	void _filter_methods_text_changed(const String &p_newtext);
	void _update_script_names();

	void _members_overview_selected(int p_idx);
	void _script_selected(int p_idx);

	void _update_online_doc();

	void _find_scripts(Node *p_base, Node *p_current, HashSet<Ref<Script>> &used);

	void _tree_changed();
	void _split_dragged(float);

	Variant get_drag_data_fw(const Point2 &p_point, Control *p_from);
	bool can_drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from) const;
	void drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from);

	virtual void input(const Ref<InputEvent> &p_event) override;
	virtual void shortcut_input(const Ref<InputEvent> &p_event) override;

	void _script_list_clicked(int p_item, Vector2 p_local_mouse_pos, MouseButton p_mouse_button_index);
	void _make_script_list_context_menu();

	void _help_search(const String &p_text);

	void _history_forward();
	void _history_back();

	void _unlock_history();

	void _update_history_arrows();
	void _save_history();
	void _save_previous_state(Dictionary p_state);
	void _go_to_tab(int p_idx);
	void _update_history_pos(int p_new_pos);
	void _update_script_colors();
	void _update_modified_scripts_for_external_editor(Ref<Script> p_for_script = Ref<Script>());

	void _script_changed();

	void _file_dialog_action(const String &p_file);

	Ref<Script> _get_current_script();
	TypedArray<Script> _get_open_scripts() const;

	Ref<TextFile> _load_text_file(const String &p_path, Error *r_error) const;
	Error _save_text_file(Ref<TextFile> p_text_file, const String &p_path);

	void _set_script_zoom_factor(float p_zoom_factor);
	void _update_code_editor_zoom_factor(CodeTextEditor *p_code_text_editor);

	void _window_changed(bool p_visible);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	bool toggle_files_panel();
	bool is_files_panel_toggled();
	void apply_scripts() const;
	void reload_scripts(bool p_refresh_only = false);
	Ref<Resource> open_file(const String &p_file);

	void ensure_select_current();

	bool is_editor_floating();

	_FORCE_INLINE_ bool edit(const Ref<Resource> &p_resource, bool p_grab_focus = true) {
		return edit(p_resource, -1, 0, p_grab_focus);
	}
	bool edit(const Ref<Resource> &p_resource, int p_line, int p_col, bool p_grab_focus = true);
	ScriptEditorBase *get_open_editor_for_path(const String &p_path) const;
	bool apply_script_refactor_plan(const ScriptRefactorApplyPlan &p_plan, String &r_error_message);
	bool can_undo_script_refactor() const;
	bool can_redo_script_refactor() const;
	bool undo_script_refactor();
	bool redo_script_refactor();

	void collect_breakpoints(List<String> *p_breakpoints) const;
	PackedStringArray collect_unsaved_scripts() const;
	Vector<Ref<Script>> collect_open_scripts() const;

	void save_current_script();
	void update_script_times();

	// Close the active editor tab, routing through the existing save/discard/cancel
	// prompt when it has unsaved changes. Returns true when the prompt was shown
	// (close deferred to the user's choice), false when the tab closed immediately
	// or there was nothing to close. Used by the workspace ScriptResourceTab close
	// path so tab close reuses this view's dirty-close flow. When the prompt is
	// shown, p_on_closed (if valid) is invoked once the user chooses Save or
	// Discard, and is not invoked if the user cancels.
	bool request_close_active_tab(const Callable &p_on_closed = Callable());

	void set_window_layout(Ref<ConfigFile> p_layout);
	void get_window_layout(Ref<ConfigFile> p_layout);

	void set_scene_root_script(Ref<Script> p_script);

	bool get_current_script_view_state(String &r_path, int &r_line, int &r_column) const;

	bool script_goto_method(Ref<Script> p_script, const String &p_method);

	void edited_scene_changed();

	void clear_docs_from_script(const Ref<Script> &p_script);
	void update_docs_from_script(const Ref<Script> &p_script);

	VSplitContainer *get_left_list_split() { return list_split; }

	void get_view_layout(Ref<ConfigFile> p_layout, const String &p_section) const;
	void set_view_layout(const Ref<ConfigFile> &p_layout, const String &p_section);
	void sync_script_leaf_path();

	ScriptEditorController *controller = nullptr;
	ScriptLeaf *script_leaf = nullptr;

	ScriptEditorView(ScriptEditorController *p_controller, ScriptLeaf *p_leaf = nullptr);
	ScriptEditorController *get_controller() const { return controller; }
	ScriptLeaf *get_script_leaf() const { return script_leaf; }
	TabContainer *get_tab_container() const { return tab_container; }
	HSplitContainer *get_script_split() const { return script_split; }
	VSplitContainer *get_list_split() const { return list_split; }

	void setup_view_chrome(WindowWrapper *p_wrapper = nullptr);
};
