/**************************************************************************/
/*  script_editor_plugin.cpp                                              */
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

#include "script_editor_plugin.h"

#include "core/config/project_build_pipeline_config.h"
#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/os/keyboard.h"
#include "core/os/os.h"
#include "core/string/fuzzy_search.h"
#include "core/version.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/doc/editor_help_search.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/docks/inspector_dock.h"
#include "editor/docks/signals_dock.h"
#include "editor/editor_interface.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/editor_scene_workspace.h"
#include "editor/editor_script_leaf.h"
#include "editor/script/script_editor_controller.h"
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

/*** SYNTAX HIGHLIGHTER ****/

String EditorSyntaxHighlighter::_get_name() const {
	String ret = "Unnamed";
	FOUNDRY_VIRTUAL_CALL(_get_name, ret);
	return ret;
}

PackedStringArray EditorSyntaxHighlighter::_get_supported_languages() const {
	PackedStringArray ret;
	FOUNDRY_VIRTUAL_CALL(_get_supported_languages, ret);
	return ret;
}

Ref<EditorSyntaxHighlighter> EditorSyntaxHighlighter::_create() const {
	Ref<EditorSyntaxHighlighter> syntax_highlighter;
	if (FOUNDRY_VIRTUAL_IS_OVERRIDDEN(_create)) {
		FOUNDRY_VIRTUAL_CALL(_create, syntax_highlighter);
	} else {
		syntax_highlighter.instantiate();
		if (get_script_instance()) {
			syntax_highlighter->set_script(get_script_instance()->get_script());
		}
	}
	return syntax_highlighter;
}

void EditorSyntaxHighlighter::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_get_edited_resource"), &EditorSyntaxHighlighter::_get_edited_resource);

	FOUNDRY_VIRTUAL_BIND(_get_name)
	FOUNDRY_VIRTUAL_BIND(_get_supported_languages)
	FOUNDRY_VIRTUAL_BIND(_create)
}

////

