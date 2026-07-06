/**************************************************************************/
/*  script_editor_view.cpp                                               */
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

#include "script_editor_controller.h"
#include "script_editor_view.h"
#include "script_editor_plugin.h"

#include "core/config/project_build_pipeline_config.h"
#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/io/resource.h"
#include "core/os/keyboard.h"
#include "core/os/os.h"
#include "core/string/fuzzy_search.h"
#include "core/version.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/doc/editor_help.h"
#include "editor/doc/editor_help_search.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/docks/inspector_dock.h"
#include "editor/docks/signals_dock.h"
#include "editor/editor_interface.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/editor_script_leaf.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/file_system/editor_paths.h"
#include "editor/gui/code_editor.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/gui/editor_toaster.h"
#include "editor/gui/window_wrapper.h"
#include "editor/inspector/editor_context_menu_plugin.h"
#include "editor/run/editor_run_bar.h"
#include "editor/scene/editor_scene_tabs.h"
#include "editor/script/editor_script.h"
#include "editor/script/find_in_files.h"
#include "editor/script/script_refactor_apply.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/settings/editor_settings.h"
#include "editor/shader/shader_editor_plugin.h"
#include "editor/shader/text_shader_editor.h"
#include "editor/themes/editor_scale.h"
#include "editor/themes/editor_theme_manager.h"
#include "modules/modules_enabled.gen.h"

#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
#include "modules/foundry_script/fs_autoload_index.h"
#endif
#include "scene/gui/separator.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/texture_rect.h"
#include "scene/main/node.h"
#include "scene/main/window.h"
#include "script_text_editor.h"
#include "servers/display/display_server.h"
#include "text_editor.h"

namespace {

const char *SCRIPT_REFACTOR_ACTION_NAME = "Apply Script Refactor";

String get_script_refactor_action_name() {
	return TTR(SCRIPT_REFACTOR_ACTION_NAME);
}

bool is_embedded_script_path(const String &p_path) {
	return p_path.begins_with("local://") || p_path.contains("::");
}

void warn_if_saving_declared_build_output(const String &p_path) {
	if (!p_path.begins_with("res://") || !FileAccess::exists("res://project.foundry")) {
		return;
	}

	Ref<ConfigFile> config;
	config.instantiate();
	if (config->load("res://project.foundry") != OK) {
		return;
	}

	ProjectBuildPipelineConfig build_config;
	if (build_config.load_from_config_file(config) != OK) {
		return;
	}

	String task_name;
	String output_root;
	if (!build_config.is_declared_output_path(p_path, &task_name, &output_root)) {
		return;
	}

	EditorToaster::get_singleton()->popup_str(
			vformat(TTR("Saving \"%s\" edits generated output declared by build task \"%s\" (%s). Manual edits may be overwritten the next time that task runs."),
					p_path, task_name, output_root),
			EditorToaster::SEVERITY_WARNING);
}

} // namespace

/*** SCRIPT EDITOR VIEW ******/

/////////////////////////////////


/*** SCRIPT EDITOR ******/

String ScriptEditorView::_get_debug_tooltip(const String &p_text, Node *p_se) {
	if (EDITOR_GET("text_editor/behavior/documentation/enable_tooltips")) {
		return String();
	}

	// NOTE: See also `ScriptTextEditor::_show_symbol_tooltip()` for documentation tooltips enabled.
	String debug_value = EditorDebuggerNode::get_singleton()->get_var_value(p_text);
	if (!debug_value.is_empty()) {
		constexpr int DISPLAY_LIMIT = 1024;
		if (debug_value.size() > DISPLAY_LIMIT) {
			debug_value = debug_value.left(DISPLAY_LIMIT) + "... " + TTR("(truncated)");
		}
		debug_value = TTR("Current value: ") + debug_value;
	}

	return debug_value;
}



void ScriptEditorView::_goto_script_line2(int p_line) {
	ScriptEditorBase *current = _get_current_editor();
	if (current) {
		current->goto_line(p_line);
	}
}


ScriptEditorBase *ScriptEditorView::_get_current_editor() const {
	int selected = tab_container->get_current_tab();
	if (selected < 0 || selected >= tab_container->get_tab_count()) {
		return nullptr;
	}

	return Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(selected));
}

bool ScriptEditorView::get_current_script_view_state(String &r_path, int &r_line, int &r_column) const {
	ScriptEditorBase *editor = _get_current_editor();
	if (editor == nullptr) {
		return false;
	}

	Ref<Resource> resource = editor->get_edited_resource();
	if (resource.is_valid()) {
		r_path = resource->get_path();
	} else {
		r_path = editor->edited_file_data.path;
	}
	if (r_path.is_empty()) {
		return false;
	}

	CodeTextEditor *code_editor = editor->get_code_editor();
	if (code_editor == nullptr) {
		r_line = -1;
		r_column = -1;
		return true;
	}

	TextEdit *text_editor = code_editor->get_text_editor();
	if (text_editor == nullptr) {
		r_line = -1;
		r_column = -1;
		return true;
	}

	r_line = text_editor->get_caret_line() + 1;
	r_column = text_editor->get_caret_column() + 1;
	return true;
}

ScriptEditorBase *ScriptEditorView::get_open_editor_for_path(const String &p_path) const {
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));
		if (!se) {
			continue;
		}

		Ref<Resource> edited_resource = se->get_edited_resource();
		if (edited_resource.is_valid() && edited_resource->get_path() == p_path) {
			return se;
		}
		if (se->edited_file_data.path == p_path) {
			return se;
		}
	}
	return nullptr;
}

void ScriptEditorView::_update_history_arrows() {
	script_back->set_disabled(history_pos <= 0);
	script_forward->set_disabled(history_pos >= history.size() - 1);
}

void ScriptEditorView::_save_history() {
	if (history_pos >= 0 && history_pos < history.size() && history[history_pos].control == tab_container->get_current_tab_control()) {
		Node *n = tab_container->get_current_tab_control();

		if (Object::cast_to<ScriptEditorBase>(n)) {
			history.write[history_pos].state = Object::cast_to<ScriptEditorBase>(n)->get_navigation_state();
		}
		if (Object::cast_to<EditorHelp>(n)) {
			history.write[history_pos].state = Object::cast_to<EditorHelp>(n)->get_scroll();
		}
	}

	history.resize(history_pos + 1);
	ScriptHistory sh;
	sh.control = tab_container->get_current_tab_control();
	sh.state = Variant();

	history.push_back(sh);
	history_pos++;

	_update_history_arrows();
}

void ScriptEditorView::_save_previous_state(Dictionary p_state) {
	if (lock_history) {
		// Done as a result of a deferred call triggered by set_edit_state().
		return;
	}

	if (history_pos >= 0 && history_pos < history.size() && history[history_pos].control == tab_container->get_current_tab_control()) {
		Node *n = tab_container->get_current_tab_control();

		if (Object::cast_to<ScriptTextEditor>(n)) {
			history.write[history_pos].state = p_state;
		}
	}

	history.resize(history_pos + 1);
	ScriptHistory sh;
	sh.control = tab_container->get_current_tab_control();
	sh.state = Variant();

	history.push_back(sh);
	history_pos++;

	_update_history_arrows();
}

void ScriptEditorView::_go_to_tab(int p_idx) {
	ScriptEditorBase *current = _get_current_editor();
	if (current) {
		if (current->is_unsaved()) {
			current->apply_code();
		}
	}

	Control *c = tab_container->get_tab_control(p_idx);
	if (!c) {
		return;
	}

	if (history_pos >= 0 && history_pos < history.size() && history[history_pos].control == tab_container->get_current_tab_control()) {
		Node *n = tab_container->get_current_tab_control();

		if (Object::cast_to<ScriptEditorBase>(n)) {
			history.write[history_pos].state = Object::cast_to<ScriptEditorBase>(n)->get_navigation_state();
		}
		if (Object::cast_to<EditorHelp>(n)) {
			history.write[history_pos].state = Object::cast_to<EditorHelp>(n)->get_scroll();
		}
	}

	history.resize(history_pos + 1);
	ScriptHistory sh;
	sh.control = c;
	sh.state = Variant();

	if (!lock_history && (history.is_empty() || history[history.size() - 1].control != sh.control)) {
		history.push_back(sh);
		history_pos++;
	}

	tab_container->set_current_tab(p_idx);

	c = tab_container->get_current_tab_control();

	ScriptEditorBase *seb = Object::cast_to<ScriptEditorBase>(c);
	if (seb) {
		if (is_visible_in_tree()) {
			seb->ensure_focus();
		}

		Ref<Script> scr = seb->get_edited_resource();
		if (scr.is_valid()) {
			controller->notify_script_changed(scr);
		}

		seb->validate();
	}

	EditorHelp *eh = Object::cast_to<EditorHelp>(c);
	if (eh) {
		script_name_label->set_text(eh->get_class());

		if (is_visible_in_tree()) {
			eh->set_focused();
		}
	}

	c->set_meta("__editor_pass", ++edit_pass);
	_update_history_arrows();
	_update_script_colors();
	_update_members_overview();
	_update_help_overview();
	_update_selected_editor_menu();
	_update_online_doc();
	_update_members_overview_visibility();
	_update_help_overview_visibility();
}

void ScriptEditorView::_update_recent_scripts() {
	if (!recent_scripts) {
		return;
	}
	Array rc = EditorSettings::get_singleton()->get_project_metadata("recent_files", "scripts", Array());
	recent_scripts->clear();

	String path;
	for (int i = 0; i < rc.size(); i++) {
		path = rc[i];
		recent_scripts->add_item(path.replace("res://", ""));
	}

	recent_scripts->add_separator();
	recent_scripts->add_shortcut(ED_GET_SHORTCUT("script_editor/clear_recent"));
	recent_scripts->set_item_auto_translate_mode(-1, AUTO_TRANSLATE_MODE_ALWAYS);
	recent_scripts->set_item_disabled(-1, rc.is_empty());

	recent_scripts->reset_size();
}

void ScriptEditorView::_open_recent_script(int p_idx) {
	// clear button
	if (p_idx == recent_scripts->get_item_count() - 1) {
		EditorSettings::get_singleton()->set_project_metadata("recent_files", "scripts", Array());
		callable_mp(this, &ScriptEditorView::_update_recent_scripts).call_deferred();
		return;
	}

	Array rc = EditorSettings::get_singleton()->get_project_metadata("recent_files", "scripts", Array());
	ERR_FAIL_INDEX(p_idx, rc.size());

	String path = rc[p_idx];
	// if its not on disk its a help file or deleted
	if (FileAccess::exists(path)) {
		List<String> extensions;
		ResourceLoader::get_recognized_extensions_for_type("Script", &extensions);
		ResourceLoader::get_recognized_extensions_for_type("JSON", &extensions);

		if (extensions.find(path.get_extension())) {
			Ref<Resource> scr = ResourceLoader::load(path);
			if (scr.is_valid()) {
				edit(scr, true);
				return;
			}
		}

		Error err;
		Ref<TextFile> text_file = _load_text_file(path, &err);
		if (text_file.is_valid()) {
			edit(text_file, true);
			return;
		}
		// if it's a path then it's most likely a deleted file not help
	} else if (path.contains("::")) {
		// built-in script
		String res_path = path.get_slice("::", 0);
		EditorNode::get_singleton()->load_scene_or_resource(res_path, false, false);

		Ref<Script> scr = ResourceLoader::load(path);
		if (scr.is_valid()) {
			edit(scr, true);
			return;
		}
	} else if (!path.is_resource_file()) {
		_help_class_open(path);
		return;
	}

	rc.remove_at(p_idx);
	EditorSettings::get_singleton()->set_project_metadata("recent_files", "scripts", rc);
	_update_recent_scripts();
	controller->show_error_dialog(path);
}

void ScriptEditorView::_close_tab(int p_idx, bool p_save, bool p_history_back) {
	int selected = p_idx;
	if (selected < 0 || selected >= tab_container->get_tab_count()) {
		return;
	}

	Node *tselected = tab_container->get_tab_control(selected);

	ScriptEditorBase *current = Object::cast_to<ScriptEditorBase>(tselected);
	if (current) {
		Ref<Resource> file = current->get_edited_resource();
		if (p_save && file.is_valid()) {
			// Do not try to save internal scripts, but prompt to save in-memory
			// scripts which are not saved to disk yet (have empty path).
			if (!file->is_built_in()) {
				save_current_script();
			}
		}
		if (file.is_valid()) {
			if (!file->get_path().is_empty()) {
				// Only saved scripts can be restored.
				controller->get_previous_scripts().push_back(file->get_path());
			}

			Ref<Script> scr = file;
			if (scr.is_valid()) {
				controller->notify_script_close(scr);
			}
		}
	}

	// roll back to previous tab
	if (p_history_back) {
		_history_back();
	}

	//remove from history
	history.resize(history_pos + 1);

	for (int i = 0; i < history.size(); i++) {
		if (history[i].control == tselected) {
			history.remove_at(i);
			i--;
			history_pos--;
		}
	}

	if (history_pos >= history.size()) {
		history_pos = history.size() - 1;
	}

	int idx = tab_container->get_current_tab();
	if (current) {
		current->clear_edit_menu();
		_save_editor_state(current);
	}
	memdelete(tselected);

	if (controller->get_script_close_queue().is_empty()) {
		if (idx >= tab_container->get_tab_count()) {
			idx = tab_container->get_tab_count() - 1;
		}
		if (idx >= 0) {
			if (history_pos >= 0) {
				idx = tab_container->get_tab_idx_from_control(history[history_pos].control);
			}
			_go_to_tab(idx);
		} else {
			_update_selected_editor_menu();
			_update_online_doc();
			script_name_label->set_text(String());
		}

		_update_history_arrows();
		_update_script_names();
		_save_layout();
		_update_find_replace_bar();
	}
}

void ScriptEditorView::_close_current_tab(bool p_save, bool p_history_back) {
	_close_tab(tab_container->get_current_tab(), p_save, p_history_back);
}

void ScriptEditorView::_close_discard_current_tab(const String &p_str) {
	Ref<Script> scr = _get_current_script();
	if (scr.is_valid()) {
		scr->reload_from_file();
	}
	_close_tab(tab_container->get_current_tab(), false);
	erase_tab_confirm->hide();
}

void ScriptEditorView::_close_docs_tab() {
	int child_count = tab_container->get_tab_count();
	for (int i = child_count - 1; i >= 0; i--) {
		EditorHelp *se = Object::cast_to<EditorHelp>(tab_container->get_tab_control(i));

		if (se) {
			_close_tab(i, true, false);
		}
	}
}

void ScriptEditorView::_copy_script_path() {
	ScriptEditorBase *se = _get_current_editor();
	if (se) {
		Ref<Resource> scr = se->get_edited_resource();
		DisplayServer::get_singleton()->clipboard_set(scr->get_path());
	}
}

void ScriptEditorView::_copy_script_uid() {
	ScriptEditorBase *se = _get_current_editor();
	if (se) {
		Ref<Resource> scr = se->get_edited_resource();
		ResourceUID::ID uid = ResourceLoader::get_resource_uid(scr->get_path());
		DisplayServer::get_singleton()->clipboard_set(ResourceUID::get_singleton()->id_to_text(uid));
	}
}

void ScriptEditorView::_set_refactor_file_source(const String &p_path, const String &p_source, bool p_source_is_saved_version) {
	ScriptEditorBase *se = get_open_editor_for_path(p_path);
	if (se != nullptr) {
		CodeTextEditor *code_editor = se->get_code_editor();
		ERR_FAIL_NULL(code_editor);
		CodeEdit *text_editor = code_editor->get_text_editor();
		ERR_FAIL_NULL(text_editor);

		ScriptRefactorApply::replace_editor_text(text_editor, p_source, p_source_is_saved_version);
		se->apply_code();
		return;
	}

	String error_message;
	ERR_FAIL_COND_MSG(!ScriptRefactorApply::write_file(p_path, p_source, error_message), error_message);

	if (EditorFileSystem::get_singleton()) {
		EditorFileSystem::get_singleton()->update_file(p_path);
	}
}

void ScriptEditorView::_close_other_tabs() {
	int current_idx = tab_container->get_current_tab();
	for (int i = tab_container->get_tab_count() - 1; i >= 0; i--) {
		if (i != current_idx) {
			controller->get_script_close_queue().push_back(i);
		}
	}
	_queue_close_tabs();
}

void ScriptEditorView::_close_tabs_below() {
	int current_idx = tab_container->get_current_tab();
	for (int i = tab_container->get_tab_count() - 1; i > current_idx; i--) {
		controller->get_script_close_queue().push_back(i);
	}
	_go_to_tab(current_idx);
	_queue_close_tabs();
}

void ScriptEditorView::_close_all_tabs() {
	for (int i = tab_container->get_tab_count() - 1; i >= 0; i--) {
		controller->get_script_close_queue().push_back(i);
	}
	_queue_close_tabs();
}

void ScriptEditorView::_queue_close_tabs() {
	while (!controller->get_script_close_queue().is_empty()) {
		int idx = controller->get_script_close_queue().front()->get();
		controller->get_script_close_queue().pop_front();

		tab_container->set_current_tab(idx);
		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(idx));
		if (se) {
			// Maybe there are unsaved changes.
			if (se->is_unsaved()) {
				_ask_close_current_unsaved_tab(se);
				erase_tab_confirm->connect(SceneStringName(visibility_changed), callable_mp(this, &ScriptEditorView::_queue_close_tabs), CONNECT_ONE_SHOT);
				break;
			}
		}

		_close_current_tab(false, false);
	}
	_update_find_replace_bar();
}

