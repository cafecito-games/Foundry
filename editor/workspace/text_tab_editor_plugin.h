/**************************************************************************/
/*  text_tab_editor_plugin.h                                              */
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

#pragma once

#include "editor/plugins/editor_plugin.h"

class ConfirmationDialog;
class TextTabType;
class Tree;

/**
 * Bridges workspace TextTab documents into the editor-wide unsaved-changes flow.
 * Non-script text now opens as a workspace "text" tab instead of the script
 * editor, so this plugin re-reports and saves dirty TextDocuments through the
 * same get_unsaved_status()/save_external_data() aggregation used for quit,
 * reload, and Save All -- keeping unsaved text edits from being silently lost.
 *
 * It also drives external on-disk change detection for text tabs: on editor
 * foreground it scans the open documents and, mirroring the script editor,
 * silently reloads clean tabs or prompts reload/keep when a changed tab is dirty
 * (or auto-reload is disabled), so an external edit is never silently overwritten.
 */
class TextTabEditorPlugin : public EditorPlugin {
	FOUNDRY_CLASS(TextTabEditorPlugin, EditorPlugin);

	ConfirmationDialog *disk_changed = nullptr;
	Tree *disk_changed_list = nullptr;

	TextTabType *_text_tab_type() const;
	void _ensure_disk_changed_dialog();
	void _scan_external_changes();
	void _on_disk_changed_reload();
	void _on_disk_changed_action(const String &p_action);
	static void _report_failures(const String &p_heading, const PackedStringArray &p_paths);

protected:
	void _notification(int p_what);

public:
	virtual String get_plugin_name() const override { return "TextTab"; }
	virtual bool has_main_screen() const override { return false; }

	virtual String get_unsaved_status(const String &p_for_scene) const override;
	virtual void save_external_data() override;

	TextTabEditorPlugin() {}
};
