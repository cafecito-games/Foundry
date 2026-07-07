/**************************************************************************/
/*  text_tab.cpp                                                          */
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

#include "text_tab.h"

#include "core/io/file_access.h"
#include "core/object/object.h"
#include "editor/editor_node.h"
#include "editor/workspace/text_tab_surface.h"
#include "editor/workspace/text_view_registry.h"
#include "scene/gui/control.h"

TextTabType::TextTabType(const StringName &p_type_id) :
		type_id_value(p_type_id) {
}

String TextTabType::derive_title(const String &p_resource_key) {
	if (p_resource_key.is_empty()) {
		return String("Text");
	}
	return p_resource_key.get_file();
}

TextTabSurface *TextTabType::_resolve_surface(int p_stable_id) const {
	const ObjectID *id = mounted_surfaces.getptr(p_stable_id);
	if (!id) {
		return nullptr;
	}
	return Object::cast_to<TextTabSurface>(ObjectDB::get_instance(*id));
}

Ref<TextDocument> TextTabType::_resolve_document(const WorkspaceTab &p_tab) {
	if (Ref<TextDocument> *existing = documents.getptr(p_tab.get_stable_id())) {
		return *existing;
	}
	Ref<TextDocument> document;
	document.instantiate();
	document->set_path(p_tab.get_resource_key());
	// A missing/unreadable file yields an empty buffer; is_resource_available
	// drops persisted tabs for deleted files before we get here on restore.
	document->load();
	documents[p_tab.get_stable_id()] = document;
	return document;
}

TextTabSurface *TextTabType::_create_surface(const WorkspaceTab &p_tab, Control *p_chrome_host) {
	TextTabSurface *surface = memnew(TextTabSurface);
	p_chrome_host->add_child(surface);

	Ref<TextDocument> document = _resolve_document(p_tab);
	const Dictionary &payload = p_tab.get_payload();
	const StringName view_mode = payload.has("view_mode") ? StringName(payload["view_mode"]) : StringName();

	surface->configure(document, TextViewRegistry::get_singleton(), view_mode);

	const int caret_line = payload.has("caret_line") ? (int)payload["caret_line"] : 0;
	const int caret_column = payload.has("caret_column") ? (int)payload["caret_column"] : 0;
	const int scroll = payload.has("scroll") ? (int)payload["scroll"] : 0;
	surface->apply_view_state(caret_line, caret_column, scroll);
	return surface;
}

void TextTabType::_capture_payload(WorkspaceTab &p_tab, TextTabSurface *p_surface) const {
	if (!p_surface) {
		return;
	}
	Dictionary payload;
	payload["view_mode"] = p_surface->get_active_mode();
	payload["caret_line"] = p_surface->get_caret_line();
	payload["caret_column"] = p_surface->get_caret_column();
	payload["scroll"] = p_surface->get_scroll();
	p_tab.set_payload(payload);
}

StringName TextTabType::type_id() const {
	return type_id_value;
}

bool TextTabType::can_open(const String &p_resource) const {
	return !p_resource.is_empty();
}

WorkspaceTab TextTabType::make_tab(const String &p_resource, int p_stable_id) const {
	WorkspaceTab tab;
	tab.set_stable_id(p_stable_id);
	tab.set_type_id(type_id_value);
	tab.set_resource_key(p_resource);
	tab.set_title_cache(derive_title(p_resource));
	tab.set_icon_key_cache("TextFile");
	return tab;
}

String TextTabType::get_title(const WorkspaceTab &p_tab) const {
	return derive_title(p_tab.get_resource_key());
}

Ref<Texture2D> TextTabType::get_icon(const WorkspaceTab &p_tab) const {
	if (EditorNode::get_singleton()) {
		return EditorNode::get_singleton()->get_class_icon("TextFile", "TextFile");
	}
	return Ref<Texture2D>();
}

void TextTabType::mount(WorkspaceTab &p_tab, Control *p_chrome_host) {
	ERR_FAIL_NULL(p_chrome_host);

	TextTabSurface *surface = _resolve_surface(p_tab.get_stable_id());
	if (surface) {
		if (surface->get_parent() != p_chrome_host) {
			if (surface->get_parent()) {
				surface->get_parent()->remove_child(surface);
			}
			p_chrome_host->add_child(surface);
		}
		surface->show();
	} else {
		surface = _create_surface(p_tab, p_chrome_host);
		mounted_surfaces[p_tab.get_stable_id()] = surface->get_instance_id();
	}
	// Focus is claimed in activate(), not here, so a non-focused pane's tab
	// mounted during layout restore does not steal focus from the focused pane.
}

