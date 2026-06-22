/**************************************************************************/
/*  test_refactor_diff_preview.h                                          */
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

#include "editor/script/refactor_diff_preview.h"

#include "editor/script/script_refactor_apply.h"
#include "tests/test_macros.h"

namespace TestRefactorDiffPreview {

TEST_CASE("[Editor][RefactorDiffPreview] File selection state") {
	Vector<RefactorDiffPreviewFile> files;
	RefactorDiffPreviewFile player;
	player.path = "res://player.gd";
	player.before_source = "var speed = 10\n";
	player.after_source = "var velocity = 10\n";
	player.edit_count = 1;
	files.push_back(player);

	RefactorDiffPreviewFile enemy;
	enemy.path = "res://enemy.gd";
	enemy.before_source = "target.speed += 1\n";
	enemy.after_source = "target.velocity += 1\n";
	enemy.edit_count = 1;
	files.push_back(enemy);

	RefactorDiffPreviewModel model;
	model.set_files(files);

	CHECK_EQ(model.get_file_count(), 2);
	CHECK_EQ(model.get_selected_index(), 0);
	CHECK(model.is_file_accepted(0));
	CHECK(model.is_file_accepted(1));
	CHECK_EQ(model.get_accepted_file_count(), 2);

	model.reject_file(1);
	CHECK(model.is_file_accepted(0));
	CHECK_FALSE(model.is_file_accepted(1));
	CHECK_EQ(model.get_accepted_file_count(), 1);
	CHECK_EQ(model.get_accepted_paths()[0], "res://player.gd");

	model.select_file(1);
	CHECK_EQ(model.get_selected_index(), 1);
	CHECK_EQ(model.get_selected_file().path, "res://enemy.gd");

	model.accept_file(1);
	CHECK(model.is_file_accepted(1));
	CHECK_EQ(model.get_accepted_file_count(), 2);
}

TEST_CASE("[Editor][RefactorDiffPreview] File selection handles empty input") {
	RefactorDiffPreviewModel model;
	model.set_files(Vector<RefactorDiffPreviewFile>());

	CHECK_EQ(model.get_file_count(), 0);
	CHECK_EQ(model.get_selected_index(), -1);
	CHECK_EQ(model.get_accepted_file_count(), 0);
	CHECK(model.get_accepted_paths().is_empty());
}

TEST_CASE("[Editor][RefactorDiffPreview] File selection rejects out-of-range indexes") {
	Vector<RefactorDiffPreviewFile> files;
	RefactorDiffPreviewFile player;
	player.path = "res://player.gd";
	files.push_back(player);

	RefactorDiffPreviewModel model;
	model.set_files(files);

	ERR_PRINT_OFF;
	CHECK_FALSE(model.select_file(-1));
	CHECK_FALSE(model.select_file(1));
	ERR_PRINT_ON;
	CHECK_EQ(model.get_selected_index(), 0);
}

TEST_CASE("[Editor][RefactorDiffPreview] Accepted files produce filtered apply plan") {
	ScriptRefactorApplyPlan plan;

	ScriptRefactorFilePlan player;
	player.path = "res://player.gd";
	player.before_source = "var speed := 10\n";
	player.after_source = "var move_speed := 10\n";
	player.edit_count = 1;
	player.before_source_is_saved_version = false;
	plan.files.push_back(player);

	ScriptRefactorFilePlan enemy;
	enemy.path = "res://enemy.gd";
	enemy.before_source = "target.speed += 1\n";
	enemy.after_source = "target.move_speed += 1\n";
	enemy.edit_count = 1;
	plan.files.push_back(enemy);

	RefactorDiffPreviewModel model;
	model.set_apply_plan(plan);

	const ScriptRefactorApplyPlan accepted = model.get_accepted_apply_plan();
	REQUIRE_EQ(accepted.files.size(), 2);
	CHECK_EQ(accepted.files[0].path, "res://player.gd");
	CHECK_EQ(accepted.files[0].before_source, "var speed := 10\n");
	CHECK_EQ(accepted.files[0].after_source, "var move_speed := 10\n");
	CHECK_EQ(accepted.files[0].edit_count, 1);
	CHECK_FALSE(accepted.files[0].before_source_is_saved_version);
	CHECK_EQ(accepted.files[1].path, "res://enemy.gd");
	CHECK(accepted.files[1].before_source_is_saved_version);

	model.reject_file(1);
	const ScriptRefactorApplyPlan filtered = model.get_accepted_apply_plan();
	REQUIRE_EQ(filtered.files.size(), 1);
	CHECK_EQ(filtered.files[0].path, "res://player.gd");
}

TEST_CASE("[Editor][RefactorDiffPreview] Side-by-side diff rows") {
	SUBCASE("empty sources produce no rows") {
		const Vector<RefactorDiffPreviewLine> lines = RefactorDiffPreviewModel::make_diff_lines("", "");

		CHECK(lines.is_empty());
	}

	SUBCASE("identical sources produce unchanged rows") {
		const Vector<RefactorDiffPreviewLine> lines = RefactorDiffPreviewModel::make_diff_lines(
				"var speed = 10\nprint(speed)\n",
				"var speed = 10\nprint(speed)\n");

		REQUIRE_EQ(lines.size(), 2);
		CHECK_FALSE(lines[0].changed);
		CHECK_FALSE(lines[1].changed);
		CHECK_EQ(lines[0].old_text, "var speed = 10");
		CHECK_EQ(lines[0].new_text, "var speed = 10");
	}

	SUBCASE("brand-new file leaves old side blank") {
		const Vector<RefactorDiffPreviewLine> lines = RefactorDiffPreviewModel::make_diff_lines(
				"",
				"extends Node\nfunc ready():\n\tpass\n");

		REQUIRE_EQ(lines.size(), 3);
		CHECK_EQ(lines[0].old_line, -1);
		CHECK_EQ(lines[0].new_line, 1);
		CHECK_FALSE(lines[0].old_changed);
		CHECK(lines[0].new_changed);
		CHECK_EQ(lines[0].new_text, "extends Node");
	}

	SUBCASE("replacement pairs old and new lines") {
		Vector<RefactorDiffPreviewLine> lines = RefactorDiffPreviewModel::make_diff_lines(
				"var speed = 10\nprint(speed)\n",
				"var velocity = 10\nprint(velocity)\n");

		REQUIRE_EQ(lines.size(), 2);
		CHECK_EQ(lines[0].old_line, 1);
		CHECK_EQ(lines[0].new_line, 1);
		CHECK_EQ(lines[0].old_text, "var speed = 10");
		CHECK_EQ(lines[0].new_text, "var velocity = 10");
		CHECK(lines[0].changed);
		CHECK(lines[0].old_changed);
		CHECK(lines[0].new_changed);
		CHECK_EQ(lines[1].old_text, "print(speed)");
		CHECK_EQ(lines[1].new_text, "print(velocity)");
		CHECK(lines[1].changed);
		CHECK(lines[1].old_changed);
		CHECK(lines[1].new_changed);
	}

	SUBCASE("insertion leaves the old side blank") {
		Vector<RefactorDiffPreviewLine> lines = RefactorDiffPreviewModel::make_diff_lines(
				"func ready():\n",
				"func ready():\n\tprint(\"ready\")\n");

		REQUIRE_EQ(lines.size(), 2);
		CHECK_FALSE(lines[0].changed);
		CHECK_EQ(lines[1].old_line, -1);
		CHECK_EQ(lines[1].new_line, 2);
		CHECK_EQ(lines[1].old_text, "");
		CHECK_EQ(lines[1].new_text, "\tprint(\"ready\")");
		CHECK(lines[1].changed);
		CHECK_FALSE(lines[1].old_changed);
		CHECK(lines[1].new_changed);
	}

	SUBCASE("deletion leaves the new side blank") {
		Vector<RefactorDiffPreviewLine> lines = RefactorDiffPreviewModel::make_diff_lines(
				"var unused = 1\nreturn value\n",
				"return value\n");

		REQUIRE_EQ(lines.size(), 2);
		CHECK_EQ(lines[0].old_line, 1);
		CHECK_EQ(lines[0].new_line, -1);
		CHECK_EQ(lines[0].old_text, "var unused = 1");
		CHECK_EQ(lines[0].new_text, "");
		CHECK(lines[0].changed);
		CHECK(lines[0].old_changed);
		CHECK_FALSE(lines[0].new_changed);
		CHECK_FALSE(lines[1].changed);
	}
}

} // namespace TestRefactorDiffPreview

#endif // TOOLS_ENABLED
