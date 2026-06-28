/**************************************************************************/
/*  test_file_access.h                                                    */
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

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

#ifdef UNIX_ENABLED
#include <unistd.h>
#endif

namespace TestFileAccess {

TEST_CASE("[FileAccess] Exclusive create (WRITE_EXCL)") {
	const String base_dir = TestUtils::get_data_path("file_access_excl");
	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(dir.is_valid());
	dir->make_dir_recursive(base_dir);

	SUBCASE("Creates a new file when none exists") {
		const String target = base_dir.path_join("fresh.txt");
		dir->remove(target);

		Error err = ERR_BUG;
		Ref<FileAccess> f = FileAccess::open(target, FileAccess::WRITE | FileAccess::WRITE_EXCL, &err);
		REQUIRE(f.is_valid());
		CHECK(err == OK);
		f->store_string("formatted");
		f->close();

		CHECK(FileAccess::exists(target));
		CHECK(FileAccess::get_file_as_string(target) == "formatted");

		dir->remove(target);
	}

	SUBCASE("Refuses to open and never truncates an existing file") {
		const String target = base_dir.path_join("existing.txt");
		{
			Ref<FileAccess> seed = FileAccess::open(target, FileAccess::WRITE);
			REQUIRE(seed.is_valid());
			seed->store_string("original");
			seed->close();
		}

		Error err = OK;
		Ref<FileAccess> f = FileAccess::open(target, FileAccess::WRITE | FileAccess::WRITE_EXCL, &err);
		CHECK(f.is_null());
		CHECK(err != OK);
		// The pre-existing content must remain intact (no truncation).
		CHECK(FileAccess::get_file_as_string(target) == "original");

		dir->remove(target);
	}

#ifdef UNIX_ENABLED
	SUBCASE("Refuses to follow a planted symlink (TOCTOU)") {
		const String victim = base_dir.path_join("victim.txt");
		const String link = base_dir.path_join("planted_link.txt");
		{
			Ref<FileAccess> seed = FileAccess::open(victim, FileAccess::WRITE);
			REQUIRE(seed.is_valid());
			seed->store_string("victim-data");
			seed->close();
		}
		dir->remove(link);

		// Simulate an attacker planting a symlink at the temp path after the
		// existence check: the exclusive/no-follow open must refuse to write
		// through it, leaving the victim untouched.
		const CharString victim_native = victim.utf8();
		const CharString link_native = link.utf8();
		REQUIRE(symlink(victim_native.get_data(), link_native.get_data()) == 0);

		Error err = OK;
		Ref<FileAccess> f = FileAccess::open(link, FileAccess::WRITE | FileAccess::WRITE_EXCL, &err);
		CHECK(f.is_null());
		CHECK(err != OK);
		CHECK(FileAccess::get_file_as_string(victim) == "victim-data");

		dir->remove(link);
		dir->remove(victim);
	}
#endif

	dir->remove(base_dir);
}

TEST_CASE("[FileAccess] CSV read") {
	Ref<FileAccess> f = FileAccess::open(TestUtils::get_data_path("testdata.csv"), FileAccess::READ);
	REQUIRE(f.is_valid());

	Vector<String> header = f->get_csv_line(); // Default delimiter: ",".
	REQUIRE(header.size() == 4);

	Vector<String> row1 = f->get_csv_line(","); // Explicit delimiter, should be the same.
	REQUIRE(row1.size() == 4);
	CHECK(row1[0] == "GOOD_MORNING");
	CHECK(row1[1] == "Good Morning");
	CHECK(row1[2] == "Guten Morgen");
	CHECK(row1[3] == "Bonjour");

	Vector<String> row2 = f->get_csv_line();
	REQUIRE(row2.size() == 4);
	CHECK(row2[0] == "GOOD_EVENING");
	CHECK(row2[1] == "Good Evening");
	CHECK(row2[2].is_empty()); // Use case: not yet translated!
	// https://github.com/godotengine/godot/issues/44269
	CHECK_MESSAGE(row2[2] != "\"", "Should not parse empty string as a single double quote.");
	CHECK(row2[3] == "\"\""); // Intentionally testing only escaped double quotes.

	Vector<String> row3 = f->get_csv_line();
	REQUIRE(row3.size() == 6);
	CHECK(row3[0] == "Without quotes");
	CHECK(row3[1] == "With, comma");
	CHECK(row3[2] == "With \"inner\" quotes");
	CHECK(row3[3] == "With \"inner\", quotes\",\" and comma");
	CHECK(row3[4] == "With \"inner\nsplit\" quotes and\nline breaks");
	CHECK(row3[5] == "With \\nnewline chars"); // Escaped, not an actual newline.

	Vector<String> row4 = f->get_csv_line("~"); // Custom delimiter, makes inline commas easier.
	REQUIRE(row4.size() == 3);
	CHECK(row4[0] == "Some other");
	CHECK(row4[1] == "delimiter");
	CHECK(row4[2] == "should still work, shouldn't it?");

	Vector<String> row5 = f->get_csv_line("\t"); // Tab separated variables.
	REQUIRE(row5.size() == 3);
	CHECK(row5[0] == "What about");
	CHECK(row5[1] == "tab separated");
	CHECK(row5[2] == "lines, good?");
}

TEST_CASE("[FileAccess] Get as UTF-8 String") {
	SUBCASE("Newline == \\n (Unix)") {
		Ref<FileAccess> f_lf = FileAccess::open(TestUtils::get_data_path("line_endings_lf.test.txt"), FileAccess::READ);
		REQUIRE(f_lf.is_valid());
		String s_lf = f_lf->get_as_utf8_string();
		CHECK(s_lf == "Hello darkness\nMy old friend\nI've come to talk\nWith you again\n");
		f_lf->seek(0);
		CHECK(f_lf->get_line() == "Hello darkness");
		CHECK(f_lf->get_line() == "My old friend");
		CHECK(f_lf->get_line() == "I've come to talk");
		CHECK(f_lf->get_line() == "With you again");
		CHECK(f_lf->get_error() == Error::OK);
	}

	SUBCASE("Newline == \\r\\n (Windows)") {
		Ref<FileAccess> f_crlf = FileAccess::open(TestUtils::get_data_path("line_endings_crlf.test.txt"), FileAccess::READ);
		REQUIRE(f_crlf.is_valid());
		String s_crlf = f_crlf->get_as_utf8_string();
		CHECK(s_crlf == "Hello darkness\r\nMy old friend\r\nI've come to talk\r\nWith you again\r\n");
		f_crlf->seek(0);
		CHECK(f_crlf->get_line() == "Hello darkness");
		CHECK(f_crlf->get_line() == "My old friend");
		CHECK(f_crlf->get_line() == "I've come to talk");
		CHECK(f_crlf->get_line() == "With you again");
		CHECK(f_crlf->get_error() == Error::OK);
	}

	SUBCASE("Newline == \\r (Legacy macOS)") {
		Ref<FileAccess> f_cr = FileAccess::open(TestUtils::get_data_path("line_endings_cr.test.txt"), FileAccess::READ);
		REQUIRE(f_cr.is_valid());
		String s_cr = f_cr->get_as_utf8_string();
		CHECK(s_cr == "Hello darkness\rMy old friend\rI've come to talk\rWith you again\r");
		f_cr->seek(0);
		CHECK(f_cr->get_line() == "Hello darkness");
		CHECK(f_cr->get_line() == "My old friend");
		CHECK(f_cr->get_line() == "I've come to talk");
		CHECK(f_cr->get_line() == "With you again");
		CHECK(f_cr->get_error() == Error::OK);
	}

	SUBCASE("Newline == Mixed") {
		Ref<FileAccess> f_mix = FileAccess::open(TestUtils::get_data_path("line_endings_mixed.test.txt"), FileAccess::READ);
		REQUIRE(f_mix.is_valid());
		String s_mix = f_mix->get_as_utf8_string();
		CHECK(s_mix == "Hello darkness\nMy old friend\r\nI've come to talk\rWith you again");
		f_mix->seek(0);
		CHECK(f_mix->get_line() == "Hello darkness");
		CHECK(f_mix->get_line() == "My old friend");
		CHECK(f_mix->get_line() == "I've come to talk");
		CHECK(f_mix->get_line() == "With you again");
		CHECK(f_mix->get_error() == Error::ERR_FILE_EOF); // Not a bug; the file lacks a final newline.
	}
}

TEST_CASE("[FileAccess] Get/Store floating point values") {
	// BigEndian Hex: 0x40490E56
	// LittleEndian Hex: 0x560E4940
	float value = 3.1415f;

	SUBCASE("Little Endian") {
		const String file_path = TestUtils::get_data_path("floating_point_little_endian.bin");
		const String file_path_new = TestUtils::get_data_path("floating_point_little_endian_new.bin");

		Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::READ);
		REQUIRE(f.is_valid());
		CHECK_EQ(f->get_float(), value);

		Ref<FileAccess> fw = FileAccess::open(file_path_new, FileAccess::WRITE);
		REQUIRE(fw.is_valid());
		fw->store_float(value);
		fw->close();

		CHECK_EQ(FileAccess::get_sha256(file_path_new), FileAccess::get_sha256(file_path));

		DirAccess::remove_file_or_error(file_path_new);
	}

