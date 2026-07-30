/**************************************************************************/
/*  bottom_drawer_layout.h                                                */
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

#include "core/io/config_file.h"
#include "core/templates/hash_map.h"
#include "editor/gui/bottom_drawer_geometry.h"

// Pure layout persistence state for the editor bottom drawer.
// Kept free of editor UI dependencies so it is unit-testable headlessly.
struct BottomDrawerLayoutState {
	HashMap<String, int> offsets;
	HashMap<String, int> widths;
	HashMap<String, bool> pinned;
	bool pinned_by_default = false;

	bool is_pinned(const String &p_layout_key) const {
		HashMap<String, bool>::ConstIterator E = pinned.find(p_layout_key);
		return E ? E->value : pinned_by_default;
	}

	int get_offset(const String &p_layout_key, int p_fallback) const {
		HashMap<String, int>::ConstIterator E = offsets.find(p_layout_key);
		return E ? E->value : p_fallback;
	}

	int get_width(const String &p_layout_key) const {
		HashMap<String, int>::ConstIterator E = widths.find(p_layout_key);
		return E ? E->value : 0;
	}

	void set_offset(const String &p_layout_key, int p_offset) {
		offsets[p_layout_key] = p_offset;
	}

	void set_width(const String &p_layout_key, int p_width) {
		if (p_width <= 0) {
			widths.erase(p_layout_key);
			return;
		}
		widths[p_layout_key] = p_width;
	}

	void erase_width(const String &p_layout_key) {
		widths.erase(p_layout_key);
	}

	void set_pinned(const String &p_layout_key, bool p_pinned) {
		pinned[p_layout_key] = p_pinned;
	}

	void save_to_config(Ref<ConfigFile> p_config_file, const String &p_section) const {
		Dictionary offsets_dict;
		for (const KeyValue<String, int> &E : offsets) {
			offsets_dict[E.key] = E.value;
		}
		p_config_file->set_value(p_section, "bottom_panel_offsets", offsets_dict);

		Dictionary widths_dict;
		for (const KeyValue<String, int> &E : widths) {
			if (E.value > 0) {
				widths_dict[E.key] = E.value;
			}
		}
		p_config_file->set_value(p_section, "bottom_panel_widths", widths_dict);

		Dictionary pinned_dict;
		for (const KeyValue<String, bool> &E : pinned) {
			pinned_dict[E.key] = E.value;
		}
		p_config_file->set_value(p_section, "bottom_panel_pinned", pinned_dict);
		p_config_file->set_value(p_section, "bottom_panel_pinned_by_default", pinned_by_default);
	}

	void load_from_config(Ref<ConfigFile> p_config_file, const String &p_section) {
		offsets.clear();
		widths.clear();
		pinned.clear();

		const Dictionary offsets_dict = p_config_file->get_value(p_section, "bottom_panel_offsets", Dictionary());
		const LocalVector<Variant> offset_list = offsets_dict.get_key_list();
		for (const Variant &v : offset_list) {
			offsets[v] = BottomDrawerGeometry::body_height_from_stored(offsets_dict[v], 0);
		}

		const Dictionary widths_dict = p_config_file->get_value(p_section, "bottom_panel_widths", Dictionary());
		const LocalVector<Variant> width_list = widths_dict.get_key_list();
		for (const Variant &v : width_list) {
			const int width = widths_dict[v];
			if (width > 0) {
				widths[v] = width;
			}
		}

		// Layouts written before the drawer existed lack the key and keep the
		// familiar in-flow behavior for every panel.
		pinned_by_default = p_config_file->get_value(p_section, "bottom_panel_pinned_by_default", true);
		const Dictionary pinned_dict = p_config_file->get_value(p_section, "bottom_panel_pinned", Dictionary());
		const LocalVector<Variant> pinned_list = pinned_dict.get_key_list();
		for (const Variant &v : pinned_list) {
			pinned[v] = pinned_dict[v];
		}
	}
};
