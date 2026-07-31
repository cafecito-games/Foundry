/**************************************************************************/
/*  test_fs_builtin_sources.h                                             */
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

#include "modules/foundry_script/fs_builtin_sources.h"

#include "tests/test_macros.h"

namespace TestFSBuiltinSources {

TEST_CASE("[FSBuiltinSources] Registered source is retrievable by path") {
	FSBuiltinSources::register_source("foundry://builtin/test_only.fs", "enum_name TestOnly:\n\tA\n");

	String source;
	CHECK(FSBuiltinSources::get_source("foundry://builtin/test_only.fs", source));
	CHECK(source.contains("enum_name TestOnly"));

	FSBuiltinSources::clear();
}

TEST_CASE("[FSBuiltinSources] Unknown path reports absence") {
	String source;
	CHECK_FALSE(FSBuiltinSources::get_source("foundry://builtin/missing.fs", source));
	CHECK(source.is_empty());
}

TEST_CASE("[FSBuiltinSources] Only the reserved prefix is a builtin path") {
	CHECK(FSBuiltinSources::is_builtin_path("foundry://builtin/json_node.fs"));
	CHECK_FALSE(FSBuiltinSources::is_builtin_path("res://json_node.fs"));
	CHECK_FALSE(FSBuiltinSources::is_builtin_path("user://json_node.fs"));
	CHECK_FALSE(FSBuiltinSources::is_builtin_path("foundry://other/json_node.fs"));
}

TEST_CASE("[FSBuiltinSources] Project paths cannot masquerade as builtin") {
	FSBuiltinSources::register_source("foundry://builtin/shadow_test.fs", "enum_name ShadowTest:\n\tA\n");

	String source;
	CHECK_FALSE(FSBuiltinSources::get_source("res://shadow_test.fs", source));

	ERR_PRINT_OFF;
	FSBuiltinSources::register_source("res://not_builtin.fs", "enum_name NotBuiltin:\n\tA\n");
	ERR_PRINT_ON;
	CHECK_FALSE(FSBuiltinSources::get_source("res://not_builtin.fs", source));

	FSBuiltinSources::clear();
}

} // namespace TestFSBuiltinSources
