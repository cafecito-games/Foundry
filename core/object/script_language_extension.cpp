/**************************************************************************/
/*  script_language_extension.cpp                                         */
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

#include "script_language_extension.h"

void ScriptExtension::_bind_methods() {
	FOUNDRY_VIRTUAL_BIND(_editor_can_reload_from_file);
	FOUNDRY_VIRTUAL_BIND(_placeholder_erased, "placeholder");

	FOUNDRY_VIRTUAL_BIND(_can_instantiate);
	FOUNDRY_VIRTUAL_BIND(_get_base_script);
	FOUNDRY_VIRTUAL_BIND(_get_global_name);
	FOUNDRY_VIRTUAL_BIND(_inherits_script, "script");

	FOUNDRY_VIRTUAL_BIND(_get_instance_base_type);
	FOUNDRY_VIRTUAL_BIND(_instance_create, "for_object");
	FOUNDRY_VIRTUAL_BIND(_placeholder_instance_create, "for_object");

	FOUNDRY_VIRTUAL_BIND(_instance_has, "object");

	FOUNDRY_VIRTUAL_BIND(_has_source_code);
	FOUNDRY_VIRTUAL_BIND(_get_source_code);

	FOUNDRY_VIRTUAL_BIND(_set_source_code, "code");
	FOUNDRY_VIRTUAL_BIND(_reload, "keep_state");

	FOUNDRY_VIRTUAL_BIND(_get_doc_class_name);
	FOUNDRY_VIRTUAL_BIND(_get_documentation);
	FOUNDRY_VIRTUAL_BIND(_get_class_icon_path);

	FOUNDRY_VIRTUAL_BIND(_has_method, "method");
	FOUNDRY_VIRTUAL_BIND(_has_static_method, "method");

	FOUNDRY_VIRTUAL_BIND(_get_script_method_argument_count, "method");

	FOUNDRY_VIRTUAL_BIND(_get_method_info, "method");

	FOUNDRY_VIRTUAL_BIND(_is_tool);
	FOUNDRY_VIRTUAL_BIND(_is_valid);
	FOUNDRY_VIRTUAL_BIND(_is_abstract);
	FOUNDRY_VIRTUAL_BIND(_get_language);

	FOUNDRY_VIRTUAL_BIND(_has_script_signal, "signal");
	FOUNDRY_VIRTUAL_BIND(_get_script_signal_list);
	FOUNDRY_VIRTUAL_BIND(_get_script_trait_list);

	FOUNDRY_VIRTUAL_BIND(_has_property_default_value, "property");
	FOUNDRY_VIRTUAL_BIND(_get_property_default_value, "property");

	FOUNDRY_VIRTUAL_BIND(_update_exports);
	FOUNDRY_VIRTUAL_BIND(_get_script_method_list);
	FOUNDRY_VIRTUAL_BIND(_get_script_property_list);

	FOUNDRY_VIRTUAL_BIND(_get_member_line, "member");

	FOUNDRY_VIRTUAL_BIND(_get_constants);
	FOUNDRY_VIRTUAL_BIND(_get_members);
	FOUNDRY_VIRTUAL_BIND(_is_placeholder_fallback_enabled);

	FOUNDRY_VIRTUAL_BIND(_get_rpc_config);
}

