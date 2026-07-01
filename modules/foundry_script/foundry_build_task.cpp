/**************************************************************************/
/*  foundry_build_task.cpp                                                */
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

#include "foundry_build_task.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/string/string_builder.h"

#if defined(UNIX_ENABLED) && !defined(WEB_ENABLED)
#include <signal.h>
#include <unistd.h>
#endif

namespace {

static constexpr uint64_t COMMAND_PIPE_READ_CHUNK_SIZE = 65536;
static constexpr uint64_t COMMAND_PIPE_DRAIN_BUDGET_BYTES = COMMAND_PIPE_READ_CHUNK_SIZE * 4;
static constexpr uint64_t COMMAND_PIPE_POST_EXIT_DRAIN_USEC = 1000000;

struct CommandRunData {
	String stdout_text;
	String stderr_text;
	int exit_code = 0;
	bool timed_out = false;
	String launch_error;
};

struct CommandInvocation {
	String original_executable;
	String executable;
	PackedStringArray arguments;
	String working_directory;
	Dictionary environment;
	int timeout_seconds = 60;
};

static String _globalize_project_path(const String &p_path) {
	const String path = p_path.replace_char('\\', '/');
	if (path.begins_with("res://")) {
		return ProjectSettings::get_singleton()->globalize_path(path).simplify_path();
	}
	return p_path;
}

static PackedStringArray _globalize_project_paths(const PackedStringArray &p_paths) {
	PackedStringArray globalized;
	for (int i = 0; i < p_paths.size(); i++) {
		globalized.push_back(_globalize_project_path(p_paths[i]));
	}
	return globalized;
}

static bool _has_glob_wildcard(const String &p_path) {
	return p_path.contains("*") || p_path.contains("?");
}

static String _resolve_declared_input_path(const String &p_path, const String &p_working_directory) {
	const String globalized = _globalize_project_path(p_path);
	if (globalized != p_path || !p_path.is_relative_path() || p_working_directory.is_empty()) {
		return globalized;
	}
	return p_working_directory.path_join(p_path).simplify_path();
}

static String _glob_search_root(const String &p_pattern) {
	const int star = p_pattern.find_char('*');
	const int question = p_pattern.find_char('?');
	int wildcard = -1;
	if (star >= 0 && question >= 0) {
		wildcard = MIN(star, question);
	} else {
		wildcard = MAX(star, question);
	}

	if (wildcard < 0) {
		return p_pattern.get_base_dir();
	}

	String prefix = p_pattern.substr(0, wildcard);
	if (prefix.ends_with("/") && prefix.length() > 1) {
		if (prefix.ends_with("://")) {
			return prefix;
		}
		const String stripped_prefix = prefix.substr(0, prefix.length() - 1);
		if (!stripped_prefix.ends_with(":")) {
			return stripped_prefix;
		}
	}
	String root = prefix.get_base_dir();
	if (root == "res:") {
		root = "res://";
	}
	return root.is_empty() ? "." : root;
}

static bool _glob_match_path(const String &p_path, const String &p_pattern, int p_path_index, int p_pattern_index) {
	const int path_length = p_path.length();
	const int pattern_length = p_pattern.length();
	if (p_pattern_index == pattern_length) {
		return p_path_index == path_length;
	}

	const char32_t pattern_char = p_pattern[p_pattern_index];
	if (pattern_char == '*') {
		const bool globstar = p_pattern_index + 1 < pattern_length && p_pattern[p_pattern_index + 1] == '*';
		if (globstar) {
			const int next_pattern_index = p_pattern_index + 2;
			if (next_pattern_index < pattern_length && p_pattern[next_pattern_index] == '/') {
				if (_glob_match_path(p_path, p_pattern, p_path_index, next_pattern_index + 1)) {
					return true;
				}
				for (int i = p_path_index; i < path_length; i++) {
					if (p_path[i] == '/' && _glob_match_path(p_path, p_pattern, i + 1, next_pattern_index + 1)) {
						return true;
					}
				}
				return false;
			}
			for (int i = p_path_index; i <= path_length; i++) {
				if (_glob_match_path(p_path, p_pattern, i, next_pattern_index)) {
					return true;
				}
			}
			return false;
		}

		for (int i = p_path_index; i <= path_length; i++) {
			if (_glob_match_path(p_path, p_pattern, i, p_pattern_index + 1)) {
				return true;
			}
			if (i == path_length || p_path[i] == '/') {
				break;
			}
		}
		return false;
	}

	if (p_path_index == path_length) {
		return false;
	}
	if (pattern_char == '?') {
		return p_path[p_path_index] != '/' && _glob_match_path(p_path, p_pattern, p_path_index + 1, p_pattern_index + 1);
	}
	return pattern_char == p_path[p_path_index] && _glob_match_path(p_path, p_pattern, p_path_index + 1, p_pattern_index + 1);
}

static bool _path_matches_input_pattern(const String &p_path, const String &p_pattern) {
	return _glob_match_path(p_path.replace_char('\\', '/'), p_pattern.replace_char('\\', '/'), 0, 0);
}

static void _collect_matching_input_files(const String &p_root, const String &p_pattern, PackedStringArray &r_files) {
	Ref<DirAccess> dir = DirAccess::open(p_root);
	if (dir.is_null()) {
		return;
	}
	dir->set_include_hidden(true);
	if (dir->list_dir_begin() != OK) {
		return;
	}

	for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
		if (entry == "." || entry == "..") {
			continue;
		}

		const String child = p_root.path_join(entry).simplify_path();
		if (dir->current_is_dir() && !dir->is_link(child)) {
			_collect_matching_input_files(child, p_pattern, r_files);
		} else if (_path_matches_input_pattern(child, p_pattern)) {
			r_files.push_back(child);
		}
	}
	dir->list_dir_end();
}

