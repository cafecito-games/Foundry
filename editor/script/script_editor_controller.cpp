/**************************************************************************/
/*  script_editor_controller.cpp                                          */
/**************************************************************************/

#include "script_editor_controller.h"

#include "editor/editor_script_leaf.h"
#include "script_editor_view.h"
#include "script_editor_plugin.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/doc/editor_help.h"
#include "editor/doc/editor_help_search.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/docks/inspector_dock.h"
#include "scene/gui/box_container.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/tree.h"
#include "editor/editor_node.h"
#include "editor/editor_scene_workspace.h"
#include "editor/editor_script_leaf.h"
#include "editor/editor_string_names.h"
#include "scene/gui/control.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/file_system/editor_paths.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/gui/window_wrapper.h"
#include "editor/script/find_in_files.h"
#include "editor/script/script_create_dialog.h"
#include "editor/settings/editor_settings.h"
#include "editor/shader/shader_editor_plugin.h"
#include "editor/shader/text_shader_editor.h"
#include "script_text_editor.h"
#include "text_editor.h"
#include "servers/display/display_server.h"
#include "scene/main/timer.h"
#include "scene/gui/tab_container.h"

ScriptEditorController *ScriptEditorController::singleton = nullptr;
int ScriptEditorController::script_editor_func_count = 0;
CreateScriptEditorFunc ScriptEditorController::script_editor_funcs[ScriptEditorController::SCRIPT_EDITOR_FUNC_MAX];

static void ensure_script_editor_registrations() {
	ScriptTextEditor::register_editor();
	TextEditor::register_editor();
	ED_SHORTCUT("script_text_editor/convert_to_uppercase", TTRC("Uppercase"), KeyModifierMask::SHIFT | Key::F4);
	ED_SHORTCUT("script_text_editor/convert_to_lowercase", TTRC("Lowercase"), KeyModifierMask::SHIFT | Key::F5);
	ED_SHORTCUT("script_text_editor/capitalize", TTRC("Capitalize"), KeyModifierMask::SHIFT | Key::F6);

	// These script_editor/* shortcuts used to be registered eagerly by the
	// monolithic ScriptEditor at editor startup. Other subsystems (shader editor
	// menus, code editor / help "toggle files" buttons) resolve them through
	// ED_GET_SHORTCUT before any ScriptEditorView exists, so register them here
	// rather than lazily in ScriptEditorView::setup_view_chrome(). ED_SHORTCUT is
	// idempotent, so the view's own registrations still return these instances.
	ED_SHORTCUT("script_editor/save", TTRC("Save"), KeyModifierMask::ALT | KeyModifierMask::CMD_OR_CTRL | Key::S);
	ED_SHORTCUT("script_editor/save_as", TTRC("Save As..."));
	ED_SHORTCUT("script_editor/close_file", TTRC("Close"), KeyModifierMask::CMD_OR_CTRL | Key::W);
	ED_SHORTCUT("script_editor/close_all", TTRC("Close All"));
	ED_SHORTCUT("script_editor/close_other_tabs", TTRC("Close Other Tabs"));
	ED_SHORTCUT("script_editor/show_in_file_system", TTRC("Show in FileSystem"));
	ED_SHORTCUT("script_editor/toggle_files_panel", TTRC("Toggle Files Panel"), KeyModifierMask::CMD_OR_CTRL | Key::BACKSLASH);
}

static void connect_if_needed(Object *p_object, const StringName &p_signal, const Callable &p_callable) {
	ERR_FAIL_NULL(p_object);
	if (!p_object->has_signal(p_signal)) {
		return;
	}
	if (!p_object->is_connected(p_signal, p_callable)) {
		p_object->connect(p_signal, p_callable);
	}
}

static bool script_path_exists(const String &p_path) {
	if (p_path.is_empty()) {
		return false;
	}
	return FileAccess::exists(p_path);
}

ScriptEditorController::ScriptEditorController() {
	singleton = this;

	ensure_script_editor_registrations();

	ED_SHORTCUT("script_editor/reopen_closed_script", TTRC("Reopen Closed Script"), KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::T);
	ED_SHORTCUT("script_editor/clear_recent", TTRC("Clear Recent Scripts"));
	ED_SHORTCUT("script_editor/replace_in_files", TTRC("Replace in Files..."), KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::R);

	script_editor_cache.instantiate();
	script_editor_cache->load(EditorPaths::get_singleton()->get_project_settings_dir().path_join("script_editor_cache.cfg"));

	trim_trailing_whitespace_on_save = EDITOR_GET("text_editor/behavior/files/trim_trailing_whitespace_on_save");
	trim_final_newlines_on_save = EDITOR_GET("text_editor/behavior/files/trim_final_newlines_on_save");
	convert_indent_on_save = EDITOR_GET("text_editor/behavior/files/convert_indent_on_save");
	format_on_save = EDITOR_GET("text_editor/behavior/files/format_on_save");

	ScriptServer::edit_request_func = _open_script_request_static;

	Ref<EditorJSONSyntaxHighlighter> json_syntax_highlighter;
	json_syntax_highlighter.instantiate();
	register_syntax_highlighter(json_syntax_highlighter);

	Ref<EditorMarkdownSyntaxHighlighter> markdown_syntax_highlighter;
	markdown_syntax_highlighter.instantiate();
	register_syntax_highlighter(markdown_syntax_highlighter);

	Ref<EditorConfigFileSyntaxHighlighter> config_file_syntax_highlighter;
	config_file_syntax_highlighter.instantiate();
	register_syntax_highlighter(config_file_syntax_highlighter);
}

