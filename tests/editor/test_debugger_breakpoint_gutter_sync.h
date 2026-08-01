/**************************************************************************/
/*  test_debugger_breakpoint_gutter_sync.h                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             FOUNDRY ENGINE                             */
/*          A fork of the Godot Engine (https://godotengine.org)          */
/*                       https://www.cafecito.games                       */
/**************************************************************************/
/* Copyright (c) 2026-present Cafecito Games LLC.                         */
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

#include "editor/editor_script_leaf.h"
#include "editor/script/script_editor_controller.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/script/script_editor_view.h"
#include "scene/gui/control.h"
#include "scene/main/window.h"
#include "scene/resources/text_file.h"

#include "tests/test_macros.h"

namespace TestDebuggerBreakpointGutterSync {

// Stands in for ScriptTextEditor without pulling in its theme/menu/EditorNode setup,
// which only work inside a fully booted editor. This still exercises the real
// ScriptEditorBase interface that ScriptEditorController::_sync_breakpoint_gutter()
// dispatches through, so it is faithful to what a real script tab receives.
class FakeScriptEditorBase : public ScriptEditorBase {
	FOUNDRY_CLASS(FakeScriptEditorBase, ScriptEditorBase);

public:
	Ref<Resource> edited_resource;
	int set_breakpoint_call_count = 0;
	int last_breakpoint_line = -1;
	bool last_breakpoint_enabled = false;

	virtual void add_syntax_highlighter(Ref<EditorSyntaxHighlighter> p_highlighter) override {}
	virtual void set_syntax_highlighter(Ref<EditorSyntaxHighlighter> p_highlighter) override {}
	virtual void apply_code() override {}
	virtual Ref<Resource> get_edited_resource() const override { return edited_resource; }
	virtual Vector<String> get_functions() override { return Vector<String>(); }
	virtual void set_edited_resource(const Ref<Resource> &p_res) override { edited_resource = p_res; }
	virtual void enable_editor(Control *p_shortcut_context = nullptr) override {}
	virtual void reload_text() override {}
	virtual String get_name() override { return "fake"; }
	virtual Ref<Texture2D> get_theme_icon() override { return Ref<Texture2D>(); }
	virtual bool is_unsaved() override { return false; }
	virtual Variant get_edit_state() override { return Variant(); }
	virtual void set_edit_state(const Variant &p_state) override {}
	virtual Variant get_navigation_state() override { return Variant(); }
	virtual void goto_line(int p_line, int p_column = 0) override {}
	virtual void set_executing_line(int p_line) override {}
	virtual void clear_executing_line() override {}
	virtual void trim_trailing_whitespace() override {}
	virtual void trim_final_newlines() override {}
	virtual void insert_final_newline() override {}
	virtual void convert_indent() override {}
	virtual void ensure_focus() override {}
	virtual void tag_saved_version() override {}
	virtual PackedInt32Array get_breakpoints() override { return PackedInt32Array(); }

	virtual void set_breakpoint(int p_line, bool p_enabled) override {
		set_breakpoint_call_count++;
		last_breakpoint_line = p_line;
		last_breakpoint_enabled = p_enabled;
	}

	virtual void clear_breakpoints() override {}
	virtual void add_callback(const String &p_function, const PackedStringArray &p_args) override {}
	virtual void update_settings() override {}
	virtual void set_debugger_active(bool p_active) override {}
	virtual bool show_members_overview() override { return false; }
	virtual void set_tooltip_request_func(const Callable &p_toolip_callback) override {}
	virtual Control *get_edit_menu() override { return nullptr; }
	virtual void clear_edit_menu() override {}
	virtual void set_find_replace_bar(FindReplaceBar *p_bar) override {}
	virtual Control *get_base_editor() const override { return nullptr; }
	virtual CodeTextEditor *get_code_editor() const override { return nullptr; }
	virtual void validate() override {}
};

struct ScriptControllerHarness {
	Control *host = nullptr;
	ScriptEditorController *controller = nullptr;

