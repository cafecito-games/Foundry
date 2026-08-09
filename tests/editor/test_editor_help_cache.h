/**************************************************************************/
/*  test_editor_help_cache.h                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             FOUNDRY ENGINE                             */
/*          A fork of the Godot Engine (https://godotengine.org)          */
/*                       https://www.cafecito.games                       */
/**************************************************************************/
/* Copyright (c) 2026-present Cafecito Games LLC.                         */
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

#ifdef TOOLS_ENABLED

#include "editor/doc/editor_help.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_saver.h"
#include "core/os/os.h"

#include "tests/test_macros.h"
#include "tests/test_tools.h"

namespace TestEditorHelpCache {

class ScopedDocCacheFile {
	String path;

public:
	explicit ScopedDocCacheFile(const String &p_stem) {
		String root;
		if (OS::get_singleton()->has_environment("FOUNDRY_TEST_SCRATCH")) {
			root = OS::get_singleton()->get_environment("FOUNDRY_TEST_SCRATCH");
		}
		if (root.is_empty()) {
			root = OS::get_singleton()->get_temp_path();
		}
		root = root.simplify_path();
		CHECK_EQ(DirAccess::make_dir_recursive_absolute(root), OK);
		path = root.path_join(vformat("%s_%d.res", p_stem, OS::get_singleton()->get_process_id()));
		if (FileAccess::exists(path)) {
			CHECK_EQ(DirAccess::remove_absolute(path), OK);
		}
	}

	~ScopedDocCacheFile() {
		if (FileAccess::exists(path)) {
			DirAccess::remove_absolute(path);
		}
	}

	const String &get_path() const { return path; }
};

static void save_cache_resource(const String &p_path, uint32_t p_flags = ResourceSaver::FLAG_NONE) {
	Ref<Resource> resource;
	resource.instantiate();
	REQUIRE_EQ(ResourceSaver::save(resource, p_path, p_flags), OK);
}

TEST_CASE("[Editor][EditorHelpCache] a current binary resource cache is retained") {
	ScopedDocCacheFile cache("editor_help_current_cache");
	save_cache_resource(cache.get_path(), ResourceSaver::FLAG_COMPRESS);

	CHECK(EditorHelp::prepare_doc_cache_for_tests(cache.get_path()));
	CHECK(FileAccess::exists(cache.get_path()));
}

TEST_CASE("[Editor][EditorHelpCache] a future binary resource cache is discarded") {
	ScopedDocCacheFile cache("editor_help_future_cache");
	save_cache_resource(cache.get_path());

	Ref<FileAccess> file = FileAccess::open(cache.get_path(), FileAccess::READ_WRITE);
	REQUIRE(file.is_valid());
	file->seek(20); // RSRC magic, endian, real width, engine major, and engine minor.
	file->store_32(UINT32_MAX);
	file.unref();

	ErrorDetector error_detector;
	CHECK_FALSE(EditorHelp::prepare_doc_cache_for_tests(cache.get_path()));
	CHECK_FALSE(FileAccess::exists(cache.get_path()));
	CHECK_FALSE(error_detector.has_error);
}

} // namespace TestEditorHelpCache

#endif // TOOLS_ENABLED
