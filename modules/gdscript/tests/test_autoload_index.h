/**************************************************************************/
/*  test_autoload_index.h                                                 */
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

#include "modules/gdscript/gdscript_autoload_index.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/script_language.h"
#include "core/os/os.h"
#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript_cache.h"
#include "modules/gdscript/gdscript_parser.h"

#include "tests/test_macros.h"

namespace GDScriptTests {

namespace {

class ScopedAutoloadSettings {
	Vector<StringName> names;

	void clear_name(const StringName &p_name) {
		ProjectSettings *project_settings = ProjectSettings::get_singleton();
		const String setting = "autoload/" + String(p_name);
		if (project_settings->has_setting(setting)) {
			project_settings->clear(setting);
		}
		if (project_settings->has_autoload(p_name)) {
			project_settings->remove_autoload(p_name);
		}
	}

public:
	~ScopedAutoloadSettings() {
		for (const StringName &name : names) {
			clear_name(name);
		}
	}

	void set(const StringName &p_name, const String &p_path, bool p_singleton, int p_order) {
		clear_name(p_name);
		names.push_back(p_name);

		const String setting = "autoload/" + String(p_name);
		ProjectSettings::get_singleton()->set_setting(setting, p_singleton ? "*" + p_path : p_path);
		ProjectSettings::get_singleton()->set_order(setting, p_order);
	}
};

class ScopedScriptServerClass {
	StringName class_name;

public:
	ScopedScriptServerClass(const StringName &p_class_name, const String &p_base, const String &p_path) {
		class_name = p_class_name;
		ScriptServer::remove_global_class(class_name);
		ScriptServer::add_global_class(class_name, p_base, SNAME("GDScript"), p_path, false, false, false);
	}

	~ScopedScriptServerClass() {
		ScriptServer::remove_global_class(class_name);
	}
};

class ScopedTempFiles {
	String root;
	Vector<String> files;

public:
	ScopedTempFiles(const String &p_name) {
		root = OS::get_singleton()->get_temp_path().path_join(p_name);
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		CHECK_EQ(dir->make_dir_recursive(root), OK);
	}

	~ScopedTempFiles() {
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		for (const String &file : files) {
			dir->remove(file);
		}
		dir->remove(root);
	}

	String write(const String &p_file_name, const String &p_contents) {
		const String path = root.path_join(p_file_name);
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
		CHECK(file.is_valid());
		if (file.is_null()) {
			return path;
		}
		file->store_string(p_contents);
		files.push_back(path);
		return path;
	}

