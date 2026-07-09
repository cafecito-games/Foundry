/**************************************************************************/
/*  startup_dialog.cpp                                                    */
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

#include "startup_dialog.h"

#include "core/io/file_access.h"
#include "core/os/keyboard.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/version.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/gui/editor_about.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/project_manager/project_dialog.h"
#include "editor/project_manager/project_scanner.h"
#include "editor/project_manager/project_tag.h"
#include "editor/project_manager/startup_router.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "main/main.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
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
	if (scan_button != nullptr) {
		scan_button->set_button_icon(editor_theme->get_icon(SNAME("Search"), EditorStringName(EditorIcons)));
	}
	if (remove_missing_button != nullptr) {
		remove_missing_button->set_button_icon(editor_theme->get_icon(SNAME("Remove"), EditorStringName(EditorIcons)));
	}
	if (manage_add_tag_button != nullptr) {
		manage_add_tag_button->set_button_icon(editor_theme->get_icon(SNAME("Add"), EditorStringName(EditorIcons)));
	}

	// Cards are rebuilt with the current theme so their icons/colors stay in sync.
	_refresh_recents();
	_refresh_manage_list();
}

bool StartupDialog::manage_filter_matches(const KnownProjectStore::KnownProject &p_project, const String &p_query) {
	const String query = p_query.strip_edges().to_lower();
	if (query.is_empty()) {
		return true;
	}

	const String display_name = p_project.display_name.is_empty() ? p_project.path.get_file() : p_project.display_name;
	if (display_name.to_lower().contains(query)) {
		return true;
	}
	if (p_project.path.to_lower().contains(query)) {
		return true;
	}
	for (const String &tag : p_project.tags) {
		if (tag.to_lower().contains(query)) {
			return true;
		}
	}
	return false;
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
	_refresh_manage_list();
}

void StartupDialog::_on_project_created(const String &p_dir, bool p_edit) {
	known_projects.add_project(p_dir);
	known_projects.save();
	_refresh_recents();
	_refresh_manage_list();

	if (p_edit) {
		if (_open_project_and_restart(p_dir) != OK) {
			ERR_PRINT(vformat("Failed to open newly created project at '%s'.", p_dir));
		}
	}
}

void StartupDialog::_on_project_duplicated(const String &p_original_path, const String &p_duplicate_path, bool p_edit) {
	known_projects.add_project(p_duplicate_path);
	known_projects.save();
	_refresh_recents();
	_refresh_manage_list();

	if (p_edit) {
		if (_open_project_and_restart(p_duplicate_path) != OK) {
			ERR_PRINT(vformat("Failed to open duplicated project at '%s'.", p_duplicate_path));
		}
	}
}

void StartupDialog::_on_projects_updated() {
	_refresh_recents();
	_refresh_manage_list();
}

// -- Manage tab -----------------------------------------------------------