ScriptEditorController::~ScriptEditorController() {
	views.clear();
	focused_view = nullptr;
	file_dialog_view = nullptr;
	if (singleton == this) {
		singleton = nullptr;
	}
}

ScriptEditorView *ScriptEditorController::_active_view() const {
	if (focused_view) {
		return focused_view;
	}
	return views.is_empty() ? nullptr : views[0];
}

void ScriptEditorController::_on_file_dialog_selected(const String &p_file) {
	if (file_dialog_view) {
		file_dialog_view->_file_dialog_action(p_file);
	} else if (focused_view) {
		focused_view->_file_dialog_action(p_file);
	} else if (!views.is_empty()) {
		views[0]->_file_dialog_action(p_file);
	}
}

void ScriptEditorController::init_global_services(Node *p_dialog_parent) {
	ERR_FAIL_NULL(p_dialog_parent);
	if (script_create_dialog) {
		return;
	}

	script_create_dialog = memnew(ScriptCreateDialog);
	script_create_dialog->set_title(TTRC("Create Script"));
	p_dialog_parent->add_child(script_create_dialog);
	script_create_dialog->connect("script_created", callable_mp(this, &ScriptEditorController::_script_created));

	file_dialog = memnew(EditorFileDialog);
	p_dialog_parent->add_child(file_dialog);
	file_dialog->connect("file_selected", callable_mp(this, &ScriptEditorController::_on_file_dialog_selected));

	error_dialog = memnew(AcceptDialog);
	p_dialog_parent->add_child(error_dialog);

	disk_changed = memnew(ConfirmationDialog);
	{
		disk_changed->set_title(TTRC("Files have been modified outside Foundry"));
		VBoxContainer *vbc = memnew(VBoxContainer);
		disk_changed->add_child(vbc);
		Label *files_are_newer_label = memnew(Label);
		files_are_newer_label->set_text(TTRC("The following files are newer on disk:"));
		vbc->add_child(files_are_newer_label);
		disk_changed_list = memnew(Tree);
		disk_changed_list->set_hide_root(true);
		disk_changed_list->set_auto_translate_mode(Control::AUTO_TRANSLATE_MODE_DISABLED);
		disk_changed_list->set_accessibility_name(TTRC("The following files are newer on disk:"));
		disk_changed_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		vbc->add_child(disk_changed_list);
		Label *what_action_label = memnew(Label);
		what_action_label->set_text(TTRC("What action should be taken?"));
		vbc->add_child(what_action_label);
		disk_changed->connect(SceneStringName(confirmed), callable_mp(this, &ScriptEditorController::reload_scripts).bind(false));
		disk_changed->set_ok_button_text(TTRC("Reload from disk"));
		disk_changed->add_button(TTRC("Ignore external changes"), !DisplayServer::get_singleton()->get_swap_cancel_ok(), "resave");
		disk_changed->connect("custom_action", callable_mp(this, &ScriptEditorController::_resave_scripts));
	}
	p_dialog_parent->add_child(disk_changed);

	help_search_dialog = memnew(EditorHelpSearch);
	p_dialog_parent->add_child(help_search_dialog);
	help_search_dialog->connect("go_to_help", callable_mp(this, &ScriptEditorController::_help_class_goto));

	find_in_files_dialog = memnew(FindInFilesDialog);
	find_in_files_dialog->connect(FindInFilesDialog::SIGNAL_FIND_REQUESTED, callable_mp(this, &ScriptEditorController::_start_find_in_files).bind(false));
	find_in_files_dialog->connect(FindInFilesDialog::SIGNAL_REPLACE_REQUESTED, callable_mp(this, &ScriptEditorController::_start_find_in_files).bind(true));
	p_dialog_parent->add_child(find_in_files_dialog);

	if (EditorNode::get_singleton() && EditorDockManager::get_singleton()) {
		find_in_files = memnew(FindInFilesContainer);
		EditorDockManager::get_singleton()->add_dock(find_in_files);
		find_in_files->close();
		find_in_files->connect("result_selected", callable_mp(this, &ScriptEditorController::_on_find_in_files_result_selected));
		find_in_files->connect("files_modified", callable_mp(this, &ScriptEditorController::_on_find_in_files_modified_files));
	}

	autosave_timer = memnew(Timer);
	autosave_timer->set_one_shot(false);
	autosave_timer->connect(SceneStringName(tree_entered), callable_mp(this, &ScriptEditorController::_update_autosave_timer));
	autosave_timer->connect("timeout", callable_mp(this, &ScriptEditorController::_autosave_scripts));
	p_dialog_parent->add_child(autosave_timer);

	_connect_global_signals();
}

