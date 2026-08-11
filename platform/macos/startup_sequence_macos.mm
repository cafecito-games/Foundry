/**************************************************************************/
/*  startup_sequence_macos.mm                                             */
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

#import "startup_sequence_macos.h"

#import "core/error/error_macros.h"
#import "core/os/main_loop.h"
#import "core/os/os.h"
#import "core/string/ustring.h"
#import "core/templates/local_vector.h"
#import "main/main.h"

#import <AppKit/AppKit.h>

#import <cstdlib>

bool StartupInputGateMacOS::suppressed = false;

bool StartupInputGateMacOS::is_user_input_event_type(unsigned long p_ns_event_type) {
	switch ((NSEventType)p_ns_event_type) {
		case NSEventTypeLeftMouseDown:
		case NSEventTypeLeftMouseUp:
		case NSEventTypeRightMouseDown:
		case NSEventTypeRightMouseUp:
		case NSEventTypeOtherMouseDown:
		case NSEventTypeOtherMouseUp:
		case NSEventTypeMouseMoved:
		case NSEventTypeLeftMouseDragged:
		case NSEventTypeRightMouseDragged:
		case NSEventTypeOtherMouseDragged:
		case NSEventTypeMouseEntered:
		case NSEventTypeMouseExited:
		case NSEventTypeScrollWheel:
		case NSEventTypeKeyDown:
		case NSEventTypeKeyUp:
		case NSEventTypeFlagsChanged:
		case NSEventTypeTabletPoint:
		case NSEventTypeTabletProximity:
		case NSEventTypeGesture:
		case NSEventTypeMagnify:
		case NSEventTypeSwipe:
		case NSEventTypeRotate:
		case NSEventTypeBeginGesture:
		case NSEventTypeEndGesture:
		case NSEventTypeSmartMagnify:
		case NSEventTypeQuickLook:
		case NSEventTypePressure:
		case NSEventTypeDirectTouch:
		case NSEventTypeChangeMode:
			return true;
		default:
			return false;
	}
}

bool StartupBootGateMacOS::engaged = false;

namespace {

struct PendingWindow {
	int64_t window_id = 0;
	uint32_t dirty = 0;
	bool focused = false;
	bool mouse_inside = false;
};

// Boot touches a handful of windows at most, so a flat vector beats a hash map and keeps replay in
// a deterministic order: the order the windows were first seen.
LocalVector<PendingWindow> &pending_windows() {
	static LocalVector<PendingWindow> windows;
	return windows;
}

PendingWindow *find_or_add(int64_t p_window_id) {
	LocalVector<PendingWindow> &windows = pending_windows();
	for (PendingWindow &window : windows) {
		if (window.window_id == p_window_id) {
			return &window;
		}
	}
	PendingWindow added;
	added.window_id = p_window_id;
	windows.push_back(added);
	return &windows[windows.size() - 1];
}

} // namespace

void StartupBootGateMacOS::engage() {
	engaged = true;
	pending_windows().clear();
}

bool StartupBootGateMacOS::defer_window_state(int64_t p_window_id, WindowState p_state, bool p_value) {
	if (!engaged) {
		return false;
	}
	ERR_FAIL_INDEX_V(p_state, WINDOW_STATE_MAX, false);
	PendingWindow *window = find_or_add(p_window_id);
	window->dirty |= 1u << (uint32_t)p_state;
	if (p_state == WINDOW_STATE_FOCUS) {
		window->focused = p_value;
	} else if (p_state == WINDOW_STATE_MOUSE) {
		window->mouse_inside = p_value;
	}
	return true;
}

bool StartupBootGateMacOS::has_pending_state() {
	for (const PendingWindow &window : pending_windows()) {
		if (window.dirty != 0) {
			return true;
		}
	}
	return false;
}

void StartupBootGateMacOS::release_and_replay(ReplayCallback p_replay, void *p_userdata) {
	// Disengage first: a replayed callback may re-enter the display server and ask it about window
	// state, and that read must see the running editor rather than a gate still claiming to boot.
	engaged = false;

	LocalVector<PendingWindow> windows = pending_windows();
	pending_windows().clear();
	if (p_replay == nullptr) {
		return;
	}
	for (const PendingWindow &window : windows) {
		// Fixed order rather than the order the changes arrived: geometry before appearance before
		// focus, so the tree sees a window settled into its final shape before it is told it is
		// focused. Focus last is what the editor reacts to most visibly.
		static const WindowState replay_order[] = {
			WINDOW_STATE_RECT,
			WINDOW_STATE_DPI,
			WINDOW_STATE_TITLEBAR,
			WINDOW_STATE_MOUSE,
			WINDOW_STATE_FOCUS,
		};
		for (const WindowState state : replay_order) {
			if ((window.dirty & (1u << (uint32_t)state)) == 0) {
				continue;
			}
			bool value = false;
			if (state == WINDOW_STATE_FOCUS) {
				value = window.focused;
			} else if (state == WINDOW_STATE_MOUSE) {
				value = window.mouse_inside;
			}
			p_replay(window.window_id, state, value, p_userdata);
		}
	}
}

