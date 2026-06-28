/**************************************************************************/
/*  test_script_refactor_apply.h                                          */
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

#ifdef TOOLS_ENABLED

#include "editor/script/script_refactor_apply.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "modules/foundry_script/editor/fs_refactoring.h"
#include "scene/gui/code_edit.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestScriptRefactorApply {

RefactorTextEdit make_edit(int p_line, int p_start_column, int p_end_column, const String &p_expected_text, const String &p_new_text) {
	RefactorTextEdit edit;
	edit.start_line = p_line;
	edit.start_column = p_start_column;
	edit.end_line = p_line;
	edit.end_column = p_end_column;
	edit.has_expected_text = true;
	edit.expected_text = p_expected_text;
	edit.new_text = p_new_text;
	return edit;
}

ScriptRefactorSource make_source(const String &p_path, const String &p_source) {
	ScriptRefactorSource source;
	source.path = p_path;
	source.source = p_source;
	return source;
}

String make_temp_dir(const String &p_prefix) {
	const String dir = TestUtils::get_temp_path(p_prefix + "_" + itos(OS::get_singleton()->get_ticks_usec()));
	REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(dir), OK);
	return dir;
}

void write_text_for_test(const String &p_path, const String &p_source) {
	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
	REQUIRE_EQ(err, OK);
	REQUIRE(file.is_valid());
	CHECK(file->store_string(p_source));
	file->close();
}

TEST_CASE("[Editor][ScriptRefactorApply] Builds grouped before and after plan") {
	RefactorFileEdit player_edits;
	player_edits.path = "res://player.fs";
	player_edits.edits.push_back(make_edit(0, 4, 9, "speed", "move_speed"));
	player_edits.edits.push_back(make_edit(1, 6, 11, "speed", "move_speed"));

	RefactorFileEdit enemy_edits;
	enemy_edits.path = "res://enemy.fs";
	enemy_edits.edits.push_back(make_edit(0, 7, 12, "speed", "move_speed"));

	Vector<RefactorFileEdit> edits;
	edits.push_back(player_edits);
	edits.push_back(enemy_edits);

	Vector<ScriptRefactorSource> sources;
	sources.push_back(make_source("res://player.fs", "var speed := 10\nprint(speed)\n"));
	sources.push_back(make_source("res://enemy.fs", "target.speed += 1\n"));

	ScriptRefactorApplyPlan plan;
	String error;
	REQUIRE(ScriptRefactorApply::build_plan(edits, sources, plan, error));
	CHECK(error.is_empty());
	REQUIRE_EQ(plan.files.size(), 2);

	CHECK_EQ(plan.files[0].path, "res://player.fs");
	CHECK_EQ(plan.files[0].before_source, "var speed := 10\nprint(speed)\n");
	CHECK_EQ(plan.files[0].after_source, "var move_speed := 10\nprint(move_speed)\n");
	CHECK_EQ(plan.files[0].edit_count, 2);

	CHECK_EQ(plan.files[1].path, "res://enemy.fs");
	CHECK_EQ(plan.files[1].before_source, "target.speed += 1\n");
	CHECK_EQ(plan.files[1].after_source, "target.move_speed += 1\n");
	CHECK_EQ(plan.files[1].edit_count, 1);

	// The individual edits are retained so the preview can re-apply a subset.
	REQUIRE_EQ(plan.files[0].edits.size(), 2);
	CHECK_EQ(plan.files[0].edits[0].start_line, 0);
	CHECK_EQ(plan.files[0].edits[0].new_text, "move_speed");
	REQUIRE_EQ(plan.files[1].edits.size(), 1);
	CHECK_EQ(plan.files[1].edits[0].new_text, "move_speed");
}

TEST_CASE("[Editor][ScriptRefactorApply] Missing source fails before producing a partial plan") {
	RefactorFileEdit player_edits;
	player_edits.path = "res://player.fs";
	player_edits.edits.push_back(make_edit(0, 4, 9, "speed", "move_speed"));

	RefactorFileEdit missing_edits;
	missing_edits.path = "res://missing.fs";
	missing_edits.edits.push_back(make_edit(0, 4, 9, "speed", "move_speed"));

	Vector<RefactorFileEdit> edits;
	edits.push_back(player_edits);
	edits.push_back(missing_edits);

	Vector<ScriptRefactorSource> sources;
	sources.push_back(make_source("res://player.fs", "var speed := 10\n"));

	ScriptRefactorApplyPlan plan;
	String error;
	CHECK_FALSE(ScriptRefactorApply::build_plan(edits, sources, plan, error));
	CHECK(error.contains("res://missing.fs"));
	CHECK(plan.files.is_empty());
}

TEST_CASE("[Editor][ScriptRefactorApply] Stale source fails before producing a partial plan") {
	RefactorFileEdit player_edits;
	player_edits.path = "res://player.fs";
	player_edits.edits.push_back(make_edit(0, 4, 9, "speed", "move_speed"));

	Vector<RefactorFileEdit> edits;
	edits.push_back(player_edits);

	Vector<ScriptRefactorSource> sources;
	sources.push_back(make_source("res://player.fs", "var sprint_speed := 10\n"));

	ScriptRefactorApplyPlan plan;
	String error;
	CHECK_FALSE(ScriptRefactorApply::build_plan(edits, sources, plan, error));
	CHECK(error.to_lower().contains("changed"));
	CHECK(plan.files.is_empty());
}

