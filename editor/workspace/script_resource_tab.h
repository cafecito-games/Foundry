/**************************************************************************/
/*  script_resource_tab.h                                                 */
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

#include "editor/workspace/workspace_tab_type.h"

class ScriptLeaf;

/**
 * Concrete workspace tab type for an open script resource. The tab owns the
 * visible workspace identity and location of one script; the global
 * ScriptEditorController keeps ownership of cross-view services (find-in-files,
 * debugger, autosave, recent scripts, completion caches).
 *
 * Each mounted tab hosts its own ScriptLeaf (and its per-leaf ScriptEditorView)
 * inside the pane's chrome host. Switching away from a script tab captures the
 * editor's caret/scroll/fold state into the tab payload and lets the pane free
 * the surface; switching back recreates it and restores that state, so a tab
 * moved between panes keeps its editing state without any dirty prompt.
 */
class ScriptResourceTabType : public WorkspaceTabType {
	StringName type_id_value;

	// stable_id -> ScriptLeaf instance currently mounted for that tab. Entries
	// are validated through ObjectDB so a surface freed by the pane is detected
	// and transparently recreated on the next mount.
	HashMap<int, ObjectID> mounted_surfaces;

	ScriptLeaf *_resolve_surface(int p_stable_id) const;
	ScriptLeaf *_create_surface(const WorkspaceTab &p_tab, Control *p_chrome_host);
	void _apply_payload(ScriptLeaf *p_leaf, const Dictionary &p_payload) const;
	Dictionary _capture_payload(ScriptLeaf *p_leaf) const;
	void _focus_surface(ScriptLeaf *p_leaf) const;

public:
	explicit ScriptResourceTabType(const StringName &p_type_id = StringName("script"));

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
};
