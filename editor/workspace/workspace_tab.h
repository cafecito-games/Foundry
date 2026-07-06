/**************************************************************************/
/*  workspace_tab.h                                                       */
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
#include "core/variant/dictionary.h"

class ConfigFile;
class WorkspaceTabType;

/**
 * Serializable workspace tab identity record. Pane mounting and editor chrome
 * are owned by WorkspaceTabType; the registry indexes tabs canonically by
 * (type_id, resource_key).
 */
class WorkspaceTab {
	int stable_id = -1;
	StringName type_id;
	String resource_key;
	String title_cache;
	String icon_key_cache;
	Dictionary payload;

public:
	int get_stable_id() const { return stable_id; }
	const StringName &get_type_id() const { return type_id; }
	const String &get_resource_key() const { return resource_key; }
	const String &get_title_cache() const { return title_cache; }
	const String &get_icon_key_cache() const { return icon_key_cache; }
	const Dictionary &get_payload() const { return payload; }

	void set_stable_id(int p_stable_id) { stable_id = p_stable_id; }
	void set_type_id(const StringName &p_type_id) { type_id = p_type_id; }
	void set_resource_key(const String &p_resource_key) { resource_key = p_resource_key; }
	void set_title_cache(const String &p_title_cache) { title_cache = p_title_cache; }
	void set_icon_key_cache(const String &p_icon_key_cache) { icon_key_cache = p_icon_key_cache; }
	void set_payload(const Dictionary &p_payload) { payload = p_payload; }

	bool is_valid() const { return stable_id >= 0 && !type_id.is_empty() && !resource_key.is_empty(); }

	void save_to_config(const Ref<ConfigFile> &p_config, const String &p_section, const WorkspaceTabType *p_type) const;
	void load_from_config(const Ref<ConfigFile> &p_config, const String &p_section, const WorkspaceTabType *p_type);

	bool operator==(const WorkspaceTab &p_other) const;
};
