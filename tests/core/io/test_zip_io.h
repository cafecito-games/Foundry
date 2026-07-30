/**************************************************************************/
/*  test_zip_io.h                                                         */
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

#include "core/io/file_access.h"
#include "core/io/zip_io.h"

#include "tests/test_utils.h"
#include "thirdparty/doctest/doctest.h"

namespace TestZipIO {

// Writes one stored entry into `p_path`, leaving the archive open. The caller
// owns the returned handle. minizip only writes the central directory when the
// archive is closed, so whether the guard closed is directly observable: an
// archive that was never closed cannot be reopened for reading.
static zipFile open_archive_with_entry(Ref<FileAccess> *p_io_fa, const String &p_path, const String &p_entry) {
	zlib_filefunc_def io = zipio_create_io(p_io_fa);
	zipFile archive = zipOpen2(p_path.utf8().get_data(), APPEND_STATUS_CREATE, nullptr, &io);
	REQUIRE(archive != nullptr);

	zip_fileinfo fileinfo = {};
	zipOpenNewFileInZip(archive,
			p_entry.utf8().get_data(),
			&fileinfo,
			nullptr,
			0,
			nullptr,
			0,
			nullptr,
			0,
			Z_DEFAULT_COMPRESSION);
	const CharString payload = String("payload").utf8();
	zipWriteInFileInZip(archive, payload.get_data(), payload.length());
	zipCloseFileInZip(archive);
	return archive;
}

static bool archive_is_readable(const String &p_path, const String &p_entry) {
	Ref<FileAccess> io_fa;
	zlib_filefunc_def io = zipio_create_io(&io_fa);
	unzFile archive = unzOpen2(p_path.utf8().get_data(), &io);
	if (!archive) {
		return false;
	}
	const bool located = foundry_unzip_locate_file(archive, p_entry) == UNZ_OK;
	unzClose(archive);
	return located;
}

TEST_CASE("[ZipIO] Guard closes the archive when it goes out of scope") {
	const String path = TestUtils::get_temp_path("zip_io_guard_scope.zip");
	Ref<FileAccess> io_fa;
	{
		ZipFileGuard guard(open_archive_with_entry(&io_fa, path, "entry.txt"));
		CHECK(guard.is_valid());
		// Still open here, so the central directory has not been written yet.
		CHECK_FALSE(archive_is_readable(path, "entry.txt"));
	}
	CHECK_MESSAGE(archive_is_readable(path, "entry.txt"),
			"Leaving the guard's scope must close the archive so it can be reopened.");
}

TEST_CASE("[ZipIO] Explicit close is not repeated by the destructor") {
	const String path = TestUtils::get_temp_path("zip_io_guard_explicit.zip");
	Ref<FileAccess> io_fa;
	{
		ZipFileGuard guard(open_archive_with_entry(&io_fa, path, "entry.txt"));
		guard.close();
		CHECK_FALSE(guard.is_valid());
		CHECK(archive_is_readable(path, "entry.txt"));
		// Falling out of scope here must not close the handle a second time.
	}
	CHECK(archive_is_readable(path, "entry.txt"));
}

TEST_CASE("[ZipIO] Guard tolerates a failed open") {
	ZipFileGuard zip_guard(nullptr);
	CHECK_FALSE(zip_guard.is_valid());
	zip_guard.close();
	CHECK_FALSE(zip_guard.is_valid());

	UnzFileGuard unz_guard(nullptr);
	CHECK_FALSE(unz_guard.is_valid());
	unz_guard.close();
	CHECK_FALSE(unz_guard.is_valid());
}

TEST_CASE("[ZipIO] Reader guard closes its archive") {
	const String path = TestUtils::get_temp_path("zip_io_guard_reader.zip");
	Ref<FileAccess> io_fa;
	{
		ZipFileGuard writer(open_archive_with_entry(&io_fa, path, "entry.txt"));
	}

	Ref<FileAccess> read_io_fa;
	zlib_filefunc_def io = zipio_create_io(&read_io_fa);
	{
		UnzFileGuard guard(unzOpen2(path.utf8().get_data(), &io));
		REQUIRE(guard.is_valid());
		CHECK(foundry_unzip_locate_file(guard.get(), "entry.txt") == UNZ_OK);
	}
	// The reader's FileAccess is released on close, so the file can be replaced.
	CHECK(DirAccess::remove_absolute(path) == OK);
}

} // namespace TestZipIO
