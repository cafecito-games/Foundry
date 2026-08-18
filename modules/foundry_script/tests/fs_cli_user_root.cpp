/**************************************************************************/
/*  fs_cli_user_root.cpp                                                */
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

#include "fs_cli_user_root.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"

namespace FSTests {

bool artifact_is_inside_user_root(const String &p_artifact_path, const String &p_user_root) {
	if (p_artifact_path.is_empty() || p_user_root.is_empty()) {
		return false;
	}
	const String root = p_user_root.simplify_path().trim_suffix("/");
	const String artifact = ProjectSettings::get_singleton()->globalize_path(p_artifact_path).simplify_path();
	// Compared with the separator attached so a sibling directory whose name merely starts with
	// the root's name (`/scratch/user-benchmark-9` next to `/scratch/user-benchmark-91`) does
	// not look contained.
	return artifact == root || artifact.begins_with(root + "/");
}

void remove_per_process_user_root(const String &p_user_root, const Vector<String> &p_artifact_paths) {
	if (p_user_root.is_empty() || !DirAccess::exists(p_user_root)) {
		return;
	}
	for (const String &artifact_path : p_artifact_paths) {
		if (artifact_is_inside_user_root(artifact_path, p_user_root)) {
			return;
		}
	}
	Ref<DirAccess> user_dir = DirAccess::open(p_user_root);
	if (user_dir.is_valid() && user_dir->erase_contents_recursive() == OK) {
		DirAccess::remove_absolute(p_user_root);
	}
}

} // namespace FSTests
