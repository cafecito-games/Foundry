/**************************************************************************/
/*  cli_user_root.h                                                       */
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

#include "core/string/ustring.h"
#include "core/templates/vector.h"

// `user://` root policy for the test CLI verbs that own their root. Those verbs recreate the
// root clean at startup, so each invocation is given a per-process root: a shared one would
// erase the `user://` tree of a run started next to it.
class FoundryCLIUserRoot {
public:
	// Whether a requested artifact path resolves to an existing file or directory inside a
	// per-process `user://` root. A `user://` output path lands in the root the run owns, so the
	// caller has to keep the root when it holds the artifact the run was asked to produce. A
	// directory counts because a run may publish a tree of outputs rather than one file. A path
	// that was never created keeps nothing: the run produced no artifact worth preserving.
	static bool root_holds_artifact(const String &p_artifact_path, const String &p_user_root);

	// Removes a finished run's per-process `user://` root. The root is nobody else's to reuse,
	// so leaving it behind would accumulate one directory per invocation; it is kept when it
	// holds one of `p_artifact_paths`.
	static void remove_owned_root(const String &p_user_root, const Vector<String> &p_artifact_paths);
};
