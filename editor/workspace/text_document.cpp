/**************************************************************************/
/*  text_document.cpp                                                     */
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

#include "text_document.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "editor/file_system/editor_file_system.h"
#include "scene/resources/text_file.h"

void TextDocument::set_text(const String &p_text) {
	if (text == p_text) {
		return;
	}
	text = p_text;
	// Dirty tracks divergence from the last saved/loaded content, so editing and
	// then undoing back to the saved text correctly clears the unsaved state.
	dirty = text != saved_text;
}

Error TextDocument::load() {
	ERR_FAIL_COND_V(path.is_empty(), ERR_INVALID_PARAMETER);

	const String local_path = ProjectSettings::get_singleton()->localize_path(path);
	const String remapped_path = ResourceLoader::path_remap(local_path);

	Ref<TextFile> text_file;
	text_file.instantiate();
	const Error err = text_file->load_text(remapped_path);
	if (err != OK) {
		// Leave the previous content untouched and flag the failure so save() will
		// not overwrite the (unreadable but present) file with a truncated buffer.
		load_failed = true;
		ERR_FAIL_V_MSG(err, "Cannot load text file '" + remapped_path + "'.");
	}

	text = text_file->get_text();
	saved_text = text;
	dirty = false;
	load_failed = false;
	// Record the on-disk timestamp so a later external change is detectable.
	last_modified_time = FileAccess::get_modified_time(path);
	has_modified_time_baseline = true;
	return OK;
}

bool TextDocument::has_external_modification() const {
	// Without a baseline (never successfully loaded) there is nothing to compare
	// against; a deleted file is handled by tab availability, not the reload flow.
	if (path.is_empty() || !has_modified_time_baseline) {
		return false;
	}
	if (!FileAccess::exists(path)) {
		return false;
	}
	// Seconds-resolution timestamps mean any different value is an external write;
	// mirror ScriptEditorView's inequality check rather than a strictly-newer one.
	return FileAccess::get_modified_time(path) != last_modified_time;
}

Error TextDocument::reload() {
	const Error err = load();
	if (err != OK) {
		// load() kept the previous good buffer but flagged load_failed and left the
		// baseline unchanged, so the tab would be unsavable and would re-prompt every
		// foreground. Since we still hold valid content, clear the failure flag and
		// re-baseline to the current file: the stale-but-good buffer stays editable
		// and savable, and only a subsequent (different) external write re-triggers.
		load_failed = false;
		last_modified_time = FileAccess::get_modified_time(path);
		has_modified_time_baseline = true;
	}
	return err;
}

Error TextDocument::save() {
	ERR_FAIL_COND_V(path.is_empty(), ERR_INVALID_PARAMETER);
	// The backing file exists but could not be read, so this buffer never held its
	// real contents; refuse rather than truncate the file with an empty save.
	ERR_FAIL_COND_V_MSG(load_failed, ERR_FILE_CANT_READ, "Refusing to save text file whose contents failed to load: '" + path + "'.");

	Error err = OK;
	{
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE, &err);
		ERR_FAIL_COND_V_MSG(err != OK, err, "Cannot save text file '" + path + "'.");
		file->store_string(text);
		if (file->get_error() != OK && file->get_error() != ERR_FILE_EOF) {
			return ERR_CANT_CREATE;
		}
	}

	saved_text = text;
	dirty = false;
	if (EditorFileSystem *fs = EditorFileSystem::get_singleton()) {
		fs->update_file(path);
	}
	// Re-baseline the timestamp to this write so our own save is not later
	// misdetected as an external change on the next editor foreground.
	last_modified_time = FileAccess::get_modified_time(path);
	has_modified_time_baseline = true;
	return OK;
}
