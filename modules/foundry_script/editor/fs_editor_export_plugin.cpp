/**************************************************************************/
/*  fs_editor_export_plugin.cpp                                           */
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

#include "fs_editor_export_plugin.h"

#include "fs_export_compilation_scope.h"

#include "../foundry_script.h"
#include "../fs_bytecode_export.h"
#include "../fs_cache.h"
#include "../fs_parser.h"
#include "../fs_tokenizer_buffer.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/string/print_string.h"
#include "editor/export/editor_export.h"

void EditorExportFoundryScript::_add_export_info(const String &p_message) {
	Ref<EditorExportPlatform> platform = get_export_platform();
	if (platform.is_valid()) {
		platform->add_message(EditorExportPlatform::EXPORT_MESSAGE_INFO, TTR("Compiled Script Export"), p_message);
	} else {
		print_line(p_message);
	}
}

void EditorExportFoundryScript::_add_export_error(const String &p_message) {
	Ref<EditorExportPlatform> platform = get_export_platform();
	if (platform.is_valid()) {
		platform->add_message(EditorExportPlatform::EXPORT_MESSAGE_ERROR, TTR("Compiled Script Export"), p_message);
	} else {
		ERR_PRINT(p_message);
	}
}

void EditorExportFoundryScript::_clear_name_mangling_state() {
	name_mangling_prepared = false;
	mangled_scripts.clear();
}

String EditorExportFoundryScript::_describe_script_errors(const String &p_path, Error p_fallback_error) {
	Error parser_error = OK;
	Ref<FSParserRef> parser_ref = FSCache::get_parser(p_path, FSParserRef::FULLY_SOLVED, parser_error);
	if (parser_ref.is_valid() && parser_ref->get_parser() != nullptr) {
		const List<FSParser::ParserError> &errors = parser_ref->get_parser()->get_errors();
		if (!errors.is_empty()) {
			const FSParser::ParserError &first_error = errors.front()->get();
			return vformat("%s (line %d)", first_error.message, first_error.line);
		}
	}
	if (p_fallback_error == OK) {
		// The cache reported no error and the parser holds no diagnostics, yet the script is
		// not valid; "OK" would read as nonsense here.
		return TTR("script is not valid");
	}
	return error_names[p_fallback_error];
}

void EditorExportFoundryScript::_check_resource_for_built_in_script(const String &p_path) {
	HashSet<StringName> classes_used;
	ResourceLoader::get_classes_used(p_path, &classes_used);
	if (classes_used.has(SNAME("FoundryScript"))) {
		skip();
		_add_export_error(vformat(TTR("\"%s\" contains a built-in script, which cannot be exported as compiled bytecode. Save the script to its own .fs file."), p_path));
	}
}

bool EditorExportFoundryScript::_is_native_resource_file(const String &p_path) {
	const String extension = p_path.get_extension().to_lower();
	if (extension == "tscn" || extension == "tres") {
		return true;
	}
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null()) {
		return false;
	}
	uint8_t magic[4] = { 0, 0, 0, 0 };
	if (file->get_buffer(magic, 4) != 4) {
		return false;
	}
	if (magic[0] != 'R' || magic[1] != 'S') {
		return false;
	}
	return (magic[2] == 'R' && magic[3] == 'C') || (magic[2] == 'C' && magic[3] == 'C');
}

void EditorExportFoundryScript::_export_file_mangled_bytecode(const String &p_path) {
	const String extension = p_path.get_extension().to_lower();
	if (extension != "fs" && extension != "fsc" && extension != "fsb") {
		return;
	}

	const RBMap<String, FSNameManglerExport::PreparedScript>::Element *entry = mangled_scripts.find(p_path);
	if (!name_mangling_prepared || entry == nullptr) {
		skip();
		_add_export_error(vformat(TTR("Script \"%s\" has no prepared cache entry for name-mangled export."), p_path));
		return;
	}

	const FSNameManglerExport::PreparedScript &prepared = entry->value();
	if (prepared.source_path != p_path || prepared.output_path.is_empty() || prepared.bytes.is_empty()) {
		skip();
		_add_export_error(vformat(TTR("Script \"%s\" has an invalid prepared cache entry for name-mangled export."), p_path));
		return;
	}

	add_file(prepared.output_path, prepared.bytes, prepared.remap);
	if (!prepared.remap) {
		skip();
	}
}

