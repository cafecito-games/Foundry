/**************************************************************************/
/*  script_editor_plugin.h                                                */
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
#include "editor/plugins/editor_plugin.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/panel_container.h"
#include "scene/resources/syntax_highlighter.h"
#include "scene/resources/text_file.h"

class CodeTextEditor;
class EditorFileDialog;
class EditorHelpSearch;
class FindReplaceBar;
class HSplitContainer;
class ItemList;
class MenuButton;
class TabContainer;
class TextureRect;
class Tree;
class VSplitContainer;
class WindowWrapper;

class EditorSyntaxHighlighter : public SyntaxHighlighter {
	FOUNDRY_CLASS(EditorSyntaxHighlighter, SyntaxHighlighter)

private:
	Ref<RefCounted> edited_resource;

protected:
	static void _bind_methods();

	FOUNDRY_VIRTUAL0RC(String, _get_name)
	FOUNDRY_VIRTUAL0RC(PackedStringArray, _get_supported_languages)
	FOUNDRY_VIRTUAL0RC(Ref<EditorSyntaxHighlighter>, _create)

public:
	virtual String _get_name() const;
	virtual PackedStringArray _get_supported_languages() const;

	void _set_edited_resource(const Ref<Resource> &p_res) { edited_resource = p_res; }
	Ref<RefCounted> _get_edited_resource() { return edited_resource; }

	virtual Ref<EditorSyntaxHighlighter> _create() const;
};

class EditorStandardSyntaxHighlighter : public EditorSyntaxHighlighter {
	FOUNDRY_CLASS(EditorStandardSyntaxHighlighter, EditorSyntaxHighlighter)

private:
	Ref<CodeHighlighter> highlighter;
	ScriptLanguage *script_language = nullptr; // See GH-89610.

public:
	virtual void _update_cache() override;
	virtual Dictionary _get_line_syntax_highlighting_impl(int p_line) override { return highlighter->get_line_syntax_highlighting(p_line); }

	virtual String _get_name() const override { return TTR("Standard"); }

	virtual Ref<EditorSyntaxHighlighter> _create() const override;

	void _set_script_language(ScriptLanguage *p_script_language) { script_language = p_script_language; }

	EditorStandardSyntaxHighlighter() { highlighter.instantiate(); }
};

class EditorPlainTextSyntaxHighlighter : public EditorSyntaxHighlighter {
	FOUNDRY_CLASS(EditorPlainTextSyntaxHighlighter, EditorSyntaxHighlighter)

public:
	virtual String _get_name() const override { return TTR("Plain Text"); }

	virtual Ref<EditorSyntaxHighlighter> _create() const override;
};

class EditorJSONSyntaxHighlighter : public EditorSyntaxHighlighter {
	FOUNDRY_CLASS(EditorJSONSyntaxHighlighter, EditorSyntaxHighlighter)

private:
	Ref<CodeHighlighter> highlighter;

public:
	virtual void _update_cache() override;
	virtual Dictionary _get_line_syntax_highlighting_impl(int p_line) override { return highlighter->get_line_syntax_highlighting(p_line); }

	virtual PackedStringArray _get_supported_languages() const override { return PackedStringArray{ "json" }; }
	virtual String _get_name() const override { return TTR("JSON"); }

	virtual Ref<EditorSyntaxHighlighter> _create() const override;

	EditorJSONSyntaxHighlighter() { highlighter.instantiate(); }
};

class EditorMarkdownSyntaxHighlighter : public EditorSyntaxHighlighter {
	FOUNDRY_CLASS(EditorMarkdownSyntaxHighlighter, EditorSyntaxHighlighter)

private:
	Ref<CodeHighlighter> highlighter;

public:
	virtual void _update_cache() override;
	virtual Dictionary _get_line_syntax_highlighting_impl(int p_line) override { return highlighter->get_line_syntax_highlighting(p_line); }

	virtual PackedStringArray _get_supported_languages() const override { return PackedStringArray{ "md", "markdown" }; }
	virtual String _get_name() const override { return TTR("Markdown"); }

	virtual Ref<EditorSyntaxHighlighter> _create() const override;

	EditorMarkdownSyntaxHighlighter() { highlighter.instantiate(); }
};

class EditorConfigFileSyntaxHighlighter : public EditorSyntaxHighlighter {
	FOUNDRY_CLASS(EditorConfigFileSyntaxHighlighter, EditorSyntaxHighlighter)

private:
	Ref<CodeHighlighter> highlighter;

public:
	virtual void _update_cache() override;
	virtual Dictionary _get_line_syntax_highlighting_impl(int p_line) override { return highlighter->get_line_syntax_highlighting(p_line); }

	// While not explicitly designed for those formats, this highlighter happens
	// to handle TSCN, TRES, `project.foundry` well. We can expose it in case the
	// user opens one of these using the script editor (which can be done using
	// the All Files filter).
	virtual PackedStringArray _get_supported_languages() const override { return PackedStringArray{ "ini", "cfg", "tscn", "tres", "godot" }; }
	virtual String _get_name() const override { return TTR("ConfigFile"); }

	virtual Ref<EditorSyntaxHighlighter> _create() const override;

	EditorConfigFileSyntaxHighlighter() { highlighter.instantiate(); }
};

///////////////////////////////////////////////////////////////////////////////

