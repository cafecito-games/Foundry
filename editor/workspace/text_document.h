/**************************************************************************/
/*  text_document.h                                                       */
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
#include "core/string/ustring.h"

/**
 * The single source of truth for one open text document backing a TextTab.
 *
 * A document owns the canonical text and the dirty flag; one or more editable
 * TextViews are bound to it and render/mutate that text. Because the document
 * (not a view) owns dirty state and content, switching a tab's active view mode
 * -- or moving the tab between panes, which recreates the surface -- never loses
 * unsaved edits: the recreated view re-syncs from this document.
 *
 * Load/save reuse the TextFile resource so encoding and line endings match the
 * script editor's plain-text path byte-for-byte.
 */
class TextDocument : public RefCounted {
	FOUNDRY_CLASS(TextDocument, RefCounted);

	String path;
	String text;
	// The last saved/loaded content; dirty is text != saved_text.
	String saved_text;
	bool dirty = false;
	// True when the last load() of an existing backing file failed. The buffer is
	// then empty but does NOT reflect the file, so saving would truncate it; save()
	// refuses until a successful load replaces the content.
	bool load_failed = false;

public:
	const String &get_path() const { return path; }
	void set_path(const String &p_path) { path = p_path; }

	const String &get_text() const { return text; }
	// Marks the document dirty only when the content actually changes, so
	// re-syncing a view with identical text never fabricates an unsaved state.
	void set_text(const String &p_text);

	bool is_dirty() const { return dirty; }
	void mark_clean() { dirty = false; }
	bool is_load_failed() const { return load_failed; }

	// (Re)load the canonical text from `path` on disk; clears the dirty flag.
	Error load();
	// Write the canonical text to `path`; clears the dirty flag and notifies the
	// editor filesystem so the change is picked up like any other file save.
	Error save();
};