void EditorExportFoundryScript::_export_file_compiled_bytecode(const String &p_path) {
	if (name_mangling_enabled) {
		_export_file_mangled_bytecode(p_path);
		return;
	}

	const String extension = p_path.get_extension();
	if (extension != "fs") {
		if (_is_native_resource_file(p_path)) {
			_check_resource_for_built_in_script(p_path);
		}
		return;
	}

	// The export runs in the editor process where project settings and autoloads are live, so
	// the cache can compile the script exactly as the runtime would. Updating from disk forces
	// a fresh compile under the current call-stack-tracking flag instead of reusing bytecode
	// the editor session compiled earlier. Export-only compiler flags are scoped to this file
	// so @tool scripts that tick during the export never execute placeholder-global bytecode.
	FSExportCompilationScope export_scope(!export_debug);
	if (!export_scope.is_valid()) {
		skip();
		_add_export_error(TTR("Another Foundry Script export compilation is already active."));
		return;
	}
	Error error = OK;
	Ref<FoundryScript> script = FSCache::get_full_script(p_path, error, String(), true);
	if (error != OK || script.is_null() || !script->is_valid()) {
		skip();
		_add_export_error(vformat(TTR("Script \"%s\" failed to compile: %s"), p_path, _describe_script_errors(p_path, error)));
		return;
	}

	const Vector<StringName> unsupported_named_globals = FSBytecodeExporter::collect_unsupported_named_globals(script);
	if (!unsupported_named_globals.is_empty()) {
		Vector<String> printable_names;
		Vector<String> autoload_names;
		for (const StringName &name : unsupported_named_globals) {
			printable_names.push_back(String(name));
			if (ProjectSettings::get_singleton()->has_autoload(name)) {
				autoload_names.push_back(String(name));
			}
		}
		skip();
		String message = vformat(TTR("Script \"%s\" references named globals that exist only in this editor session and are not defined by an exported game's runtime: %s."), p_path, String(", ").join(printable_names));
		if (!autoload_names.is_empty()) {
			// Autoload singletons compile as runtime globals under the export-compile flag,
			// so landing here means that translation did not happen.
			message += " " + vformat(TTR("%s matches a project autoload and should have compiled as a runtime global; this is a bug in the compiled-bytecode export."), String(", ").join(autoload_names));
		}
		_add_export_error(message);
		return;
	}

	// The @static_unload flag lives on the parse tree and is not recoverable from the
	// compiled script, so it is fetched from the cached parser.
	bool annotated_static_unload = false;
	Error parser_error = OK;
	Ref<FSParserRef> parser_ref = FSCache::get_parser(p_path, FSParserRef::PARSED, parser_error);
	if (parser_error == OK && parser_ref.is_valid() && parser_ref->get_parser() != nullptr && parser_ref->get_parser()->get_tree() != nullptr) {
		annotated_static_unload = parser_ref->get_parser()->get_tree()->annotated_static_unload;
	}

	Vector<uint8_t> buffer;
	FSBytecodeExporter exporter;
	error = exporter.serialize(script, buffer, annotated_static_unload);
	if (error != OK || buffer.is_empty()) {
		skip();
		_add_export_error(vformat(TTR("Script \"%s\" could not be serialized to compiled bytecode: %s."), p_path, error_names[error]));
		return;
	}

	add_file(p_path.get_basename() + ".fsb", buffer, true);
}