static PackedStringArray _input_files_for_signature(const String &p_input, const String &p_working_directory) {
	const String resolved_input = _resolve_declared_input_path(p_input, p_working_directory);
	PackedStringArray files;
	if (!_has_glob_wildcard(resolved_input)) {
		if (FileAccess::exists(resolved_input)) {
			files.push_back(resolved_input);
		}
		return files;
	}

	const String root = _glob_search_root(resolved_input);
	_collect_matching_input_files(root, resolved_input, files);
	files.sort();
	return files;
}

static String _environment_value(const Dictionary &p_environment, const String &p_name) {
	if (p_environment.has(p_name)) {
		return String(p_environment[p_name]);
	}
#ifdef WINDOWS_ENABLED
	Array environment_keys = p_environment.keys();
	for (int i = 0; i < environment_keys.size(); i++) {
		const String key = environment_keys[i];
		if (key.to_lower() == p_name.to_lower()) {
			return String(p_environment[key]);
		}
	}
#endif
	return OS::get_singleton()->has_environment(p_name) ? OS::get_singleton()->get_environment(p_name) : String();
}

static String _environment_path(const Dictionary &p_environment) {
	return _environment_value(p_environment, "PATH");
}

static bool _command_has_path_separator(const String &p_command) {
	return p_command.contains("/") || p_command.contains("\\");
}

#ifdef WINDOWS_ENABLED
static bool _is_windows_batch_extension(const String &p_extension) {
	const String extension = p_extension.trim_prefix(".").to_lower();
	return extension == "bat" || extension == "cmd";
}

static void _push_unique_path_extension(PackedStringArray &r_extensions, const String &p_extension) {
	String extension = p_extension.strip_edges();
	if (extension.is_empty()) {
		return;
	}
	if (!extension.begins_with(".")) {
		extension = "." + extension;
	}

	const String normalized_extension = extension.to_lower();
	if (_is_windows_batch_extension(normalized_extension)) {
		return;
	}
	for (int i = 0; i < r_extensions.size(); i++) {
		if (r_extensions[i].to_lower() == normalized_extension) {
			return;
		}
	}
	r_extensions.push_back(extension);
}

static PackedStringArray _windows_path_extensions(const Dictionary &p_environment) {
	String pathext = _environment_value(p_environment, "PATHEXT");
	if (pathext.is_empty()) {
		pathext = ".COM;.EXE";
	}

	PackedStringArray extensions;
	const PackedStringArray pathext_entries = pathext.split(";", false);
	for (int i = 0; i < pathext_entries.size(); i++) {
		_push_unique_path_extension(extensions, pathext_entries[i]);
	}
	return extensions;
}

static String _resolve_windows_extended_candidate(const String &p_candidate, const String &p_command, const Dictionary &p_environment) {
	if (FileAccess::exists(p_candidate)) {
		if (_is_windows_batch_extension(p_candidate.get_extension())) {
			return String();
		}
		return p_candidate;
	}

	if (!p_command.get_extension().is_empty()) {
		return String();
	}

	const PackedStringArray extensions = _windows_path_extensions(p_environment);
	for (int i = 0; i < extensions.size(); i++) {
		const String extended_candidate = p_candidate + extensions[i];
		if (FileAccess::exists(extended_candidate)) {
			return extended_candidate;
		}
	}
	return String();
}
#endif

static String _resolve_existing_command_candidate(const String &p_candidate, const String &p_command, const Dictionary &p_environment) {
#ifdef WINDOWS_ENABLED
	return _resolve_windows_extended_candidate(p_candidate, p_command, p_environment);
#else
	(void)p_command;
	(void)p_environment;
	if (!FileAccess::exists(p_candidate)) {
		return String();
	}
#if defined(UNIX_ENABLED) && !defined(WEB_ENABLED)
	if (access(p_candidate.utf8().get_data(), X_OK) == 0) {
		return p_candidate;
	}
	return String();
#else
	return p_candidate;
#endif
#endif
}