	SUBCASE("Big Endian") {
		const String file_path = TestUtils::get_data_path("floating_point_big_endian.bin");
		const String file_path_new = TestUtils::get_data_path("floating_point_big_endian_new.bin");

		Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::READ);
		REQUIRE(f.is_valid());
		f->set_big_endian(true);
		CHECK_EQ(f->get_float(), value);

		Ref<FileAccess> fw = FileAccess::open(file_path_new, FileAccess::WRITE);
		REQUIRE(fw.is_valid());
		fw->set_big_endian(true);
		fw->store_float(value);
		fw->close();

		CHECK_EQ(FileAccess::get_sha256(file_path_new), FileAccess::get_sha256(file_path));

		DirAccess::remove_file_or_error(file_path_new);
	}
}

TEST_CASE("[FileAccess] Get/Store floating point half precision values") {
	// IEEE 754 half-precision binary floating-point format:
	// sign exponent (5 bits)    fraction (10 bits)
	//  0        01101               0101010101
	// BigEndian Hex: 0x3555
	// LittleEndian Hex: 0x5535
	float value = 0.33325195f;

	SUBCASE("Little Endian") {
		const String file_path = TestUtils::get_data_path("half_precision_floating_point_little_endian.bin");
		const String file_path_new = TestUtils::get_data_path("half_precision_floating_point_little_endian_new.bin");

		Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::READ);
		REQUIRE(f.is_valid());
		CHECK_EQ(f->get_half(), value);

		Ref<FileAccess> fw = FileAccess::open(file_path_new, FileAccess::WRITE);
		REQUIRE(fw.is_valid());
		fw->store_half(value);
		fw->close();

		CHECK_EQ(FileAccess::get_sha256(file_path_new), FileAccess::get_sha256(file_path));

		DirAccess::remove_file_or_error(file_path_new);
	}

	SUBCASE("Big Endian") {
		const String file_path = TestUtils::get_data_path("half_precision_floating_point_big_endian.bin");
		const String file_path_new = TestUtils::get_data_path("half_precision_floating_point_big_endian_new.bin");

		Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::READ);
		REQUIRE(f.is_valid());
		f->set_big_endian(true);
		CHECK_EQ(f->get_half(), value);

		Ref<FileAccess> fw = FileAccess::open(file_path_new, FileAccess::WRITE);
		REQUIRE(fw.is_valid());
		fw->set_big_endian(true);
		fw->store_half(value);
		fw->close();

		CHECK_EQ(FileAccess::get_sha256(file_path_new), FileAccess::get_sha256(file_path));

		DirAccess::remove_file_or_error(file_path_new);
	}

	SUBCASE("4096 bytes fastlz compressed") {
		const String file_path = TestUtils::get_data_path("exactly_4096_bytes_fastlz.bin");

		Ref<FileAccess> f = FileAccess::open_compressed(file_path, FileAccess::READ, FileAccess::COMPRESSION_FASTLZ);
		const Vector<uint8_t> full_data = f->get_buffer(4096 * 2);
		CHECK(full_data.size() == 4096);
		CHECK(f->eof_reached());

		// Data should be empty.
		PackedByteArray reference;
		reference.resize_initialized(4096);
		CHECK(reference == full_data);

		f->seek(0);
		const Vector<uint8_t> partial_data = f->get_buffer(4095);
		CHECK(partial_data.size() == 4095);
		CHECK(!f->eof_reached());

		reference.resize_initialized(4095);
		CHECK(reference == partial_data);
	}
}

} // namespace TestFileAccess
