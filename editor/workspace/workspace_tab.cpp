/**************************************************************************/
/*  workspace_tab.cpp                                                     */
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

#include "workspace_tab.h"

#include "core/io/config_file.h"

#include "workspace_tab_type.h"

void WorkspaceTab::save_to_config(const Ref<ConfigFile> &p_config, const String &p_section, const WorkspaceTabType *p_type) const {
	ERR_FAIL_COND(p_config.is_null());
	ERR_FAIL_NULL(p_type);

	p_config->set_value(p_section, "stable_id", stable_id);
	p_config->set_value(p_section, "type_id", type_id);
	p_config->set_value(p_section, "resource_key", resource_key);
	p_config->set_value(p_section, "title_cache", title_cache);
	p_config->set_value(p_section, "icon_key_cache", icon_key_cache);

	const Dictionary payload_data = p_type->save_payload(*this);
	const String payload_section = p_section.path_join("payload");
	if (p_config->has_section(payload_section)) {
		p_config->erase_section(payload_section);
	}
	for (const Variant &key : payload_data.keys()) {
		p_config->set_value(payload_section, key, payload_data[key]);
	}
}

void WorkspaceTab::load_from_config(const Ref<ConfigFile> &p_config, const String &p_section, const WorkspaceTabType *p_type) {
	ERR_FAIL_COND(p_config.is_null());
	ERR_FAIL_NULL(p_type);

	stable_id = p_config->get_value(p_section, "stable_id", -1);
	type_id = p_config->get_value(p_section, "type_id", StringName());
	resource_key = p_config->get_value(p_section, "resource_key", String());
	title_cache = p_config->get_value(p_section, "title_cache", String());
	icon_key_cache = p_config->get_value(p_section, "icon_key_cache", String());

	const String payload_section = p_section.path_join("payload");
	Dictionary payload_data;
	if (p_config->has_section(payload_section)) {
		const Vector<String> keys = p_config->get_section_keys(payload_section);
		for (int i = 0; i < keys.size(); i++) {
			const String &key = keys[i];
			payload_data[key] = p_config->get_value(payload_section, key);
		}
	}
	p_type->restore_payload(*this, payload_data);
}

bool WorkspaceTab::operator==(const WorkspaceTab &p_other) const {
	return stable_id == p_other.stable_id &&
			type_id == p_other.type_id &&
			resource_key == p_other.resource_key &&
			title_cache == p_other.title_cache &&
			icon_key_cache == p_other.icon_key_cache &&
			payload == p_other.payload;
}
