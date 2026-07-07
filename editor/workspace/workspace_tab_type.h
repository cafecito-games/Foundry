/**************************************************************************/
/*  workspace_tab_type.h                                                  */
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
#include "core/variant/callable.h"
#include "core/variant/dictionary.h"

#include "editor/workspace/workspace_tab.h"

class Control;
class Texture2D;

enum class WorkspaceTabCloseResult {
	CLOSE,
	CANCEL,
	DEFERRED,
};

/**
 * Behavior surface for a registered workspace tab type. Workspace mechanics
 * delegate open/close/mount/persistence to the tab type without scene/script
 * branching outside registry registration.
 */
class WorkspaceTabType {
public:
	virtual ~WorkspaceTabType() = default;

	virtual StringName type_id() const = 0;
	virtual bool can_open(const String &p_resource) const = 0;
	virtual WorkspaceTab make_tab(const String &p_resource, int p_stable_id) const = 0;
	virtual String get_title(const WorkspaceTab &p_tab) const = 0;
	virtual Ref<Texture2D> get_icon(const WorkspaceTab &p_tab) const = 0;
	virtual void mount(WorkspaceTab &p_tab, Control *p_chrome_host) = 0;
	virtual void unmount(WorkspaceTab &p_tab) = 0;
	virtual void activate(WorkspaceTab &p_tab) = 0;

	// Request that the tab close. Returns CLOSE for an immediate close, CANCEL to
	// abort, or DEFERRED when the type is driving its own confirmation flow. For
	// a DEFERRED result the type invokes p_on_deferred_close once (and only once)
	// if the flow ultimately resolves to a close; it is not invoked if the flow
	// is cancelled. The workspace uses this to drop the tab after an async prompt.
	virtual WorkspaceTabCloseResult request_close(WorkspaceTab &p_tab, const Callable &p_on_deferred_close = Callable()) = 0;
	virtual Dictionary save_payload(const WorkspaceTab &p_tab) const = 0;
	virtual void restore_payload(WorkspaceTab &p_tab, const Dictionary &p_payload) const = 0;

	// Whether the tab's backing resource is still available when restoring a
	// persisted session. A tab whose resource was deleted since the layout was
	// saved is dropped with a diagnostic instead of restored. Types with no
	// file-backed resource of their own (e.g. scene tabs, whose existence is
	// owned by EditorData) return true.
	virtual bool is_resource_available(const WorkspaceTab &p_tab) const { return true; }
};
