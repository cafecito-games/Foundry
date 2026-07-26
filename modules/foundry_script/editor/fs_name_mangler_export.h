/**************************************************************************/
/*  fs_name_mangler_export.h                                              */
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

#ifdef TOOLS_ENABLED

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/templates/rb_map.h"
#include "core/templates/vector.h"

class FSNameManglerExport {
public:
	struct Input {
		Vector<String> manifest_paths;
		String keep_rules_path;
		bool release_profile = false;
	};

	struct PreparedScript {
		String source_path;
		String output_path;
		Vector<uint8_t> bytes;
		bool remap = false;
	};

	struct Diagnostic {
		String stage;
		String source;
		String message;

		String format() const;
	};

	struct Result {
		Error error = OK;
		RBMap<String, PreparedScript> scripts;
		Vector<String> keep_log;
		Vector<Diagnostic> diagnostics;
	};

	static Result prepare(const Input &p_input);
	static bool is_sensitive_generated_path(const String &p_path);
};

#endif // TOOLS_ENABLED
