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
#include "core/os/time.h"
#include "core/version.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/project_manager/project_dialog.h"
#include "editor/project_manager/startup_router.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "main/main.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/scroll_container.h"
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

	Ref<Theme> editor_theme = get_theme();
	if (editor_theme.is_null()) {
		return;
	}

	if (logo != nullptr) {
		logo->set_texture(editor_theme->get_icon(SNAME("Logo"), EditorStringName(EditorIcons)));
	}

	const Color accent_color = editor_theme->get_color(SNAME("accent_color"), EditorStringName(Editor));
	const Color muted_color = editor_theme->get_color(SceneStringName(font_color), EditorStringName(Editor)) * Color(1, 1, 1, 0.55);

	if (version_label != nullptr) {
		version_label->add_theme_color_override(SceneStringName(font_color), muted_color);
	}
	if (tagline_label != nullptr) {
		tagline_label->add_theme_color_override(SceneStringName(font_color), muted_color);
	}
	if (create_project_button != nullptr) {
		create_project_button->set_button_icon(editor_theme->get_icon(SNAME("New"), EditorStringName(EditorIcons)));
		create_project_button->add_theme_color_override(SceneStringName(font_color), accent_color);
		create_project_button->add_theme_color_override(SNAME("font_hover_color"), accent_color);
		create_project_button->add_theme_color_override(SNAME("font_pressed_color"), accent_color);
	}
	if (open_existing_button != nullptr) {
		open_existing_button->set_button_icon(editor_theme->get_icon(SNAME("FolderBrowse"), EditorStringName(EditorIcons)));
	}

	// Recent cards are rebuilt with the current theme so their icons/colors stay in sync.
	_refresh_recents();
}

String StartupDialog::_recent_display_name(const KnownProjectStore::KnownProject &p_project) const {
	if (!p_project.display_name.is_empty()) {
		return p_project.display_name;
	}
	return p_project.path.get_file();
}

String StartupDialog::_format_last_opened(uint64_t p_unix_time) const {
	if (p_unix_time == 0) {
		return String();
	}
	return Time::get_singleton()->get_date_string_from_unix_time((int64_t)p_unix_time);
}

void StartupDialog::_clear_recent_cards() {
	if (recents_container == nullptr) {
		return;
	}
	for (int i = recents_container->get_child_count() - 1; i >= 0; i--) {
		Node *child = recents_container->get_child(i);
		recents_container->remove_child(child);
		child->queue_free();
	}
}

void StartupDialog::_refresh_recents() {
	if (recents_container == nullptr) {
		return;
	}

	_clear_recent_cards();

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
	for (int i = 0; i < count; i++) {
		_build_recent_card(recents[i]);
	}

	const bool has_entries = count > 0;
	recents_scroll->set_visible(has_entries);
	recents_empty_state->set_visible(!has_entries);
}

