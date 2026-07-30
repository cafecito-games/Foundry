/**************************************************************************/
/*  test_autoload_index.h                                                 */
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

#include "modules/foundry_script/fs_autoload_index.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/script_language.h"
#include "core/os/os.h"
#include "modules/foundry_script/fs_analyzer.h"
#include "modules/foundry_script/fs_cache.h"
#include "modules/foundry_script/fs_parser.h"

#include "tests/test_macros.h"

namespace FSTests {

namespace {

class ScopedAutoloadSettings {
	Vector<StringName> names;

	void clear_name(const StringName &p_name) {
		ProjectSettings *project_settings = ProjectSettings::get_singleton();
		const String setting = "autoload/" + String(p_name);
		const String prepend_setting = "autoload_prepend/" + String(p_name);
		if (project_settings->has_setting(setting)) {
			project_settings->clear(setting);
		}
		if (project_settings->has_setting(prepend_setting)) {
			project_settings->clear(prepend_setting);
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

	void set_prepend(const StringName &p_name, const String &p_path, bool p_singleton, int p_order) {
		clear_name(p_name);
		names.push_back(p_name);

		const String setting = "autoload_prepend/" + String(p_name);
		ProjectSettings::get_singleton()->set_setting(setting, p_singleton ? "*" + p_path : p_path);
		ProjectSettings::get_singleton()->set_order(setting, p_order);
	}

	void track(const StringName &p_name) {
		clear_name(p_name);
		names.push_back(p_name);
	}
};

class ScopedScriptServerClass {
	StringName class_name;

public:
	ScopedScriptServerClass(const StringName &p_class_name, const String &p_base, const String &p_path) {
		class_name = p_class_name;
		ScriptServer::remove_global_class(class_name);
		ScriptServer::add_global_class(class_name, p_base, SNAME("FoundryScript"), p_path, false, false, false);
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

	String reserve(const String &p_file_name) {
		const String path = root.path_join(p_file_name);
		files.push_back(path);
		return path;
	}
};

bool has_diagnostic(const FSAutoloadIndexEntry &p_entry, FSAutoloadIndexDiagnostic::Code p_code) {
	for (const FSAutoloadIndexDiagnostic &diagnostic : p_entry.diagnostics) {
		if (diagnostic.code == p_code) {
			return true;
		}
	}
	return false;
}

bool has_hard_diagnostic_containing(const FSAutoloadIndexEntry &p_entry, FSAutoloadIndexDiagnostic::Code p_code, const String &p_fragment) {
	for (const FSAutoloadIndexDiagnostic &diagnostic : p_entry.diagnostics) {
		if (diagnostic.code == p_code && diagnostic.is_error && diagnostic.message.contains(p_fragment)) {
			return true;
		}
	}
	return false;
}

FSAutoloadIndexDependency make_dependency(const StringName &p_name) {
	FSAutoloadIndexDependency dependency;
	dependency.name = p_name;
	return dependency;
}

FSAutoloadIndexDependency make_non_autoload_dependency(const StringName &p_name) {
	FSAutoloadIndexDependency dependency;
	dependency.name = p_name;
	dependency.is_autoload = false;
	return dependency;
}

FSAutoloadIndexEntry make_dependency_entry(const StringName &p_name, int p_order, const Vector<FSAutoloadIndexDependency> &p_dependencies = Vector<FSAutoloadIndexDependency>()) {
	FSAutoloadIndexEntry entry;
	entry.name = p_name;
	entry.path = "res://" + String(p_name) + ".fs";
	entry.is_singleton = true;
	entry.order = p_order;
	entry.source = FSAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION;
	entry.is_node = true;
	entry.dependencies = p_dependencies;
	return entry;
}

void check_entry_order(const FSAutoloadIndex &p_index, const Vector<StringName> &p_expected_names) {
	const Vector<FSAutoloadIndexEntry> &entries = p_index.get_entries();
	REQUIRE_EQ(entries.size(), p_expected_names.size());
	for (int i = 0; i < p_expected_names.size(); i++) {
		CHECK_EQ(entries[i].name, p_expected_names[i]);
	}
}

void check_startup_info_order(const Vector<ProjectSettings::AutoloadInfo> &p_infos, const Vector<StringName> &p_expected_names) {
	REQUIRE_EQ(p_infos.size(), p_expected_names.size());
	for (int i = 0; i < p_expected_names.size(); i++) {
		CHECK_EQ(p_infos[i].name, p_expected_names[i]);
	}
}

Error analyze_autoload_source(FSParser &r_parser, const String &p_source, const String &p_path) {
	Error err = r_parser.parse(p_source, p_path, false);
	if (err != OK) {
		return err;
	}

	FSAnalyzer analyzer(&r_parser);
	return analyzer.analyze();
}

Error analyze_autoload_source_with_index(
		FSParser &r_parser,
		const String &p_source,
		const String &p_path,
		FSAutoloadIndex &r_index) {
	Error err = r_parser.parse(p_source, p_path, false);
	if (err != OK) {
		return err;
	}

	FSAnalyzer analyzer(&r_parser);
	err = analyzer.analyze();
	r_index = analyzer.get_autoload_index();
	return err;
}

bool autoload_analyzer_has_error(const FSParser &p_parser, const String &p_expected_error) {
	for (const FSParser::ParserError &parser_error : p_parser.get_errors()) {
		if (parser_error.message == p_expected_error) {
			return true;
		}
	}
	return false;
}

bool autoload_analyzer_has_error_containing(const FSParser &p_parser, const String &p_expected_fragment) {
	for (const FSParser::ParserError &parser_error : p_parser.get_errors()) {
		if (parser_error.message.contains(p_expected_fragment)) {
			return true;
		}
	}
	return false;
}

} // namespace

TEST_CASE("[Modules][FoundryScript] Autoload index builds project settings entries in order") {
	ScopedTempFiles files("fs_autoload_index_order");
	ScopedAutoloadSettings autoloads;

	const String earlier_path = files.write("autoload_earlier.fs",
			"class_name IndexEarlier extends Node\n"
			"func marker() -> int:\n"
			"\treturn 1\n");
	const String later_path = files.write("autoload_later.fs",
			"extends Node\n"
			"func marker() -> int:\n"
			"\treturn 2\n");

	autoloads.set(SNAME("IndexLater"), later_path, true, 40);
	autoloads.set(SNAME("IndexEarlier"), earlier_path, false, 10);

	FSAutoloadIndex index;
	index.rebuild_from_project_settings();

	const Vector<FSAutoloadIndexEntry> &entries = index.get_entries();
	CHECK_EQ(entries.size(), 2);
	if (entries.size() != 2) {
		return;
	}

	CHECK_EQ(entries[0].name, SNAME("IndexEarlier"));
	CHECK_EQ(entries[0].path, earlier_path);
	CHECK_FALSE(entries[0].is_singleton);
	CHECK_EQ(entries[0].order, 10);
	CHECK_EQ(entries[0].source, FSAutoloadIndexEntry::SOURCE_PROJECT_SETTINGS);
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

TEST_CASE("[Modules][FoundryScript] Autoload index preserves project settings prepend order") {
	ScopedTempFiles files("fs_autoload_index_prepend_order");
	ScopedAutoloadSettings autoloads;

	const String normal_path = files.write("autoload_normal.fs", "extends Node\n");
	const String prepended_path = files.write("autoload_prepended.fs", "extends Node\n");

	autoloads.set(SNAME("IndexNormalAutoload"), normal_path, true, 10);
	autoloads.set_prepend(SNAME("IndexPrependedAutoload"), prepended_path, true, 20);

	FSAutoloadIndex index;
	index.rebuild_from_project_settings();

	Vector<StringName> expected_order;
	expected_order.push_back(SNAME("IndexPrependedAutoload"));
	expected_order.push_back(SNAME("IndexNormalAutoload"));
	check_entry_order(index, expected_order);
}

TEST_CASE("[Modules][FoundryScript] Autoload index sorts dependencies before dependents") {
	Vector<FSAutoloadIndexEntry> entries;

	Vector<FSAutoloadIndexDependency> consumer_dependencies;
	consumer_dependencies.push_back(make_dependency(SNAME("IndexDependencyService")));

	entries.push_back(make_dependency_entry(SNAME("IndexDependencyConsumer"), 10, consumer_dependencies));
	entries.push_back(make_dependency_entry(SNAME("IndexDependencyIndependent"), 20));
	entries.push_back(make_dependency_entry(SNAME("IndexDependencyService"), 30));

	FSAutoloadIndex index;
	index.rebuild_from_entries(entries);

	Vector<StringName> expected_order;
	expected_order.push_back(SNAME("IndexDependencyIndependent"));
	expected_order.push_back(SNAME("IndexDependencyService"));
	expected_order.push_back(SNAME("IndexDependencyConsumer"));
	check_entry_order(index, expected_order);
}

TEST_CASE("[Modules][FoundryScript] Autoload index keeps project settings order as dependency tie-breaker") {
	Vector<FSAutoloadIndexEntry> entries;
	entries.push_back(make_dependency_entry(SNAME("IndexTieLater"), 30));
	entries.push_back(make_dependency_entry(SNAME("IndexTieFirst"), 10));
	entries.push_back(make_dependency_entry(SNAME("IndexTieEqualA"), 40));
	entries.push_back(make_dependency_entry(SNAME("IndexTieSecond"), 20));
	entries.push_back(make_dependency_entry(SNAME("IndexTieEqualB"), 40));

	FSAutoloadIndex index;
	index.rebuild_from_entries(entries);

	Vector<StringName> expected_order;
	expected_order.push_back(SNAME("IndexTieFirst"));
	expected_order.push_back(SNAME("IndexTieSecond"));
	expected_order.push_back(SNAME("IndexTieLater"));
	expected_order.push_back(SNAME("IndexTieEqualA"));
	expected_order.push_back(SNAME("IndexTieEqualB"));
	check_entry_order(index, expected_order);
}

TEST_CASE("[Modules][FoundryScript] Autoload index diagnoses dependency cycles with the cycle path") {
	Vector<FSAutoloadIndexEntry> entries;

	Vector<FSAutoloadIndexDependency> a_dependencies;
	a_dependencies.push_back(make_dependency(SNAME("IndexCycleB")));
	Vector<FSAutoloadIndexDependency> b_dependencies;
	b_dependencies.push_back(make_dependency(SNAME("IndexCycleC")));
	Vector<FSAutoloadIndexDependency> c_dependencies;
	c_dependencies.push_back(make_dependency(SNAME("IndexCycleA")));

	entries.push_back(make_dependency_entry(SNAME("IndexCycleA"), 10, a_dependencies));
	entries.push_back(make_dependency_entry(SNAME("IndexCycleB"), 20, b_dependencies));
	entries.push_back(make_dependency_entry(SNAME("IndexCycleC"), 30, c_dependencies));

	FSAutoloadIndex index;
	index.rebuild_from_entries(entries);

	const String expected_cycle = "IndexCycleA -> IndexCycleB -> IndexCycleC -> IndexCycleA";
	const FSAutoloadIndexEntry *a = index.get_by_name(SNAME("IndexCycleA"));
	const FSAutoloadIndexEntry *b = index.get_by_name(SNAME("IndexCycleB"));
	const FSAutoloadIndexEntry *c = index.get_by_name(SNAME("IndexCycleC"));
	REQUIRE(a != nullptr);
	REQUIRE(b != nullptr);
	REQUIRE(c != nullptr);

	CHECK(has_hard_diagnostic_containing(*a, FSAutoloadIndexDiagnostic::CYCLIC_DEPENDENCY, expected_cycle));
	CHECK(has_hard_diagnostic_containing(*b, FSAutoloadIndexDiagnostic::CYCLIC_DEPENDENCY, expected_cycle));
	CHECK(has_hard_diagnostic_containing(*c, FSAutoloadIndexDiagnostic::CYCLIC_DEPENDENCY, expected_cycle));
}

TEST_CASE("[Modules][FoundryScript] Autoload index diagnoses each independent dependency cycle") {
	Vector<FSAutoloadIndexEntry> entries;

	Vector<FSAutoloadIndexDependency> first_a_dependencies;
	first_a_dependencies.push_back(make_dependency(SNAME("IndexFirstCycleB")));
	Vector<FSAutoloadIndexDependency> first_b_dependencies;
	first_b_dependencies.push_back(make_dependency(SNAME("IndexFirstCycleA")));
	Vector<FSAutoloadIndexDependency> second_a_dependencies;
	second_a_dependencies.push_back(make_dependency(SNAME("IndexSecondCycleB")));
	Vector<FSAutoloadIndexDependency> second_b_dependencies;
	second_b_dependencies.push_back(make_dependency(SNAME("IndexSecondCycleA")));

	entries.push_back(make_dependency_entry(SNAME("IndexFirstCycleA"), 10, first_a_dependencies));
	entries.push_back(make_dependency_entry(SNAME("IndexFirstCycleB"), 20, first_b_dependencies));
	entries.push_back(make_dependency_entry(SNAME("IndexSecondCycleA"), 30, second_a_dependencies));
	entries.push_back(make_dependency_entry(SNAME("IndexSecondCycleB"), 40, second_b_dependencies));

	FSAutoloadIndex index;
	index.rebuild_from_entries(entries);

	const String first_cycle = "IndexFirstCycleA -> IndexFirstCycleB -> IndexFirstCycleA";
	const String second_cycle = "IndexSecondCycleA -> IndexSecondCycleB -> IndexSecondCycleA";
	const FSAutoloadIndexEntry *first_a = index.get_by_name(SNAME("IndexFirstCycleA"));
	const FSAutoloadIndexEntry *first_b = index.get_by_name(SNAME("IndexFirstCycleB"));
	const FSAutoloadIndexEntry *second_a = index.get_by_name(SNAME("IndexSecondCycleA"));
	const FSAutoloadIndexEntry *second_b = index.get_by_name(SNAME("IndexSecondCycleB"));
	REQUIRE(first_a != nullptr);
	REQUIRE(first_b != nullptr);
	REQUIRE(second_a != nullptr);
	REQUIRE(second_b != nullptr);

	CHECK(has_hard_diagnostic_containing(*first_a, FSAutoloadIndexDiagnostic::CYCLIC_DEPENDENCY, first_cycle));
	CHECK(has_hard_diagnostic_containing(*first_b, FSAutoloadIndexDiagnostic::CYCLIC_DEPENDENCY, first_cycle));
	CHECK(has_hard_diagnostic_containing(*second_a, FSAutoloadIndexDiagnostic::CYCLIC_DEPENDENCY, second_cycle));
	CHECK(has_hard_diagnostic_containing(*second_b, FSAutoloadIndexDiagnostic::CYCLIC_DEPENDENCY, second_cycle));
}

TEST_CASE("[Modules][FoundryScript] Autoload index diagnoses overlapping dependency cycles") {
	Vector<FSAutoloadIndexEntry> entries;

	Vector<FSAutoloadIndexDependency> a_dependencies;
	a_dependencies.push_back(make_dependency(SNAME("IndexOverlapB")));
	Vector<FSAutoloadIndexDependency> b_dependencies;
	b_dependencies.push_back(make_dependency(SNAME("IndexOverlapA")));
	b_dependencies.push_back(make_dependency(SNAME("IndexOverlapC")));
	Vector<FSAutoloadIndexDependency> c_dependencies;
	c_dependencies.push_back(make_dependency(SNAME("IndexOverlapB")));

	entries.push_back(make_dependency_entry(SNAME("IndexOverlapA"), 10, a_dependencies));
	entries.push_back(make_dependency_entry(SNAME("IndexOverlapB"), 20, b_dependencies));
	entries.push_back(make_dependency_entry(SNAME("IndexOverlapC"), 30, c_dependencies));

	FSAutoloadIndex index;
	index.rebuild_from_entries(entries);

	const String first_cycle = "IndexOverlapA -> IndexOverlapB -> IndexOverlapA";
	const String second_cycle = "IndexOverlapC -> IndexOverlapB -> IndexOverlapC";
	const FSAutoloadIndexEntry *a = index.get_by_name(SNAME("IndexOverlapA"));
	const FSAutoloadIndexEntry *b = index.get_by_name(SNAME("IndexOverlapB"));
	const FSAutoloadIndexEntry *c = index.get_by_name(SNAME("IndexOverlapC"));
	REQUIRE(a != nullptr);
	REQUIRE(b != nullptr);
	REQUIRE(c != nullptr);

	CHECK(has_hard_diagnostic_containing(*a, FSAutoloadIndexDiagnostic::CYCLIC_DEPENDENCY, first_cycle));
	CHECK(has_hard_diagnostic_containing(*b, FSAutoloadIndexDiagnostic::CYCLIC_DEPENDENCY, first_cycle));
	CHECK(has_hard_diagnostic_containing(*c, FSAutoloadIndexDiagnostic::CYCLIC_DEPENDENCY, second_cycle));
}

TEST_CASE("[Modules][FoundryScript] Autoload index keeps downstream dependents after cyclic dependencies") {
	Vector<FSAutoloadIndexEntry> entries;

	Vector<FSAutoloadIndexDependency> dependent_dependencies;
	dependent_dependencies.push_back(make_dependency(SNAME("IndexCyclicDependencyA")));
	Vector<FSAutoloadIndexDependency> a_dependencies;
	a_dependencies.push_back(make_dependency(SNAME("IndexCyclicDependencyB")));
	Vector<FSAutoloadIndexDependency> b_dependencies;
	b_dependencies.push_back(make_dependency(SNAME("IndexCyclicDependencyA")));

	entries.push_back(make_dependency_entry(SNAME("IndexDownstreamDependent"), 10, dependent_dependencies));
	entries.push_back(make_dependency_entry(SNAME("IndexCyclicDependencyA"), 20, a_dependencies));
	entries.push_back(make_dependency_entry(SNAME("IndexCyclicDependencyB"), 30, b_dependencies));

	FSAutoloadIndex index;
	index.rebuild_from_entries(entries);

	Vector<StringName> expected_order;
	expected_order.push_back(SNAME("IndexCyclicDependencyA"));
	expected_order.push_back(SNAME("IndexCyclicDependencyB"));
	expected_order.push_back(SNAME("IndexDownstreamDependent"));
	check_entry_order(index, expected_order);
}

TEST_CASE("[Modules][FoundryScript] Autoload index diagnoses missing and non-autoload dependencies") {
	Vector<FSAutoloadIndexEntry> entries;

	Vector<FSAutoloadIndexDependency> missing_dependencies;
	missing_dependencies.push_back(make_dependency(SNAME("IndexMissingDependency")));
	Vector<FSAutoloadIndexDependency> non_autoload_dependencies;
	non_autoload_dependencies.push_back(make_non_autoload_dependency(SNAME("IndexPlainClassDependency")));

	entries.push_back(make_dependency_entry(SNAME("IndexMissingConsumer"), 10, missing_dependencies));
	entries.push_back(make_dependency_entry(SNAME("IndexPlainConsumer"), 20, non_autoload_dependencies));

	FSAutoloadIndex index;
	index.rebuild_from_entries(entries);

	const FSAutoloadIndexEntry *missing_consumer = index.get_by_name(SNAME("IndexMissingConsumer"));
	const FSAutoloadIndexEntry *plain_consumer = index.get_by_name(SNAME("IndexPlainConsumer"));
	REQUIRE(missing_consumer != nullptr);
	REQUIRE(plain_consumer != nullptr);

	CHECK(has_hard_diagnostic_containing(*missing_consumer, FSAutoloadIndexDiagnostic::MISSING_DEPENDENCY, "IndexMissingDependency"));
	CHECK(has_hard_diagnostic_containing(*plain_consumer, FSAutoloadIndexDiagnostic::NON_AUTOLOAD_DEPENDENCY, "IndexPlainClassDependency"));
}

TEST_CASE("[Modules][FoundryScript] Autoload index records project settings diagnostics") {
	ScopedTempFiles files("fs_autoload_index_diagnostics");
	ScopedAutoloadSettings autoloads;

	const String node_path = files.write("valid_node.fs", "class_name IndexNode extends Node\n");
	const String non_node_path = files.write("plain_resource.fs", "class_name IndexPlain extends RefCounted\n");
	const String text_path = files.write("notes.txt", "not a script or a scene\n");
	const String unrelated_path = files.write("unrelated_autoload.fs", "class_name IndexOther extends Node\n");
	const String registered_path = files.write("registered_global.fs", "class_name IndexCollision extends Node\n");

	ScopedScriptServerClass registered_collision(SNAME("IndexCollision"), "Node", registered_path);

	autoloads.set(SNAME("IndexMissing"), files.missing("missing.fs"), true, 10);
	autoloads.set(SNAME("IndexText"), text_path, true, 20);
	autoloads.set(SNAME("IndexPlain"), non_node_path, true, 30);
	autoloads.set(SNAME("foundry"), node_path, true, 40);
	autoloads.set(SNAME("IndexCollision"), unrelated_path, true, 50);

	FSAutoloadIndex index;
	index.rebuild_from_project_settings();

	CHECK(index.get_by_name(SNAME("IndexMissing")) != nullptr);
	if (index.get_by_name(SNAME("IndexMissing")) == nullptr) {
		return;
	}
	CHECK(has_diagnostic(*index.get_by_name(SNAME("IndexMissing")), FSAutoloadIndexDiagnostic::MISSING_PATH));

	CHECK(index.get_by_name(SNAME("IndexText")) != nullptr);
	if (index.get_by_name(SNAME("IndexText")) == nullptr) {
		return;
	}
	CHECK(has_diagnostic(*index.get_by_name(SNAME("IndexText")), FSAutoloadIndexDiagnostic::NON_SCRIPT_NON_SCENE_PATH));

	CHECK(index.get_by_name(SNAME("IndexPlain")) != nullptr);
	if (index.get_by_name(SNAME("IndexPlain")) == nullptr) {
		return;
	}
	CHECK_EQ(index.get_by_name(SNAME("IndexPlain"))->native_base, SNAME("RefCounted"));
	CHECK_FALSE(index.get_by_name(SNAME("IndexPlain"))->is_node);
	CHECK(has_diagnostic(*index.get_by_name(SNAME("IndexPlain")), FSAutoloadIndexDiagnostic::NON_NODE_SCRIPT));

	CHECK(index.get_by_name(SNAME("foundry")) != nullptr);
	if (index.get_by_name(SNAME("foundry")) == nullptr) {
		return;
	}
	CHECK(has_diagnostic(*index.get_by_name(SNAME("foundry")), FSAutoloadIndexDiagnostic::RESERVED_GLOBAL_NAME_COLLISION));

	CHECK(index.get_by_name(SNAME("IndexCollision")) != nullptr);
	if (index.get_by_name(SNAME("IndexCollision")) == nullptr) {
		return;
	}
	CHECK(has_diagnostic(*index.get_by_name(SNAME("IndexCollision")), FSAutoloadIndexDiagnostic::UNRELATED_GLOBAL_CLASS_COLLISION));
}

TEST_CASE("[Modules][FoundryScript] Autoload index allows same-script class and autoload names") {
	ScopedTempFiles files("fs_autoload_index_same_script");
	ScopedAutoloadSettings autoloads;

	const String same_path = files.write("same_script.fs", "class_name IndexSame extends Node\n");
	ScopedScriptServerClass registered_same(SNAME("IndexSame"), "Node", same_path);

	autoloads.set(SNAME("IndexSame"), same_path, true, 10);

	FSAutoloadIndex index;
	index.rebuild_from_project_settings();

	const FSAutoloadIndexEntry *entry = index.get_by_name(SNAME("IndexSame"));
	CHECK(entry != nullptr);
	if (entry == nullptr) {
		return;
	}
	CHECK_EQ(entry->global_class_name, SNAME("IndexSame"));
	CHECK(entry->is_same_script_global_class);
	CHECK_FALSE(has_diagnostic(*entry, FSAutoloadIndexDiagnostic::UNRELATED_GLOBAL_CLASS_COLLISION));
	CHECK(index.get_by_global_class(SNAME("IndexSame")) != nullptr);
	if (index.get_by_global_class(SNAME("IndexSame")) == nullptr) {
		return;
	}
	CHECK_EQ(index.get_by_global_class(SNAME("IndexSame"))->path, same_path);
}

TEST_CASE("[Modules][FoundryScript] Analyzer extracts script-owned autoload entries") {
	ScopedTempFiles files("fs_analyzer_autoload_annotation_valid");
	ScopedAutoloadSettings autoloads;

	const String save_source =
			"@autoload\n"
			"class_name AnalyzerAnnotationSaveManager extends Node\n";
	const String save_path = files.write("save_manager.fs", save_source);
	ScopedScriptServerClass registered_save(SNAME("AnalyzerAnnotationSaveManager"), "Node", save_path);

	const String event_source =
			"@autoload(depends_on = [AnalyzerAnnotationSaveManager], order_id = 10 + 5)\n"
			"class_name AnalyzerAnnotationEventBus extends Node\n";
	const String event_path = files.write("event_bus.fs", event_source);
	ScopedScriptServerClass registered_event(SNAME("AnalyzerAnnotationEventBus"), "Node", event_path);

	FSParser save_parser;
	FSAutoloadIndex save_index;
	CHECK_EQ(analyze_autoload_source_with_index(save_parser, save_source, save_path, save_index), OK);
	const FSAutoloadIndexEntry *save_entry = save_index.get_by_name(SNAME("AnalyzerAnnotationSaveManager"));
	REQUIRE(save_entry != nullptr);
	CHECK_EQ(save_entry->source, FSAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION);
	CHECK_EQ(save_entry->name, SNAME("AnalyzerAnnotationSaveManager"));
	CHECK_EQ(save_entry->path, save_path);
	CHECK(save_entry->is_singleton);
	CHECK(save_entry->is_node);
	CHECK(save_entry->dependencies.is_empty());

	FSParser event_parser;
	FSAutoloadIndex event_index;
	CHECK_EQ(analyze_autoload_source_with_index(event_parser, event_source, event_path, event_index), OK);
	const FSAutoloadIndexEntry *event_entry = event_index.get_by_name(SNAME("AnalyzerAnnotationEventBus"));
	REQUIRE(event_entry != nullptr);
	CHECK_EQ(event_entry->source, FSAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION);
	CHECK_EQ(event_entry->name, SNAME("AnalyzerAnnotationEventBus"));
	CHECK_EQ(event_entry->path, event_path);
	CHECK(event_entry->is_singleton);
	CHECK(event_entry->is_node);
	CHECK_EQ(event_entry->global_class_name, SNAME("AnalyzerAnnotationEventBus"));
	CHECK_EQ(event_entry->script_path, event_path);
	CHECK_EQ(event_entry->native_base, SNAME("Node"));
	CHECK_EQ(event_entry->order, 15);
	REQUIRE_EQ(event_entry->dependencies.size(), 1);
	CHECK_EQ(event_entry->dependencies[0].name, SNAME("AnalyzerAnnotationSaveManager"));
	CHECK(event_entry->dependencies[0].is_autoload);

	Vector<FSAutoloadIndexEntry> entries;
	entries.push_back(*event_entry);
	entries.push_back(*save_entry);

	FSAutoloadIndex combined;
	combined.rebuild_from_entries(entries);

	Vector<StringName> expected_order;
	expected_order.push_back(SNAME("AnalyzerAnnotationSaveManager"));
	expected_order.push_back(SNAME("AnalyzerAnnotationEventBus"));
	check_entry_order(combined, expected_order);
}

TEST_CASE("[Modules][FoundryScript] Analyzer indexes namespaced autoloads under simple singleton names") {
	ScopedTempFiles files("fs_analyzer_autoload_annotation_namespace");
	ScopedAutoloadSettings autoloads;

	const String source =
			"@autoload\n"
			"namespace AnalyzerAnnotationNamespace\n"
			"class_name NamespacedAutoload extends Node\n";
	const String path = files.write("namespaced_autoload.fs", source);
	ScopedScriptServerClass registered_namespaced(
			SNAME("AnalyzerAnnotationNamespace.NamespacedAutoload"),
			"Node",
			path);

	FSParser parser;
	FSAutoloadIndex index;
	const Error err = analyze_autoload_source_with_index(parser, source, path, index);
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}
	CHECK(index.get_by_name(SNAME("AnalyzerAnnotationNamespace.NamespacedAutoload")) == nullptr);

	const FSAutoloadIndexEntry *entry = index.get_by_name(SNAME("NamespacedAutoload"));
	CHECK(entry != nullptr);
	if (entry == nullptr) {
		return;
	}
	CHECK_EQ(entry->name, SNAME("NamespacedAutoload"));
	CHECK_EQ(entry->global_class_name, SNAME("AnalyzerAnnotationNamespace.NamespacedAutoload"));
	CHECK_EQ(entry->path, path);
	CHECK_EQ(index.get_by_global_class(SNAME("AnalyzerAnnotationNamespace.NamespacedAutoload")), entry);
}

TEST_CASE("[Modules][FoundryScript] Analyzer resolves namespaced autoload dependencies through global classes") {
	ScopedTempFiles files("fs_analyzer_autoload_annotation_namespaced_dependency");
	ScopedAutoloadSettings autoloads;

	const String save_source =
			"@autoload\n"
			"namespace AnalyzerAnnotationDependencyNamespace\n"
			"class_name NamespacedSave extends Node\n";
	const String save_path = files.write("namespaced_save.fs", save_source);
	ScopedScriptServerClass registered_save(
			SNAME("AnalyzerAnnotationDependencyNamespace.NamespacedSave"),
			"Node",
			save_path);

	const String event_source =
			"@autoload(depends_on = [NamespacedSave])\n"
			"namespace AnalyzerAnnotationDependencyNamespace\n"
			"class_name NamespacedEvent extends Node\n";
	const String event_path = files.write("namespaced_event.fs", event_source);
	ScopedScriptServerClass registered_event(
			SNAME("AnalyzerAnnotationDependencyNamespace.NamespacedEvent"),
			"Node",
			event_path);

	FSParser save_parser;
	FSAutoloadIndex save_index;
	CHECK_EQ(analyze_autoload_source_with_index(save_parser, save_source, save_path, save_index), OK);
	const FSAutoloadIndexEntry *save_entry = save_index.get_by_name(SNAME("NamespacedSave"));
	REQUIRE(save_entry != nullptr);

	FSParser event_parser;
	FSAutoloadIndex event_index;
	CHECK_EQ(analyze_autoload_source_with_index(event_parser, event_source, event_path, event_index), OK);
	const FSAutoloadIndexEntry *event_entry = event_index.get_by_name(SNAME("NamespacedEvent"));
	REQUIRE(event_entry != nullptr);
	REQUIRE_EQ(event_entry->dependencies.size(), 1);
	CHECK_EQ(event_entry->dependencies[0].name, SNAME("AnalyzerAnnotationDependencyNamespace.NamespacedSave"));

	Vector<FSAutoloadIndexEntry> entries;
	entries.push_back(*event_entry);
	entries.push_back(*save_entry);

	FSAutoloadIndex combined;
	combined.rebuild_from_entries(entries);

	const FSAutoloadIndexEntry *combined_event = combined.get_by_name(SNAME("NamespacedEvent"));
	REQUIRE(combined_event != nullptr);
	CHECK_FALSE(has_diagnostic(*combined_event, FSAutoloadIndexDiagnostic::MISSING_DEPENDENCY));

	Vector<StringName> expected_order;
	expected_order.push_back(SNAME("NamespacedSave"));
	expected_order.push_back(SNAME("NamespacedEvent"));
	check_entry_order(combined, expected_order);
}

TEST_CASE("[Modules][FoundryScript] Analyzer populates script-owned autoload name diagnostics") {
	ScopedTempFiles files("fs_analyzer_autoload_annotation_name_diagnostics");
	ScopedAutoloadSettings autoloads;

	const String reserved_source =
			"@autoload\n"
			"class_name foundry extends Node\n";
	const String reserved_path = files.write("reserved.fs", reserved_source);

	FSParser reserved_parser;
	FSAutoloadIndex reserved_index;
	const Error reserved_err = analyze_autoload_source_with_index(
			reserved_parser,
			reserved_source,
			reserved_path,
			reserved_index);
	CHECK_EQ(reserved_err, OK);
	if (reserved_err != OK) {
		return;
	}

	const FSAutoloadIndexEntry *reserved_entry = reserved_index.get_by_name(SNAME("foundry"));
	REQUIRE(reserved_entry != nullptr);
	CHECK(has_diagnostic(*reserved_entry, FSAutoloadIndexDiagnostic::RESERVED_GLOBAL_NAME_COLLISION));

	const String unrelated_global_path = files.write("simple_global.fs",
			"class_name AnalyzerAnnotationSimpleCollision extends Node\n");
	ScopedScriptServerClass registered_unrelated_simple(
			SNAME("AnalyzerAnnotationSimpleCollision"),
			"Node",
			unrelated_global_path);

	const String namespaced_collision_source =
			"@autoload\n"
			"namespace AnalyzerAnnotationCollisionNamespace\n"
			"class_name AnalyzerAnnotationSimpleCollision extends Node\n";
	const String namespaced_collision_path = files.write("namespaced_collision.fs", namespaced_collision_source);
	ScopedScriptServerClass registered_namespaced_collision(
			SNAME("AnalyzerAnnotationCollisionNamespace.AnalyzerAnnotationSimpleCollision"),
			"Node",
			namespaced_collision_path);

	FSParser collision_parser;
	FSAutoloadIndex collision_index;
	const Error collision_err = analyze_autoload_source_with_index(
			collision_parser,
			namespaced_collision_source,
			namespaced_collision_path,
			collision_index);
	CHECK_EQ(collision_err, OK);
	if (collision_err != OK) {
		return;
	}

	const FSAutoloadIndexEntry *collision_entry =
			collision_index.get_by_name(SNAME("AnalyzerAnnotationSimpleCollision"));
	REQUIRE(collision_entry != nullptr);
	CHECK(has_diagnostic(*collision_entry, FSAutoloadIndexDiagnostic::UNRELATED_GLOBAL_CLASS_COLLISION));
}

TEST_CASE("[Modules][FoundryScript] Autoload annotation rejects invalid declarations") {
	ScopedTempFiles files("fs_analyzer_autoload_annotation_invalid");

	const String invalid_target_path = files.write("invalid_target.fs",
			"class_name AnalyzerAnnotationInvalidTarget extends Node\n"
			"@autoload\n"
			"var value = 1\n");
	FSParser invalid_target_parser;
	CHECK_NE(analyze_autoload_source(invalid_target_parser,
					 "class_name AnalyzerAnnotationInvalidTarget extends Node\n"
					 "@autoload\n"
					 "var value = 1\n",
					 invalid_target_path),
			OK);
	CHECK(autoload_analyzer_has_error_containing(
			invalid_target_parser,
			R"(Annotation "@autoload" must be at the top of the script)"));

	const String missing_class_name_path = files.write("missing_class_name.fs",
			"@autoload\n"
			"extends Node\n");
	FSParser missing_class_name_parser;
	CHECK_NE(analyze_autoload_source(missing_class_name_parser,
					 "@autoload\n"
					 "extends Node\n",
					 missing_class_name_path),
			OK);
	CHECK(autoload_analyzer_has_error(missing_class_name_parser, R"("@autoload" requires "class_name".)"));

	const String trait_name_path = files.write("trait_name.fs",
			"@autoload\n"
			"trait_name AnalyzerAnnotationAutoloadTrait\n");
	FSParser trait_name_parser;
	CHECK_NE(analyze_autoload_source(trait_name_parser,
					 "@autoload\n"
					 "trait_name AnalyzerAnnotationAutoloadTrait\n",
					 trait_name_path),
			OK);
	CHECK(autoload_analyzer_has_error(trait_name_parser, R"("@autoload" requires "class_name".)"));

	const String non_node_path = files.write("non_node.fs",
			"@autoload\n"
			"class_name AnalyzerAnnotationPlainAutoload extends RefCounted\n");
	FSParser non_node_parser;
	CHECK_NE(analyze_autoload_source(non_node_parser,
					 "@autoload\n"
					 "class_name AnalyzerAnnotationPlainAutoload extends RefCounted\n",
					 non_node_path),
			OK);
	CHECK(autoload_analyzer_has_error(non_node_parser, R"("@autoload" requires the script to inherit from "Node".)"));

	const String order_overflow_path = files.write("order_overflow.fs",
			"@autoload(order_id = 1 << 40)\n"
			"class_name AnalyzerAnnotationOrderOverflow extends Node\n");
	FSParser order_overflow_parser;
	CHECK_NE(analyze_autoload_source(order_overflow_parser,
					 "@autoload(order_id = 1 << 40)\n"
					 "class_name AnalyzerAnnotationOrderOverflow extends Node\n",
					 order_overflow_path),
			OK);
	CHECK(autoload_analyzer_has_error(
			order_overflow_parser,
			R"("order_id" of annotation "@autoload" must fit in a 32-bit signed integer.)"));

	const String shadowed_dependency_path = files.write("shadowed_dependency.fs",
			"class_name AnalyzerAnnotationShadowedDependency extends Node\n");
	ScopedScriptServerClass registered_shadowed_dependency(
			SNAME("AnalyzerAnnotationShadowedDependency"),
			"Node",
			shadowed_dependency_path);
	const String shadowed_dependency_consumer_path = files.write("shadowed_dependency_consumer.fs",
			"@autoload(depends_on = [AnalyzerAnnotationShadowedDependency])\n"
			"class_name AnalyzerAnnotationShadowConsumer extends Node\n"
			"const AnalyzerAnnotationShadowedDependency = 1\n");
	FSParser shadowed_dependency_parser;
	CHECK_NE(analyze_autoload_source(shadowed_dependency_parser,
					 "@autoload(depends_on = [AnalyzerAnnotationShadowedDependency])\n"
					 "class_name AnalyzerAnnotationShadowConsumer extends Node\n"
					 "const AnalyzerAnnotationShadowedDependency = 1\n",
					 shadowed_dependency_consumer_path),
			OK);
	CHECK(autoload_analyzer_has_error_containing(
			shadowed_dependency_parser,
			R"(Dependency 1 of annotation "@autoload" must resolve to a script class.)"));
}

TEST_CASE("[Modules][FoundryScript] Autoload index merges compatible migration entries") {
	Vector<FSAutoloadIndexDependency> dependencies;
	dependencies.push_back(make_dependency(SNAME("IndexMigrationDependency")));

	FSAutoloadIndexEntry project_entry = make_dependency_entry(SNAME("IndexMigrationSame"), 40);
	project_entry.path = "res://migration_same.fs";
	project_entry.source = FSAutoloadIndexEntry::SOURCE_PROJECT_SETTINGS;

	FSAutoloadIndexEntry script_entry = make_dependency_entry(SNAME("IndexMigrationSame"), 10, dependencies);
	script_entry.path = "res://migration_same.fs";
	script_entry.source = FSAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION;

	Vector<FSAutoloadIndexEntry> entries;
	entries.push_back(project_entry);
	entries.push_back(script_entry);

	FSAutoloadIndex index;
	index.rebuild_from_entries(entries);

	const Vector<FSAutoloadIndexEntry> &indexed_entries = index.get_entries();
	REQUIRE_EQ(indexed_entries.size(), 1);
	const FSAutoloadIndexEntry *entry = index.get_by_name(SNAME("IndexMigrationSame"));
	REQUIRE(entry != nullptr);
	CHECK_EQ(entry->path, "res://migration_same.fs");
	REQUIRE_EQ(entry->dependencies.size(), 1);
	CHECK_EQ(entry->dependencies[0].name, SNAME("IndexMigrationDependency"));
	CHECK_FALSE(has_diagnostic(*entry, FSAutoloadIndexDiagnostic::CONFLICTING_AUTOLOAD_PATH));
}

TEST_CASE("[Modules][FoundryScript] Autoload index diagnoses same-name different-path migration conflicts") {
	FSAutoloadIndexEntry project_entry = make_dependency_entry(SNAME("IndexMigrationConflict"), 10);
	project_entry.path = "res://migration_settings.fs";
	project_entry.source = FSAutoloadIndexEntry::SOURCE_PROJECT_SETTINGS;

	FSAutoloadIndexEntry script_entry = make_dependency_entry(SNAME("IndexMigrationConflict"), 20);
	script_entry.path = "res://migration_script.fs";
	script_entry.source = FSAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION;

	Vector<FSAutoloadIndexEntry> entries;
	entries.push_back(project_entry);
	entries.push_back(script_entry);

	FSAutoloadIndex index;
	index.rebuild_from_entries(entries);

	const Vector<FSAutoloadIndexEntry> &indexed_entries = index.get_entries();
	REQUIRE_EQ(indexed_entries.size(), 1);
	const FSAutoloadIndexEntry *entry = index.get_by_name(SNAME("IndexMigrationConflict"));
	REQUIRE(entry != nullptr);
	CHECK_EQ(entry->source, FSAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION);
	CHECK_EQ(entry->path, "res://migration_script.fs");
	CHECK(has_hard_diagnostic_containing(
			*entry,
			FSAutoloadIndexDiagnostic::CONFLICTING_AUTOLOAD_PATH,
			"res://migration_settings.fs"));
	CHECK(has_hard_diagnostic_containing(
			*entry,
			FSAutoloadIndexDiagnostic::CONFLICTING_AUTOLOAD_PATH,
			"res://migration_script.fs"));
}

TEST_CASE("[Modules][FoundryScript] Autoload index saves and loads script-owned runtime metadata") {
	ScopedTempFiles files("fs_autoload_index_cache_roundtrip");

	Vector<FSAutoloadIndexDependency> consumer_dependencies;
	consumer_dependencies.push_back(make_dependency(SNAME("IndexCachedService")));

	FSAutoloadIndexEntry consumer = make_dependency_entry(SNAME("IndexCachedConsumer"), 5, consumer_dependencies);
	consumer.path = "res://exported/missing_consumer.fs";
	consumer.script_path = consumer.path;
	consumer.global_class_name = SNAME("IndexCachedNamespace.IndexCachedConsumer");
	consumer.native_base = SNAME("Node");
	consumer.is_tool = true;
	consumer.is_same_script_global_class = false;

	FSAutoloadIndexEntry service = make_dependency_entry(SNAME("IndexCachedService"), 10);
	service.path = "res://exported/missing_service.fs";
	service.script_path = service.path;
	service.global_class_name = SNAME("IndexCachedService");
	service.native_base = SNAME("Node");
	service.is_same_script_global_class = true;

	Vector<FSAutoloadIndexEntry> entries;
	entries.push_back(consumer);
	entries.push_back(service);

	FSAutoloadIndex saved;
	saved.rebuild_from_entries(entries);

	const String cache_path = files.reserve("autoload_index_cache.cfg");
	CHECK_EQ(saved.save_to_cache(cache_path), OK);

	FSAutoloadIndex loaded;
	CHECK_EQ(loaded.load_from_cache(cache_path), OK);

	Vector<StringName> expected_order;
	expected_order.push_back(SNAME("IndexCachedService"));
	expected_order.push_back(SNAME("IndexCachedConsumer"));
	check_entry_order(loaded, expected_order);

	const FSAutoloadIndexEntry *loaded_consumer = loaded.get_by_name(SNAME("IndexCachedConsumer"));
	REQUIRE(loaded_consumer != nullptr);
	CHECK_EQ(loaded_consumer->source, FSAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION);
	CHECK_EQ(loaded_consumer->path, "res://exported/missing_consumer.fs");
	CHECK_EQ(loaded_consumer->script_path, "res://exported/missing_consumer.fs");
	CHECK_EQ(loaded_consumer->global_class_name, SNAME("IndexCachedNamespace.IndexCachedConsumer"));
	CHECK_EQ(loaded_consumer->native_base, SNAME("Node"));
	CHECK(loaded_consumer->is_node);
	CHECK(loaded_consumer->is_tool);
	CHECK_FALSE(loaded_consumer->is_same_script_global_class);
	REQUIRE_EQ(loaded_consumer->dependencies.size(), 1);
	CHECK_EQ(loaded_consumer->dependencies[0].name, SNAME("IndexCachedService"));
	CHECK_FALSE(has_diagnostic(*loaded_consumer, FSAutoloadIndexDiagnostic::MISSING_PATH));
}

TEST_CASE("[Modules][FoundryScript] Autoload index merges runtime cache with project settings compatibility entries") {
	ScopedTempFiles files("fs_autoload_index_runtime_merge");
	ScopedAutoloadSettings autoloads;

	FSAutoloadIndexEntry cached = make_dependency_entry(SNAME("IndexRuntimeCached"), 20);
	cached.path = "res://runtime_cached.fs";
	cached.script_path = cached.path;
	cached.global_class_name = SNAME("IndexRuntimeCached");
	cached.native_base = SNAME("Node");
	cached.is_same_script_global_class = true;

	Vector<FSAutoloadIndexEntry> cache_entries;
	cache_entries.push_back(cached);

	FSAutoloadIndex saved;
	saved.rebuild_from_entries(cache_entries);
	const String cache_path = files.reserve("runtime_autoload_index_cache.cfg");
	CHECK_EQ(saved.save_to_cache(cache_path), OK);

	const String legacy_path = files.write("legacy_autoload.fs", "extends Node\n");
	autoloads.set(SNAME("IndexRuntimeLegacy"), legacy_path, true, 10);

	FSAutoloadIndex runtime;
	CHECK_EQ(runtime.rebuild_from_cache_and_project_settings(cache_path), OK);

	Vector<StringName> expected_order;
	expected_order.push_back(SNAME("IndexRuntimeLegacy"));
	expected_order.push_back(SNAME("IndexRuntimeCached"));
	check_entry_order(runtime, expected_order);

	Vector<ProjectSettings::AutoloadInfo> startup_infos = runtime.get_startup_autoloads();
	check_startup_info_order(startup_infos, expected_order);
	CHECK_EQ(startup_infos[0].path, legacy_path);
	CHECK_EQ(startup_infos[1].path, "res://runtime_cached.fs");
}

TEST_CASE("[Modules][FoundryScript] Runtime merge prefers current project settings over stale cached settings") {
	ScopedTempFiles files("fs_autoload_index_runtime_stale_settings");
	ScopedAutoloadSettings autoloads;

	FSAutoloadIndexEntry stale_settings = make_dependency_entry(SNAME("IndexRuntimeCurrentSettings"), 5);
	stale_settings.path = "res://stale_cached_settings.fs";
	stale_settings.source = FSAutoloadIndexEntry::SOURCE_PROJECT_SETTINGS;

	FSAutoloadIndexEntry cached_script = make_dependency_entry(SNAME("IndexRuntimeCachedScriptOwned"), 10);
	cached_script.path = "res://cached_script_owned.fs";
	cached_script.source = FSAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION;

	Vector<FSAutoloadIndexEntry> cache_entries;
	cache_entries.push_back(stale_settings);
	cache_entries.push_back(cached_script);

	FSAutoloadIndex saved;
	saved.rebuild_from_entries(cache_entries);
	const String cache_path = files.reserve("runtime_stale_settings_autoload_index_cache.cfg");
	CHECK_EQ(saved.save_to_cache(cache_path), OK);

	const String current_settings_path = files.write("current_settings.fs", "extends Node\n");
	autoloads.set(SNAME("IndexRuntimeCurrentSettings"), current_settings_path, true, 1);

	FSAutoloadIndex runtime;
	CHECK_EQ(runtime.rebuild_from_cache_and_project_settings(cache_path), OK);

	const FSAutoloadIndexEntry *current_settings = runtime.get_by_name(SNAME("IndexRuntimeCurrentSettings"));
	REQUIRE(current_settings != nullptr);
	CHECK_EQ(current_settings->source, FSAutoloadIndexEntry::SOURCE_PROJECT_SETTINGS);
	CHECK_EQ(current_settings->path, current_settings_path);
	CHECK_FALSE(has_diagnostic(*current_settings, FSAutoloadIndexDiagnostic::CONFLICTING_AUTOLOAD_PATH));

	const FSAutoloadIndexEntry *script_owned = runtime.get_by_name(SNAME("IndexRuntimeCachedScriptOwned"));
	REQUIRE(script_owned != nullptr);
	CHECK_EQ(script_owned->source, FSAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION);
	CHECK_EQ(script_owned->path, "res://cached_script_owned.fs");
}

TEST_CASE("[Modules][FoundryScript] Runtime autoload metadata preserves dependency startup order") {
	ScopedTempFiles files("fs_autoload_index_runtime_dependency_order");

	Vector<FSAutoloadIndexDependency> consumer_dependencies;
	consumer_dependencies.push_back(make_dependency(SNAME("IndexRuntimeDependency")));

	FSAutoloadIndexEntry consumer = make_dependency_entry(SNAME("IndexRuntimeDependent"), 5, consumer_dependencies);
	consumer.path = "res://runtime_dependent.fs";
	FSAutoloadIndexEntry dependency = make_dependency_entry(SNAME("IndexRuntimeDependency"), 50);
	dependency.path = "res://runtime_dependency.fs";

	Vector<FSAutoloadIndexEntry> cache_entries;
	cache_entries.push_back(consumer);
	cache_entries.push_back(dependency);

	FSAutoloadIndex saved;
	saved.rebuild_from_entries(cache_entries);
	const String cache_path = files.reserve("runtime_dependency_autoload_index_cache.cfg");
	CHECK_EQ(saved.save_to_cache(cache_path), OK);

	FSAutoloadIndex runtime;
	CHECK_EQ(runtime.rebuild_from_cache_and_project_settings(cache_path), OK);

	Vector<StringName> expected_order;
	expected_order.push_back(SNAME("IndexRuntimeDependency"));
	expected_order.push_back(SNAME("IndexRuntimeDependent"));
	check_entry_order(runtime, expected_order);
	check_startup_info_order(runtime.get_startup_autoloads(), expected_order);
}

TEST_CASE("[Modules][FoundryScript] Script-owned runtime autoload metadata wins project-settings conflicts") {
	ScopedTempFiles files("fs_autoload_index_runtime_conflict_precedence");
	ScopedAutoloadSettings autoloads;

	FSAutoloadIndexEntry script_entry = make_dependency_entry(SNAME("IndexRuntimeConflict"), 10);
	script_entry.path = "res://script_owned_conflict.fs";
	script_entry.script_path = script_entry.path;
	script_entry.global_class_name = SNAME("IndexRuntimeConflict");

	Vector<FSAutoloadIndexEntry> cache_entries;
	cache_entries.push_back(script_entry);

	FSAutoloadIndex saved;
	saved.rebuild_from_entries(cache_entries);
	const String cache_path = files.reserve("runtime_conflict_autoload_index_cache.cfg");
	CHECK_EQ(saved.save_to_cache(cache_path), OK);

	const String settings_path = files.write("settings_conflict.fs", "extends Node\n");
	autoloads.set(SNAME("IndexRuntimeConflict"), settings_path, true, 5);

	FSAutoloadIndex runtime;
	CHECK_EQ(runtime.rebuild_from_cache_and_project_settings(cache_path), OK);

	const FSAutoloadIndexEntry *entry = runtime.get_by_name(SNAME("IndexRuntimeConflict"));
	REQUIRE(entry != nullptr);
	CHECK_EQ(entry->source, FSAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION);
	CHECK_EQ(entry->path, "res://script_owned_conflict.fs");
	CHECK(has_hard_diagnostic_containing(
			*entry,
			FSAutoloadIndexDiagnostic::CONFLICTING_AUTOLOAD_PATH,
			"res://script_owned_conflict.fs"));
	CHECK(has_hard_diagnostic_containing(
			*entry,
			FSAutoloadIndexDiagnostic::CONFLICTING_AUTOLOAD_PATH,
			settings_path));

	Vector<ProjectSettings::AutoloadInfo> startup_infos = runtime.get_startup_autoloads();
	REQUIRE_EQ(startup_infos.size(), 1);
	CHECK_EQ(startup_infos[0].name, SNAME("IndexRuntimeConflict"));
	CHECK_EQ(startup_infos[0].path, "res://script_owned_conflict.fs");
}

TEST_CASE("[Modules][FoundryScript] Runtime startup registers index autoloads for compiler compatibility") {
	ScopedAutoloadSettings autoloads;
	autoloads.track(SNAME("IndexRuntimeCompilerVisible"));

	FSAutoloadIndexEntry script_entry = make_dependency_entry(SNAME("IndexRuntimeCompilerVisible"), 10);
	script_entry.path = "res://script_owned_compiler_visible.fs";
	script_entry.script_path = script_entry.path;
	script_entry.global_class_name = SNAME("IndexRuntimeCompilerVisible");

	Vector<FSAutoloadIndexEntry> entries;
	entries.push_back(script_entry);

	FSAutoloadIndex runtime;
	runtime.rebuild_from_entries(entries);
	runtime.register_startup_autoloads_in_project_settings();

	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	REQUIRE(project_settings->has_autoload(SNAME("IndexRuntimeCompilerVisible")));
	const ProjectSettings::AutoloadInfo info = project_settings->get_autoload(SNAME("IndexRuntimeCompilerVisible"));
	CHECK_EQ(info.name, SNAME("IndexRuntimeCompilerVisible"));
	CHECK_EQ(info.path, "res://script_owned_compiler_visible.fs");
	CHECK(info.is_singleton);
}

#ifdef TOOLS_ENABLED
TEST_CASE("[Modules][FoundryScript] Runtime startup preparation rebuilds script annotations when cache is absent") {
	ScopedTempFiles files("fs_autoload_index_runtime_tool_rebuild");

	const String source =
			"@autoload\n"
			"class_name IndexRuntimeToolCached extends Node\n";
	const String script_path = files.write("runtime_tool_cached.fs", source);
	ScopedScriptServerClass registered_tool_cached(SNAME("IndexRuntimeToolCached"), "Node", script_path);

	const String cache_path = files.reserve("runtime_tool_autoload_index_cache.cfg");
	CHECK_FALSE(FileAccess::exists(cache_path));

	FSAutoloadIndex runtime;
	CHECK_EQ(runtime.rebuild_for_runtime_startup(cache_path), OK);
	CHECK(runtime.has_autoload(SNAME("IndexRuntimeToolCached")));
	CHECK(FileAccess::exists(cache_path));

	FSAutoloadIndex loaded;
	CHECK_EQ(loaded.load_from_cache(cache_path), OK);
	CHECK(loaded.has_autoload(SNAME("IndexRuntimeToolCached")));
}

TEST_CASE("[Modules][FoundryScript] Script-owned autoload cache discovery does not require body analysis") {
	ScopedTempFiles files("fs_autoload_index_export_body_reference");

	const String service_source =
			"@autoload(order_id = 1)\n"
			"class_name IndexExportBodyService extends Node\n"
			"func ping() -> void:\n"
			"\tpass\n";
	const String consumer_source =
			"@autoload(order_id = 2)\n"
			"class_name IndexExportBodyConsumer extends Node\n"
			"func _ready() -> void:\n"
			"\tIndexExportBodyService.ping()\n";

	const String service_path = files.write("export_body_service.fs", service_source);
	const String consumer_path = files.write("export_body_consumer.fs", consumer_source);
	ScopedScriptServerClass registered_service(SNAME("IndexExportBodyService"), "Node", service_path);
	ScopedScriptServerClass registered_consumer(SNAME("IndexExportBodyConsumer"), "Node", consumer_path);

	FSCache::remove_parser(service_path);
	FSCache::remove_parser(consumer_path);

	FSAutoloadIndex index;
	CHECK_EQ(index.rebuild_from_project_settings_and_script_annotations(), OK);

	CHECK(index.has_autoload(SNAME("IndexExportBodyService")));
	CHECK(index.has_autoload(SNAME("IndexExportBodyConsumer")));

	const String cache_path = files.reserve("export_body_reference_autoload_index_cache.cfg");
	CHECK_EQ(index.save_to_cache(cache_path), OK);

	FSAutoloadIndex loaded;
	CHECK_EQ(loaded.load_from_cache(cache_path), OK);
	CHECK(loaded.has_autoload(SNAME("IndexExportBodyService")));
	CHECK(loaded.has_autoload(SNAME("IndexExportBodyConsumer")));
}

TEST_CASE("[Modules][FoundryScript] Script-owned autoload cache discovery wins project-settings conflicts") {
	ScopedTempFiles files("fs_autoload_index_export_conflict_precedence");
	ScopedAutoloadSettings autoloads;

	const String settings_path = files.write("export_conflict_settings.fs", "extends Node\n");
	autoloads.set(SNAME("IndexExportConflict"), settings_path, true, 5);

	const String script_source =
			"@autoload\n"
			"class_name IndexExportConflict extends Node\n";
	const String script_path = files.write("export_conflict_script.fs", script_source);
	ScopedScriptServerClass registered_conflict(SNAME("IndexExportConflict"), "Node", script_path);

	FSCache::remove_parser(script_path);

	FSAutoloadIndex index;
	CHECK_EQ(index.rebuild_from_project_settings_and_script_annotations(), OK);

	const FSAutoloadIndexEntry *entry = index.get_by_name(SNAME("IndexExportConflict"));
	CHECK(entry != nullptr);
	if (entry == nullptr) {
		return;
	}
	CHECK_EQ(entry->source, FSAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION);
	CHECK_EQ(entry->path, script_path);
	CHECK(has_hard_diagnostic_containing(
			*entry,
			FSAutoloadIndexDiagnostic::CONFLICTING_AUTOLOAD_PATH,
			script_path));
	CHECK(has_hard_diagnostic_containing(
			*entry,
			FSAutoloadIndexDiagnostic::CONFLICTING_AUTOLOAD_PATH,
			settings_path));

	const String cache_path = files.reserve("export_conflict_autoload_index_cache.cfg");
	CHECK_EQ(index.save_to_cache(cache_path), OK);

	FSAutoloadIndex loaded;
	CHECK_EQ(loaded.load_from_cache(cache_path), OK);
	const FSAutoloadIndexEntry *loaded_entry = loaded.get_by_name(SNAME("IndexExportConflict"));
	CHECK(loaded_entry != nullptr);
	if (loaded_entry == nullptr) {
		return;
	}
	CHECK_EQ(loaded_entry->source, FSAutoloadIndexEntry::SOURCE_SCRIPT_ANNOTATION);
	CHECK_EQ(loaded_entry->path, script_path);
}

TEST_CASE("[Modules][FoundryScript] Script-owned autoload cache discovery reports invalid annotations") {
	ScopedTempFiles files("fs_autoload_index_export_invalid_annotation");

	const String invalid_source =
			"@autoload\n"
			"class_name IndexExportInvalidAutoload extends RefCounted\n";
	const String invalid_path = files.write("export_invalid_autoload.fs", invalid_source);
	ScopedScriptServerClass registered_invalid(SNAME("IndexExportInvalidAutoload"), "RefCounted", invalid_path);

	FSCache::remove_parser(invalid_path);

	FSAutoloadIndex index;
	CHECK_NE(index.rebuild_from_project_settings_and_script_annotations(), OK);
	CHECK_FALSE(index.has_autoload(SNAME("IndexExportInvalidAutoload")));
}
#endif // TOOLS_ENABLED

TEST_CASE("[Modules][FoundryScript] Analyzer allows same-script class name and autoload singleton names") {
	ScopedTempFiles files("fs_analyzer_autoload_same_script");
	ScopedAutoloadSettings autoloads;

	const String source = "class_name AnalyzerSameAutoload extends Node\n";
	const String same_path = files.write("same_script.fs", source);
	ScopedScriptServerClass registered_same(SNAME("AnalyzerSameAutoload"), "Node", same_path);

	autoloads.set(SNAME("AnalyzerSameAutoload"), same_path, true, 10);

	FSParser parser;
	const Error err = analyze_autoload_source(parser, source, same_path);

	CHECK_EQ(err, OK);
	CHECK_FALSE(autoload_analyzer_has_error(parser, R"(Class "AnalyzerSameAutoload" hides an autoload singleton.)"));
}

TEST_CASE("[Modules][FoundryScript] Analyzer rejects class names that hide unrelated autoload singletons") {
	ScopedTempFiles files("fs_analyzer_autoload_name_conflict");
	ScopedAutoloadSettings autoloads;

	const String autoload_path = files.write("autoload_script.fs", "extends Node\n");
	const String class_path = files.write("class_script.fs", "class_name AnalyzerConflictAutoload extends Node\n");

	autoloads.set(SNAME("AnalyzerConflictAutoload"), autoload_path, true, 10);

	FSParser parser;
	const Error err = analyze_autoload_source(parser, "class_name AnalyzerConflictAutoload extends Node\n", class_path);

	CHECK_NE(err, OK);
	CHECK(autoload_analyzer_has_error(parser, R"(Class "AnalyzerConflictAutoload" hides an autoload singleton.)"));
}

TEST_CASE("[Modules][FoundryScript] Analyzer resolves value-position same-script autoload as singleton instance") {
	ScopedTempFiles files("fs_analyzer_autoload_value_same_script");
	ScopedAutoloadSettings autoloads;

	const String singleton_path = files.write("autoload_script.fs", "class_name AnalyzerValueAutoload extends Node\n");
	const String consumer_path = files.write("consumer.fs", "var singleton = AnalyzerValueAutoload\n");
	ScopedScriptServerClass registered_same(SNAME("AnalyzerValueAutoload"), "Node", singleton_path);

	autoloads.set(SNAME("AnalyzerValueAutoload"), singleton_path, true, 10);

	FSParser parser;
	const Error err = analyze_autoload_source(parser, "var singleton = AnalyzerValueAutoload\n", consumer_path);

	CHECK_EQ(err, OK);
	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(root->has_member(SNAME("singleton")));
	if (!root->has_member(SNAME("singleton"))) {
		return;
	}

	const FSParser::VariableNode *singleton = root->get_member(SNAME("singleton")).variable;
	CHECK(singleton != nullptr);
	if (singleton == nullptr) {
		return;
	}
	CHECK(singleton->initializer != nullptr);
	if (singleton->initializer == nullptr) {
		return;
	}

	const FSParser::DataType initializer_type = singleton->initializer->get_datatype();
	CHECK_FALSE(initializer_type.is_meta_type);
	CHECK_EQ(initializer_type.kind, FSParser::DataType::CLASS);
	CHECK_EQ(initializer_type.native_type, SNAME("Node"));
}

TEST_CASE("[Modules][FoundryScript] Analyzer keeps value-position dotted autoload lookup ahead of namespaced globals") {
	ScopedTempFiles files("fs_analyzer_autoload_dotted_value");
	ScopedAutoloadSettings autoloads;

	const String singleton_path = files.write("autoload_script.fs", "extends Node\n");
	const String namespaced_global_path = files.write("namespaced_global.fs", "extends Node\n");
	const String consumer_path = files.write("consumer.fs", "var member = AnalyzerDottedAutoload.Member\n");
	ScopedScriptServerClass registered_namespaced_global(SNAME("AnalyzerDottedAutoload.Member"), "Node", namespaced_global_path);

	autoloads.set(SNAME("AnalyzerDottedAutoload"), singleton_path, true, 10);

	FSParser parser;
	const Error err = analyze_autoload_source(parser, "var member = AnalyzerDottedAutoload.Member\n", consumer_path);

	CHECK_EQ(err, OK);
	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(root->has_member(SNAME("member")));
	if (!root->has_member(SNAME("member"))) {
		return;
	}

	const FSParser::VariableNode *member = root->get_member(SNAME("member")).variable;
	CHECK(member != nullptr);
	if (member == nullptr) {
		return;
	}
	CHECK(member->initializer != nullptr);
	if (member->initializer == nullptr) {
		return;
	}

	const FSParser::DataType initializer_type = member->initializer->get_datatype();
	CHECK_FALSE(initializer_type.is_meta_type);
	CHECK_NE(initializer_type.kind, FSParser::DataType::CLASS);
}

TEST_CASE("[Modules][FoundryScript] Cached analyzer resolves value-position autoloads through the index") {
	ScopedTempFiles files("fs_analyzer_cached_autoload_value");
	ScopedAutoloadSettings autoloads;

	const String singleton_path = files.write("autoload_script.fs", "class_name AnalyzerCachedValueAutoload extends Node\n");
	const String consumer_path = files.write("consumer.fs", "var singleton = AnalyzerCachedValueAutoload\n");
	ScopedScriptServerClass registered_same(SNAME("AnalyzerCachedValueAutoload"), "Node", singleton_path);

	autoloads.set(SNAME("AnalyzerCachedValueAutoload"), singleton_path, true, 10);
	FSCache::remove_parser(consumer_path);

	Error err = OK;
	Ref<FSParserRef> parser_ref = FSCache::get_parser(consumer_path, FSParserRef::FULLY_SOLVED, err);

	CHECK_EQ(err, OK);
	CHECK(parser_ref.is_valid());
	if (parser_ref.is_null()) {
		return;
	}
	const FSParser::ClassNode *root = parser_ref->get_parser()->get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(root->has_member(SNAME("singleton")));
	if (!root->has_member(SNAME("singleton"))) {
		return;
	}

	const FSParser::VariableNode *singleton = root->get_member(SNAME("singleton")).variable;
	CHECK(singleton != nullptr);
	if (singleton == nullptr) {
		return;
	}
	CHECK(singleton->initializer != nullptr);
	if (singleton->initializer == nullptr) {
		return;
	}

	const FSParser::DataType initializer_type = singleton->initializer->get_datatype();
	CHECK_FALSE(initializer_type.is_meta_type);
	CHECK_EQ(initializer_type.kind, FSParser::DataType::CLASS);
	CHECK_EQ(initializer_type.native_type, SNAME("Node"));

	FSCache::remove_parser(consumer_path);
}

TEST_CASE("[Modules][FoundryScript] Analyzer refreshes autoload index after settings removal") {
	ScopedTempFiles files("fs_analyzer_autoload_removed_mid_analysis");
	ScopedAutoloadSettings autoloads;

	const String singleton_path = files.write("autoload_script.fs", "extends Node\n");
	const String consumer_path = files.write("consumer.fs", "var singleton = AnalyzerRemovedAutoload\n");

	autoloads.set(SNAME("AnalyzerRemovedAutoload"), singleton_path, true, 10);

	FSParser parser;
	CHECK_EQ(parser.parse("var singleton = AnalyzerRemovedAutoload\n", consumer_path, false), OK);
	if (!parser.get_errors().is_empty()) {
		return;
	}

	FSAnalyzer analyzer(&parser);
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

TEST_CASE("[Modules][FoundryScript] Analyzer keeps Node fallback for value-position unresolved autoload scripts") {
	ScopedTempFiles files("fs_analyzer_autoload_value_fallback");
	ScopedAutoloadSettings autoloads;

	const String missing_path = files.missing("missing_singleton.fs");
	const String consumer_path = files.write("consumer.fs", "var singleton = AnalyzerFallbackAutoload\n");

	autoloads.set(SNAME("AnalyzerFallbackAutoload"), missing_path, true, 10);

	FSParser parser;
	const Error err = analyze_autoload_source(parser, "var singleton = AnalyzerFallbackAutoload\n", consumer_path);

	CHECK_EQ(err, OK);
	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(root->has_member(SNAME("singleton")));
	if (!root->has_member(SNAME("singleton"))) {
		return;
	}

	const FSParser::VariableNode *singleton = root->get_member(SNAME("singleton")).variable;
	CHECK(singleton != nullptr);
	if (singleton == nullptr) {
		return;
	}

	const FSParser::DataType singleton_type = singleton->get_datatype();
	CHECK_FALSE(singleton_type.is_meta_type);
	CHECK_EQ(singleton_type.kind, FSParser::DataType::NATIVE);
	CHECK_EQ(singleton_type.native_type, SNAME("Node"));
}

TEST_CASE("[Modules][FoundryScript] Analyzer keeps global classes as types when an autoload shares the name") {
	ScopedTempFiles files("fs_analyzer_autoload_global_type");
	ScopedAutoloadSettings autoloads;

	const String global_path = files.write("global_type.fs", "class_name AnalyzerGlobalTypeAutoload extends Node\n");
	const String autoload_path = files.write("autoload_script.fs", "extends Node\n");
	const String consumer_path = files.write("consumer.fs", "var typed: AnalyzerGlobalTypeAutoload\n");
	ScopedScriptServerClass registered_global(SNAME("AnalyzerGlobalTypeAutoload"), "Node", global_path);

	autoloads.set(SNAME("AnalyzerGlobalTypeAutoload"), autoload_path, true, 10);

	FSParser parser;
	const Error err = analyze_autoload_source(parser, "var typed: AnalyzerGlobalTypeAutoload\n", consumer_path);

	CHECK_EQ(err, OK);
	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(root->has_member(SNAME("typed")));
	if (!root->has_member(SNAME("typed"))) {
		return;
	}

	const FSParser::VariableNode *typed = root->get_member(SNAME("typed")).variable;
	CHECK(typed != nullptr);
	if (typed == nullptr) {
		return;
	}

	const PropertyInfo typed_property = typed->get_datatype().to_property_info("typed");
	CHECK_EQ(typed_property.type, Variant::OBJECT);
	CHECK_EQ(typed_property.class_name, "AnalyzerGlobalTypeAutoload");
}

TEST_CASE("[Modules][FoundryScript] Analyzer rejects autoload singleton instances as base types") {
	ScopedTempFiles files("fs_analyzer_autoload_type_position");
	ScopedAutoloadSettings autoloads;

	const String autoload_path = files.write("autoload_script.fs", "extends Node\n");
	const String consumer_path = files.write("consumer.fs",
			"class Child:\n"
			"\textends AnalyzerTypeOnlyAutoload\n");

	autoloads.set(SNAME("AnalyzerTypeOnlyAutoload"), autoload_path, true, 10);

	FSParser parser;
	const Error err = analyze_autoload_source(parser,
			"class Child:\n"
			"\textends AnalyzerTypeOnlyAutoload\n",
			consumer_path);

	CHECK_NE(err, OK);
	CHECK(autoload_analyzer_has_error(parser, R"(Could not find base class "AnalyzerTypeOnlyAutoload".)"));
}

} // namespace FSTests
