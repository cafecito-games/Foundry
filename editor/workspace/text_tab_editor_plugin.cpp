/**************************************************************************/
/*  text_tab_editor_plugin.cpp                                            */
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

#include "text_tab_editor_plugin.h"

#include "editor/workspace/text_tab.h"
#include "editor/workspace/workspace_pane.h"
#include "editor/workspace/workspace_tab_registry.h"

TextTabType *TextTabEditorPlugin::_text_tab_type() const {
	return static_cast<TextTabType *>(WorkspacePane::get_shared_tab_registry().find_type(StringName("text")));
}

String TextTabEditorPlugin::get_unsaved_status(const String &p_for_scene) const {
	// Text documents are standalone files, not scene-scoped built-ins, so they
	// only participate in the global (no specific scene) unsaved prompt.
	if (!p_for_scene.is_empty()) {
		return String();
	}
	TextTabType *type = _text_tab_type();
	if (!type) {
		return String();
	}
	const PackedStringArray unsaved = type->get_unsaved_document_paths();
	if (unsaved.is_empty()) {
		return String();
	}

	PackedStringArray message;
	message.push_back(TTR("Save changes to the following text file(s) before quitting?"));
	for (const String &path : unsaved) {
		message.push_back(path);
	}
	return String("\n").join(message);
}

void TextTabEditorPlugin::save_external_data() {
	if (TextTabType *type = _text_tab_type()) {
		type->save_all_documents();
	}
}
