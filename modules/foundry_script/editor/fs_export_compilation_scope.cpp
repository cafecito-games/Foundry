/**************************************************************************/
/*  fs_export_compilation_scope.cpp                                       */
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

#include "fs_export_compilation_scope.h"

#ifdef TOOLS_ENABLED

#include "../foundry_script.h"
#include "../fs_cache.h"

bool FSExportCompilationScope::active = false;

FSExportCompilationScope::FSExportCompilationScope(bool p_release_profile) {
	ERR_FAIL_COND_MSG(active, "Foundry Script export compilation scopes cannot be nested.");

	FSLanguage *language = FSLanguage::get_singleton();
	ERR_FAIL_NULL(language);

	active = true;
	owns_scope = true;
	compiling_for_export_previous = language->is_compiling_for_export();
	language->set_compiling_for_export(true);
	FSCache::begin_script_reload_recording();

	if (p_release_profile) {
		call_stack_tracking_previous = language->should_track_call_stack();
		language->set_track_call_stack(false);
		call_stack_tracking_overridden = true;
	}
}

FSExportCompilationScope::~FSExportCompilationScope() {
	if (!owns_scope) {
		return;
	}

	FSLanguage *language = FSLanguage::get_singleton();
	if (language != nullptr) {
		language->set_compiling_for_export(compiling_for_export_previous);
		if (call_stack_tracking_overridden) {
			language->set_track_call_stack(call_stack_tracking_previous);
		}
	}

	const Vector<String> reloaded_paths = FSCache::end_script_reload_recording();
	for (const String &path : reloaded_paths) {
		Error error = OK;
		FSCache::get_full_script(path, error, String(), true);
		if (error != OK) {
			WARN_PRINT(vformat("Could not recompile \"%s\" for the editor session after compiled-bytecode export compilation: %s.", path, error_names[error]));
		}
	}
	active = false;
}

#endif // TOOLS_ENABLED
