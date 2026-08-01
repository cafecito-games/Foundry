/**************************************************************************/
/*  test_conformance_witness_scope.h                                      */
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

#include "modules/foundry_script/fs_analyzer.h"
#include "modules/foundry_script/fs_cache.h"
#include "modules/foundry_script/fs_conformance_registry.h"
#include "modules/foundry_script/fs_parser.h"
#include "modules/foundry_script/tests/fs_temporary_project_tree.h"

#include "core/io/file_access.h"
#include "core/object/script_language.h"
#include "tests/test_macros.h"

// A conformance witness sees a dual scope: the target's member/type scope first, and the lexical type
// scope of the file declaring the `extend` as a fallback. Two properties of that fallback can only be
// observed across several analyses inside one process, which the `.fs` fixture runner cannot express
// because it isolates every fixture:
//
//   * two files conforming the *same* foreign target must not contaminate each other's scope, and
//   * the outcome must not depend on which of them is analyzed first.
namespace FSTests {

struct ConformanceWitnessScopeFixture {
	TemporaryProjectTree tree;
	String target_path;
	String first_conformance_path;
	String second_conformance_path;

	ConformanceWitnessScopeFixture() :
			tree("foundry_conformance_witness_scope") {
		// The shared foreign target. Its own file declares no helper types at all, so anything a
		// witness names has to come from that witness's declaring file.
		target_path = write("fws_holder.fs",
				"class_name FwsHolder\n"
				"extends RefCounted\n"
				"\n"
				"var power: int = 3\n");

		first_conformance_path = write("fws_first.fs",
				"extend FwsHolder uses FwsFirstTrait:\n"
				"\tfunc first_helper() -> Helper:\n"
				"\t\treturn Helper.new()\n"
				"\n"
				"\n"
				"trait FwsFirstTrait:\n"
				"\tabstract func first_helper() -> Helper\n"
				"\n"
				"\n"
				"class Helper:\n"
				"\tvar first_marker: int = 1\n");

		// The same target, a different trait, and a *different* class that happens to share the name
		// `Helper`. Each witness must bind to the `Helper` of its own file.
		second_conformance_path = write("fws_second.fs",
				"extend FwsHolder uses FwsSecondTrait:\n"
				"\tfunc second_helper() -> Helper:\n"
				"\t\treturn Helper.new()\n"
				"\n"
				"\n"
				"trait FwsSecondTrait:\n"
				"\tabstract func second_helper() -> Helper\n"
				"\n"
				"\n"
				"class Helper:\n"
				"\tvar second_marker: int = 2\n");

		ScriptServer::add_global_class("FwsHolder", "RefCounted", "FoundryScript", target_path, false, false, false, false);
	}

	~ConformanceWitnessScopeFixture() {
		ScriptServer::remove_global_class("FwsHolder");
		FSConformanceRegistry::get_singleton()->clear();
		FSCache::remove_parser(target_path);
		FSCache::remove_parser(first_conformance_path);
		FSCache::remove_parser(second_conformance_path);
	}