void ScriptEditorView::_ask_close_current_unsaved_tab(ScriptEditorBase *current) {
	erase_tab_confirm->set_text(TTR("Close and save changes?") + "\n\"" + current->get_name() + "\"");
	erase_tab_confirm->popup_centered();
}


void ScriptEditorView::_res_saved_callback(const Ref<Resource> &p_res) {
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));
		if (!se) {
			continue;
		}

		Ref<Resource> scr = se->get_edited_resource();

		if (scr == p_res) {
			se->tag_saved_version();
		}
	}

	if (p_res.is_valid()) {
		// In case the Resource has built-in scripts.
		_mark_built_in_scripts_as_saved(p_res->get_path());
	}

	_update_script_names();
	Ref<Script> scr = p_res;
	if (scr.is_valid()) {
		controller->trigger_live_script_reload(scr->get_path());
	}
}

void ScriptEditorView::_scene_saved_callback(const String &p_path) {
	// If scene was saved, mark all built-in scripts from that scene as saved.
	_mark_built_in_scripts_as_saved(p_path);
}

void ScriptEditorView::_mark_built_in_scripts_as_saved(const String &p_parent_path) {
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));
		if (!se) {
			continue;
		}

		Ref<Resource> edited_res = se->get_edited_resource();
		if (!edited_res->is_built_in()) {
			continue; // External script, who cares.
		}

		if (edited_res->get_path().get_slice("::", 0) != p_parent_path) {
			continue; // Wrong scene.
		}
		se->tag_saved_version();

		Ref<Script> scr = edited_res;
		if (scr.is_valid()) {
			controller->trigger_live_script_reload(scr->get_path());
			clear_docs_from_script(scr);
			scr->reload(true);
			update_docs_from_script(scr);
		}
	}
}


bool ScriptEditorView::_test_script_times_on_disk(Ref<Resource> p_for_script) {
	controller->get_disk_changed_list()->clear();
	TreeItem *r = controller->get_disk_changed_list()->create_item();

	bool need_ask = false;
	bool need_reload = false;
	bool use_autoreload = EDITOR_GET("text_editor/behavior/files/auto_reload_scripts_on_external_change");

	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));
		if (se) {
			Ref<Resource> edited_res = se->get_edited_resource();
			if (p_for_script.is_valid() && edited_res.is_valid() && p_for_script != edited_res) {
				continue;
			}

			if (edited_res->is_built_in()) {
				continue; // Internal script, who cares.
			}

			uint64_t last_date = se->edited_file_data.last_modified_time;
			uint64_t date = FileAccess::get_modified_time(se->edited_file_data.path);

			if (last_date != date) {
				TreeItem *ti = controller->get_disk_changed_list()->create_item(r);
				ti->set_text(0, se->edited_file_data.path.get_file());

				if (!use_autoreload || se->is_unsaved()) {
					need_ask = true;
				}
				need_reload = true;
			}
		}
	}

	if (need_reload) {
		if (!need_ask) {
			controller->reload_scripts();
			need_reload = false;
		} else {
			callable_mp((Window *)controller->get_disk_changed(), &Window::popup_centered_ratio).call_deferred(0.3);
		}
	}

	return need_reload;
}

void _import_text_editor_theme(const String &p_file) {
	if (p_file.get_extension() != "tet") {
		EditorToaster::get_singleton()->popup_str(TTR("Importing theme failed. File is not a text editor theme file (.tet)."), EditorToaster::SEVERITY_ERROR);
		return;
	}
	const String theme_name = p_file.get_file().get_basename();
	if (EditorSettings::is_default_text_editor_theme(theme_name.to_lower())) {
		EditorToaster::get_singleton()->popup_str(TTR("Importing theme failed. File name cannot be 'Default', 'Custom', or 'Godot 2'."), EditorToaster::SEVERITY_ERROR);
		return;
	}

	const String theme_dir = EditorPaths::get_singleton()->get_text_editor_themes_dir();
	Ref<DirAccess> d = DirAccess::open(theme_dir);
	Error err = FAILED;
	if (d.is_valid()) {
		err = d->copy(p_file, theme_dir.path_join(p_file.get_file()));
	}

	if (err != OK) {
		EditorToaster::get_singleton()->popup_str(TTR("Importing theme failed. Failed to copy theme file."), EditorToaster::SEVERITY_ERROR);
		return;
	}

	// Reload themes and switch to new theme.
	EditorSettings::get_singleton()->update_text_editor_themes_list();
	EditorSettings::get_singleton()->set_manually("text_editor/theme/color_theme", theme_name, true);
	EditorSettings::get_singleton()->notify_changes();
}

void _save_text_editor_theme_as(const String &p_file) {
	String file = p_file;
	if (p_file.get_extension() != "tet") {
		file += ".tet";
	}

	const String theme_name = file.get_file().get_basename();
	if (EditorSettings::is_default_text_editor_theme(theme_name.to_lower())) {
		EditorToaster::get_singleton()->popup_str(TTR("Saving theme failed. File name cannot be 'Default', 'Custom', or 'Godot 2'."), EditorToaster::SEVERITY_ERROR);
		return;
	}

	const String theme_section = "color_theme";
	const Ref<ConfigFile> cf = memnew(ConfigFile);

	// Use the keys from the Godot 2 theme to know which settings to save.
	HashMap<StringName, Color> text_colors = EditorSettings::get_godot2_text_editor_theme();
	text_colors.sort();
	for (const KeyValue<StringName, Color> &text_color : text_colors) {
		const Color val = EditorSettings::get_singleton()->get_setting(text_color.key);
		const String &key = text_color.key.operator String().replace("text_editor/theme/highlighting/", "");
		cf->set_value(theme_section, key, val.to_html());
	}

	const Error err = cf->save(file);
	if (err != OK) {
		EditorToaster::get_singleton()->popup_str(TTR("Saving theme failed."), EditorToaster::SEVERITY_ERROR);
		return;
	}

	// Reload themes and switch to saved theme.
	EditorSettings::get_singleton()->update_text_editor_themes_list();
	if (p_file.get_base_dir() == EditorPaths::get_singleton()->get_text_editor_themes_dir()) {
		// Don't need to emit signal or notify changes as the colors are already set.
		EditorSettings::get_singleton()->set_manually("text_editor/theme/color_theme", theme_name, false);
	}
}

void ScriptEditorView::_file_dialog_action(const String &p_file) {
	switch (controller->get_file_dialog_option()) {
		case FILE_MENU_NEW_TEXTFILE: {
			Error err;
			{
				Ref<FileAccess> file = FileAccess::open(p_file, FileAccess::WRITE, &err);
				if (err) {
					EditorNode::get_singleton()->show_warning(TTR("Error writing TextFile:") + "\n" + p_file, TTR("Error!"));
					break;
				}
			}

			if (EditorFileSystem::get_singleton()) {
				if (textfile_extensions.has(p_file.get_extension())) {
					EditorFileSystem::get_singleton()->update_file(p_file);
				}
			}
			[[fallthrough]];
		}
		case FILE_MENU_OPEN: {
			if (!is_visible_in_tree()) {
				EditorNode::get_singleton()->get_editor_main_screen()->select(EditorMainScreen::EDITOR_SCRIPT);
			}
			open_file(p_file);
			controller->set_file_dialog_option(-1);
		} break;
		case FILE_MENU_SAVE_AS: {
			ScriptEditorBase *current = _get_current_editor();
			if (current) {
				Ref<Resource> resource = current->get_edited_resource();
				String path = ProjectSettings::get_singleton()->localize_path(p_file);
				Error err = _save_text_file(resource, path);

				if (err != OK) {
					EditorNode::get_singleton()->show_accept(TTR("Error saving file!"), TTR("OK"));
					return;
				}

				resource->set_path(path);
				_update_script_names();
			}
		} break;
		case THEME_SAVE_AS: {
			_save_text_editor_theme_as(p_file);
		} break;
		case THEME_IMPORT: {
			_import_text_editor_theme(p_file);
		} break;
	}
	controller->set_file_dialog_option(-1);
}

bool ScriptEditorView::_script_exists(const String &p_path) const {
	if (p_path.is_empty()) {
		return false;
	} else if (p_path.is_resource_file()) {
		return FileAccess::exists(p_path);
	} else {
		return FileAccess::exists(p_path.get_slice("::", 0));
	}
}


Ref<Script> ScriptEditorView::_get_current_script() {
	ScriptEditorBase *current = _get_current_editor();

	if (current) {
		Ref<Script> scr = current->get_edited_resource();
		return scr.is_valid() ? scr : nullptr;
	} else {
		return nullptr;
	}
}

TypedArray<Script> ScriptEditorView::_get_open_scripts() const {
	TypedArray<Script> ret;
	Vector<Ref<Script>> scripts = collect_open_scripts();
	int scripts_amount = scripts.size();
	for (int idx_script = 0; idx_script < scripts_amount; idx_script++) {
		ret.push_back(scripts[idx_script]);
	}
	return ret;
}

bool ScriptEditorView::toggle_files_panel() {
	list_split->set_visible(!list_split->is_visible());
	EditorSettings::get_singleton()->set_project_metadata("files_panel", "show_files_panel", list_split->is_visible());
	return list_split->is_visible();
}

bool ScriptEditorView::is_files_panel_toggled() {
	return list_split->is_visible();
}

void ScriptEditorView::_menu_option(int p_option) {
	ScriptEditorBase *current = _get_current_editor();
	switch (p_option) {
		case FILE_MENU_NEW: {
			controller->get_script_create_dialog()->config("Node", "new_script", false, false);
			controller->get_script_create_dialog()->popup_centered();
		} break;
		case FILE_MENU_NEW_TEXTFILE: {
			controller->get_file_dialog()->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
			controller->get_file_dialog()->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
			controller->set_file_dialog_option(FILE_MENU_NEW_TEXTFILE);

			controller->get_file_dialog()->clear_filters();
			for (const String &E : textfile_extensions) {
				controller->get_file_dialog()->add_filter("*." + E, E.to_upper());
			}
			controller->get_file_dialog()->set_title(TTRC("New Text File..."));
			controller->get_file_dialog()->popup_file_dialog();
		} break;
		case FILE_MENU_OPEN: {
			controller->get_file_dialog()->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
			controller->get_file_dialog()->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
			controller->set_file_dialog_option(FILE_MENU_OPEN);

			List<String> extensions;
			ResourceLoader::get_recognized_extensions_for_type("Script", &extensions);
			controller->get_file_dialog()->clear_filters();
			for (const String &extension : extensions) {
				controller->get_file_dialog()->add_filter("*." + extension, extension.to_upper());
			}

			for (const String &E : textfile_extensions) {
				controller->get_file_dialog()->add_filter("*." + E, E.to_upper());
			}

			controller->get_file_dialog()->set_title(TTRC("Open File"));
			controller->get_file_dialog()->popup_file_dialog();
			return;
		} break;
		case FILE_MENU_REOPEN_CLOSED: {
			if (controller->get_previous_scripts().is_empty()) {
				return;
			}

			String path = controller->get_previous_scripts().back()->get();
			controller->get_previous_scripts().pop_back();

			List<String> extensions;
			ResourceLoader::get_recognized_extensions_for_type("Script", &extensions);
			ResourceLoader::get_recognized_extensions_for_type("JSON", &extensions);
			bool built_in = !path.is_resource_file();

			if (extensions.find(path.get_extension()) || built_in) {
				if (built_in) {
					String res_path = path.get_slice("::", 0);
					EditorNode::get_singleton()->load_scene_or_resource(res_path, false, false);
				}

				Ref<Resource> scr = ResourceLoader::load(path);
				if (scr.is_null()) {
					EditorNode::get_singleton()->show_warning(TTR("Could not load file at:") + "\n\n" + path, TTR("Error!"));
					controller->set_file_dialog_option(-1);
					return;
				}

				edit(scr);
				controller->set_file_dialog_option(-1);
			} else {
				Error error;
				Ref<TextFile> text_file = _load_text_file(path, &error);
				if (error != OK) {
					EditorNode::get_singleton()->show_warning(TTR("Could not load file at:") + "\n\n" + path, TTR("Error!"));
				}

				if (text_file.is_valid()) {
					edit(text_file);
					controller->set_file_dialog_option(-1);
				}
			}
		} break;
		case FILE_MENU_SAVE_ALL: {
			if (_test_script_times_on_disk()) {
				return;
			}

			controller->save_all_scripts();
		} break;
		case SEARCH_IN_FILES: {
			controller->open_find_in_files_dialog("");
		} break;
		case REPLACE_IN_FILES: {
			controller->open_replace_in_files_dialog("");
		} break;
		case SEARCH_HELP: {
			controller->get_help_search_dialog()->popup_dialog();
		} break;
		case SEARCH_WEBSITE: {
			Control *tab = tab_container->get_current_tab_control();

			EditorHelp *eh = Object::cast_to<EditorHelp>(tab);
			bool native_class_doc = false;
			if (eh) {
				const HashMap<String, DocData::ClassDoc>::ConstIterator E = EditorHelp::get_doc_data()->class_list.find(eh->get_class());
				native_class_doc = E && !E->value.is_script_doc;
			}
			if (native_class_doc) {
				String name = eh->get_class().to_lower();
				String doc_url = vformat(FOUNDRY_VERSION_DOCS_URL "/classes/class_%s.html", name);
				OS::get_singleton()->shell_open(doc_url);
			} else {
				OS::get_singleton()->shell_open(FOUNDRY_VERSION_DOCS_URL "/");
			}
		} break;
		case FILE_MENU_HISTORY_NEXT: {
			_history_forward();
		} break;
		case FILE_MENU_HISTORY_PREV: {
			_history_back();
		} break;
		case FILE_MENU_SORT: {
			_sort_list_on_update = true;
			_update_script_names();
		} break;
		case FILE_MENU_TOGGLE_FILES_PANEL: {
			toggle_files_panel();
			if (current) {
				current->update_toggle_files_button();
			} else {
				Control *tab = tab_container->get_current_tab_control();
				EditorHelp *editor_help = Object::cast_to<EditorHelp>(tab);
				if (editor_help) {
					editor_help->update_toggle_files_button();
				}
			}
		}
	}

	if (p_option >= EditorContextMenuPlugin::BASE_ID) {
		Ref<Resource> resource;
		if (current) {
			resource = current->get_edited_resource();
		}
		EditorContextMenuPluginManager::get_singleton()->activate_custom_option(EditorContextMenuPlugin::CONTEXT_SLOT_SCRIPT_EDITOR, p_option, resource);
		return;
	}

	if (current) {
		switch (p_option) {
			case FILE_MENU_SAVE: {
				save_current_script();
			} break;
			case FILE_MENU_SAVE_AS: {
				if (controller->get_trim_trailing_whitespace_on_save()) {
					current->trim_trailing_whitespace();
				}

				if (controller->get_trim_final_newlines_on_save()) {
					current->trim_final_newlines();
				}

				if (controller->get_convert_indent_on_save()) {
					current->convert_indent();
				}

				if (controller->get_format_on_save()) {
					current->format_document(false);
				}

				Ref<Resource> resource = current->get_edited_resource();
				Ref<TextFile> text_file = resource;
				Ref<Script> scr = resource;

				if (text_file.is_valid()) {
					controller->get_file_dialog()->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
					controller->get_file_dialog()->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
					controller->set_file_dialog_option(FILE_MENU_SAVE_AS);

					List<String> extensions;
					ResourceLoader::get_recognized_extensions_for_type("Script", &extensions);
					controller->get_file_dialog()->clear_filters();
					controller->get_file_dialog()->set_current_dir(text_file->get_path().get_base_dir());
					controller->get_file_dialog()->set_current_file(text_file->get_path().get_file());
					controller->get_file_dialog()->set_title(TTRC("Save File As..."));
					controller->get_file_dialog()->popup_file_dialog();
					break;
				}

				if (scr.is_valid()) {
					clear_docs_from_script(scr);
				}

				EditorNode::get_singleton()->push_item(resource.ptr());
				EditorNode::get_singleton()->save_resource_as(resource);

				if (scr.is_valid()) {
					update_docs_from_script(scr);
				}
			} break;

			case FILE_MENU_SOFT_RELOAD_TOOL: {
				Ref<Script> scr = current->get_edited_resource();
				if (scr.is_null()) {
					EditorNode::get_singleton()->show_warning(TTR("Can't obtain the script for reloading."));
					break;
				}
				if (!scr->is_tool()) {
					EditorNode::get_singleton()->show_warning(TTR("Reload only takes effect on tool scripts."));
					return;
				}
				scr->reload(true);

			} break;

			case FILE_MENU_RUN: {
				Ref<Script> scr = current->get_edited_resource();
				if (scr.is_null()) {
					EditorToaster::get_singleton()->popup_str(TTR("Cannot run the edited file because it's not a script."), EditorToaster::SEVERITY_WARNING);
					break;
				}

				current->apply_code();

				EditorNode::get_singleton()->run_editor_script(scr);
			} break;

			case FILE_MENU_CLOSE: {
				if (current->is_unsaved()) {
					_ask_close_current_unsaved_tab(current);
				} else {
					_close_current_tab(false);
				}
			} break;
			case FILE_MENU_COPY_PATH: {
				_copy_script_path();
			} break;
			case FILE_MENU_COPY_UID: {
				_copy_script_uid();
			} break;
			case FILE_MENU_SHOW_IN_FILE_SYSTEM: {
				const Ref<Resource> scr = current->get_edited_resource();
				String path = scr->get_path();
				if (!path.is_empty()) {
					if (scr->is_built_in()) {
						path = path.get_slice("::", 0); // Show the scene instead.
					}

					FileSystemDock::get_singleton()->navigate_to_path(path);
				}
			} break;
			case FILE_MENU_CLOSE_DOCS: {
				_close_docs_tab();
			} break;
			case FILE_MENU_CLOSE_OTHER_TABS: {
				_close_other_tabs();
			} break;
			case FILE_MENU_CLOSE_TABS_BELOW: {
				_close_tabs_below();
			} break;
			case FILE_MENU_CLOSE_ALL: {
				_close_all_tabs();
			} break;
			case FILE_MENU_MOVE_UP: {
				if (tab_container->get_current_tab() > 0) {
					tab_container->move_child(current, tab_container->get_current_tab() - 1);
					tab_container->set_current_tab(tab_container->get_current_tab());
					_update_script_names();
				}
			} break;
			case FILE_MENU_MOVE_DOWN: {
				if (tab_container->get_current_tab() < tab_container->get_tab_count() - 1) {
					tab_container->move_child(current, tab_container->get_current_tab() + 1);
					tab_container->set_current_tab(tab_container->get_current_tab());
					_update_script_names();
				}
			} break;
		}
	} else {
		EditorHelp *help = Object::cast_to<EditorHelp>(tab_container->get_current_tab_control());
		if (help) {
			switch (p_option) {
				case HELP_SEARCH_FIND: {
					help->popup_search();
				} break;
				case HELP_SEARCH_FIND_NEXT: {
					help->search_again();
				} break;
				case HELP_SEARCH_FIND_PREVIOUS: {
					help->search_again(true);
				} break;
				case FILE_MENU_CLOSE: {
					_close_current_tab();
				} break;
				case FILE_MENU_CLOSE_DOCS: {
					_close_docs_tab();
				} break;
				case FILE_MENU_CLOSE_OTHER_TABS: {
					_close_other_tabs();
				} break;
				case FILE_MENU_CLOSE_TABS_BELOW: {
					_close_tabs_below();
				} break;
				case FILE_MENU_CLOSE_ALL: {
					_close_all_tabs();
				} break;
				case FILE_MENU_MOVE_UP: {
					if (tab_container->get_current_tab() > 0) {
						tab_container->move_child(help, tab_container->get_current_tab() - 1);
						tab_container->set_current_tab(tab_container->get_current_tab());
						_update_script_names();
					}
				} break;
				case FILE_MENU_MOVE_DOWN: {
					if (tab_container->get_current_tab() < tab_container->get_tab_count() - 1) {
						tab_container->move_child(help, tab_container->get_current_tab() + 1);
						tab_container->set_current_tab(tab_container->get_current_tab());
						_update_script_names();
					}
				} break;
			}
		}
	}
}

