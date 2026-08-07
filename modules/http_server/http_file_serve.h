/**************************************************************************/
/*  http_file_serve.h                                                     */
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

#include "http_response.h"
#include "http_server_request.h"

// Answers a request from a directory on disk. This is where a mounted URL prefix turns into a file
// path, and it is the only place that decides whether a request is allowed to reach a given file.
//
// The security invariant is that a served path is always inside the mount root: the requested path
// is canonicalized — percent-escapes are already decoded by the request, `.` and `..` are collapsed,
// and every symbolic link is followed — before anything is opened, and a path that resolves outside
// the root is refused without ever being opened. A refusal says nothing about what exists outside
// the root, so probing through a mount cannot map the rest of the filesystem.
namespace HTTPFileServe {

enum Result {
	// The request was answered; the response carries the status and the committed body.
	RESULT_RESOLVED,
	// The mount holds nothing for this path, so the caller keeps walking its resolution ladder.
	// A refusal is never reported this way: an escaping path is answered here and nowhere else.
	RESULT_NOT_FOUND,
};

// Resolves a mount root to an absolute path with every symbolic link followed, which is the form
// `serve()` compares a request against. Returns an empty string when the path does not name a
// directory that can be read. `res://` and `user://` roots are resolved to their location on disk.
String canonicalize_directory(const String &p_directory);

// Answers `p_relative_path` (which always starts with `/`, and is the request path with the mount
// prefix removed) out of `p_canonical_root`, which must come from `canonicalize_directory()`.
Result serve(const String &p_canonical_root, const String &p_relative_path,
		const Ref<HTTPServerRequest> &p_request, const Ref<HTTPResponse> &p_response);

} // namespace HTTPFileServe
