/**************************************************************************/
/*  text_tab_surface.cpp                                                  */
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

#include "text_tab_surface.h"

#include "core/object/callable_method_pointer.h"
#include "editor/workspace/text_view_registry.h"
#include "scene/gui/button.h"
#include "scene/gui/dialogs.h"

TextTabSurface::TextTabSurface() {
	set_anchors_and_offsets_preset(PRESET_FULL_RECT);
	set_h_size_flags(SIZE_EXPAND_FILL);
	set_v_size_flags(SIZE_EXPAND_FILL);

	toggle_bar = memnew(HBoxContainer);
	toggle_bar->hide();
	add_child(toggle_bar);

	view_host = memnew(Control);
	view_host->set_h_size_flags(SIZE_EXPAND_FILL);
	view_host->set_v_size_flags(SIZE_EXPAND_FILL);
	add_child(view_host);
}

void TextTabSurface::configure(const Ref<TextDocument> &p_document, TextViewRegistry *p_registry, const StringName &p_initial_mode) {
	ERR_FAIL_COND(p_document.is_null());
	ERR_FAIL_NULL(p_registry);

	document = p_document;
	registry = p_registry;

	StringName mode = p_initial_mode;
	if (mode == StringName() || !registry->has_view(document->get_path(), mode)) {
		mode = registry->default_mode_for_path(document->get_path());
	}

	_rebuild_toggle_bar();
	_mount_view(mode);
}

void TextTabSurface::_rebuild_toggle_bar() {
	for (int i = toggle_bar->get_child_count() - 1; i >= 0; i--) {
		Node *child = toggle_bar->get_child(i);
		toggle_bar->remove_child(child);
		memdelete(child);
	}

	const Vector<TextViewRegistry::ViewEntry> views = registry->get_views_for_path(document->get_path());
	// A single view mode needs no chooser; the bar stays hidden until a second
	// view is registered for the extension.
	toggle_bar->set_visible(views.size() > 1);
	if (views.size() <= 1) {
		return;
	}

	for (const TextViewRegistry::ViewEntry &entry : views) {
		Button *button = memnew(Button);
		button->set_text(entry.label);
		button->set_toggle_mode(true);
		button->set_theme_type_variation("FlatButton");
		button->set_meta(SNAME("mode_id"), entry.mode_id);
		button->set_pressed_no_signal(entry.mode_id == active_mode);
		button->connect(SceneStringName(pressed), callable_mp(this, &TextTabSurface::_on_mode_selected).bind(entry.mode_id));
		toggle_bar->add_child(button);
	}
}

void TextTabSurface::_update_toggle_pressed() {
	for (int i = 0; i < toggle_bar->get_child_count(); i++) {
		Button *button = Object::cast_to<Button>(toggle_bar->get_child(i));
		if (button) {
			button->set_pressed_no_signal(StringName(button->get_meta(SNAME("mode_id"), StringName())) == active_mode);
		}
	}
}

void TextTabSurface::_mount_view(const StringName &p_mode_id) {
	Ref<TextView> view = registry->create_view(document->get_path(), p_mode_id, document);
	ERR_FAIL_COND(view.is_null());

	active_view = view;
	active_mode = view->mode_id();

	Control *control = view->get_control();
	ERR_FAIL_NULL(control);
	view_host->add_child(control);
	control->set_anchors_and_offsets_preset(PRESET_FULL_RECT);
	control->set_h_size_flags(SIZE_EXPAND_FILL);
	control->set_v_size_flags(SIZE_EXPAND_FILL);

	view->sync_from_document();
}

void TextTabSurface::_on_mode_selected(const StringName &p_mode_id) {
	switch_to_mode(p_mode_id);
}

void TextTabSurface::switch_to_mode(const StringName &p_mode_id) {
	if (p_mode_id == active_mode) {
		return;
	}
	if (registry == nullptr || !registry->has_view(document->get_path(), p_mode_id)) {
		return;
	}

	// Push the outgoing view's edits into the document, then swap the mounted
	// control and load the incoming view from the same document.
	if (active_view.is_valid()) {
		active_view->flush_to_document();
	}
	for (int i = view_host->get_child_count() - 1; i >= 0; i--) {
		Node *child = view_host->get_child(i);
		view_host->remove_child(child);
		memdelete(child);
	}
	active_view = Ref<TextView>();

	_mount_view(p_mode_id);
	_update_toggle_pressed();
}

void TextTabSurface::apply_view_state(int p_caret_line, int p_caret_column, int p_scroll) {
	if (active_view.is_valid()) {
		active_view->set_caret(p_caret_line, p_caret_column);
		active_view->set_scroll(p_scroll);
	}
}

int TextTabSurface::get_caret_line() const {
	return active_view.is_valid() ? active_view->get_caret_line() : 0;
}

int TextTabSurface::get_caret_column() const {
	return active_view.is_valid() ? active_view->get_caret_column() : 0;
}

int TextTabSurface::get_scroll() const {
	return active_view.is_valid() ? active_view->get_scroll() : 0;
}

void TextTabSurface::flush_to_document() {
	if (active_view.is_valid()) {
		active_view->flush_to_document();
	}
}

void TextTabSurface::focus_view() {
	if (active_view.is_valid()) {
		active_view->grab_view_focus();
	}
}

bool TextTabSurface::begin_close(const Callable &p_on_close) {
	flush_to_document();
	if (document.is_null() || !document->is_dirty()) {
		return false;
	}

	deferred_close = p_on_close;
	if (!close_confirm) {
		close_confirm = memnew(ConfirmationDialog);
		close_confirm->set_ok_button_text(TTR("Save"));
		Button *discard = close_confirm->add_button(TTR("Discard"), true, "discard");
		discard->connect(SceneStringName(pressed), callable_mp(this, &TextTabSurface::_on_discard_pressed));
		close_confirm->connect(SceneStringName(confirmed), callable_mp(this, &TextTabSurface::_on_save_confirmed));
		add_child(close_confirm);
	}
	close_confirm->set_text(vformat(TTR("Save changes to '%s' before closing?"), document->get_path().get_file()));
	close_confirm->popup_centered();
	return true;
}

void TextTabSurface::_on_save_confirmed() {
	if (document.is_valid()) {
		document->save();
	}
	_finish_deferred_close();
}

void TextTabSurface::_on_discard_pressed() {
	if (close_confirm) {
		close_confirm->hide();
	}
	_finish_deferred_close();
}

void TextTabSurface::_finish_deferred_close() {
	if (deferred_close.is_valid()) {
		const Callable callback = deferred_close;
		deferred_close = Callable();
		callback.call();
	}
}
