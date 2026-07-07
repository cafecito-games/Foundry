/**************************************************************************/
/*  text_tab.h                                                            */
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
#include "core/templates/hash_map.h"

#include "editor/workspace/text_document.h"
#include "editor/workspace/workspace_tab_type.h"

class TextTabSurface;

/**
 * Workspace tab type for a non-script text document (.txt, .md, .json, .cfg,
 * README, ...). One tab per file, dockable and splittable next to scenes,
 * scripts, and help. Structured after ScriptResourceTabType: the tab owns its
 * own surface under the pane's chrome host and drives a deferred save prompt on
 * close.
 *
 * Unlike the script tab, the text document (source of truth for content and
 * dirty state) is owned here, keyed by stable id, and outlives the mounted
 * surface: moving the tab between panes recreates the surface but reuses the
 * document, so unsaved edits are preserved without a dirty prompt. A clean
 * document is dropped on unmount and reloaded from disk on the next mount.
 *
 * Rendering is a view mode (TextView), never identity: the active mode lives in
 * the payload, so a future editable Markdown preview is just another registered
 * TextView with no change here.
 */
class TextTabType : public WorkspaceTabType {
	StringName type_id_value;

	// stable_id -> TextTabSurface instance currently mounted (validated through
	// ObjectDB so a surface freed by the pane is detected and recreated).
	HashMap<int, ObjectID> mounted_surfaces;
	// stable_id -> owning document. Retained across unmount only while dirty so
	// unsaved edits survive a pane move; clean documents are reloaded from disk.
	HashMap<int, Ref<TextDocument>> documents;

	TextTabSurface *_resolve_surface(int p_stable_id) const;
	Ref<TextDocument> _resolve_document(const WorkspaceTab &p_tab);
	TextTabSurface *_create_surface(const WorkspaceTab &p_tab, Control *p_chrome_host);
	void _capture_payload(WorkspaceTab &p_tab, TextTabSurface *p_surface) const;

public:
	explicit TextTabType(const StringName &p_type_id = StringName("text"));

	static String derive_title(const String &p_resource_key);

	StringName type_id() const override;
	bool can_open(const String &p_resource) const override;
	WorkspaceTab make_tab(const String &p_resource, int p_stable_id) const override;
	String get_title(const WorkspaceTab &p_tab) const override;
	Ref<Texture2D> get_icon(const WorkspaceTab &p_tab) const override;
	void mount(WorkspaceTab &p_tab, Control *p_chrome_host) override;
	void unmount(WorkspaceTab &p_tab) override;
	void activate(WorkspaceTab &p_tab) override;
	WorkspaceTabCloseResult request_close(WorkspaceTab &p_tab, const Callable &p_on_deferred_close = Callable()) override;
	Dictionary save_payload(const WorkspaceTab &p_tab) const override;
	void restore_payload(WorkspaceTab &p_tab, const Dictionary &p_payload) const override;
	bool is_resource_available(const WorkspaceTab &p_tab) const override;

	// Test/introspection hook: the live document backing a mounted tab (or a
	// retained dirty document), or an invalid Ref if none.
	Ref<TextDocument> get_document_for(int p_stable_id) const;
};
