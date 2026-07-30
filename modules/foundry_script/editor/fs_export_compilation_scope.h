/**************************************************************************/
/*  fs_export_compilation_scope.h                                         */
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

#ifdef TOOLS_ENABLED

// Compiles scripts with export-only bytecode settings, then restores every script touched by the
// cache to ordinary editor bytecode. FSCache supports one reload-recording window, so this scope
// deliberately rejects nesting and cannot be copied or moved.
class FSExportCompilationScope {
	static bool active;

	bool owns_scope = false;
	bool compiling_for_export_previous = false;
	bool call_stack_tracking_overridden = false;
	bool call_stack_tracking_previous = false;

public:
	explicit FSExportCompilationScope(bool p_release_profile);
	~FSExportCompilationScope();

	FSExportCompilationScope(const FSExportCompilationScope &) = delete;
	FSExportCompilationScope &operator=(const FSExportCompilationScope &) = delete;

	bool is_valid() const { return owns_scope; }
};

#endif // TOOLS_ENABLED
