/**************************************************************************/
/*  project_build_state.h                                                 */
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

#include "core/error/error_list.h"
#include "core/templates/hash_map.h"
#include "core/variant/variant.h"

class ProjectBuildState {
public:
	enum DirtyReason {
		DIRTY_NONE,
		DIRTY_MISSING_STATE,
		DIRTY_UNREADABLE_STATE,
		DIRTY_PREVIOUS_FAILURE,
		DIRTY_FINGERPRINT_CHANGED,
		DIRTY_OUTPUT_MISSING,
		DIRTY_OUTPUT_CHANGED,
		DIRTY_PROVIDER_FORCED,
	};

	struct TaskRecord {
		String task_name;
		String fingerprint;
		PackedStringArray output_manifest;
		bool success = false;
	};

	struct DirtyStatus {
		bool dirty = true;
		DirtyReason reason = DIRTY_MISSING_STATE;
		String message;
	};

private:
	String state_path;
	HashMap<String, TaskRecord> records;
	Error load_error = OK;

	static String _default_state_path();
	static String _task_section(const String &p_task_name);
	static bool _is_task_section(const String &p_section);
	static String _task_name_from_section(const String &p_section);
	static PackedStringArray _compute_output_manifest(const PackedStringArray &p_outputs, bool *r_missing_output = nullptr);

public:
	ProjectBuildState();
	explicit ProjectBuildState(const String &p_state_path);

	String get_state_path() const { return state_path; }
	Error get_load_error() const { return load_error; }

	void clear();
	Error load();
	Error save() const;

	void record_task_result(const String &p_task_name, const String &p_fingerprint,
			const PackedStringArray &p_outputs, bool p_success);
	bool has_task_record(const String &p_task_name) const;
	const TaskRecord *get_task_record(const String &p_task_name) const;

	DirtyStatus get_task_dirty_status(const String &p_task_name, const String &p_current_fingerprint,
			const PackedStringArray &p_outputs, bool p_provider_forced = false) const;
};
