/**************************************************************************/
/*  editor_automation_types.h                                             */
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

#include "core/math/rect2i.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"

struct EditorAutomationElement {
	String id;
	String handle;
	String role;
	String name;
	String text;
	String class_name;
	String path;
	bool visible = false;
	bool enabled = true;
	bool focused = false;
	bool pressed = false;
	bool selected = false;
	Rect2i bounds;
	PackedStringArray actions;
	Dictionary metadata;
	Vector<int> children;
	uint64_t object_id = 0;
	int parent_index = -1;
};

struct EditorAutomationSnapshotData {
	uint64_t generation = 0;
	Vector<EditorAutomationElement> elements;
	HashMap<String, int> id_to_index;
	HashMap<String, int> handle_to_index;
	HashMap<uint64_t, int> object_id_to_index;
	Vector<int> root_indices;
	String focused_element_id;
};

enum class EditorAutomationSelectorStatus {
	OK,
	NO_MATCH,
	AMBIGUOUS,
	STALE_ID,
	INVALID_SELECTOR,
};

struct EditorAutomationSelectorResult {
	EditorAutomationSelectorStatus status = EditorAutomationSelectorStatus::INVALID_SELECTOR;
	Vector<int> match_indices;
	String error_kind;
	String message;
	Array candidates;
	bool reconciled = false;
	String requested_reference;
	String current_element_id;
	uint64_t snapshot_generation = 0;

	Dictionary to_dictionary() const;
};
