/**************************************************************************/
/*  test_foundry_version_info.h                                           */
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

#include "core/io/json.h"
#include "core/version.h"
#include "main/version_info.h"
#include "tests/test_macros.h"

namespace TestFoundryVersionInfo {

TEST_CASE("[FoundryVersionInfo] Version JSON contains the stable release schema") {
	const Variant parsed = JSON::parse_string(FoundryVersionInfo::get_json());
	REQUIRE(parsed.get_type() == Variant::DICTIONARY);
	const Dictionary root = parsed;

	CHECK_EQ(root["product"], String(FOUNDRY_VERSION_NAME));
	CHECK_EQ(root["version"], vformat("%d.%d.%d", FOUNDRY_VERSION_MAJOR, FOUNDRY_VERSION_MINOR, FOUNDRY_VERSION_PATCH));
	CHECK(root.has("release_tag"));
	CHECK(root.has("channel"));
	CHECK(root.has("git_commit"));
	CHECK(root["git_dirty"].get_type() == Variant::BOOL);
	CHECK(root.has("build_id"));
	CHECK(root.has("target"));

	const Dictionary extension_api = root["extension_api"];
	CHECK_EQ(int(extension_api["interface_format"]), FOUNDRY_EXTENSION_INTERFACE_FORMAT);
	CHECK_EQ(int(extension_api["abi_revision"]), FOUNDRY_EXTENSION_ABI_REVISION);
}

} // namespace TestFoundryVersionInfo
