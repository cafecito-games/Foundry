/**************************************************************************/
/*  test_editor_export_platform_macos.h                                   */
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

#ifdef TOOLS_ENABLED

#include "core/io/dir_access.h"
#include "core/io/zip_io.h"
#include "editor/export/editor_export.h"
#include "editor/export/editor_export_preset.h"
#include "platform/macos/export/export_plugin.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

#include "tests/test_utils.h"
#include "thirdparty/doctest/doctest.h"

namespace TestEditorExportPlatformMacOS {

// `EditorExportPlatform::ExportNotifier` dereferences `EditorExport`'s singleton
// on both construction and destruction, and only the full editor creates one.
class ScopedEditorExport {
	EditorExport *previous = nullptr;
	EditorExport *owned = nullptr;

public:
	ScopedEditorExport() {
		previous = EditorExport::get_singleton();
		if (!previous) {
			owned = memnew(EditorExport);
			// Setting a preset value schedules a deferred save through a Timer,
			// which errors unless it is inside the tree.
			SceneTree::get_singleton()->get_root()->add_child(owned);
		}
	}

	~ScopedEditorExport() {
		if (owned) {
			SceneTree::get_singleton()->get_root()->remove_child(owned);
			memdelete(owned);
		}
	}
};

// A template archive only has to open for the export to reach the format check;
// its contents are never inspected on that path.
static void write_openable_archive(const String &p_path) {
	Ref<FileAccess> io_fa;
	zlib_filefunc_def io = zipio_create_io(&io_fa);
	zipFile archive = zipOpen2(p_path.utf8().get_data(), APPEND_STATUS_CREATE, nullptr, &io);
	REQUIRE(archive != nullptr);

	zip_fileinfo fileinfo = {};
	zipOpenNewFileInZip(archive, "placeholder", &fileinfo, nullptr, 0, nullptr, 0, nullptr, 0, Z_DEFAULT_COMPRESSION);
	zipCloseFileInZip(archive);
	zipClose(archive, nullptr);
}

TEST_CASE("[Editor][EditorExportPlatformMacOS] An unrecognized output extension is rejected") {
	ScopedEditorExport editor_export;

	const String template_path = TestUtils::get_temp_path("macos_export_template.zip");
	write_openable_archive(template_path);

	Ref<EditorExportPlatformMacOS> platform;
	platform.instantiate();

	// `create_preset()` is what wires the preset back to its platform; the export
	// dereferences that link while gathering features.
	Ref<EditorExportPreset> preset = platform->create_preset();
	REQUIRE(preset.is_valid());
	preset->set("custom_template/release", template_path);

	// The export opens the template archive before validating the output format,
	// so this path returns with that archive already open. It must report the
	// invalid format rather than proceeding or crashing.
	const String output_path = TestUtils::get_temp_path("macos_export_output.unsupported");
	const Error result = platform->export_project(preset, false, output_path, 0);

	CHECK(result == ERR_CANT_CREATE);
	CHECK_FALSE_MESSAGE(FileAccess::exists(output_path),
			"A rejected export format must not leave an output artifact behind.");

	DirAccess::remove_absolute(template_path);
}

} // namespace TestEditorExportPlatformMacOS

#endif // TOOLS_ENABLED