void EditorStandardSyntaxHighlighter::_update_cache() {
	highlighter->set_text_edit(text_edit);
	highlighter->clear_keyword_colors();
	highlighter->clear_member_keyword_colors();
	highlighter->clear_color_regions();

	highlighter->set_symbol_color(EDITOR_GET("text_editor/theme/highlighting/symbol_color"));
	highlighter->set_function_color(EDITOR_GET("text_editor/theme/highlighting/function_color"));
	highlighter->set_number_color(EDITOR_GET("text_editor/theme/highlighting/number_color"));
	highlighter->set_member_variable_color(EDITOR_GET("text_editor/theme/highlighting/member_variable_color"));

	/* Engine types. */
	const Color type_color = EDITOR_GET("text_editor/theme/highlighting/engine_type_color");
	LocalVector<StringName> types;
	ClassDB::get_class_list(types);
	for (const StringName &type : types) {
		highlighter->add_keyword_color(type, type_color);
	}

	/* User types. */
	const Color usertype_color = EDITOR_GET("text_editor/theme/highlighting/user_type_color");
	LocalVector<StringName> global_classes;
	ScriptServer::get_global_class_list(global_classes);
	for (const StringName &class_name : global_classes) {
		highlighter->add_keyword_color(class_name, usertype_color);
	}

	/* Autoloads. */
#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
	FSAutoloadIndex autoload_index;
	autoload_index.rebuild_from_project_settings();
	for (const FSAutoloadIndexEntry &autoload : autoload_index.get_entries()) {
		if (autoload.is_singleton) {
			highlighter->add_keyword_color(autoload.name, usertype_color);
		}
	}
#else
	HashMap<StringName, ProjectSettings::AutoloadInfo> autoloads = ProjectSettings::get_singleton()->get_autoload_list();
	for (const KeyValue<StringName, ProjectSettings::AutoloadInfo> &E : autoloads) {
		const ProjectSettings::AutoloadInfo &info = E.value;
		if (info.is_singleton) {
			highlighter->add_keyword_color(info.name, usertype_color);
		}
	}
#endif

	const ScriptLanguage *scr_lang = script_language;
	StringName instance_base;

	if (scr_lang == nullptr) {
		const Ref<Script> scr = _get_edited_resource();
		if (scr.is_valid()) {
			scr_lang = scr->get_language();
			instance_base = scr->get_instance_base_type();
		}
	}

	if (scr_lang != nullptr) {
		/* Core types. */
		const Color basetype_color = EDITOR_GET("text_editor/theme/highlighting/base_type_color");
		List<String> core_types;
		scr_lang->get_core_type_words(&core_types);
		for (const String &E : core_types) {
			highlighter->add_keyword_color(E, basetype_color);
		}

		/* Reserved words. */
		const Color keyword_color = EDITOR_GET("text_editor/theme/highlighting/keyword_color");
		const Color control_flow_keyword_color = EDITOR_GET("text_editor/theme/highlighting/control_flow_keyword_color");
		for (const String &keyword : scr_lang->get_reserved_words()) {
			if (scr_lang->is_control_flow_keyword(keyword)) {
				highlighter->add_keyword_color(keyword, control_flow_keyword_color);
			} else {
				highlighter->add_keyword_color(keyword, keyword_color);
			}
		}

		/* Member types. */
		const Color member_variable_color = EDITOR_GET("text_editor/theme/highlighting/member_variable_color");
		if (instance_base != StringName()) {
			List<PropertyInfo> plist;
			ClassDB::get_property_list(instance_base, &plist);
			for (const PropertyInfo &E : plist) {
				String prop_name = E.name;
				if (E.usage & PROPERTY_USAGE_CATEGORY || E.usage & PROPERTY_USAGE_GROUP || E.usage & PROPERTY_USAGE_SUBGROUP) {
					continue;
				}
				if (prop_name.contains_char('/')) {
					continue;
				}
				highlighter->add_member_keyword_color(prop_name, member_variable_color);
			}

			List<String> clist;
			ClassDB::get_integer_constant_list(instance_base, &clist);
			for (const String &E : clist) {
				highlighter->add_member_keyword_color(E, member_variable_color);
			}
		}

		/* Comments */
		const Color comment_color = EDITOR_GET("text_editor/theme/highlighting/comment_color");
		for (const String &comment : scr_lang->get_comment_delimiters()) {
			String beg = comment.get_slicec(' ', 0);
			String end = comment.get_slice_count(" ") > 1 ? comment.get_slicec(' ', 1) : String();
			highlighter->add_color_region(beg, end, comment_color, end.is_empty());
		}

		/* Doc comments */
		const Color doc_comment_color = EDITOR_GET("text_editor/theme/highlighting/doc_comment_color");
		for (const String &doc_comment : scr_lang->get_doc_comment_delimiters()) {
			String beg = doc_comment.get_slicec(' ', 0);
			String end = doc_comment.get_slice_count(" ") > 1 ? doc_comment.get_slicec(' ', 1) : String();
			highlighter->add_color_region(beg, end, doc_comment_color, end.is_empty());
		}

		/* Strings */
		const Color string_color = EDITOR_GET("text_editor/theme/highlighting/string_color");
		for (const String &string : scr_lang->get_string_delimiters()) {
			String beg = string.get_slicec(' ', 0);
			String end = string.get_slice_count(" ") > 1 ? string.get_slicec(' ', 1) : String();
			highlighter->add_color_region(beg, end, string_color, end.is_empty());
		}
	}
}

Ref<EditorSyntaxHighlighter> EditorStandardSyntaxHighlighter::_create() const {
	Ref<EditorStandardSyntaxHighlighter> syntax_highlighter;
	syntax_highlighter.instantiate();
	return syntax_highlighter;
}

////

Ref<EditorSyntaxHighlighter> EditorPlainTextSyntaxHighlighter::_create() const {
	Ref<EditorPlainTextSyntaxHighlighter> syntax_highlighter;
	syntax_highlighter.instantiate();
	return syntax_highlighter;
}

////

void EditorJSONSyntaxHighlighter::_update_cache() {
	highlighter->set_text_edit(text_edit);
	highlighter->clear_keyword_colors();
	highlighter->clear_member_keyword_colors();
	highlighter->clear_color_regions();

	highlighter->set_symbol_color(EDITOR_GET("text_editor/theme/highlighting/symbol_color"));
	highlighter->set_number_color(EDITOR_GET("text_editor/theme/highlighting/number_color"));

	const Color string_color = EDITOR_GET("text_editor/theme/highlighting/string_color");
	highlighter->add_color_region("\"", "\"", string_color);
}

Ref<EditorSyntaxHighlighter> EditorJSONSyntaxHighlighter::_create() const {
	Ref<EditorJSONSyntaxHighlighter> syntax_highlighter;
	syntax_highlighter.instantiate();
	return syntax_highlighter;
}

////