void ScriptEditorView::_theme_option(int p_option) {
	switch (p_option) {
		case THEME_IMPORT: {
			controller->get_file_dialog()->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
			controller->get_file_dialog()->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
			controller->set_file_dialog_option(THEME_IMPORT);
			controller->get_file_dialog()->clear_filters();
			controller->get_file_dialog()->add_filter("*.tet");
			controller->get_file_dialog()->set_title(TTRC("Import Theme"));
			controller->get_file_dialog()->popup_file_dialog();
		} break;
		case THEME_RELOAD: {
			EditorSettings::get_singleton()->mark_setting_changed("text_editor/theme/color_theme");
			EditorSettings::get_singleton()->notify_changes();
		} break;
		case THEME_SAVE_AS: {
			ScriptEditorView::_show_save_theme_as_dialog();
		} break;
	}
}

void ScriptEditorView::_show_save_theme_as_dialog() {
	controller->get_file_dialog()->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
	controller->get_file_dialog()->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	controller->set_file_dialog_option(THEME_SAVE_AS);
	controller->get_file_dialog()->clear_filters();
	controller->get_file_dialog()->add_filter("*.tet");
	controller->get_file_dialog()->set_current_path(EditorPaths::get_singleton()->get_text_editor_themes_dir().path_join(EDITOR_GET("text_editor/theme/color_theme")) + " New");
	controller->get_file_dialog()->set_title(TTRC("Save Theme As..."));
	controller->get_file_dialog()->popup_file_dialog();
}

bool ScriptEditorView::_has_docs_tab() const {
	const int child_count = tab_container->get_tab_count();
	for (int i = 0; i < child_count; i++) {
		if (Object::cast_to<EditorHelp>(tab_container->get_tab_control(i))) {
			return true;
		}
	}
	return false;
}

bool ScriptEditorView::_has_script_tab() const {
	const int child_count = tab_container->get_tab_count();
	for (int i = 0; i < child_count; i++) {
		if (Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i))) {
			return true;
		}
	}
	return false;
}

void ScriptEditorView::_prepare_file_menu() {
	PopupMenu *menu = file_menu->get_popup();
	ScriptEditorBase *editor = _get_current_editor();
	const Ref<Resource> res = editor ? editor->get_edited_resource() : Ref<Resource>();

	menu->set_item_disabled(menu->get_item_index(FILE_MENU_REOPEN_CLOSED), controller->get_previous_scripts().is_empty());

	menu->set_item_disabled(menu->get_item_index(FILE_MENU_SAVE), res.is_null());
	menu->set_item_disabled(menu->get_item_index(FILE_MENU_SAVE_AS), res.is_null());
	menu->set_item_disabled(menu->get_item_index(FILE_MENU_SAVE_ALL), !_has_script_tab());

	menu->set_item_disabled(menu->get_item_index(FILE_MENU_SOFT_RELOAD_TOOL), res.is_null());
	menu->set_item_disabled(menu->get_item_index(FILE_MENU_COPY_PATH), res.is_null() || res->get_path().is_empty());
	menu->set_item_disabled(menu->get_item_index(FILE_MENU_COPY_UID), res.is_null() || ResourceLoader::get_resource_uid(res->get_path()) == ResourceUID::INVALID_ID);
	menu->set_item_disabled(menu->get_item_index(FILE_MENU_SHOW_IN_FILE_SYSTEM), res.is_null());

	menu->set_item_disabled(menu->get_item_index(FILE_MENU_HISTORY_PREV), history_pos <= 0);
	menu->set_item_disabled(menu->get_item_index(FILE_MENU_HISTORY_NEXT), history_pos >= history.size() - 1);

	menu->set_item_disabled(menu->get_item_index(FILE_MENU_CLOSE), tab_container->get_tab_count() < 1);
	menu->set_item_disabled(menu->get_item_index(FILE_MENU_CLOSE_ALL), tab_container->get_tab_count() < 1);
	menu->set_item_disabled(menu->get_item_index(FILE_MENU_CLOSE_OTHER_TABS), tab_container->get_tab_count() <= 1);
	menu->set_item_disabled(menu->get_item_index(FILE_MENU_CLOSE_TABS_BELOW), tab_container->get_current_tab() >= tab_container->get_tab_count() - 1);
	menu->set_item_disabled(menu->get_item_index(FILE_MENU_CLOSE_DOCS), !_has_docs_tab());

	menu->set_item_disabled(menu->get_item_index(FILE_MENU_RUN), res.is_null());
}

void ScriptEditorView::_file_menu_closed() {
	PopupMenu *menu = file_menu->get_popup();

	menu->set_item_disabled(menu->get_item_index(FILE_MENU_REOPEN_CLOSED), false);

	menu->set_item_disabled(menu->get_item_index(FILE_MENU_SAVE), false);
	menu->set_item_disabled(menu->get_item_index(FILE_MENU_SAVE_AS), false);
	menu->set_item_disabled(menu->get_item_index(FILE_MENU_SAVE_ALL), false);

	menu->set_item_disabled(menu->get_item_index(FILE_MENU_SOFT_RELOAD_TOOL), false);
	menu->set_item_disabled(menu->get_item_index(FILE_MENU_COPY_PATH), false);
	menu->set_item_disabled(menu->get_item_index(FILE_MENU_SHOW_IN_FILE_SYSTEM), false);

	menu->set_item_disabled(menu->get_item_index(FILE_MENU_HISTORY_PREV), false);
	menu->set_item_disabled(menu->get_item_index(FILE_MENU_HISTORY_NEXT), false);

	menu->set_item_disabled(menu->get_item_index(FILE_MENU_CLOSE), false);
	menu->set_item_disabled(menu->get_item_index(FILE_MENU_CLOSE_ALL), false);
	menu->set_item_disabled(menu->get_item_index(FILE_MENU_CLOSE_OTHER_TABS), false);
	menu->set_item_disabled(menu->get_item_index(FILE_MENU_CLOSE_DOCS), false);

	menu->set_item_disabled(menu->get_item_index(FILE_MENU_RUN), false);
}

void ScriptEditorView::_tab_changed(int p_which) {
	ensure_select_current();
	sync_script_leaf_path();
}

void ScriptEditorView::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (EditorRunBar *run_bar = EditorRunBar::get_singleton()) {
				run_bar->connect("stop_pressed", callable_mp(this, &ScriptEditorView::_editor_stop));
			}
			_apply_editor_settings();
			[[fallthrough]];
		}

		case NOTIFICATION_TRANSLATION_CHANGED: {
			_update_online_doc();
			if (make_floating && !make_floating->is_disabled()) {
				// Override default ScreenSelect tooltip if multi-window support is available.
				make_floating->set_tooltip_text(TTR("Make the script editor floating.") + "\n" + TTR("Right-click to open the screen selector."));
			}
			[[fallthrough]];
		}
		case NOTIFICATION_LAYOUT_DIRECTION_CHANGED:
		case NOTIFICATION_THEME_CHANGED: {
			tab_container->add_theme_style_override(SceneStringName(panel), get_theme_stylebox(SNAME("ScriptEditor"), EditorStringName(EditorStyles)));

			help_search->set_button_icon(get_editor_theme_icon(SNAME("HelpSearch")));
			site_search->set_button_icon(get_editor_theme_icon(SNAME("ExternalLink")));

			if (is_layout_rtl()) {
				script_forward->set_button_icon(get_editor_theme_icon(SNAME("Back")));
				script_back->set_button_icon(get_editor_theme_icon(SNAME("Forward")));
			} else {
				script_forward->set_button_icon(get_editor_theme_icon(SNAME("Forward")));
				script_back->set_button_icon(get_editor_theme_icon(SNAME("Back")));
			}

			members_overview_alphabeta_sort_button->set_button_icon(get_editor_theme_icon(SNAME("Sort")));

			filter_scripts->set_right_icon(get_editor_theme_icon(SNAME("Search")));
			filter_methods->set_right_icon(get_editor_theme_icon(SNAME("Search")));

			recent_scripts->reset_size();
			script_list->set_fixed_icon_size(Vector2i(1, 1) * get_theme_constant("class_icon_size", EditorStringName(Editor)));

			if (is_inside_tree()) {
				_update_script_names();
			}
		} break;

		case EditorSettings::NOTIFICATION_EDITOR_SETTINGS_CHANGED: {
			if (EditorThemeManager::is_generated_theme_outdated() ||
					EditorSettings::get_singleton()->check_changed_settings_in_group("interface/editor") ||
					EditorSettings::get_singleton()->check_changed_settings_in_group("text_editor") ||
					EditorSettings::get_singleton()->check_changed_settings_in_group("docks/filesystem")) {
				_apply_editor_settings();
			}
		} break;

		case NOTIFICATION_READY: {
			// Can't set own styles in NOTIFICATION_THEME_CHANGED, so for now this will do.
			add_theme_style_override(SceneStringName(panel), get_theme_stylebox(SNAME("ScriptEditorPanel"), EditorStringName(EditorStyles)));

			get_tree()->connect("tree_changed", callable_mp(this, &ScriptEditorView::_tree_changed));
			if (EditorNode *editor_node = EditorNode::get_singleton()) {
				editor_node->get_focused_inspector_dock()->connect("request_help", callable_mp(this, &ScriptEditorView::_help_class_open));
				editor_node->connect("request_help_search", callable_mp(this, &ScriptEditorView::_help_search));
				editor_node->connect("scene_closed", callable_mp(this, &ScriptEditorView::_close_builtin_scripts_from_scene));
				editor_node->connect("script_add_function_request", callable_mp(this, &ScriptEditorView::_add_callback));
				editor_node->connect("resource_saved", callable_mp(this, &ScriptEditorView::_res_saved_callback));
				editor_node->connect("scene_saved", callable_mp(this, &ScriptEditorView::_scene_saved_callback));
			}
			if (FileSystemDock *filesystem_dock = FileSystemDock::get_singleton()) {
				filesystem_dock->connect("files_moved", callable_mp(controller, &ScriptEditorController::_files_moved));
				filesystem_dock->connect("file_removed", callable_mp(controller, &ScriptEditorController::_file_removed));
			}
			script_list->connect(SceneStringName(item_selected), callable_mp(this, &ScriptEditorView::_script_selected));

			members_overview->connect(SceneStringName(item_selected), callable_mp(this, &ScriptEditorView::_members_overview_selected));
			help_overview->connect(SceneStringName(item_selected), callable_mp(this, &ScriptEditorView::_help_overview_selected));
			script_split->connect("dragged", callable_mp(this, &ScriptEditorView::_split_dragged));
			list_split->connect("dragged", callable_mp(this, &ScriptEditorView::_split_dragged));

			if (EditorFileSystem *editor_file_system = EditorFileSystem::get_singleton()) {
				editor_file_system->connect("filesystem_changed", callable_mp(controller, &ScriptEditorController::_filesystem_changed));
			}
#ifdef ANDROID_ENABLED
			set_process(true);
#endif
		} break;

#ifdef ANDROID_ENABLED
		case NOTIFICATION_VISIBILITY_CHANGED: {
			set_process(is_visible_in_tree());
		} break;

		case NOTIFICATION_PROCESS: {
			const int kb_height = DisplayServer::get_singleton()->virtual_keyboard_get_height();
			if (kb_height == last_kb_height) {
				break;
			}

			last_kb_height = kb_height;
			float spacer_height = 0.0f;
			const float status_bar_height = 28 * EDSCALE; // Magic number
			const bool kb_visible = kb_height > 0;

			if (kb_visible) {
				if (ScriptEditorBase *editor = _get_current_editor()) {
					if (CodeTextEditor *code_editor = editor->get_code_editor()) {
						if (CodeEdit *text_editor = code_editor->get_text_editor()) {
							if (!text_editor->has_focus()) {
								break;
							}
							text_editor->adjust_viewport_to_caret();
						}
					}
				}

				const float control_bottom = get_global_position().y + get_size().y;
				const float extra_bottom = get_viewport_rect().size.y - control_bottom;
				spacer_height = float(kb_height) - extra_bottom - status_bar_height;

				if (spacer_height < 0.0f) {
					spacer_height = 0.0f;
				}
			}

			virtual_keyboard_spacer->set_custom_minimum_size(Size2(0, spacer_height));
			EditorSceneTabs::get_singleton()->set_visible(!kb_height);
			menu_hb->set_visible(!kb_visible);
		} break;
#endif

		case NOTIFICATION_EXIT_TREE: {
			if (EditorRunBar *run_bar = EditorRunBar::get_singleton()) {
				run_bar->disconnect("stop_pressed", callable_mp(this, &ScriptEditorView::_editor_stop));
			}
		} break;

		case NOTIFICATION_PREDELETE: {
			if (controller) {
				controller->unregister_view(this);
			}
		} break;

		case NOTIFICATION_APPLICATION_FOCUS_IN: {
			if (is_inside_tree()) {
				_test_script_times_on_disk();
				_update_modified_scripts_for_external_editor();
			}
		} break;
	}
}

void ScriptEditorView::_close_builtin_scripts_from_scene(const String &p_scene) {
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));

		if (se) {
			Ref<Script> scr = se->get_edited_resource();
			if (scr.is_null()) {
				continue;
			}

			if (scr->is_built_in() && scr->get_path().begins_with(p_scene)) { // Is an internal script and belongs to scene being closed.
				_close_tab(i, false);
				i--;
			}
		}
	}
}

void ScriptEditorView::edited_scene_changed() {
	_update_modified_scripts_for_external_editor();
}


void ScriptEditorView::_members_overview_selected(int p_idx) {
	int line = members_overview->get_item_metadata(p_idx);
	ScriptEditorBase *current = _get_current_editor();
	if (ScriptTextEditor *script_text_editor = Object::cast_to<ScriptTextEditor>(current)) {
		script_text_editor->goto_line_centered(line);
	} else if (current) {
		current->goto_line(line);
	}
}

void ScriptEditorView::_help_overview_selected(int p_idx) {
	Node *current = tab_container->get_tab_control(tab_container->get_current_tab());
	EditorHelp *se = Object::cast_to<EditorHelp>(current);
	if (!se) {
		return;
	}
	se->scroll_to_section(help_overview->get_item_metadata(p_idx));
}

void ScriptEditorView::_script_selected(int p_idx) {
	grab_focus_block = !Input::get_singleton()->is_mouse_button_pressed(MouseButton::LEFT); //amazing hack, simply amazing

	_go_to_tab(script_list->get_item_metadata(p_idx));
	script_name_label->set_text(script_list->get_item_text(p_idx));
	grab_focus_block = false;
}

void ScriptEditorView::ensure_select_current() {
	if (tab_container->get_tab_count() && tab_container->get_current_tab() >= 0) {
		ScriptEditorBase *se = _get_current_editor();
		if (se) {
			se->enable_editor(this);

			if (!grab_focus_block && is_visible_in_tree()) {
				se->ensure_focus();
			}
		}
	}
	_update_find_replace_bar();

	_update_selected_editor_menu();
}

