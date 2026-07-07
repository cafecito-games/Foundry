/**************************************************************************/
/*  text_view.h                                                           */
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

#include "core/object/ref_counted.h"
#include "core/string/string_name.h"
#include "core/string/ustring.h"

#include "editor/workspace/text_document.h"

class Control;

/**
 * An editable view mode bound to a TextDocument. A view renders the document's
 * text through some surface (a code editor, later a rendered Markdown control)
 * and writes user edits back into the document, so a specialized rendering can
 * be added later purely as another TextView -- with no change to the tab type,
 * the pane, or persistence.
 *
 * A view is intentionally cheap and recreated on every mount: it never owns the
 * document (which outlives it) and never owns its Control's lifetime past
 * handing it to the pane's chrome host, which parents and frees it.
 */
class TextView : public RefCounted {
	FOUNDRY_CLASS(TextView, RefCounted);

protected:
	Ref<TextDocument> document;

public:
	void set_document(const Ref<TextDocument> &p_document) { document = p_document; }
	Ref<TextDocument> get_document() const { return document; }

	virtual StringName mode_id() const = 0;
	virtual String label() const = 0;

	// The surface mounted under the pane's chrome host. Created lazily; once the
	// caller parents it, the scene tree owns its lifetime.
	virtual Control *get_control() = 0;

	// (Re)load the view from the document's text.
	virtual void sync_from_document() = 0;
	// Write the view's pending edits back into the document.
	virtual void flush_to_document() = 0;

	virtual bool is_editable() const = 0;

	// View-state hooks captured into the tab payload. Views without a caret or
	// scroll (e.g. a future rendered preview) keep the defaults.
	virtual int get_caret_line() const { return 0; }
	virtual int get_caret_column() const { return 0; }
	virtual int get_scroll() const { return 0; }
	virtual void set_caret(int p_line, int p_column) {}
	virtual void set_scroll(int p_scroll) {}
	virtual void grab_view_focus() {}
};
