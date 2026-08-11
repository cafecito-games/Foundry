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

#import "core/os/main_loop.h"
#import "core/os/os.h"
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

void StartupSequenceMacOS::begin() {
	StartupInputGateMacOS::set_suppressed(true);
}

StartupSequenceMacOS::StepResult StartupSequenceMacOS::step() {
	switch (phase) {
		case PHASE_MAIN_START: {
			if (_main_start() != EXIT_SUCCESS) {
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
			phase = PHASE_RUNNING;
			StartupInputGateMacOS::set_suppressed(false);
			return STEP_RUNNING;
		}
		case PHASE_RUNNING:
			break;
	}
	return STEP_RUNNING;
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