void ScriptEditorController::_connect_global_signals() {
	if (EditorNode *editor_node = EditorNode::get_singleton()) {
		if (InspectorDock *inspector = editor_node->get_focused_inspector_dock()) {
			connect_if_needed(inspector, SNAME("request_help"), callable_mp(this, &ScriptEditorController::_on_request_help));
		}
	}

	if (FileSystemDock *filesystem_dock = FileSystemDock::get_singleton()) {
		connect_if_needed(filesystem_dock, SNAME("files_moved"), callable_mp(this, &ScriptEditorController::_files_moved));
		connect_if_needed(filesystem_dock, SNAME("file_removed"), callable_mp(this, &ScriptEditorController::_file_removed));
	}

	if (EditorFileSystem *editor_file_system = EditorFileSystem::get_singleton()) {
		connect_if_needed(editor_file_system, SNAME("filesystem_changed"), callable_mp(this, &ScriptEditorController::_filesystem_changed));
	}

	if (EditorDebuggerNode *debugger = EditorDebuggerNode::get_singleton()) {
		connect_if_needed(debugger, SNAME("goto_script_line"), callable_mp(this, &ScriptEditorController::_goto_script_line));
		connect_if_needed(debugger, SNAME("set_execution"), callable_mp(this, &ScriptEditorController::_set_execution));
		connect_if_needed(debugger, SNAME("clear_execution"), callable_mp(this, &ScriptEditorController::_clear_execution));
		connect_if_needed(debugger, SNAME("breaked"), callable_mp(this, &ScriptEditorController::_breaked));
		connect_if_needed(debugger, SNAME("breakpoint_set_in_tree"), callable_mp(this, &ScriptEditorController::_set_breakpoint));
		connect_if_needed(debugger, SNAME("breakpoints_cleared_in_tree"), callable_mp(this, &ScriptEditorController::_clear_breakpoints));
	}
}

void ScriptEditorController::_on_request_help(const String &p_topic) {
	_open_help_in_workspace(p_topic);
}

void ScriptEditorController::_on_request_help_search(const String &p_text) {
	if (ScriptEditorView *view = _active_view()) {
		view->_help_search(p_text);
	}
}

void ScriptEditorController::notify_request_help_search(const String &p_text) {
	_on_request_help_search(p_text);
}

void ScriptEditorController::_on_scene_closed(const String &p_path) {
	for (ScriptEditorView *view : views) {
		view->_close_built_in_text_resources_from_scene(p_path);
	}
}

void ScriptEditorController::notify_scene_closed(const String &p_path) {
	_on_scene_closed(p_path);
}

void ScriptEditorController::_on_script_add_function_request(Object *p_obj, const String &p_function, const PackedStringArray &p_args) {
	if (ScriptEditorView *view = _active_view()) {
		view->_add_callback(p_obj, p_function, p_args);
	}
}

void ScriptEditorController::notify_script_add_function_request(Object *p_obj, const String &p_function, const PackedStringArray &p_args) {
	_on_script_add_function_request(p_obj, p_function, p_args);
	emit_signal(SNAME("script_add_function_request"), p_obj, p_function, p_args);
}

void ScriptEditorController::_on_resource_saved(const Ref<Resource> &p_res) {
	for (ScriptEditorView *view : views) {
		view->_res_saved_callback(p_res);
	}
}

void ScriptEditorController::notify_resource_saved(const Ref<Resource> &p_res) {
	_on_resource_saved(p_res);
}

void ScriptEditorController::_on_scene_saved(const String &p_path) {
	for (ScriptEditorView *view : views) {
		view->_mark_built_in_text_resources_as_saved(p_path);
	}
}

void ScriptEditorController::notify_scene_saved(const String &p_path) {
	_on_scene_saved(p_path);
}

ScriptEditorView *ScriptEditorController::create_view_for_leaf(ScriptLeaf *p_leaf) {
	ERR_FAIL_NULL_V(p_leaf, nullptr);
	ScriptEditorView *view = memnew(ScriptEditorView(this, p_leaf));
	view->setup_view_chrome();

	Control *host = p_leaf->get_surface_host();
	ERR_FAIL_NULL_V(host, view);
	host->add_child(view);
	view->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	view->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	view->set_h_size_flags(Control::SIZE_EXPAND_FILL);

	p_leaf->set_script_editor_view(view);

	// Only open the leaf's script once the leaf is in the scene tree. Editing
	// while detached drives ScriptTextEditor validation through a null get_tree();
	// detached leaves open their script from ScriptLeaf once they enter the tree.
	if (p_leaf->is_inside_tree() && !p_leaf->get_script_path().is_empty()) {
		if (ResourceLoader::exists(p_leaf->get_script_path())) {
			Ref<Resource> resource = ResourceLoader::load(p_leaf->get_script_path());
			if (resource.is_valid()) {
				view->edit(resource, false);
			}
		}
	}

	if (focused_view == nullptr) {
		focused_view = view;
	}

	return view;
}

void ScriptEditorController::register_view(ScriptEditorView *p_view) {
	ERR_FAIL_NULL(p_view);
	if (!views.has(p_view)) {
		views.push_back(p_view);
	}
}

void ScriptEditorController::unregister_view(ScriptEditorView *p_view) {
	views.erase(p_view);
	if (focused_view == p_view) {
		focused_view = views.is_empty() ? nullptr : views[0];
	}
}

void ScriptEditorController::set_focused_view(ScriptEditorView *p_view) {
	focused_view = p_view;
}

CreateScriptEditorFunc ScriptEditorController::get_script_editor_func(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, script_editor_func_count, nullptr);
	return script_editor_funcs[p_index];
}

Array ScriptEditorController::get_cached_breakpoints_for_script(const String &p_path) const {
	if (!ResourceLoader::exists(p_path, "Script") || p_path.begins_with("local://") || !script_editor_cache->has_section_key(p_path, "state")) {
		return Array();
	}
	Dictionary state = script_editor_cache->get_value(p_path, "state");
	if (!state.has("breakpoints")) {
		return Array();
	}
	return state["breakpoints"];
}

