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

#include "../foundry_script.h"
#include "../fs_bytecode_export.h"
#include "../fs_cache.h"
#include "../fs_parser.h"
#include "../fs_tokenizer_buffer.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "editor/export/editor_export.h"

EditorExportFoundryScript::CompiledBytecodeExportScope::CompiledBytecodeExportScope(bool p_release_profile) {
	FSLanguage::get_singleton()->set_compiling_for_export(true);
	FSCache::begin_script_reload_recording();
	if (p_release_profile) {
		call_stack_tracking_previous = FSLanguage::get_singleton()->should_track_call_stack();
		FSLanguage::get_singleton()->set_track_call_stack(false);
		call_stack_tracking_overridden = true;
	}
}

EditorExportFoundryScript::CompiledBytecodeExportScope::~CompiledBytecodeExportScope() {
	FSLanguage::get_singleton()->set_compiling_for_export(false);
	if (call_stack_tracking_overridden) {
		FSLanguage::get_singleton()->set_track_call_stack(call_stack_tracking_previous);
	}
	for (const String &path : FSCache::end_script_reload_recording()) {
		Error error = OK;
		FSCache::get_full_script(path, error, String(), true);
		if (error != OK) {
			WARN_PRINT(vformat("Could not recompile \"%s\" for the editor session after the compiled-bytecode export: %s.", path, error_names[error]));
		}
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

void EditorExportFoundryScript::_export_file_compiled_bytecode(const String &p_path) {
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
	CompiledBytecodeExportScope export_scope(!export_debug);
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

void EditorExportFoundryScript::_export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) {
	export_debug = p_debug;
	script_mode = DEFAULT_SCRIPT_MODE;

	const Ref<EditorExportPreset> &preset = get_export_preset();
	if (preset.is_valid()) {
		script_mode = preset->get_script_export_mode();
	}
}

void EditorExportFoundryScript::_export_end() {
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