void StartupDialog::_build_manage_tab(Control *p_parent) {
	VBoxContainer *body = memnew(VBoxContainer);
	body->add_theme_constant_override("separation", 10 * EDSCALE);
	p_parent->add_child(body);

	HBoxContainer *toolbar = memnew(HBoxContainer);
	toolbar->add_theme_constant_override("separation", 8 * EDSCALE);
	body->add_child(toolbar);

	manage_filter = memnew(LineEdit);
	manage_filter->set_placeholder(TTRC("Filter by name, path, or tag..."));
	manage_filter->set_accessibility_name(TTRC("Filter Projects"));
	manage_filter->set_clear_button_enabled(true);
	manage_filter->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	manage_filter->connect(SceneStringName(text_changed), callable_mp(this, &StartupDialog::_on_manage_filter_changed));
	toolbar->add_child(manage_filter);

	scan_button = memnew(Button);
	scan_button->set_text(TTRC("Scan Folder"));
	scan_button->set_accessibility_name(TTRC("Scan Folder"));
	scan_button->connect(SceneStringName(pressed), callable_mp(this, &StartupDialog::_scan_folder));
	toolbar->add_child(scan_button);

	remove_missing_button = memnew(Button);
	remove_missing_button->set_text(TTRC("Remove Missing"));
	remove_missing_button->set_accessibility_name(TTRC("Remove Missing Projects"));
	// Visually secondary: a flat button so bulk cleanup does not compete with Scan.
	remove_missing_button->set_theme_type_variation(SNAME("FlatButton"));
	remove_missing_button->connect(SceneStringName(pressed), callable_mp(this, &StartupDialog::_prompt_remove_missing));
	toolbar->add_child(remove_missing_button);

	HBoxContainer *split = memnew(HBoxContainer);
	split->add_theme_constant_override("separation", 12 * EDSCALE);
	split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	body->add_child(split);

	// Left: known-project list.
	VBoxContainer *list_column = memnew(VBoxContainer);
	list_column->add_theme_constant_override("separation", 6 * EDSCALE);
	list_column->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	list_column->set_stretch_ratio(1.4);
	split->add_child(list_column);

	Label *known_label = memnew(Label);
	known_label->set_text(TTRC("Known projects"));
	list_column->add_child(known_label);

	manage_list_scroll = memnew(ScrollContainer);
	manage_list_scroll->set_accessibility_name(TTRC("Known Projects"));
	manage_list_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	manage_list_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	list_column->add_child(manage_list_scroll);

	manage_list_container = memnew(VBoxContainer);
	manage_list_container->add_theme_constant_override("separation", 4 * EDSCALE);
	manage_list_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	manage_list_scroll->add_child(manage_list_container);

	manage_empty_state = memnew(VBoxContainer);
	manage_empty_state->set_alignment(BoxContainer::ALIGNMENT_CENTER);
	manage_empty_state->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	manage_empty_state->hide();
	list_column->add_child(manage_empty_state);

	manage_empty_hint = memnew(Label);
	manage_empty_hint->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	manage_empty_hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	manage_empty_hint->set_modulate(Color(1, 1, 1, 0.5));
	manage_empty_state->add_child(manage_empty_hint);

	// Right: selected-project detail pane.
	VBoxContainer *detail_column = memnew(VBoxContainer);
	detail_column->add_theme_constant_override("separation", 6 * EDSCALE);
	detail_column->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	split->add_child(detail_column);

	Label *detail_label = memnew(Label);
	detail_label->set_text(TTRC("Selected project"));
	detail_column->add_child(detail_label);

	manage_detail_empty = memnew(VBoxContainer);
	manage_detail_empty->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	detail_column->add_child(manage_detail_empty);

	Label *detail_empty_hint = memnew(Label);
	detail_empty_hint->set_text(TTRC("Select a project to manage it."));
	detail_empty_hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	detail_empty_hint->set_modulate(Color(1, 1, 1, 0.5));
	manage_detail_empty->add_child(detail_empty_hint);

	manage_detail_pane = memnew(VBoxContainer);
	manage_detail_pane->add_theme_constant_override("separation", 6 * EDSCALE);
	manage_detail_pane->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	manage_detail_pane->hide();
	detail_column->add_child(manage_detail_pane);

	manage_detail_name = memnew(Label);
	manage_detail_name->set_clip_text(true);
	manage_detail_pane->add_child(manage_detail_name);

	manage_detail_path = memnew(Label);
	manage_detail_path->set_clip_text(true);
	manage_detail_path->set_structured_text_bidi_override(TextServer::STRUCTURED_TEXT_FILE);
	manage_detail_path->set_modulate(Color(1, 1, 1, 0.5));
	manage_detail_pane->add_child(manage_detail_path);

	manage_missing_label = memnew(Label);
	manage_missing_label->set_text(TTRC("This project's folder is missing."));
	manage_missing_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	manage_missing_label->set_modulate(Color(1, 0.5, 0.5));
	manage_missing_label->hide();
	manage_detail_pane->add_child(manage_missing_label);

	manage_open_button = memnew(Button);
	manage_open_button->set_text(TTRC("Open Project"));
	manage_open_button->set_accessibility_name(TTRC("Open Project"));
	manage_open_button->connect(SceneStringName(pressed), callable_mp(this, &StartupDialog::_manage_open_selected));
	manage_detail_pane->add_child(manage_open_button);

	manage_duplicate_button = memnew(Button);
	manage_duplicate_button->set_text(TTRC("Duplicate..."));
	manage_duplicate_button->set_accessibility_name(TTRC("Duplicate Project"));
	manage_duplicate_button->connect(SceneStringName(pressed), callable_mp(this, &StartupDialog::_manage_duplicate_selected));
	manage_detail_pane->add_child(manage_duplicate_button);

	manage_reveal_button = memnew(Button);
	manage_reveal_button->set_text(TTRC("Reveal Folder"));
	manage_reveal_button->set_accessibility_name(TTRC("Reveal Folder"));
	manage_reveal_button->connect(SceneStringName(pressed), callable_mp(this, &StartupDialog::_manage_reveal_selected));
	manage_detail_pane->add_child(manage_reveal_button);

	manage_detail_pane->add_child(memnew(HSeparator));

	Label *tags_label = memnew(Label);
	tags_label->set_text(TTRC("Tags"));
	manage_detail_pane->add_child(tags_label);

	manage_tags_row = memnew(HBoxContainer);
	manage_tags_row->add_theme_constant_override("separation", 4 * EDSCALE);
	manage_detail_pane->add_child(manage_tags_row);

	manage_add_tag_button = memnew(Button);
	manage_add_tag_button->set_text(TTRC("Add Tag"));
	manage_add_tag_button->set_accessibility_name(TTRC("Add Tag"));
	manage_add_tag_button->connect(SceneStringName(pressed), callable_mp(this, &StartupDialog::_prompt_add_tag));
	manage_detail_pane->add_child(manage_add_tag_button);
}