TEST_CASE("[Editor][ScriptRefactorApply] Editor text replacement does not enter the local undo stack") {
	CodeEdit *text_editor = memnew(CodeEdit);

	text_editor->set_text("var speed := 10\n");
	text_editor->clear_undo_history();
	text_editor->tag_saved_version();
	text_editor->insert_text_at_caret("# local draft\n");
	REQUIRE(text_editor->has_undo());

	ScriptRefactorApply::replace_editor_text(text_editor, "var move_speed := 10\n", false);
	CHECK_EQ(text_editor->get_text(), "var move_speed := 10\n");
	CHECK_FALSE(text_editor->has_undo());
	CHECK_FALSE(text_editor->has_redo());
	CHECK_NE(text_editor->get_version(), text_editor->get_saved_version());

	ScriptRefactorApply::replace_editor_text(text_editor, "var speed := 10\n", true);
	CHECK_EQ(text_editor->get_text(), "var speed := 10\n");
	CHECK_FALSE(text_editor->has_undo());
	CHECK_FALSE(text_editor->has_redo());
	CHECK_EQ(text_editor->get_version(), text_editor->get_saved_version());

	memdelete(text_editor);
}

TEST_CASE("[Editor][ScriptRefactorApply] Closed file write replaces through a temporary file") {
	const String dir = make_temp_dir("script_refactor_apply_write");
	const String path = dir.path_join("player.fs");
	write_text_for_test(path, "var speed := 10\n");

	String error;
	CHECK(ScriptRefactorApply::write_file(path, "var move_speed := 10\n", error));
	CHECK(error.is_empty());

	Error read_error = OK;
	CHECK_EQ(FileAccess::get_file_as_string(path, &read_error), "var move_speed := 10\n");
	CHECK_EQ(read_error, OK);

	const PackedStringArray files = DirAccess::get_files_at(dir);
	for (const String &file : files) {
		CHECK_FALSE(file.begins_with("player.fs.tmp"));
	}

	DirAccess::remove_absolute(path);
	DirAccess::remove_absolute(dir);
}

#ifdef UNIX_ENABLED
TEST_CASE("[Editor][ScriptRefactorApply] Closed file write failure leaves original file untouched") {
	const String dir = make_temp_dir("script_refactor_apply_write_fail");
	const String path = dir.path_join("player.fs");
	write_text_for_test(path, "var speed := 10\n");
	REQUIRE_EQ(FileAccess::set_unix_permissions(path, 0644), OK);
	REQUIRE_EQ(FileAccess::set_unix_permissions(dir, 0555), OK);

	String error;
	ERR_PRINT_OFF;
	const bool wrote = ScriptRefactorApply::write_file(path, "var move_speed := 10\n", error);
	ERR_PRINT_ON;

	CHECK_FALSE(wrote);
	CHECK_FALSE(error.is_empty());

	Error read_error = OK;
	CHECK_EQ(FileAccess::get_file_as_string(path, &read_error), "var speed := 10\n");
	CHECK_EQ(read_error, OK);

	FileAccess::set_unix_permissions(dir, 0755);
	FileAccess::set_unix_permissions(path, 0644);
	DirAccess::remove_absolute(path);
	DirAccess::remove_absolute(dir);
}
#endif // UNIX_ENABLED

TEST_CASE("[Editor][ScriptRefactorApply] Plan retains exact pre-apply source so undo restores it") {
	// The wizard's single-step undo restores each file from the plan's
	// before_source. This guards the acceptance criterion that applying and then
	// undoing restores the project exactly, exercised at the file-write layer
	// (apply writes after_source; undo writes before_source).
	const String dir = make_temp_dir("script_refactor_apply_roundtrip");
	const String path = dir.path_join("player.fs");
	const String original = "var speed := 10\r\nprint(speed)\n";
	write_text_for_test(path, original);

	RefactorFileEdit player_edits;
	player_edits.path = path;
	player_edits.edits.push_back(make_edit(0, 4, 9, "speed", "move_speed"));
	player_edits.edits.push_back(make_edit(1, 6, 11, "speed", "move_speed"));

	Vector<RefactorFileEdit> edits;
	edits.push_back(player_edits);

	Vector<ScriptRefactorSource> sources;
	sources.push_back(make_source(path, original));

	ScriptRefactorApplyPlan plan;
	String error;
	REQUIRE(ScriptRefactorApply::build_plan(edits, sources, plan, error));
	REQUIRE_EQ(plan.files.size(), 1);

	// before_source must be byte-identical to the original (including the mixed
	// line endings) so undo cannot silently normalize the file.
	CHECK_EQ(plan.files[0].before_source, original);

	// Apply (write after_source), then undo (write before_source) and confirm
	// the file matches the original exactly.
	CHECK(ScriptRefactorApply::write_file(path, plan.files[0].after_source, error));
	Error read_error = OK;
	CHECK_NE(FileAccess::get_file_as_string(path, &read_error), original);

	CHECK(ScriptRefactorApply::write_file(path, plan.files[0].before_source, error));
	CHECK_EQ(FileAccess::get_file_as_string(path, &read_error), original);
	CHECK_EQ(read_error, OK);

	DirAccess::remove_absolute(path);
	DirAccess::remove_absolute(dir);
}

} // namespace TestScriptRefactorApply

#endif // TOOLS_ENABLED