void EditorMarkdownSyntaxHighlighter::_update_cache() {
	highlighter->set_text_edit(text_edit);
	highlighter->clear_keyword_colors();
	highlighter->clear_member_keyword_colors();
	highlighter->clear_color_regions();

	// Disable automatic symbolic highlights, as these don't make sense for prose.
	highlighter->set_symbol_color(EDITOR_GET("text_editor/theme/highlighting/text_color"));
	highlighter->set_number_color(EDITOR_GET("text_editor/theme/highlighting/text_color"));
	highlighter->set_member_variable_color(EDITOR_GET("text_editor/theme/highlighting/text_color"));
	highlighter->set_function_color(EDITOR_GET("text_editor/theme/highlighting/text_color"));

	// Headings (any level).
	const Color function_color = EDITOR_GET("text_editor/theme/highlighting/function_color");
	highlighter->add_color_region("#", "", function_color);

	// Bold.
	highlighter->add_color_region("**", "**", function_color);
	// `__bold__` syntax is not supported as color regions must begin with a symbol,
	// not a character that is valid in an identifier.

	// Code (both inline code and triple-backticks code blocks).
	const Color code_color = EDITOR_GET("text_editor/theme/highlighting/engine_type_color");
	highlighter->add_color_region("`", "`", code_color);

	// Link (both references and inline links with URLs). The URL is not highlighted.
	const Color link_color = EDITOR_GET("text_editor/theme/highlighting/keyword_color");
	highlighter->add_color_region("[", "]", link_color);

	// Quote.
	const Color quote_color = EDITOR_GET("text_editor/theme/highlighting/string_color");
	highlighter->add_color_region(">", "", quote_color, true);

	// HTML comment, which is also supported in Markdown.
	const Color comment_color = EDITOR_GET("text_editor/theme/highlighting/comment_color");
	highlighter->add_color_region("<!--", "-->", comment_color);
}

Ref<EditorSyntaxHighlighter> EditorMarkdownSyntaxHighlighter::_create() const {
	Ref<EditorMarkdownSyntaxHighlighter> syntax_highlighter;
	syntax_highlighter.instantiate();
	return syntax_highlighter;
}

///

void EditorConfigFileSyntaxHighlighter::_update_cache() {
	highlighter->set_text_edit(text_edit);
	highlighter->clear_keyword_colors();
	highlighter->clear_member_keyword_colors();
	highlighter->clear_color_regions();

	highlighter->set_symbol_color(EDITOR_GET("text_editor/theme/highlighting/symbol_color"));
	highlighter->set_number_color(EDITOR_GET("text_editor/theme/highlighting/number_color"));
	// Assume that all function-style syntax is for types such as `Vector2()` and `PackedStringArray()`.
	highlighter->set_function_color(EDITOR_GET("text_editor/theme/highlighting/base_type_color"));

	// Disable member variable highlighting as it's not relevant for ConfigFile.
	highlighter->set_member_variable_color(EDITOR_GET("text_editor/theme/highlighting/text_color"));

	const Color string_color = EDITOR_GET("text_editor/theme/highlighting/string_color");
	highlighter->add_color_region("\"", "\"", string_color);

	// FIXME: Sections in ConfigFile must be at the beginning of a line. Otherwise, it can be an array within a line.
	const Color function_color = EDITOR_GET("text_editor/theme/highlighting/function_color");
	highlighter->add_color_region("[", "]", function_color);

	const Color keyword_color = EDITOR_GET("text_editor/theme/highlighting/keyword_color");
	highlighter->add_keyword_color("true", keyword_color);
	highlighter->add_keyword_color("false", keyword_color);
	highlighter->add_keyword_color("null", keyword_color);
	highlighter->add_keyword_color("ExtResource", keyword_color);
	highlighter->add_keyword_color("SubResource", keyword_color);

	const Color comment_color = EDITOR_GET("text_editor/theme/highlighting/comment_color");
	highlighter->add_color_region(";", "", comment_color);
}

Ref<EditorSyntaxHighlighter> EditorConfigFileSyntaxHighlighter::_create() const {
	Ref<EditorConfigFileSyntaxHighlighter> syntax_highlighter;
	syntax_highlighter.instantiate();
	return syntax_highlighter;
}

////////////////////////////////////////////////////////////////////////////////

/*** SCRIPT EDITOR ****/