void StartupDialog::_build_recent_card(const KnownProjectStore::KnownProject &p_project) {
	Ref<Theme> editor_theme = get_theme();

	// The whole card is a flat button so the row highlights on hover and opens on
	// click or keyboard activation. Rich content is layered on top with mouse
	// input passing through to the button.
	Button *card = memnew(Button);
	card->set_theme_type_variation(SNAME("FlatButton"));
	card->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	card->set_custom_minimum_size(Size2(0, 52) * EDSCALE);
	card->set_clip_text(true);
	card->set_focus_mode(Control::FOCUS_ALL);
	card->set_tooltip_text(p_project.path);
	card->set_accessibility_name(vformat(TTRC("Open recent project %s"), _recent_display_name(p_project)));
	if (p_project.missing) {
		card->set_disabled(true);
	} else {
		card->connect(SceneStringName(pressed), callable_mp(this, &StartupDialog::_open_recent_path).bind(p_project.path));
	}
	recents_container->add_child(card);

	MarginContainer *card_margin = memnew(MarginContainer);
	card_margin->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	card_margin->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	card_margin->add_theme_constant_override(SNAME("margin_left"), 10 * EDSCALE);
	card_margin->add_theme_constant_override(SNAME("margin_right"), 6 * EDSCALE);
	card->add_child(card_margin);

	HBoxContainer *row = memnew(HBoxContainer);
	row->add_theme_constant_override(SNAME("separation"), 10 * EDSCALE);
	card_margin->add_child(row);

	TextureRect *icon = memnew(TextureRect);
	icon->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	icon->set_custom_minimum_size(Size2(28, 28) * EDSCALE);
	icon->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	if (editor_theme.is_valid()) {
		const StringName icon_name = p_project.missing ? SNAME("FileBroken") : SNAME("Folder");
		icon->set_texture(editor_theme->get_icon(icon_name, EditorStringName(EditorIcons)));
	}
	row->add_child(icon);

	VBoxContainer *text_column = memnew(VBoxContainer);
	text_column->add_theme_constant_override(SNAME("separation"), 0);
	text_column->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	text_column->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	row->add_child(text_column);

	String title_text = _recent_display_name(p_project);
	if (p_project.missing) {
		title_text = vformat(TTR("%s (missing)"), title_text);
	}
	Label *title_label = memnew(Label);
	title_label->set_text(title_text);
	title_label->set_clip_text(true);
	text_column->add_child(title_label);

	Label *path_label = memnew(Label);
	path_label->set_text(p_project.path);
	path_label->set_clip_text(true);
	path_label->set_structured_text_bidi_override(TextServer::STRUCTURED_TEXT_FILE);
	path_label->set_modulate(Color(1, 1, 1, 0.5));
	if (editor_theme.is_valid()) {
		path_label->add_theme_font_size_override(SceneStringName(font_size), editor_theme->get_font_size(SNAME("font_size"), SNAME("Label")) - 1);
	}
	text_column->add_child(path_label);

	const String last_opened = _format_last_opened(p_project.last_opened_unix_time);
	if (!last_opened.is_empty()) {
		Label *time_label = memnew(Label);
		time_label->set_text(last_opened);
		time_label->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
		time_label->set_modulate(Color(1, 1, 1, 0.5));
		row->add_child(time_label);
	}

	// Remove button lives above the card and intercepts its own clicks so it does
	// not also trigger the card's open action.
	Button *remove_button = memnew(Button);
	remove_button->set_theme_type_variation(SNAME("FlatButton"));
	remove_button->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	remove_button->set_mouse_filter(Control::MOUSE_FILTER_STOP);
	remove_button->set_tooltip_text(TTRC("Remove from Recents"));
	remove_button->set_accessibility_name(vformat(TTRC("Remove %s from recents"), _recent_display_name(p_project)));
	if (editor_theme.is_valid()) {
		remove_button->set_button_icon(editor_theme->get_icon(SNAME("Remove"), EditorStringName(EditorIcons)));
	}
	remove_button->connect(SceneStringName(pressed), callable_mp(this, &StartupDialog::_remove_recent_path).bind(p_project.path));
	row->add_child(remove_button);
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

void StartupDialog::_open_recent_path(const String &p_path) {
	if (p_path.is_empty()) {
		return;
	}

	KnownProjectStore::KnownProject project;
	if (!known_projects.get_project(p_path, project) || project.missing) {
		return;
	}

	if (_open_project_and_restart(p_path) != OK) {
		ERR_PRINT(vformat("Failed to start an editor instance for the project at '%s'.", p_path));
	}
}

void StartupDialog::_remove_recent_path(const String &p_path) {
	if (p_path.is_empty()) {
		return;
	}

	if (known_projects.remove_project(p_path)) {
		known_projects.save();
	}
	_refresh_recents();
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
	EditorInterface::get_singleton()->popup_dialog_centered_clamped(this, Size2i(560, 0) * EDSCALE, 0.85);
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

	MarginContainer *outer_margin = memnew(MarginContainer);
	outer_margin->add_theme_constant_override("margin_left", 12 * EDSCALE);
	outer_margin->add_theme_constant_override("margin_right", 12 * EDSCALE);
	outer_margin->add_theme_constant_override("margin_top", 8 * EDSCALE);
	outer_margin->add_theme_constant_override("margin_bottom", 8 * EDSCALE);
	add_child(outer_margin);

	VBoxContainer *root = memnew(VBoxContainer);
	root->add_theme_constant_override("separation", 16 * EDSCALE);
	root->set_custom_minimum_size(Size2(560, 0) * EDSCALE);
	outer_margin->add_child(root);

	{
		HBoxContainer *header = memnew(HBoxContainer);
		header->add_theme_constant_override("separation", 14 * EDSCALE);
		header->set_alignment(BoxContainer::ALIGNMENT_BEGIN);
		root->add_child(header);

		logo = memnew(TextureRect);
		logo->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		logo->set_custom_minimum_size(Size2(56, 56) * EDSCALE);
		logo->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
		header->add_child(logo);

		VBoxContainer *header_text = memnew(VBoxContainer);
		header_text->add_theme_constant_override("separation", 2 * EDSCALE);
		header_text->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		header_text->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
		header->add_child(header_text);

		HBoxContainer *product_line = memnew(HBoxContainer);
		product_line->add_theme_constant_override("separation", 8 * EDSCALE);
		product_line->set_alignment(BoxContainer::ALIGNMENT_BEGIN);
		header_text->add_child(product_line);

		product_name_label = memnew(Label);
		product_name_label->set_text(FOUNDRY_VERSION_NAME);
		product_name_label->set_accessibility_name(TTRC("Foundry Engine"));
		product_name_label->add_theme_font_size_override(SceneStringName(font_size), 24 * EDSCALE);
		product_line->add_child(product_name_label);

		version_label = memnew(Label);
		version_label->set_text(String(FOUNDRY_VERSION_NUMBER) + " " + FOUNDRY_VERSION_STATUS);
		version_label->set_v_size_flags(Control::SIZE_SHRINK_END);
		product_line->add_child(version_label);

		tagline_label = memnew(Label);
		tagline_label->set_text(TTRC("Create or open a project workspace"));
		header_text->add_child(tagline_label);
	}

	root->add_child(memnew(HSeparator));

	tabs = memnew(TabContainer);
	tabs->set_accessibility_name(TTRC("Startup Dialog Tabs"));
	tabs->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tabs->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	tabs->set_custom_minimum_size(Size2(0, 360) * EDSCALE);
	root->add_child(tabs);

	{
		MarginContainer *projects_margin = memnew(MarginContainer);
		projects_margin->set_name(TTRC("Projects"));
		projects_margin->add_theme_constant_override("margin_left", 4 * EDSCALE);
		projects_margin->add_theme_constant_override("margin_right", 4 * EDSCALE);
		projects_margin->add_theme_constant_override("margin_top", 12 * EDSCALE);
		projects_margin->add_theme_constant_override("margin_bottom", 4 * EDSCALE);
		tabs->add_child(projects_margin);
		projects_tab = projects_margin;

		VBoxContainer *projects_body = memnew(VBoxContainer);
		projects_body->add_theme_constant_override("separation", 10 * EDSCALE);
		projects_margin->add_child(projects_body);

		Label *start_label = memnew(Label);
		start_label->set_text(TTRC("Start a project"));
		projects_body->add_child(start_label);

		HBoxContainer *primary_actions = memnew(HBoxContainer);
		primary_actions->add_theme_constant_override("separation", 10 * EDSCALE);
		projects_body->add_child(primary_actions);

		create_project_button = memnew(Button);
		create_project_button->set_text(TTRC("Create Project"));
		create_project_button->set_accessibility_name(TTRC("Create Project"));
		create_project_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		create_project_button->set_custom_minimum_size(Size2(0, 40) * EDSCALE);
		create_project_button->set_shortcut(ED_SHORTCUT("startup_dialog/create_project", TTRC("Create Project"), KeyModifierMask::CMD_OR_CTRL | Key::N));
		create_project_button->connect(SceneStringName(pressed), callable_mp(this, &StartupDialog::_create_project));
		primary_actions->add_child(create_project_button);

		open_existing_button = memnew(Button);
		open_existing_button->set_text(TTRC("Open Existing Project"));
		open_existing_button->set_accessibility_name(TTRC("Open Existing Project"));
		open_existing_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		open_existing_button->set_custom_minimum_size(Size2(0, 40) * EDSCALE);
		open_existing_button->set_shortcut(ED_SHORTCUT("startup_dialog/open_existing", TTRC("Open Existing Project"), KeyModifierMask::CMD_OR_CTRL | Key::O));
		open_existing_button->connect(SceneStringName(pressed), callable_mp(this, &StartupDialog::_open_existing_project));
		primary_actions->add_child(open_existing_button);

		projects_body->add_child(memnew(HSeparator));

		Label *recent_label = memnew(Label);
		recent_label->set_text(TTRC("Recent"));
		projects_body->add_child(recent_label);

		recents_scroll = memnew(ScrollContainer);
		recents_scroll->set_accessibility_name(TTRC("Recent Projects"));
		recents_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
		recents_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		projects_body->add_child(recents_scroll);

		recents_container = memnew(VBoxContainer);
		recents_container->add_theme_constant_override("separation", 4 * EDSCALE);
		recents_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		recents_scroll->add_child(recents_container);

		recents_empty_state = memnew(VBoxContainer);
		recents_empty_state->set_alignment(BoxContainer::ALIGNMENT_CENTER);
		recents_empty_state->add_theme_constant_override("separation", 4 * EDSCALE);
		recents_empty_state->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		recents_empty_state->hide();
		projects_body->add_child(recents_empty_state);

		Label *empty_title = memnew(Label);
		empty_title->set_text(TTRC("No recent projects yet"));
		empty_title->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		recents_empty_state->add_child(empty_title);

		Label *empty_hint = memnew(Label);
		empty_hint->set_text(TTRC("Create a project or open an existing one to get started."));
		empty_hint->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		empty_hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		empty_hint->set_modulate(Color(1, 1, 1, 0.5));
		recents_empty_state->add_child(empty_hint);
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
