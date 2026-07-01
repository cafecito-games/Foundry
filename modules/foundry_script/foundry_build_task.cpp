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

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "success"), "set_success", "is_success");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "message"), "set_message", "get_message");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "commands"), "set_commands", "get_commands");
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
	properties["timeout_seconds"] = "int";
	schema->set_properties(properties);

	PackedStringArray required;
	required.push_back("command");
	schema->set_required(required);
	return schema;
}