	String write(const String &p_file_name, const String &p_source) {
		tree.write_file(p_file_name, p_source);
		const String path = tree.root.path_join(p_file_name);
		FSCache::remove_parser(path);
		return path;
	}
};

// Analyzes one file and reports the class the named witness's return annotation resolved to, plus
// whether the analysis was clean. The parser owns the resolved AST, so the caller only receives
// identity facts extracted while it is still alive.
struct WitnessReturnTypeProbe {
	bool clean = false;
	bool resolved_to_class = false;
	String declaring_script_path;
	String fully_qualified_name;
	StringName member_name;
	Vector<String> errors;
};

static String join_probe_errors(const WitnessReturnTypeProbe &p_probe) {
	String joined;
	for (const String &message : p_probe.errors) {
		if (!joined.is_empty()) {
			joined += " | ";
		}
		joined += message;
	}
	return joined;
}

static WitnessReturnTypeProbe probe_witness_return_type(const String &p_path, const StringName &p_witness_name) {
	WitnessReturnTypeProbe probe;
	FSCache::remove_parser(p_path);

	FSParser parser;
	if (parser.parse(FileAccess::get_file_as_string(p_path), p_path, false) != OK) {
		return probe;
	}
	FSAnalyzer analyzer(&parser);
	const Error err = analyzer.analyze();
	for (const FSParser::ParserError &parser_error : parser.get_errors()) {
		probe.errors.push_back(parser_error.message);
	}
	probe.clean = err == OK && probe.errors.is_empty();

	const FSParser::ClassNode *head = parser.get_tree();
	if (head == nullptr) {
		return probe;
	}
	for (FSParser::ConformanceNode *conformance : head->conformances) {
		if (conformance == nullptr) {
			continue;
		}
		for (FSParser::FunctionNode *witness : conformance->witnesses) {
			if (witness == nullptr || witness->identifier == nullptr || witness->identifier->name != p_witness_name) {
				continue;
			}
			const FSParser::DataType return_type = witness->get_datatype();
			if (return_type.kind != FSParser::DataType::CLASS || return_type.class_type == nullptr) {
				return probe;
			}
			probe.resolved_to_class = true;
			probe.declaring_script_path = return_type.script_path;
			probe.fully_qualified_name = return_type.class_type->fqcn;
			// The distinguishing member proves *which* same-named `Helper` was bound.
			for (const KeyValue<StringName, int> &member : return_type.class_type->members_indices) {
				probe.member_name = member.key;
			}
			return probe;
		}
	}
	return probe;
}

TEST_CASE("[Modules][FoundryScript][Conformance] witness declaration scopes stay isolated per file") {
	ConformanceWitnessScopeFixture fixture;

	// Both analysis orders are exercised because the foreign target's parse tree is cached and shared:
	// whichever file is analyzed first must not leave its own declarations reachable from the other.
	SUBCASE("first file analyzed first") {
		const WitnessReturnTypeProbe first = probe_witness_return_type(fixture.first_conformance_path, SNAME("first_helper"));
		const WitnessReturnTypeProbe second = probe_witness_return_type(fixture.second_conformance_path, SNAME("second_helper"));

		CHECK_MESSAGE(first.clean, join_probe_errors(first));
		CHECK_MESSAGE(second.clean, join_probe_errors(second));
		REQUIRE(first.resolved_to_class);
		REQUIRE(second.resolved_to_class);
		CHECK(first.declaring_script_path == fixture.first_conformance_path);
		CHECK(second.declaring_script_path == fixture.second_conformance_path);
		CHECK(first.member_name == SNAME("first_marker"));
		CHECK(second.member_name == SNAME("second_marker"));
		CHECK(first.fully_qualified_name != second.fully_qualified_name);
	}

	SUBCASE("second file analyzed first") {
		const WitnessReturnTypeProbe second = probe_witness_return_type(fixture.second_conformance_path, SNAME("second_helper"));
		const WitnessReturnTypeProbe first = probe_witness_return_type(fixture.first_conformance_path, SNAME("first_helper"));

		CHECK_MESSAGE(first.clean, join_probe_errors(first));
		CHECK_MESSAGE(second.clean, join_probe_errors(second));
		REQUIRE(first.resolved_to_class);
		REQUIRE(second.resolved_to_class);
		CHECK(first.declaring_script_path == fixture.first_conformance_path);
		CHECK(second.declaring_script_path == fixture.second_conformance_path);
		CHECK(first.member_name == SNAME("first_marker"));
		CHECK(second.member_name == SNAME("second_marker"));
	}
}

TEST_CASE("[Modules][FoundryScript][Conformance] the declaration-site fallback exposes only types") {
	ConformanceWitnessScopeFixture fixture;

	// A witness sees the declaring file's *types*, never its values: `secret` is a file-level constant
	// of the conformance file, so naming it from the witness has to stay an error.
	const String value_conformance_path = fixture.write("fws_values.fs",
			"const secret: int = 9\n"
			"\n"
			"\n"
			"extend FwsHolder uses FwsValueTrait:\n"
			"\tfunc leak() -> int:\n"
			"\t\treturn secret\n"
			"\n"
			"\n"
			"trait FwsValueTrait:\n"
			"\tabstract func leak() -> int\n");

	const WitnessReturnTypeProbe probe = probe_witness_return_type(value_conformance_path, SNAME("leak"));
	CHECK_FALSE(probe.clean);
	bool reported_unknown_identifier = false;
	for (const String &message : probe.errors) {
		if (message == R"(Identifier "secret" not declared in the current scope.)") {
			reported_unknown_identifier = true;
		}
	}
	CHECK_MESSAGE(reported_unknown_identifier, join_probe_errors(probe));

	FSCache::remove_parser(value_conformance_path);
}

} // namespace FSTests