static String _resolve_relative_command_path(const String &p_command, const String &p_working_directory, const Dictionary &p_environment) {
	const String candidate = p_working_directory.path_join(p_command).simplify_path();
	return _resolve_existing_command_candidate(candidate, p_command, p_environment);
}

static String _resolve_with_path(const String &p_command, const String &p_working_directory, const Dictionary &p_environment) {
	const String path_env = _environment_path(p_environment);
#ifdef WINDOWS_ENABLED
	const String separator = ";";
#else
	const String separator = ":";
#endif
	const PackedStringArray path_entries = path_env.split(separator, true);
	for (int i = 0; i < path_entries.size(); i++) {
		String path_entry = path_entries[i].is_empty() ? p_working_directory : path_entries[i];
		if (path_entry.is_relative_path() && !p_working_directory.is_empty()) {
			path_entry = p_working_directory.path_join(path_entry);
		}
		const String candidate = path_entry.path_join(p_command).simplify_path();
		const String resolved_candidate = _resolve_existing_command_candidate(candidate, p_command, p_environment);
		if (!resolved_candidate.is_empty()) {
			return resolved_candidate;
		}
	}

	return String();
}

static bool _resolve_executable(const String &p_command, const String &p_working_directory, const Dictionary &p_environment, String &r_executable, String &r_error) {
	const String command = p_command.strip_edges();
	if (command.is_empty()) {
		r_error = "Command provider tasks require a command.";
		return false;
	}

	if (command.begins_with("res://")) {
		const String resolved = _resolve_existing_command_candidate(_globalize_project_path(command), command, p_environment);
		if (resolved.is_empty()) {
			r_error = vformat("Command executable '%s' does not exist.", command);
			return false;
		}
		r_executable = resolved;
		return true;
	}

	if (command.is_absolute_path()) {
		const String resolved = _resolve_existing_command_candidate(command.simplify_path(), command, p_environment);
		if (resolved.is_empty()) {
			r_error = vformat("Command executable '%s' does not exist.", command);
			return false;
		}
		r_executable = resolved;
		return true;
	}

	if (_command_has_path_separator(command)) {
		const String resolved = _resolve_relative_command_path(command, p_working_directory, p_environment);
		if (resolved.is_empty()) {
			r_error = vformat("Command executable '%s' does not exist relative to working directory '%s'.", command, p_working_directory);
			return false;
		}
		r_executable = resolved;
		return true;
	}

	const String resolved = _resolve_with_path(command, p_working_directory, p_environment);
	if (resolved.is_empty()) {
		r_error = vformat("Command executable '%s' was not found in PATH.", command);
		return false;
	}

	r_executable = resolved;
	return true;
}

static PackedStringArray _string_array_from_options(const Dictionary &p_options, const String &p_key) {
	if (!p_options.has(p_key)) {
		return PackedStringArray();
	}

	const Variant value = p_options[p_key];
	if (value.get_type() == Variant::PACKED_STRING_ARRAY) {
		return value;
	}

	PackedStringArray strings;
	if (value.get_type() == Variant::ARRAY) {
		Array array = value;
		for (int i = 0; i < array.size(); i++) {
			strings.push_back(String(array[i]));
		}
	}
	return strings;
}

static Dictionary _dictionary_from_options(const Dictionary &p_options, const String &p_key) {
	if (!p_options.has(p_key) || p_options[p_key].get_type() != Variant::DICTIONARY) {
		return Dictionary();
	}
	return Dictionary(p_options[p_key]).duplicate(true);
}

static int _int_from_options(const Dictionary &p_options, const String &p_key, int p_default) {
	if (!p_options.has(p_key) || p_options[p_key].get_type() != Variant::INT) {
		return p_default;
	}
	return MAX(1, int(p_options[p_key]));
}

static bool _append_pipe_text(const Ref<FileAccess> &p_pipe, uint64_t p_deadline_usec, String &r_text, uint64_t *r_bytes_read = nullptr) {
	if (r_bytes_read != nullptr) {
		*r_bytes_read = 0;
	}
	if (p_pipe.is_null()) {
		return true;
	}

	uint64_t remaining_budget = COMMAND_PIPE_DRAIN_BUDGET_BYTES;
	while (remaining_budget > 0) {
		if (OS::get_singleton()->get_ticks_usec() >= p_deadline_usec) {
			return false;
		}

		const uint64_t available = p_pipe->get_length();
		if (available == 0) {
			return true;
		}

		Vector<uint8_t> buffer;
		buffer.resize(MIN(MIN(available, COMMAND_PIPE_READ_CHUNK_SIZE), remaining_budget));
		const uint64_t read = p_pipe->get_buffer(buffer.ptrw(), buffer.size());
		if (read == 0) {
			return true;
		}
		r_text += String::utf8(reinterpret_cast<const char *>(buffer.ptr()), read);
		if (r_bytes_read != nullptr) {
			*r_bytes_read += read;
		}
		remaining_budget -= read;
	}

	return true;
}