void ScriptEditorController::restore_cached_breakpoints() {
	EditorDebuggerNode *debugger = EditorDebuggerNode::get_singleton();

	HashSet<String> loaded_scripts;
	for (ScriptEditorView *view : views) {
		for (int i = 0; i < view->get_tab_container()->get_tab_count(); i++) {
			ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(view->get_tab_container()->get_tab_control(i));
			if (!se) {
				continue;
			}
			Ref<Resource> edited_res = se->get_edited_resource();
			if (edited_res.is_valid()) {
				loaded_scripts.insert(edited_res->get_path());
			}
		}
	}

	Vector<String> cached_editors = script_editor_cache->get_sections();
	for (const String &E : cached_editors) {
		if (loaded_scripts.has(E)) {
			continue;
		}

		if (!script_path_exists(E)) {
			script_editor_cache->erase_section(E);
			continue;
		}

		Array breakpoints = get_cached_breakpoints_for_script(E);
		for (int breakpoint : breakpoints) {
			if (debugger) {
				debugger->set_breakpoint(E, (int)breakpoint + 1, true);
			}
		}
	}
}

void ScriptEditorController::save_script_editor_cache() const {
	script_editor_cache->save(EditorPaths::get_singleton()->get_project_settings_dir().path_join("script_editor_cache.cfg"));
}

void ScriptEditorController::add_recent_script(const String &p_path) {
	if (p_path.is_empty()) {
		return;
	}
	Array rc = EditorSettings::get_singleton()->get_project_metadata("recent_files", "scripts", Array());
	if (rc.has(p_path)) {
		rc.erase(p_path);
	}
	rc.push_front(p_path);
	if (rc.size() > 10) {
		rc.resize(10);
	}
	EditorSettings::get_singleton()->set_project_metadata("recent_files", "scripts", rc);
	for (ScriptEditorView *view : views) {
		view->_update_recent_scripts();
	}
}

void ScriptEditorController::show_error_dialog(const String &p_path) {
	if (error_dialog) {
		error_dialog->set_text(vformat(TTR("Can't open '%s'. The file could have been moved or deleted."), p_path));
		error_dialog->popup_centered();
	}
}

ScriptEditorView *ScriptEditorController::find_view_for_script_path(const String &p_path) const {
	for (ScriptEditorView *view : views) {
		if (view->get_open_editor_for_path(p_path)) {
			return view;
		}
	}
	return focused_view;
}

ScriptEditorBase *ScriptEditorController::find_open_editor_for_path(const String &p_path) const {
	for (ScriptEditorView *view : views) {
		ScriptEditorBase *editor = view->get_open_editor_for_path(p_path);
		if (editor) {
			return editor;
		}
	}
	return nullptr;
}

bool ScriptEditorController::edit(const Ref<Resource> &p_resource, int p_line, int p_col, bool p_grab_focus) {
	if (p_resource.is_valid() && !p_resource->get_path().is_empty()) {
		for (ScriptEditorView *view : views) {
			if (view->get_open_editor_for_path(p_resource->get_path())) {
				set_focused_view(view);
				return view->edit(p_resource, p_line, p_col, p_grab_focus);
			}
		}
	}

	ScriptEditorView *view = focused_view;
	if (!view && !views.is_empty()) {
		view = views[0];
	}
	if (!view && EditorNode::get_singleton()) {
		EditorNode::get_singleton()->reveal_script_leaf();
		view = focused_view;
	}
	ERR_FAIL_NULL_V(view, false);
	if (p_grab_focus) {
		set_focused_view(view);
	}
	return view->edit(p_resource, p_line, p_col, p_grab_focus);
}

void ScriptEditorController::ensure_select_current() {
	if (focused_view) {
		focused_view->ensure_select_current();
	}
}

bool ScriptEditorController::toggle_files_panel() {
	ScriptEditorView *view = _active_view();
	ERR_FAIL_NULL_V(view, false);
	return view->toggle_files_panel();
}

bool ScriptEditorController::is_files_panel_toggled() {
	ScriptEditorView *view = _active_view();
	ERR_FAIL_NULL_V(view, false);
	return view->is_files_panel_toggled();
}

void ScriptEditorController::apply_scripts() const {
	for (ScriptEditorView *view : views) {
		view->apply_scripts();
	}
}

void ScriptEditorController::reload_scripts(bool p_refresh_only) {
	for (ScriptEditorView *view : views) {
		view->reload_scripts(p_refresh_only);
	}
}

void ScriptEditorController::open_replace_in_files_dialog(const String &text) {
	_on_replace_in_files_requested(text);
}

void ScriptEditorController::open_find_in_files_dialog(const String &text) {
	ERR_FAIL_NULL(find_in_files_dialog);
	find_in_files_dialog->set_find_in_files_mode(FindInFilesDialog::SEARCH_MODE);
	find_in_files_dialog->set_search_text(text);
	find_in_files_dialog->popup_centered();
}

void ScriptEditorController::open_script_create_dialog(const String &p_base_name, const String &p_base_path) {
	if (ScriptEditorView *view = _active_view()) {
		view->_menu_option(ScriptEditorView::FILE_MENU_NEW);
	}
	if (script_create_dialog) {
		script_create_dialog->config(p_base_name, p_base_path);
	}
}

void ScriptEditorController::open_text_file_create_dialog(const String &p_base_path, const String &p_base_name) {
	if (ScriptEditorView *view = _active_view()) {
		view->_menu_option(ScriptEditorView::FILE_MENU_NEW_TEXTFILE);
	}
	if (file_dialog) {
		file_dialog->set_current_dir(p_base_path);
		file_dialog->set_current_file(p_base_name);
	}
}

Ref<Resource> ScriptEditorController::open_file(const String &p_file) {
	ScriptEditorView *view = _active_view();
	ERR_FAIL_NULL_V(view, Ref<Resource>());
	return view->open_file(p_file);
}