	void mount() {
		host = memnew(Control);
		SceneTree::get_singleton()->get_root()->add_child(host);
		controller = memnew(ScriptEditorController);
		controller->init_global_services(host);
	}

	void pump() {
		SceneTree::get_singleton()->process(0.016);
		MessageQueue::get_singleton()->flush();
	}

	void unmount() {
		memdelete(controller);
		SceneTree::get_singleton()->get_root()->remove_child(host);
		memdelete(host);
	}
};

static Ref<TextFile> make_resource_at(const String &p_path) {
	Ref<TextFile> resource;
	resource.instantiate();
	resource->set_path(p_path);
	return resource;
}

TEST_CASE("[Editor][breakpoint-gutter-sync] Syncing a gutter refreshes only the tab open on that path") {
	ScriptControllerHarness h;
	h.mount();
	h.pump();

	ScriptLeaf *leaf = memnew(ScriptLeaf);
	h.host->add_child(leaf);
	h.pump();

	ScriptEditorView *view = h.controller->create_view_for_leaf(leaf);
	REQUIRE(view != nullptr);

	const String target_path = "res://debugger_gutter_sync_target.fs";
	const String other_path = "res://debugger_gutter_sync_other.fs";

	FakeScriptEditorBase *target_tab = memnew(FakeScriptEditorBase);
	target_tab->set_edited_resource(make_resource_at(target_path));
	view->get_tab_container()->add_child(target_tab);

	FakeScriptEditorBase *other_tab = memnew(FakeScriptEditorBase);
	other_tab->set_edited_resource(make_resource_at(other_path));
	view->get_tab_container()->add_child(other_tab);

	h.pump();

	// EditorDebuggerNode::set_breakpoint() reports its own line numbers 1-indexed;
	// this is the exact call ScriptEditorController::_sync_breakpoint_gutter() makes
	// in response to the "breakpoint_gutter_sync_requested" signal EditorDebuggerNode
	// emits for callers that register breakpoints outside the script tab's own
	// gutter-click chain (currently only the DAP tooling host).
	h.controller->_sync_breakpoint_gutter(target_path, 5, true);

	CHECK(target_tab->set_breakpoint_call_count == 1);
	CHECK(target_tab->last_breakpoint_line == 4);
	CHECK(target_tab->last_breakpoint_enabled == true);

	// The tab open on a different resource must not observe the sync at all.
	CHECK(other_tab->set_breakpoint_call_count == 0);

	h.controller->_sync_breakpoint_gutter(target_path, 5, false);

	CHECK(target_tab->set_breakpoint_call_count == 2);
	CHECK(target_tab->last_breakpoint_line == 4);
	CHECK(target_tab->last_breakpoint_enabled == false);
	CHECK(other_tab->set_breakpoint_call_count == 0);

	h.host->remove_child(leaf);
	memdelete(leaf);
	h.unmount();
}

TEST_CASE("[Editor][breakpoint-gutter-sync] Syncing a gutter for a path with no open tab is a no-op") {
	ScriptControllerHarness h;
	h.mount();
	h.pump();

	ScriptLeaf *leaf = memnew(ScriptLeaf);
	h.host->add_child(leaf);
	h.pump();

	ScriptEditorView *view = h.controller->create_view_for_leaf(leaf);
	REQUIRE(view != nullptr);

	FakeScriptEditorBase *tab = memnew(FakeScriptEditorBase);
	tab->set_edited_resource(make_resource_at("res://debugger_gutter_sync_open.fs"));
	view->get_tab_container()->add_child(tab);
	h.pump();

	// No script tab is open on this path, so nothing should be touched or crash.
	h.controller->_sync_breakpoint_gutter("res://debugger_gutter_sync_never_opened.fs", 1, true);

	CHECK(tab->set_breakpoint_call_count == 0);

	h.host->remove_child(leaf);
	memdelete(leaf);
	h.unmount();
}

} // namespace TestDebuggerBreakpointGutterSync

#endif // TOOLS_ENABLED
