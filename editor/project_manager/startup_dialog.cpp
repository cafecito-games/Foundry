/**************************************************************************/
/*  startup_dialog.cpp                                                    */
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

#include "startup_dialog.h"

#include "core/io/file_access.h"
#include "core/os/keyboard.h"
#include "core/os/os.h"
#include "core/version.h"
#include "editor/editor_string_names.h"
#include "editor/project_manager/project_dialog.h"
#include "editor/project_manager/startup_router.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "main/main.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/separator.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/texture_rect.h"

void StartupDialog::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			_update_theme();
		} break;

		case NOTIFICATION_VISIBILITY_CHANGED: {
			set_process_shortcut_input(is_visible());
		} break;
	}
}

void StartupDialog::_update_theme() {
	if (!is_inside_tree()) {
		return;
	}

	Ref<Theme> theme = get_theme();
	if (theme.is_null()) {
		return;
	}

	if (logo != nullptr) {
		logo->set_texture(theme->get_icon(SNAME("Logo"), EditorStringName(EditorIcons)));
	}
}

String StartupDialog::_recent_display_name(const KnownProjectStore::KnownProject &p_project) const {
	if (!p_project.display_name.is_empty()) {
		return p_project.display_name;
	}
	return p_project.path.get_file();
}

String StartupDialog::_recent_item_text(const KnownProjectStore::KnownProject &p_project) const {
	String title = _recent_display_name(p_project);
	if (p_project.missing) {
		title = vformat(TTR("%s (missing)"), title);
	}
	return vformat("%s\n%s", title, p_project.path);
}

void StartupDialog::_refresh_recents() {
	recents_list->clear();

	bool store_dirty = false;
	Vector<KnownProjectStore::KnownProject> recents = known_projects.get_recent_projects();
	for (KnownProjectStore::KnownProject &project : recents) {
		if (!project.missing) {
			const String config_path = project.path.path_join("project.foundry");
			if (!FileAccess::exists(config_path)) {
				known_projects.mark_project_missing(project.path);
				project.missing = true;
				store_dirty = true;
			} else {
				known_projects.refresh_project(project.path);
				known_projects.get_project(project.path, project);
			}
		}
	}
	if (store_dirty) {
		known_projects.save();
		recents = known_projects.get_recent_projects();
	}

	const int count = MIN(recents.size(), MAX_RECENT_PROJECTS);
	bool has_entries = false;
	for (int i = 0; i < count; i++) {
		const KnownProjectStore::KnownProject &project = recents[i];
		const int index = recents_list->add_item(_recent_item_text(project));
		recents_list->set_item_metadata(index, project.path);
		recents_list->set_item_tooltip(index, project.path);
		has_entries = true;
	}

	recents_list->set_visible(has_entries);
	recents_empty_label->set_visible(!has_entries);
	_update_recent_action_buttons();
}

void StartupDialog::_update_recent_action_buttons() {
	const PackedInt32Array selected = recents_list->get_selected_items();
	const bool has_selection = !selected.is_empty();
	bool missing = false;
	if (has_selection) {
		const String path = recents_list->get_item_metadata(selected[0]);
		KnownProjectStore::KnownProject project;
		if (known_projects.get_project(path, project)) {
			missing = project.missing;
		}
	}

	open_recent_button->set_disabled(!has_selection || missing);
	remove_recent_button->set_disabled(!has_selection || !missing);
}

void StartupDialog::_create_project() {
	project_dialog->set_mode(ProjectDialog::MODE_NEW);
	project_dialog->show_dialog();
}

void StartupDialog::_open_existing_project() {
	project_dialog->set_mode(ProjectDialog::MODE_IMPORT);
	project_dialog->ask_for_path_and_show();
}

Error StartupDialog::_open_project_and_restart(const String &p_path) {
	const String canonical = KnownProjectStore::canonicalize_path(p_path);
	const String config_path = canonical.path_join("project.foundry");
	if (!FileAccess::exists(config_path)) {
		return ERR_FILE_NOT_FOUND;
	}

	const Error store_err = StartupRouter::record_project_opened(known_projects, canonical);
	if (store_err != OK) {
		ERR_PRINT(vformat("Failed to save known projects after opening '%s' (error %d).", canonical, store_err));
	}

	print_line("Editing project: " + canonical);

	List<String> args;
	for (const String &a : Main::get_forwardable_cli_arguments(Main::CLI_SCOPE_TOOL)) {
		args.push_back(a);
	}
	args.push_back("editor");
	args.push_back("open");
	args.push_back("--project");
	args.push_back(canonical);

	const Error err = OS::get_singleton()->create_instance(args);
	if (err != OK) {
		return err;
	}

	get_tree()->quit();
	return OK;
}

