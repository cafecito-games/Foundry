/**************************************************************************/
/*  test_user_data_case_isolation.h                                       */
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
/* MERCHANTABILITY, FITNESS FOR NONINFRINGEMENT. IN NO EVENT SHALL THE   */
/* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR      */
/* OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, */
/* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR  */
/* OTHER DEALINGS IN THE SOFTWARE.                                        */
/**************************************************************************/

#pragma once

#include "main/test_user_data_root_policy.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

#include "tests/test_macros.h"

namespace TestUserDataCaseIsolation {

// The per-run `user://` isolation root is erased clean at boot, so its leaf name must be
// process-unique: two runs resolving to the same name would let the second one delete the
// first one's `user://` tree mid-run.
TEST_CASE("[TestUserDataRoot] Two processes never share an isolation root") {
	const String first = TestUserDataRootPolicy::leaf_name(0, 1, 111);
	const String second = TestUserDataRootPolicy::leaf_name(0, 1, 222);
	CHECK_NE(first, second);
	CHECK_EQ(TestUserDataRootPolicy::leaf_name(0, 1, 111), TestUserDataRootPolicy::leaf_name(0, 1, 111));
}

TEST_CASE("[TestUserDataRoot] Shard identity survives the process suffix") {
	const String shard_one = TestUserDataRootPolicy::leaf_name(1, 3, 111);
	const String shard_two = TestUserDataRootPolicy::leaf_name(2, 3, 111);
	CHECK_MESSAGE(shard_one.contains("shard-1-of-3"), "Shard 1 of 3 should stay identifiable: ", shard_one);
	CHECK_MESSAGE(shard_two.contains("shard-2-of-3"), "Shard 2 of 3 should stay identifiable: ", shard_two);
	CHECK_NE(shard_one, shard_two);
	CHECK_MESSAGE(TestUserDataRootPolicy::leaf_name(0, 1, 111).contains("unsharded"),
			"A single-shard run should stay identifiable as unsharded: ",
			TestUserDataRootPolicy::leaf_name(0, 1, 111));
}

// The two cases below are an ordering-dependent regression pair: doctest orders cases by
// file then line, so the leak probe runs immediately before the writability probe and
// observes exactly the state it leaves behind. `--shard` can split the pair, in which case
// the writability probe passes trivially — a false negative, never a false positive.
TEST_CASE("[OS] A leaked project rename does not survive into the next test case") {
	// Renaming the project retargets `user://` for every later case, and nothing ever
	// creates this leaf. Deliberately leak it; restoring it here would defeat the point.
	ProjectSettings::get_singleton()->set_setting("application/config/name", "user_data_case_isolation_leak_probe");
}

TEST_CASE("[OS] user:// is writable at the start of every test case") {
	// Captured after the leaked rename above is live, so restoring below re-leaks it: the
	// per-case `user://` invariant must hold for whatever mapping a case starts with.
	const String saved_app_name = GLOBAL_GET("application/config/name");
	{
		Ref<FileAccess> probe = FileAccess::open("user://case_isolation_probe.txt", FileAccess::WRITE);
		CHECK_MESSAGE(probe.is_valid(),
				"FileAccess::open(\"user://…\", WRITE) should succeed at the start of a test case");
	}
	Ref<DirAccess> user_dir = DirAccess::open("user://");
	REQUIRE(user_dir.is_valid());
	CHECK_EQ(user_dir->remove("case_isolation_probe.txt"), OK);
	ProjectSettings::get_singleton()->set_setting("application/config/name", saved_app_name);
}

} // namespace TestUserDataCaseIsolation