void TextTabType::unmount(WorkspaceTab &p_tab) {
	TextTabSurface *surface = _resolve_surface(p_tab.get_stable_id());
	if (surface) {
		// Flush pending edits into the document, then persist view state before
		// the pane frees the detached surface so a later mount restores it.
		surface->flush_to_document();
		_capture_payload(p_tab, surface);
	}
	mounted_surfaces.erase(p_tab.get_stable_id());

	// Keep only dirty documents alive across the unmount (so a pane move does not
	// lose unsaved edits). A clean document is cheap to reload from disk on the
	// next mount, so drop it to avoid retaining every closed tab's contents.
	if (Ref<TextDocument> *document = documents.getptr(p_tab.get_stable_id())) {
		if (document->is_valid() && !(*document)->is_dirty()) {
			documents.erase(p_tab.get_stable_id());
		}
	}
}

void TextTabType::activate(WorkspaceTab &p_tab) {
	if (TextTabSurface *surface = _resolve_surface(p_tab.get_stable_id())) {
		surface->focus_view();
	}
}

WorkspaceTabCloseResult TextTabType::request_close(WorkspaceTab &p_tab, const Callable &p_on_deferred_close) {
	TextTabSurface *surface = _resolve_surface(p_tab.get_stable_id());
	if (!surface) {
		documents.erase(p_tab.get_stable_id());
		return WorkspaceTabCloseResult::CLOSE;
	}
	// A dirty document drives its own save/discard/cancel prompt and defers the
	// close; the document is dropped later by unmount once it is clean (save) or
	// by process teardown (discard leaves it dirty but the tab is gone).
	if (surface->begin_close(p_on_deferred_close)) {
		return WorkspaceTabCloseResult::DEFERRED;
	}
	documents.erase(p_tab.get_stable_id());
	return WorkspaceTabCloseResult::CLOSE;
}

Dictionary TextTabType::save_payload(const WorkspaceTab &p_tab) const {
	if (TextTabSurface *surface = _resolve_surface(p_tab.get_stable_id())) {
		WorkspaceTab scratch = p_tab;
		_capture_payload(scratch, surface);
		return scratch.get_payload();
	}
	return p_tab.get_payload();
}

void TextTabType::restore_payload(WorkspaceTab &p_tab, const Dictionary &p_payload) const {
	p_tab.set_payload(p_payload);
}

bool TextTabType::is_resource_available(const WorkspaceTab &p_tab) const {
	// The document (and its file) is owned by the tab, so the tab type detects a
	// backing file deleted between sessions. Mirror ScriptResourceTabType: a
	// built-in/subresource path ("<file>::<subpath>") is backed by its base file.
	const String &path = p_tab.get_resource_key();
	if (path.is_empty()) {
		return true;
	}
	const String base_path = path.is_resource_file() ? path : path.get_slice("::", 0);
	if (!FileAccess::exists(base_path)) {
		return false;
	}
	// Also require the file to load as text: an existing-but-unreadable or
	// invalid-UTF-8 file is dropped with a diagnostic on restore rather than
	// mounted as an empty buffer that a later save could overwrite the file with.
	Ref<TextDocument> probe;
	probe.instantiate();
	probe->set_path(path);
	return probe->load() == OK;
}

Ref<TextDocument> TextTabType::get_document_for(int p_stable_id) const {
	if (const Ref<TextDocument> *document = documents.getptr(p_stable_id)) {
		return *document;
	}
	return Ref<TextDocument>();
}

PackedStringArray TextTabType::get_unsaved_document_paths() const {
	PackedStringArray paths;
	for (const KeyValue<int, Ref<TextDocument>> &entry : documents) {
		if (entry.value.is_valid() && entry.value->is_dirty()) {
			paths.push_back(entry.value->get_path());
		}
	}
	return paths;
}

void TextTabType::save_all_documents() {
	for (const KeyValue<int, Ref<TextDocument>> &entry : documents) {
		if (entry.value.is_valid() && entry.value->is_dirty()) {
			entry.value->save();
		}
	}
}