bool ScriptEditorController::apply_script_refactor_plan(const ScriptRefactorApplyPlan &p_plan, String &r_error_message) {
	ScriptEditorView *view = _active_view();
	ERR_FAIL_NULL_V(view, false);
	return view->apply_script_refactor_plan(p_plan, r_error_message);
}

bool ScriptEditorController::can_undo_script_refactor() const {
	ScriptEditorView *view = _active_view();
	return view ? view->can_undo_script_refactor() : false;
}

bool ScriptEditorController::can_redo_script_refactor() const {
	ScriptEditorView *view = _active_view();
	return view ? view->can_redo_script_refactor() : false;
}

bool ScriptEditorController::undo_script_refactor() {
	ScriptEditorView *view = _active_view();
	ERR_FAIL_NULL_V(view, false);
	return view->undo_script_refactor();
}

bool ScriptEditorController::redo_script_refactor() {
	ScriptEditorView *view = _active_view();
	ERR_FAIL_NULL_V(view, false);
	return view->redo_script_refactor();
}

Vector<String> ScriptEditorController::_get_breakpoints() {
	Vector<String> ret;
	List<String> breakpoints;
	get_breakpoints(&breakpoints);
	for (const String &bp : breakpoints) {
		ret.push_back(bp);
	}
	return ret;
}

void ScriptEditorController::get_breakpoints(List<String> *p_breakpoints) {
	ERR_FAIL_NULL(p_breakpoints);
	HashSet<String> loaded_scripts;
	for (ScriptEditorView *view : views) {
		for (int i = 0; i < view->get_tab_container()->get_tab_count(); i++) {
			ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(view->get_tab_container()->get_tab_control(i));
			if (se) {
				Ref<Script> scr = se->get_edited_resource();
				if (scr.is_valid()) {
					loaded_scripts.insert(scr->get_path());
				}
			}
		}
		view->collect_breakpoints(p_breakpoints);
	}
	Vector<String> cached_editors = script_editor_cache->get_sections();
	for (const String &E : cached_editors) {
		if (loaded_scripts.has(E)) {
			continue;
		}
		Array breakpoints = get_cached_breakpoints_for_script(E);
		for (int breakpoint : breakpoints) {
			p_breakpoints->push_back(E + ":" + itos((int)breakpoint + 1));
		}
	}
}

PackedStringArray ScriptEditorController::get_unsaved_scripts() const {
	PackedStringArray unsaved;
	for (ScriptEditorView *view : views) {
		unsaved.append_array(view->collect_unsaved_scripts());
	}
	return unsaved;
}

PackedStringArray ScriptEditorController::get_unsaved_built_in_text_resources_for_scene(const String &p_scene_path) const {
	PackedStringArray unsaved;
	for (ScriptEditorView *view : views) {
		unsaved.append_array(view->collect_unsaved_built_in_text_resources_for_scene(p_scene_path));
	}
	return unsaved;
}

void ScriptEditorController::save_current_script() {
	if (ScriptEditorView *view = _active_view()) {
		view->save_current_script();
	}
}

void ScriptEditorController::save_all_scripts() {
	for (ScriptEditorView *view : views) {
		for (int i = 0; i < view->get_tab_container()->get_tab_count(); i++) {
			ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(view->get_tab_container()->get_tab_control(i));
			if (!se) {
				continue;
			}
			if (convert_indent_on_save) {
				se->convert_indent();
			}
			if (trim_trailing_whitespace_on_save) {
				se->trim_trailing_whitespace();
			}
			if (trim_final_newlines_on_save) {
				se->trim_final_newlines();
			}
			if (format_on_save) {
				se->format_document(false);
			}
			if (!se->is_unsaved()) {
				continue;
			}
			Ref<Resource> edited_res = se->get_edited_resource();
			if (edited_res.is_valid()) {
				se->apply_code();
			}
			Ref<Script> scr = edited_res;
			if (scr.is_valid()) {
				clear_docs_from_script(scr);
			}
			Ref<TextFile> text_file = edited_res;
			if (text_file.is_valid() && !edited_res->is_built_in()) {
				view->_save_text_file(text_file, text_file->get_path());
			} else {
				EditorNode::get_singleton()->save_resource(edited_res);
			}
			if (scr.is_valid()) {
				update_docs_from_script(scr);
			}
		}
		view->_update_script_names();
	}
}

void ScriptEditorController::update_script_times() {
	for (ScriptEditorView *view : views) {
		view->update_script_times();
	}
}

bool ScriptEditorController::test_script_times_on_disk(Ref<Resource> p_for_script) {
	if (!disk_changed_list || !disk_changed) {
		return false;
	}

	disk_changed_list->clear();
	TreeItem *root = disk_changed_list->create_item();

	bool need_ask = false;
	bool need_reload = false;
	for (ScriptEditorView *view : views) {
		view->_collect_scripts_modified_on_disk(root, need_ask, need_reload, p_for_script);
	}

	if (need_reload) {
		if (!need_ask) {
			reload_scripts();
			need_reload = false;
		} else {
			callable_mp((Window *)disk_changed, &Window::popup_centered_ratio).call_deferred(0.3);
		}
	}

	return need_reload;
}

void ScriptEditorController::set_window_layout(Ref<ConfigFile> p_layout) {
	if (!views.is_empty()) {
		views[0]->set_window_layout(p_layout);
	}
	restore_cached_breakpoints();
}