void ScriptLanguageExtension::_bind_methods() {
	FOUNDRY_VIRTUAL_BIND(_get_name);
	FOUNDRY_VIRTUAL_BIND(_init);
	FOUNDRY_VIRTUAL_BIND(_get_type);
	FOUNDRY_VIRTUAL_BIND(_get_extension);
	FOUNDRY_VIRTUAL_BIND(_finish);

	FOUNDRY_VIRTUAL_BIND(_get_reserved_words);
	FOUNDRY_VIRTUAL_BIND(_is_control_flow_keyword, "keyword");
	FOUNDRY_VIRTUAL_BIND(_get_comment_delimiters);
	FOUNDRY_VIRTUAL_BIND(_get_doc_comment_delimiters);
	FOUNDRY_VIRTUAL_BIND(_get_string_delimiters);
	FOUNDRY_VIRTUAL_BIND(_make_template, "template", "class_name", "base_class_name");
	FOUNDRY_VIRTUAL_BIND(_get_built_in_templates, "object");
	FOUNDRY_VIRTUAL_BIND(_is_using_templates);
	FOUNDRY_VIRTUAL_BIND(_validate, "script", "path", "validate_functions", "validate_errors", "validate_warnings", "validate_safe_lines");

	FOUNDRY_VIRTUAL_BIND(_validate_path, "path");
	FOUNDRY_VIRTUAL_BIND(_create_script);
	FOUNDRY_VIRTUAL_BIND(_supports_builtin_mode);
	FOUNDRY_VIRTUAL_BIND(_supports_documentation);
	FOUNDRY_VIRTUAL_BIND(_can_inherit_from_file);
	FOUNDRY_VIRTUAL_BIND(_find_function, "function", "code");
	FOUNDRY_VIRTUAL_BIND(_make_function, "class_name", "function_name", "function_args");
	FOUNDRY_VIRTUAL_BIND(_can_make_function);
	FOUNDRY_VIRTUAL_BIND(_open_in_external_editor, "script", "line", "column");
	FOUNDRY_VIRTUAL_BIND(_overrides_external_editor);
	FOUNDRY_VIRTUAL_BIND(_preferred_file_name_casing);

	FOUNDRY_VIRTUAL_BIND(_complete_code, "code", "path", "owner");
	FOUNDRY_VIRTUAL_BIND(_lookup_code, "code", "symbol", "path", "owner");
	FOUNDRY_VIRTUAL_BIND(_auto_indent_code, "code", "from_line", "to_line");

	FOUNDRY_VIRTUAL_BIND(_add_global_constant, "name", "value");
	FOUNDRY_VIRTUAL_BIND(_add_named_global_constant, "name", "value");
	FOUNDRY_VIRTUAL_BIND(_remove_named_global_constant, "name");

	FOUNDRY_VIRTUAL_BIND(_thread_enter);
	FOUNDRY_VIRTUAL_BIND(_thread_exit);
	FOUNDRY_VIRTUAL_BIND(_debug_get_error);
	FOUNDRY_VIRTUAL_BIND(_debug_get_stack_level_count);

	FOUNDRY_VIRTUAL_BIND(_debug_get_stack_level_line, "level");
	FOUNDRY_VIRTUAL_BIND(_debug_get_stack_level_function, "level");
	FOUNDRY_VIRTUAL_BIND(_debug_get_stack_level_source, "level");
	FOUNDRY_VIRTUAL_BIND(_debug_get_stack_level_locals, "level", "max_subitems", "max_depth");
	FOUNDRY_VIRTUAL_BIND(_debug_get_stack_level_members, "level", "max_subitems", "max_depth");
	FOUNDRY_VIRTUAL_BIND(_debug_get_stack_level_instance, "level");
	FOUNDRY_VIRTUAL_BIND(_debug_get_globals, "max_subitems", "max_depth");
	FOUNDRY_VIRTUAL_BIND(_debug_parse_stack_level_expression, "level", "expression", "max_subitems", "max_depth");

	FOUNDRY_VIRTUAL_BIND(_debug_get_current_stack_info);

	FOUNDRY_VIRTUAL_BIND(_reload_all_scripts);
	FOUNDRY_VIRTUAL_BIND(_reload_scripts, "scripts", "soft_reload");
	FOUNDRY_VIRTUAL_BIND(_reload_tool_script, "script", "soft_reload");

	FOUNDRY_VIRTUAL_BIND(_get_recognized_extensions);
	FOUNDRY_VIRTUAL_BIND(_get_public_functions);
	FOUNDRY_VIRTUAL_BIND(_get_public_constants);
	FOUNDRY_VIRTUAL_BIND(_get_public_annotations);

	FOUNDRY_VIRTUAL_BIND(_profiling_start);
	FOUNDRY_VIRTUAL_BIND(_profiling_stop);
	FOUNDRY_VIRTUAL_BIND(_profiling_set_save_native_calls, "enable");

	FOUNDRY_VIRTUAL_BIND(_profiling_get_accumulated_data, "info_array", "info_max");
	FOUNDRY_VIRTUAL_BIND(_profiling_get_frame_data, "info_array", "info_max");

	FOUNDRY_VIRTUAL_BIND(_frame);

	FOUNDRY_VIRTUAL_BIND(_handles_global_class_type, "type");
	FOUNDRY_VIRTUAL_BIND(_get_global_class_name, "path");

	BIND_ENUM_CONSTANT(LOOKUP_RESULT_SCRIPT_LOCATION);
	BIND_ENUM_CONSTANT(LOOKUP_RESULT_CLASS);
	BIND_ENUM_CONSTANT(LOOKUP_RESULT_CLASS_CONSTANT);
	BIND_ENUM_CONSTANT(LOOKUP_RESULT_CLASS_PROPERTY);
	BIND_ENUM_CONSTANT(LOOKUP_RESULT_CLASS_METHOD);
	BIND_ENUM_CONSTANT(LOOKUP_RESULT_CLASS_SIGNAL);
	BIND_ENUM_CONSTANT(LOOKUP_RESULT_CLASS_ENUM);
	BIND_ENUM_CONSTANT(LOOKUP_RESULT_CLASS_TBD_GLOBALSCOPE); // Deprecated.
	BIND_ENUM_CONSTANT(LOOKUP_RESULT_CLASS_ANNOTATION);
	BIND_ENUM_CONSTANT(LOOKUP_RESULT_LOCAL_CONSTANT);
	BIND_ENUM_CONSTANT(LOOKUP_RESULT_LOCAL_VARIABLE);
	BIND_ENUM_CONSTANT(LOOKUP_RESULT_CLASS_TUPLE);
	BIND_ENUM_CONSTANT(LOOKUP_RESULT_MAX);

	BIND_ENUM_CONSTANT(LOCATION_LOCAL);
	BIND_ENUM_CONSTANT(LOCATION_PARENT_MASK);
	BIND_ENUM_CONSTANT(LOCATION_OTHER_USER_CODE);
	BIND_ENUM_CONSTANT(LOCATION_OTHER);

	BIND_ENUM_CONSTANT(CODE_COMPLETION_KIND_CLASS);
	BIND_ENUM_CONSTANT(CODE_COMPLETION_KIND_FUNCTION);
	BIND_ENUM_CONSTANT(CODE_COMPLETION_KIND_SIGNAL);
	BIND_ENUM_CONSTANT(CODE_COMPLETION_KIND_VARIABLE);
	BIND_ENUM_CONSTANT(CODE_COMPLETION_KIND_MEMBER);
	BIND_ENUM_CONSTANT(CODE_COMPLETION_KIND_ENUM);
	BIND_ENUM_CONSTANT(CODE_COMPLETION_KIND_CONSTANT);
	BIND_ENUM_CONSTANT(CODE_COMPLETION_KIND_NODE_PATH);
	BIND_ENUM_CONSTANT(CODE_COMPLETION_KIND_FILE_PATH);
	BIND_ENUM_CONSTANT(CODE_COMPLETION_KIND_PLAIN_TEXT);
	BIND_ENUM_CONSTANT(CODE_COMPLETION_KIND_MAX);
}