void ScriptEditorBase::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_base_editor"), &ScriptEditorBase::get_base_editor);
	ClassDB::bind_method(D_METHOD("add_syntax_highlighter", "highlighter"), &ScriptEditorBase::add_syntax_highlighter);

	ADD_SIGNAL(MethodInfo("name_changed"));
	ADD_SIGNAL(MethodInfo("edited_script_changed"));
	ADD_SIGNAL(MethodInfo("request_help", PropertyInfo(Variant::STRING, "topic")));
	ADD_SIGNAL(MethodInfo("request_open_script_at_line", PropertyInfo(Variant::OBJECT, "script"), PropertyInfo(Variant::INT, "line")));
	ADD_SIGNAL(MethodInfo("request_save_history"));
	ADD_SIGNAL(MethodInfo("request_save_previous_state", PropertyInfo(Variant::DICTIONARY, "state")));
	ADD_SIGNAL(MethodInfo("go_to_help", PropertyInfo(Variant::STRING, "what")));
	ADD_SIGNAL(MethodInfo("search_in_files_requested", PropertyInfo(Variant::STRING, "text")));
	ADD_SIGNAL(MethodInfo("replace_in_files_requested", PropertyInfo(Variant::STRING, "text")));
	ADD_SIGNAL(MethodInfo("go_to_method", PropertyInfo(Variant::OBJECT, "script"), PropertyInfo(Variant::STRING, "method")));
}

void ScriptEditorQuickOpen::popup_dialog(const Vector<String> &p_functions, bool p_dontclear) {
	popup_centered_ratio(0.6);
	if (p_dontclear) {
		search_box->select_all();
	} else {
		search_box->clear();
	}
	search_box->grab_focus();
	functions = p_functions;
	_update_search();
}

void ScriptEditorQuickOpen::_text_changed(const String &p_newtext) {
	_update_search();
}

void ScriptEditorQuickOpen::_sbox_input(const Ref<InputEvent> &p_event) {
	// Redirect navigational key events to the tree.
	Ref<InputEventKey> key = p_event;
	if (key.is_valid()) {
		if (key->is_action("ui_up", true) || key->is_action("ui_down", true) || key->is_action("ui_page_up") || key->is_action("ui_page_down")) {
			search_options->gui_input(key);
			search_box->accept_event();
		}
	}
}

void ScriptEditorQuickOpen::_update_search() {
	search_options->clear();
	TreeItem *root = search_options->create_item();

	for (int i = 0; i < functions.size(); i++) {
		String file = functions[i];
		if ((search_box->get_text().is_empty() || file.containsn(search_box->get_text()))) {
			TreeItem *ti = search_options->create_item(root);
			ti->set_text(0, file);
			if (root->get_first_child() == ti) {
				ti->select(0);
			}
		}
	}

	get_ok_button()->set_disabled(root->get_first_child() == nullptr);
}

void ScriptEditorQuickOpen::_confirmed() {
	TreeItem *ti = search_options->get_selected();
	if (!ti) {
		return;
	}
	int line = ti->get_text(0).get_slicec(':', 1).to_int();

	emit_signal(SNAME("goto_line"), line - 1);
	hide();
}

void ScriptEditorQuickOpen::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			connect(SceneStringName(confirmed), callable_mp(this, &ScriptEditorQuickOpen::_confirmed));

			search_box->set_clear_button_enabled(true);
			[[fallthrough]];
		}
		case NOTIFICATION_VISIBILITY_CHANGED: {
			search_box->set_right_icon(search_options->get_editor_theme_icon(SNAME("Search")));
		} break;

		case NOTIFICATION_EXIT_TREE: {
			disconnect(SceneStringName(confirmed), callable_mp(this, &ScriptEditorQuickOpen::_confirmed));
		} break;
	}
}

void ScriptEditorQuickOpen::_bind_methods() {
	ADD_SIGNAL(MethodInfo("goto_line", PropertyInfo(Variant::INT, "line")));
}

ScriptEditorQuickOpen::ScriptEditorQuickOpen() {
	VBoxContainer *vbc = memnew(VBoxContainer);
	add_child(vbc);
	search_box = memnew(LineEdit);
	vbc->add_margin_child(TTRC("Search:"), search_box);
	search_box->connect(SceneStringName(text_changed), callable_mp(this, &ScriptEditorQuickOpen::_text_changed));
	search_box->connect(SceneStringName(gui_input), callable_mp(this, &ScriptEditorQuickOpen::_sbox_input));
	search_options = memnew(Tree);
	vbc->add_margin_child(TTRC("Matches:"), search_options, true);
	set_ok_button_text(TTRC("Open"));
	get_ok_button()->set_disabled(true);
	register_text_enter(search_box);
	set_hide_on_ok(false);
	search_options->connect("item_activated", callable_mp(this, &ScriptEditorQuickOpen::_confirmed));
	search_options->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	search_options->set_hide_root(true);
	search_options->set_hide_folding(true);
	search_options->add_theme_constant_override("draw_guides", 1);
}