class ScriptEditorQuickOpen : public ConfirmationDialog {
	FOUNDRY_CLASS(ScriptEditorQuickOpen, ConfirmationDialog);

	LineEdit *search_box = nullptr;
	Tree *search_options = nullptr;
	String function;

	void _update_search();

	void _sbox_input(const Ref<InputEvent> &p_event);
	Vector<String> functions;

	void _confirmed();
	void _text_changed(const String &p_newtext);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void popup_dialog(const Vector<String> &p_functions, bool p_dontclear = false);
	ScriptEditorQuickOpen();
};

class EditorDebuggerNode;

class ScriptEditorBase : public VBoxContainer {
	FOUNDRY_CLASS(ScriptEditorBase, VBoxContainer);

protected:
	static void _bind_methods();

public:
	struct EditedFileData {
		String path;
		uint64_t last_modified_time = -1;
	} edited_file_data;

	virtual void add_syntax_highlighter(Ref<EditorSyntaxHighlighter> p_highlighter) = 0;
	virtual void set_syntax_highlighter(Ref<EditorSyntaxHighlighter> p_highlighter) = 0;

	virtual void apply_code() = 0;
	virtual Ref<Resource> get_edited_resource() const = 0;
	virtual Vector<String> get_functions() = 0;
	virtual void set_edited_resource(const Ref<Resource> &p_res) = 0;
	virtual void enable_editor(Control *p_shortcut_context = nullptr) = 0;
	virtual void reload_text() = 0;
	virtual String get_name() = 0;
	virtual Ref<Texture2D> get_theme_icon() = 0;
	virtual bool is_unsaved() = 0;
	virtual Variant get_edit_state() = 0;
	virtual void set_edit_state(const Variant &p_state) = 0;
	virtual Variant get_navigation_state() = 0;
	virtual void goto_line(int p_line, int p_column = 0) = 0;
	virtual void set_executing_line(int p_line) = 0;
	virtual void clear_executing_line() = 0;
	virtual void trim_trailing_whitespace() = 0;
	virtual void trim_final_newlines() = 0;
	virtual void insert_final_newline() = 0;
	virtual void convert_indent() = 0;
	// Reformats the whole buffer through the script language's canonical formatter,
	// preserving caret/scroll where practical. No-op for languages without a
	// formatter or on parse error; surfaces the diagnostic only when p_notify_on_error.
	virtual void format_document(bool p_notify_on_error) {}
	virtual void ensure_focus() = 0;
	virtual void tag_saved_version() = 0;
	virtual void reload(bool p_soft) {}
	virtual PackedInt32Array get_breakpoints() = 0;
	virtual void set_breakpoint(int p_line, bool p_enabled) = 0;
	virtual void clear_breakpoints() = 0;
	virtual void add_callback(const String &p_function, const PackedStringArray &p_args) = 0;
	virtual void update_settings() = 0;
	virtual void set_debugger_active(bool p_active) = 0;
	virtual void update_toggle_files_button() {}

	virtual bool show_members_overview() = 0;

	virtual void set_tooltip_request_func(const Callable &p_toolip_callback) = 0;
	virtual Control *get_edit_menu() = 0;
	virtual void clear_edit_menu() = 0;
	virtual void set_find_replace_bar(FindReplaceBar *p_bar) = 0;

	virtual Control *get_base_editor() const = 0;
	virtual CodeTextEditor *get_code_editor() const = 0;

	virtual void validate() = 0;
};

typedef ScriptEditorBase *(*CreateScriptEditorFunc)(const Ref<Resource> &p_resource);

class EditorScriptCodeCompletionCache;
class FindInFilesContainer;
class FindInFilesDialog;
struct ScriptRefactorApplyPlan;

#include "script_editor_controller.h"
#include "script_editor_view.h"

typedef ScriptEditorController ScriptEditor;

class ScriptEditorPlugin : public EditorPlugin {
	FOUNDRY_CLASS(ScriptEditorPlugin, EditorPlugin);

	String last_editor;
	// The controller is a plain Object (not owned by the scene tree), so the
	// plugin that created it is responsible for freeing it.
	ScriptEditorController *owned_controller = nullptr;

	void _save_last_editor(const String &p_editor);

protected:
	void _notification(int p_what);

public:
	static bool open_in_external_editor(const String &p_path, int p_line, int p_col, bool p_ignore_project = false);

	virtual String get_plugin_name() const override { return TTRC("Script"); }
	// The script editor is not a main screen: it edits scripts into a workspace
	// leaf (see EditorNode::reveal_script_leaf), so it does not register a
	// toolbar tab and inherits has_main_screen() == false from the base plugin.
	virtual void edit(Object *p_object) override;
	virtual bool handles(Object *p_object) const override;
	virtual void make_visible(bool p_visible) override;
	virtual void selected_notify() override;

	virtual String get_unsaved_status(const String &p_for_scene) const override;
	virtual void save_external_data() override;
	virtual void apply_changes() override;

	virtual void set_window_layout(Ref<ConfigFile> p_layout) override;
	virtual void get_window_layout(Ref<ConfigFile> p_layout) override;

	virtual void get_breakpoints(List<String> *p_breakpoints) override;

	virtual void edited_scene_changed() override;

	ScriptEditorPlugin();
	~ScriptEditorPlugin();
};