static Error _launch_process(const CommandInvocation &p_invocation, Dictionary &r_process, String &r_error) {
	List<String> arguments;
	for (int i = 0; i < p_invocation.arguments.size(); i++) {
		arguments.push_back(p_invocation.arguments[i]);
	}

	r_process = OS::get_singleton()->execute_with_pipe(p_invocation.executable, arguments, false, p_invocation.working_directory, p_invocation.environment, false);
	if (r_process.is_empty() || !r_process.has("pid")) {
		r_error = vformat("Could not launch command executable '%s'.", p_invocation.original_executable);
		return ERR_CANT_FORK;
	}

	return OK;
}

static void _kill_invocation_process(const OS::ProcessID &p_pid) {
#if defined(UNIX_ENABLED) && !defined(WEB_ENABLED)
	// execute_with_pipe() starts a new session on Unix, so kill the process group before reaping the leader.
	::kill(-pid_t(p_pid), SIGKILL);
#endif
	OS::get_singleton()->kill(p_pid);
}

static void _mark_invocation_timed_out(CommandRunData &r_data, const OS::ProcessID &p_pid) {
	r_data.timed_out = true;
	_kill_invocation_process(p_pid);
	r_data.exit_code = -1;
}

static CommandRunData _run_invocation(const CommandInvocation &p_invocation) {
	CommandRunData data;

	Dictionary process;
	const Error launch_error = _launch_process(p_invocation, process, data.launch_error);
	if (launch_error != OK) {
		data.exit_code = -1;
		return data;
	}

	const OS::ProcessID pid = process["pid"];
	Ref<FileAccess> stdout_pipe = process["stdio"];
	Ref<FileAccess> stderr_pipe = process["stderr"];
	const uint64_t timeout_usec = uint64_t(p_invocation.timeout_seconds) * 1000000ULL;
	const uint64_t deadline_usec = OS::get_singleton()->get_ticks_usec() + timeout_usec;

	while (OS::get_singleton()->is_process_running(pid)) {
		if (!_append_pipe_text(stdout_pipe, deadline_usec, data.stdout_text) ||
				!_append_pipe_text(stderr_pipe, deadline_usec, data.stderr_text)) {
			_mark_invocation_timed_out(data, pid);
			break;
		}

		if (OS::get_singleton()->get_ticks_usec() >= deadline_usec) {
			_mark_invocation_timed_out(data, pid);
			break;
		}

		OS::get_singleton()->delay_usec(10000);
	}

	const uint64_t post_exit_drain_deadline_usec = OS::get_singleton()->get_ticks_usec() + COMMAND_PIPE_POST_EXIT_DRAIN_USEC;
	while (!data.timed_out) {
		uint64_t stdout_bytes_read = 0;
		uint64_t stderr_bytes_read = 0;
		if (!_append_pipe_text(stdout_pipe, post_exit_drain_deadline_usec, data.stdout_text, &stdout_bytes_read) ||
				!_append_pipe_text(stderr_pipe, post_exit_drain_deadline_usec, data.stderr_text, &stderr_bytes_read)) {
			break;
		}
		if (stdout_bytes_read == 0 && stderr_bytes_read == 0) {
			break;
		}
	}

	if (!data.timed_out) {
		data.exit_code = OS::get_singleton()->get_process_exit_code(pid);
	}

	return data;
}

static void _append_signature_line(StringBuilder &r_builder, const String &p_key, const String &p_value) {
	r_builder += itos(p_key.length());
	r_builder += "\n";
	r_builder += p_key;
	r_builder += "\n";
	r_builder += itos(p_value.length());
	r_builder += "\n";
	r_builder += p_value;
	r_builder += "\n";
}

