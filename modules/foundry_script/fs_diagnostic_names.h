/**************************************************************************/
/*  fs_diagnostic_names.h                                                 */
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

// How a diagnostic spells a script-declared entity and how it spells a file. The two rules are
// deliberately different and must not be merged: a *type* is something the reader copies back into
// source, a *file* is a location the reader opens.
//
// This header takes only string-level dependencies so runtime translation units can share the rules
// with the front-end instead of re-deriving them; a re-derivation is how the same declaration came
// to be named one way at compile time and another way at run time.

// A script-declared entity named for a diagnostic: never a directory, never a `res://` prefix, never
// an absolute build path. The declaring file's name survives because it is the only distinguishing
// information an entity with no declared name has, and any `::` segments the identity carries are
// preserved because they are the declared part of the name.
//
// `res://a/b/c.fs` -> `c.fs`; `/abs/a/b/c.fs::Inner` -> `c.fs::Inner`; `Marker` -> `Marker`.
String fs_diagnostic_type_name_for_path(const String &p_fully_qualified_name);

// A file named for a diagnostic: the `res://` path when the project root can localize it, otherwise
// the file name alone. A location is genuinely useful and clickable, so it keeps its directories,
// but it never degrades into a build-machine path when there is no project to localize against.
String fs_diagnostic_file_reference(const String &p_path);