void ScriptEditorController::get_window_layout(Ref<ConfigFile> p_layout) {
	if (views.size() == 1) {
		views[0]->get_window_layout(p_layout);
	}
	save_script_editor_cache();
}

void ScriptEditorController::set_scene_root_script(Ref<Script> p_script) {
	if (ScriptEditorView *view = _active_view()) {
		view->set_scene_root_script(p_script);
	}
}

Vector<Ref<Script>> ScriptEditorController::get_open_scripts() const {
	Vector<Ref<Script>> scripts;
	for (ScriptEditorView *view : views) {
		scripts.append_array(view->collect_open_scripts());
	}
	return scripts;
}

bool ScriptEditorController::get_current_script_view_state(String &r_path, int &r_line, int &r_column) const {
	if (ScriptEditorView *view = _active_view()) {
		return view->get_current_script_view_state(r_path, r_line, r_column);
	}
	return false;
}

bool ScriptEditorController::script_goto_method(Ref<Script> p_script, const String &p_method) {
	for (ScriptEditorView *view : views) {
		if (view->script_goto_method(p_script, p_method)) {
			set_focused_view(view);
			return true;
		}
	}
	return false;
}

void ScriptEditorController::edited_scene_changed() {
	for (ScriptEditorView *view : views) {
		view->edited_scene_changed();
	}
}

void ScriptEditorController::notify_script_close(const Ref<Script> &p_script) {
	emit_signal(SNAME("script_close"), p_script);
}

void ScriptEditorController::notify_script_changed(const Ref<Script> &p_script) {
	emit_signal(SNAME("editor_script_changed"), p_script);
}

void ScriptEditorController::goto_help(const String &p_desc) {
	_help_class_goto(p_desc);
}

void ScriptEditorController::clear_docs_from_script(const Ref<Script> &p_script) {
	for (ScriptEditorView *view : views) {
		view->clear_docs_from_script(p_script);
	}
}

void ScriptEditorController::update_docs_from_script(const Ref<Script> &p_script) {
	for (ScriptEditorView *view : views) {
		view->update_docs_from_script(p_script);
	}
	// The doc database now carries the script's latest documentation; refresh any
	// already-open workspace help page for those classes so it stops showing stale
	// docs until closed and reopened.
	if (p_script.is_valid()) {
		if (EditorSceneWorkspace *workspace = EditorNode::get_scene_workspace()) {
			for (const DocData::ClassDoc &cd : p_script->get_documentation()) {
				workspace->refresh_help_tab(cd.name);
			}
		}
	}
}

void ScriptEditorController::set_live_auto_reload_running_scripts(bool p_enabled) {
	auto_reload_running_scripts = p_enabled;
}

void ScriptEditorController::register_syntax_highlighter(const Ref<EditorSyntaxHighlighter> &p_syntax_highlighter) {
	ERR_FAIL_COND(p_syntax_highlighter.is_null());
	if (!syntax_highlighters.has(p_syntax_highlighter)) {
		syntax_highlighters.push_back(p_syntax_highlighter);
	}
}

void ScriptEditorController::unregister_syntax_highlighter(const Ref<EditorSyntaxHighlighter> &p_syntax_highlighter) {
	ERR_FAIL_COND(p_syntax_highlighter.is_null());
	syntax_highlighters.erase(p_syntax_highlighter);
}

void ScriptEditorController::register_create_script_editor_function(CreateScriptEditorFunc p_func) {
	ERR_FAIL_COND(script_editor_func_count == SCRIPT_EDITOR_FUNC_MAX);
	script_editor_funcs[script_editor_func_count++] = p_func;
}

VSplitContainer *ScriptEditorController::get_left_list_split() {
	ScriptEditorView *view = _active_view();
	return view ? view->get_left_list_split() : nullptr;
}

void ScriptEditorController::_script_created(Ref<Script> p_script) {
	EditorNode::get_singleton()->push_item(p_script.operator->());
}

void ScriptEditorController::_open_script_request_static(const String &p_path) {
	if (singleton) {
		singleton->_open_script_request(p_path);
	}
}

void ScriptEditorController::_open_script_request(const String &p_path) {
	Ref<Script> scr = ResourceLoader::load(p_path);
	if (scr.is_valid()) {
		edit(scr, false);
		return;
	}
	Ref<JSON> json = ResourceLoader::load(p_path);
	if (json.is_valid()) {
		edit(json, false);
		return;
	}
	if (ScriptEditorView *view = _active_view()) {
		Error err;
		Ref<TextFile> text_file = view->_load_text_file(p_path, &err);
		if (text_file.is_valid()) {
			edit(text_file, false);
		}
	}
}

void ScriptEditorController::_on_replace_in_files_requested(const String &text) {
	ERR_FAIL_NULL(find_in_files_dialog);
	find_in_files_dialog->set_find_in_files_mode(FindInFilesDialog::REPLACE_MODE);
	find_in_files_dialog->set_search_text(text);
	find_in_files_dialog->set_replace_text("");
	find_in_files_dialog->popup_centered();
}

void ScriptEditorController::_start_find_in_files(bool with_replace) {
	FindInFilesPanel *panel = find_in_files->get_panel_for_results(with_replace ? TTR("Replace:") + " " + find_in_files_dialog->get_search_text() : TTR("Find:") + " " + find_in_files_dialog->get_search_text());
	FindInFiles *f = panel->get_finder();

	f->set_search_text(find_in_files_dialog->get_search_text());
	f->set_match_case(find_in_files_dialog->is_match_case());
	f->set_whole_words(find_in_files_dialog->is_whole_words());
	f->set_folder(find_in_files_dialog->get_folder());
	f->set_filter(find_in_files_dialog->get_filter());
	f->set_includes(find_in_files_dialog->get_includes());
	f->set_excludes(find_in_files_dialog->get_excludes());

	panel->set_with_replace(with_replace);
	panel->set_replace_text(find_in_files_dialog->get_replace_text());
	panel->start_search();

	find_in_files->make_visible();
}