/*** SCRIPT EDITOR split to script_editor_controller/view ***/


void ScriptEditorPlugin::_save_last_editor(const String &p_editor) {
	if (p_editor != get_plugin_name()) {
		last_editor = p_editor;
	}
}

void ScriptEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			connect("main_screen_changed", callable_mp(this, &ScriptEditorPlugin::_save_last_editor));
		} break;
		case NOTIFICATION_EXIT_TREE: {
			disconnect("main_screen_changed", callable_mp(this, &ScriptEditorPlugin::_save_last_editor));
		} break;
		default: {
		} break;
	}
}

bool ScriptEditorPlugin::open_in_external_editor(const String &p_path, int p_line, int p_col, bool p_ignore_project) {
	const String path = EDITOR_GET("text_editor/external/exec_path");
	if (path.is_empty()) {
		return false;
	}

	String flags = EDITOR_GET("text_editor/external/exec_flags");

	List<String> args;
	bool has_file_flag = false;

	if (!flags.is_empty()) {
		flags = flags.replacen("{line}", itos(MAX(p_line + 1, 1)));
		flags = flags.replacen("{col}", itos(p_col + 1));
		flags = flags.strip_edges().replace("\\\\", "\\");

		int from = 0;
		int num_chars = 0;
		bool inside_quotes = false;

		for (int i = 0; i < flags.size(); i++) {
			if (flags[i] == '"' && (!i || flags[i - 1] != '\\')) {
				if (!inside_quotes) {
					from++;
				}
				inside_quotes = !inside_quotes;

			} else if (flags[i] == '\0' || (!inside_quotes && flags[i] == ' ')) {
				String arg = flags.substr(from, num_chars);
				if (arg.contains("{file}")) {
					has_file_flag = true;
				}

				// Do path replacement here, else there will be issues with spaces and quotes
				if (p_ignore_project) {
					arg = arg.replacen("{project}", String());
				} else {
					arg = arg.replacen("{project}", ProjectSettings::get_singleton()->get_resource_path());
				}
				arg = arg.replacen("{file}", p_path);
				args.push_back(arg);

				from = i + 1;
				num_chars = 0;
			} else {
				num_chars++;
			}
		}
	}

	// Default to passing script path if no {file} flag is specified.
	if (!has_file_flag) {
		args.push_back(p_path);
	}
	return OS::get_singleton()->create_process(path, args) == OK;
}

void ScriptEditorPlugin::edit(Object *p_object) {
	if (Object::cast_to<Script>(p_object)) {
		Script *p_script = Object::cast_to<Script>(p_object);
		String res_path = p_script->get_path().get_slice("::", 0);

		if (p_script->is_built_in() && !res_path.is_empty()) {
			EditorNode::get_singleton()->load_scene_or_resource(res_path, false, false);
		}
		ScriptEditorController::get_singleton()->edit(p_script);
	} else if (Object::cast_to<JSON>(p_object)) {
		ScriptEditorController::get_singleton()->edit(Object::cast_to<JSON>(p_object));
	} else if (Object::cast_to<TextFile>(p_object)) {
		ScriptEditorController::get_singleton()->edit(Object::cast_to<TextFile>(p_object));
	}
}

bool ScriptEditorPlugin::handles(Object *p_object) const {
	if (Object::cast_to<TextFile>(p_object)) {
		return true;
	}

	if (Object::cast_to<Script>(p_object)) {
		return true;
	}

	if (Object::cast_to<JSON>(p_object)) {
		// This is here to stop resource files of class JSON from getting confused
		// with json files and being opened in the text editor.
		if (Object::cast_to<JSON>(p_object)->get_path().get_extension().to_lower() == "json") {
			return true;
		}
	}

	return p_object->is_class("Script");
}

void ScriptEditorPlugin::make_visible(bool p_visible) {
	if (p_visible && ScriptEditorController::get_singleton()) {
		ScriptEditorController::get_singleton()->ensure_select_current();
	}
}

void ScriptEditorPlugin::selected_notify() {
	if (ScriptEditorController::get_singleton()) {
		ScriptEditorController::get_singleton()->ensure_select_current();
	}
}

