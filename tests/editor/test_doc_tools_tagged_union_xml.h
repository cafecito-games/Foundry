/**************************************************************************/
/*  test_doc_tools_tagged_union_xml.h                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             FOUNDRY ENGINE                             */
/*          A fork of the Godot Engine (https://godotengine.org)          */
/*                       https://www.cafecito.games                       */
/**************************************************************************/
/* Copyright (c) 2026-present Cafecito Games LLC.                         */
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

#include "editor/doc/doc_tools.h"

#include "core/io/dir_access.h"
#include "core/os/os.h"

#include "tests/test_macros.h"

namespace TestDocToolsTaggedUnionXml {

// A throwaway directory under the shared test scratch root, removed on destruction, so
// `DocTools::save_classes()`/`load_classes()` round trips exercise real files on disk without
// polluting the repository or a fixture directory.
class ScopedDocToolsScratchDir {
	String directory;

public:
	ScopedDocToolsScratchDir() {
		String root;
		if (OS::get_singleton()->has_environment("FOUNDRY_TEST_SCRATCH")) {
			root = OS::get_singleton()->get_environment("FOUNDRY_TEST_SCRATCH");
		}
		if (root.is_empty()) {
			root = OS::get_singleton()->get_temp_path();
		}
		directory = root.simplify_path().path_join(vformat("doc_tools_tagged_union_xml_%d", OS::get_singleton()->get_process_id()));

		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		remove_recursive(dir, directory);
		CHECK_EQ(dir->make_dir_recursive(directory), OK);
	}

	~ScopedDocToolsScratchDir() {
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		remove_recursive(dir, directory);
	}

	const String &path() const { return directory; }

private:
	static void remove_recursive(Ref<DirAccess> &p_dir, const String &p_path) {
		Ref<DirAccess> dir = DirAccess::open(p_path);
		if (dir.is_null()) {
			return;
		}
		dir->set_include_hidden(true);
		dir->list_dir_begin();
		for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
			if (entry == "." || entry == "..") {
				continue;
			}
			const String child = p_path.path_join(entry);
			if (dir->current_is_dir()) {
				remove_recursive(p_dir, child);
			} else {
				DirAccess::remove_absolute(child);
			}
		}
		dir->list_dir_end();
		DirAccess::remove_absolute(p_path);
	}
};

// Builds a class doc with a tagged-union enum: one payload case carrying named fields, one
// payload-less singleton case, matching what `FSDocGen` produces for a FoundryScript tagged
// union declaration.
static DocData::ClassDoc build_tagged_union_class_doc() {
	DocData::ClassDoc class_doc;
	class_doc.name = "DocToolsTaggedUnionXmlFixture";

	DocData::EnumDoc enum_doc;
	enum_doc.description = "A direction, either a fixed heading or a free-form move.";
	enum_doc.is_tagged_union = true;
	class_doc.enums["Direction"] = enum_doc;

	DocData::ConstantDoc move_case;
	move_case.name = "Move";
	move_case.value = "Move(x: int, y: int)";
	move_case.is_value_valid = true;
	move_case.enumeration = "Direction";
	move_case.description = "Moves by a free-form offset.";
	DocData::TupleFieldDoc field_x;
	field_x.name = "x";
	field_x.type = "int";
	move_case.payload_fields.push_back(field_x);
	DocData::TupleFieldDoc field_y;
	field_y.name = "y";
	field_y.type = "int";
	move_case.payload_fields.push_back(field_y);
	class_doc.constants.push_back(move_case);

	DocData::ConstantDoc stay_case;
	stay_case.name = "Stay";
	stay_case.value = "Stay";
	stay_case.is_value_valid = true;
	stay_case.enumeration = "Direction";
	stay_case.description = "Stays in place; carries no payload.";
	class_doc.constants.push_back(stay_case);

	return class_doc;
}