void StartupDialog::_clear_manage_rows() {
	if (manage_list_container == nullptr) {
		return;
	}
	for (int i = manage_list_container->get_child_count() - 1; i >= 0; i--) {
		Node *child = manage_list_container->get_child(i);
		manage_list_container->remove_child(child);
		child->queue_free();
	}
}

void StartupDialog::_refresh_manage_list() {
	if (manage_list_container == nullptr) {
		return;
	}

	_clear_manage_rows();

	Vector<KnownProjectStore::KnownProject> known = known_projects.get_known_projects();
	int shown = 0;
	bool selection_visible = false;
	for (const KnownProjectStore::KnownProject &project : known) {
		if (!manage_filter_matches(project, manage_filter_query)) {
			continue;
		}
		_build_manage_row(project);
		shown++;
		if (project.path == manage_selected_path) {
			selection_visible = true;
		}
	}

	// Drop a selection that filtering hid so the detail pane never references a
	// project the user can no longer see.
	if (!selection_visible) {
		manage_selected_path = String();
	}

	const bool has_rows = shown > 0;
	manage_list_scroll->set_visible(has_rows);
	manage_empty_state->set_visible(!has_rows);
	if (!has_rows && manage_empty_hint != nullptr) {
		manage_empty_hint->set_text(known.is_empty()
						? TTRC("No known projects yet. Use Scan Folder to discover projects.")
						: TTRC("No projects match the filter."));
	}

	// Disable bulk cleanup unless at least one known project is actually missing.
	bool any_missing = false;
	for (const KnownProjectStore::KnownProject &project : known) {
		if (project.missing) {
			any_missing = true;
			break;
		}
	}
	if (remove_missing_button != nullptr) {
		remove_missing_button->set_disabled(!any_missing);
	}

	_refresh_manage_detail();
}