void StartupBootGateMacOS::abandon() {
	engaged = false;
	pending_windows().clear();
}

void StartupBootGateMacOS::reset() {
	engaged = false;
	pending_windows().clear();
}

void StartupSequenceMacOS::begin() {
	StartupInputGateMacOS::set_suppressed(true);
	StartupBootGateMacOS::engage();
}

StartupSequenceMacOS::StepResult StartupSequenceMacOS::step() {
	if (stepping) {
		// Re-entered from a nested run loop while a phase is still running; see the header.
		return STEP_PENDING;
	}
	if (phase == PHASE_FAILED) {
		return STEP_EXIT_FAILURE;
	}
	stepping = true;
	StepResult result = STEP_PENDING;
	@try {
		result = _run_phase();
	} @catch (NSException *exception) {
		// Defense in depth. An `NSException` raised below a C++ frame cannot reach here — those
		// frames are compiled `-fno-exceptions` and abort the process at that boundary — but a
		// raise from Objective-C called directly by a phase body does, and retrying a phase that
		// stopped part way through would repeat whatever it managed to do first.
		ERR_PRINT("Startup phase failed with NSException: " + String::utf8([exception reason].UTF8String));
		_fail();
		result = STEP_EXIT_FAILURE;
	} @finally {
		// `@finally` rather than a scope guard: an `NSException` raised by a boot phase unwinds
		// without running C++ destructors in this translation unit, and the observer catches it.
		// A guard left standing would make every later turn a no-op and wedge boot for good.
		stepping = false;
	}
	return result;
}

StartupSequenceMacOS::StepResult StartupSequenceMacOS::_run_phase() {
	switch (phase) {
		case PHASE_MAIN_START: {
			const int result = _main_start();
			if (phase == PHASE_FAILED) {
				// The phase abandoned the boot part way through; advancing would hide that.
				return STEP_EXIT_FAILURE;
			}
			if (result != EXIT_SUCCESS) {
				return STEP_EXIT_FAILURE;
			}
			if (!_has_main_loop()) {
				// `Main::start()` ran a command line tool to completion; there is nothing to run.
				return STEP_EXIT_SUCCESS;
			}
			phase = PHASE_MAIN_LOOP_INITIALIZE;
			return STEP_PENDING;
		}
		case PHASE_MAIN_LOOP_INITIALIZE: {
			_main_loop_initialize();
			if (phase == PHASE_FAILED) {
				return STEP_EXIT_FAILURE;
			}
			phase = PHASE_RUNNING;
			if (boot_complete_callback) {
				// Before the input gate is lifted; see `set_boot_complete_callback()`.
				boot_complete_callback(boot_complete_userdata);
			}
			StartupInputGateMacOS::set_suppressed(false);
			return STEP_RUNNING;
		}
		case PHASE_RUNNING:
			break;
		case PHASE_FAILED:
			return STEP_EXIT_FAILURE;
	}
	return STEP_RUNNING;
}

void StartupSequenceMacOS::set_boot_complete_callback(BootCompleteCallback p_callback, void *p_userdata) {
	boot_complete_callback = p_callback;
	boot_complete_userdata = p_userdata;
}

void StartupSequenceMacOS::_fail() {
	phase = PHASE_FAILED;
	// The tree that would have received the coalesced window state does not exist. Dropping the
	// state also disengages the gate, so a shutdown dialog is not gated behind a boot that ended.
	StartupBootGateMacOS::abandon();
}

int StartupSequenceMacOS::_main_start() {
	int ret;
	@autoreleasepool {
		ret = Main::start();
	}
	return ret;
}

bool StartupSequenceMacOS::_has_main_loop() const {
	return OS::get_singleton()->get_main_loop() != nullptr;
}

void StartupSequenceMacOS::_main_loop_initialize() {
	MainLoop *main_loop = OS::get_singleton()->get_main_loop();
	ERR_FAIL_NULL(main_loop);
	// `main_loop->initialize()` is the single most expensive boot phase and is invisible to
	// `--benchmark-file` without this mark.
	OS::get_singleton()->benchmark_begin_measure("Startup", "Main Loop Initialize");
	@autoreleasepool {
		main_loop->initialize();
	}
	OS::get_singleton()->benchmark_end_measure("Startup", "Main Loop Initialize");
}
