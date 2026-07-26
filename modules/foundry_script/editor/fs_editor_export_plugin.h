/**************************************************************************/
/*  fs_editor_export_plugin.h                                             */
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

#include "fs_name_mangler_export.h"

#include "editor/export/editor_export_plugin.h"

class EditorExportFoundryScript : public EditorExportPlugin {
	FOUNDRY_CLASS(EditorExportFoundryScript, EditorExportPlugin);

	static constexpr EditorExportPreset::ScriptExportMode DEFAULT_SCRIPT_MODE = EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE;
	EditorExportPreset::ScriptExportMode script_mode = DEFAULT_SCRIPT_MODE;
	bool export_debug = true;
	bool name_mangling_enabled = false;
	bool name_mangling_prepared = false;
	RBMap<String, FSNameManglerExport::PreparedScript> mangled_scripts;
	mutable HashSet<String> pending_mangled_output_authorizations;

	// Export plugin callbacks cannot return an error; an EXPORT_MESSAGE_ERROR on the platform is
	// what fails the export (see EditorExportPlatform::export_project_files).
	void _add_export_info(const String &p_message);
	void _add_export_warning(const String &p_message);
	void _add_export_error(const String &p_message);
	void _clear_name_mangling_state();
	String _describe_script_errors(const String &p_path, Error p_fallback_error);
	bool _is_native_resource_file(const String &p_path);
	bool _validate_native_resource_for_compiled_bytecode(const String &p_path);
	void _export_file_mangled_bytecode(const String &p_path);
	void _export_file_compiled_bytecode(const String &p_path);

protected:
	virtual Error _prepare_export_file_manifest(const ExportFileManifest &p_manifest, String &r_error) override;
	virtual Error _validate_late_export_file(const String &p_path, String &r_error) const override;
	virtual void _export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) override;
	virtual void _export_end() override;
	virtual void _export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) override;

public:
	virtual String get_name() const override { return "FoundryScript"; }
};
