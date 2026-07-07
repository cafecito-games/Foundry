/**************************************************************************/
/*  text_tab_surface.h                                                    */
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

#include "core/variant/callable.h"
#include "scene/gui/box_container.h"

#include "editor/workspace/text_document.h"
#include "editor/workspace/text_view.h"

class Button;
class ConfirmationDialog;
class TextViewRegistry;

/**
 * The mounted surface for one TextTab: the mode-toggle chrome plus the active
 * TextView's control, over a shared TextDocument. Recreated whenever the tab is
 * mounted (including when it moves panes); the document it renders is owned by
 * the tab type and outlives it, so unsaved edits survive the recreation.
 *
 * The toggle bar is hidden while a file offers only one view mode (the v1 case
 * for plain text), and appears automatically once a second view is registered
 * for the extension.
 */
class TextTabSurface : public VBoxContainer {
	FOUNDRY_CLASS(TextTabSurface, VBoxContainer);

	Ref<TextDocument> document;
	TextViewRegistry *registry = nullptr; // Borrowed; the singleton outlives this.

	HBoxContainer *toggle_bar = nullptr;
	Control *view_host = nullptr;
	ConfirmationDialog *close_confirm = nullptr;

	Ref<TextView> active_view;
	StringName active_mode;

	Callable deferred_close;

	void _rebuild_toggle_bar();
	void _update_toggle_pressed();
	void _mount_view(const StringName &p_mode_id);
	void _on_mode_selected(const StringName &p_mode_id);
	void _on_save_confirmed();
	void _on_discard_pressed();
	void _finish_deferred_close();

public:
	void configure(const Ref<TextDocument> &p_document, TextViewRegistry *p_registry, const StringName &p_initial_mode);

	Ref<TextDocument> get_document() const { return document; }
	StringName get_active_mode() const { return active_mode; }
	TextView *get_active_view() const { return active_view.ptr(); }

	void switch_to_mode(const StringName &p_mode_id);
	void apply_view_state(int p_caret_line, int p_caret_column, int p_scroll);
	int get_caret_line() const;
	int get_caret_column() const;
	int get_scroll() const;
	void flush_to_document();
	void focus_view();

	// When the document is dirty, shows a save/discard/cancel prompt and returns
	// true (deferred). p_on_close is invoked once if the prompt resolves to save
	// or discard, never on cancel. Returns false immediately when clean.
	bool begin_close(const Callable &p_on_close);

	TextTabSurface();
};
