/**************************************************************************/
/*  text_view_registry.h                                                  */
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

#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

#include "editor/workspace/text_view.h"

/**
 * Maps a file extension (or detected format) to the ordered set of available
 * TextView factories. Every format offers the built-in Source view; a format
 * may register extra views (e.g. a Markdown preview) that appear alongside it.
 *
 * This is the extension point that keeps "specialized rendering" a view mode
 * rather than a second tab: registering a view for an extension makes it
 * offered by every TextTab of that extension, with no change to the tab type.
 */
class TextViewRegistry {
public:
	typedef Ref<TextView> (*ViewFactory)(const Ref<TextDocument> &p_document);

	struct ViewEntry {
		StringName mode_id;
		String label;
		ViewFactory factory = nullptr;
	};

private:
	// Views offered for every extension (Source), in registration order.
	Vector<ViewEntry> default_views;
	// Extra views keyed by lower-cased extension, in registration order.
	HashMap<String, Vector<ViewEntry>> views_by_extension;

	void register_builtin_views();

public:
	static TextViewRegistry *get_singleton();

	void register_default_view(const StringName &p_mode_id, const String &p_label, ViewFactory p_factory);
	void register_view(const String &p_extension, const StringName &p_mode_id, const String &p_label, ViewFactory p_factory);

	// The ordered views available for a path: the default views first, then any
	// views registered for the path's extension.
	Vector<ViewEntry> get_views_for_path(const String &p_path) const;
	bool has_view(const String &p_path, const StringName &p_mode_id) const;
	// The mode id of the first (default) view offered for a path.
	StringName default_mode_for_path(const String &p_path) const;
	// Instantiate the view with p_mode_id for the path, bound to p_document.
	// Falls back to the default view when the requested mode is unavailable.
	Ref<TextView> create_view(const String &p_path, const StringName &p_mode_id, const Ref<TextDocument> &p_document) const;

	TextViewRegistry();
};
