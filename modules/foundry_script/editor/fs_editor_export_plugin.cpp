/**************************************************************************/
/*  fs_editor_export_plugin.cpp                                           */
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

#include "fs_editor_export_plugin.h"

#include "fs_export_compilation_scope.h"

#include "../foundry_script.h"
#include "../fs_builtin_sources.h"
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

void EditorExportFoundryScript::_add_export_warning(const String &p_message) {
	Ref<EditorExportPlatform> platform = get_export_platform();
	if (platform.is_valid()) {
		platform->add_message(EditorExportPlatform::EXPORT_MESSAGE_WARNING, TTR("Compiled Script Export"), p_message);
	} else {
		WARN_PRINT(p_message);
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
	pending_mangled_output_authorizations.clear();
}

void EditorExportFoundryScript::_clear_builtin_bytecode_state() {
	builtin_bytecode_prepared = false;
	published_builtin_outputs.clear();
	pending_builtin_output_authorizations.clear();
}

Error EditorExportFoundryScript::_compile_script_to_bytecode(const String &p_path, Vector<uint8_t> &r_buffer, String &r_error) {
	r_error.clear();
	Error error = OK;
	Ref<FoundryScript> script = FSCache::get_full_script(p_path, error, String(), true);
	if (error != OK || script.is_null() || !script->is_valid()) {
		r_error = vformat(TTR("Script \"%s\" failed to compile: %s"), p_path, _describe_script_errors(p_path, error));
		return error != OK ? error : ERR_INVALID_DATA;
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
		r_error = vformat(TTR("Script \"%s\" references named globals that exist only in this editor session and are not defined by an exported game's runtime: %s."), p_path, String(", ").join(printable_names));
		if (!autoload_names.is_empty()) {
			// Autoload singletons compile as runtime globals under the export-compile flag,
			// so landing here means that translation did not happen.
			r_error += " " + vformat(TTR("%s matches a project autoload and should have compiled as a runtime global; this is a bug in the compiled-bytecode export."), String(", ").join(autoload_names));
		}
		return ERR_INVALID_DATA;
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
		r_error = vformat(TTR("Script \"%s\" could not be serialized to compiled bytecode: %s."), p_path,
				error == OK ? TTR("serializer produced no data") : String(error_names[error]));
		return error != OK ? error : ERR_INVALID_DATA;
	}

	r_buffer = buffer;
	return OK;
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

bool EditorExportFoundryScript::_validate_native_resource_for_compiled_bytecode(const String &p_path) {
	if (!_is_native_resource_file(p_path)) {
		return true;
	}

	HashSet<StringName> classes_used;
	ResourceLoader::get_classes_used(p_path, &classes_used);
	if (classes_used.has(SNAME("FoundryScript"))) {
		skip();
		_add_export_error(vformat(TTR("\"%s\" contains a built-in script, which cannot be exported as compiled bytecode. Save the script to its own .fs file."), p_path));
		return false;
	}
	return true;
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

	if (pending_mangled_output_authorizations.has(prepared.output_path)) {
		skip();
		_add_export_error(vformat(TTR("Script \"%s\" still has an unconsumed late-file authorization for \"%s\"."), p_path, prepared.output_path));
		return;
	}
	pending_mangled_output_authorizations.insert(prepared.output_path);
	add_file(prepared.output_path, prepared.bytes, prepared.remap);
	if (!prepared.remap) {
		skip();
	}
}

void EditorExportFoundryScript::_export_file_compiled_bytecode(const String &p_path) {
	if (!_validate_native_resource_for_compiled_bytecode(p_path)) {
		return;
	}

	if (name_mangling_enabled) {
		_export_file_mangled_bytecode(p_path);
		return;
	}

	const String extension = p_path.get_extension();
	if (extension != "fs") {
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
	Vector<uint8_t> buffer;
	String compile_error;
	if (_compile_script_to_bytecode(p_path, buffer, compile_error) != OK) {
		skip();
		_add_export_error(compile_error);
		return;
	}

	add_file(p_path.get_basename() + ".fsb", buffer, true);
}

Error EditorExportFoundryScript::_prepare_export_file_manifest(const ExportFileManifest &p_manifest, String &r_error) {
	r_error.clear();
	const Error mangling_error = _prepare_name_mangling(p_manifest, r_error);
	if (mangling_error != OK) {
		return mangling_error;
	}
	const Error builtin_error = _prepare_builtin_bytecode(p_manifest, r_error);
	if (builtin_error != OK) {
		// The mangling manifest is only meaningful alongside a complete builtin artifact set, and
		// leaving it prepared would let a later callback publish mangled scripts for an export that
		// is already failing.
		_clear_name_mangling_state();
		return builtin_error;
	}
	return OK;
}

Error EditorExportFoundryScript::_prepare_builtin_bytecode(const ExportFileManifest &p_manifest, String &r_error) {
	if (script_mode != EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE || builtin_bytecode_prepared) {
		return OK;
	}

	List<String> builtin_paths;
	FSBuiltinSources::get_registered_paths(&builtin_paths);
	if (builtin_paths.is_empty()) {
		builtin_bytecode_prepared = true;
		return OK;
	}

	// A stripped template can neither parse nor find the embedded builtin source, so every
	// registered builtin ships as a private compiled companion. Inclusion is unconditional: the
	// complete set is tiny, and it keeps inter-builtin dependencies satisfied without reachability
	// analysis.
	HashSet<String> reserved_paths;
	for (const String &path : p_manifest.source_paths) {
		reserved_paths.insert(path.simplify_path());
		// A project script reaches the pack as its compiled output, not as its source, so the
		// derived path is what a builtin artifact can actually collide with. Without this, a
		// project file placed next to the builtin artifacts would pass the check here and then
		// publish a second record for a path a builtin already owns.
		const String extension = path.get_extension().to_lower();
		if (extension == "fs" || extension == "fsc" || extension == "fsb") {
			reserved_paths.insert((path.get_basename() + ".fsb").simplify_path());
		}
	}
	for (const String &path : p_manifest.generated_paths) {
		reserved_paths.insert(path.simplify_path());
	}

	Vector<StagedBuiltinBytecode> staged;
	{
		FSExportCompilationScope export_scope(!export_debug);
		if (!export_scope.is_valid()) {
			r_error = TTR("Another Foundry Script export compilation is already active.");
			_add_export_error(r_error);
			return ERR_BUSY;
		}

		for (const String &builtin_path : builtin_paths) {
			StagedBuiltinBytecode entry;
			entry.builtin_path = builtin_path;
			entry.output_path = FSBuiltinSources::get_exported_bytecode_path(builtin_path);
			if (entry.output_path.is_empty()) {
				r_error = vformat(TTR("Builtin script \"%s\" has no private compiled-bytecode path."), builtin_path);
				_add_export_error(r_error);
				return ERR_INVALID_PARAMETER;
			}
			if (reserved_paths.has(entry.output_path.simplify_path())) {
				r_error = vformat(TTR("Private builtin bytecode path \"%s\" collides with a file already included in this export."), entry.output_path);
				_add_export_error(r_error);
				return ERR_ALREADY_EXISTS;
			}
			reserved_paths.insert(entry.output_path.simplify_path());

			if (_compile_script_to_bytecode(builtin_path, entry.bytes, r_error) != OK) {
				_add_export_error(r_error);
				return ERR_INVALID_DATA;
			}
			staged.push_back(entry);
		}
	}

	// Publication happens only once every builtin has compiled, so a failure above leaves nothing
	// queued for the pack.
	for (const StagedBuiltinBytecode &entry : staged) {
		// These are deterministic engine-owned outputs, not project files, so each one is
		// explicitly authorized once against the sealed generated-file policy.
		const String normalized_output = entry.output_path.simplify_path();
		published_builtin_outputs.insert(normalized_output);
		pending_builtin_output_authorizations.insert(normalized_output);
		add_file(entry.output_path, entry.bytes, false);
	}
	builtin_bytecode_prepared = true;
	return OK;
}

Error EditorExportFoundryScript::_prepare_name_mangling(const ExportFileManifest &p_manifest, String &r_error) {
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
	String first_error_diagnostic;
	for (const FSNameManglerExport::Diagnostic &diagnostic : result.diagnostics) {
		if (diagnostic.severity == FSNameManglerExport::DIAGNOSTIC_WARNING) {
			_add_export_warning(diagnostic.format());
		} else {
			if (first_error_diagnostic.is_empty()) {
				first_error_diagnostic = diagnostic.format();
			}
			_add_export_error(diagnostic.format());
		}
	}
	if (result.error != OK || !first_error_diagnostic.is_empty()) {
		_clear_name_mangling_state();
		if (!first_error_diagnostic.is_empty()) {
			r_error = first_error_diagnostic;
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
	const String normalized_path = p_path.simplify_path();
	if (published_builtin_outputs.has(normalized_path)) {
		// This exporter's own publication consumes the artifact's single authorization. Any other
		// plugin adding a file here — whether name mangling is on or not — would overwrite the
		// engine-owned bytecode a stripped runtime loads the builtin from.
		if (pending_builtin_output_authorizations.erase(normalized_path)) {
			return OK;
		}
		r_error = vformat(TTR("Generated file \"%s\" collides with the packaged builtin Foundry Script bytecode for this export."), p_path);
		return ERR_INVALID_DATA;
	}
	if (!name_mangling_enabled || !name_mangling_prepared) {
		return OK;
	}
	if (pending_mangled_output_authorizations.erase(p_path)) {
		return OK;
	}
	if (!FSNameManglerExport::is_sensitive_generated_path(p_path)) {
		return OK;
	}
	r_error = vformat(TTR("Generated file \"%s\" cannot be added after the Foundry Script name-mangling manifest is sealed."), p_path);
	return ERR_INVALID_DATA;
}

void EditorExportFoundryScript::_export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) {
	_clear_name_mangling_state();
	_clear_builtin_bytecode_state();
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
	_clear_builtin_bytecode_state();
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
