/**************************************************************************/
/*  text_view_registry.cpp                                                */
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

#include "text_view_registry.h"

#include "editor/workspace/source_text_view.h"

TextViewRegistry::TextViewRegistry() {
	register_builtin_views();
}

TextViewRegistry *TextViewRegistry::get_singleton() {
	// Process-wide, mirroring the shared WorkspaceTabRegistry: view providers are
	// registered once and outlive every tab that queries them.
	static TextViewRegistry registry;
	return &registry;
}

void TextViewRegistry::register_builtin_views() {
	register_default_view(StringName("source"), TTR("Source"), &SourceTextView::create);
}

void TextViewRegistry::register_default_view(const StringName &p_mode_id, const String &p_label, ViewFactory p_factory) {
	ERR_FAIL_NULL(p_factory);
	ViewEntry entry;
	entry.mode_id = p_mode_id;
	entry.label = p_label;
	entry.factory = p_factory;
	default_views.push_back(entry);
}

void TextViewRegistry::register_view(const String &p_extension, const StringName &p_mode_id, const String &p_label, ViewFactory p_factory) {
	ERR_FAIL_NULL(p_factory);
	ViewEntry entry;
	entry.mode_id = p_mode_id;
	entry.label = p_label;
	entry.factory = p_factory;
	views_by_extension[p_extension.to_lower()].push_back(entry);
}

Vector<TextViewRegistry::ViewEntry> TextViewRegistry::get_views_for_path(const String &p_path) const {
	Vector<ViewEntry> views = default_views;
	const String extension = p_path.get_extension().to_lower();
	if (const Vector<ViewEntry> *extra = views_by_extension.getptr(extension)) {
		for (const ViewEntry &entry : *extra) {
			views.push_back(entry);
		}
	}
	return views;
}

bool TextViewRegistry::has_view(const String &p_path, const StringName &p_mode_id) const {
	for (const ViewEntry &entry : get_views_for_path(p_path)) {
		if (entry.mode_id == p_mode_id) {
			return true;
		}
	}
	return false;
}

StringName TextViewRegistry::default_mode_for_path(const String &p_path) const {
	const Vector<ViewEntry> views = get_views_for_path(p_path);
	if (views.is_empty()) {
		return StringName();
	}
	return views[0].mode_id;
}

Ref<TextView> TextViewRegistry::create_view(const String &p_path, const StringName &p_mode_id, const Ref<TextDocument> &p_document) const {
	const Vector<ViewEntry> views = get_views_for_path(p_path);
	ERR_FAIL_COND_V_MSG(views.is_empty(), Ref<TextView>(), "No text views registered.");

	const ViewEntry *selected = &views[0];
	for (const ViewEntry &entry : views) {
		if (entry.mode_id == p_mode_id) {
			selected = &entry;
			break;
		}
	}
	ERR_FAIL_NULL_V(selected->factory, Ref<TextView>());
	return selected->factory(p_document);
}
