/**************************************************************************/
/*  fs_project_scripts.cpp                                                */
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

#include "fs_project_scripts.h"

#include "foundry_script.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/resource_loader.h"

#ifndef FOUNDRY_SCRIPT_NO_FRONTEND

#include "fs_cache.h"
#include "fs_compiler.h"
#include "fs_parser.h"
#include "fs_trait_utils.h"

#ifdef DEBUG_ENABLED
#include "fs_warning.h"
#endif // DEBUG_ENABLED

namespace {
TypedArray<FSAnnotation> usages_to_descriptors(const Vector<FoundryScript::AnnotationUsage> &p_usages) {
	TypedArray<FSAnnotation> result;
	for (const FoundryScript::AnnotationUsage &usage : p_usages) {
		result.push_back(FSAnnotation::from_usage(usage));
	}
	return result;
}

HashMap<StringName, TypedArray<FSAnnotation>> parameter_usages_to_descriptors(const HashMap<StringName, Vector<FoundryScript::AnnotationUsage>> &p_usages) {
	HashMap<StringName, TypedArray<FSAnnotation>> result;
	for (const KeyValue<StringName, Vector<FoundryScript::AnnotationUsage>> &entry : p_usages) {
		result[entry.key] = usages_to_descriptors(entry.value);
	}
	return result;
}

TypedArray<Dictionary> collect_index_diagnostics(const FSParser *p_parser) {
	TypedArray<Dictionary> diagnostics;
	if (p_parser == nullptr) {
		return diagnostics;
	}

	for (const FSParser::ParserError &error : p_parser->get_errors()) {
		Dictionary entry;
		entry["message"] = error.message;
		entry["line"] = error.line;
		entry["column"] = error.column;
		diagnostics.push_back(entry);
	}

#ifdef DEBUG_ENABLED
	for (const FSWarning &warning : p_parser->get_warnings()) {
		if (FSWarning::get_default_value(warning.code) != FSWarning::ERROR) {
			continue;
		}
		Dictionary entry;
		entry["message"] = warning.get_message();
		entry["line"] = warning.start_line;
		entry["column"] = 0;
		diagnostics.push_back(entry);
	}
#endif // DEBUG_ENABLED

	return diagnostics;
}

const FSParser::ClassNode *get_root_class(const FSParser *p_parser) {
	return p_parser != nullptr ? p_parser->get_tree() : nullptr;
}

const FSParser::ClassNode *resolve_descriptor_class(const FSParser::ClassNode *p_head) {
	if (p_head == nullptr) {
		return nullptr;
	}

	if (p_head->identifier != nullptr) {
		return p_head;
	}

	if (p_head->extends_used && p_head->namespace_name.is_empty()) {
		return p_head;
	}

	for (const FSParser::ClassNode::Member &member : p_head->members) {
		if (member.type == FSParser::ClassNode::Member::CLASS && member.m_class != nullptr) {
			return member.m_class;
		}
	}

	return p_head;
}

String build_fully_qualified_name(const FSParser::ClassNode *p_class) {
	if (p_class == nullptr) {
		return String();
	}

	const StringName global_name = p_class->get_global_name();
	if (global_name != StringName()) {
		return global_name;
	}

	String namespace_name = p_class->namespace_name;
	if (namespace_name.is_empty() && p_class->outer != nullptr) {
		namespace_name = p_class->outer->namespace_name;
	}

	if (!namespace_name.is_empty() && p_class->identifier != nullptr) {
		return namespace_name + "." + String(p_class->identifier->name);
	}

	if (p_class->identifier != nullptr) {
		return p_class->identifier->name;
	}

	return p_class->fqcn;
}

Ref<FSParserRef> fetch_indexed_parser(const String &p_path, TypedArray<Dictionary> &r_diagnostics, bool &r_indexed_ok) {
	r_indexed_ok = false;
	r_diagnostics = TypedArray<Dictionary>();

	Error err = OK;
	Ref<FSParserRef> parser_ref = FSCache::get_parser(p_path, FSParserRef::INTERFACE_SOLVED, err);
	FSParser *parser = parser_ref.is_valid() ? parser_ref->get_parser() : nullptr;
	r_diagnostics = collect_index_diagnostics(parser);
	r_indexed_ok = err == OK && parser != nullptr && parser->get_errors().is_empty();
	return parser_ref;
}

StringName base_type_name_from_class(const FSParser::ClassNode *p_class) {
	if (p_class == nullptr || !p_class->base_type.is_set()) {
		return StringName();
	}
	return StringName(p_class->base_type.to_string());
}
} // namespace

