/**************************************************************************/
/*  foundry_build_task.h                                                  */
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

#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"

class FoundryBuildTaskConfigSchema : public RefCounted {
	FOUNDRY_CLASS(FoundryBuildTaskConfigSchema, RefCounted);

	Dictionary properties;
	PackedStringArray required;

protected:
	static void _bind_methods();

public:
	void set_properties(const Dictionary &p_properties);
	Dictionary get_properties() const;

	void set_required(const PackedStringArray &p_required);
	PackedStringArray get_required() const;

	bool is_empty() const;
};

class FoundryBuildCommand : public RefCounted {
	FOUNDRY_CLASS(FoundryBuildCommand, RefCounted);

	String executable;
	PackedStringArray arguments;
	String working_directory = "res://";
	Dictionary environment;
	int timeout_seconds = 60;

protected:
	static void _bind_methods();

public:
	void set_executable(const String &p_executable);
	String get_executable() const;

	void set_arguments(const PackedStringArray &p_arguments);
	PackedStringArray get_arguments() const;

	void set_working_directory(const String &p_working_directory);
	String get_working_directory() const;

	void set_environment(const Dictionary &p_environment);
	Dictionary get_environment() const;

	void set_timeout_seconds(int p_timeout_seconds);
	int get_timeout_seconds() const;
};

class FoundryBuildResult : public RefCounted {
	FOUNDRY_CLASS(FoundryBuildResult, RefCounted);

	bool success = true;
	String message;
	Array commands;
	String stdout_text;
	String stderr_text;
	int exit_code = 0;
	bool timed_out = false;
	String launch_error;
	Array diagnostics;
	String fingerprint;

protected:
	static void _bind_methods();

public:
	void set_success(bool p_success);
	bool is_success() const;

	void set_message(const String &p_message);
	String get_message() const;

	void set_commands(const Array &p_commands);
	Array get_commands() const;

	void add_command(const Ref<FoundryBuildCommand> &p_command);

	void set_stdout(const String &p_stdout);
	String get_stdout() const;

	void set_stderr(const String &p_stderr);
	String get_stderr() const;

	void set_exit_code(int p_exit_code);
	int get_exit_code() const;

	void set_timed_out(bool p_timed_out);
	bool has_timed_out() const;

	void set_launch_error(const String &p_launch_error);
	String get_launch_error() const;

	void set_diagnostics(const Array &p_diagnostics);
	Array get_diagnostics() const;

	void add_diagnostic(const Dictionary &p_diagnostic);

	void set_fingerprint(const String &p_fingerprint);
	String get_fingerprint() const;
};

class FoundryBuildContext : public RefCounted {
	FOUNDRY_CLASS(FoundryBuildContext, RefCounted);

	String provider_id;
	String task_name;
	String project_config_path = "res://project.foundry";
	Dictionary options;

protected:
	static void _bind_methods();

public:
	void set_provider_id(const String &p_provider_id);
	String get_provider_id() const;

	void set_task_name(const String &p_task_name);
	String get_task_name() const;

	void set_project_config_path(const String &p_project_config_path);
	String get_project_config_path() const;

	void set_options(const Dictionary &p_options);
	Dictionary get_options() const;
};

class FoundryBuildTask : public RefCounted {
	FOUNDRY_CLASS(FoundryBuildTask, RefCounted);

protected:
	static void _bind_methods();

public:
	virtual Ref<FoundryBuildTaskConfigSchema> get_config_schema() const;
	virtual Ref<FoundryBuildResult> run(const Ref<FoundryBuildContext> &p_context);
};

class FoundryCommandBuildTask : public FoundryBuildTask {
	FOUNDRY_CLASS(FoundryCommandBuildTask, FoundryBuildTask);

protected:
	static void _bind_methods();

public:
	virtual Ref<FoundryBuildTaskConfigSchema> get_config_schema() const override;
	virtual Ref<FoundryBuildResult> run(const Ref<FoundryBuildContext> &p_context) override;
};
