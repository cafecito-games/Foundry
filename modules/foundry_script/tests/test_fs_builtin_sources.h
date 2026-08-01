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

	FSBuiltinSources::unregister_source("foundry://builtin/test_only.fs");
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

	FSBuiltinSources::unregister_source("foundry://builtin/shadow_test.fs");
}

TEST_CASE("[FSBuiltinSources] Registered paths enumerate in sorted order") {
	FSBuiltinSources::register_source("foundry://builtin/zeta_order_test.fs", "enum_name ZetaOrderTest:\n\tA\n");
	FSBuiltinSources::register_source("foundry://builtin/alpha_order_test.fs", "enum_name AlphaOrderTest:\n\tA\n");

	List<String> paths;
	FSBuiltinSources::get_registered_paths(&paths);
	String previous_path;
	for (const String &path : paths) {
		CHECK(previous_path <= path);
		previous_path = path;
	}
	CHECK(paths.find("foundry://builtin/alpha_order_test.fs") != nullptr);
	CHECK(paths.find("foundry://builtin/zeta_order_test.fs") != nullptr);

	FSBuiltinSources::unregister_source("foundry://builtin/zeta_order_test.fs");
	FSBuiltinSources::unregister_source("foundry://builtin/alpha_order_test.fs");
}

TEST_CASE("[FSBuiltinSources] Exported bytecode path replaces only the final extension") {
	CHECK_EQ(FSBuiltinSources::get_exported_bytecode_path("foundry://builtin/json_node.fs"),
			"res://.foundry/builtin/json_node.fsb");
	// Relative subdirectories are preserved so two builtins can share a leaf name.
	CHECK_EQ(FSBuiltinSources::get_exported_bytecode_path("foundry://builtin/nested/dir/thing.fs"),
			"res://.foundry/builtin/nested/dir/thing.fsb");
	// Only the trailing `.fs` is replaced; interior dots stay part of the name.
	CHECK_EQ(FSBuiltinSources::get_exported_bytecode_path("foundry://builtin/json.node.fs"),
			"res://.foundry/builtin/json.node.fsb");
	CHECK_EQ(FSBuiltinSources::get_exported_bytecode_path("foundry://builtin/CASE.FS"),
			"res://.foundry/builtin/CASE.fsb");
}

TEST_CASE("[FSBuiltinSources] Exported bytecode path rejects non-builtin and non-source paths") {
	CHECK(FSBuiltinSources::get_exported_bytecode_path("res://json_node.fs").is_empty());
	CHECK(FSBuiltinSources::get_exported_bytecode_path("user://json_node.fs").is_empty());
	CHECK(FSBuiltinSources::get_exported_bytecode_path("foundry://other/json_node.fs").is_empty());
	CHECK(FSBuiltinSources::get_exported_bytecode_path("foundry://builtin/json_node.fsc").is_empty());
	CHECK(FSBuiltinSources::get_exported_bytecode_path("foundry://builtin/json_node.fsb").is_empty());
	CHECK(FSBuiltinSources::get_exported_bytecode_path("foundry://builtin/json_node").is_empty());
	CHECK(FSBuiltinSources::get_exported_bytecode_path("foundry://builtin/").is_empty());
	CHECK(FSBuiltinSources::get_exported_bytecode_path("foundry://builtin/.fs").is_empty());
	CHECK(FSBuiltinSources::get_exported_bytecode_path(String()).is_empty());
}

} // namespace TestFSBuiltinSources
