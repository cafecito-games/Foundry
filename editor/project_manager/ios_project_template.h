/**************************************************************************/
/*  ios_project_template.h                                                */
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

#include "editor/run/run_target.h"

#include "core/error/error_list.h"
#include "core/string/ustring.h"

// Seeds a freshly created project for the "Mobile (iOS)" template so the iOS run
// target is half-configured before the editor first opens: an `iOS` export
// preset plus a `run_targets.cfg` with one default target. Deliberately minimal:
// no signing attempts and no device detection (no device is plugged in yet).
namespace IOSProjectTemplate {

// The export preset name the template creates and the default target links to.
inline constexpr const char *PRESET_NAME = "iOS";

// Derives a placeholder reverse-DNS bundle identifier from a project name, e.g.
// "My Game!" -> "com.example.mygame". Non-alphanumeric and non-ASCII characters
// are stripped after lowercasing. When the name has no usable ASCII alphanumeric
// characters, the slug falls back to "game".
String derive_bundle_identifier(const String &p_project_name);

// Builds the default iOS run target ("iOS Device") for a new project: automatic
// signing, "auto" device, and an empty team id (filled on first run).
RunTarget make_default_target();

// Writes `export_presets.cfg` (an `iOS` preset with the derived bundle id) and
// `run_targets.cfg` (the default target, plus a one-shot marker asking the editor
// to reveal the Targets dock on first open) into `p_project_path`. Returns OK on
// success, or the first underlying write error otherwise.
Error seed(const String &p_project_path, const String &p_project_name);

} // namespace IOSProjectTemplate