String ScriptEditorPlugin::get_unsaved_status(const String &p_for_scene) const {
	const PackedStringArray unsaved_scripts = ScriptEditorController::get_singleton()->get_unsaved_scripts();
	if (unsaved_scripts.is_empty()) {
		return String();
	}

	PackedStringArray message;
	if (!p_for_scene.is_empty()) {
		PackedStringArray unsaved_built_in_scripts;

		const String scene_file = p_for_scene.get_file();
		for (const String &E : unsaved_scripts) {
			if (!E.is_resource_file() && E.contains(scene_file)) {
				unsaved_built_in_scripts.append(E);
			}
		}

		if (unsaved_built_in_scripts.is_empty()) {
			return String();
		} else {
			message.resize(unsaved_built_in_scripts.size() + 1);
			message.write[0] = TTR("There are unsaved changes in the following built-in script(s):");

			int i = 1;
			for (const String &E : unsaved_built_in_scripts) {
				message.write[i] = E.trim_suffix("(*)");
				i++;
			}
			return String("\n").join(message);
		}
	}

	message.resize(unsaved_scripts.size() + 1);
	message.write[0] = TTR("Save changes to the following script(s) before quitting?");

	int i = 1;
	for (const String &E : unsaved_scripts) {
		message.write[i] = E.trim_suffix("(*)");
		i++;
	}
	return String("\n").join(message);
}

void ScriptEditorPlugin::save_external_data() {
	if (!EditorNode::get_singleton()->is_exiting()) {
		ScriptEditorController::get_singleton()->save_all_scripts();
	}
}

void ScriptEditorPlugin::apply_changes() {
	ScriptEditorController::get_singleton()->apply_scripts();
}

void ScriptEditorPlugin::set_window_layout(Ref<ConfigFile> p_layout) {
	ScriptEditorController *controller = ScriptEditorController::get_singleton();
	if (!controller) {
		return;
	}

	// Legacy single-surface layout: apply to the first script leaf when per-leaf
	// open_scripts were not persisted yet.
	if (p_layout->has_section_key("ScriptEditor", "open_scripts")) {
		bool has_per_leaf_layout = false;
		if (EditorNode::get_singleton() && EditorNode::get_singleton()->get_scene_workspace()) {
			for (WorkspaceLeafNode *leaf : EditorNode::get_singleton()->get_scene_workspace()->get_script_leaves()) {
				ScriptLeaf *script_leaf = Object::cast_to<ScriptLeaf>(leaf->get_leaf_content()->get_root_control());
				if (script_leaf && script_leaf->get_script_editor_view()) {
					const String section = EditorSceneWorkspace::leaf_layout_section(leaf->get_leaf_id());
					if (p_layout->has_section_key(section, "open_scripts")) {
						has_per_leaf_layout = true;
						break;
					}
				}
			}
		}
		if (!has_per_leaf_layout) {
			controller->set_window_layout(p_layout);
		}
	}
}

void ScriptEditorPlugin::get_window_layout(Ref<ConfigFile> p_layout) {
	if (ScriptEditorController *controller = ScriptEditorController::get_singleton()) {
		controller->get_window_layout(p_layout);
	}
}

void ScriptEditorPlugin::get_breakpoints(List<String> *p_breakpoints) {
	ScriptEditorController::get_singleton()->get_breakpoints(p_breakpoints);
}

void ScriptEditorPlugin::edited_scene_changed() {
	ScriptEditorController::get_singleton()->edited_scene_changed();
}

ScriptEditorPlugin::ScriptEditorPlugin() {
	ED_SHORTCUT("script_editor/reopen_closed_script", TTRC("Reopen Closed Script"), KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::T);
	ED_SHORTCUT("script_editor/clear_recent", TTRC("Clear Recent Scripts"));
	ED_SHORTCUT("script_editor/replace_in_files", TTRC("Replace in Files..."), KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::R);

	ED_SHORTCUT("script_text_editor/convert_to_uppercase", TTRC("Uppercase"), KeyModifierMask::SHIFT | Key::F4);
	ED_SHORTCUT("script_text_editor/convert_to_lowercase", TTRC("Lowercase"), KeyModifierMask::SHIFT | Key::F5);
	ED_SHORTCUT("script_text_editor/capitalize", TTRC("Capitalize"), KeyModifierMask::SHIFT | Key::F6);

	if (!ScriptEditorController::get_singleton()) {
		ScriptEditorController *controller = memnew(ScriptEditorController);
		controller->init_global_services(EditorNode::get_singleton());
	}

	ScriptServer::set_reload_scripts_on_save(EDITOR_GET("text_editor/behavior/files/auto_reload_and_parse_scripts_on_save"));
}
