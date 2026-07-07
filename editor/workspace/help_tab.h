/**************************************************************************/
/*  help_tab.h                                                            */
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

class EditorHelp;

/**
 * Concrete workspace tab type for a class-reference help page. The tab owns one
 * EditorHelp surface, so several classes can be open at once in different panes
 * and there is at most one tab per class (the registry canonical index keys on
 * (type_id, resource_key) and the resource key is the bare class name).
 *
 * Structured after ScriptResourceTabType: each mounted tab hosts its own
 * EditorHelp inside the pane's chrome host. Help has no unsaved state, so
 * request_close always closes without a prompt; the payload persists only the
 * displayed class and scroll position.
 */
class HelpTabType : public WorkspaceTabType {
	StringName type_id_value;

	// stable_id -> EditorHelp instance currently mounted for that tab. Entries are
	// validated through ObjectDB so a surface freed by the pane is detected and
	// transparently recreated on the next mount.
	HashMap<int, ObjectID> mounted_surfaces;

	EditorHelp *_resolve_surface(int p_stable_id) const;
	EditorHelp *_create_surface(Control *p_chrome_host);
	void _apply_payload(EditorHelp *p_help, const WorkspaceTab &p_tab) const;
	Dictionary _capture_payload(EditorHelp *p_help, const WorkspaceTab &p_tab) const;

public:
	explicit HelpTabType(const StringName &p_type_id = StringName("help"));

	// The bare class key for a topic. Accepts a plain class name ("Node2D") or a
	// deep topic in "class*:Class[:member]" form ("class_method:Node2D:queue_free")
	// and returns the class ("Node2D"). Empty for an empty topic.
	static String class_key_for_topic(const String &p_topic);
	static String derive_title(const String &p_class_key);

	// The live EditorHelp backing a mounted tab, or null when the tab is not
	// currently mounted. Used to scroll a revealed page to a deep anchor.
	EditorHelp *get_mounted_help(int p_stable_id) const { return _resolve_surface(p_stable_id); }

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