Error EditorExportFoundryScript::_prepare_export_file_manifest(const ExportFileManifest &p_manifest, String &r_error) {
	r_error.clear();
	if (!name_mangling_enabled) {
		return OK;
	}
	if (name_mangling_prepared) {
		return OK;
	}
	if (script_mode != EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE) {
		_clear_name_mangling_state();
		r_error = TTR("Foundry Script name mangling requires the Compiled bytecode script export mode.");
		return ERR_INVALID_PARAMETER;
	}

	for (const String &path : p_manifest.generated_paths) {
		if (FSNameManglerExport::is_sensitive_generated_path(path)) {
			_clear_name_mangling_state();
			r_error = vformat(TTR("Generated file \"%s\" cannot be included after Foundry Script name mangling begins."), path);
			return ERR_INVALID_DATA;
		}
	}

	FSNameManglerExport::Input input;
	input.manifest_paths = p_manifest.source_paths;
	input.release_profile = !export_debug;
	const Ref<EditorExportPreset> &preset = get_export_preset();
	if (preset.is_valid()) {
		input.keep_rules_path = preset->get_script_name_mangling_keep_rules();
	}

	const FSNameManglerExport::Result result = FSNameManglerExport::prepare(input);
	for (const String &message : result.keep_log) {
		_add_export_info(message);
	}
	for (const FSNameManglerExport::Diagnostic &diagnostic : result.diagnostics) {
		_add_export_error(diagnostic.format());
	}
	if (result.error != OK || !result.diagnostics.is_empty()) {
		_clear_name_mangling_state();
		if (!result.diagnostics.is_empty()) {
			r_error = result.diagnostics[0].format();
		} else {
			r_error = vformat(TTR("Foundry Script name mangling preparation failed: %s."), error_names[result.error]);
		}
		return result.error != OK ? result.error : ERR_INVALID_DATA;
	}

	mangled_scripts = result.scripts;
	name_mangling_prepared = true;
	return OK;
}

Error EditorExportFoundryScript::_validate_late_export_file(const String &p_path, String &r_error) const {
	r_error.clear();
	if (!name_mangling_enabled || !name_mangling_prepared || !FSNameManglerExport::is_sensitive_generated_path(p_path)) {
		return OK;
	}
	r_error = vformat(TTR("Generated file \"%s\" cannot be added after the Foundry Script name-mangling manifest is sealed."), p_path);
	return ERR_INVALID_DATA;
}

void EditorExportFoundryScript::_export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) {
	_clear_name_mangling_state();
	name_mangling_enabled = false;
	export_debug = p_debug;
	script_mode = DEFAULT_SCRIPT_MODE;

	const Ref<EditorExportPreset> &preset = get_export_preset();
	if (preset.is_valid()) {
		script_mode = preset->get_script_export_mode();
		name_mangling_enabled = preset->is_script_name_mangling_enabled();
	}
}

void EditorExportFoundryScript::_export_end() {
	_clear_name_mangling_state();
	name_mangling_enabled = false;
	script_mode = DEFAULT_SCRIPT_MODE;
}

void EditorExportFoundryScript::_export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) {
	if (script_mode == EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE) {
		_export_file_compiled_bytecode(p_path);
		return;
	}

	if (p_path.get_extension() != "fs" || script_mode == EditorExportPreset::MODE_SCRIPT_TEXT) {
		return;
	}

	Vector<uint8_t> file = FileAccess::get_file_as_bytes(p_path);
	if (file.is_empty()) {
		return;
	}

	String source = String::utf8(reinterpret_cast<const char *>(file.ptr()), file.size());
	FSTokenizerBuffer::CompressMode compress_mode = script_mode == EditorExportPreset::MODE_SCRIPT_BINARY_TOKENS_COMPRESSED ? FSTokenizerBuffer::COMPRESS_ZSTD : FSTokenizerBuffer::COMPRESS_NONE;
	file = FSTokenizerBuffer::parse_code_string(source, compress_mode);
	if (file.is_empty()) {
		return;
	}

	add_file(p_path.get_basename() + ".fsc", file, true);
}