void StartupDialog::_build_manage_row(const KnownProjectStore::KnownProject &p_project) {
	Ref<Theme> editor_theme = get_theme();

	Button *row = memnew(Button);
	row->set_theme_type_variation(SNAME("FlatButton"));
	row->set_toggle_mode(true);
	row->set_pressed_no_signal(p_project.path == manage_selected_path);
	row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	row->set_custom_minimum_size(Size2(0, 48) * EDSCALE);
	row->set_clip_text(true);
	row->set_tooltip_text(p_project.path);
	row->set_accessibility_name(vformat(TTRC("Select project %s"), _recent_display_name(p_project)));
	row->connect(SceneStringName(pressed), callable_mp(this, &StartupDialog::_select_manage_project).bind(p_project.path));
	manage_list_container->add_child(row);

	MarginContainer *row_margin = memnew(MarginContainer);
	row_margin->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	row_margin->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	row_margin->add_theme_constant_override(SNAME("margin_left"), 10 * EDSCALE);
	row_margin->add_theme_constant_override(SNAME("margin_right"), 6 * EDSCALE);
	row->add_child(row_margin);

	HBoxContainer *row_hbox = memnew(HBoxContainer);
	row_hbox->add_theme_constant_override(SNAME("separation"), 10 * EDSCALE);
	row_margin->add_child(row_hbox);

	VBoxContainer *text_column = memnew(VBoxContainer);
	text_column->add_theme_constant_override(SNAME("separation"), 0);
	text_column->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	text_column->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	row_hbox->add_child(text_column);

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

	if (!p_project.tags.is_empty()) {
		Label *tags_label = memnew(Label);
		tags_label->set_text(String(" ").join(p_project.tags));
		tags_label->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
		tags_label->set_modulate(Color(1, 1, 1, 0.5));
		row_hbox->add_child(tags_label);
	}

	// Missing entries expose an inline removal action; valid entries are managed
	// from the detail pane and the recents tab.
	if (p_project.missing) {
		Button *remove_button = memnew(Button);
		remove_button->set_theme_type_variation(SNAME("FlatButton"));
		remove_button->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
		remove_button->set_mouse_filter(Control::MOUSE_FILTER_STOP);
		remove_button->set_tooltip_text(TTRC("Remove from list"));
		remove_button->set_accessibility_name(vformat(TTRC("Remove %s from list"), _recent_display_name(p_project)));
		if (editor_theme.is_valid()) {
			remove_button->set_button_icon(editor_theme->get_icon(SNAME("Remove"), EditorStringName(EditorIcons)));
		}
		remove_button->connect(SceneStringName(pressed), callable_mp(this, &StartupDialog::_remove_recent_path).bind(p_project.path));
		row_hbox->add_child(remove_button);
	}
}

void StartupDialog::_select_manage_project(const String &p_path) {
	manage_selected_path = p_path;
	// Re-render rows so the pressed state tracks the new selection, then update
	// the detail pane.
	_refresh_manage_list();
}

void StartupDialog::_clear_manage_tags() {
	if (manage_tags_row == nullptr) {
		return;
	}
	for (int i = manage_tags_row->get_child_count() - 1; i >= 0; i--) {
		Node *child = manage_tags_row->get_child(i);
		manage_tags_row->remove_child(child);
		child->queue_free();
	}
}

void StartupDialog::_refresh_manage_detail() {
	if (manage_detail_pane == nullptr) {
		return;
	}

	KnownProjectStore::KnownProject project;
	const bool has_selection = !manage_selected_path.is_empty() && known_projects.get_project(manage_selected_path, project);

	manage_detail_pane->set_visible(has_selection);
	manage_detail_empty->set_visible(!has_selection);
	if (!has_selection) {
		return;
	}

	manage_detail_name->set_text(_recent_display_name(project));
	manage_detail_path->set_text(project.path);
	manage_missing_label->set_visible(project.missing);

	// Actions that require a valid project folder are disabled for missing entries.
	manage_open_button->set_disabled(project.missing);
	manage_duplicate_button->set_disabled(project.missing);
	manage_reveal_button->set_disabled(project.missing);
	manage_add_tag_button->set_disabled(project.missing);

	_clear_manage_tags();
	for (const String &tag : project.tags) {
		ProjectTag *tag_control = memnew(ProjectTag(tag, !project.missing));
		manage_tags_row->add_child(tag_control);
		if (!project.missing) {
			tag_control->connect_button_to(callable_mp(this, &StartupDialog::_remove_selected_tag).bind(tag));
		}
	}
}

