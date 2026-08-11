/**************************************************************************/
/*  startup_markers_macos.mm                                              */
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

#import "startup_markers_macos.h"

#import <os/log.h>
#import <os/signpost.h>

uint32_t StartupMarkersMacOS::first_window_calls = 0;
uint32_t StartupMarkersMacOS::first_main_iteration_calls = 0;

namespace {

// Its own subsystem, not the engine-wide one: a Time Profiler trace also carries the Metal driver's
// Points of Interest, and the profiler selects the startup markers by subsystem so an unrelated
// signpost can never be mistaken for one.
API_AVAILABLE(macos(10.14))
os_log_t startup_log() {
	static os_log_t log = os_log_create("org.cafecito.foundry.startup", OS_LOG_CATEGORY_POINTS_OF_INTEREST);
	return log;
}

} // namespace

// Signposts arrived in macOS 10.14, and the x86_64 build still deploys to 10.13
// (`platform/macos/detect.py:90`), where the strict build promotes the resulting
// `-Wunguarded-availability-new` to an error. The markers are a measurement aid, so being absent on
// a system too old to record them costs nothing; the latches below still run either way, so the
// once-per-process contract holds identically on every deployment target.

void StartupMarkersMacOS::first_window_visible() {
	if (first_window_calls++ > 0) {
		return;
	}
	if (__builtin_available(macOS 10.14, *)) {
		os_signpost_event_emit(startup_log(), OS_SIGNPOST_ID_EXCLUSIVE, "FoundryFirstWindowVisible");
	}
}

void StartupMarkersMacOS::first_main_iteration() {
	if (first_main_iteration_calls++ > 0) {
		return;
	}
	if (__builtin_available(macOS 10.14, *)) {
		os_signpost_event_emit(startup_log(), OS_SIGNPOST_ID_EXCLUSIVE, "FoundryFirstMainIteration");
	}
}

void StartupMarkersMacOS::reset() {
	first_window_calls = 0;
	first_main_iteration_calls = 0;
}