void ScriptEditorController::_on_find_in_files_modified_files(const PackedStringArray &paths) {
	test_script_times_on_disk();
	for (ScriptEditorView *view : views) {
		view->_update_modified_scripts_for_external_editor();
	}
}

void ScriptEditorController::_autosave_scripts() {
	save_all_scripts();
}

void ScriptEditorController::_update_autosave_timer() {
	if (!autosave_timer || !autosave_timer->is_inside_tree()) {
		return;
	}
	float autosave_time = EDITOR_GET("text_editor/behavior/files/autosave_interval_secs");
	if (autosave_time > 0) {
		autosave_timer->set_wait_time(autosave_time);
		autosave_timer->start();
	} else {
		autosave_timer->stop();
	}
}

void ScriptEditorController::_resave_scripts(const String &p_str) {
	if (p_str == "resave") {
		save_all_scripts();
	}
}

void ScriptEditorController::_filesystem_changed() {
	reload_scripts(true);
}

void ScriptEditorController::_files_moved(const String &p_old_file, const String &p_new_file) {
	if (!script_editor_cache->has_section(p_old_file)) {
		return;
	}
	Variant state = script_editor_cache->get_value(p_old_file, "state");
	script_editor_cache->erase_section(p_old_file);
	script_editor_cache->set_value(p_new_file, "state", state);
}

void ScriptEditorController::_file_removed(const String &p_removed_file) {
	if (script_editor_cache->has_section(p_removed_file)) {
		script_editor_cache->erase_section(p_removed_file);
	}
}

void ScriptEditorController::trigger_live_script_reload(const String &p_script_path) {
	if (!script_paths_to_reload.has(p_script_path)) {
		Ref<Script> reloaded_script = ResourceCache::get_ref(p_script_path);
		if (reloaded_script.is_null()) {
			reloaded_script = ResourceLoader::load(p_script_path);
		}
		if (reloaded_script.is_valid()) {
			if (!reloaded_script->get_language()->validate(reloaded_script->get_source_code(), p_script_path)) {
				return;
			}
		}
		script_paths_to_reload.append(p_script_path);
	}
	if (!pending_auto_reload && auto_reload_running_scripts) {
		callable_mp(this, &ScriptEditorController::_live_auto_reload_running_scripts).call_deferred();
		pending_auto_reload = true;
	}
}

void ScriptEditorController::_live_auto_reload_running_scripts() {
	pending_auto_reload = false;
	if (EditorDebuggerNode *debugger = EditorDebuggerNode::get_singleton()) {
		debugger->reload_scripts(script_paths_to_reload);
	}
	script_paths_to_reload.clear();
}

void ScriptEditorController::_goto_script_line(Ref<RefCounted> p_script, int p_line) {
	Ref<Script> scr = Object::cast_to<Script>(*p_script);
	if (!scr.is_valid()) {
		return;
	}
	for (ScriptEditorView *view : views) {
		if (view->edit(p_script, p_line, 0)) {
			set_focused_view(view);
			EditorNode::get_singleton()->push_item(p_script.ptr());
			if (ScriptEditorBase *current = view->_get_current_editor()) {
				if (ScriptTextEditor *script_text_editor = Object::cast_to<ScriptTextEditor>(current)) {
					script_text_editor->goto_line_centered(p_line);
				} else {
					current->goto_line(p_line);
				}
				view->_save_history();
			}
			return;
		}
	}
}

void ScriptEditorController::_set_execution(Ref<RefCounted> p_script, int p_line) {
	for (ScriptEditorView *view : views) {
		Ref<Script> scr = Object::cast_to<Script>(*p_script);
		if (!scr.is_valid()) {
			continue;
		}
		for (int i = 0; i < view->get_tab_container()->get_tab_count(); i++) {
			ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(view->get_tab_container()->get_tab_control(i));
			if (se && ((scr.is_valid() && se->get_edited_resource() == p_script) || se->get_edited_resource()->get_path() == scr->get_path())) {
				se->set_executing_line(p_line);
			}
		}
	}
}

void ScriptEditorController::_clear_execution(Ref<RefCounted> p_script) {
	for (ScriptEditorView *view : views) {
		Ref<Script> scr = Object::cast_to<Script>(*p_script);
		if (!scr.is_valid()) {
			continue;
		}
		for (int i = 0; i < view->get_tab_container()->get_tab_count(); i++) {
			ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(view->get_tab_container()->get_tab_control(i));
			if (se && ((scr.is_valid() && se->get_edited_resource() == p_script) || se->get_edited_resource()->get_path() == scr->get_path())) {
				se->clear_executing_line();
			}
		}
	}
}

void ScriptEditorController::_breaked(bool p_breaked, bool p_can_debug) {
	if (external_editor_active) {
		return;
	}
	for (ScriptEditorView *view : views) {
		for (int i = 0; i < view->get_tab_container()->get_tab_count(); i++) {
			ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(view->get_tab_container()->get_tab_control(i));
			if (se) {
				se->set_debugger_active(p_breaked);
			}
		}
	}
}

