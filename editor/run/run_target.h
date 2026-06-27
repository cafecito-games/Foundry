/**************************************************************************/
/*  run_target.h                                                          */
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

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

// A small named record describing where the editor can run a project. Stored in
// a `run_targets.cfg` ConfigFile (parallel to `export_presets.cfg`). A target
// references an export preset by name rather than duplicating its build config;
// the export preset stays the source of truth.
struct RunTarget {
	String name;
	String platform;
	String export_preset; // Links to an existing export preset by name.
	String device_id; // Last-selected device, or "auto".
	String signing_mode; // e.g. "automatic" (delegated to Xcode).
	String team_id; // Selected once, remembered.

	// Unknown keys read from a target section, preserved verbatim on save so a
	// newer editor's fields survive a round trip through an older one.
	Dictionary extra_keys;

	// Persists `p_targets` to the ConfigFile at `p_path`, one `[target.N]`
	// section per target. Returns the result of the underlying file write.
	static Error save_all(const String &p_path, const Vector<RunTarget> &p_targets);

	// Loads all `[target.N]` sections from the ConfigFile at `p_path`. A missing
	// or empty file yields an empty list and is not treated as an error. When
	// `r_error` is supplied it receives OK for a missing/empty/valid file, or the
	// parse error for a malformed file.
	static Vector<RunTarget> load_all(const String &p_path, Error *r_error = nullptr);

	bool operator==(const RunTarget &p_other) const {
		return name == p_other.name &&
				platform == p_other.platform &&
				export_preset == p_other.export_preset &&
				device_id == p_other.device_id &&
				signing_mode == p_other.signing_mode &&
				team_id == p_other.team_id &&
				extra_keys == p_other.extra_keys;
	}
	bool operator!=(const RunTarget &p_other) const {
		return !(*this == p_other);
	}
};
