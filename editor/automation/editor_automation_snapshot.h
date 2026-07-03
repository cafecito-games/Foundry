/**************************************************************************/
/*  editor_automation_snapshot.h                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "editor/automation/editor_automation_types.h"

class Control;
class Node;

class EditorAutomationSnapshot {
	EditorAutomationSnapshotData data;

public:
	static EditorAutomationSnapshot capture_from_editor();
	static EditorAutomationSnapshot capture_from_node(Node *p_root);
	static EditorAutomationSnapshot capture_from_roots(const LocalVector<Node *> &p_roots);

	uint64_t get_generation() const { return data.generation; }
	int get_element_count() const { return data.elements.size(); }
	const EditorAutomationElement &get_element(int p_index) const { return data.elements[p_index]; }
	const EditorAutomationSnapshotData &get_data() const { return data; }
	const String &get_focused_element_id() const { return data.focused_element_id; }

	const EditorAutomationElement *find_by_id(const String &p_id) const;
	const EditorAutomationElement *find_by_object_id(uint64_t p_object_id) const;

	Dictionary to_dictionary() const;
	Array get_root_elements() const;

	static String make_control_element_id(uint64_t p_generation, uint64_t p_object_id);
	static String make_virtual_element_id(uint64_t p_generation, const String &p_kind, const String &p_key);
	static bool parse_element_id(const String &p_id, uint64_t &r_generation, String &r_kind, String &r_key);
};
