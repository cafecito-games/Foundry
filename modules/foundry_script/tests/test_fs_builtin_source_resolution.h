/**************************************************************************/
/*  test_fs_builtin_source_resolution.h                                   */
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
#include "modules/foundry_script/fs_builtin_sources.h"
#include "modules/foundry_script/fs_cache.h"
#include "modules/foundry_script/fs_parser.h"

#include "core/object/script_language.h"

#include "tests/test_macros.h"

namespace TestFSBuiltinSourceResolution {

namespace {

class ScopedBuiltinGlobalClass {
	StringName class_name;
	String path;

public:
	ScopedBuiltinGlobalClass(const StringName &p_class_name, const String &p_base, const String &p_path) {
		class_name = p_class_name;
		path = p_path;
		ScriptServer::remove_global_class(class_name);
		ScriptServer::add_global_class(class_name, p_base, SNAME("FoundryScript"), path, false, false, false);
	}

	~ScopedBuiltinGlobalClass() {
		ScriptServer::remove_global_class(class_name);
		FSCache::remove_parser(path);
		FSBuiltinSources::clear();
	}
};

Error analyze_source(FSParser &r_parser, const String &p_source, const String &p_path) {
	Error err = r_parser.parse(p_source, p_path, false);
	if (err != OK) {
		return err;
	}

	FSAnalyzer analyzer(&r_parser);
	return analyzer.analyze();
}

} // namespace

TEST_CASE("[FSBuiltinSources] A registered builtin path parses through the dependency-parser path") {
	const String builtin_path = "foundry://builtin/resolution_test_registered.fs";
	FSBuiltinSources::register_source(builtin_path, "class_name FSBuiltinResolutionRegistered\nextends RefCounted\n");
	ScopedBuiltinGlobalClass registered_class(SNAME("FSBuiltinResolutionRegistered"), "RefCounted", builtin_path);

	FSParser parser;
	const Error err = analyze_source(parser, "var member: FSBuiltinResolutionRegistered\n", "res://fs_builtin_resolution_registered_consumer.fs");

	CHECK_EQ(err, OK);
	const FSParser::ClassNode *root = parser.get_tree();
	REQUIRE(root != nullptr);
	REQUIRE(root->has_member(SNAME("member")));

	const FSParser::VariableNode *member = root->get_member(SNAME("member")).variable;
	REQUIRE(member != nullptr);
	CHECK_EQ(member->get_datatype().native_type, SNAME("RefCounted"));
}

TEST_CASE("[FSBuiltinSources] An unregistered builtin path reports the standard missing-script diagnostic") {
	const String builtin_path = "foundry://builtin/resolution_test_missing.fs";
	ScopedBuiltinGlobalClass registered_class(SNAME("FSBuiltinResolutionMissing"), "RefCounted", builtin_path);

	FSParser parser;
	ERR_PRINT_OFF;
	const Error err = analyze_source(parser, "var member: FSBuiltinResolutionMissing\n", "res://fs_builtin_resolution_missing_consumer.fs");
	ERR_PRINT_ON;

	CHECK_NE(err, OK);
}

} // namespace TestFSBuiltinSourceResolution
