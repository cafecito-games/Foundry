/**************************************************************************/
/*  startup_markers_macos.h                                               */
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

#include <cstdint>

// The two endpoints of the startup interval the run loop responsiveness bound is defined over:
// from the moment the user can first see an editor window until the engine's first
// `Main::iteration()`. Everything between those is boot the user is already waiting through.
//
// These are Instruments Points of Interest signposts rather than `--benchmark-file` marks because
// the bound is about run loop *servicing*, which only `runloop-events` records — and Instruments
// puts both tables on one trace clock, so the interval and the gaps need no separate alignment.
//
// Unlike `FoundryProfileZone`, these are compiled unconditionally rather than under
// `profiler=instruments`. The acceptance capture profiles a plain `production=yes` editor with no
// `profiler=` flag, so a build-gated marker would be absent from the binary being measured. The
// cost of shipping them is a load and a branch: `os_signpost_event_emit()` checks
// `os_signpost_enabled()` first and does nothing unless a trace tool has subscribed, and each
// marker is latched to fire once per process. `drivers/metal/rendering_device_driver_metal.cpp`
// already ships unconditional Points of Interest on the same terms.
class StartupMarkersMacOS {
	static uint32_t first_window_calls;
	static uint32_t first_main_iteration_calls;

public:
	// Called from every window-ordering path; only the first call for the main window emits.
	static void first_window_visible();
	// Called on every run loop turn that iterates the engine; only the first emits.
	static void first_main_iteration();

	// The markers are one-shot: a capture that sees a marker twice cannot tell which one bounds the
	// interval, so the profiler treats duplicates as an invalid measurement rather than picking one.
	static bool has_emitted_first_window() { return first_window_calls > 0; }
	static bool has_emitted_first_main_iteration() { return first_main_iteration_calls > 0; }

	static void reset();
};