bool ScriptEditorView::is_editor_floating() {
	return is_floating;
}

void ScriptEditorView::_find_scripts(Node *p_base, Node *p_current, HashSet<Ref<Script>> &used) {
	if (p_current != p_base && p_current->get_owner() != p_base) {
		return;
	}

	if (p_current->get_script_instance()) {
		Ref<Script> scr = p_current->get_script();
		if (scr.is_valid()) {
			used.insert(scr);
		}
	}

	for (int i = 0; i < p_current->get_child_count(); i++) {
		_find_scripts(p_base, p_current->get_child(i), used);
	}
}

struct _ScriptEditorItemData {
	String name;
	String sort_key;
	Ref<Texture2D> icon;
	bool tool = false;
	int index = 0;
	String tooltip;
	bool used = false;
	int category = 0;
	Node *ref = nullptr;

	bool operator<(const _ScriptEditorItemData &id) const {
		if (category == id.category) {
			if (sort_key == id.sort_key) {
				return index < id.index;
			} else {
				return sort_key.filenocasecmp_to(id.sort_key) < 0;
			}
		} else {
			return category < id.category;
		}
	}
};

void ScriptEditorView::_update_members_overview_visibility() {
	ScriptEditorBase *se = _get_current_editor();
	if (!se) {
		members_overview_alphabeta_sort_button->set_visible(false);
		members_overview->set_visible(false);

		Node *current = tab_container->get_tab_control(tab_container->get_current_tab());
		EditorHelp *editor_help = Object::cast_to<EditorHelp>(current);
		overview_vbox->set_visible(help_overview_enabled && editor_help);
		return;
	}

	if (members_overview_enabled && se->show_members_overview()) {
		members_overview_alphabeta_sort_button->set_visible(true);
		filter_methods->set_visible(true);
		members_overview->set_visible(true);
		overview_vbox->set_visible(true);
	} else {
		members_overview_alphabeta_sort_button->set_visible(false);
		filter_methods->set_visible(false);
		members_overview->set_visible(false);
		overview_vbox->set_visible(false);
	}
}

void ScriptEditorView::_toggle_members_overview_alpha_sort(bool p_alphabetic_sort) {
	EditorSettings::get_singleton()->set("text_editor/script_list/sort_members_outline_alphabetically", p_alphabetic_sort);
	_update_members_overview();
}

void ScriptEditorView::_update_members_overview() {
	members_overview->clear();

	ScriptEditorBase *se = _get_current_editor();
	if (!se) {
		return;
	}

	Vector<String> functions = se->get_functions();
	if (EDITOR_GET("text_editor/script_list/sort_members_outline_alphabetically")) {
		functions.sort();
	}

	String filter = filter_methods->get_text();
	if (filter.is_empty()) {
		for (int i = 0; i < functions.size(); i++) {
			String name = functions[i].get_slicec(':', 0);
			members_overview->add_item(name);
			members_overview->set_item_metadata(-1, functions[i].get_slicec(':', 1).to_int() - 1);
		}
	} else {
		PackedStringArray search_names;
		for (int i = 0; i < functions.size(); i++) {
			search_names.append(functions[i].get_slicec(':', 0));
		}

		Vector<FuzzySearchResult> results;
		FuzzySearch fuzzy;
		fuzzy.set_query(filter, false);
		fuzzy.search_all(search_names, results);

		for (const FuzzySearchResult &res : results) {
			String name = functions[res.original_index].get_slicec(':', 0);
			int line = functions[res.original_index].get_slicec(':', 1).to_int() - 1;
			members_overview->add_item(name);
			members_overview->set_item_metadata(-1, line);
		}
	}
}

void ScriptEditorView::_update_help_overview_visibility() {
	int selected = tab_container->get_current_tab();
	if (selected < 0 || selected >= tab_container->get_tab_count()) {
		help_overview->set_visible(false);
		return;
	}

	Node *current = tab_container->get_tab_control(tab_container->get_current_tab());
	EditorHelp *se = Object::cast_to<EditorHelp>(current);
	if (!se) {
		help_overview->set_visible(false);
		return;
	}

	if (help_overview_enabled) {
		members_overview_alphabeta_sort_button->set_visible(false);
		filter_methods->set_visible(false);
		help_overview->set_visible(true);
		overview_vbox->set_visible(true);
	} else {
		help_overview->set_visible(false);
		overview_vbox->set_visible(false);
	}
}

void ScriptEditorView::_update_help_overview() {
	help_overview->clear();

	int selected = tab_container->get_current_tab();
	if (selected < 0 || selected >= tab_container->get_tab_count()) {
		return;
	}

	Node *current = tab_container->get_tab_control(tab_container->get_current_tab());
	EditorHelp *se = Object::cast_to<EditorHelp>(current);
	if (!se) {
		return;
	}

	Vector<Pair<String, int>> sections = se->get_sections();
	for (int i = 0; i < sections.size(); i++) {
		help_overview->add_item(sections[i].first);
		help_overview->set_item_metadata(i, sections[i].second);
	}
}

void ScriptEditorView::_update_online_doc() {
	Node *current = tab_container->get_tab_control(tab_container->get_current_tab());

	EditorHelp *eh = Object::cast_to<EditorHelp>(current);
	bool native_class_doc = false;
	if (eh) {
		const HashMap<String, DocData::ClassDoc>::ConstIterator E = EditorHelp::get_doc_data()->class_list.find(eh->get_class());
		native_class_doc = E && !E->value.is_script_doc;
	}
	if (native_class_doc) {
		String name = eh->get_class();
		String tooltip = vformat(TTR("Open '%s' in Foundry online documentation."), name);
		site_search->set_text(TTRC("Open in Online Docs"));
		site_search->set_tooltip_text(tooltip);
	} else {
		site_search->set_text(TTRC("Online Docs"));
		site_search->set_tooltip_text(TTRC("Open Foundry online documentation."));
	}
}

void ScriptEditorView::_update_script_colors() {
	bool script_temperature_enabled = EDITOR_GET("text_editor/script_list/script_temperature_enabled");

	int hist_size = EDITOR_GET("text_editor/script_list/script_temperature_history_size");
	Color hot_color = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	hot_color.set_s(hot_color.get_s() * 0.9);
	Color cold_color = get_theme_color(SceneStringName(font_color), EditorStringName(Editor));

	for (int i = 0; i < script_list->get_item_count(); i++) {
		int c = script_list->get_item_metadata(i);
		Node *n = tab_container->get_tab_control(c);
		if (!n) {
			continue;
		}

		if (script_temperature_enabled) {
			int pass = n->get_meta("__editor_pass", -1);
			if (pass < 0) {
				continue;
			}

			int h = edit_pass - pass;
			if (h > hist_size) {
				continue;
			}
			int non_zero_hist_size = (hist_size == 0) ? 1 : hist_size;
			float v = Math::ease((edit_pass - pass) / float(non_zero_hist_size), 0.4);

			script_list->set_item_custom_fg_color(i, hot_color.lerp(cold_color, v));
		}
	}
}

void ScriptEditorView::_update_script_names() {
	if (restoring_layout) {
		return;
	}

	HashSet<Ref<Script>> used;
	Node *edited = nullptr;
	if (EditorNode *editor_node = EditorNode::get_singleton()) {
		edited = editor_node->get_edited_scene();
	}
	if (edited && EDITOR_GET("text_editor/script_list/highlight_scene_scripts")) {
		_find_scripts(edited, edited, used);
	}

	script_list->clear();
	bool split_script_help = EDITOR_GET("text_editor/script_list/group_help_pages");
	ScriptSortBy sort_by = (ScriptSortBy)(int)EDITOR_GET("text_editor/script_list/sort_scripts_by");
	ScriptListName display_as = (ScriptListName)(int)EDITOR_GET("text_editor/script_list/list_script_names_as");

	Vector<_ScriptEditorItemData> sedata;

	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));
		if (se) {
			Ref<Texture2D> icon = se->get_theme_icon();
			String path = se->get_edited_resource()->get_path();
			bool saved = !path.is_empty();
			String name = se->get_name();
			Ref<Script> scr = se->get_edited_resource();

			_ScriptEditorItemData sd;
			sd.icon = icon;
			sd.name = name;
			sd.tooltip = saved ? path : TTR("Unsaved file.");
			sd.index = i;
			sd.used = used.has(se->get_edited_resource());
			sd.category = 0;
			sd.ref = se;
			if (scr.is_valid()) {
				sd.tool = scr->is_tool();
			}

			switch (sort_by) {
				case SORT_BY_NAME: {
					sd.sort_key = name.to_lower();
				} break;
				case SORT_BY_PATH: {
					sd.sort_key = path;
				} break;
				case SORT_BY_NONE: {
					sd.sort_key = "";
				} break;
			}

			switch (display_as) {
				case DISPLAY_NAME: {
					sd.name = name;
				} break;
				case DISPLAY_DIR_AND_NAME: {
					if (!path.get_base_dir().get_file().is_empty()) {
						sd.name = path.get_base_dir().get_file().path_join(name);
					} else {
						sd.name = name;
					}
				} break;
				case DISPLAY_FULL_PATH: {
					sd.name = path;
				} break;
			}
			if (!saved) {
				sd.name = se->get_name();
			}

			sedata.push_back(sd);
		}

		EditorHelp *eh = Object::cast_to<EditorHelp>(tab_container->get_tab_control(i));
		if (eh && !eh->get_class().is_empty()) {
			String name = eh->get_class().unquote();
			Ref<Texture2D> icon = get_editor_theme_icon(SNAME("Help"));
			String tooltip = vformat(TTR("%s Class Reference"), name);

			_ScriptEditorItemData sd;
			sd.icon = icon;
			sd.name = name;
			sd.sort_key = name.to_lower();
			sd.tooltip = tooltip;
			sd.index = i;
			sd.used = false;
			sd.category = split_script_help ? 1 : 0;
			sd.ref = eh;

			sedata.push_back(sd);
		}
	}

	Vector<String> disambiguated_script_names;
	Vector<String> full_script_paths;
	for (int j = 0; j < sedata.size(); j++) {
		String name = sedata[j].name.replace("(*)", "");
		ScriptListName script_display = (ScriptListName)(int)EDITOR_GET("text_editor/script_list/list_script_names_as");
		switch (script_display) {
			case DISPLAY_NAME: {
				name = name.get_file();
			} break;
			case DISPLAY_DIR_AND_NAME: {
				name = name.get_base_dir().get_file().path_join(name.get_file());
			} break;
			default:
				break;
		}

		disambiguated_script_names.append(name);
		full_script_paths.append(sedata[j].tooltip);
	}

	EditorNode::disambiguate_filenames(full_script_paths, disambiguated_script_names);

	for (int j = 0; j < sedata.size(); j++) {
		if (sedata[j].name.ends_with("(*)")) {
			sedata.write[j].name = disambiguated_script_names[j] + "(*)";
		} else {
			sedata.write[j].name = disambiguated_script_names[j];
		}
	}

	if (_sort_list_on_update && !sedata.is_empty()) {
		sedata.sort();

		// change actual order of tab_container so that the order can be rearranged by user
		int cur_tab = tab_container->get_current_tab();
		int prev_tab = tab_container->get_previous_tab();
		int new_cur_tab = -1;
		int new_prev_tab = -1;
		for (int i = 0; i < sedata.size(); i++) {
			tab_container->move_child(sedata[i].ref, i);
			if (new_prev_tab == -1 && sedata[i].index == prev_tab) {
				new_prev_tab = i;
			}
			if (new_cur_tab == -1 && sedata[i].index == cur_tab) {
				new_cur_tab = i;
			}
			// Update index of sd entries for sorted order
			_ScriptEditorItemData sd = sedata[i];
			sd.index = i;
			sedata.set(i, sd);
		}

		lock_history = true;
		_go_to_tab(new_prev_tab);
		_go_to_tab(new_cur_tab);
		lock_history = false;
		_sort_list_on_update = false;
	}

	Vector<_ScriptEditorItemData> sedata_filtered;

	String filter = filter_scripts->get_text();

	if (filter.is_empty()) {
		sedata_filtered = sedata;
	} else {
		PackedStringArray search_names;
		for (int i = 0; i < sedata.size(); i++) {
			search_names.append(sedata[i].name);
		}

		Vector<FuzzySearchResult> results;
		FuzzySearch fuzzy;
		fuzzy.set_query(filter, false);
		fuzzy.search_all(search_names, results);

		for (const FuzzySearchResult &res : results) {
			sedata_filtered.push_back(sedata[res.original_index]);
		}
	}

	Color tool_color = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	tool_color.set_s(tool_color.get_s() * 1.5);
	for (int i = 0; i < sedata_filtered.size(); i++) {
		script_list->add_item(sedata_filtered[i].name, sedata_filtered[i].icon);
		if (sedata_filtered[i].tool) {
			script_list->set_item_icon_modulate(-1, tool_color);
		}

		int index = script_list->get_item_count() - 1;
		script_list->set_item_tooltip(index, sedata_filtered[i].tooltip);
		script_list->set_item_metadata(index, sedata_filtered[i].index); /* Saving as metadata the script's index in the tab container and not the filtered one */
		if (sedata_filtered[i].used) {
			script_list->set_item_custom_bg_color(index, Color(.5, .5, .5, .125));
		}
		if (tab_container->get_current_tab() == sedata_filtered[i].index) {
			script_list->select(index);

			script_name_label->set_text(sedata_filtered[i].name);

			ScriptEditorBase *se = _get_current_editor();
			if (se) {
				se->enable_editor(this);
				_update_selected_editor_menu();
			}
		}
	}

	if (!waiting_update_names) {
		_update_members_overview();
		_update_help_overview();
	} else {
		waiting_update_names = false;
	}
	_update_members_overview_visibility();
	_update_help_overview_visibility();
	_update_script_colors();
}

Ref<TextFile> ScriptEditorView::_load_text_file(const String &p_path, Error *r_error) const {
	if (r_error) {
		*r_error = ERR_FILE_CANT_OPEN;
	}

	String local_path = ProjectSettings::get_singleton()->localize_path(p_path);
	String path = ResourceLoader::path_remap(local_path);

	TextFile *text_file = memnew(TextFile);
	Ref<TextFile> text_res(text_file);
	Error err = text_file->load_text(path);

	ERR_FAIL_COND_V_MSG(err != OK, Ref<Resource>(), "Cannot load text file '" + path + "'.");

	text_file->set_file_path(local_path);
	text_file->set_path(local_path, true);

	if (ResourceLoader::get_timestamp_on_load()) {
		text_file->set_last_modified_time(FileAccess::get_modified_time(path));
	}

	if (r_error) {
		*r_error = OK;
	}

	return text_res;
}

Error ScriptEditorView::_save_text_file(Ref<TextFile> p_text_file, const String &p_path) {
	Ref<TextFile> sqscr = p_text_file;
	ERR_FAIL_COND_V(sqscr.is_null(), ERR_INVALID_PARAMETER);

	warn_if_saving_declared_build_output(p_path);

	String source = sqscr->get_text();

	Error err;
	{
		Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);

		ERR_FAIL_COND_V_MSG(err, err, "Cannot save text file '" + p_path + "'.");

		file->store_string(source);
		if (file->get_error() != OK && file->get_error() != ERR_FILE_EOF) {
			return ERR_CANT_CREATE;
		}
	}

	if (ResourceSaver::get_timestamp_on_save()) {
		p_text_file->set_last_modified_time(FileAccess::get_modified_time(p_path));
	}

	if (EditorFileSystem *editor_file_system = EditorFileSystem::get_singleton()) {
		editor_file_system->update_file(p_path);
	}

	_res_saved_callback(sqscr);
	return OK;
}

