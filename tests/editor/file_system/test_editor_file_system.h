/**************************************************************************/
/*  test_editor_file_system.h                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#ifdef TOOLS_ENABLED

#include "editor/file_system/editor_file_system.h"

#include "core/io/resource_importer.h"
#include "core/io/resource_loader.h"

#include "tests/test_macros.h"

namespace TestEditorFileSystem {

// Tests in other suites repeatedly create and destroy a transient
// `EditorFileSystem` to exercise editor tooling. Its constructor installs
// process-global callbacks (`ResourceLoader::import`,
// `ResourceImporter::load_on_startup`) and points the `singleton` at itself;
// those callbacks dereference `singleton`. If destruction does not undo them,
// a later resource import in an unrelated suite calls into freed memory, which
// previously surfaced as a use-after-free crash under randomized test ordering.
TEST_CASE("[EditorFileSystem] Destruction clears the singleton and restores global hooks") {
	ResourceLoaderImport previous_import = ResourceLoader::import;
	ResourceFormatImporterLoadOnStartup previous_load_on_startup = ResourceImporter::load_on_startup;

	REQUIRE(EditorFileSystem::get_singleton() == nullptr);

	EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
	CHECK(EditorFileSystem::get_singleton() == editor_file_system);
	CHECK(ResourceLoader::import != previous_import);
	CHECK(ResourceImporter::load_on_startup != previous_load_on_startup);

	memdelete(editor_file_system);
	CHECK(EditorFileSystem::get_singleton() == nullptr);
	CHECK(ResourceLoader::import == previous_import);
	CHECK(ResourceImporter::load_on_startup == previous_load_on_startup);
}

} // namespace TestEditorFileSystem

#endif // TOOLS_ENABLED