TEST_CASE("[DocToolsTaggedUnionXml] payload_fields and is_tagged_union survive an XML round trip") {
	ScopedDocToolsScratchDir scratch;

	DocTools writer;
	writer.add_doc(build_tagged_union_class_doc());
	REQUIRE_EQ(writer.save_classes(scratch.path(), HashMap<String, String>(), false), OK);

	DocTools reader;
	REQUIRE_EQ(reader.load_classes(scratch.path()), OK);
	REQUIRE(reader.class_list.has("DocToolsTaggedUnionXmlFixture"));

	const DocData::ClassDoc &loaded = reader.class_list["DocToolsTaggedUnionXmlFixture"];

	REQUIRE(loaded.enums.has("Direction"));
	const DocData::EnumDoc &direction_enum = loaded.enums["Direction"];
	CHECK(direction_enum.is_tagged_union);
	// The writer indents description text like every other doc string; strip it back the same
	// way `EditorHelp` does when it renders a loaded description.
	CHECK_EQ(direction_enum.description.strip_edges(), "A direction, either a fixed heading or a free-form move.");

	REQUIRE_EQ(loaded.constants.size(), 2);
	const DocData::ConstantDoc *move_case = nullptr;
	const DocData::ConstantDoc *stay_case = nullptr;
	for (const DocData::ConstantDoc &constant : loaded.constants) {
		if (constant.name == "Move") {
			move_case = &constant;
		} else if (constant.name == "Stay") {
			stay_case = &constant;
		}
	}
	REQUIRE(move_case != nullptr);
	REQUIRE(stay_case != nullptr);

	REQUIRE_EQ(move_case->payload_fields.size(), 2);
	CHECK_EQ(move_case->payload_fields[0].name, "x");
	CHECK_EQ(move_case->payload_fields[0].type, "int");
	CHECK_EQ(move_case->payload_fields[1].name, "y");
	CHECK_EQ(move_case->payload_fields[1].type, "int");
	// The plain-string payload signature docgen writes into `value` is left untouched; only the
	// structured fields are newly round-tripped.
	CHECK_EQ(move_case->value, "Move(x: int, y: int)");

	CHECK(stay_case->payload_fields.is_empty());
}

TEST_CASE("[DocToolsTaggedUnionXml] a payload field with an empty name round trips as positional") {
	ScopedDocToolsScratchDir scratch;

	DocData::ClassDoc class_doc;
	class_doc.name = "DocToolsPositionalTupleFixture";

	DocData::ConstantDoc point_case;
	point_case.name = "Point";
	point_case.value = "Point(int, int)";
	point_case.is_value_valid = true;
	DocData::TupleFieldDoc first_field;
	first_field.type = "int";
	point_case.payload_fields.push_back(first_field);
	DocData::TupleFieldDoc second_field;
	second_field.type = "int";
	point_case.payload_fields.push_back(second_field);
	class_doc.constants.push_back(point_case);

	DocTools writer;
	writer.add_doc(class_doc);
	REQUIRE_EQ(writer.save_classes(scratch.path(), HashMap<String, String>(), false), OK);

	DocTools reader;
	REQUIRE_EQ(reader.load_classes(scratch.path()), OK);
	REQUIRE(reader.class_list.has("DocToolsPositionalTupleFixture"));

	const DocData::ClassDoc &loaded = reader.class_list["DocToolsPositionalTupleFixture"];
	REQUIRE_EQ(loaded.constants.size(), 1);
	REQUIRE_EQ(loaded.constants[0].payload_fields.size(), 2);
	CHECK(loaded.constants[0].payload_fields[0].name.is_empty());
	CHECK_EQ(loaded.constants[0].payload_fields[0].type, "int");
	CHECK(loaded.constants[0].payload_fields[1].name.is_empty());
	CHECK_EQ(loaded.constants[0].payload_fields[1].type, "int");
}

TEST_CASE("[DocToolsTaggedUnionXml] a constant without payload fields round trips exactly as before") {
	ScopedDocToolsScratchDir scratch;

	DocData::ClassDoc class_doc;
	class_doc.name = "DocToolsPlainConstantFixture";

	DocData::ConstantDoc plain_constant;
	plain_constant.name = "MAX_SIZE";
	plain_constant.value = "1024";
	plain_constant.is_value_valid = true;
	plain_constant.description = "The maximum size in bytes.";
	class_doc.constants.push_back(plain_constant);

	DocTools writer;
	writer.add_doc(class_doc);
	REQUIRE_EQ(writer.save_classes(scratch.path(), HashMap<String, String>(), false), OK);

	const String saved_file = scratch.path().path_join("DocToolsPlainConstantFixture.xml");
	Ref<FileAccess> file = FileAccess::open(saved_file, FileAccess::READ);
	REQUIRE(file.is_valid());
	const String contents = file->get_as_text();
	CHECK(contents.contains("<payload_field") == false);

	DocTools reader;
	REQUIRE_EQ(reader.load_classes(scratch.path()), OK);
	REQUIRE(reader.class_list.has("DocToolsPlainConstantFixture"));
	const DocData::ClassDoc &loaded = reader.class_list["DocToolsPlainConstantFixture"];
	REQUIRE_EQ(loaded.constants.size(), 1);
	CHECK(loaded.constants[0].payload_fields.is_empty());
	CHECK_EQ(loaded.constants[0].value, "1024");
	CHECK_EQ(loaded.constants[0].description.strip_edges(), "The maximum size in bytes.");
}

} // namespace TestDocToolsTaggedUnionXml

#endif // TOOLS_ENABLED