bool ScriptEditorView::edit(const Ref<Resource> &p_resource, int p_line, int p_col, bool p_grab_focus) {
	if (p_resource.is_null()) {
		return false;
	}

	Ref<Script> scr = p_resource;

	// Don't open dominant script if using an external editor.
	bool use_external_editor =
			controller->is_external_editor_active() ||
			(scr.is_valid() && scr->get_language()->overrides_external_editor());
	use_external_editor = use_external_editor && !(scr.is_valid() && scr->is_built_in()); // Ignore external editor for built-in scripts.
	const bool open_dominant = EDITOR_GET("text_editor/behavior/files/open_dominant_script_on_scene_change");

	EditorNode *editor_node = EditorNode::get_singleton();
	const bool should_open = (open_dominant && !use_external_editor) || !editor_node || !editor_node->is_changing_scene();

	if (scr.is_valid() && scr->get_language()->overrides_external_editor()) {
		if (should_open) {
			Error err = scr->get_language()->open_in_external_editor(scr, p_line >= 0 ? p_line : 0, p_col);
			if (err != OK) {
				ERR_PRINT("Couldn't open script in the overridden external text editor");
			}
		}
		return false;
	}

	if (use_external_editor &&
			(EditorDebuggerNode::get_singleton()->get_dump_stack_script() != p_resource || EditorDebuggerNode::get_singleton()->get_debug_with_external_editor()) &&
			p_resource->get_path().is_resource_file()) {
		if (ScriptEditorPlugin::open_in_external_editor(ProjectSettings::get_singleton()->globalize_path(p_resource->get_path()), p_line, p_col)) {
			return false;
		} else {
			ERR_PRINT("Couldn't open external text editor, falling back to the internal editor. Review your `text_editor/external/` editor settings.");
		}
	}

	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));
		if (!se) {
			continue;
		}

		if ((scr.is_valid() && se->get_edited_resource() == p_resource) || se->get_edited_resource()->get_path() == p_resource->get_path()) {
			if (should_open) {
				se->enable_editor(this);

				if (tab_container->get_current_tab() != i) {
					_go_to_tab(i);
				}
				if (is_visible_in_tree()) {
					se->ensure_focus();
				}

				if (p_line >= 0) {
					se->goto_line(p_line, p_col);
				}
			}
			_update_script_names();
			script_list->ensure_current_is_visible();
			return true;
		}
	}

	// doesn't have it, make a new one

	ScriptEditorBase *se = nullptr;

	for (int i = controller->get_script_editor_func_count() - 1; i >= 0; i--) {
		se = controller->get_script_editor_func(i)(p_resource);
		if (se) {
			break;
		}
	}
	ERR_FAIL_NULL_V(se, false);

	se->set_edited_resource(p_resource);

	// Syntax highlighting.
	bool highlighter_set = false;
	for (int i = 0; i < controller->get_syntax_highlighters().size(); i++) {
		Ref<EditorSyntaxHighlighter> highlighter = controller->get_syntax_highlighters()[i]->_create();
		if (highlighter.is_null()) {
			continue;
		}
		se->add_syntax_highlighter(highlighter);

		if (highlighter_set) {
			continue;
		}

		PackedStringArray languages = highlighter->_get_supported_languages();
		// If script try language, else use extension.
		if (scr.is_valid()) {
			if (languages.has(scr->get_language()->get_name())) {
				se->set_syntax_highlighter(highlighter);
				highlighter_set = true;
			}
			continue;
		}

		if (languages.has(p_resource->get_path().get_extension())) {
			se->set_syntax_highlighter(highlighter);
			highlighter_set = true;
		}
	}

	tab_container->add_child(se);

	if (p_grab_focus) {
		se->enable_editor(this);
	}

	// If we delete a script within the filesystem, the original resource path
	// is lost, so keep it as `edited_file_data` to figure out the exact tab to delete.
	se->edited_file_data.path = p_resource->get_path();
	se->edited_file_data.last_modified_time = FileAccess::get_modified_time(p_resource->get_path());

	se->set_tooltip_request_func(callable_mp(this, &ScriptEditorView::_get_debug_tooltip));

	if (se->get_edit_menu()) {
		se->get_edit_menu()->hide();
		menu_hb->add_child(se->get_edit_menu());
		menu_hb->move_child(se->get_edit_menu(), 1);
	}

	if (p_grab_focus) {
		_go_to_tab(tab_container->get_tab_count() - 1);
		controller->add_recent_script(p_resource->get_path());
	}

	if (controller->get_script_editor_cache()->has_section(p_resource->get_path())) {
		se->set_edit_state(controller->get_script_editor_cache()->get_value(p_resource->get_path(), "state"));
		ScriptTextEditor *ste = Object::cast_to<ScriptTextEditor>(se);
		if (ste) {
			ste->store_previous_state();
		}
	}

	_sort_list_on_update = true;
	_update_script_names();
	_save_layout();
	se->connect("name_changed", callable_mp(this, &ScriptEditorView::_update_script_names));
	se->connect("edited_script_changed", callable_mp(this, &ScriptEditorView::_script_changed));
	se->connect("request_help", callable_mp(this, &ScriptEditorView::_help_search));
	se->connect("request_open_script_at_line", callable_mp(controller, &ScriptEditorController::_goto_script_line));
	se->connect("go_to_help", callable_mp(this, &ScriptEditorView::_help_class_goto));
	se->connect("request_save_history", callable_mp(this, &ScriptEditorView::_save_history));
	se->connect("request_save_previous_state", callable_mp(this, &ScriptEditorView::_save_previous_state));
	se->connect("search_in_files_requested", callable_mp(controller, &ScriptEditorController::open_find_in_files_dialog));
	se->connect("replace_in_files_requested", callable_mp(controller, &ScriptEditorController::_on_replace_in_files_requested));
	se->connect("go_to_method", callable_mp(this, &ScriptEditorView::script_goto_method));

	CodeTextEditor *cte = se->get_code_editor();
	if (cte) {
		cte->set_zoom_factor(zoom_factor);
		cte->connect("zoomed", callable_mp(this, &ScriptEditorView::_set_script_zoom_factor));
		cte->connect(SceneStringName(visibility_changed), callable_mp(this, &ScriptEditorView::_update_code_editor_zoom_factor).bind(cte));
	}

	//test for modification, maybe the script was not edited but was loaded

	_test_script_times_on_disk(p_resource);
	_update_modified_scripts_for_external_editor(p_resource);

	if (p_line >= 0) {
		se->goto_line(p_line, p_col);
	}

	controller->notify_script_changed(p_resource);
	return true;
}


void ScriptEditorView::save_current_script() {
	ScriptEditorBase *current = _get_current_editor();
	if (!current || _test_script_times_on_disk()) {
		return;
	}

	if (controller->get_trim_trailing_whitespace_on_save()) {
		current->trim_trailing_whitespace();
	}

	if (controller->get_trim_final_newlines_on_save()) {
		current->trim_final_newlines();
	}

	if (controller->get_convert_indent_on_save()) {
		current->convert_indent();
	}

	// Format last so its canonical output is the final state of the buffer and
	// matches the CLI byte-for-byte. No-op for non-FoundryScript buffers and on parse
	// error; the diagnostic is left to the live parser to avoid save-time noise.
	if (controller->get_format_on_save()) {
		current->format_document(false);
	}

	Ref<Resource> resource = current->get_edited_resource();
	Ref<TextFile> text_file = resource;
	Ref<Script> scr = resource;

	if (text_file.is_valid()) {
		current->apply_code();
		_save_text_file(text_file, text_file->get_path());
		return;
	}

	if (scr.is_valid()) {
		clear_docs_from_script(scr);
	}

	warn_if_saving_declared_build_output(resource->get_path());
	if (EditorNode *editor_node = EditorNode::get_singleton()) {
		editor_node->save_resource(resource);
	}

	if (scr.is_valid()) {
		update_docs_from_script(scr);
	}
}


void ScriptEditorView::update_script_times() {
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));
		if (se) {
			se->edited_file_data.last_modified_time = FileAccess::get_modified_time(se->edited_file_data.path);
		}
	}
}

void ScriptEditorView::apply_scripts() const {
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));
		if (!se) {
			continue;
		}
		se->insert_final_newline();
		se->apply_code();
	}
}

bool ScriptEditorView::apply_script_refactor_plan(const ScriptRefactorApplyPlan &p_plan, String &r_error_message) {
	if (p_plan.files.is_empty()) {
		r_error_message = TTR("There are no refactor edits to apply.");
		return false;
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	ERR_FAIL_NULL_V(undo_redo, false);

	for (const ScriptRefactorFilePlan &file : p_plan.files) {
		if (is_embedded_script_path(file.path)) {
			r_error_message = vformat(TTR("Cannot apply refactor to embedded script path '%s'."), file.path);
			return false;
		}
		if (get_open_editor_for_path(file.path) != nullptr) {
			continue;
		}

		if (!ScriptRefactorApply::can_write_file(file.path, r_error_message)) {
			return false;
		}
	}

	// Closed-file writability is checked before commit so common failures happen
	// before any file changes. The actual write step is still best-effort against
	// races such as disk-full or files changing permissions between check and use.
	undo_redo->create_action_for_history(get_script_refactor_action_name(), EditorUndoRedoManager::GLOBAL_HISTORY);
	for (const ScriptRefactorFilePlan &file : p_plan.files) {
		undo_redo->add_do_method(this, "_set_refactor_file_source", file.path, file.after_source, false);
		undo_redo->add_undo_method(this, "_set_refactor_file_source", file.path, file.before_source, file.before_source_is_saved_version);
	}
	undo_redo->commit_action();

	_update_script_names();
	r_error_message = String();
	return true;
}

bool ScriptEditorView::can_undo_script_refactor() const {
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	return undo_redo != nullptr &&
			undo_redo->get_current_action_history_id() == EditorUndoRedoManager::GLOBAL_HISTORY &&
			undo_redo->get_current_action_name() == get_script_refactor_action_name();
}

bool ScriptEditorView::can_redo_script_refactor() const {
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	return undo_redo != nullptr &&
			undo_redo->get_current_redo_action_history_id() == EditorUndoRedoManager::GLOBAL_HISTORY &&
			undo_redo->get_current_redo_action_name() == get_script_refactor_action_name();
}

bool ScriptEditorView::undo_script_refactor() {
	if (!can_undo_script_refactor()) {
		return false;
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	ERR_FAIL_NULL_V(undo_redo, false);

	const bool undone = undo_redo->undo_history(EditorUndoRedoManager::GLOBAL_HISTORY);
	if (undone) {
		_update_script_names();
	}
	return undone;
}

bool ScriptEditorView::redo_script_refactor() {
	if (!can_redo_script_refactor()) {
		return false;
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	ERR_FAIL_NULL_V(undo_redo, false);

	const bool redone = undo_redo->redo_history(EditorUndoRedoManager::GLOBAL_HISTORY);
	if (redone) {
		_update_script_names();
	}
	return redone;
}

void ScriptEditorView::reload_scripts(bool p_refresh_only) {
	// Call deferred to make sure it runs on the main thread.
	if (!Thread::is_main_thread()) {
		callable_mp(this, &ScriptEditorView::_reload_scripts).call_deferred(p_refresh_only);
		return;
	}
	_reload_scripts(p_refresh_only);
}

void ScriptEditorView::_reload_scripts(bool p_refresh_only) {
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));
		if (!se) {
			continue;
		}

		Ref<Resource> edited_res = se->get_edited_resource();

		if (edited_res->is_built_in()) {
			continue; // Internal script, who cares.
		}

		if (p_refresh_only) {
			// Make sure the modified time is correct.
			se->edited_file_data.last_modified_time = FileAccess::get_modified_time(edited_res->get_path());
		} else {
			uint64_t last_date = se->edited_file_data.last_modified_time;
			uint64_t date = FileAccess::get_modified_time(edited_res->get_path());

			if (last_date == date) {
				continue;
			}
			se->edited_file_data.last_modified_time = date;

			Ref<Script> scr = edited_res;
			if (scr.is_valid()) {
				Ref<Script> rel_scr = ResourceLoader::load(scr->get_path(), scr->get_class(), ResourceFormatLoader::CACHE_MODE_IGNORE);
				ERR_CONTINUE(rel_scr.is_null());
				scr->set_source_code(rel_scr->get_source_code());
				scr->reload(true);

				update_docs_from_script(scr);
			}

			Ref<JSON> json = edited_res;
			if (json.is_valid()) {
				Ref<JSON> rel_json = ResourceLoader::load(json->get_path(), json->get_class(), ResourceFormatLoader::CACHE_MODE_IGNORE);
				ERR_CONTINUE(rel_json.is_null());
				json->parse(rel_json->get_parsed_text(), true);
			}

			Ref<TextFile> text_file = edited_res;
			if (text_file.is_valid()) {
				text_file->reload_from_file();
			}
		}

		se->reload_text();
	}

	controller->get_disk_changed()->hide();
	_update_script_names();
}


Ref<Resource> ScriptEditorView::open_file(const String &p_file) {
	List<String> extensions;
	ResourceLoader::get_recognized_extensions_for_type("Script", &extensions);
	ResourceLoader::get_recognized_extensions_for_type("JSON", &extensions);
	if (extensions.find(p_file.get_extension())) {
		Ref<Resource> scr = ResourceLoader::load(p_file);
		if (scr.is_null()) {
			EditorNode::get_singleton()->show_warning(TTR("Could not load file at:") + "\n\n" + p_file, TTR("Error!"));
			return Ref<Resource>();
		}

		edit(scr);
		return scr;
	}

	Error error;
	Ref<TextFile> text_file = _load_text_file(p_file, &error);
	if (error != OK) {
		EditorNode::get_singleton()->show_warning(TTR("Could not load file at:") + "\n\n" + p_file, TTR("Error!"));
		return Ref<Resource>();
	}

	if (text_file.is_valid()) {
		edit(text_file);
		return text_file;
	}
	return Ref<Resource>();
}

void ScriptEditorView::_editor_stop() {
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));
		if (!se) {
			continue;
		}

		se->set_debugger_active(false);
	}
}

void ScriptEditorView::_add_callback(Object *p_obj, const String &p_function, const PackedStringArray &p_args) {
	ERR_FAIL_NULL(p_obj);
	Ref<Script> scr = p_obj->get_script();
	ERR_FAIL_COND(scr.is_null());

	if (!scr->get_language()->can_make_function()) {
		return;
	}

	EditorNode::get_singleton()->push_item(scr.ptr());

	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));
		if (!se) {
			continue;
		}
		if (se->get_edited_resource() != scr) {
			continue;
		}

		se->add_callback(p_function, p_args);

		_go_to_tab(i);

		script_list->select(script_list->find_metadata(i));

		// Save the current script so the changes can be picked up by an external editor.
		if (!scr.ptr()->is_built_in()) { // But only if it's not built-in script.
			save_current_script();
		}

		break;
	}

	// Move back to the previously edited node to reselect it in the Inspector and the SignalsDock.
	// We assume that the previous item is the node on which the callbacks were added.
	EditorNode::get_singleton()->edit_previous_item();
}

void ScriptEditorView::_save_editor_state(ScriptEditorBase *p_editor) {
	if (restoring_layout) {
		return;
	}

	const String &path = p_editor->get_edited_resource()->get_path();
	if (path.is_empty()) {
		return;
	}

	controller->get_script_editor_cache()->set_value(path, "state", p_editor->get_edit_state());
	// This is saved later when we save the editor layout.
}

void ScriptEditorView::_save_layout() {
	if (restoring_layout) {
		return;
	}

	if (EditorNode *editor_node = EditorNode::get_singleton()) {
		editor_node->save_editor_layout_delayed();
	}
}

void ScriptEditorView::_apply_editor_settings() {
	textfile_extensions.clear();
	const Vector<String> textfile_ext = ((String)(EDITOR_GET("docks/filesystem/textfile_extensions"))).split(",", false);
	for (const String &E : textfile_ext) {
		textfile_extensions.insert(E);
	}

	controller->set_trim_trailing_whitespace_on_save(EDITOR_GET("text_editor/behavior/files/trim_trailing_whitespace_on_save"));
	controller->set_trim_final_newlines_on_save(EDITOR_GET("text_editor/behavior/files/trim_final_newlines_on_save"));
	controller->set_convert_indent_on_save(EDITOR_GET("text_editor/behavior/files/convert_indent_on_save"));
	controller->set_format_on_save(EDITOR_GET("text_editor/behavior/files/format_on_save"));

	members_overview_enabled = EDITOR_GET("text_editor/script_list/show_members_overview");
	help_overview_enabled = EDITOR_GET("text_editor/help/show_help_index");
	controller->set_external_editor_active(EDITOR_GET("text_editor/external/use_external_editor"));
	_update_members_overview_visibility();
	_update_help_overview_visibility();

	controller->_update_autosave_timer();

	_update_script_names();

	ScriptServer::set_reload_scripts_on_save(EDITOR_GET("text_editor/behavior/files/auto_reload_and_parse_scripts_on_save"));

	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));
		if (!se) {
			continue;
		}

		se->update_settings();
	}
}


void ScriptEditorView::_update_find_replace_bar() {
	ScriptEditorBase *se = _get_current_editor();
	if (se) {
		se->set_find_replace_bar(find_replace_bar);
	} else {
		find_replace_bar->set_text_edit(nullptr);
		find_replace_bar->hide();
	}
}


void ScriptEditorView::_tree_changed() {
	if (waiting_update_names) {
		return;
	}

	waiting_update_names = true;
	callable_mp(this, &ScriptEditorView::_update_script_names).call_deferred();
}

void ScriptEditorView::_split_dragged(float) {
	_save_layout();
}

Variant ScriptEditorView::get_drag_data_fw(const Point2 &p_point, Control *p_from) {
	if (tab_container->get_tab_count() == 0) {
		return Variant();
	}

	Node *cur_node = tab_container->get_tab_control(tab_container->get_current_tab());

	HBoxContainer *drag_preview = memnew(HBoxContainer);
	String preview_name = "";
	Ref<Texture2D> preview_icon;

	ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(cur_node);
	if (se) {
		preview_name = se->get_name();
		preview_icon = se->get_theme_icon();
	}
	EditorHelp *eh = Object::cast_to<EditorHelp>(cur_node);
	if (eh) {
		preview_name = eh->get_class();
		preview_icon = get_editor_theme_icon(SNAME("Help"));
	}

	if (preview_icon.is_valid()) {
		TextureRect *tf = memnew(TextureRect);
		tf->set_texture(preview_icon);
		tf->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
		drag_preview->add_child(tf);
	}
	Label *label = memnew(Label(preview_name));
	label->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED); // Don't translate script names and class names.
	drag_preview->add_child(label);
	set_drag_preview(drag_preview);

	Dictionary drag_data;
	drag_data["type"] = "script_list_element"; // using a custom type because node caused problems when dragging to scene tree
	drag_data["script_list_element"] = cur_node;

	return drag_data;
}