void ScriptEditorController::_set_breakpoint(Ref<RefCounted> p_script, int p_line, bool p_enabled) {
	Ref<Script> scr = Object::cast_to<Script>(*p_script);
	if (!scr.is_valid()) {
		return;
	}
	for (ScriptEditorView *view : views) {
		for (int i = 0; i < view->get_tab_container()->get_tab_count(); i++) {
			ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(view->get_tab_container()->get_tab_control(i));
			if (se && se->get_edited_resource()->get_path() == scr->get_path()) {
				se->set_breakpoint(p_line, p_enabled);
				return;
			}
		}
	}
	Dictionary state = script_editor_cache->get_value(scr->get_path(), "state");
	Array breakpoints;
	if (state.has("breakpoints")) {
		breakpoints = state["breakpoints"];
	}
	if (breakpoints.has(p_line)) {
		if (!p_enabled) {
			breakpoints.erase(p_line);
		}
	} else if (p_enabled) {
		breakpoints.push_back(p_line);
	}
	state["breakpoints"] = breakpoints;
	script_editor_cache->set_value(scr->get_path(), "state", state);
	if (EditorDebuggerNode *debugger = EditorDebuggerNode::get_singleton()) {
		debugger->set_breakpoint(scr->get_path(), p_line + 1, p_enabled);
	}
}

void ScriptEditorController::_clear_breakpoints() {
	for (ScriptEditorView *view : views) {
		for (int i = 0; i < view->get_tab_container()->get_tab_count(); i++) {
			ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(view->get_tab_container()->get_tab_control(i));
			if (se) {
				se->clear_breakpoints();
			}
		}
	}
	Vector<String> cached_editors = script_editor_cache->get_sections();
	EditorDebuggerNode *debugger = EditorDebuggerNode::get_singleton();
	for (const String &E : cached_editors) {
		Array breakpoints = get_cached_breakpoints_for_script(E);
		for (int breakpoint : breakpoints) {
			if (debugger) {
				debugger->set_breakpoint(E, (int)breakpoint + 1, false);
			}
		}
		if (breakpoints.size() > 0) {
			Dictionary state = script_editor_cache->get_value(E, "state");
			state["breakpoints"] = Array();
			script_editor_cache->set_value(E, "state", state);
		}
	}
}

bool ScriptEditorController::_open_help_in_workspace(const String &p_topic) {
	EditorSceneWorkspace *workspace = EditorNode::get_scene_workspace();
	if (!workspace) {
		return false;
	}
	WorkspaceLeafNode *source = workspace->get_focused_leaf();
	if (!source) {
		const Vector<WorkspaceLeafNode *> workspace_leaves = workspace->get_leaves();
		if (workspace_leaves.is_empty()) {
			return false;
		}
		source = workspace_leaves[0];
	}
	return workspace->open_help_tab(source, p_topic) != nullptr;
}

void ScriptEditorController::_help_class_goto(const String &p_desc) {
	// Class reference lives in the workspace as its own help tab.
	_open_help_in_workspace(p_desc);
}

void ScriptEditorController::_on_find_in_files_result_selected(const String &fpath, int line_number, int begin, int end) {
	if (ResourceLoader::exists(fpath)) {
		Ref<Resource> res = ResourceLoader::load(fpath);
		if (fpath.get_extension() == "gdshader") {
			ShaderEditorPlugin *shader_editor = Object::cast_to<ShaderEditorPlugin>(EditorNode::get_editor_data().get_editor_by_name("Shader"));
			if (shader_editor) {
				shader_editor->edit(res.ptr());
				shader_editor->make_visible(true);
				TextShaderEditor *text_shader_editor = Object::cast_to<TextShaderEditor>(shader_editor->get_shader_editor(res));
				if (text_shader_editor) {
					text_shader_editor->goto_line_selection(line_number - 1, begin, end);
				}
			}
			return;
		}
		edit(res, line_number - 1, begin, true);
	}
}

void ScriptEditorController::_bind_methods() {
	ClassDB::bind_method("get_breakpoints", &ScriptEditorController::_get_breakpoints);
	ClassDB::bind_method(D_METHOD("register_syntax_highlighter", "syntax_highlighter"), &ScriptEditorController::register_syntax_highlighter);
	ClassDB::bind_method(D_METHOD("unregister_syntax_highlighter", "syntax_highlighter"), &ScriptEditorController::unregister_syntax_highlighter);
	ClassDB::bind_method(D_METHOD("open_script_create_dialog", "base_name", "base_path"), &ScriptEditorController::open_script_create_dialog);
	ClassDB::bind_method(D_METHOD("goto_help", "topic"), &ScriptEditorController::goto_help);
	ClassDB::bind_method(D_METHOD("update_docs_from_script", "script"), &ScriptEditorController::update_docs_from_script);
	ClassDB::bind_method(D_METHOD("clear_docs_from_script", "script"), &ScriptEditorController::clear_docs_from_script);
	ADD_SIGNAL(MethodInfo("editor_script_changed", PropertyInfo(Variant::OBJECT, "script", PROPERTY_HINT_RESOURCE_TYPE, "Script")));
	ADD_SIGNAL(MethodInfo("script_close", PropertyInfo(Variant::OBJECT, "script", PROPERTY_HINT_RESOURCE_TYPE, "Script")));
	ADD_SIGNAL(MethodInfo("script_add_function_request", PropertyInfo(Variant::OBJECT, "obj"), PropertyInfo(Variant::STRING, "function"), PropertyInfo(Variant::PACKED_STRING_ARRAY, "args")));
}
