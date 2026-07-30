/**************************************************************************/
/*  zip_io.h                                                              */
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

// Not directly used in this header, but assumed available in downstream users
// like platform/*/export/export.cpp. Could be fixed, but probably better to have
// thirdparty includes in as little headers as possible.
#include "thirdparty/minizip/unzip.h"
#include "thirdparty/minizip/zip.h"

// Get the current file info and safely convert the full filepath to a String.
int foundry_unzip_get_current_file_info(unzFile p_zip_file, unz_file_info64 &r_file_info, String &r_filepath);
// Try to locate the file in the archive specified by the filepath (works with large paths and Unicode).
int foundry_unzip_locate_file(unzFile p_zip_file, const String &p_filepath, bool p_case_sensitive = true);

//

void *zipio_open(voidpf opaque, const char *p_fname, int mode);
uLong zipio_read(voidpf opaque, voidpf stream, void *buf, uLong size);
uLong zipio_write(voidpf opaque, voidpf stream, const void *buf, uLong size);

long zipio_tell(voidpf opaque, voidpf stream);
long zipio_seek(voidpf opaque, voidpf stream, uLong offset, int origin);

int zipio_close(voidpf opaque, voidpf stream);

int zipio_testerror(voidpf opaque, voidpf stream);

voidpf zipio_alloc(voidpf opaque, uInt items, uInt size);
void zipio_free(voidpf opaque, voidpf address);

zlib_filefunc_def zipio_create_io(Ref<FileAccess> *p_data);

// Owns a minizip handle for the duration of a scope.
//
// Writers in particular have to be closed: minizip only writes an archive's
// central directory on close, so an archive abandoned by an early return is
// both a leak and an unreadable file. Functions that open an archive and then
// have several failure exits are the usual place this goes wrong, so let the
// scope own the handle instead of repeating the close on every path.
//
// Closing is idempotent: `close()` may be called explicitly when ordering
// matters (for instance before reopening the same path for reading), and the
// destructor will not close a second time. Constructing from `nullptr` is
// allowed so the result of a failed open can be adopted unconditionally.
template <typename T, void (*CloseFunction)(T)>
class ZipHandleGuard {
	T handle = nullptr;

public:
	ZipHandleGuard(const ZipHandleGuard &) = delete;
	ZipHandleGuard &operator=(const ZipHandleGuard &) = delete;

	explicit ZipHandleGuard(T p_handle) :
			handle(p_handle) {}

	~ZipHandleGuard() { close(); }

	bool is_valid() const { return handle != nullptr; }
	T get() const { return handle; }

	void close() {
		if (handle) {
			// Clear first: a closer that re-enters must not see a dangling handle.
			T closing = handle;
			handle = nullptr;
			CloseFunction(closing);
		}
	}

	// Gives up ownership without closing, for the rare caller that has to hand
	// the handle to something else.
	T release() {
		T released = handle;
		handle = nullptr;
		return released;
	}
};

inline void zipio_close_write_handle(zipFile p_handle) {
	zipClose(p_handle, nullptr);
}

inline void zipio_close_read_handle(unzFile p_handle) {
	unzClose(p_handle);
}

using ZipFileGuard = ZipHandleGuard<zipFile, zipio_close_write_handle>;
using UnzFileGuard = ZipHandleGuard<unzFile, zipio_close_read_handle>;