bool ScriptEditorView::can_drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from) const {
	Dictionary d = p_data;
	if (!d.has("type")) {
		return false;
	}

	if (String(d["type"]) == "script_list_element") {
		Node *node = Object::cast_to<Node>(d["script_list_element"]);

		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(node);
		if (se) {
			return true;
		}
		EditorHelp *eh = Object::cast_to<EditorHelp>(node);
		if (eh) {
			return true;
		}
	}

	if (String(d["type"]) == "nodes") {
		Array nodes = d["nodes"];
		if (nodes.is_empty()) {
			return false;
		}
		Node *node = get_node((nodes[0]));

		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(node);
		if (se) {
			return true;
		}
		EditorHelp *eh = Object::cast_to<EditorHelp>(node);
		if (eh) {
			return true;
		}
	}

	if (String(d["type"]) == "files") {
		Vector<String> files = d["files"];

		if (files.is_empty()) {
			return false; //weird
		}

		for (int i = 0; i < files.size(); i++) {
			const String &file = files[i];
			if (file.is_empty() || !FileAccess::exists(file)) {
				continue;
			}
			if (ResourceLoader::exists(file, "Script") || ResourceLoader::exists(file, "JSON")) {
				Ref<Resource> scr = ResourceLoader::load(file);
				if (scr.is_valid()) {
					return true;
				}
			}

			if (textfile_extensions.has(file.get_extension())) {
				Error err;
				Ref<TextFile> text_file = _load_text_file(file, &err);
				if (text_file.is_valid() && err == OK) {
					return true;
				}
			}
		}
		return false;
	}

	return false;
}

void ScriptEditorView::drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from) {
	if (!can_drop_data_fw(p_point, p_data, p_from)) {
		return;
	}

	Dictionary d = p_data;
	if (!d.has("type")) {
		return;
	}

	if (String(d["type"]) == "script_list_element") {
		Node *node = Object::cast_to<Node>(d["script_list_element"]);

		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(node);
		EditorHelp *eh = Object::cast_to<EditorHelp>(node);
		if (se || eh) {
			int new_index = 0;
			if (script_list->get_item_count() > 0) {
				int pos = 0;
				if (p_point == Vector2(Math::INF, Math::INF)) {
					if (script_list->is_anything_selected()) {
						pos = script_list->get_selected_items()[0];
					}
				} else {
					pos = script_list->get_item_at_position(p_point);
				}
				new_index = script_list->get_item_metadata(pos);
			}
			tab_container->move_child(node, new_index);
			tab_container->set_current_tab(new_index);
			_update_script_names();
		}
	}

	if (String(d["type"]) == "nodes") {
		Array nodes = d["nodes"];
		if (nodes.is_empty()) {
			return;
		}
		Node *node = get_node(nodes[0]);

		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(node);
		EditorHelp *eh = Object::cast_to<EditorHelp>(node);
		if (se || eh) {
			int new_index = 0;
			if (script_list->get_item_count() > 0) {
				int pos = 0;
				if (p_point == Vector2(Math::INF, Math::INF)) {
					if (script_list->is_anything_selected()) {
						pos = script_list->get_selected_items()[0];
					}
				} else {
					pos = script_list->get_item_at_position(p_point);
				}
				new_index = script_list->get_item_metadata(pos);
			}
			tab_container->move_child(node, new_index);
			tab_container->set_current_tab(new_index);
			_update_script_names();
		}
	}

	if (String(d["type"]) == "files") {
		Vector<String> files = d["files"];

		int new_index = 0;
		if (script_list->get_item_count() > 0) {
			int pos = 0;
			if (p_point == Vector2(Math::INF, Math::INF)) {
				if (script_list->is_anything_selected()) {
					pos = script_list->get_selected_items()[0];
				}
			} else {
				pos = script_list->get_item_at_position(p_point);
			}
			new_index = script_list->get_item_metadata(pos);
		}
		int num_tabs_before = tab_container->get_tab_count();
		for (int i = 0; i < files.size(); i++) {
			const String &file = files[i];
			if (file.is_empty() || !FileAccess::exists(file)) {
				continue;
			}

			if (!ResourceLoader::exists(file, "Script") && !ResourceLoader::exists(file, "JSON") && !textfile_extensions.has(file.get_extension())) {
				continue;
			}

			Ref<Resource> res = open_file(file);
			if (res.is_valid()) {
				const int num_tabs = tab_container->get_tab_count();
				if (num_tabs > num_tabs_before) {
					tab_container->move_child(tab_container->get_tab_control(tab_container->get_tab_count() - 1), new_index);
					num_tabs_before = num_tabs;
				} else if (num_tabs > 0) { /* Maybe script was already open */
					tab_container->move_child(tab_container->get_tab_control(tab_container->get_current_tab()), new_index);
				}
			}
		}
		if (tab_container->get_tab_count() > 0) {
			tab_container->set_current_tab(new_index);
		}
		_update_script_names();
	}
}

void ScriptEditorView::input(const Ref<InputEvent> &p_event) {
	// This is implemented in `input()` rather than `unhandled_input()` to allow
	// the shortcut to be used regardless of the click location.
	// This feature can be disabled to avoid interfering with other uses of the additional
	// mouse buttons, such as push-to-talk in a VoIP program.
	if (EDITOR_GET("interface/editor/mouse_extra_buttons_navigate_history")) {
		const Ref<InputEventMouseButton> mb = p_event;

		// Navigate the script history using additional mouse buttons present on some mice.
		// This must be hardcoded as the editor shortcuts dialog doesn't allow assigning
		// more than one shortcut per action.
		if (mb.is_valid() && mb->is_pressed() && is_visible_in_tree()) {
			if (mb->get_button_index() == MouseButton::MB_XBUTTON1) {
				_history_back();
			}

			if (mb->get_button_index() == MouseButton::MB_XBUTTON2) {
				_history_forward();
			}
		}
	}
}

void ScriptEditorView::shortcut_input(const Ref<InputEvent> &p_event) {
	ERR_FAIL_COND(p_event.is_null());

	if (!is_visible_in_tree() || !p_event->is_pressed()) {
		return;
	}
	if (ED_IS_SHORTCUT("script_editor/next_script", p_event)) {
		if (script_list->get_item_count() > 1) {
			int next_tab = script_list->get_current() + 1;
			next_tab %= script_list->get_item_count();
			_go_to_tab(script_list->get_item_metadata(next_tab));
			_update_script_names();
		}
		accept_event();
	}
	if (ED_IS_SHORTCUT("script_editor/prev_script", p_event)) {
		if (script_list->get_item_count() > 1) {
			int next_tab = script_list->get_current() - 1;
			next_tab = next_tab >= 0 ? next_tab : script_list->get_item_count() - 1;
			_go_to_tab(script_list->get_item_metadata(next_tab));
			_update_script_names();
		}
		accept_event();
	}
	if (ED_IS_SHORTCUT("script_editor/window_move_up", p_event)) {
		_menu_option(FILE_MENU_MOVE_UP);
		accept_event();
	}
	if (ED_IS_SHORTCUT("script_editor/window_move_down", p_event)) {
		_menu_option(FILE_MENU_MOVE_DOWN);
		accept_event();
	}

	if (p_event->is_echo()) {
		return;
	}

	Callable custom_callback = EditorContextMenuPluginManager::get_singleton()->match_custom_shortcut(EditorContextMenuPlugin::CONTEXT_SLOT_SCRIPT_EDITOR, p_event);
	if (custom_callback.is_valid()) {
		Ref<Resource> resource;
		ScriptEditorBase *current = _get_current_editor();
		if (current) {
			resource = current->get_edited_resource();
		}
		EditorContextMenuPluginManager::get_singleton()->invoke_callback(custom_callback, resource);
		accept_event();
	}
}

void ScriptEditorView::_script_list_clicked(int p_item, Vector2 p_local_mouse_pos, MouseButton p_mouse_button_index) {
	if (p_mouse_button_index == MouseButton::MIDDLE) {
		script_list->select(p_item);
		_script_selected(p_item);
		_menu_option(FILE_MENU_CLOSE);
	}

	if (p_mouse_button_index == MouseButton::RIGHT) {
		_make_script_list_context_menu();
	}
}

void ScriptEditorView::_make_script_list_context_menu() {
	context_menu->clear();

	int selected = tab_container->get_current_tab();
	if (selected < 0 || selected >= tab_container->get_tab_count()) {
		return;
	}

	ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(selected));
	if (se) {
		context_menu->add_shortcut(ED_GET_SHORTCUT("script_editor/save"), FILE_MENU_SAVE);
		context_menu->add_shortcut(ED_GET_SHORTCUT("script_editor/save_as"), FILE_MENU_SAVE_AS);
	}
	context_menu->add_shortcut(ED_GET_SHORTCUT("script_editor/close_file"), FILE_MENU_CLOSE);
	context_menu->add_shortcut(ED_GET_SHORTCUT("script_editor/close_other_tabs"), FILE_MENU_CLOSE_OTHER_TABS);
	context_menu->add_shortcut(ED_GET_SHORTCUT("script_editor/close_tabs_below"), FILE_MENU_CLOSE_TABS_BELOW);
	context_menu->add_shortcut(ED_GET_SHORTCUT("script_editor/close_all"), FILE_MENU_CLOSE_ALL);
	context_menu->add_shortcut(ED_GET_SHORTCUT("script_editor/close_docs"), FILE_MENU_CLOSE_DOCS);
	context_menu->add_separator();
	if (se) {
		Ref<Script> scr = se->get_edited_resource();
		if (scr.is_valid() && scr->is_tool()) {
			context_menu->add_shortcut(ED_GET_SHORTCUT("script_editor/reload_script_soft"), FILE_MENU_SOFT_RELOAD_TOOL);
			context_menu->add_shortcut(ED_GET_SHORTCUT("script_editor/run_file"), FILE_MENU_RUN);
			context_menu->add_separator();
		}
		context_menu->add_shortcut(ED_GET_SHORTCUT("script_editor/copy_path"), FILE_MENU_COPY_PATH);
		context_menu->set_item_disabled(-1, se->get_edited_resource()->get_path().is_empty());
		context_menu->add_shortcut(ED_GET_SHORTCUT("script_editor/copy_uid"), FILE_MENU_COPY_UID);
		context_menu->set_item_disabled(-1, ResourceLoader::get_resource_uid(se->get_edited_resource()->get_path()) == ResourceUID::INVALID_ID);
		context_menu->add_shortcut(ED_GET_SHORTCUT("script_editor/show_in_file_system"), FILE_MENU_SHOW_IN_FILE_SYSTEM);
		context_menu->add_separator();
	}

	context_menu->add_shortcut(ED_GET_SHORTCUT("script_editor/window_move_up"), FILE_MENU_MOVE_UP);
	context_menu->add_shortcut(ED_GET_SHORTCUT("script_editor/window_move_down"), FILE_MENU_MOVE_DOWN);
	context_menu->add_shortcut(ED_GET_SHORTCUT("script_editor/window_sort"), FILE_MENU_SORT);
	context_menu->add_shortcut(ED_GET_SHORTCUT("script_editor/toggle_files_panel"), FILE_MENU_TOGGLE_FILES_PANEL);

	context_menu->set_item_disabled(context_menu->get_item_index(FILE_MENU_CLOSE_ALL), tab_container->get_tab_count() <= 0);
	context_menu->set_item_disabled(context_menu->get_item_index(FILE_MENU_CLOSE_OTHER_TABS), tab_container->get_tab_count() <= 1);
	context_menu->set_item_disabled(context_menu->get_item_index(FILE_MENU_CLOSE_DOCS), !_has_docs_tab());
	context_menu->set_item_disabled(context_menu->get_item_index(FILE_MENU_CLOSE_TABS_BELOW), tab_container->get_current_tab() >= tab_container->get_tab_count() - 1);
	context_menu->set_item_disabled(context_menu->get_item_index(FILE_MENU_MOVE_UP), tab_container->get_current_tab() <= 0);
	context_menu->set_item_disabled(context_menu->get_item_index(FILE_MENU_MOVE_DOWN), tab_container->get_current_tab() >= tab_container->get_tab_count() - 1);
	context_menu->set_item_disabled(context_menu->get_item_index(FILE_MENU_SORT), tab_container->get_tab_count() <= 1);

	// Context menu plugin.
	Vector<String> selected_paths;
	if (se) {
		Ref<Resource> scr = se->get_edited_resource();
		if (scr.is_valid()) {
			String path = scr->get_path();
			selected_paths.push_back(path);
		}
	}
	EditorContextMenuPluginManager::get_singleton()->add_options_from_plugins(context_menu, EditorContextMenuPlugin::CONTEXT_SLOT_SCRIPT_EDITOR, selected_paths);

	context_menu->set_position(get_screen_position() + get_local_mouse_position());
	context_menu->reset_size();
	context_menu->popup();
}

void ScriptEditorView::set_window_layout(Ref<ConfigFile> p_layout) {
	if (!bool(EDITOR_GET("text_editor/behavior/files/restore_scripts_on_load"))) {
		return;
	}

	if (!p_layout->has_section_key("ScriptEditor", "open_scripts") && !p_layout->has_section_key("ScriptEditor", "open_help")) {
		return;
	}

	Array scripts = p_layout->get_value("ScriptEditor", "open_scripts");
	Array helps;
	if (p_layout->has_section_key("ScriptEditor", "open_help")) {
		helps = p_layout->get_value("ScriptEditor", "open_help");
	}

	restoring_layout = true;

	HashSet<String> loaded_scripts;
	List<String> extensions;
	ResourceLoader::get_recognized_extensions_for_type("Script", &extensions);
	ResourceLoader::get_recognized_extensions_for_type("JSON", &extensions);

	for (const Variant &v : scripts) {
		String path = v;

		Dictionary script_info = v;
		if (!script_info.is_empty()) {
			path = script_info["path"];
		}

		if (!_script_exists(path)) {
			if (controller->get_script_editor_cache()->has_section(path)) {
				controller->get_script_editor_cache()->erase_section(path);
			}
			continue;
		} else if (!path.is_resource_file()) {
			if (EditorNode *editor_node = EditorNode::get_singleton()) {
				if (!editor_node->is_scene_open(path.get_slice("::", 0))) {
					continue;
				}
			}
		}
		loaded_scripts.insert(path);

		bool is_script = false;
		if (path.is_resource_file()) {
			is_script = extensions.find(path.get_extension());
		} else {
			Ref<Script> scr = ResourceCache::get_ref(path);
			if (scr.is_valid()) {
				is_script = true;
			}
		}

		if (is_script) {
			Ref<Resource> scr = ResourceLoader::load(path);
			if (scr.is_null()) {
				continue;
			}
			if (!edit(scr, false)) {
				continue;
			}
		} else {
			Error error;
			Ref<TextFile> text_file = _load_text_file(path, &error);
			if (error != OK || text_file.is_null()) {
				continue;
			}
			if (!edit(text_file, false)) {
				continue;
			}
		}

		if (!script_info.is_empty()) {
			ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(tab_container->get_tab_count() - 1));
			if (se) {
				se->set_edit_state(script_info["state"]);
			}
		}
	}

	for (int i = 0; i < helps.size(); i++) {
		String path = helps[i];
		if (path.is_empty()) { // invalid, skip
			continue;
		}
		_help_class_open(path);
	}

	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		tab_container->get_tab_control(i)->set_meta("__editor_pass", Variant());
	}

	if (p_layout->has_section_key("ScriptEditor", "script_split_offset")) {
		script_split->set_split_offset(p_layout->get_value("ScriptEditor", "script_split_offset"));
	}

	if (p_layout->has_section_key("ScriptEditor", "list_split_offset")) {
		list_split->set_split_offset(p_layout->get_value("ScriptEditor", "list_split_offset"));
	}

	// Remove any deleted editors that have been removed between launches.
	// and if a Script, register breakpoints with the debugger.
	Vector<String> cached_editors = controller->get_script_editor_cache()->get_sections();
	for (const String &E : cached_editors) {
		if (loaded_scripts.has(E)) {
			continue;
		}

		if (!_script_exists(E)) {
			controller->get_script_editor_cache()->erase_section(E);
			continue;
		}

		Array breakpoints = controller->get_cached_breakpoints_for_script(E);
		for (int breakpoint : breakpoints) {
			EditorDebuggerNode::get_singleton()->set_breakpoint(E, (int)breakpoint + 1, true);
		}
	}

	_set_script_zoom_factor(p_layout->get_value("ScriptEditor", "zoom_factor", 1.0f));

	restoring_layout = false;

	_update_script_names();

	if (p_layout->has_section_key("ScriptEditor", "selected_script")) {
		String selected_script = p_layout->get_value("ScriptEditor", "selected_script");
		// If the selected script is not in the list of open scripts, select nothing.
		for (int i = 0; i < tab_container->get_tab_count(); i++) {
			ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));
			if (se && se->get_edited_resource()->get_path() == selected_script) {
				_go_to_tab(i);
				break;
			}
		}
	}
}

void ScriptEditorView::get_window_layout(Ref<ConfigFile> p_layout) {
	Array scripts;
	Array helps;
	String selected_script;
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));
		if (se) {
			const String path = se->get_edited_resource()->get_path();
			if (path.is_empty()) {
				continue;
			}

			if (tab_container->get_current_tab_control() == tab_container->get_tab_control(i)) {
				selected_script = path;
			}

			_save_editor_state(se);
			scripts.push_back(path);
		}

		EditorHelp *eh = Object::cast_to<EditorHelp>(tab_container->get_tab_control(i));

		if (eh) {
			helps.push_back(eh->get_class());
		}
	}

	p_layout->set_value("ScriptEditor", "open_scripts", scripts);
	p_layout->set_value("ScriptEditor", "selected_script", selected_script);
	p_layout->set_value("ScriptEditor", "open_help", helps);
	p_layout->set_value("ScriptEditor", "script_split_offset", script_split->get_split_offset());
	p_layout->set_value("ScriptEditor", "list_split_offset", list_split->get_split_offset());
	p_layout->set_value("ScriptEditor", "zoom_factor", zoom_factor);

	// Save the cache.
	controller->get_script_editor_cache()->save(EditorPaths::get_singleton()->get_project_settings_dir().path_join("script_editor_cache.cfg"));
}

