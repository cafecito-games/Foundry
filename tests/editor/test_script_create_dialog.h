/**************************************************************************/
/*  test_script_create_dialog.h                                           */
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

#include "editor/script/script_create_dialog.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/script_language.h"
#include "core/os/os.h"

#include "scene/gui/label.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"

namespace TestScriptCreateDialog {

static ScriptLanguage *find_foundry_script_language() {
	for (int i = 0; i < ScriptServer::get_language_count(); i++) {
		if (String(ScriptServer::get_language(i)->get_name()) == "FoundryScript") {
			return ScriptServer::get_language(i);
		}
	}
	return nullptr;
}

// Collects the text of every Label in the dialog's own content, without descending
// into nested sub-dialogs (the class picker and file browser are separate Windows
// that carry their own "Name:"/"Path:" fields). This lets the dialog's exposed
// fields be asserted without depending on private members.
static void collect_own_label_texts(Node *p_node, Vector<String> &r_texts) {
	for (int i = 0; i < p_node->get_child_count(); i++) {
		Node *child = p_node->get_child(i);
		if (Object::cast_to<Window>(child)) {
			continue;
		}
		Label *label = Object::cast_to<Label>(child);
		if (label) {
			r_texts.push_back(label->get_text());
		}
		collect_own_label_texts(child, r_texts);
	}
}

static void pump() {
	SceneTree::get_singleton()->process(0.016);
	MessageQueue::get_singleton()->flush();
}

TEST_CASE("[Editor][ScriptCreateDialog] FoundryScript disallows built-in/embedded scripts") {
	ScriptLanguage *language = find_foundry_script_language();
	REQUIRE(language != nullptr);
	CHECK_FALSE(language->supports_builtin_mode());
}

TEST_CASE("[Editor][ScriptCreateDialog] Exposes no built-in option and always shows the path field") {
	ScriptCreateDialog *dialog = memnew(ScriptCreateDialog);
	SceneTree::get_singleton()->get_root()->add_child(dialog);
	pump();

	Vector<String> label_texts;
	collect_own_label_texts(dialog, label_texts);

	// The embedded/built-in checkbox and its name field are gone entirely.
	CHECK(label_texts.find("Built-in Script:") == -1);
	CHECK(label_texts.find("Name:") == -1);
	// The path field is always present.
	CHECK(label_texts.find("Path:") != -1);

	SceneTree::get_singleton()->get_root()->remove_child(dialog);
	memdelete(dialog);
}

TEST_CASE("[Editor][ScriptCreateDialog] Creates a file-backed script on disk") {
	REQUIRE(find_foundry_script_language() != nullptr);

	// Write into the shared test scratch space so aborted runs never pollute the
	// repository or the test project. The base name is already snake_case, which
	// is FoundryScript's preferred casing, so the dialog keeps it verbatim.
	String scratch_root = OS::get_singleton()->get_temp_path().path_join("foundry-tests");
	if (OS::get_singleton()->has_environment("FOUNDRY_TEST_SCRATCH")) {
		const String configured = OS::get_singleton()->get_environment("FOUNDRY_TEST_SCRATCH");
		if (!configured.is_empty()) {
			scratch_root = configured;
		}
	}
	scratch_root = scratch_root.simplify_path();

	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(dir->make_dir_recursive(scratch_root) == OK);
	const String script_path = scratch_root.path_join("foundry_script_create_dialog_no_builtin.fs");
	dir->remove(script_path);

	ScriptCreateDialog *dialog = memnew(ScriptCreateDialog);
	SceneTree::get_singleton()->get_root()->add_child(dialog);
	pump();

	dialog->config("Node", script_path);
	pump();

	// Confirm the OK button as a user would; the dialog saves the new script.
	dialog->get_ok_button()->emit_signal(StringName("pressed"));
	pump();

	// The result is a real file on disk, not an embedded resource whose path is a
	// "scene.tscn::id" sub-resource reference.
	CHECK(FileAccess::exists(script_path));
	CHECK_FALSE(script_path.contains("::"));
	CHECK(script_path.get_extension() == "fs");

	Ref<FileAccess> written = FileAccess::open(script_path, FileAccess::READ);
	REQUIRE(written.is_valid());
	CHECK(written->get_length() > 0);
	written.unref();

	dir->remove(script_path);
	SceneTree::get_singleton()->get_root()->remove_child(dialog);
	memdelete(dialog);
}

} // namespace TestScriptCreateDialog
