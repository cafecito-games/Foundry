/**************************************************************************/
/*  editor_automation_log.h                                               */
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

#include "core/string/ustring.h"
#include "core/variant/variant.h"

struct EditorAutomationLogMarker {
	int message_index = 0;
};

class EditorAutomationLog {
public:
	static String message_type_to_string(int p_type);
	static bool message_type_matches_severity(int p_type, const String &p_severity);

	static EditorAutomationLogMarker create_marker();
	static int get_message_count();
	static Dictionary read_message_at(int p_index);

	static Array read_since(const EditorAutomationLogMarker &p_marker, const PackedStringArray &p_severities = PackedStringArray());
	static bool contains_text(const String &p_text, const String &p_severity = String());
	static bool has_new_messages_since(const EditorAutomationLogMarker &p_marker, const PackedStringArray &p_severities = PackedStringArray());
	static Array read_recent(int p_count = 32, const PackedStringArray &p_severities = PackedStringArray());

	// Unit-test scaffolding when EditorLog is unavailable.
	static void push_test_message(const String &p_text, const String &p_severity);
	static void clear_test_messages();
	static bool is_using_test_log();
};