void ScriptEditorView::_help_class_open(const String &p_class) {
	if (p_class.is_empty()) {
		return;
	}

	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		EditorHelp *eh = Object::cast_to<EditorHelp>(tab_container->get_tab_control(i));

		if (eh && eh->get_class() == p_class) {
			_go_to_tab(i);
			_update_script_names();
			return;
		}
	}

	EditorHelp *eh = memnew(EditorHelp);

	eh->set_name(p_class);
	tab_container->add_child(eh);
	_go_to_tab(tab_container->get_tab_count() - 1);
	eh->go_to_class(p_class);
	eh->connect("go_to_help", callable_mp(this, &ScriptEditorView::_help_class_goto));
	eh->connect("request_save_history", callable_mp(this, &ScriptEditorView::_save_history));
	controller->add_recent_script(p_class);
	_sort_list_on_update = true;
	_update_script_names();
	_save_layout();
}

void ScriptEditorView::goto_help(const String &p_desc) {
	_help_class_goto(p_desc);
}

void ScriptEditorView::_help_class_goto(const String &p_desc) {
	String cname = p_desc.get_slicec(':', 1);

	if (_help_tab_goto(cname, p_desc)) {
		return;
	}

	EditorHelp *eh = memnew(EditorHelp);

	eh->set_name(cname);
	tab_container->add_child(eh);
	_go_to_tab(tab_container->get_tab_count() - 1);
	eh->go_to_help(p_desc);
	eh->connect("go_to_help", callable_mp(this, &ScriptEditorView::_help_class_goto));
	controller->add_recent_script(eh->get_class());
	_sort_list_on_update = true;
	_update_script_names();
	_save_layout();
}

bool ScriptEditorView::_help_tab_goto(const String &p_name, const String &p_desc) {
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		EditorHelp *eh = Object::cast_to<EditorHelp>(tab_container->get_tab_control(i));

		if (eh && eh->get_class() == p_name) {
			_go_to_tab(i);
			eh->go_to_help(p_desc);
			_update_script_names();
			return true;
		}
	}
	return false;
}

void ScriptEditorView::update_doc(const String &p_name) {
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		EditorHelp *eh = Object::cast_to<EditorHelp>(tab_container->get_tab_control(i));
		if (eh && eh->get_class() == p_name) {
			eh->update_doc();
			return;
		}
	}
}

void ScriptEditorView::clear_docs_from_script(const Ref<Script> &p_script) {
	ERR_FAIL_COND(p_script.is_null());

	for (const DocData::ClassDoc &cd : p_script->get_documentation()) {
		if (EditorHelp::get_doc_data()->has_doc(cd.name)) {
			EditorHelp::get_doc_data()->remove_doc(cd.name);
		}
	}
}

void ScriptEditorView::update_docs_from_script(const Ref<Script> &p_script) {
	ERR_FAIL_COND(p_script.is_null());

	for (const DocData::ClassDoc &cd : p_script->get_documentation()) {
		EditorHelp::get_doc_data()->add_doc(cd);
		update_doc(cd.name);
	}
}

void ScriptEditorView::_update_selected_editor_menu() {
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		bool current = tab_container->get_current_tab() == i;

		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));
		if (se && se->get_edit_menu()) {
			if (current) {
				se->get_edit_menu()->show();
			} else {
				se->get_edit_menu()->hide();
			}
		}
	}

	EditorHelp *eh = Object::cast_to<EditorHelp>(tab_container->get_current_tab_control());
	script_search_menu->get_popup()->clear();
	if (eh) {
		script_search_menu->get_popup()->add_shortcut(ED_SHORTCUT("script_editor/find", TTRC("Find..."), KeyModifierMask::CMD_OR_CTRL | Key::F), HELP_SEARCH_FIND);
		script_search_menu->get_popup()->add_shortcut(ED_SHORTCUT("script_editor/find_next", TTRC("Find Next"), Key::F3), HELP_SEARCH_FIND_NEXT);
		script_search_menu->get_popup()->add_shortcut(ED_SHORTCUT("script_editor/find_previous", TTRC("Find Previous"), KeyModifierMask::SHIFT | Key::F3), HELP_SEARCH_FIND_PREVIOUS);
		script_search_menu->get_popup()->add_separator();
		script_search_menu->get_popup()->add_shortcut(ED_GET_SHORTCUT("editor/find_in_files"), SEARCH_IN_FILES);
		script_search_menu->get_popup()->add_shortcut(ED_GET_SHORTCUT("script_editor/replace_in_files"), REPLACE_IN_FILES);
		script_search_menu->show();
	} else {
		if (tab_container->get_tab_count() == 0) {
			script_search_menu->get_popup()->add_shortcut(ED_GET_SHORTCUT("editor/find_in_files"), SEARCH_IN_FILES);
			script_search_menu->get_popup()->add_shortcut(ED_GET_SHORTCUT("script_editor/replace_in_files"), REPLACE_IN_FILES);
			script_search_menu->show();
		} else {
			script_search_menu->hide();
		}
	}
}

void ScriptEditorView::_unlock_history() {
	lock_history = false;
}

void ScriptEditorView::_update_history_pos(int p_new_pos) {
	Node *n = tab_container->get_current_tab_control();

	if (Object::cast_to<ScriptEditorBase>(n)) {
		history.write[history_pos].state = Object::cast_to<ScriptEditorBase>(n)->get_navigation_state();
	}
	if (Object::cast_to<EditorHelp>(n)) {
		history.write[history_pos].state = Object::cast_to<EditorHelp>(n)->get_scroll();
	}

	history_pos = p_new_pos;
	tab_container->set_current_tab(tab_container->get_tab_idx_from_control(history[history_pos].control));

	n = history[history_pos].control;

	ScriptEditorBase *seb = Object::cast_to<ScriptEditorBase>(n);
	if (seb) {
		lock_history = true;
		seb->set_edit_state(history[history_pos].state);
		// `set_edit_state()` can modify the caret position which might trigger a
		// request to save the history. Since `TextEdit::caret_changed` is emitted
		// deferred, we need to defer unlocking of the history as well.
		callable_mp(this, &ScriptEditorView::_unlock_history).call_deferred();
		seb->ensure_focus();

		Ref<Script> scr = seb->get_edited_resource();
		if (scr.is_valid()) {
			controller->notify_script_changed(scr);
		}

		seb->validate();
	}

	EditorHelp *eh = Object::cast_to<EditorHelp>(n);
	if (eh) {
		eh->set_scroll(history[history_pos].state);
		eh->set_focused();
	}

	n->set_meta("__editor_pass", ++edit_pass);
	_update_script_names();
	_update_history_arrows();
	_update_selected_editor_menu();
}

void ScriptEditorView::_history_forward() {
	if (history_pos < history.size() - 1) {
		_update_history_pos(history_pos + 1);
	}
}

void ScriptEditorView::_history_back() {
	if (history_pos > 0) {
		_update_history_pos(history_pos - 1);
	}
}


TypedArray<ScriptEditorBase> ScriptEditorView::_get_open_script_editors() const {
	TypedArray<ScriptEditorBase> script_editors;
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));
		if (!se) {
			continue;
		}
		script_editors.push_back(se);
	}
	return script_editors;
}

void ScriptEditorView::set_scene_root_script(Ref<Script> p_script) {
	// Don't open dominant script if using an external editor.
	bool use_external_editor =
			controller->is_external_editor_active() ||
			(p_script.is_valid() && p_script->get_language()->overrides_external_editor());
	use_external_editor = use_external_editor && !(p_script.is_valid() && p_script->is_built_in()); // Ignore external editor for built-in scripts.
	const bool open_dominant = EDITOR_GET("text_editor/behavior/files/open_dominant_script_on_scene_change");

	if (open_dominant && !use_external_editor && p_script.is_valid()) {
		edit(p_script);
	}
}

bool ScriptEditorView::script_goto_method(Ref<Script> p_script, const String &p_method) {
	int line = p_script->get_member_line(p_method);

	if (line == -1) {
		return false;
	}

	return edit(p_script, line, 0);
}


void ScriptEditorView::_help_search(const String &p_text) {
	controller->get_help_search_dialog()->popup_dialog(p_text);
}





void ScriptEditorView::_script_changed() {
	if (EditorNode *editor_node = EditorNode::get_singleton()) {
		if (SignalsDock *signals_dock = editor_node->get_focused_signals_dock()) {
			signals_dock->update_lists();
		}
	}
}


void ScriptEditorView::_set_script_zoom_factor(float p_zoom_factor) {
	if (zoom_factor == p_zoom_factor) {
		return;
	}

	zoom_factor = p_zoom_factor;
}

void ScriptEditorView::_update_code_editor_zoom_factor(CodeTextEditor *p_code_text_editor) {
	if (p_code_text_editor && p_code_text_editor->is_visible_in_tree() && zoom_factor != p_code_text_editor->get_zoom_factor()) {
		p_code_text_editor->set_zoom_factor(zoom_factor);
	}
}

void ScriptEditorView::_window_changed(bool p_visible) {
	if (make_floating) {
		make_floating->set_visible(!p_visible);
	}
	is_floating = p_visible;
}

void ScriptEditorView::_filter_scripts_text_changed(const String &p_newtext) {
	_update_script_names();
}

void ScriptEditorView::_filter_methods_text_changed(const String &p_newtext) {
	_update_members_overview();
}


ScriptEditorView::ScriptEditorView(ScriptEditorController *p_controller, ScriptLeaf *p_leaf) {
	ERR_FAIL_NULL(p_controller);
	controller = p_controller;
	script_leaf = p_leaf;

	restoring_layout = false;
	waiting_update_names = false;
	members_overview_enabled = EDITOR_GET("text_editor/script_list/show_members_overview");
	help_overview_enabled = EDITOR_GET("text_editor/help/show_help_index");

	VBoxContainer *main_container = memnew(VBoxContainer);
	add_child(main_container);

	menu_hb = memnew(HBoxContainer);
	main_container->add_child(menu_hb);

	script_split = memnew(HSplitContainer);
	main_container->add_child(script_split);
	script_split->set_v_size_flags(SIZE_EXPAND_FILL);

#ifdef ANDROID_ENABLED
	virtual_keyboard_spacer = memnew(Control);
	virtual_keyboard_spacer->set_h_size_flags(SIZE_EXPAND_FILL);
	main_container->add_child(virtual_keyboard_spacer);
#endif

	list_split = memnew(VSplitContainer);
	script_split->add_child(list_split);
	list_split->set_v_size_flags(SIZE_EXPAND_FILL);

	scripts_vbox = memnew(VBoxContainer);
	scripts_vbox->set_v_size_flags(SIZE_EXPAND_FILL);
	list_split->add_child(scripts_vbox);

	filter_scripts = memnew(LineEdit);
	filter_scripts->set_placeholder(TTRC("Filter Scripts"));
	filter_scripts->set_accessibility_name(TTRC("Filter Scripts"));
	filter_scripts->set_clear_button_enabled(true);
	filter_scripts->connect(SceneStringName(text_changed), callable_mp(this, &ScriptEditorView::_filter_scripts_text_changed));
	scripts_vbox->add_child(filter_scripts);

	_sort_list_on_update = true;
	script_list = memnew(ItemList);
	script_list->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	script_list->set_custom_minimum_size(Size2(100, 60) * EDSCALE);
	script_list->set_v_size_flags(SIZE_EXPAND_FILL);
	script_list->set_theme_type_variation("ItemListSecondary");
	script_split->set_split_offset(200 * EDSCALE);
	script_list->set_allow_rmb_select(true);
	scripts_vbox->add_child(script_list);
	script_list->connect("item_clicked", callable_mp(this, &ScriptEditorView::_script_list_clicked), CONNECT_DEFERRED);
	SET_DRAG_FORWARDING_GCD(script_list, ScriptEditorView);

	context_menu = memnew(PopupMenu);
	add_child(context_menu);
	context_menu->connect(SceneStringName(id_pressed), callable_mp(this, &ScriptEditorView::_menu_option));

	overview_vbox = memnew(VBoxContainer);
	overview_vbox->set_custom_minimum_size(Size2(0, 90));
	overview_vbox->set_v_size_flags(SIZE_EXPAND_FILL);
	list_split->add_child(overview_vbox);
	list_split->set_visible(EditorSettings::get_singleton()->get_project_metadata("files_panel", "show_files_panel", true));
	buttons_hbox = memnew(HBoxContainer);
	overview_vbox->add_child(buttons_hbox);

	filter_methods = memnew(LineEdit);
	filter_methods->set_placeholder(TTRC("Filter Methods"));
	filter_methods->set_accessibility_name(TTRC("Filter Methods"));
	filter_methods->set_clear_button_enabled(true);
	filter_methods->set_h_size_flags(SIZE_EXPAND_FILL);
	filter_methods->connect(SceneStringName(text_changed), callable_mp(this, &ScriptEditorView::_filter_methods_text_changed));
	buttons_hbox->add_child(filter_methods);

	members_overview_alphabeta_sort_button = memnew(Button);
	members_overview_alphabeta_sort_button->set_theme_type_variation(SceneStringName(FlatButton));
	members_overview_alphabeta_sort_button->set_tooltip_text(TTRC("Toggle alphabetical sorting of the method list."));
	members_overview_alphabeta_sort_button->set_toggle_mode(true);
	members_overview_alphabeta_sort_button->set_pressed(EDITOR_GET("text_editor/script_list/sort_members_outline_alphabetically"));
	members_overview_alphabeta_sort_button->connect(SceneStringName(toggled), callable_mp(this, &ScriptEditorView::_toggle_members_overview_alpha_sort));
	buttons_hbox->add_child(members_overview_alphabeta_sort_button);

	members_overview = memnew(ItemList);
	members_overview->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	members_overview->set_theme_type_variation("ItemListSecondary");
	overview_vbox->add_child(members_overview);
	members_overview->set_allow_reselect(true);
	members_overview->set_custom_minimum_size(Size2(0, 60) * EDSCALE);
	members_overview->set_v_size_flags(SIZE_EXPAND_FILL);
	members_overview->set_allow_rmb_select(true);

	help_overview = memnew(ItemList);
	help_overview->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	help_overview->set_theme_type_variation("ItemListSecondary");
	overview_vbox->add_child(help_overview);
	help_overview->set_allow_reselect(true);
	help_overview->set_custom_minimum_size(Size2(0, 60) * EDSCALE);
	help_overview->set_v_size_flags(SIZE_EXPAND_FILL);

	VBoxContainer *code_editor_container = memnew(VBoxContainer);
	script_split->add_child(code_editor_container);

	tab_container = memnew(TabContainer);
	tab_container->set_tabs_visible(false);
	tab_container->set_accessibility_name(TTRC("Script Tabs"));
	tab_container->set_custom_minimum_size(Size2(200, 0) * EDSCALE);
	code_editor_container->add_child(tab_container);
	tab_container->set_h_size_flags(SIZE_EXPAND_FILL);
	tab_container->set_v_size_flags(SIZE_EXPAND_FILL);

	find_replace_bar = memnew(FindReplaceBar);
	code_editor_container->add_child(find_replace_bar);
	find_replace_bar->hide();

	ED_SHORTCUT("script_editor/window_sort", TTRC("Sort"));
	ED_SHORTCUT("script_editor/window_move_up", TTRC("Move Up"), KeyModifierMask::SHIFT | KeyModifierMask::ALT | Key::UP);
	ED_SHORTCUT("script_editor/window_move_down", TTRC("Move Down"), KeyModifierMask::SHIFT | KeyModifierMask::ALT | Key::DOWN);
	ED_SHORTCUT("script_editor/next_script", TTRC("Next Script"), KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::PERIOD);
	ED_SHORTCUT("script_editor/prev_script", TTRC("Previous Script"), KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::COMMA);
	set_process_input(true);
	set_process_shortcut_input(true);
	set_accessibility_name(TTRC("Script Editor"));

	tab_container->connect("tab_changed", callable_mp(this, &ScriptEditorView::_tab_changed));

	erase_tab_confirm = memnew(ConfirmationDialog);
	erase_tab_confirm->set_ok_button_text(TTRC("Save"));
	erase_tab_confirm->add_button(TTRC("Discard"), DisplayServer::get_singleton()->get_swap_cancel_ok(), "discard");
	erase_tab_confirm->connect(SceneStringName(confirmed), callable_mp(this, &ScriptEditorView::_close_current_tab).bind(true, true));
	erase_tab_confirm->connect("custom_action", callable_mp(this, &ScriptEditorView::_close_discard_current_tab));
	add_child(erase_tab_confirm);

	grab_focus_block = false;
	history_pos = -1;
	edit_pass = 0;

	controller->register_view(this);
	if (controller->get_focused_view() == nullptr) {
		controller->set_focused_view(this);
	}
}