void StartupDialog::_on_manage_filter_changed(const String &p_text) {
	manage_filter_query = p_text;
	_refresh_manage_list();
}

void StartupDialog::_scan_folder() {
	if (scan_dir_dialog != nullptr) {
		scan_dir_dialog->popup_file_dialog();
	}
}

void StartupDialog::_on_scan_dir_selected(const String &p_dir) {
	if (project_scanner != nullptr) {
		project_scanner->scan_folder(p_dir);
	}
}

void StartupDialog::_on_scan_finished(const PackedStringArray &p_found) {
	// add_project dedups by canonical path, so re-discovering known projects is a
	// no-op rather than a duplicate entry.
	for (const String &path : p_found) {
		known_projects.add_project(path);
	}
	if (!p_found.is_empty()) {
		known_projects.save();
	}
	_refresh_recents();
	_refresh_manage_list();
}

void StartupDialog::_prompt_remove_missing() {
	if (remove_missing_dialog != nullptr) {
		remove_missing_dialog->popup_centered();
	}
}

void StartupDialog::_confirm_remove_missing() {
	const int removed = known_projects.remove_missing_projects();
	if (removed > 0) {
		known_projects.save();
	}
	_refresh_recents();
	_refresh_manage_list();
}

void StartupDialog::_manage_open_selected() {
	if (manage_selected_path.is_empty()) {
		return;
	}
	_open_recent_path(manage_selected_path);
}

void StartupDialog::_manage_duplicate_selected() {
	if (manage_selected_path.is_empty()) {
		return;
	}
	KnownProjectStore::KnownProject project;
	if (!known_projects.get_project(manage_selected_path, project) || project.missing) {
		return;
	}

	project_dialog->set_mode(ProjectDialog::MODE_DUPLICATE);
	project_dialog->set_project_name(vformat("%s (%s)", _recent_display_name(project), TTR("Copy")));
	project_dialog->set_original_project_path(project.path);
	project_dialog->set_duplicate_can_edit(true);
	project_dialog->show_dialog(false);
}

void StartupDialog::_manage_reveal_selected() {
	if (manage_selected_path.is_empty()) {
		return;
	}
	KnownProjectStore::KnownProject project;
	if (!known_projects.get_project(manage_selected_path, project) || project.missing) {
		return;
	}
	OS::get_singleton()->shell_show_in_file_manager(project.path, true);
}

void StartupDialog::_prompt_add_tag() {
	if (manage_selected_path.is_empty() || add_tag_dialog == nullptr) {
		return;
	}
	add_tag_error->set_text(String());
	add_tag_dialog->get_ok_button()->set_disabled(true);
	add_tag_dialog->popup_centered(Size2(360, 0) * EDSCALE);
}

