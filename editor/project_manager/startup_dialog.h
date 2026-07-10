/**************************************************************************/
/*  startup_dialog.h                                                      */
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

#include "editor/project_manager/known_project_store.h"
#include "scene/gui/dialogs.h"

class Button;
class ConfirmationDialog;
class Control;
class EditorAbout;
class EditorFileDialog;
class HBoxContainer;
class Label;
class LineEdit;
class MarginContainer;
class ProjectDialog;
class ProjectScanner;
class ScrollContainer;
class TabContainer;
class TextureRect;
class VBoxContainer;

// Centered startup dialog for the projectless editor shell. Owns the tab
// scaffolding and the Projects tab (create/open/recents). Manage and About tab
// contents are delivered in follow-up issues; this class provides empty shells.
class StartupDialog : public AcceptDialog {
	FOUNDRY_CLASS(StartupDialog, AcceptDialog);

	static constexpr int MAX_RECENT_PROJECTS = 10;

	KnownProjectStore known_projects;

	TextureRect *logo = nullptr;
	Label *product_name_label = nullptr;
	Label *version_label = nullptr;
	Label *tagline_label = nullptr;

	TabContainer *tabs = nullptr;
	Control *projects_tab = nullptr;
	Control *manage_tab = nullptr;
	Control *about_tab = nullptr;

	// About tab: reuses the editor About/Credits data sources for product, version,
	// copyright, and license, and defers full credits/third-party notices to the
	// shared EditorAbout dialog.
	Label *about_version_label = nullptr;
	EditorAbout *about_dialog = nullptr;

	Button *create_project_button = nullptr;
	Button *open_existing_button = nullptr;
	ScrollContainer *recents_scroll = nullptr;
	VBoxContainer *recents_container = nullptr;
	VBoxContainer *recents_empty_state = nullptr;

	// Manage tab: known-project maintenance (filter, scan, tags, duplicate, reveal,
	// missing cleanup). `Manage` is never the default startup tab.
	LineEdit *manage_filter = nullptr;
	Button *scan_button = nullptr;
	Button *remove_missing_button = nullptr;
	ScrollContainer *manage_list_scroll = nullptr;
	VBoxContainer *manage_list_container = nullptr;
	VBoxContainer *manage_empty_state = nullptr;
	Label *manage_empty_hint = nullptr;

	VBoxContainer *manage_detail_pane = nullptr;
	VBoxContainer *manage_detail_empty = nullptr;
	Label *manage_detail_name = nullptr;
	Label *manage_detail_path = nullptr;
	Label *manage_missing_label = nullptr;
	Button *manage_open_button = nullptr;
	Button *manage_duplicate_button = nullptr;
	Button *manage_reveal_button = nullptr;
	HBoxContainer *manage_tags_row = nullptr;
	Button *manage_add_tag_button = nullptr;

	ProjectScanner *project_scanner = nullptr;
	EditorFileDialog *scan_dir_dialog = nullptr;
	ConfirmationDialog *remove_missing_dialog = nullptr;
	AcceptDialog *manage_error_dialog = nullptr;
	ConfirmationDialog *add_tag_dialog = nullptr;
	LineEdit *add_tag_name = nullptr;
	Label *add_tag_error = nullptr;

	// Canonical path of the project selected in the Manage list, or empty if none.
	String manage_selected_path;
	String manage_filter_query;

	ProjectDialog *project_dialog = nullptr;

	void _update_theme();
	void _refresh_recents();
	void _clear_recent_cards();
	void _build_recent_card(const KnownProjectStore::KnownProject &p_project);

	String _recent_display_name(const KnownProjectStore::KnownProject &p_project) const;
	String _format_last_opened(uint64_t p_unix_time) const;

	void _create_project();
	void _open_existing_project();
	void _open_recent_path(const String &p_path);
	void _remove_recent_path(const String &p_path);

	void _on_project_created(const String &p_dir, bool p_edit);
	void _on_project_duplicated(const String &p_original_path, const String &p_duplicate_path, bool p_edit);
	void _on_projects_updated();

	// Manage tab helpers.
	void _build_manage_tab(Control *p_parent);
	void _refresh_manage_list();
	void _clear_manage_rows();
	void _build_manage_row(const KnownProjectStore::KnownProject &p_project);
	void _select_manage_project(const String &p_path);
	void _refresh_manage_detail();
	void _clear_manage_tags();

	void _on_manage_filter_changed(const String &p_text);
	void _scan_folder();
	void _on_scan_dir_selected(const String &p_dir);
	void _on_scan_finished(const PackedStringArray &p_found);

	void _prompt_remove_missing();
	void _confirm_remove_missing();

	void _manage_open_selected();
	void _manage_duplicate_selected();
	void _manage_reveal_selected();

	void _prompt_add_tag();
	void _validate_new_tag(const String &p_name);
	void _confirm_add_tag();
	void _remove_selected_tag(const String &p_tag);
	void _apply_selected_tags(const PackedStringArray &p_tags);

	// About tab helpers.
	void _build_about_tab(Control *p_parent);
	void _show_full_credits();
	void _show_license();
	void _show_third_party_notices();

	// Records the open in the global store, launches `editor open --project`, and
	// quits the projectless shell. Returns the create_instance error when launch fails.
	Error _open_project_and_restart(const String &p_path);

	// Handles the dialog's cancel/close/Escape path. In the projectless launcher
	// there is no project workspace to return to, so dismissing quits Foundry
	// rather than revealing an empty no-project editor.
	void _on_dialog_dismissed();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// True when p_project matches the Manage-tab filter query. An empty (or
	// whitespace-only) query matches everything; otherwise the query is matched
	// case-insensitively against the project's display name, its path, and each of
	// its tags. Static and UI-free so the filtering rule is unit-testable.
	static bool manage_filter_matches(const KnownProjectStore::KnownProject &p_project, const String &p_query);

	bool is_visible_dialog() const;

	// Opens the given project from the startup dialog: an in-process load when running
	// as the projectless shell, a graceful project switch when a project is already
	// open, or a process relaunch as a fallback. This is the shared entry the recent
	// cards and the Manage tab's Open action route through.
	Error open_project_path(const String &p_path);

	void show_startup_dialog();
	void hide_startup_dialog();

	StartupDialog();
};
