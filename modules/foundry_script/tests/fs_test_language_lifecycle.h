/**************************************************************************/
/*  fs_test_language_lifecycle.h                                          */
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

#include "../foundry_script.h"
#include "../fs_reflection.h"

namespace FSTests {

// Brings the FoundryScript language up for a test that needs a usable language, and reports
// whether this call is the one that initialized it, so a fixture can tear down only the state it
// created.
//
// `FSLanguage::finish()` releases the `foundry` reflection namespace but leaves the ordinary
// global constants populated, so the presence of a leftover global such as `RefCounted` says
// nothing about whether the language is usable. The reflection namespace singleton exists exactly
// between `init()` and `finish()`, which makes it the only lifecycle signal a gate may read. The
// singleton is released before `finish()` reaches any of its later failure points, so a
// half-torn-down language re-initializes here instead of being treated as live.
inline bool ensure_fs_language_initialized() {
	if (FSLanguage::get_singleton()->get_namespace_singleton().is_valid()) {
		return false;
	}
	FSLanguage::get_singleton()->init();
	return true;
}

} // namespace FSTests
