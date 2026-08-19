/**************************************************************************/
/*  fs_diagnostic_names.cpp                                               */
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

#include "fs_diagnostic_names.h"

#include "core/config/project_settings.h"

String fs_diagnostic_type_name_for_path(const String &p_fully_qualified_name) {
	if (p_fully_qualified_name.is_empty()) {
		return p_fully_qualified_name;
	}

	// Only the leading segment is a path. Everything from the first `::` onward is declared name —
	// inner classes, traits, and namespace segments alike — and is kept verbatim.
	const int separator = p_fully_qualified_name.find("::");
	if (separator < 0) {
		return p_fully_qualified_name.get_file();
	}
	return p_fully_qualified_name.substr(0, separator).get_file() + p_fully_qualified_name.substr(separator);
}

String fs_diagnostic_file_reference(const String &p_path) {
	if (p_path.is_empty()) {
		return p_path;
	}

	const ProjectSettings *settings = ProjectSettings::get_singleton();
	if (settings == nullptr) {
		return p_path.get_file();
	}

	const String localized = settings->localize_path(p_path);
	if (!localized.begins_with("res://") && !localized.begins_with("user://")) {
		// A file outside every resource root has no location a reader of this project can act on,
		// and its absolute spelling depends on the machine that produced the diagnostic.
		return p_path.get_file();
	}
	return localized;
}
