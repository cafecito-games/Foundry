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
	dirty = true;
}

Error TextDocument::load() {
	ERR_FAIL_COND_V(path.is_empty(), ERR_INVALID_PARAMETER);

	const String local_path = ProjectSettings::get_singleton()->localize_path(path);
	const String remapped_path = ResourceLoader::path_remap(local_path);

	Ref<TextFile> text_file;
	text_file.instantiate();
	const Error err = text_file->load_text(remapped_path);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Cannot load text file '" + remapped_path + "'.");

	text = text_file->get_text();
	dirty = false;
	return OK;
}

Error TextDocument::save() {
	ERR_FAIL_COND_V(path.is_empty(), ERR_INVALID_PARAMETER);

	Error err = OK;
	{
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE, &err);
		ERR_FAIL_COND_V_MSG(err != OK, err, "Cannot save text file '" + path + "'.");
		file->store_string(text);
		if (file->get_error() != OK && file->get_error() != ERR_FILE_EOF) {
			return ERR_CANT_CREATE;
		}
	}

	dirty = false;
	if (EditorFileSystem *fs = EditorFileSystem::get_singleton()) {
		fs->update_file(path);
	}
	return OK;
}
