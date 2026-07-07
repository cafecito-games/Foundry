/**************************************************************************/
/*  source_text_view.h                                                    */
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

#include "core/object/object_id.h"

#include "editor/workspace/text_view.h"

class CodeTextEditor;

/**
 * The default, always-available TextView: a code editor showing the document's
 * text as source, with a per-extension syntax highlighter. Every format offers
 * this view; specialized formats may register additional views alongside it.
 *
 * User edits are pushed into the document live (on text_changed), so the
 * document's dirty flag and content stay authoritative even as the surface is
 * recreated when the tab moves between panes.
 */
class SourceTextView : public TextView {
	FOUNDRY_CLASS(SourceTextView, TextView);

	// The code editor is owned by the scene tree once mounted; a weak id keeps a
	// dangling pointer from surviving a pane-driven free of the surface.
	ObjectID code_editor_id;
	bool syncing = false;

	CodeTextEditor *_editor() const;
	void _on_text_changed();
	void _apply_highlighter(CodeTextEditor *p_editor) const;

public:
	static Ref<TextView> create(const Ref<TextDocument> &p_document);

	virtual StringName mode_id() const override { return StringName("source"); }
	virtual String label() const override;
	virtual Control *get_control() override;
	virtual void sync_from_document() override;
	virtual void flush_to_document() override;
	virtual bool is_editable() const override { return true; }

	virtual int get_caret_line() const override;
	virtual int get_caret_column() const override;
	virtual int get_scroll() const override;
	virtual void set_caret(int p_line, int p_column) override;
	virtual void set_scroll(int p_scroll) override;
	virtual void grab_view_focus() override;
};
