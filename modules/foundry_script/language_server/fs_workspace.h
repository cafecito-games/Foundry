/**************************************************************************/
/*  fs_workspace.h                                                        */
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

#include "core/config/project_build_pipeline_status.h"
#include "core/error/error_macros.h"
#include "foundry_lsp.h"
#include "fs_extend_parser.h"

#include "core/variant/variant.h"
#include "editor/file_system/editor_file_system.h"

class FSParseResultProvider {
public:
	virtual const ExtendFSParser *get_parse_result(const String &p_path) const = 0;
	// Return an already-parsed result for p_path without triggering a parse, or
	// nullptr when nothing is cached. Lets the raw-text pre-filter honor unsaved
	// buffers while avoiding a fresh parse just to inspect a file's contents.
	virtual const ExtendFSParser *peek_parse_result(const String &p_path) const { return nullptr; }
	virtual ~FSParseResultProvider() = default;
};

class FSWorkspace : public RefCounted {
	FOUNDRY_CLASS(FSWorkspace, RefCounted);

private:
	void _get_owners(EditorFileSystemDirectory *efsd, String p_path, List<String> &owners);
	Node *_get_owner_scene_node(String p_path);

protected:
	static void _bind_methods();
	bool initialized = false;
	HashMap<StringName, LSP::DocumentSymbol> native_symbols;

	// Absolute paths that are known to point to res://
	HashSet<String> absolute_res_paths;

	const LSP::DocumentSymbol *get_native_symbol(const String &p_class, const String &p_member = "") const;
	const ExtendFSParser *get_parse_result(const String &p_path, const FSParseResultProvider *p_parse_result_provider) const;
	const ExtendFSParser *peek_parse_result(const String &p_path, const FSParseResultProvider *p_parse_result_provider) const;
	const LSP::DocumentSymbol *get_script_symbol(const String &p_path, const FSParseResultProvider *p_parse_result_provider = nullptr) const;
	const LSP::DocumentSymbol *get_parameter_symbol(const LSP::DocumentSymbol *p_parent, const String &symbol_identifier);
	const LSP::DocumentSymbol *get_local_symbol_at(const ExtendFSParser *p_parser, const String &p_symbol_identifier, const LSP::Position p_position);

	void reload_all_workspace_scripts();

	void list_script_files(const String &p_root_dir, List<String> &r_files);

	void apply_new_signal(Object *obj, String function, PackedStringArray args);
	void _connect_editor_signals();
	ProjectBuildPipelineStatusSnapshot _get_build_pipeline_status_snapshot(bool p_compute_current_fingerprints = true) const;
	ProjectBuildPipelineStatusSnapshot _run_dirty_pre_compile_tasks();
	void _publish_build_pipeline_diagnostics(const ProjectBuildPipelineStatusSnapshot &p_snapshot);
	void _clear_build_pipeline_diagnostics();
	void _publish_diagnostics_array(const String &p_path, const Array &p_errors);
	static bool _output_may_include_foundry_scripts(const String &p_output);
	static bool _output_is_declared_res_root(const String &p_output);

	HashSet<String> build_pipeline_diagnostic_paths;
	bool build_pipeline_pre_compile_run_active = false;
	bool build_pipeline_initialization_ready_for_recovery = false;

#ifdef TESTS_ENABLED
	bool build_pipeline_status_override_enabled = false;
	ProjectBuildPipelineStatusSnapshot build_pipeline_status_override;
#endif

public:
	String root;
	String root_uri;

	HashMap<StringName, ClassMembers> native_members;

public:
	Error initialize();
	bool is_initialized() const { return initialized; }

	String get_file_path(const String &p_uri);
	String get_file_uri(const String &p_path) const;

	void publish_diagnostics(const String &p_path);
	bool refresh_after_successful_build_outputs(const PackedStringArray &p_outputs);
#ifdef TESTS_ENABLED
	void set_build_pipeline_status_override_for_tests(const ProjectBuildPipelineStatusSnapshot &p_snapshot);
	void clear_build_pipeline_status_override_for_tests();
#endif
	void completion(const LSP::CompletionParams &p_params, List<ScriptLanguage::CodeCompletionOption> *r_options);

	const LSP::DocumentSymbol *resolve_symbol(
			const LSP::TextDocumentPositionParams &p_doc_pos,
			const String &p_symbol_name = "",
			bool p_func_required = false,
			const FSParseResultProvider *p_parse_result_provider = nullptr);

	const LSP::DocumentSymbol *resolve_native_symbol(const LSP::NativeSymbolInspectParams &p_params);
	void resolve_document_links(const String &p_uri, List<LSP::DocumentLink> &r_list);
	Dictionary generate_script_api(const String &p_path);
	Error resolve_signature(const LSP::TextDocumentPositionParams &p_doc_pos, LSP::SignatureHelp &r_signature);
	Dictionary rename(const LSP::TextDocumentPositionParams &p_doc_pos, const String &new_name);
	bool can_rename(
			const LSP::TextDocumentPositionParams &p_doc_pos,
			LSP::DocumentSymbol &r_symbol,
			LSP::Range &r_range,
			const FSParseResultProvider *p_parse_result_provider = nullptr);
	void list_project_script_files(List<String> &r_files) { list_script_files("res://", r_files); }
	// Cheap pre-filter for project-wide refactors: a file whose source never
	// mentions p_symbol_name cannot reference it, so it can be skipped before the
	// expensive parse + AST walk. Prefers an already-parsed result (honoring
	// unsaved buffers) and otherwise reads the file from disk once. Returns true
	// conservatively when the contents cannot be determined.
	bool source_may_reference_symbol(
			const String &p_path,
			const String &p_symbol_name,
			const FSParseResultProvider *p_parse_result_provider) const;
	Vector<LSP::Location> find_usages_in_file(
			const LSP::DocumentSymbol &p_symbol,
			const String &p_file_path,
			const FSParseResultProvider *p_parse_result_provider = nullptr);
	Vector<LSP::Location> find_all_usages(const LSP::DocumentSymbol &p_symbol, const FSParseResultProvider *p_parse_result_provider = nullptr);

	FSWorkspace();
	~FSWorkspace();
};