#endif // FOUNDRY_SCRIPT_NO_FRONTEND

namespace {
void collect_script_paths_recursive(ProjectSettings *p_project_settings, const String &p_directory_path, bool p_recursive, Vector<String> &r_paths) {
	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (dir.is_null() || dir->change_dir(p_directory_path) != OK) {
		return;
	}

	dir->list_dir_begin();
	for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
		if (entry == "." || entry == "..") {
			continue;
		}
		if (entry.begins_with(".")) {
			continue;
		}

		const String absolute_entry = dir->get_current_dir().path_join(entry);
		if (dir->current_is_dir()) {
			if (p_recursive) {
				collect_script_paths_recursive(p_project_settings, absolute_entry, true, r_paths);
			}
			continue;
		}

		if (entry.ends_with(".fs")) {
			r_paths.push_back(p_project_settings->localize_path(absolute_entry));
		}
	}
	dir->list_dir_end();
}

void collect_script_paths(const String &p_root_path, bool p_recursive, Vector<String> &r_paths) {
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	ERR_FAIL_NULL(project_settings);

	const String absolute_root = project_settings->globalize_path(p_root_path);
	collect_script_paths_recursive(project_settings, absolute_root, p_recursive, r_paths);
	r_paths.sort();
}
} // namespace

Ref<FSScriptDescriptor> FSScriptDescriptor::build(const String &p_path) {
	Ref<FSScriptDescriptor> descriptor;
	descriptor.instantiate();
	descriptor->path = p_path;

#ifndef FOUNDRY_SCRIPT_NO_FRONTEND
	bool indexed_ok = false;
	TypedArray<Dictionary> diagnostics;
	Ref<FSParserRef> parser_ref = fetch_indexed_parser(p_path, diagnostics, indexed_ok);
	descriptor->indexed_ok = indexed_ok;
	descriptor->index_diagnostics = diagnostics;

	const FSParser::ClassNode *root = parser_ref.is_valid() ? resolve_descriptor_class(get_root_class(parser_ref->get_parser())) : nullptr;
	if (root != nullptr) {
		descriptor->global_class_name = root->get_global_name();
		descriptor->fully_qualified_name = StringName(build_fully_qualified_name(root));
		descriptor->base_type = base_type_name_from_class(root);
		descriptor->is_trait = root->is_trait;
		descriptor->is_abstract = root->is_abstract;
	}
#else
	descriptor->indexed_ok = false;
#endif // FOUNDRY_SCRIPT_NO_FRONTEND

	return descriptor;
}

TypedArray<FSAnnotation> FSScriptDescriptor::get_class_annotations() const {
	TypedArray<FSAnnotation> result;
#ifndef FOUNDRY_SCRIPT_NO_FRONTEND
	if (!indexed_ok) {
		return result;
	}

	bool indexed = false;
	TypedArray<Dictionary> diagnostics;
	Ref<FSParserRef> parser_ref = fetch_indexed_parser(path, diagnostics, indexed);
	const FSParser::ClassNode *root = parser_ref.is_valid() ? resolve_descriptor_class(get_root_class(parser_ref->get_parser())) : nullptr;
	if (root == nullptr) {
		return result;
	}

	Vector<FoundryScript::AnnotationUsage> usages;
	FSCompiler::collect_passive_annotations(root->annotations, usages);
	return usages_to_descriptors(usages);
#else
	(void)indexed_ok;
#endif // FOUNDRY_SCRIPT_NO_FRONTEND
	return result;
}

TypedArray<FSMethodDescriptor> FSScriptDescriptor::get_methods() const {
	TypedArray<FSMethodDescriptor> result;
#ifndef FOUNDRY_SCRIPT_NO_FRONTEND
	if (!indexed_ok) {
		return result;
	}

	bool indexed = false;
	TypedArray<Dictionary> diagnostics;
	Ref<FSParserRef> parser_ref = fetch_indexed_parser(path, diagnostics, indexed);
	const FSParser::ClassNode *root = parser_ref.is_valid() ? resolve_descriptor_class(get_root_class(parser_ref->get_parser())) : nullptr;
	if (root == nullptr) {
		return result;
	}

	for (const FSParser::ClassNode::Member &member : root->members) {
		if (member.type != FSParser::ClassNode::Member::FUNCTION || member.function == nullptr) {
			continue;
		}

		Vector<FoundryScript::AnnotationUsage> method_usages;
		FSCompiler::collect_passive_annotations(member.function->annotations, method_usages);
		HashMap<StringName, Vector<FoundryScript::AnnotationUsage>> parameter_usages;
		FSCompiler::collect_passive_parameter_annotations(member.function->parameters, member.function->rest_parameter, parameter_usages);
		result.push_back(FSMethodDescriptor::create(
				member.function->info,
				usages_to_descriptors(method_usages),
				true,
				parameter_usages_to_descriptors(parameter_usages)));
	}
#else
	(void)indexed_ok;
#endif // FOUNDRY_SCRIPT_NO_FRONTEND
	return result;
}

