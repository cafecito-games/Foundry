/**************************************************************************/
/*  test_os_user_data_override.h                                          */
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

#include "core/config/project_settings.h"
#include "core/os/os.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

#include "thirdparty/doctest/doctest.h"

namespace TestOSUserDataOverride {

// `set_user_data_root_override()` (set by the test entrypoint for per-shard `user://` isolation)
// must make every branch of the no-arg `get_user_data_dir()` resolve under the override root,
// while a cleared override restores the platform path. Project settings and the override itself
// are process-global, so every value is saved and restored to keep this case order-independent.
TEST_CASE("[OS] user-data root override redirects every get_user_data_dir branch") {
	ProjectSettings *ps = ProjectSettings::get_singleton();
	OS *os = OS::get_singleton();

	const String saved_override = os->get_user_data_root_override();
	const Variant saved_name = GLOBAL_GET("application/config/name");
	const Variant saved_use_custom = GLOBAL_GET("application/config/use_custom_user_dir");
	const Variant saved_custom_dir = GLOBAL_GET("application/config/custom_user_dir_name");

	const String override_root = TestUtils::get_temp_path("user_data_override_probe").simplify_path();
	const String override_prefix = override_root + "/";

	os->set_user_data_root_override(override_root);

	// Named project branch.
	ps->set_setting("application/config/name", "foundry_override_probe");
	ps->set_setting("application/config/use_custom_user_dir", false);
	{
		const String resolved = os->get_user_data_dir();
		CHECK_MESSAGE(resolved.begins_with(override_prefix),
				"A named project's user data should resolve under the override root: ", resolved);
	}

	// Custom user dir branch.
	ps->set_setting("application/config/use_custom_user_dir", true);
	ps->set_setting("application/config/custom_user_dir_name", "custom_override_probe");
	{
		const String resolved = os->get_user_data_dir();
		CHECK_MESSAGE(resolved.begins_with(override_prefix),
				"A custom user dir should resolve under the override root: ", resolved);
	}

	// Unnamed project fallback branch.
	ps->set_setting("application/config/name", String());
	ps->set_setting("application/config/use_custom_user_dir", false);
	{
		const String resolved = os->get_user_data_dir();
		CHECK_MESSAGE(resolved.begins_with(override_prefix),
				"An unnamed project's user data should resolve under the override root: ", resolved);
		CHECK_MESSAGE(resolved.contains("[unnamed project]"),
				"The unnamed fallback should keep its [unnamed project] leaf: ", resolved);
	}

	// Clearing the override restores the platform resolution, which must not live under the
	// override root we just exercised.
	os->set_user_data_root_override(String());
	{
		const String resolved = os->get_user_data_dir();
		CHECK_MESSAGE(!resolved.begins_with(override_prefix),
				"With the override cleared, user data should resolve on the platform path: ", resolved);
	}

	// Restore process-global state.
	os->set_user_data_root_override(saved_override);
	ps->set_setting("application/config/name", saved_name);
	ps->set_setting("application/config/use_custom_user_dir", saved_use_custom);
	ps->set_setting("application/config/custom_user_dir_name", saved_custom_dir);
}

} // namespace TestOSUserDataOverride
