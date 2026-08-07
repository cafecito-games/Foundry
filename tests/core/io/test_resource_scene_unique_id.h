/**************************************************************************/
/*  test_resource_scene_unique_id.h                                       */
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

#include "core/io/resource.h"

#include "tests/test_macros.h"

namespace TestResourceSceneUniqueId {

TEST_CASE("[SceneUniqueId] The prefix helper keeps only the simple class name") {
	CHECK_EQ(Resource::derive_scene_unique_id_prefix("foundry.test.resource.Widget"), "Widget");
	CHECK_EQ(Resource::derive_scene_unique_id_prefix("Widget"), "Widget");
	CHECK_EQ(Resource::derive_scene_unique_id_prefix(""), "");
	CHECK_EQ(Resource::derive_scene_unique_id_prefix(".Widget"), "Widget");
	// A trailing dot yields an empty prefix. Documented edge case, not an error.
	CHECK_EQ(Resource::derive_scene_unique_id_prefix("Widget."), "");
}

TEST_CASE("[SceneUniqueId] A rejected id leaves the previous id untouched") {
	Ref<Resource> resource;
	resource.instantiate();
	resource->set_scene_unique_id("abc_123");
	REQUIRE_EQ(resource->get_scene_unique_id(), "abc_123");

	ERR_PRINT_OFF;
	resource->set_scene_unique_id("foundry.bad.id_x");
	ERR_PRINT_ON;

	CHECK_EQ(resource->get_scene_unique_id(), "abc_123");
}

TEST_CASE("[SceneUniqueId] A rejected id on a fresh resource leaves it empty") {
	Ref<Resource> resource;
	resource.instantiate();

	ERR_PRINT_OFF;
	resource->set_scene_unique_id("has.dots");
	ERR_PRINT_ON;

	CHECK(resource->get_scene_unique_id().is_empty());
}

TEST_CASE("[SceneUniqueId] An empty id clears the current id") {
	Ref<Resource> resource;
	resource.instantiate();
	resource->set_scene_unique_id("abc_123");
	REQUIRE_EQ(resource->get_scene_unique_id(), "abc_123");

	resource->set_scene_unique_id("");

	CHECK(resource->get_scene_unique_id().is_empty());
}

// Generated ids can start with a digit, so validation must stay per-character and must not become an
// identifier check that rejects a leading digit.
TEST_CASE("[SceneUniqueId] An id starting with a digit is accepted") {
	Ref<Resource> resource;
	resource.instantiate();

	resource->set_scene_unique_id("0abcd");

	CHECK_EQ(resource->get_scene_unique_id(), "0abcd");
}

} // namespace TestResourceSceneUniqueId