void StartupDialog::_validate_new_tag(const String &p_name) {
	add_tag_dialog->get_ok_button()->set_disabled(true);

	const String stripped = p_name.strip_edges();
	if (stripped.is_empty()) {
		add_tag_error->set_text(TTRC("Tag name can't be empty."));
		return;
	}
	if (stripped[0] == '_' || stripped[stripped.length() - 1] == '_') {
		add_tag_error->set_text(TTRC("Tag name can't begin or end with underscore."));
		return;
	}
	// Spaces are normalized to underscores on save, so reject runs of spaces or
	// underscores that would collapse into consecutive underscores. This matches
	// the project manager's tag rules so Manage cannot write tags it rejects.
	bool was_underscore = false;
	for (const char32_t &c : stripped.span()) {
		if (c == '_' || c == ' ') {
			if (was_underscore) {
				add_tag_error->set_text(TTRC("Tag name can't contain consecutive underscores or spaces."));
				return;
			}
			was_underscore = true;
		} else {
			was_underscore = false;
		}
	}
	const String forbidden = "/\\-";
	for (const char32_t &c : stripped.span()) {
		if (forbidden.contains_char(c)) {
			add_tag_error->set_text(TTRC("Tag name can't contain '/', '\\', or '-'."));
			return;
		}
	}

	add_tag_error->set_text(String());
	add_tag_dialog->get_ok_button()->set_disabled(false);
}

void StartupDialog::_confirm_add_tag() {
	if (!add_tag_error->get_text().is_empty()) {
		return;
	}
	const String new_tag = add_tag_name->get_text().strip_edges().to_lower().replace_char(' ', '_');
	if (new_tag.is_empty() || manage_selected_path.is_empty()) {
		return;
	}
	add_tag_dialog->hide();

	KnownProjectStore::KnownProject project;
	if (!known_projects.get_project(manage_selected_path, project) || project.missing) {
		return;
	}
	if (project.tags.has(new_tag)) {
		return;
	}
	PackedStringArray tags = project.tags;
	tags.push_back(new_tag);
	_apply_selected_tags(tags);
}

void StartupDialog::_remove_selected_tag(const String &p_tag) {
	if (manage_selected_path.is_empty()) {
		return;
	}
	KnownProjectStore::KnownProject project;
	if (!known_projects.get_project(manage_selected_path, project) || project.missing) {
		return;
	}
	PackedStringArray tags;
	for (const String &tag : project.tags) {
		if (tag != p_tag) {
			tags.push_back(tag);
		}
	}
	_apply_selected_tags(tags);
}

void StartupDialog::_apply_selected_tags(const PackedStringArray &p_tags) {
	const Error err = known_projects.set_project_tags(manage_selected_path, p_tags);
	if (err != OK) {
		ERR_PRINT(vformat("Failed to persist tags for project at '%s' (error %d).", manage_selected_path, err));
		if (manage_error_dialog != nullptr) {
			manage_error_dialog->set_text(vformat(TTR("Couldn't save tags for the project at '%s' (error %d).\nThe project's project.foundry may be read-only or missing."), manage_selected_path, err));
			manage_error_dialog->popup_centered();
		}
		return;
	}
	// set_project_tags refreshes this entry's cache; persist the store so the
	// cached tags survive a relaunch, then re-render the list and detail pane.
	known_projects.save();
	_refresh_manage_list();
}

// -- About tab ------------------------------------------------------------