static String _fingerprint_for_invocation(const CommandInvocation &p_invocation, const PackedStringArray &p_inputs,
		const PackedStringArray &p_outputs, const CommandRunData &p_tool_version_data) {
	StringBuilder builder;
	_append_signature_line(builder, "executable", p_invocation.executable);
	_append_signature_line(builder, "working_directory", p_invocation.working_directory);
	_append_signature_line(builder, "timeout_seconds", itos(p_invocation.timeout_seconds));
	for (int i = 0; i < p_invocation.arguments.size(); i++) {
		_append_signature_line(builder, "arg", p_invocation.arguments[i]);
	}

	Array environment_keys = p_invocation.environment.keys();
	environment_keys.sort();
	for (int i = 0; i < environment_keys.size(); i++) {
		const String key = environment_keys[i];
		_append_signature_line(builder, "env", key + "=" + String(p_invocation.environment[key]));
	}

	for (int i = 0; i < p_inputs.size(); i++) {
		const String input = _resolve_declared_input_path(p_inputs[i], p_invocation.working_directory);
		_append_signature_line(builder, "input", input);
		const PackedStringArray input_files = _input_files_for_signature(p_inputs[i], p_invocation.working_directory);
		for (int j = 0; j < input_files.size(); j++) {
			_append_signature_line(builder, "input_file", input_files[j]);
			_append_signature_line(builder, "input_sha256", FileAccess::get_sha256(input_files[j]));
		}
	}

	const PackedStringArray globalized_outputs = _globalize_project_paths(p_outputs);
	for (int i = 0; i < globalized_outputs.size(); i++) {
		_append_signature_line(builder, "output", globalized_outputs[i]);
	}

	_append_signature_line(builder, "tool_exit_code", itos(p_tool_version_data.exit_code));
	_append_signature_line(builder, "tool_timed_out", p_tool_version_data.timed_out ? "true" : "false");
	_append_signature_line(builder, "tool_launch_error", p_tool_version_data.launch_error);
	_append_signature_line(builder, "tool_stdout", p_tool_version_data.stdout_text);
	_append_signature_line(builder, "tool_stderr", p_tool_version_data.stderr_text);

	return builder.as_string().sha256_text();
}

static Dictionary _make_command_diagnostic(const String &p_message, const CommandRunData &p_run_data) {
	Dictionary diagnostic;
	diagnostic["message"] = p_message;
	diagnostic["stdout"] = p_run_data.stdout_text;
	diagnostic["stderr"] = p_run_data.stderr_text;
	diagnostic["exit_code"] = p_run_data.exit_code;
	diagnostic["timed_out"] = p_run_data.timed_out;
	diagnostic["launch_error"] = p_run_data.launch_error;
	return diagnostic;
}

static Ref<FoundryBuildCommand> _make_result_command(const CommandInvocation &p_invocation) {
	Ref<FoundryBuildCommand> command;
	command.instantiate();
	command->set_executable(p_invocation.executable);
	command->set_arguments(p_invocation.arguments);
	command->set_working_directory(p_invocation.working_directory);
	command->set_environment(p_invocation.environment);
	command->set_timeout_seconds(p_invocation.timeout_seconds);
	return command;
}

static bool _make_invocation(const String &p_command, const PackedStringArray &p_args, const String &p_working_directory,
		const Dictionary &p_environment, int p_timeout_seconds, CommandInvocation &r_invocation, String &r_error) {
	r_invocation.original_executable = p_command;
	r_invocation.arguments = _globalize_project_paths(p_args);
	r_invocation.working_directory = p_working_directory.is_empty() ? _globalize_project_path("res://") : _globalize_project_path(p_working_directory);
	r_invocation.environment = p_environment.duplicate(true);
	r_invocation.timeout_seconds = MAX(1, p_timeout_seconds);
	return _resolve_executable(p_command, r_invocation.working_directory, r_invocation.environment, r_invocation.executable, r_error);
}

} // namespace

void FoundryBuildTaskConfigSchema::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_properties", "properties"), &FoundryBuildTaskConfigSchema::set_properties);
	ClassDB::bind_method(D_METHOD("get_properties"), &FoundryBuildTaskConfigSchema::get_properties);
	ClassDB::bind_method(D_METHOD("set_required", "required"), &FoundryBuildTaskConfigSchema::set_required);
	ClassDB::bind_method(D_METHOD("get_required"), &FoundryBuildTaskConfigSchema::get_required);
	ClassDB::bind_method(D_METHOD("is_empty"), &FoundryBuildTaskConfigSchema::is_empty);

	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "properties"), "set_properties", "get_properties");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "required"), "set_required", "get_required");
}

void FoundryBuildTaskConfigSchema::set_properties(const Dictionary &p_properties) {
	properties = p_properties.duplicate(true);
}

Dictionary FoundryBuildTaskConfigSchema::get_properties() const {
	return properties.duplicate(true);
}

void FoundryBuildTaskConfigSchema::set_required(const PackedStringArray &p_required) {
	required = p_required;
}

PackedStringArray FoundryBuildTaskConfigSchema::get_required() const {
	return required;
}

bool FoundryBuildTaskConfigSchema::is_empty() const {
	return properties.is_empty() && required.is_empty();
}

