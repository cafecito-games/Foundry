/**************************************************************************/
/*  test_doc_tools_namespace_xml.h                                        */
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

namespace TestDocToolsNamespaceXml {

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
		directory = root.simplify_path().path_join(vformat("doc_tools_namespace_xml_%d", OS::get_singleton()->get_process_id()));

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

static DocData::ClassDoc build_namespaced_class_doc() {
	DocData::ClassDoc class_doc;
	class_doc.name = "NamespaceXmlFixture";
	class_doc.namespace_path = "foundry.doc.fixture";
	class_doc.inherits = "Node";
	class_doc.brief_description = "A namespaced fixture class.";

	DocData::MethodDoc method;
	method.name = "ping";
	method.return_type = "void";
	class_doc.methods.push_back(method);

	return class_doc;
}

TEST_CASE("[DocToolsNamespaceXml] qualified_name joins the namespace and the simple name") {
	DocData::ClassDoc namespaced = build_namespaced_class_doc();
	CHECK_EQ(namespaced.qualified_name(), "foundry.doc.fixture.NamespaceXmlFixture");

	DocData::ClassDoc global;
	global.name = "NamespaceXmlGlobalFixture";
	CHECK_EQ(global.qualified_name(), "NamespaceXmlGlobalFixture");
}

TEST_CASE("[DocToolsNamespaceXml] a namespaced class is keyed, named, and filed by its qualified name") {
	ScopedDocToolsScratchDir scratch;

	DocTools writer;
	writer.add_doc(build_namespaced_class_doc());
	// `add_doc()` keys the in-memory map by the qualified name so help lookups can pass a
	// `ClassDB` key verbatim.
	CHECK(writer.class_list.has("foundry.doc.fixture.NamespaceXmlFixture"));
	CHECK_FALSE(writer.class_list.has("NamespaceXmlFixture"));

	REQUIRE_EQ(writer.save_classes(scratch.path(), HashMap<String, String>(), false), OK);

	const String saved_file = scratch.path().path_join("foundry.doc.fixture.NamespaceXmlFixture.xml");
	Ref<FileAccess> file = FileAccess::open(saved_file, FileAccess::READ);
	REQUIRE(file.is_valid());
	const String contents = file->get_as_text();
	// The namespace is a separate attribute; the flat dotted form is never emitted.
	CHECK(contents.contains("<class name=\"NamespaceXmlFixture\" namespace=\"foundry.doc.fixture\" inherits=\"Node\""));
	CHECK_FALSE(contents.contains("name=\"foundry.doc.fixture.NamespaceXmlFixture\""));

	DocTools reader;
	REQUIRE_EQ(reader.load_classes(scratch.path()), OK);
	REQUIRE(reader.class_list.has("foundry.doc.fixture.NamespaceXmlFixture"));
	CHECK_FALSE(reader.class_list.has("NamespaceXmlFixture"));

	const DocData::ClassDoc &loaded = reader.class_list["foundry.doc.fixture.NamespaceXmlFixture"];
	CHECK_EQ(loaded.name, "NamespaceXmlFixture");
	CHECK_EQ(loaded.namespace_path, "foundry.doc.fixture");
	CHECK_EQ(loaded.inherits, "Node");
	// Inheritance bookkeeping records the child under its qualified name too.
	REQUIRE(reader.inheriting.has("Node"));
	CHECK(reader.inheriting["Node"].has("foundry.doc.fixture.NamespaceXmlFixture"));
}

TEST_CASE("[DocToolsNamespaceXml] a global class round trips without a namespace attribute") {
	ScopedDocToolsScratchDir scratch;

	DocData::ClassDoc class_doc;
	class_doc.name = "NamespaceXmlGlobalFixture";
	class_doc.inherits = "Node";

	DocTools writer;
	writer.add_doc(class_doc);
	REQUIRE_EQ(writer.save_classes(scratch.path(), HashMap<String, String>(), false), OK);

	const String saved_file = scratch.path().path_join("NamespaceXmlGlobalFixture.xml");
	Ref<FileAccess> file = FileAccess::open(saved_file, FileAccess::READ);
	REQUIRE(file.is_valid());
	CHECK_FALSE(file->get_as_text().contains("namespace="));

	DocTools reader;
	REQUIRE_EQ(reader.load_classes(scratch.path()), OK);
	REQUIRE(reader.class_list.has("NamespaceXmlGlobalFixture"));
	CHECK(reader.class_list["NamespaceXmlGlobalFixture"].namespace_path.is_empty());
}

TEST_CASE("[DocToolsNamespaceXml] merge_from matches namespaced classes by qualified name") {
	DocData::ClassDoc generated = build_namespaced_class_doc();
	generated.description = "";

	DocData::ClassDoc authored;
	authored.name = "NamespaceXmlFixture";
	authored.namespace_path = "foundry.doc.fixture";
	authored.description = "Hand-authored prose.";

	// A same-named class in the global namespace must not be mistaken for the namespaced one.
	DocData::ClassDoc decoy;
	decoy.name = "NamespaceXmlFixture";
	decoy.description = "Decoy prose.";

	DocTools target;
	target.add_doc(generated);

	DocTools source;
	source.add_doc(authored);
	source.add_doc(decoy);

	target.merge_from(source);

	CHECK_EQ(target.class_list["foundry.doc.fixture.NamespaceXmlFixture"].description, "Hand-authored prose.");
}

TEST_CASE("[DocToolsNamespaceXml] the namespace survives a dictionary round trip") {
	const DocData::ClassDoc class_doc = build_namespaced_class_doc();
	const Dictionary dict = DocData::ClassDoc::to_dict(class_doc);
	REQUIRE(dict.has("namespace"));
	CHECK_EQ(String(dict["namespace"]), "foundry.doc.fixture");

	const DocData::ClassDoc restored = DocData::ClassDoc::from_dict(dict);
	CHECK_EQ(restored.name, "NamespaceXmlFixture");
	CHECK_EQ(restored.namespace_path, "foundry.doc.fixture");
	CHECK_EQ(restored.qualified_name(), "foundry.doc.fixture.NamespaceXmlFixture");

	DocData::ClassDoc global;
	global.name = "NamespaceXmlGlobalFixture";
	CHECK_FALSE(DocData::ClassDoc::to_dict(global).has("namespace"));
}

} // namespace TestDocToolsNamespaceXml

#endif // TOOLS_ENABLED
