/**************************************************************************/
/*  fs_name_mangler_export_test_utils.h                                   */
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

#ifdef TOOLS_ENABLED

#include "core/io/file_access.h"
#include "core/io/file_access_pack.h"
#include "core/os/os.h"

#include <cstring>

namespace FSTests {

struct NameManglerPackProcessResult {
	Error error = FAILED;
	int exit_code = -1;
	String output;
};

static NameManglerPackProcessResult name_mangler_export_run_process(
		const List<String> &p_arguments,
		const String &p_working_directory = String()) {
	NameManglerPackProcessResult result;
	Vector<uint8_t> stdout_bytes;
	Vector<uint8_t> stderr_bytes;
	Dictionary pipe_info = OS::get_singleton()->execute_with_pipe(
			OS::get_singleton()->get_executable_path(), p_arguments,
			false, p_working_directory, Dictionary(), false);
	if (pipe_info.is_empty()) {
		return result;
	}

	Ref<FileAccess> stdout_pipe = pipe_info["stdio"];
	Ref<FileAccess> stderr_pipe = pipe_info["stderr"];
	const OS::ProcessID pid = pipe_info["pid"];
	auto pump = [](const Ref<FileAccess> &p_pipe,
						Vector<uint8_t> &r_bytes) {
		if (p_pipe.is_null() || !p_pipe->is_open()) {
			return;
		}
		const uint64_t available = p_pipe->get_length();
		if (available == 0) {
			return;
		}
		Vector<uint8_t> chunk;
		chunk.resize(available);
		const uint64_t read =
				p_pipe->get_buffer(chunk.ptrw(), available);
		const int old_size = r_bytes.size();
		r_bytes.resize(old_size + read);
		if (read > 0) {
			memcpy(r_bytes.ptrw() + old_size, chunk.ptr(), read);
		}
	};

	const uint64_t deadline =
			OS::get_singleton()->get_ticks_msec() + 180000;
	while (OS::get_singleton()->get_ticks_msec() < deadline) {
		pump(stdout_pipe, stdout_bytes);
		pump(stderr_pipe, stderr_bytes);
		if (!OS::get_singleton()->is_process_running(pid)) {
			pump(stdout_pipe, stdout_bytes);
			pump(stderr_pipe, stderr_bytes);
			result.error = OK;
			break;
		}
		OS::get_singleton()->delay_usec(20000);
	}
	if (result.error != OK &&
			OS::get_singleton()->is_process_running(pid)) {
		OS::get_singleton()->kill(pid);
	}
	if (stdout_pipe.is_valid()) {
		stdout_pipe->close();
	}
	if (stderr_pipe.is_valid()) {
		stderr_pipe->close();
	}
	result.exit_code =
			OS::get_singleton()->get_process_exit_code(pid);
	result.output = String::utf8(
			(const char *)stdout_bytes.ptr(), stdout_bytes.size());
	result.output += String::utf8(
			(const char *)stderr_bytes.ptr(), stderr_bytes.size());
	return result;
}

struct NameManglerPackMount {
	bool owns_packed_data = false;

	Error mount(const String &p_path) {
		PackedData *packed_data = PackedData::get_singleton();
		ERR_FAIL_NULL_V(packed_data, ERR_UNAVAILABLE);
		ERR_FAIL_COND_V(!packed_data->get_file_paths().is_empty(),
				ERR_ALREADY_IN_USE);
		owns_packed_data = true;
		return packed_data->add_pack(p_path, false, 0);
	}

	Vector<uint8_t> read(const String &p_path) const {
		PackedData *packed_data = PackedData::get_singleton();
		ERR_FAIL_NULL_V(packed_data, Vector<uint8_t>());
		Ref<FileAccess> file = packed_data->try_open_path(p_path);
		ERR_FAIL_COND_V(file.is_null(), Vector<uint8_t>());
		Vector<uint8_t> bytes;
		bytes.resize(file->get_length());
		if (!bytes.is_empty()) {
			file->get_buffer(bytes.ptrw(), bytes.size());
		}
		return bytes;
	}

	~NameManglerPackMount() {
		if (owns_packed_data &&
				PackedData::get_singleton() != nullptr) {
			PackedData::get_singleton()->clear();
		}
	}
};

} // namespace FSTests

#endif // TOOLS_ENABLED