void FoundryBuildCommand::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_executable", "executable"), &FoundryBuildCommand::set_executable);
	ClassDB::bind_method(D_METHOD("get_executable"), &FoundryBuildCommand::get_executable);
	ClassDB::bind_method(D_METHOD("set_arguments", "arguments"), &FoundryBuildCommand::set_arguments);
	ClassDB::bind_method(D_METHOD("get_arguments"), &FoundryBuildCommand::get_arguments);
	ClassDB::bind_method(D_METHOD("set_working_directory", "working_directory"), &FoundryBuildCommand::set_working_directory);
	ClassDB::bind_method(D_METHOD("get_working_directory"), &FoundryBuildCommand::get_working_directory);
	ClassDB::bind_method(D_METHOD("set_environment", "environment"), &FoundryBuildCommand::set_environment);
	ClassDB::bind_method(D_METHOD("get_environment"), &FoundryBuildCommand::get_environment);
	ClassDB::bind_method(D_METHOD("set_timeout_seconds", "timeout_seconds"), &FoundryBuildCommand::set_timeout_seconds);
	ClassDB::bind_method(D_METHOD("get_timeout_seconds"), &FoundryBuildCommand::get_timeout_seconds);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "executable"), "set_executable", "get_executable");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "arguments"), "set_arguments", "get_arguments");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "working_directory"), "set_working_directory", "get_working_directory");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "environment"), "set_environment", "get_environment");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "timeout_seconds"), "set_timeout_seconds", "get_timeout_seconds");
}

void FoundryBuildCommand::set_executable(const String &p_executable) {
	executable = p_executable;
}

String FoundryBuildCommand::get_executable() const {
	return executable;
}

void FoundryBuildCommand::set_arguments(const PackedStringArray &p_arguments) {
	arguments = p_arguments;
}

PackedStringArray FoundryBuildCommand::get_arguments() const {
	return arguments;
}

void FoundryBuildCommand::set_working_directory(const String &p_working_directory) {
	working_directory = p_working_directory;
}

String FoundryBuildCommand::get_working_directory() const {
	return working_directory;
}

void FoundryBuildCommand::set_environment(const Dictionary &p_environment) {
	environment = p_environment.duplicate(true);
}

Dictionary FoundryBuildCommand::get_environment() const {
	return environment.duplicate(true);
}

void FoundryBuildCommand::set_timeout_seconds(int p_timeout_seconds) {
	timeout_seconds = p_timeout_seconds;
}

int FoundryBuildCommand::get_timeout_seconds() const {
	return timeout_seconds;
}

void FoundryBuildResult::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_success", "success"), &FoundryBuildResult::set_success);
	ClassDB::bind_method(D_METHOD("is_success"), &FoundryBuildResult::is_success);
	ClassDB::bind_method(D_METHOD("set_message", "message"), &FoundryBuildResult::set_message);
	ClassDB::bind_method(D_METHOD("get_message"), &FoundryBuildResult::get_message);
	ClassDB::bind_method(D_METHOD("set_commands", "commands"), &FoundryBuildResult::set_commands);
	ClassDB::bind_method(D_METHOD("get_commands"), &FoundryBuildResult::get_commands);
	ClassDB::bind_method(D_METHOD("add_command", "command"), &FoundryBuildResult::add_command);
	ClassDB::bind_method(D_METHOD("set_stdout", "stdout"), &FoundryBuildResult::set_stdout);
	ClassDB::bind_method(D_METHOD("get_stdout"), &FoundryBuildResult::get_stdout);
	ClassDB::bind_method(D_METHOD("set_stderr", "stderr"), &FoundryBuildResult::set_stderr);
	ClassDB::bind_method(D_METHOD("get_stderr"), &FoundryBuildResult::get_stderr);
	ClassDB::bind_method(D_METHOD("set_exit_code", "exit_code"), &FoundryBuildResult::set_exit_code);
	ClassDB::bind_method(D_METHOD("get_exit_code"), &FoundryBuildResult::get_exit_code);
	ClassDB::bind_method(D_METHOD("set_timed_out", "timed_out"), &FoundryBuildResult::set_timed_out);
	ClassDB::bind_method(D_METHOD("has_timed_out"), &FoundryBuildResult::has_timed_out);
	ClassDB::bind_method(D_METHOD("set_launch_error", "launch_error"), &FoundryBuildResult::set_launch_error);
	ClassDB::bind_method(D_METHOD("get_launch_error"), &FoundryBuildResult::get_launch_error);
	ClassDB::bind_method(D_METHOD("set_diagnostics", "diagnostics"), &FoundryBuildResult::set_diagnostics);
	ClassDB::bind_method(D_METHOD("get_diagnostics"), &FoundryBuildResult::get_diagnostics);
	ClassDB::bind_method(D_METHOD("add_diagnostic", "diagnostic"), &FoundryBuildResult::add_diagnostic);
	ClassDB::bind_method(D_METHOD("set_fingerprint", "fingerprint"), &FoundryBuildResult::set_fingerprint);
	ClassDB::bind_method(D_METHOD("get_fingerprint"), &FoundryBuildResult::get_fingerprint);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "success"), "set_success", "is_success");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "message"), "set_message", "get_message");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "commands"), "set_commands", "get_commands");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "stdout"), "set_stdout", "get_stdout");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "stderr"), "set_stderr", "get_stderr");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "exit_code"), "set_exit_code", "get_exit_code");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "timed_out"), "set_timed_out", "has_timed_out");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "launch_error"), "set_launch_error", "get_launch_error");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "diagnostics"), "set_diagnostics", "get_diagnostics");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "fingerprint"), "set_fingerprint", "get_fingerprint");
}

