/**************************************************************************/
/*  startup_dialog.h                                                      */
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

#include "editor/project_manager/known_project_store.h"
#include "scene/gui/dialogs.h"

class Button;
class ItemList;
class Label;
class ProjectDialog;
class TabContainer;
class TextureRect;

// Centered startup dialog for the projectless editor shell. Owns the tab
// scaffolding and the Projects tab (create/open/recents). Manage and About tab
// contents are delivered in follow-up issues; this class provides empty shells.
class StartupDialog : public AcceptDialog {
	FOUNDRY_CLASS(StartupDialog, AcceptDialog);

	static constexpr int MAX_RECENT_PROJECTS = 10;

	KnownProjectStore known_projects;

	TextureRect *logo = nullptr;
	Label *product_name_label = nullptr;
	Label *tagline_label = nullptr;

	TabContainer *tabs = nullptr;
	Control *projects_tab = nullptr;
	Control *manage_tab = nullptr;
	Control *about_tab = nullptr;

	Button *create_project_button = nullptr;
	Button *open_existing_button = nullptr;
	ItemList *recents_list = nullptr;
	Label *recents_empty_label = nullptr;
	Button *open_recent_button = nullptr;
	Button *remove_recent_button = nullptr;

	ProjectDialog *project_dialog = nullptr;

	void _update_theme();
	void _refresh_recents();
	void _update_recent_action_buttons();

	String _recent_display_name(const KnownProjectStore::KnownProject &p_project) const;
	String _recent_item_text(const KnownProjectStore::KnownProject &p_project) const;

	void _create_project();
	void _open_existing_project();
	void _open_selected_recent();
	void _remove_selected_recent();

	void _on_recents_selected(int p_index);
	void _on_recents_activated(int p_index);
	void _on_project_created(const String &p_dir, bool p_edit);
	void _on_projects_updated();

	// Records the open in the global store, launches `editor open --project`, and
	// quits the projectless shell. Returns the create_instance error when launch fails.
	Error _open_project_and_restart(const String &p_path);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	bool is_visible_dialog() const;

	void show_startup_dialog();
	void hide_startup_dialog();

	StartupDialog();
};