void StartupDialog::_build_about_tab(Control *p_parent) {
	ScrollContainer *scroll = memnew(ScrollContainer);
	scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	p_parent->add_child(scroll);

	VBoxContainer *body = memnew(VBoxContainer);
	body->add_theme_constant_override("separation", 10 * EDSCALE);
	body->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	scroll->add_child(body);

	Label *product_label = memnew(Label);
	product_label->set_text(FOUNDRY_VERSION_NAME);
	product_label->set_accessibility_name(TTRC("Foundry Engine"));
	product_label->add_theme_font_size_override(SceneStringName(font_size), 20 * EDSCALE);
	body->add_child(product_label);

	// Exact version string, sourced from the same core/version.h macros the editor
	// About dialog and version button use. Development builds that carry a git hash
	// also surface the commit hash and, when recorded, the commit date.
	String version_text = String("v") + FOUNDRY_VERSION_FULL_BUILD;
	const String version_hash = String(FOUNDRY_VERSION_HASH);
	if (!version_hash.is_empty()) {
		version_text += vformat(" [%s]", version_hash.left(9));
		if (FOUNDRY_VERSION_TIMESTAMP > 0) {
			const String commit_date = Time::get_singleton()->get_datetime_string_from_unix_time(FOUNDRY_VERSION_TIMESTAMP, true) + " UTC";
			version_text += "\n" + vformat(TTR("Commit date: %s"), commit_date);
		}
	}
	about_version_label = memnew(Label);
	about_version_label->set_text(version_text);
	about_version_label->set_accessibility_name(TTRC("Foundry Engine Version"));
	about_version_label->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	about_version_label->set_modulate(Color(1, 1, 1, 0.7));
	body->add_child(about_version_label);

	body->add_child(memnew(HSeparator));

	Label *copyright_label = memnew(Label);
	copyright_label->set_text(EditorAbout::get_copyright_text());
	copyright_label->set_accessibility_name(TTRC("Copyright"));
	copyright_label->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	copyright_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	copyright_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	body->add_child(copyright_label);

	Label *license_summary = memnew(Label);
	license_summary->set_text(TTRC("Foundry Engine and Godot Engine are free and open source software released under the permissive MIT license."));
	license_summary->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	license_summary->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	license_summary->set_modulate(Color(1, 1, 1, 0.7));
	body->add_child(license_summary);

	body->add_child(memnew(HSeparator));

	HBoxContainer *actions = memnew(HBoxContainer);
	actions->add_theme_constant_override("separation", 8 * EDSCALE);
	body->add_child(actions);

	Button *credits_button = memnew(Button);
	credits_button->set_text(TTRC("Full Credits"));
	credits_button->set_accessibility_name(TTRC("Full Credits"));
	credits_button->connect(SceneStringName(pressed), callable_mp(this, &StartupDialog::_show_full_credits));
	actions->add_child(credits_button);

	Button *license_button = memnew(Button);
	license_button->set_text(TTRC("License"));
	license_button->set_accessibility_name(TTRC("License"));
	license_button->connect(SceneStringName(pressed), callable_mp(this, &StartupDialog::_show_license));
	actions->add_child(license_button);

	Button *third_party_button = memnew(Button);
	third_party_button->set_text(TTRC("Third-party Notices"));
	third_party_button->set_accessibility_name(TTRC("Third-party Notices"));
	third_party_button->connect(SceneStringName(pressed), callable_mp(this, &StartupDialog::_show_third_party_notices));
	actions->add_child(third_party_button);
}

void StartupDialog::_show_full_credits() {
	if (about_dialog != nullptr) {
		about_dialog->show_section(EditorAbout::SECTION_AUTHORS);
	}
}

void StartupDialog::_show_license() {
	if (about_dialog != nullptr) {
		about_dialog->show_section(EditorAbout::SECTION_LICENSE);
	}
}

void StartupDialog::_show_third_party_notices() {
	if (about_dialog != nullptr) {
		about_dialog->show_section(EditorAbout::SECTION_THIRDPARTY);
	}
}

bool StartupDialog::is_visible_dialog() const {
	return is_visible();
}