	String missing(const String &p_file_name) const {
		return root.path_join(p_file_name);
	}
};

bool has_diagnostic(const GDScriptAutoloadIndexEntry &p_entry, GDScriptAutoloadIndexDiagnostic::Code p_code) {
	for (const GDScriptAutoloadIndexDiagnostic &diagnostic : p_entry.diagnostics) {
		if (diagnostic.code == p_code) {
			return true;
		}
	}
	return false;
}

Error analyze_autoload_source(GDScriptParser &r_parser, const String &p_source, const String &p_path) {
	Error err = r_parser.parse(p_source, p_path, false);
	if (err != OK) {
		return err;
	}

	GDScriptAnalyzer analyzer(&r_parser);
	return analyzer.analyze();
}

bool autoload_analyzer_has_error(const GDScriptParser &p_parser, const String &p_expected_error) {
	for (const GDScriptParser::ParserError &parser_error : p_parser.get_errors()) {
		if (parser_error.message == p_expected_error) {
			return true;
		}
	}
	return false;
}

} // namespace

TEST_CASE("[Modules][GDScript] Autoload index builds project settings entries in order") {
	ScopedTempFiles files("gdscript_autoload_index_order");
	ScopedAutoloadSettings autoloads;

	const String earlier_path = files.write("autoload_earlier.gd",
			"class_name IndexEarlier extends Node\n"
			"func marker() -> int:\n"
			"\treturn 1\n");
	const String later_path = files.write("autoload_later.gd",
			"extends Node\n"
			"func marker() -> int:\n"
			"\treturn 2\n");

	autoloads.set(SNAME("IndexLater"), later_path, true, 40);
	autoloads.set(SNAME("IndexEarlier"), earlier_path, false, 10);

	GDScriptAutoloadIndex index;
	index.rebuild_from_project_settings();

	const Vector<GDScriptAutoloadIndexEntry> &entries = index.get_entries();
	CHECK_EQ(entries.size(), 2);
	if (entries.size() != 2) {
		return;
	}

	CHECK_EQ(entries[0].name, SNAME("IndexEarlier"));
	CHECK_EQ(entries[0].path, earlier_path);
	CHECK_FALSE(entries[0].is_singleton);
	CHECK_EQ(entries[0].order, 10);
	CHECK_EQ(entries[0].source, GDScriptAutoloadIndexEntry::SOURCE_PROJECT_SETTINGS);
	CHECK_EQ(entries[0].global_class_name, SNAME("IndexEarlier"));
	CHECK_EQ(entries[0].script_path, earlier_path);
	CHECK_EQ(entries[0].native_base, SNAME("Node"));
	CHECK(entries[0].is_node);
	CHECK(entries[0].is_same_script_global_class);
	CHECK(entries[0].diagnostics.is_empty());

	CHECK_EQ(entries[1].name, SNAME("IndexLater"));
	CHECK(entries[1].is_singleton);
	CHECK_EQ(entries[1].order, 40);

	CHECK(index.has_autoload(SNAME("IndexEarlier")));
	CHECK(index.get_by_name(SNAME("IndexEarlier")) != nullptr);
	if (index.get_by_name(SNAME("IndexEarlier")) == nullptr) {
		return;
	}
	CHECK_EQ(index.get_by_name(SNAME("IndexEarlier"))->path, earlier_path);
	CHECK(index.get_by_path(earlier_path) != nullptr);
	if (index.get_by_path(earlier_path) == nullptr) {
		return;
	}
	CHECK_EQ(index.get_by_path(earlier_path)->name, SNAME("IndexEarlier"));
	CHECK(index.get_by_global_class(SNAME("IndexEarlier")) != nullptr);
	if (index.get_by_global_class(SNAME("IndexEarlier")) == nullptr) {
		return;
	}
	CHECK_EQ(index.get_by_global_class(SNAME("IndexEarlier"))->name, SNAME("IndexEarlier"));
	CHECK(index.get_version() > 0);
}

TEST_CASE("[Modules][GDScript] Autoload index records project settings diagnostics") {
	ScopedTempFiles files("gdscript_autoload_index_diagnostics");
	ScopedAutoloadSettings autoloads;

	const String node_path = files.write("valid_node.gd", "class_name IndexNode extends Node\n");
	const String non_node_path = files.write("plain_resource.gd", "class_name IndexPlain extends RefCounted\n");
	const String text_path = files.write("notes.txt", "not a script or a scene\n");
	const String unrelated_path = files.write("unrelated_autoload.gd", "class_name IndexOther extends Node\n");
	const String registered_path = files.write("registered_global.gd", "class_name IndexCollision extends Node\n");

	ScopedScriptServerClass registered_collision(SNAME("IndexCollision"), "Node", registered_path);

	autoloads.set(SNAME("IndexMissing"), files.missing("missing.gd"), true, 10);
	autoloads.set(SNAME("IndexText"), text_path, true, 20);
	autoloads.set(SNAME("IndexPlain"), non_node_path, true, 30);
	autoloads.set(SNAME("godot"), node_path, true, 40);
	autoloads.set(SNAME("IndexCollision"), unrelated_path, true, 50);

	GDScriptAutoloadIndex index;
	index.rebuild_from_project_settings();

	CHECK(index.get_by_name(SNAME("IndexMissing")) != nullptr);
	if (index.get_by_name(SNAME("IndexMissing")) == nullptr) {
		return;
	}
	CHECK(has_diagnostic(*index.get_by_name(SNAME("IndexMissing")), GDScriptAutoloadIndexDiagnostic::MISSING_PATH));

	CHECK(index.get_by_name(SNAME("IndexText")) != nullptr);
	if (index.get_by_name(SNAME("IndexText")) == nullptr) {
		return;
	}
	CHECK(has_diagnostic(*index.get_by_name(SNAME("IndexText")), GDScriptAutoloadIndexDiagnostic::NON_SCRIPT_NON_SCENE_PATH));

	CHECK(index.get_by_name(SNAME("IndexPlain")) != nullptr);
	if (index.get_by_name(SNAME("IndexPlain")) == nullptr) {
		return;
	}
	CHECK_EQ(index.get_by_name(SNAME("IndexPlain"))->native_base, SNAME("RefCounted"));
	CHECK_FALSE(index.get_by_name(SNAME("IndexPlain"))->is_node);
	CHECK(has_diagnostic(*index.get_by_name(SNAME("IndexPlain")), GDScriptAutoloadIndexDiagnostic::NON_NODE_SCRIPT));

	CHECK(index.get_by_name(SNAME("godot")) != nullptr);
	if (index.get_by_name(SNAME("godot")) == nullptr) {
		return;
	}
	CHECK(has_diagnostic(*index.get_by_name(SNAME("godot")), GDScriptAutoloadIndexDiagnostic::RESERVED_GLOBAL_NAME_COLLISION));

	CHECK(index.get_by_name(SNAME("IndexCollision")) != nullptr);
	if (index.get_by_name(SNAME("IndexCollision")) == nullptr) {
		return;
	}
	CHECK(has_diagnostic(*index.get_by_name(SNAME("IndexCollision")), GDScriptAutoloadIndexDiagnostic::UNRELATED_GLOBAL_CLASS_COLLISION));
}

TEST_CASE("[Modules][GDScript] Autoload index allows same-script class and autoload names") {
	ScopedTempFiles files("gdscript_autoload_index_same_script");
	ScopedAutoloadSettings autoloads;

	const String same_path = files.write("same_script.gd", "class_name IndexSame extends Node\n");
	ScopedScriptServerClass registered_same(SNAME("IndexSame"), "Node", same_path);

	autoloads.set(SNAME("IndexSame"), same_path, true, 10);

	GDScriptAutoloadIndex index;
	index.rebuild_from_project_settings();

	const GDScriptAutoloadIndexEntry *entry = index.get_by_name(SNAME("IndexSame"));
	CHECK(entry != nullptr);
	if (entry == nullptr) {
		return;
	}
	CHECK_EQ(entry->global_class_name, SNAME("IndexSame"));
	CHECK(entry->is_same_script_global_class);
	CHECK_FALSE(has_diagnostic(*entry, GDScriptAutoloadIndexDiagnostic::UNRELATED_GLOBAL_CLASS_COLLISION));
	CHECK(index.get_by_global_class(SNAME("IndexSame")) != nullptr);
	if (index.get_by_global_class(SNAME("IndexSame")) == nullptr) {
		return;
	}
	CHECK_EQ(index.get_by_global_class(SNAME("IndexSame"))->path, same_path);
}

TEST_CASE("[Modules][GDScript] Analyzer allows same-script class name and autoload singleton names") {
	ScopedTempFiles files("gdscript_analyzer_autoload_same_script");
	ScopedAutoloadSettings autoloads;

	const String source = "class_name AnalyzerSameAutoload extends Node\n";
	const String same_path = files.write("same_script.gd", source);
	ScopedScriptServerClass registered_same(SNAME("AnalyzerSameAutoload"), "Node", same_path);

	autoloads.set(SNAME("AnalyzerSameAutoload"), same_path, true, 10);

	GDScriptParser parser;
	const Error err = analyze_autoload_source(parser, source, same_path);

	CHECK_EQ(err, OK);
	CHECK_FALSE(autoload_analyzer_has_error(parser, R"(Class "AnalyzerSameAutoload" hides an autoload singleton.)"));
}

TEST_CASE("[Modules][GDScript] Analyzer rejects class names that hide unrelated autoload singletons") {
	ScopedTempFiles files("gdscript_analyzer_autoload_name_conflict");
	ScopedAutoloadSettings autoloads;

	const String autoload_path = files.write("autoload_script.gd", "extends Node\n");
	const String class_path = files.write("class_script.gd", "class_name AnalyzerConflictAutoload extends Node\n");

	autoloads.set(SNAME("AnalyzerConflictAutoload"), autoload_path, true, 10);

	GDScriptParser parser;
	const Error err = analyze_autoload_source(parser, "class_name AnalyzerConflictAutoload extends Node\n", class_path);

	CHECK_NE(err, OK);
	CHECK(autoload_analyzer_has_error(parser, R"(Class "AnalyzerConflictAutoload" hides an autoload singleton.)"));
}

TEST_CASE("[Modules][GDScript] Analyzer resolves value-position same-script autoload as singleton instance") {
	ScopedTempFiles files("gdscript_analyzer_autoload_value_same_script");
	ScopedAutoloadSettings autoloads;

	const String singleton_path = files.write("autoload_script.gd", "class_name AnalyzerValueAutoload extends Node\n");
	const String consumer_path = files.write("consumer.gd", "var singleton = AnalyzerValueAutoload\n");
	ScopedScriptServerClass registered_same(SNAME("AnalyzerValueAutoload"), "Node", singleton_path);

	autoloads.set(SNAME("AnalyzerValueAutoload"), singleton_path, true, 10);

	GDScriptParser parser;
	const Error err = analyze_autoload_source(parser, "var singleton = AnalyzerValueAutoload\n", consumer_path);

	CHECK_EQ(err, OK);
	const GDScriptParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(root->has_member(SNAME("singleton")));
	if (!root->has_member(SNAME("singleton"))) {
		return;
	}

	const GDScriptParser::VariableNode *singleton = root->get_member(SNAME("singleton")).variable;
	CHECK(singleton != nullptr);
	if (singleton == nullptr) {
		return;
	}
	CHECK(singleton->initializer != nullptr);
	if (singleton->initializer == nullptr) {
		return;
	}

	const GDScriptParser::DataType initializer_type = singleton->initializer->get_datatype();
	CHECK_FALSE(initializer_type.is_meta_type);
	CHECK_EQ(initializer_type.kind, GDScriptParser::DataType::CLASS);
	CHECK_EQ(initializer_type.native_type, SNAME("Node"));
}

TEST_CASE("[Modules][GDScript] Analyzer keeps value-position dotted autoload lookup ahead of namespaced globals") {
	ScopedTempFiles files("gdscript_analyzer_autoload_dotted_value");
	ScopedAutoloadSettings autoloads;

	const String singleton_path = files.write("autoload_script.gd", "extends Node\n");
	const String namespaced_global_path = files.write("namespaced_global.gd", "extends Node\n");
	const String consumer_path = files.write("consumer.gd", "var member = AnalyzerDottedAutoload.Member\n");
	ScopedScriptServerClass registered_namespaced_global(SNAME("AnalyzerDottedAutoload.Member"), "Node", namespaced_global_path);

	autoloads.set(SNAME("AnalyzerDottedAutoload"), singleton_path, true, 10);

	GDScriptParser parser;
	const Error err = analyze_autoload_source(parser, "var member = AnalyzerDottedAutoload.Member\n", consumer_path);

	CHECK_EQ(err, OK);
	const GDScriptParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(root->has_member(SNAME("member")));
	if (!root->has_member(SNAME("member"))) {
		return;
	}

	const GDScriptParser::VariableNode *member = root->get_member(SNAME("member")).variable;
	CHECK(member != nullptr);
	if (member == nullptr) {
		return;
	}
	CHECK(member->initializer != nullptr);
	if (member->initializer == nullptr) {
		return;
	}

	const GDScriptParser::DataType initializer_type = member->initializer->get_datatype();
	CHECK_FALSE(initializer_type.is_meta_type);
	CHECK_NE(initializer_type.kind, GDScriptParser::DataType::CLASS);
}

TEST_CASE("[Modules][GDScript] Cached analyzer resolves value-position autoloads through the index") {
	ScopedTempFiles files("gdscript_analyzer_cached_autoload_value");
	ScopedAutoloadSettings autoloads;

	const String singleton_path = files.write("autoload_script.gd", "class_name AnalyzerCachedValueAutoload extends Node\n");
	const String consumer_path = files.write("consumer.gd", "var singleton = AnalyzerCachedValueAutoload\n");
	ScopedScriptServerClass registered_same(SNAME("AnalyzerCachedValueAutoload"), "Node", singleton_path);

	autoloads.set(SNAME("AnalyzerCachedValueAutoload"), singleton_path, true, 10);
	GDScriptCache::remove_parser(consumer_path);

	Error err = OK;
	Ref<GDScriptParserRef> parser_ref = GDScriptCache::get_parser(consumer_path, GDScriptParserRef::FULLY_SOLVED, err);

	CHECK_EQ(err, OK);
	CHECK(parser_ref.is_valid());
	if (parser_ref.is_null()) {
		return;
	}
	const GDScriptParser::ClassNode *root = parser_ref->get_parser()->get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(root->has_member(SNAME("singleton")));
	if (!root->has_member(SNAME("singleton"))) {
		return;
	}

	const GDScriptParser::VariableNode *singleton = root->get_member(SNAME("singleton")).variable;
	CHECK(singleton != nullptr);
	if (singleton == nullptr) {
		return;
	}
	CHECK(singleton->initializer != nullptr);
	if (singleton->initializer == nullptr) {
		return;
	}

	const GDScriptParser::DataType initializer_type = singleton->initializer->get_datatype();
	CHECK_FALSE(initializer_type.is_meta_type);
	CHECK_EQ(initializer_type.kind, GDScriptParser::DataType::CLASS);
	CHECK_EQ(initializer_type.native_type, SNAME("Node"));

	GDScriptCache::remove_parser(consumer_path);
}

TEST_CASE("[Modules][GDScript] Analyzer refreshes autoload index after settings removal") {
	ScopedTempFiles files("gdscript_analyzer_autoload_removed_mid_analysis");
	ScopedAutoloadSettings autoloads;

	const String singleton_path = files.write("autoload_script.gd", "extends Node\n");
	const String consumer_path = files.write("consumer.gd", "var singleton = AnalyzerRemovedAutoload\n");

	autoloads.set(SNAME("AnalyzerRemovedAutoload"), singleton_path, true, 10);

	GDScriptParser parser;
	CHECK_EQ(parser.parse("var singleton = AnalyzerRemovedAutoload\n", consumer_path, false), OK);
	if (!parser.get_errors().is_empty()) {
		return;
	}

	GDScriptAnalyzer analyzer(&parser);
	CHECK_EQ(analyzer.resolve_inheritance(), OK);

	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	const String setting = "autoload/AnalyzerRemovedAutoload";
	CHECK(project_settings->has_setting(setting));
	CHECK(project_settings->has_autoload(SNAME("AnalyzerRemovedAutoload")));
	project_settings->clear(setting);
	project_settings->remove_autoload(SNAME("AnalyzerRemovedAutoload"));

	Error err = analyzer.resolve_interface();
	if (err == OK) {
		err = analyzer.resolve_body();
	}
	CHECK_NE(err, OK);
}

TEST_CASE("[Modules][GDScript] Analyzer keeps Node fallback for value-position unresolved autoload scripts") {
	ScopedTempFiles files("gdscript_analyzer_autoload_value_fallback");
	ScopedAutoloadSettings autoloads;

	const String missing_path = files.missing("missing_singleton.gd");
	const String consumer_path = files.write("consumer.gd", "var singleton = AnalyzerFallbackAutoload\n");

	autoloads.set(SNAME("AnalyzerFallbackAutoload"), missing_path, true, 10);

	GDScriptParser parser;
	const Error err = analyze_autoload_source(parser, "var singleton = AnalyzerFallbackAutoload\n", consumer_path);

	CHECK_EQ(err, OK);
	const GDScriptParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(root->has_member(SNAME("singleton")));
	if (!root->has_member(SNAME("singleton"))) {
		return;
	}

	const GDScriptParser::VariableNode *singleton = root->get_member(SNAME("singleton")).variable;
	CHECK(singleton != nullptr);
	if (singleton == nullptr) {
		return;
	}

	const GDScriptParser::DataType singleton_type = singleton->get_datatype();
	CHECK_FALSE(singleton_type.is_meta_type);
	CHECK_EQ(singleton_type.kind, GDScriptParser::DataType::NATIVE);
	CHECK_EQ(singleton_type.native_type, SNAME("Node"));
}

TEST_CASE("[Modules][GDScript] Analyzer keeps global classes as types when an autoload shares the name") {
	ScopedTempFiles files("gdscript_analyzer_autoload_global_type");
	ScopedAutoloadSettings autoloads;

	const String global_path = files.write("global_type.gd", "class_name AnalyzerGlobalTypeAutoload extends Node\n");
	const String autoload_path = files.write("autoload_script.gd", "extends Node\n");
	const String consumer_path = files.write("consumer.gd", "var typed: AnalyzerGlobalTypeAutoload\n");
	ScopedScriptServerClass registered_global(SNAME("AnalyzerGlobalTypeAutoload"), "Node", global_path);

	autoloads.set(SNAME("AnalyzerGlobalTypeAutoload"), autoload_path, true, 10);

	GDScriptParser parser;
	const Error err = analyze_autoload_source(parser, "var typed: AnalyzerGlobalTypeAutoload\n", consumer_path);

	CHECK_EQ(err, OK);
	const GDScriptParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(root->has_member(SNAME("typed")));
	if (!root->has_member(SNAME("typed"))) {
		return;
	}

	const GDScriptParser::VariableNode *typed = root->get_member(SNAME("typed")).variable;
	CHECK(typed != nullptr);
	if (typed == nullptr) {
		return;
	}

	const PropertyInfo typed_property = typed->get_datatype().to_property_info("typed");
	CHECK_EQ(typed_property.type, Variant::OBJECT);
	CHECK_EQ(typed_property.class_name, "AnalyzerGlobalTypeAutoload");
}

TEST_CASE("[Modules][GDScript] Analyzer rejects autoload singleton instances as base types") {
	ScopedTempFiles files("gdscript_analyzer_autoload_type_position");
	ScopedAutoloadSettings autoloads;

	const String autoload_path = files.write("autoload_script.gd", "extends Node\n");
	const String consumer_path = files.write("consumer.gd",
			"class Child:\n"
			"\textends AnalyzerTypeOnlyAutoload\n");

	autoloads.set(SNAME("AnalyzerTypeOnlyAutoload"), autoload_path, true, 10);

	GDScriptParser parser;
	const Error err = analyze_autoload_source(parser,
			"class Child:\n"
			"\textends AnalyzerTypeOnlyAutoload\n",
			consumer_path);

	CHECK_NE(err, OK);
	CHECK(autoload_analyzer_has_error(parser, R"(Could not find base class "AnalyzerTypeOnlyAutoload".)"));
}

} // namespace GDScriptTests