void FoundryBuildResult::set_success(bool p_success) {
	success = p_success;
}

bool FoundryBuildResult::is_success() const {
	return success;
}

void FoundryBuildResult::set_message(const String &p_message) {
	message = p_message;
}

String FoundryBuildResult::get_message() const {
	return message;
}

void FoundryBuildResult::set_commands(const Array &p_commands) {
	commands = p_commands.duplicate(true);
}

Array FoundryBuildResult::get_commands() const {
	return commands.duplicate(true);
}

void FoundryBuildResult::add_command(const Ref<FoundryBuildCommand> &p_command) {
	ERR_FAIL_COND(p_command.is_null());
	commands.push_back(p_command);
}

void FoundryBuildResult::set_stdout(const String &p_stdout) {
	stdout_text = p_stdout;
}

String FoundryBuildResult::get_stdout() const {
	return stdout_text;
}

void FoundryBuildResult::set_stderr(const String &p_stderr) {
	stderr_text = p_stderr;
}

String FoundryBuildResult::get_stderr() const {
	return stderr_text;
}

void FoundryBuildResult::set_exit_code(int p_exit_code) {
	exit_code = p_exit_code;
}

int FoundryBuildResult::get_exit_code() const {
	return exit_code;
}

void FoundryBuildResult::set_timed_out(bool p_timed_out) {
	timed_out = p_timed_out;
}

bool FoundryBuildResult::has_timed_out() const {
	return timed_out;
}

void FoundryBuildResult::set_launch_error(const String &p_launch_error) {
	launch_error = p_launch_error;
}

String FoundryBuildResult::get_launch_error() const {
	return launch_error;
}

void FoundryBuildResult::set_diagnostics(const Array &p_diagnostics) {
	diagnostics = p_diagnostics.duplicate(true);
}

Array FoundryBuildResult::get_diagnostics() const {
	return diagnostics.duplicate(true);
}

void FoundryBuildResult::add_diagnostic(const Dictionary &p_diagnostic) {
	diagnostics.push_back(p_diagnostic.duplicate(true));
}

void FoundryBuildResult::set_fingerprint(const String &p_fingerprint) {
	fingerprint = p_fingerprint;
}

String FoundryBuildResult::get_fingerprint() const {
	return fingerprint;
}

void FoundryBuildContext::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_provider_id", "provider_id"), &FoundryBuildContext::set_provider_id);
	ClassDB::bind_method(D_METHOD("get_provider_id"), &FoundryBuildContext::get_provider_id);
	ClassDB::bind_method(D_METHOD("set_task_name", "task_name"), &FoundryBuildContext::set_task_name);
	ClassDB::bind_method(D_METHOD("get_task_name"), &FoundryBuildContext::get_task_name);
	ClassDB::bind_method(D_METHOD("set_project_config_path", "project_config_path"), &FoundryBuildContext::set_project_config_path);
	ClassDB::bind_method(D_METHOD("get_project_config_path"), &FoundryBuildContext::get_project_config_path);
	ClassDB::bind_method(D_METHOD("set_options", "options"), &FoundryBuildContext::set_options);
	ClassDB::bind_method(D_METHOD("get_options"), &FoundryBuildContext::get_options);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "provider_id"), "set_provider_id", "get_provider_id");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "task_name"), "set_task_name", "get_task_name");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "project_config_path"), "set_project_config_path", "get_project_config_path");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "options"), "set_options", "get_options");
}

void FoundryBuildContext::set_provider_id(const String &p_provider_id) {
	provider_id = p_provider_id;
}

String FoundryBuildContext::get_provider_id() const {
	return provider_id;
}

void FoundryBuildContext::set_task_name(const String &p_task_name) {
	task_name = p_task_name;
}

String FoundryBuildContext::get_task_name() const {
	return task_name;
}

void FoundryBuildContext::set_project_config_path(const String &p_project_config_path) {
	project_config_path = p_project_config_path;
}

String FoundryBuildContext::get_project_config_path() const {
	return project_config_path;
}

void FoundryBuildContext::set_options(const Dictionary &p_options) {
	options = p_options.duplicate(true);
}

Dictionary FoundryBuildContext::get_options() const {
	return options.duplicate(true);
}

void FoundryBuildTask::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_config_schema"), &FoundryBuildTask::get_config_schema);
	ClassDB::bind_method(D_METHOD("run", "context"), &FoundryBuildTask::run);
}

Ref<FoundryBuildTaskConfigSchema> FoundryBuildTask::get_config_schema() const {
	Ref<FoundryBuildTaskConfigSchema> schema;
	schema.instantiate();
	return schema;
}