void ScriptEditorView::setup_view_chrome(WindowWrapper *p_wrapper) {
	window_wrapper = p_wrapper;

	file_menu = memnew(MenuButton);
	file_menu->set_flat(false);
	file_menu->set_theme_type_variation("FlatMenuButton");
	file_menu->set_text(TTRC("File"));
	file_menu->set_switch_on_hover(true);
	file_menu->set_shortcut_context(this);
	menu_hb->add_child(file_menu);

	file_menu->get_popup()->add_shortcut(ED_SHORTCUT("script_editor/new", TTRC("New Script..."), KeyModifierMask::CMD_OR_CTRL | Key::N), FILE_MENU_NEW);
	file_menu->get_popup()->add_shortcut(ED_SHORTCUT("script_editor/new_textfile", TTRC("New Text File..."), KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::N), FILE_MENU_NEW_TEXTFILE);
	file_menu->get_popup()->add_shortcut(ED_SHORTCUT("script_editor/open", TTRC("Open...")), FILE_MENU_OPEN);
	file_menu->get_popup()->add_shortcut(ED_GET_SHORTCUT("script_editor/reopen_closed_script"), FILE_MENU_REOPEN_CLOSED);

	recent_scripts = memnew(PopupMenu);
	recent_scripts->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	file_menu->get_popup()->add_submenu_node_item(TTRC("Open Recent"), recent_scripts, FILE_MENU_OPEN_RECENT);
	recent_scripts->connect(SceneStringName(id_pressed), callable_mp(this, &ScriptEditorView::_open_recent_script));
	_update_recent_scripts();

	file_menu->get_popup()->add_separator();
	file_menu->get_popup()->add_shortcut(ED_SHORTCUT("script_editor/save", TTRC("Save"), KeyModifierMask::ALT | KeyModifierMask::CMD_OR_CTRL | Key::S), FILE_MENU_SAVE);
	file_menu->get_popup()->add_shortcut(ED_SHORTCUT("script_editor/save_as", TTRC("Save As...")), FILE_MENU_SAVE_AS);
	file_menu->get_popup()->add_shortcut(ED_SHORTCUT("script_editor/save_all", TTRC("Save All"), KeyModifierMask::SHIFT | KeyModifierMask::ALT | Key::S), FILE_MENU_SAVE_ALL);
	ED_SHORTCUT_OVERRIDE("script_editor/save_all", "macos", KeyModifierMask::META | KeyModifierMask::CTRL | Key::S);
	file_menu->get_popup()->add_separator();
	file_menu->get_popup()->add_shortcut(ED_SHORTCUT("script_editor/reload_script_soft", TTRC("Soft Reload Tool Script"), KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::ALT | Key::R), FILE_MENU_SOFT_RELOAD_TOOL);
	file_menu->get_popup()->add_shortcut(ED_SHORTCUT("script_editor/copy_path", TTRC("Copy Script Path")), FILE_MENU_COPY_PATH);
	file_menu->get_popup()->add_shortcut(ED_SHORTCUT("script_editor/copy_uid", TTRC("Copy Script UID")), FILE_MENU_COPY_UID);
	file_menu->get_popup()->add_shortcut(ED_SHORTCUT("script_editor/show_in_file_system", TTRC("Show in FileSystem")), FILE_MENU_SHOW_IN_FILE_SYSTEM);
	file_menu->get_popup()->add_separator();
	file_menu->get_popup()->add_shortcut(ED_SHORTCUT_ARRAY("script_editor/history_previous", TTRC("History Previous"), { int32_t(KeyModifierMask::ALT | Key::LEFT), int32_t(Key::BACK) }), FILE_MENU_HISTORY_PREV);
	file_menu->get_popup()->add_shortcut(ED_SHORTCUT_ARRAY("script_editor/history_next", TTRC("History Next"), { int32_t(KeyModifierMask::ALT | Key::RIGHT), int32_t(Key::FORWARD) }), FILE_MENU_HISTORY_NEXT);
	ED_SHORTCUT_OVERRIDE("script_editor/history_previous", "macos", KeyModifierMask::ALT | KeyModifierMask::META | Key::LEFT);
	ED_SHORTCUT_OVERRIDE("script_editor/history_next", "macos", KeyModifierMask::ALT | KeyModifierMask::META | Key::RIGHT);
	file_menu->get_popup()->add_separator();

	theme_submenu = memnew(PopupMenu);
	theme_submenu->add_shortcut(ED_SHORTCUT("script_editor/import_theme", TTRC("Import Theme...")), THEME_IMPORT);
	theme_submenu->add_shortcut(ED_SHORTCUT("script_editor/reload_theme", TTRC("Reload Theme")), THEME_RELOAD);
	file_menu->get_popup()->add_submenu_node_item(TTRC("Theme"), theme_submenu, FILE_MENU_THEME_SUBMENU);
	theme_submenu->connect(SceneStringName(id_pressed), callable_mp(this, &ScriptEditorView::_theme_option));
	theme_submenu->add_separator();
	theme_submenu->add_shortcut(ED_SHORTCUT("script_editor/save_theme_as", TTRC("Save Theme As...")), THEME_SAVE_AS);

	file_menu->get_popup()->add_separator();
	file_menu->get_popup()->add_shortcut(ED_SHORTCUT("script_editor/close_file", TTRC("Close"), KeyModifierMask::CMD_OR_CTRL | Key::W), FILE_MENU_CLOSE);
	file_menu->get_popup()->add_shortcut(ED_SHORTCUT("script_editor/close_all", TTRC("Close All")), FILE_MENU_CLOSE_ALL);
	file_menu->get_popup()->add_shortcut(ED_SHORTCUT("script_editor/close_other_tabs", TTRC("Close Other Tabs")), FILE_MENU_CLOSE_OTHER_TABS);
	file_menu->get_popup()->add_shortcut(ED_SHORTCUT("script_editor/close_tabs_below", TTRC("Close Tabs Below")), FILE_MENU_CLOSE_TABS_BELOW);
	file_menu->get_popup()->add_shortcut(ED_SHORTCUT("script_editor/close_docs", TTRC("Close Docs")), FILE_MENU_CLOSE_DOCS);
	file_menu->get_popup()->add_separator();
	file_menu->get_popup()->add_shortcut(ED_SHORTCUT("script_editor/run_file", TTRC("Run"), KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::X), FILE_MENU_RUN);
	file_menu->get_popup()->add_separator();
	file_menu->get_popup()->add_shortcut(ED_SHORTCUT("script_editor/toggle_files_panel", TTRC("Toggle Files Panel"), KeyModifierMask::CMD_OR_CTRL | Key::BACKSLASH), FILE_MENU_TOGGLE_FILES_PANEL);
	file_menu->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &ScriptEditorView::_menu_option));
	file_menu->get_popup()->connect("about_to_popup", callable_mp(this, &ScriptEditorView::_prepare_file_menu));
	file_menu->get_popup()->connect("popup_hide", callable_mp(this, &ScriptEditorView::_file_menu_closed));

	script_search_menu = memnew(MenuButton);
	script_search_menu->set_flat(false);
	script_search_menu->set_theme_type_variation("FlatMenuButton");
	script_search_menu->set_text(TTRC("Search"));
	script_search_menu->set_switch_on_hover(true);
	script_search_menu->set_shortcut_context(this);
	script_search_menu->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &ScriptEditorView::_menu_option));
	menu_hb->add_child(script_search_menu);

	MenuButton *debug_menu_btn = memnew(MenuButton);
	debug_menu_btn->set_flat(false);
	debug_menu_btn->set_theme_type_variation("FlatMenuButton");
	menu_hb->add_child(debug_menu_btn);
	debug_menu_btn->hide();

	if (EditorDebuggerNode *debugger = EditorDebuggerNode::get_singleton()) {
		debugger->set_script_debug_button(debug_menu_btn);
		debugger->connect("goto_script_line", callable_mp(controller, &ScriptEditorController::_goto_script_line));
		debugger->connect("set_execution", callable_mp(controller, &ScriptEditorController::_set_execution));
		debugger->connect("clear_execution", callable_mp(controller, &ScriptEditorController::_clear_execution));
		debugger->connect("breaked", callable_mp(controller, &ScriptEditorController::_breaked));
		debugger->connect("breakpoint_set_in_tree", callable_mp(controller, &ScriptEditorController::_set_breakpoint));
		debugger->connect("breakpoints_cleared_in_tree", callable_mp(controller, &ScriptEditorController::_clear_breakpoints));
	}

	script_name_label = memnew(Label);
	script_name_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	script_name_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	script_name_label->set_focus_mode(FOCUS_ACCESSIBILITY);
	script_name_label->set_h_size_flags(SIZE_EXPAND_FILL);
	menu_hb->add_child(script_name_label);

	site_search = memnew(Button);
	site_search->set_theme_type_variation(SceneStringName(FlatButton));
	site_search->set_accessibility_name(TTRC("Site Search"));
	site_search->connect(SceneStringName(pressed), callable_mp(this, &ScriptEditorView::_menu_option).bind(SEARCH_WEBSITE));
	menu_hb->add_child(site_search);

	help_search = memnew(Button);
	help_search->set_theme_type_variation(SceneStringName(FlatButton));
	help_search->set_text(TTRC("Search Help"));
	help_search->connect(SceneStringName(pressed), callable_mp(this, &ScriptEditorView::_menu_option).bind(SEARCH_HELP));
	menu_hb->add_child(help_search);
	help_search->set_tooltip_text(TTRC("Search the reference documentation."));

	menu_hb->add_child(memnew(VSeparator));

	script_back = memnew(Button);
	script_back->set_theme_type_variation(SceneStringName(FlatButton));
	script_back->set_tooltip_text(TTRC("Go to previous edited document."));
	script_back->set_shortcut(ED_GET_SHORTCUT("script_editor/history_previous"));
	script_back->set_disabled(true);
	menu_hb->add_child(script_back);
	script_back->connect(SceneStringName(pressed), callable_mp(this, &ScriptEditorView::_history_back));

	script_forward = memnew(Button);
	script_forward->set_theme_type_variation(SceneStringName(FlatButton));
	script_forward->set_tooltip_text(TTRC("Go to next edited document."));
	script_forward->set_shortcut(ED_GET_SHORTCUT("script_editor/history_next"));
	script_forward->set_disabled(true);
	menu_hb->add_child(script_forward);
	script_forward->connect(SceneStringName(pressed), callable_mp(this, &ScriptEditorView::_history_forward));

	menu_hb->add_child(memnew(VSeparator));

	if (p_wrapper) {
		make_floating = memnew(ScreenSelect);
		make_floating->set_tooltip_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
		make_floating->connect("request_open_in_screen", callable_mp(p_wrapper, &WindowWrapper::enable_window_on_screen).bind(true));
		menu_hb->add_child(make_floating);
		p_wrapper->connect("window_visibility_changed", callable_mp(this, &ScriptEditorView::_window_changed));
	}

	_update_online_doc();
}


void ScriptEditorView::collect_breakpoints(List<String> *p_breakpoints) const {
	ERR_FAIL_NULL(p_breakpoints);
	HashSet<String> loaded_scripts;
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));
		if (!se) {
			continue;
		}
		Ref<Script> scr = se->get_edited_resource();
		if (scr.is_null()) {
			continue;
		}
		String base = scr->get_path();
		loaded_scripts.insert(base);
		if (base.is_empty() || base.begins_with("local://")) {
			continue;
		}
		PackedInt32Array bpoints = se->get_breakpoints();
		for (int32_t bpoint : bpoints) {
			p_breakpoints->push_back(base + ":" + itos((int)bpoint + 1));
		}
	}
}

PackedStringArray ScriptEditorView::collect_unsaved_scripts() const {
	PackedStringArray unsaved_list;
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));
		if (se && se->is_unsaved()) {
			unsaved_list.append(se->get_name());
		}
	}
	return unsaved_list;
}

Vector<Ref<Script>> ScriptEditorView::collect_open_scripts() const {
	Vector<Ref<Script>> ret;
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));
		if (!se) {
			continue;
		}
		Ref<Script> scr = se->get_edited_resource();
		if (scr.is_valid()) {
			ret.push_back(scr);
		}
	}
	return ret;
}

void ScriptEditorView::sync_script_leaf_path() {
	if (!script_leaf) {
		return;
	}
	String path;
	int line = 0;
	int column = 0;
	if (get_current_script_view_state(path, line, column) && !path.is_empty()) {
		script_leaf->set_script_path(path);
	}
}

void ScriptEditorView::get_view_layout(Ref<ConfigFile> p_layout, const String &p_section) const {
	ERR_FAIL_COND(p_layout.is_null());

	Array scripts;
	Array helps;
	String selected_script;
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));
		if (se) {
			const String path = se->get_edited_resource()->get_path();
			if (path.is_empty()) {
				continue;
			}
			if (tab_container->get_current_tab_control() == tab_container->get_tab_control(i)) {
				selected_script = path;
			}
			Dictionary script_info;
			script_info["path"] = path;
			script_info["state"] = se->get_edit_state();
			scripts.push_back(script_info);
		}

		EditorHelp *eh = Object::cast_to<EditorHelp>(tab_container->get_tab_control(i));
		if (eh) {
			helps.push_back(eh->get_class());
		}
	}

	p_layout->set_value(p_section, "open_scripts", scripts);
	p_layout->set_value(p_section, "selected_script", selected_script);
	p_layout->set_value(p_section, "open_help", helps);
	p_layout->set_value(p_section, "script_split_offset", script_split->get_split_offset());
	p_layout->set_value(p_section, "list_split_offset", list_split->get_split_offset());
	p_layout->set_value(p_section, "zoom_factor", zoom_factor);
}

void ScriptEditorView::set_view_layout(const Ref<ConfigFile> &p_layout, const String &p_section) {
	ERR_FAIL_COND(p_layout.is_null());
	if (!bool(EDITOR_GET("text_editor/behavior/files/restore_scripts_on_load"))) {
		return;
	}
	if (!p_layout->has_section_key(p_section, "open_scripts") && !p_layout->has_section_key(p_section, "open_help")) {
		return;
	}

	Array scripts = p_layout->get_value(p_section, "open_scripts", Array());
	Array helps = p_layout->get_value(p_section, "open_help", Array());

	restoring_layout = true;

	HashSet<String> loaded_scripts;
	List<String> extensions;
	ResourceLoader::get_recognized_extensions_for_type("Script", &extensions);
	ResourceLoader::get_recognized_extensions_for_type("JSON", &extensions);

	for (const Variant &v : scripts) {
		String path = v;
		Dictionary script_info = v;
		if (!script_info.is_empty()) {
			path = script_info["path"];
		}

		if (!_script_exists(path)) {
			if (controller->get_script_editor_cache()->has_section(path)) {
				controller->get_script_editor_cache()->erase_section(path);
			}
			continue;
		} else if (!path.is_resource_file()) {
			if (EditorNode *editor_node = EditorNode::get_singleton()) {
				if (!editor_node->is_scene_open(path.get_slice("::", 0))) {
					continue;
				}
			}
		}
		loaded_scripts.insert(path);

		bool is_script = false;
		if (path.is_resource_file()) {
			is_script = extensions.find(path.get_extension());
		} else {
			Ref<Script> scr = ResourceCache::get_ref(path);
			if (scr.is_valid()) {
				is_script = true;
			}
		}

		if (is_script) {
			Ref<Resource> scr = ResourceLoader::load(path);
			if (scr.is_null()) {
				continue;
			}
			if (!edit(scr, false)) {
				continue;
			}
		} else {
			Error error;
			Ref<TextFile> text_file = _load_text_file(path, &error);
			if (error != OK || text_file.is_null()) {
				continue;
			}
			if (!edit(text_file, false)) {
				continue;
			}
		}

		if (!script_info.is_empty()) {
			ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(tab_container->get_tab_count() - 1));
			if (se) {
				se->set_edit_state(script_info["state"]);
			}
		}
	}

	for (int i = 0; i < helps.size(); i++) {
		String path = helps[i];
		if (path.is_empty()) {
			continue;
		}
		_help_class_open(path);
	}

	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		tab_container->get_tab_control(i)->set_meta("__editor_pass", Variant());
	}

	if (p_layout->has_section_key(p_section, "script_split_offset")) {
		script_split->set_split_offset(p_layout->get_value(p_section, "script_split_offset"));
	}
	if (p_layout->has_section_key(p_section, "list_split_offset")) {
		list_split->set_split_offset(p_layout->get_value(p_section, "list_split_offset"));
	}

	_set_script_zoom_factor(p_layout->get_value(p_section, "zoom_factor", 1.0f));
	restoring_layout = false;
	_update_script_names();

	if (p_layout->has_section_key(p_section, "selected_script")) {
		String selected_script = p_layout->get_value(p_section, "selected_script");
		for (int i = 0; i < tab_container->get_tab_count(); i++) {
			ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tab_container->get_tab_control(i));
			if (se && se->get_edited_resource()->get_path() == selected_script) {
				_go_to_tab(i);
				break;
			}
		}
	}

	sync_script_leaf_path();
}

void ScriptEditorView::_on_focus_entered() {
	controller->set_focused_view(this);
}

void ScriptEditorView::_bind_methods() {
	ClassDB::bind_method("_help_tab_goto", &ScriptEditorView::_help_tab_goto);
	ClassDB::bind_method("get_current_editor", &ScriptEditorView::_get_current_editor);
	ClassDB::bind_method("get_open_script_editors", &ScriptEditorView::_get_open_script_editors);
	ClassDB::bind_method(D_METHOD("get_current_script"), &ScriptEditorView::_get_current_script);
	ClassDB::bind_method(D_METHOD("get_open_scripts"), &ScriptEditorView::_get_open_scripts);
	ClassDB::bind_method(D_METHOD("goto_line", "line_number"), &ScriptEditorView::_goto_script_line2);
	ClassDB::bind_method(D_METHOD("_set_refactor_file_source", "path", "source", "source_is_saved_version"), &ScriptEditorView::_set_refactor_file_source);
}
