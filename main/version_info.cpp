/**************************************************************************/
/*  version_info.cpp                                                      */
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

#include "version_info.h"

#include "core/io/json.h"
#include "core/version.h"

namespace FoundryVersionInfo {

Dictionary get_dictionary() {
	Dictionary extension_api;
	extension_api["interface_format"] = FOUNDRY_EXTENSION_INTERFACE_FORMAT;
	extension_api["abi_revision"] = FOUNDRY_EXTENSION_ABI_REVISION;

	Dictionary version_info;
	version_info["product"] = FOUNDRY_VERSION_NAME;
	version_info["version"] = vformat("%d.%d.%d", FOUNDRY_VERSION_MAJOR, FOUNDRY_VERSION_MINOR, FOUNDRY_VERSION_PATCH);
	version_info["release_tag"] = FOUNDRY_VERSION_RELEASE_TAG;
	version_info["channel"] = FOUNDRY_VERSION_CHANNEL;
	version_info["git_commit"] = FOUNDRY_VERSION_GIT_COMMIT;
	version_info["git_dirty"] = bool(FOUNDRY_VERSION_GIT_DIRTY);
	version_info["build_id"] = FOUNDRY_VERSION_BUILD_ID;
	version_info["target"] = FOUNDRY_VERSION_TARGET;
	version_info["extension_api"] = extension_api;
	return version_info;
}

String get_json() {
	return JSON::stringify(get_dictionary(), "", false);
}

} // namespace FoundryVersionInfo