void StartupDialog::_open_selected_recent() {
	const PackedInt32Array selected = recents_list->get_selected_items();
	if (selected.is_empty()) {
		return;
	}

	const String path = recents_list->get_item_metadata(selected[0]);
	if (path.is_empty()) {
		return;
	}

	KnownProjectStore::KnownProject project;
	if (!known_projects.get_project(path, project) || project.missing) {
		return;
	}

	if (_open_project_and_restart(path) != OK) {
		ERR_PRINT(vformat("Failed to start an editor instance for the project at '%s'.", path));
	}
}

void StartupDialog::_remove_selected_recent() {
	const PackedInt32Array selected = recents_list->get_selected_items();
	if (selected.is_empty()) {
		return;
	}

	const String path = recents_list->get_item_metadata(selected[0]);
	if (path.is_empty()) {
		return;
	}

	if (known_projects.remove_project(path)) {
		known_projects.save();
	}
	_refresh_recents();
}

void StartupDialog::_on_recents_selected(int p_index) {
	if (p_index < 0) {
		_update_recent_action_buttons();
		return;
	}
	_update_recent_action_buttons();
}

void StartupDialog::_on_recents_activated(int p_index) {
	recents_list->select(p_index, true);
	_open_selected_recent();
}

void StartupDialog::_on_project_created(const String &p_dir, bool p_edit) {
	known_projects.add_project(p_dir);
	known_projects.save();
	_refresh_recents();

	if (p_edit) {
		if (_open_project_and_restart(p_dir) != OK) {
			ERR_PRINT(vformat("Failed to open newly created project at '%s'.", p_dir));
		}
	}
}

void StartupDialog::_on_projects_updated() {
	_refresh_recents();
}

bool StartupDialog::is_visible_dialog() const {
	return is_visible();
}

void StartupDialog::show_startup_dialog() {
	known_projects.load();
	_refresh_recents();
	tabs->set_current_tab(0);
	popup_centered_clamped(Size2i(560, 0) * EDSCALE, 0.85);
}

void StartupDialog::hide_startup_dialog() {
	hide();
}

