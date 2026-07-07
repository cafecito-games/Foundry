/**************************************************************************/
/*  text_tab_editor_plugin.cpp                                            */
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

#include "text_tab_editor_plugin.h"

#include "core/os/os.h"
#include "editor/editor_node.h"
#include "editor/settings/editor_settings.h"
#include "editor/workspace/text_tab.h"
#include "editor/workspace/workspace_pane.h"
#include "editor/workspace/workspace_tab_registry.h"
#include "scene/gui/box_container.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/tree.h"
#include "scene/main/window.h"

TextTabType *TextTabEditorPlugin::_text_tab_type() const {
	return static_cast<TextTabType *>(WorkspacePane::get_shared_tab_registry().find_type(StringName("text")));
}

void TextTabEditorPlugin::_ensure_disk_changed_dialog() {
	if (disk_changed) {
		return;
	}
	disk_changed = memnew(ConfirmationDialog);
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

	disk_changed->set_ok_button_text(TTRC("Reload from disk"));
	disk_changed->connect(SceneStringName(confirmed), callable_mp(this, &TextTabEditorPlugin::_on_disk_changed_reload));
	disk_changed->add_button(TTRC("Ignore external changes"), !DisplayServer::get_singleton()->get_swap_cancel_ok(), "resave");
	disk_changed->connect("custom_action", callable_mp(this, &TextTabEditorPlugin::_on_disk_changed_action));

	add_child(disk_changed);
}

void TextTabEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_APPLICATION_FOCUS_IN: {
			if (is_inside_tree()) {
				_scan_external_changes();
			}
		} break;
	}
}

void TextTabEditorPlugin::_scan_external_changes() {
	TextTabType *type = _text_tab_type();
	if (!type) {
		return;
	}

	const bool autoreload = EDITOR_GET("text_editor/behavior/files/auto_reload_scripts_on_external_change");
	PackedStringArray changed;
	const bool need_ask = type->collect_external_changes(changed, autoreload);
	if (changed.is_empty()) {
		return;
	}

	if (!need_ask) {
		// Every changed tab is clean and auto-reload is on: pull disk contents in.
		_report_failures(TTR("Could not reload the following externally changed text file(s):"), type->reload_externally_changed());
		return;
	}

	_ensure_disk_changed_dialog();
	disk_changed_list->clear();
	TreeItem *root = disk_changed_list->create_item();
	for (const String &path : changed) {
		TreeItem *item = disk_changed_list->create_item(root);
		item->set_text(0, path.get_file());
	}
	callable_mp((Window *)disk_changed, &Window::popup_centered_ratio).call_deferred(0.3);
}

void TextTabEditorPlugin::_report_failures(const String &p_heading, const PackedStringArray &p_paths) {
	if (p_paths.is_empty() || !EditorNode::get_singleton()) {
		return;
	}
	PackedStringArray message;
	message.push_back(p_heading);
	for (const String &path : p_paths) {
		message.push_back(path);
	}
	EditorNode::get_singleton()->show_warning(String("\n").join(message));
}

void TextTabEditorPlugin::_on_disk_changed_reload() {
	if (TextTabType *type = _text_tab_type()) {
		_report_failures(TTR("Could not reload the following externally changed text file(s):"), type->reload_externally_changed());
	}
	if (disk_changed) {
		disk_changed->hide();
	}
}

void TextTabEditorPlugin::_on_disk_changed_action(const String &p_action) {
	if (p_action == "resave") {
		if (TextTabType *type = _text_tab_type()) {
			_report_failures(TTR("Could not save the following text file(s) over the external change:"), type->resave_externally_changed());
		}
	}
	if (disk_changed) {
		disk_changed->hide();
	}
}

String TextTabEditorPlugin::get_unsaved_status(const String &p_for_scene) const {
	// Text documents are standalone files, not scene-scoped built-ins, so they
	// only participate in the global (no specific scene) unsaved prompt.
	if (!p_for_scene.is_empty()) {
		return String();
	}
	TextTabType *type = _text_tab_type();
	if (!type) {
		return String();
	}
	const PackedStringArray unsaved = type->get_unsaved_document_paths();
	if (unsaved.is_empty()) {
		return String();
	}

	PackedStringArray message;
	message.push_back(TTR("Save changes to the following text file(s) before quitting?"));
	for (const String &path : unsaved) {
		message.push_back(path);
	}
	return String("\n").join(message);
}

void TextTabEditorPlugin::save_external_data() {
	if (TextTabType *type = _text_tab_type()) {
		type->save_all_documents();
	}
}
