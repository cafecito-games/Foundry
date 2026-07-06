/**************************************************************************/
/*  workspace_tab_registry.h                                              */
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

#include "editor/workspace/workspace_tab.h"

class WorkspaceTabType;

struct WorkspaceTabLocation {
	int pane_id = -1;
	int tab_index = -1;

	bool is_valid() const { return pane_id >= 0 && tab_index >= 0; }
	bool operator==(const WorkspaceTabLocation &p_other) const {
		return pane_id == p_other.pane_id && tab_index == p_other.tab_index;
	}
};

enum class WorkspaceTabInsertResult {
	INSERTED,
	REVEALED_EXISTING,
};

/**
 * Registry of workspace tab types and the canonical (type_id, resource_key)
 * index used for open/reveal deduplication.
 */
class WorkspaceTabRegistry {
	HashMap<StringName, WorkspaceTabType *> types;
	HashMap<StringName, HashMap<String, WorkspaceTab>> canonical_tabs;
	HashMap<StringName, HashMap<String, WorkspaceTabLocation>> canonical_locations;
	int next_stable_id = 0;

public:
	WorkspaceTabRegistry();

	void register_type(WorkspaceTabType *p_type);
	WorkspaceTabType *find_type(const StringName &p_type_id);

	int allocate_stable_id();
	void reset_stable_id_counter(int p_next_stable_id = 0) { next_stable_id = p_next_stable_id; }

	WorkspaceTabInsertResult insert_canonical(const WorkspaceTab &p_tab, const WorkspaceTabLocation &p_location, WorkspaceTab *r_existing_tab = nullptr, WorkspaceTabLocation *r_existing_location = nullptr);
	bool find_canonical(const StringName &p_type_id, const String &p_resource_key, WorkspaceTab &r_tab, WorkspaceTabLocation &r_location) const;
	bool remove_canonical(const StringName &p_type_id, const String &p_resource_key);
	void clear_canonical_index();

	void register_builtin_tab_types();
};