bool FSScriptDescriptor::implements_trait(const StringName &p_trait_name) const {
#ifndef FOUNDRY_SCRIPT_NO_FRONTEND
	if (!indexed_ok || p_trait_name == StringName()) {
		return false;
	}

	bool indexed = false;
	TypedArray<Dictionary> diagnostics;
	Ref<FSParserRef> parser_ref = fetch_indexed_parser(path, diagnostics, indexed);
	const FSParser::ClassNode *root = parser_ref.is_valid() ? resolve_descriptor_class(get_root_class(parser_ref->get_parser())) : nullptr;
	return fs_class_has_named_trait(root, p_trait_name);
#else
	(void)p_trait_name;
	return false;
#endif // FOUNDRY_SCRIPT_NO_FRONTEND
}

Ref<Script> FSScriptDescriptor::load_script() const {
#ifndef FOUNDRY_SCRIPT_NO_FRONTEND
	Error err = OK;
	Ref<FoundryScript> script = FSCache::get_full_script(path, err);
	if (err != OK || script.is_null()) {
		return Ref<Script>();
	}
	return script;
#else
	return ResourceLoader::load(path, "Script");
#endif // FOUNDRY_SCRIPT_NO_FRONTEND
}

TypedArray<FSScriptDescriptor> FSProjectScripts::list_scripts_under(const String &p_root_path, bool p_recursive) const {
	TypedArray<FSScriptDescriptor> result;
	Vector<String> paths;
	collect_script_paths(p_root_path, p_recursive, paths);
	for (const String &path : paths) {
		result.push_back(FSScriptDescriptor::build(path));
	}
	return result;
}

Ref<FSScriptDescriptor> FSProjectScripts::get_script_descriptor(const String &p_path) const {
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	ERR_FAIL_NULL_V(project_settings, Ref<FSScriptDescriptor>());

	const String absolute_path = project_settings->globalize_path(p_path);
	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (dir.is_null() || !dir->file_exists(absolute_path)) {
		return Ref<FSScriptDescriptor>();
	}

	return FSScriptDescriptor::build(p_path);
}

void FSScriptDescriptor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_path"), &FSScriptDescriptor::get_path);
	ClassDB::bind_method(D_METHOD("get_global_class_name"), &FSScriptDescriptor::get_global_class_name);
	ClassDB::bind_method(D_METHOD("get_fully_qualified_name"), &FSScriptDescriptor::get_fully_qualified_name);
	ClassDB::bind_method(D_METHOD("get_base_type"), &FSScriptDescriptor::get_base_type);
	ClassDB::bind_method(D_METHOD("get_is_trait"), &FSScriptDescriptor::get_is_trait);
	ClassDB::bind_method(D_METHOD("get_is_abstract"), &FSScriptDescriptor::get_is_abstract);
	ClassDB::bind_method(D_METHOD("get_indexed_ok"), &FSScriptDescriptor::get_indexed_ok);
	ClassDB::bind_method(D_METHOD("get_index_diagnostics"), &FSScriptDescriptor::get_index_diagnostics);
	ClassDB::bind_method(D_METHOD("get_class_annotations"), &FSScriptDescriptor::get_class_annotations);
	ClassDB::bind_method(D_METHOD("get_methods"), &FSScriptDescriptor::get_methods);
	ClassDB::bind_method(D_METHOD("implements_trait", "trait_name"), &FSScriptDescriptor::implements_trait);
	ClassDB::bind_method(D_METHOD("load_script"), &FSScriptDescriptor::load_script);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "path"), "", "get_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "global_class_name"), "", "get_global_class_name");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "fully_qualified_name"), "", "get_fully_qualified_name");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "base_type"), "", "get_base_type");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_trait"), "", "get_is_trait");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_abstract"), "", "get_is_abstract");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "indexed_ok"), "", "get_indexed_ok");
}

void FSProjectScripts::_bind_methods() {
	ClassDB::bind_method(D_METHOD("list_scripts_under", "root_path", "recursive"), &FSProjectScripts::list_scripts_under, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("get_script_descriptor", "path"), &FSProjectScripts::get_script_descriptor);
}