StartupDialog::StartupDialog() {
	set_title(TTR("Startup"));
	set_accessibility_name(TTRC("Startup Dialog"));
	set_unparent_when_invisible(true);
	set_wrap_controls(true);

	get_ok_button()->hide();

	VBoxContainer *root = memnew(VBoxContainer);
	root->add_theme_constant_override("separation", 16 * EDSCALE);
	add_child(root);

	{
		HBoxContainer *header = memnew(HBoxContainer);
		header->add_theme_constant_override("separation", 12 * EDSCALE);
		header->set_alignment(BoxContainer::ALIGNMENT_BEGIN);
		root->add_child(header);

		logo = memnew(TextureRect);
		logo->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		logo->set_custom_minimum_size(Size2(64, 64) * EDSCALE);
		header->add_child(logo);

		VBoxContainer *header_text = memnew(VBoxContainer);
		header_text->add_theme_constant_override("separation", 4 * EDSCALE);
		header->add_child(header_text);

		product_name_label = memnew(Label);
		product_name_label->set_text(FOUNDRY_VERSION_NAME);
		product_name_label->set_accessibility_name(TTRC("Foundry Engine"));
		product_name_label->add_theme_font_size_override(SceneStringName(font_size), 20 * EDSCALE);
		header_text->add_child(product_name_label);

		tagline_label = memnew(Label);
		tagline_label->set_text(TTRC("Create or open a project workspace"));
		tagline_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
		header_text->add_child(tagline_label);
	}

	root->add_child(memnew(HSeparator));

	tabs = memnew(TabContainer);
	tabs->set_accessibility_name(TTRC("Startup Dialog Tabs"));
	tabs->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tabs->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	tabs->set_custom_minimum_size(Size2(0, 320) * EDSCALE);
	root->add_child(tabs);

	{
		projects_tab = memnew(VBoxContainer);
		projects_tab->set_name(TTRC("Projects"));
		projects_tab->add_theme_constant_override("separation", 12 * EDSCALE);
		tabs->add_child(projects_tab);

		Label *start_label = memnew(Label);
		start_label->set_text(TTRC("Start a project"));
		projects_tab->add_child(start_label);

		create_project_button = memnew(Button);
		create_project_button->set_text(TTRC("Create Project"));
		create_project_button->set_accessibility_name(TTRC("Create Project"));
		create_project_button->set_theme_type_variation("PanelBackgroundButton");
		create_project_button->set_shortcut(ED_SHORTCUT("startup_dialog/create_project", TTRC("Create Project"), KeyModifierMask::CMD_OR_CTRL | Key::N));
		create_project_button->connect(SceneStringName(pressed), callable_mp(this, &StartupDialog::_create_project));
		projects_tab->add_child(create_project_button);

		open_existing_button = memnew(Button);
		open_existing_button->set_text(TTRC("Open Existing Project"));
		open_existing_button->set_accessibility_name(TTRC("Open Existing Project"));
		open_existing_button->set_theme_type_variation("PanelBackgroundButton");
		open_existing_button->set_shortcut(ED_SHORTCUT("startup_dialog/open_existing", TTRC("Open Existing Project"), KeyModifierMask::CMD_OR_CTRL | Key::O));
		open_existing_button->connect(SceneStringName(pressed), callable_mp(this, &StartupDialog::_open_existing_project));
		projects_tab->add_child(open_existing_button);

		projects_tab->add_child(memnew(HSeparator));

		Label *recent_label = memnew(Label);
		recent_label->set_text(TTRC("Recent"));
		projects_tab->add_child(recent_label);

		recents_list = memnew(ItemList);
		recents_list->set_accessibility_name(TTRC("Recent Projects"));
		recents_list->set_auto_height(true);
		recents_list->set_max_columns(1);
		recents_list->set_same_column_width(true);
		recents_list->set_fixed_column_width(480 * EDSCALE);
		recents_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		recents_list->connect(SceneStringName(item_selected), callable_mp(this, &StartupDialog::_on_recents_selected));
		recents_list->connect("item_activated", callable_mp(this, &StartupDialog::_on_recents_activated));
		projects_tab->add_child(recents_list);

		recents_empty_label = memnew(Label);
		recents_empty_label->set_text(TTRC("No recent projects yet."));
		recents_empty_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		recents_empty_label->hide();
		projects_tab->add_child(recents_empty_label);

		HBoxContainer *recent_actions = memnew(HBoxContainer);
		recent_actions->add_theme_constant_override("separation", 8 * EDSCALE);
		projects_tab->add_child(recent_actions);

		open_recent_button = memnew(Button);
		open_recent_button->set_text(TTRC("Open Recent Project"));
		open_recent_button->set_accessibility_name(TTRC("Open Recent Project"));
		open_recent_button->set_disabled(true);
		open_recent_button->connect(SceneStringName(pressed), callable_mp(this, &StartupDialog::_open_selected_recent));
		recent_actions->add_child(open_recent_button);

		remove_recent_button = memnew(Button);
		remove_recent_button->set_text(TTRC("Remove from Recents"));
		remove_recent_button->set_accessibility_name(TTRC("Remove from Recents"));
		remove_recent_button->set_disabled(true);
		remove_recent_button->connect(SceneStringName(pressed), callable_mp(this, &StartupDialog::_remove_selected_recent));
		recent_actions->add_child(remove_recent_button);
	}

	{
		manage_tab = memnew(VBoxContainer);
		manage_tab->set_name(TTRC("Manage"));
		tabs->add_child(manage_tab);

		Label *placeholder = memnew(Label);
		placeholder->set_text(TTRC("Project maintenance tools will appear here."));
		placeholder->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		placeholder->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
		placeholder->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		manage_tab->add_child(placeholder);
	}

	{
		about_tab = memnew(VBoxContainer);
		about_tab->set_name(TTRC("About"));
		tabs->add_child(about_tab);

		Label *placeholder = memnew(Label);
		placeholder->set_text(TTRC("About information will appear here."));
		placeholder->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		placeholder->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
		placeholder->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		about_tab->add_child(placeholder);
	}

	project_dialog = memnew(ProjectDialog);
	project_dialog->connect("projects_updated", callable_mp(this, &StartupDialog::_on_projects_updated));
	project_dialog->connect("project_created", callable_mp(this, &StartupDialog::_on_project_created));
	add_child(project_dialog);

	tabs->set_current_tab(0);
}

void StartupDialog::_bind_methods() {
}
