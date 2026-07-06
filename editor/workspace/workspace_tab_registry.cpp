/**************************************************************************/
/*  workspace_tab_registry.cpp                                            */
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

#include "workspace_tab_registry.h"

#include "editor/workspace/script_resource_tab.h"
#include "workspace_tab_stub_types.h"

WorkspaceTabRegistry::WorkspaceTabRegistry() {
	register_builtin_tab_types();
}

void WorkspaceTabRegistry::register_type(WorkspaceTabType *p_type) {
	ERR_FAIL_NULL(p_type);
	ERR_FAIL_COND(p_type->type_id().is_empty());
	types[p_type->type_id()] = p_type;
}

WorkspaceTabType *WorkspaceTabRegistry::find_type(const StringName &p_type_id) {
	WorkspaceTabType *const *found = types.getptr(p_type_id);
	if (!found) {
		return nullptr;
	}
	return *found;
}

int WorkspaceTabRegistry::allocate_stable_id() {
	return next_stable_id++;
}

WorkspaceTabInsertResult WorkspaceTabRegistry::insert_canonical(const WorkspaceTab &p_tab, const WorkspaceTabLocation &p_location, WorkspaceTab *r_existing_tab, WorkspaceTabLocation *r_existing_location) {
	ERR_FAIL_COND_V(!p_tab.is_valid(), WorkspaceTabInsertResult::INSERTED);

	HashMap<String, WorkspaceTab> &tabs_for_type = canonical_tabs[p_tab.get_type_id()];
	HashMap<String, WorkspaceTabLocation> &locations_for_type = canonical_locations[p_tab.get_type_id()];

	if (WorkspaceTab *existing = tabs_for_type.getptr(p_tab.get_resource_key())) {
		if (r_existing_tab) {
			*r_existing_tab = *existing;
		}
		if (r_existing_location) {
			const WorkspaceTabLocation *existing_location = locations_for_type.getptr(p_tab.get_resource_key());
			if (existing_location) {
				*r_existing_location = *existing_location;
			}
		}
		return WorkspaceTabInsertResult::REVEALED_EXISTING;
	}

	tabs_for_type[p_tab.get_resource_key()] = p_tab;
	locations_for_type[p_tab.get_resource_key()] = p_location;
	return WorkspaceTabInsertResult::INSERTED;
}

bool WorkspaceTabRegistry::find_canonical(const StringName &p_type_id, const String &p_resource_key, WorkspaceTab &r_tab, WorkspaceTabLocation &r_location) const {
	const HashMap<String, WorkspaceTab> *tabs_for_type = canonical_tabs.getptr(p_type_id);
	if (!tabs_for_type) {
		return false;
	}
	const WorkspaceTab *tab = tabs_for_type->getptr(p_resource_key);
	if (!tab) {
		return false;
	}

	r_tab = *tab;
	const HashMap<String, WorkspaceTabLocation> *locations_for_type = canonical_locations.getptr(p_type_id);
	if (locations_for_type) {
		const WorkspaceTabLocation *location = locations_for_type->getptr(p_resource_key);
		if (location) {
			r_location = *location;
		}
	}
	return true;
}

bool WorkspaceTabRegistry::remove_canonical(const StringName &p_type_id, const String &p_resource_key) {
	HashMap<String, WorkspaceTab> *tabs_for_type = canonical_tabs.getptr(p_type_id);
	if (!tabs_for_type || !tabs_for_type->has(p_resource_key)) {
		return false;
	}

	tabs_for_type->erase(p_resource_key);
	if (HashMap<String, WorkspaceTabLocation> *locations_for_type = canonical_locations.getptr(p_type_id)) {
		locations_for_type->erase(p_resource_key);
	}
	return true;
}

void WorkspaceTabRegistry::clear_canonical_index() {
	canonical_tabs.clear();
	canonical_locations.clear();
}

void WorkspaceTabRegistry::register_builtin_tab_types() {
	static SceneTabStub scene_tab(StringName("scene"));
	static ScriptResourceTabType script_tab(StringName("script"));
	register_type(&scene_tab);
	register_type(&script_tab);
}
