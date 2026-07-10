/**************************************************************************/
/*  debugger_editor_plugin.h                                              */
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

#include "editor/plugins/editor_plugin.h"

class EditorFileServer;
class MenuButton;
class PopupMenu;
class RunInstancesDialog;

class DebuggerEditorPlugin : public EditorPlugin {
	FOUNDRY_CLASS(DebuggerEditorPlugin, EditorPlugin);

private:
	static DebuggerEditorPlugin *singleton;

	PopupMenu *debug_menu = nullptr;
	EditorFileServer *file_server = nullptr;
	RunInstancesDialog *run_instances_dialog = nullptr;

	enum MenuOptions {
		RUN_FILE_SERVER,
		RUN_LIVE_DEBUG,
		RUN_DEBUG_COLLISIONS,
		RUN_DEBUG_PATHS,
		RUN_DEBUG_NAVIGATION,
		RUN_DEBUG_AVOIDANCE,
		RUN_DEBUG_CANVAS_REDRAW,
		RUN_DEPLOY_REMOTE_DEBUG,
		RUN_RELOAD_SCRIPTS,
		SERVER_KEEP_OPEN,
		RUN_MULTIPLE_INSTANCES,
	};

	// Single source of truth pairing each persisted debug option with its menu item
	// and default state, so applying project metadata and querying menu state stay in
	// sync. The order matches the order options are applied on load.
	struct DebugOption {
		MenuOptions option;
		const char *metadata_key;
		bool default_value;
	};
	static const DebugOption debug_option_table[];

	bool initializing = true;

	int _find_debug_option(const String &p_metadata_key) const;
	void _apply_debug_option(MenuOptions p_option, bool p_enabled);
	void _update_debug_options();
	void _notification(int p_what);
	void _menu_option(int p_option);

public:
	static DebuggerEditorPlugin *get_singleton() { return singleton; }

	virtual String get_plugin_name() const override { return "Debugger"; }
	bool has_main_screen() const override { return false; }

	// Reset the debug menu and its backends to match the current project's saved
	// `debug_options` metadata. Idempotent: it toggles each option only when its state
	// differs, so it can run after NOTIFICATION_READY when a project is loaded in-process.
	void apply_project_debug_options() { _update_debug_options(); }
	// Whether the debug menu item backing the given `debug_options` metadata key is
	// currently checked. Intended for tests/automation.
	bool is_debug_option_checked(const String &p_metadata_key) const;

	DebuggerEditorPlugin(PopupMenu *p_menu);
	~DebuggerEditorPlugin();
};