void StartupDialog::show_startup_dialog() {
	known_projects.load();
	_refresh_recents();
	_refresh_manage_list();
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
		MarginContainer *manage_margin = memnew(MarginContainer);
		manage_margin->set_name(TTRC("Manage"));
		manage_margin->add_theme_constant_override("margin_left", 4 * EDSCALE);
		manage_margin->add_theme_constant_override("margin_right", 4 * EDSCALE);
		manage_margin->add_theme_constant_override("margin_top", 12 * EDSCALE);
		manage_margin->add_theme_constant_override("margin_bottom", 4 * EDSCALE);
		tabs->add_child(manage_margin);
		manage_tab = manage_margin;

		_build_manage_tab(manage_margin);
	}

	{
		MarginContainer *about_margin = memnew(MarginContainer);
		about_margin->set_name(TTRC("About"));
		about_margin->add_theme_constant_override("margin_left", 4 * EDSCALE);
		about_margin->add_theme_constant_override("margin_right", 4 * EDSCALE);
		about_margin->add_theme_constant_override("margin_top", 12 * EDSCALE);
		about_margin->add_theme_constant_override("margin_bottom", 4 * EDSCALE);
		tabs->add_child(about_margin);
		about_tab = about_margin;

		_build_about_tab(about_margin);
	}

	project_dialog = memnew(ProjectDialog);
	project_dialog->connect("projects_updated", callable_mp(this, &StartupDialog::_on_projects_updated));
	project_dialog->connect("project_created", callable_mp(this, &StartupDialog::_on_project_created));
	project_dialog->connect("project_duplicated", callable_mp(this, &StartupDialog::_on_project_duplicated));
	add_child(project_dialog);

	project_scanner = memnew(ProjectScanner);
	project_scanner->connect("scan_finished", callable_mp(this, &StartupDialog::_on_scan_finished));
	add_child(project_scanner);

	scan_dir_dialog = memnew(EditorFileDialog);
	scan_dir_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	scan_dir_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR);
	scan_dir_dialog->set_title(TTRC("Select a Folder to Scan"));
	scan_dir_dialog->set_current_dir(EDITOR_GET("filesystem/directories/default_project_path"));
	scan_dir_dialog->connect("dir_selected", callable_mp(this, &StartupDialog::_on_scan_dir_selected));
	add_child(scan_dir_dialog);

	remove_missing_dialog = memnew(ConfirmationDialog);
	remove_missing_dialog->set_title(TTRC("Remove Missing Projects"));
	remove_missing_dialog->set_text(TTRC("Remove all projects whose folders no longer exist from the list?\nThe project folders' contents won't be modified."));
	remove_missing_dialog->set_ok_button_text(TTRC("Remove"));
	remove_missing_dialog->connect(SceneStringName(confirmed), callable_mp(this, &StartupDialog::_confirm_remove_missing));
	add_child(remove_missing_dialog);

	manage_error_dialog = memnew(AcceptDialog);
	manage_error_dialog->set_title(TTRC("Error"));
	add_child(manage_error_dialog);

	// Shared About/Credits dialog reused for full credits, license, and
	// third-party notices so the startup dialog never re-embeds that content.
	about_dialog = memnew(EditorAbout);
	add_child(about_dialog);

	{
		add_tag_dialog = memnew(ConfirmationDialog);
		add_tag_dialog->set_title(TTRC("Add Tag"));
		add_tag_dialog->get_ok_button()->connect(SceneStringName(pressed), callable_mp(this, &StartupDialog::_confirm_add_tag));
		add_child(add_tag_dialog);

		VBoxContainer *tag_body = memnew(VBoxContainer);
		add_tag_dialog->add_child(tag_body);

		add_tag_name = memnew(LineEdit);
		add_tag_name->set_accessibility_name(TTRC("New Tag Name"));
		add_tag_name->set_placeholder(TTRC("example_tag (will display as Example Tag)"));
		add_tag_name->connect(SceneStringName(text_changed), callable_mp(this, &StartupDialog::_validate_new_tag));
		add_tag_name->connect(SceneStringName(text_submitted), callable_mp(this, &StartupDialog::_confirm_add_tag).unbind(1));
		tag_body->add_child(add_tag_name);
		add_tag_dialog->connect("about_to_popup", callable_mp(add_tag_name, &LineEdit::clear));
		add_tag_dialog->connect("about_to_popup", callable_mp((Control *)add_tag_name, &Control::grab_focus).bind(false), CONNECT_DEFERRED);

		add_tag_error = memnew(Label);
		add_tag_error->set_modulate(Color(1, 0.4, 0.4));
		tag_body->add_child(add_tag_error);
	}

	tabs->set_current_tab(0);
}

void StartupDialog::_bind_methods() {
}