Ref<FoundryBuildResult> FoundryBuildTask::run(const Ref<FoundryBuildContext> &p_context) {
	(void)p_context;

	Ref<FoundryBuildResult> result;
	result.instantiate();
	result->set_success(true);
	return result;
}

void FoundryCommandBuildTask::_bind_methods() {
}

Ref<FoundryBuildTaskConfigSchema> FoundryCommandBuildTask::get_config_schema() const {
	Ref<FoundryBuildTaskConfigSchema> schema;
	schema.instantiate();

	Dictionary properties;
	properties["command"] = "String";
	properties["args"] = "PackedStringArray";
	properties["working_directory"] = "String";
	properties["environment"] = "Dictionary";
	properties["inputs"] = "PackedStringArray";
	properties["outputs"] = "PackedStringArray";
	properties["timeout_seconds"] = "int";
	properties["tool_version_command"] = "PackedStringArray";
	schema->set_properties(properties);

	PackedStringArray required;
	required.push_back("command");
	schema->set_required(required);
	return schema;
}

Ref<FoundryBuildResult> FoundryCommandBuildTask::run(const Ref<FoundryBuildContext> &p_context) {
	Ref<FoundryBuildResult> result;
	result.instantiate();

	if (p_context.is_null()) {
		result->set_success(false);
		result->set_message("Command provider run requires a build context.");
		result->set_exit_code(-1);
		CommandRunData diagnostic_data;
		diagnostic_data.exit_code = -1;
		diagnostic_data.launch_error = result->get_message();
		result->set_launch_error(diagnostic_data.launch_error);
		result->add_diagnostic(_make_command_diagnostic(result->get_message(), diagnostic_data));
		return result;
	}

	const Dictionary options = p_context->get_options();
	const String command_text = options.has("command") ? String(options["command"]) : String();
	const PackedStringArray args = _string_array_from_options(options, "args");
	const String working_directory = options.has("working_directory") ? String(options["working_directory"]) : "res://";
	const Dictionary environment = _dictionary_from_options(options, "environment");
	const int timeout_seconds = _int_from_options(options, "timeout_seconds", 60);
	const PackedStringArray inputs = _string_array_from_options(options, "inputs");
	const PackedStringArray outputs = _string_array_from_options(options, "outputs");
	const PackedStringArray tool_version_command = _string_array_from_options(options, "tool_version_command");

	CommandInvocation invocation;
	String invocation_error;
	if (!_make_invocation(command_text, args, working_directory, environment, timeout_seconds, invocation, invocation_error)) {
		result->set_success(false);
		result->set_message(invocation_error);
		result->set_exit_code(-1);
		result->set_launch_error(invocation_error);
		CommandRunData diagnostic_data;
		diagnostic_data.exit_code = -1;
		diagnostic_data.launch_error = invocation_error;
		result->add_diagnostic(_make_command_diagnostic(invocation_error, diagnostic_data));
		return result;
	}
	result->add_command(_make_result_command(invocation));

	CommandRunData tool_version_data;
	if (!tool_version_command.is_empty()) {
		PackedStringArray tool_args;
		for (int i = 1; i < tool_version_command.size(); i++) {
			tool_args.push_back(tool_version_command[i]);
		}

		CommandInvocation tool_invocation;
		String tool_invocation_error;
		if (_make_invocation(tool_version_command[0], tool_args, working_directory, environment, timeout_seconds, tool_invocation, tool_invocation_error)) {
			tool_version_data = _run_invocation(tool_invocation);
		} else {
			tool_version_data.exit_code = -1;
			tool_version_data.launch_error = tool_invocation_error;
		}

		if (tool_version_data.exit_code != 0 || !tool_version_data.launch_error.is_empty() || tool_version_data.timed_out) {
			result->add_diagnostic(_make_command_diagnostic("Tool version command failed.", tool_version_data));
		}
	}

	result->set_fingerprint(_fingerprint_for_invocation(invocation, inputs, outputs, tool_version_data));

	const CommandRunData run_data = _run_invocation(invocation);
	result->set_stdout(run_data.stdout_text);
	result->set_stderr(run_data.stderr_text);
	result->set_exit_code(run_data.exit_code);
	result->set_timed_out(run_data.timed_out);
	result->set_launch_error(run_data.launch_error);

	const bool success = run_data.exit_code == 0 && !run_data.timed_out && run_data.launch_error.is_empty();
	result->set_success(success);
	if (!success) {
		String message;
		if (!run_data.launch_error.is_empty()) {
			message = run_data.launch_error;
		} else if (run_data.timed_out) {
			message = vformat("Command '%s' timed out after %d second(s).", command_text, timeout_seconds);
		} else {
			message = vformat("Command '%s' exited with code %d.", command_text, run_data.exit_code);
		}
		result->set_message(message);
		result->add_diagnostic(_make_command_diagnostic(message, run_data));
	}

	return result;
}
